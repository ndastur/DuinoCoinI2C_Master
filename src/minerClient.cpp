#include "minerClient.h"
#include "config.h"
#include "utils.h"
#include "pool.h"
#include "wirewrap.h"
#include "network_services.h"
#include "Counter.h"
#include "DSHA1.h"

#include <Arduino.h>
#include <WiFiClient.h>

#define CLIENT_TIMEOUT_CONNECTION 30000
#define STATE_STUCK_TIMEOUT 30000UL

// ---------------- ctor/config ----------------
MinerClient::MinerClient(const String username, DeviceType deviceType)
  : _username(username) {
    _pool = new Pool(username, MINING_KEY, deviceType);
    init();
  }

void MinerClient::init() {
  _dsha1 = new DSHA1();
  _dsha1->warmup();
}

void MinerClient::setMasterMiner(bool flag = true) {
  _isMasterMiner = flag;
}

Pool* MinerClient::getAttachedPool() {
  return _pool;
}

void MinerClient::onEvent(MinerEventCallback cb) {
  _cb = cb;
}

void MinerClient::reset() {
  _setState(DUINO_STATE_NONE);
  _share_count = 0;
  _accepted_count = 0;
  _block_count = 0;
  _last_share_count = 0;
  _startTime = millis();
  _poolConnectTime = 0;
}

void MinerClient::setMining(bool flag) {
  _isMining = flag;
  if(flag == false) {
    _setState(DUINO_STATE_NONE);
  }
  _pool->disconnect();
}

// Set a name for the miner, this helps id worker in the wallet UI
void MinerClient::setMinerName(const String& name) {
  _pool->setMinerName(name);
}

void MinerClient::loop() {
  // If we're mining always check if we're still connected, if not then
  // any work will be lost and force new pool connection
  if(_isMining && !_pool->isConnected() ) {
    _pool->connect();
    _setState(DUINO_STATE_IDLE);     // can't mine if pool not connected
  }

  _pool->loop();
  
  switch (_state) {
    case DUINO_STATE_IDLE:
      if(_isMining && _pool->isConnected()) {
        _setState(DUINO_STATE_JOB_REQUEST);
      }
      break;

    case DUINO_STATE_JOB_REQUEST:
      if(!_isMining) {
        return;
      }
      if(_pool->requestJob()) {
        _setState(DUINO_STATE_JOB_WAIT);
      }
      break;

    case DUINO_STATE_JOB_WAIT: {
      Job* job = _pool->getJob();
      if(job != nullptr) {
        _seed = job->prevHash;
        _target = job->expectedHash;
        _diff = job->difficulty;
        _setState(DUINO_STATE_MINING);
        }
      }
      break;

    case DUINO_STATE_MINING:
      if(!_isMining)
        return;

      // // Solve + submit, then immediately wait for the text result
      // _setState(DUINO_STATE_JOB_DONE_SEND);

      if (this->_solveAndSubmit(_seed, _target, _diff * 100 + 1)) {
        _setState(DUINO_STATE_SHARE_SUBMITTED);
      } else {
        _setState(DUINO_STATE_JOB_REQUEST);  // start again
      }

      break;

    case DUINO_STATE_SHARE_SUBMITTED:    
      // Get a new job, regardless of the result
      _setState(DUINO_STATE_JOB_REQUEST);
      break;

    default:
      SERIALPRINT_LN("[MINER_CLIENT] Unknown State");
      break;
    }
}

/*
  Find the nonce
  diff = the job diff * 100 + 1
*/
bool MinerClient::findNonce(const String& seed40, const String& target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us)
{
uint8_t __hashArray[20];
uint8_t __expected_hash[20];

  hexStringToUint8Array(target40.c_str(), __expected_hash, 20);
  _dsha1->reset().write((const unsigned char *)seed40.c_str(), seed40.length());

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

void MinerClient::_setState(DUINO_STATE state) {
  _state = state;
  _stateStartMS = (state == DUINO_STATE_NONE) ? 0 : millis();
}

bool MinerClient::_is_state_stuck() {
  if (_state == DUINO_STATE_IDLE
    || _state == DUINO_STATE_NONE)
  {
      return false;
  }
  
  return (millis() - _stateStartMS > STATE_STUCK_TIMEOUT) ? true : false;
}

bool MinerClient::_solveAndSubmit(const String& seed40, const String& target40, uint32_t diff) {
  uint32_t found_nonce = 0;
  uint32_t elapsed_time = 0;

  #if defined(SERIAL_PRINT)
    Serial.print("Solving: ");
    Serial.println(seed40 + " " + target40 + " " + String(diff));
  #endif

  if( this->findNonce(seed40, target40, diff, found_nonce, elapsed_time) ) {
    float elapsed_time_s = elapsed_time * .000001f;
    _last_hashed_per_sec = (found_nonce / elapsed_time_s) * 1;
    _last_hashrate_khs = _last_hashed_per_sec / 1000.0f;
    _share_count++;
  }
  else {
    _emit_nodata(ME_SOLVE_FAILED);
    return false;
  }

  MinerEventData solved;
  solved.nonce = found_nonce;
  solved.hashrate_khs = _last_hashrate_khs;
  _emit(ME_SOLVED, solved);

  return _pool->submitJob(found_nonce, elapsed_time);
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
