#include "miner_client.h"
#include "config.h"
#include "pool.h"
#include "wirewrap.h"
#include "network_services.h"
#include "Counter.h"
#include "DSHA1.h"

#include <Arduino.h>
#include <WiFiClient.h>

// ---------------- ctor/config ----------------
MinerClient::MinerClient(const String& username, String name)
  : _username(username), _miner_name(name) {
    init();
  }

MinerClient::MinerClient(
  String host,
  int port,
  const String& username,
  String name)
  : _host(host), _port(port), _username(username), _miner_name(name) {
    init();
  }

void MinerClient::init() {
  _dsha1 = new DSHA1();
  _dsha1->warmup();
}

void MinerClient::setMasterMiner(bool flag = true) {
  _isMasterMiner = flag;
  if(flag) {
    _start_diff = "ESP32S";
  }
}

void MinerClient::setPool(const String& host, int port) {
  _host = host; _port = port;
}

void MinerClient::onEvent(MinerEventCallback cb, void* user_ctx) {
  _cb = cb; _cb_user = user_ctx;
}

bool MinerClient::connect() {
  return _connectToPool();
}

bool MinerClient::isConnected() {
  return _client.connected();
}

void MinerClient::reset() {
  _set_state(DUINO_STATE_NONE);
  _poolMOTD = "";
  _poolVersion = "";
  _share_count = 0;
  _accepted_count = 0;
  _block_count = 0;
  _last_share_count = 0;
  _startTime = millis();
  _poolConnectTime = 0;
}

void MinerClient::setMining(bool flag) {
  _is_mining = flag;
}

void MinerClient::stop_mining() {
  _is_mining = false;
  _set_state(DUINO_STATE_NONE);

  if (_client.connected()) {
    _client.stop();
  }
}

String MinerClient::get_chip_id() {
  if(_chip_id.isEmpty()) {
    _set_chip_id();
  }
  return _chip_id;
}

void MinerClient::set_miner_name(const String& name) {
  _miner_name = name;
  if(name == "Auto") {
    _generate_miner_name();
  }
}

String MinerClient::get_miner_name() {
  if (_miner_name != "None") {
    _generate_miner_name();
  }
  return _miner_name;
}

void MinerClient::request_motd() {
  if(_state != DUINO_STATE_IDLE) {
    Serial.println("request_motd() State not idle ...");
    return;
  }
  _connectToPool();

  _client.println("MOTD");
  _set_state(DUINO_STATE_MOTD_WAIT);
}

/*
  Find the nonce
  diff = the job diff * 100 + 1
*/
bool MinerClient::findNonce(const String& seed40, const String& target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us)
{
uint8_t __hashArray[20];
uint8_t __expected_hash[20];

  _hexStringToUint8Array(target40.c_str(), __expected_hash, 20);
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

void MinerClient::loop() {
  // If we're mining always check if we're still connected, if not then
  // any work will be lost and force new pool connection
  if(_is_mining && false == this->isConnected()) {
    _set_state(DUINO_POOL_CONNECT);
  }

  // Re-start things if we've got stuck in a state for a long time
  if(_is_state_stuck()) {
    _host = ""; _port = 0;    // clear the pool, start again
    if (_client.connected()) {
      _client.stop();
    }
    _set_state(DUINO_POOL_CONNECT);
  }

  switch (_state)
  {
  case DUINO_POOL_CONNECT:
    if(shouldTryConnect(_last_connect_try, _try_count)) {
      _connectToPool();
    }
    break;
  case DUINO_STATE_VERSION_WAIT:
    if(_client.available()) {
      _poolVersion = _client.readStringUntil(END_TOKEN);
      
      _set_state(DUINO_STATE_IDLE);

      // Only send the connected event once we have a server reply with the version
      char buf[64];
      snprintf(buf, sizeof(buf), "%s:%d ver:%s", _host.c_str(), _port, _poolVersion.c_str());
      _emit_text(ME_CONNECTED, buf);
    }
    break;
  
  case DUINO_STATE_MOTD_WAIT:
    if(_client.available()) {
      _poolMOTD = _client.readString();
      _set_state(DUINO_STATE_IDLE);
      _emit_text(ME_MOTD, _poolMOTD.c_str());

      if(_is_mining) {
        // Will get requested in next loop
        _set_state(DUINO_STATE_JOB_REQUEST);
      }
    }
    break;
  
  case DUINO_STATE_JOB_REQUEST:
    if(!_is_mining)
      return;

    if(_requestJob()) {
      _set_state(DUINO_STATE_JOB_WAIT);
      _emit_nodata(ME_JOB_REQUESTED);
    }
    else {
      Serial.println("Error requesting job ...");
      // stay in the request state and try again
      // TODO only try certain number of times
    }
    break;

  case DUINO_STATE_JOB_WAIT:
    if(_client.available()) {
      String seed, target; uint16_t diff = 0;
      if (_recvJobTriplet(seed, target, diff)) {
        _seed = seed;
        _target = target;
        _diff = diff;
        _set_state(DUINO_STATE_MINING);

        MinerEventData ed;
        ed.seed40 = seed.c_str();
        ed.target40 = target.c_str();
        ed.diff = diff;
        _emit(ME_JOB_RECEIVED, ed);
      }
      else if (millis() - _state_start_ms > 5000UL) {
        _last_err = F("JOB recv timeout");
        _emit_text(ME_ERROR, _last_err.c_str());
        _set_state(DUINO_STATE_JOB_REQUEST);
      }
    }
    break;

  case DUINO_STATE_MINING:
    if(!_is_mining)
      return;

    // Solve + submit, then immediately wait for the text result
    _set_state(DUINO_STATE_JOB_DONE_SEND);

    if (this->_solveAndSubmit(_seed, _target, _diff * 100 + 1)) {
      _set_state(DUINO_STATE_SHARE_SUBMITTED);
    } else {
      _set_state(DUINO_STATE_SOLVE_FAILED);
    }

    break;

  case DUINO_STATE_SHARE_SUBMITTED:
    this->_handleSubmitResponse();
    
    // Get a new job, regardless of the result
    _set_state(DUINO_STATE_JOB_REQUEST);

  default:
    //Serial.println("MINER_CLIENT: Unknown State");
    break;
  }
}

// -----------------------------------------------
//               ------ PRIVATE -------
// -----------------------------------------------

void MinerClient::_set_state(DUINO_STATE state) {
  _state = state;
  _state_start_ms = (state == DUINO_STATE_NONE) ? 0 : millis();
}

bool MinerClient::_is_state_stuck() {
  if (_state == DUINO_STATE_IDLE
    || _state == DUINO_STATE_NONE)
  {
      return false;
  }
  
  return (millis() - _state_start_ms > STATE_STUCK_TIMEOUT) ? true : false;
}

// ---------------- low-level I/O ----------------
bool MinerClient::_connectToPool() {
  if (_client.connected()) return true;

  // Fall back to pool.cpp discovery if not provided
  _host = _host.length() ? _host : get_pool_host();
  _port = (_port > 0)    ? _port : get_pool_port();

  if (_host.isEmpty() || _port <= 0) {
    _last_err = F("Pool host / port not set");
    _emit_text(ME_ERROR, _last_err.c_str());
    return false;
  }
  
  _client.clear();
  Serial.println("[Client] Connecting to ... " + _host + ":" + String(_port));

  _last_connect_try = millis();
  ++_try_count;
  if (!_client.connect(_host.c_str(), _port, CLIENT_TIMEOUT_CONNECTION)) {
    _last_err = F("TCP connect failed");
    _emit_text(ME_ERROR, _last_err.c_str());
    return false;
  }

  _last_connect_try = 0;        // Reset, as we at least got a TCP connection
  _try_count = 0;

  // DON'T send connected event here as it will be too early. Wait for
  // the version reply
  // Immediately after connection the server should return with a pool version string
  _set_state(DUINO_STATE_VERSION_WAIT); // or MOTD directly if you wish
  _poolConnectTime = millis();
  return true;
}

// Request a job
// Example from wireshark JOB,[username],AVR,[mining_key]
// From ESPCode
// JOB,[username],start_diff,miner_key,[Temp: |CPU Temp: ]Value*C
bool MinerClient::_requestJob() {
  // JOB,<username>,<platform>,<rig_id>
  String line = String("JOB")
    + SEP_TOKEN + _username
    + SEP_TOKEN + _start_diff
    + SEP_TOKEN + MINING_KEY;
  
  Serial.println(line);  
  return _sendLine(line);
}

static bool split_triplet_csv(const String& s, String& a, String& b, String& c) {
  int p1 = s.indexOf(SEP_TOKEN);
  if (p1 < 0) return false;
  int p2 = s.indexOf(SEP_TOKEN, p1 + 1);
  if (p2 < 0) return false;
  a = s.substring(0, p1);
  b = s.substring(p1 + 1, p2);
  c = s.substring(p2 + 1);
  a.trim(); b.trim(); c.trim();
  return (a.length() == 40 && b.length() == 40 && c.length() > 0);
}

bool MinerClient::_recvJobTriplet(String& seed40, String& target40, uint16_t& diff) {
  String acc;
  const uint32_t start = millis();
  while (millis() - start < 5000UL) {
    String line;
    _readLine(line, 1000UL);
    if (line.length() == 0) { delay(0); continue; }
    if (!acc.isEmpty()) acc += SEP_TOKEN;
    acc += line;

    String a, b, c;
    if (split_triplet_csv(acc, a, b, c)) {
      seed40 = a; target40 = b; diff = (uint16_t)c.toInt();
      return true;
    }
  }
  return false;
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

  String submit = String(found_nonce)
                + SEP_TOKEN + String(found_nonce / (elapsed_time * 0.000001f))
                + SEP_TOKEN + APP_NAME + " " + APP_VERSION
                + SEP_TOKEN + _miner_name
                + SEP_TOKEN + /*String("ESP32MST")*/ String("DUCOID") + this->get_chip_id()
                //+ SEP_TOKEN + String("2785")
                //+ SEP_TOKEN + String(WALLET_GRP_ID) // Might need the wallet ID for grouping String(random(0, 2811)); // Needed for miner grouping in the wallet in the official
                //+ END_TOKEN
                ;

  #if defined(SERIAL_PRINT)
    Serial.print("SUBMITTING: ");
    Serial.println(submit);
  #endif
  if (!_sendLine(submit)) {
    return false;
  }

  return true;
}

bool MinerClient::_handleSubmitResponse() {
  String resp;
  if (!_readLine(resp, 2000UL)) return false;
  resp.trim();

  #if defined(SERIAL_PRINT)
    Serial.print("Resp from share submit: ");
    Serial.println(resp);  
  #endif

  if (resp.equalsIgnoreCase(GOOD)) {
    _emit_text(ME_RESULT_GOOD, resp.c_str());
    return true;
  } else if (resp.equalsIgnoreCase(BLOCK)) {
    _emit_text(ME_RESULT_BLOCKED, resp.c_str());
    return false;
  }
  else{
    // Treat anything else as BAD
    MinerEventData d; d.text = resp.c_str();
    _emit(ME_RESULT_BAD, d);
    return false;
  }
}

// https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TypeConversion.cpp
const char base36Chars[36] PROGMEM = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};

const uint8_t base36CharValues[75] PROGMEM{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,                                                                        // 0 to 9
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0, // Upper case letters
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35                    // Lower case letters
};

uint8_t *MinerClient::_hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength) {
    assert(hexString.length() >= arrayLength * 2);
    const char *hexChars = hexString.c_str();
    for (uint32_t i = 0; i < arrayLength; ++i) {
        uint8Array[i] = (pgm_read_byte(base36CharValues + hexChars[i * 2] - '0') << 4) + pgm_read_byte(base36CharValues + hexChars[i * 2 + 1] - '0');
    }
    return uint8Array;
}

bool MinerClient::_sendLine(const String& s) {
  if (!_client.connected()) return false;
  size_t n = _client.print(s + END_TOKEN);
  return n == (s.length() + 1);
}

bool MinerClient::_readLine(String& out, uint32_t timeout_ms) {
  out = "";
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    while (_client.available()) {
      int c = _client.read();
      if (c < 0) break;
      if (c == '\r') continue;
      if (c == END_TOKEN) return true;
      out += char(c);
    }
    delay(0);
  }
  // Tolerate partial line
  return !out.isEmpty();
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

void MinerClient::_generate_miner_name() {
  // Need to generate a name / id so always generate
  _set_chip_id();

  if (_miner_name == "None") {
    return;
  }

  // If it's not Auto then leave as is as must be user defined
  if (_miner_name != "Auto") {
    return;
  }

  // Autogenerate ID if required
  _miner_name = String("ESP32-") + _chip_id;
  _miner_name.toUpperCase();    // Modifies in-situ, doesn't return value

#if defined(SERIAL_PRINTING)
  Serial.println("Rig identifier: " + MinerName);
#endif
}

// Get and set the chip ID from fuses
void MinerClient::_set_chip_id() {
  uint64_t chip_id = ESP.getEfuseMac();
  uint16_t chip = (uint16_t)(chip_id >> 32); // Prepare to print a 64 bit value into a char array
  char fullChip[23];
  snprintf(fullChip, 23, "%04x%08lx", chip,
          (uint32_t)chip_id); // Store the (actually) 48 bit chip_id into a char array

  _chip_id = String(fullChip);
}

