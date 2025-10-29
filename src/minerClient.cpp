#include "minerClient.h"
#include "config.h"
#include "utils.h"
#include "pool.h"
#include "I2CMaster.h"
//#include "wirewrap.h"
#include "network_services.h"
#include "Counter.h"
#include "DSHA1.h"
#include "led.h"

#include <Arduino.h>
#include <WiFiClient.h>

#define CLIENT_TIMEOUT_CONNECTION 30000
#define STATE_STUCK_TIMEOUT 30000UL

// ---------------- ctor/config ----------------
// Master / Slave flag must be set in ctor as not mutable
MinerClient::MinerClient(const String username, bool isMaster)
  : _username(username) {
    _isMasterMiner = isMaster;
    init();
  }

void MinerClient::init() {
  if(_isMasterMiner) {
    _numMinerClients = 1;
    _clients[0]._pool = new Pool(_username, MINING_KEY, DEVICE_ESP32);
    _clients[0]._pool->setMinerName("NDMaster");
    // Only for masters
    _dsha1 = new DSHA1();
    _dsha1->warmup();
  }
  else {
    _i2c = new I2CMaster();
  }
}

Pool* MinerClient::getAttachedPool(int idx) {
  if(_isMasterMiner && idx != 0) {
    MinerEventData ed;
    ed.text = "Master miner can only have one client, an index > 0 was passed";
    _emit(ME_ERROR, ed);
    return nullptr;
  }

  return _clients[idx]._pool;
}

void MinerClient::onEvent(MinerEventCallback cb) {
  _cb = cb;
}

void MinerClient::reset() {
  for(uint8_t c=0; c < _numMinerClients; c++) {
    _setState(DUINO_STATE_NONE, c);
  }
}

void MinerClient::setMining(bool flag) {
  _isMining = flag;

    for(uint8_t c=0; c < _numMinerClients; c++) {
      if(flag == false) {
        _clients[c]._pool->disconnect();
      }
      
      // Set state to none in both cases
      _setState(DUINO_STATE_NONE, c);
    }
}

// Setup our slave devices
// This might get rolled into the start-up code at some point
// for the moment leave as function to call separately 
bool MinerClient::setupSlaves() {
  assert(!_isMasterMiner);

  if(_i2c == nullptr) {
    _i2c = new I2CMaster();
  }

  _i2c->scan(true);
  _numMinerClients = _i2c->getFoundSlaveCount(); 
  if(_numMinerClients > 0) {
    _i2c->dumpSlaves();
    // setup a client for each slave as each one needs it's own pool connection etc
    for(uint8_t c = 0; c < _numMinerClients; c++) {
      _clients[c]._pool = new Pool(_username, MINING_KEY, DEVICE_AVR);
      _clients[c]._address = _i2c->getFoundSlaveAddress(c);
      _clients[c]._pool->setMinerName(String("AVRSlave") + String(_clients[c]._address, HEX));
    }
  }
  return true;
}

void MinerClient::loop() {
  for(u_int8_t c = 0; c < _numMinerClients; c++) {
    auto& client = _clients[c];

    // If we're mining always check if we're still connected, if not then
    // any work will be lost and force new pool connection
    if(_isMining && !client._pool->isConnected() ) {
      client._pool->connect();
      _setState(DUINO_STATE_IDLE, c);     // can't mine if pool not connected
    }

    client._pool->loop();

    switch (client._state) {
      case DUINO_STATE_NONE:
        // NO-OP
        break;

      case DUINO_STATE_IDLE:
        if(_isMining && client._pool->isConnected()) {
          _setState(DUINO_STATE_JOB_REQUEST, c);
        }
        break;

      case DUINO_STATE_JOB_REQUEST:
        if(!_isMining) {
          return;
        }

        if(client.slaveJobReqTimer.shouldRun()) {
          if(client._pool->requestJob()) {
            _setState(DUINO_STATE_JOB_WAIT, c);
          }
        }
        break;

      case DUINO_STATE_JOB_WAIT: {
        Job* job = client._pool->getJob();
        if(job != nullptr) {
          strncpy(client.seed, job->prevHash.c_str(), 40);
          client.seed[40] = '\0';
          strncpy(client.target, job->expectedHash.c_str(), 40);
          client.target[40] = '\0';
          client.diff = job->difficulty;
          _setState(DUINO_STATE_MINING, c);
          }
        break;
        }

      case DUINO_STATE_MINING:
        {
          if(!_isMining)
            return;

          if(_isMasterMiner) {
            if (_solveAndSubmit(client.seed, client.target, client.diff * 100 + 1)) {
              _setState(DUINO_STATE_SHARE_SUBMITTED, c);
              // Update stats
              client.stats_share_count++;
            } else {
              _setState(DUINO_STATE_JOB_REQUEST, c);  // start again
            }
          }
          else {
            // Need to send to worker slave device
            _i2c->sendJobData(_clients[c]._address, client.seed, client.target, (uint8_t)client.diff);
            client._jobStartTime = millis();
            _setState(DUINO_STATE_MINING_I2C, c);
          }
          break;
        }

      case DUINO_STATE_MINING_I2C:
        // test if job solved
        if(client.slaveMiningStatusTimer.shouldRun()) {
          uint16_t found_nonce;
          uint8_t timeTaken;
          if(_i2c->getJobStatus(_clients[c]._address, found_nonce, timeTaken)) {
            if(found_nonce == 0) {
              // although work finished, error. Probably start diff too high
              _setState(DUINO_STATE_NONE, c); // stop while we test
              break;
            }
            uint16_t masterTimeTaken = millis() - client._jobStartTime;
            DEBUGPRINT("[MINER_CLIENT] i2c slave solved hash in ");
            DEBUGPRINT(timeTaken);
            DEBUGPRINT("ms. Master Estimate: ");
            DEBUGPRINT_LN(masterTimeTaken);

            // Update stats
            client.stats_share_count++;

            client._pool->submitJob(found_nonce, masterTimeTaken*1000);

            // Reset the slave ready to receive data, also doesn't then respond as data available
            _i2c->sendDataBegin(_clients[c]._address);
            blinkStatus(BLINK_SHARE_GOOD);
            _setState(DUINO_STATE_JOB_REQUEST, c);  // start again
          }
        }
        break;

      case DUINO_STATE_SHARE_SUBMITTED:    
        // Get a new job, regardless of the result
        _setState(DUINO_STATE_JOB_REQUEST, c);
        break;

      default:
        DEBUGPRINT_LN("[MINER_CLIENT] Unknown State");
        break;
      }
    }
}

/*
  Find the nonce
  diff = the job diff * 100 + 1
*/
bool MinerClient::findNonce(const char *seed40, const char *target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us)
{
uint8_t __hashArray[20];
uint8_t __expected_hash[20];

  hexStringToUint8Array(target40, __expected_hash, 20);
  _dsha1->reset().write( (const unsigned char *)seed40, 40);

  const uint32_t start_time = micros();
  _max_micros_elapsed(start_time, 0);

  for (Counter<10> counter; counter < diff; ++counter) {
    DSHA1 ctx = *_dsha1;
    ctx.write((const unsigned char *)counter.c_str(), counter.strlen()).finalize(__hashArray);

    // 10ms for esp32 looks like the lowest value without false watchdog triggers
    // if (_max_micros_elapsed(micros(), 100000)) {
    //     _handleSystemEvents();
    // } 

    if (memcmp( __expected_hash, __hashArray, 20) == 0) {
        elapsed_time_us = micros() - start_time;
        nonce_found = counter;
        return true;
    }
  }
  return false;
}

// -----------------------------------------------
//               ------ PRIVATE -------
// -----------------------------------------------

void MinerClient::_setState(DUINO_STATE state, int idx) {
  assert(idx < _numMinerClients);

  _clients[idx]._state = state;
  _clients[idx]._stateStartMS = (state == DUINO_STATE_NONE) ? 0 : millis();
}

bool MinerClient::_isStateStuck(int idx) {
  assert(idx < _numMinerClients);

  if (_clients[idx]._state == DUINO_STATE_IDLE
    || _clients[idx]._state == DUINO_STATE_NONE)
  {
      return false;
  }
  
  return (millis() - _clients[idx]._stateStartMS > STATE_STUCK_TIMEOUT) ? true : false;
}

bool MinerClient::_solveAndSubmit(const char *seed40, const char *target40, uint32_t diff) {
  uint32_t found_nonce = 0;
  uint32_t elapsed_time = 0;

  if( this->findNonce(seed40, target40, diff, found_nonce, elapsed_time) ) {
    float elapsed_time_s = elapsed_time * .000001f;
    _last_hashed_per_sec = (found_nonce / elapsed_time_s) * 1;
    _last_hashrate_khs = _last_hashed_per_sec / 1000.0f;
  }
  else {
    _emit_nodata(ME_SOLVE_FAILED);
    return false;
  }

  MinerEventData solved;
  solved.nonce = found_nonce;
  solved.hashrate_khs = _last_hashrate_khs;
  _emit(ME_SOLVED, solved);

  return _clients[0]._pool->submitJob(found_nonce, elapsed_time);
}

bool MinerClient::_max_micros_elapsed(unsigned long current, unsigned long max_elapsed) {
  static unsigned long _start = 0;

  if ((current - _start) > max_elapsed) {
      _start = current;
      return true;
  }
  return false;
}

void MinerClient::_handleSystemEvents() {
  delay(10); // Required vTaskDelay by ESP-IDF
  yield();

  // TODO: implement when / if OTA implemented ArduinoOTA.handle();
}
