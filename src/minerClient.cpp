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
      auto& client = _clients[c];
      client._pool = new Pool(_username, MINING_KEY, DEVICE_AVR);
      client._address = _i2c->getFoundSlaveAddress(c);
      client._pool->setMinerName(String("AVRSlave") + String(_clients[c]._address, HEX));
      client._pool->addEventListener(&MinerClient::_poolEventSink, &_clients[c] );
      client.startTimeMs = millis();  // TODO take into account connect to pool time

      // Delay a bit between slave by a jitter amount
      delay(48);
      if(c%2) delay(16);
    }
  }
  return true;
}

void MinerClient::loop() {
  if(_reportTimer.shouldRun()) {
    _printReport();
  }

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
          uint16_t timeTaken;
          if(_i2c->getJobResult(_clients[c]._address, found_nonce, timeTaken)) {
            if(found_nonce == 0) {
              // although work finished, error. Probably start diff too high
              _setState(DUINO_STATE_NONE, c); // stop while we test
              break;
            }
            uint16_t masterTimeTakenMs = millis() - client._jobStartTime;
            DEBUGPRINT("[MINER_CLIENT] i2c slave solved hash in ");
            DEBUGPRINT(timeTaken);
            DEBUGPRINT("ms. Master Estimate: ");
            DEBUGPRINT_LN(masterTimeTakenMs);

            // Update stats
            client.stats_share_count++;
            client.lastNonce = found_nonce;
            client.lastTimeTakenMs = masterTimeTakenMs;
            client.lastHashRate = found_nonce / (masterTimeTakenMs * 0.001f);

            client._pool->submitJob(found_nonce, masterTimeTakenMs * 1000);

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

void static _printMinerPrefix(uint16_t address, bool isDebug) {
  if(isDebug) {
    DEBUGPRINT("[MINER CLIENT] 0x");
    DEBUGPRINT_HEX(address);
    DEBUGPRINT(" - ");
  }
  else {
    SERIALPRINT("[MINER CLIENT] 0x");
    SERIALPRINT_HEX(address);
    SERIALPRINT(" - ");
  }
}

void MinerClient::_poolEventSink(PoolEvent ev, const PoolEventData& d, void *user) {
  ClientStruct* client = static_cast<ClientStruct*>(user);

  switch (ev)
  {
  case POOLEVT_CONNECTED:
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("POOLEVT_CONNECTED");
    DEBUGPRINT_LN(d.text);
    break;
  case POOLEVT_DISCONNECTED:     // payload: text = reason
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("POOLEVT_DISCONNECTED");
    break;
  case POOLEVT_MOTD:             // payload: text = motd
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("POOLEVT_MOTD");
    DEBUGPRINT_LN(d.text);
    break;
  case POOLEVT_JOB_REQUESTED:
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("POOLEVT_REQUESTED");
    break;
  case POOLEVT_JOB_RECEIVED:     // payload: seed40 / target40 / diff
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("POOLEVT_JOB_RECEIVED - ");
    DEBUGPRINT(d.jobDataPtr->prevHash);
    DEBUGPRINT(" | ");
    DEBUGPRINT(d.jobDataPtr->expectedHash);
    DEBUGPRINT(" | ");
    DEBUGPRINT_LN(d.jobDataPtr->difficulty);
    break;
  case POOLEVT_RESULT_GOOD:
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("Share accepted");
    client->stats_good_count++;
    if(client->_address == 0) {
      blinkStatus(BLINK_SHARE_GOOD);
    }
    else {
      blinkStatus(BLINK_SLAVE_SHARE_GOOD);
    }
    break;
  case POOLEVT_RESULT_BAD:
    _printMinerPrefix(client->_address, false);
    SERIALPRINT("Share rejected: ");
    SERIALPRINT(d.text ? d.text : "");
    SERIALPRINT("  Diff: ");
    SERIALPRINT(client->diff);
    SERIALPRINT("  Last Nonce: ");
    SERIALPRINT(client->lastNonce);
    SERIALPRINT(".  Time: ");
    SERIALPRINT( client->lastTimeTakenMs );
    SERIALPRINT("ms");
    SERIALPRINT("  HR: ");
    SERIALPRINT(client->lastHashRate);
    //SERIALPRINT( client->lastNonce / (client->lastTimeTakenMs * 0.001f) );
    SERIALPRINT_LN("");

    client->stats_bad_count++;
    if( client->lastHashRate > client->highestHashWithError)
      client->highestHashWithError = client->lastHashRate;
    if( client->lastHashRate < client->lowestHashWithError)
      client->lowestHashWithError = client->lastHashRate;

    blinkStatus(BLINK_SHARE_ERROR);
    break;
  case POOLEVT_RESULT_BLOCK:
    _printMinerPrefix(client->_address, true);
    DEBUGPRINT_LN("Found a BLOCK ... Whoa!");
    client->stats_block_count++;
    blinkStatus(BLINK_SHARE_BLOCKFOUND);
    break;
  case POOLEVT_ERROR:
    _printMinerPrefix(client->_address, false);
    SERIALPRINT("POOLEVT_ERROR: ");
    SERIALPRINT_LN(d.text);
    break;

  default:
    break;
  }

}

bool MinerClient::_solveAndSubmit(const char *seed40, const char *target40, uint32_t diff) {
  uint32_t found_nonce = 0;
  uint32_t elapsed_time = 0;

  if( this->findNonce(seed40, target40, diff, found_nonce, elapsed_time) ) {
    float elapsed_time_s = elapsed_time * .000001f;
    _masterLastHashedPerSec = (found_nonce / elapsed_time_s) * 1;
    _masterLastHashrateKhs = _masterLastHashedPerSec / 1000.0f;
  }
  else {
    _emit_nodata(ME_SOLVE_FAILED);
    return false;
  }

  MinerEventData solved;
  solved.nonce = found_nonce;
  solved.hashrate_khs = _masterLastHashrateKhs;
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

void MinerClient::_printReport() {
  char buf[64];
  u_int32_t
    total_share_count=0,
    total_good_count=0,
    total_bad_count=0,
    total_block_count=0;

  SERIALPRINT_LN(F("************ REPORT ************"));
  SERIALPRINT("FreeRam: ");
  SERIALPRINT_LN(ESP.getFreeHeap());

  SERIALPRINT_LN(F("Addr     Count     Good      Bad  Block   Uptime  Shrs/min"));
  for(int c=0; c < _numMinerClients; c++) {
    auto const client = _clients[c];

    total_share_count += client.stats_share_count;
    total_good_count += client.stats_good_count;
    total_bad_count += client.stats_bad_count;
    total_block_count += client.stats_block_count;

    uint32_t uptimeSecs = (millis() - client.startTimeMs) / 1000;
    float sharesPerMin = (float)client.stats_good_count / ((uptimeSecs<1) ? 1 : (uptimeSecs / 60));

    snprintf(buf, 64, "%#x  %8u %8u %8u %6u %5u:%02d %7.3f",
    client._address,
    client.stats_share_count,
    client.stats_good_count,
    client.stats_bad_count,
    client.stats_block_count,
    uptimeSecs/60,
    uptimeSecs%60,
    sharesPerMin
    );
    SERIALPRINT_LN(buf);

    // SERIALPRINT("Highest / Lowest Error hash rates...   ");
    // SERIALPRINT("Highest: ");
    // SERIALPRINT(client.highestHashWithError);
    // SERIALPRINT("  Lowest: ");
    // SERIALPRINT_LN(client.lowestHashWithError);
  }

  snprintf(buf, 64, "Total %8u %8u %8u %6u",
    total_share_count, total_good_count,
    total_bad_count, total_block_count);
  SERIALPRINT_LN(buf);

  SERIALPRINT_LN("");
}
