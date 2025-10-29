#include "config.h"
#include "pool.h"
#include "network_services.h"
#include "utils.h"

#include <Arduino.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

#define CLIENT_TIMEOUT_CONNECTION 30000
#define CLIENT_TIMEOUT_RW         5000UL
#define STATE_STUCK_TIMEOUT       30000UL

#define END_TOKEN  '\n'
#define SEP_TOKEN  ','

#define AVR_WORKER_JOB "AVR"
#define ESP_WORKER_JOB "ESP32S"

#define BAD "BAD"
#define GOOD "GOOD"
#define BLOCK "BLOCK"

const char * urlPool = "https://server.duinocoin.com/getPool";
const char * urlMiningKeyStatus = "https://server.duinocoin.com/mining_key";

Pool::Pool(String username, String miningKey, DeviceType type) {
  _username = username;
  _miningKey = miningKey;
  _type = type;
  _workerId = String(getChipId());  // default

  switch (type) {
    case DEVICE_SLAVE:
    case DEVICE_AVR:
      _startingDifficulty = AVR_WORKER_JOB;
      _appName = String(APP_NAME_SLAVE) + String(APP_VERSION);
      break;
    case DEVICE_ESP32:
      _startingDifficulty = ESP_WORKER_JOB;
  _appName = String(APP_NAME_MASTER) + String(APP_VERSION);
      break;
    default:
      DEBUGPRINT_LN("[POOL] ctor no device type specified");
      break;
  }
}

void Pool::onEvent(PoolEventCallback cb) {
  _cb = cb;
}

bool Pool::isConnected() {
  return _client.connected() && _poolConnectTime > 0;
}

void Pool::setUsername(String un) {
  _username = un;
}

void Pool::setMinerName(String minerName) {
  _minerName = minerName.isEmpty() ? "None" : minerName;
}

void Pool::setWorkerId(String workerId) {
  _workerId = workerId.isEmpty() || workerId == "Auto" ? String(getChipId()) : workerId;
  DEBUGPRINT_LN("[POOL] setting worker id; " + _workerId);
}

void Pool::setMiningKey(String newMiningKey) {
    _miningKey = newMiningKey;
    DEBUGPRINT_LN("[POOL] Setting mining_key: " + _miningKey);
}

/// ******* SETUP POOL ********
void Pool::setup() {     // connect/register with DuinoCoin pool(s)
  update();
}

/// ******* MAIN LOOP ********
void Pool::loop() {

  // Re-start things if we've got stuck in a state for a long time
  if(_isStateStuck()) {
    // This might not be because of a disconnect, handle better
    _poolConnectTime = 0;
    _host = "";
    _port = 0;    // clear the pool, start again
    if (_client.connected()) {
      _client.stop();
    }
    _setState(POOL_STATE_CONNECT);
  }

  /// State engine
  switch (_state)
  {
  case POOL_STATE_CONNECT:
    if(shouldTryConnect(_lastConnectTry, _tryCount)) {
      connect();
    }
    break;
  
  case POOL_STATE_VERSION_WAIT:
    if(_client.available()) {
      _poolVersion = _client.readStringUntil(END_TOKEN);

      _setState(POOL_STATE_IDLE);
      _poolConnectTime = millis();

      DEBUGPRINT("[POOL] connected ... ");
      DEBUGPRINT_LN(_minerName);

      // Only send the connected event once we have a server reply with the version
      char buf[128];
      snprintf(buf, sizeof(buf), "%s [%s:%d] Ver: %s",
        _name.c_str(),
        _host.c_str(),
        _port,
        _poolVersion.c_str());
      _emit_text(POOLEVT_CONNECTED, buf);
    }

  case POOL_STATE_MOTD_WAIT:
    if(_client.available()) {
      _MOTD = _client.readString();
      _setState(POOL_STATE_IDLE);
      _emit_text(POOLEVT_MOTD, _MOTD.c_str());
    }
    break;

  case POOL_STATE_JOB_WAIT:
    if(_client.available()) {
    String seed, target; uint16_t diff = 0;
    if (_recvJobTriplet(seed, target, diff)) {
      _poolJob.difficulty = diff;
      _poolJob.expectedHash = target;
      _poolJob.prevHash = seed;

      _setState(POOL_STATE_SHARE_WAIT);

      PoolEventData ed;
      ed.jobDataPtr = &_poolJob;
      _emit(POOLEVT_JOB_RECEIVED, ed);
    }
    else if (millis() - _stateStartMS > 5000UL) {
      _last_err = F("JOB recv timeout");
      _emit_text(POOLEVT_ERROR, _last_err.c_str());
    }
  }
    break;

  case POOL_STATE_SHARE_WAIT:
    // No-op
    break;
    
  case POOL_STATE_SUBMITTED:
    _handleSubmitJobResponse();
    _setState(POOL_STATE_IDLE);     // our work is done
    break;
  default:
    break;
  }
}

bool Pool::update() {
  String input = http_get_string(urlPool);
  if (input == "")
    return false;
  DynamicJsonDocument doc(256);
  deserializeJson(doc, input); 

  const char* name = doc["name"];
  const char* ip = doc["ip"];
  int port = doc["port"];

  DEBUGPRINT_LN("[POOL]: " + String(name) + " (" + String(ip) + ":" + String(port) + ")");
  _name = String(name);
  _host = String(ip);
  _port = port;

  return (_host.length() > 8 && _port > 1024);
}

bool Pool::connect() {
  if (_client.connected()) return true;

  // Not connected so clear state
  _setState(POOL_STATE_NONE);

  if (_host.isEmpty() || _port <= 0) {
    if(!update()) return false;
  }
  
  _client.stop();

  Serial.println("[POOL] Connecting to ... " + _host + ":" + String(_port));
  _lastConnectTry = millis();
  ++_tryCount;
  if (!_client.connect(_host.c_str(), _port, CLIENT_TIMEOUT_CONNECTION)) {
    _last_err = F("TCP connect failed");
    _host = "";
    _port = 0;
    _emit_text(POOLEVT_ERROR, _last_err.c_str());
    return false;
  }

  _lastConnectTry = 0;        // Reset, as we at least got a TCP connection
  _tryCount = 0;

  // DON'T send connected event here as it will be too early. Wait for
  // the version reply
  // Immediately after connection the server should return with a pool version string
  _setState(POOL_STATE_VERSION_WAIT); // or MOTD directly if you wish
  return true;
}

bool Pool::disconnect() {
  _client.stop();
  return true;
}

bool Pool::requestMOTD() {
  if(_state != POOL_STATE_IDLE) {
    DEBUGPRINT_LN("[POOL] request MOTD State not idle ...");
    return false;
  }
  if(!connect()) return false;

  _client.println("MOTD");
  _setState(POOL_STATE_MOTD_WAIT);
  return true;
}

// Request a job from the Pool
bool Pool::requestJob() {
  if(!connect()) return false;

  if(_state != POOL_STATE_IDLE) {
    DEBUGPRINT("[POOL] ");
    DEBUGPRINT(_minerName);
    DEBUGPRINT(" request for job. Pool state not idle. State: ");
    DEBUGPRINT_LN(_state);
    return false;
  }

  _poolJob.difficulty = 0;

  // Example from wireshark JOB,[username],AVR,[mining_key]
  // From ESPCode
  // JOB,[username],start_diff,miner_key,[Temp: |CPU Temp: ]Value*C
  // JOB,<username>,<platform>,<rig_id>
  String line = String("JOB")
    + SEP_TOKEN + _username
    + SEP_TOKEN + _startingDifficulty
    + SEP_TOKEN + MINING_KEY;
  
  DEBUGPRINT("[POOL] ");
  DEBUGPRINT(_minerName);
  DEBUGPRINT(" Req job: ");
  DEBUGPRINT_LN(line);

  bool ret = _sendLine(line);

  if(ret == true) {
    _setState(POOL_STATE_JOB_WAIT);
  }
  return ret;
}

Job* Pool::getJob() {
  return _poolJob.difficulty == 0 ? nullptr : &_poolJob;
}

bool Pool::submitJob(uint32_t foundNonce, uint32_t elapsedTimeUS, String workerId) {
  String wrkId = workerId.isEmpty() ? _workerId : workerId;
  String submit = String(foundNonce)
                + SEP_TOKEN + String(foundNonce / (elapsedTimeUS * 0.000001f))
                + SEP_TOKEN + _appName
                + SEP_TOKEN + _minerName
                + SEP_TOKEN + String("DUCOID") + wrkId
                //+ SEP_TOKEN + String(WALLET_GRP_ID) // Might need the wallet ID for grouping String(random(0, 2811)); // Needed for miner grouping in the wallet in the official
                ;

  DEBUGPRINT("[POOL] Submit: ");
  DEBUGPRINT_LN(submit);

  bool ret = _sendLine(submit);       //+ END_TOKEN added by _sendline
  if(ret) {
    _setState(POOL_STATE_SUBMITTED);
    return false;
  }
  else {
    // TODO handle this error better
    return false;
  }
}

// -----------------------------------------------
//               ------ PRIVATE -------
// -----------------------------------------------

void Pool::_setState(DUINO_POOL_STATE state) {
  _state = state;
  _stateStartMS = (state == POOL_STATE_NONE) ? 0 : millis();
}

bool Pool::_isStateStuck() {
  if (_state == POOL_STATE_IDLE
    || _state == POOL_STATE_NONE)
  {
      return false;
  }
  
  return (millis() - _stateStartMS > STATE_STUCK_TIMEOUT) ? true : false;
}

bool Pool::_sendLine(const String& s) {
  if (!_client.connected()) return false;
  size_t n = _client.print(s + END_TOKEN);
  return n == (s.length() + 1);
}

bool Pool::_readLine(String& out, uint32_t timeout_ms) {
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

bool Pool::_splitTripletCSV(const String& s, String& a, String& b, String& c) {
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

bool Pool::_recvJobTriplet(String& seed40, String& target40, uint16_t& diff) {
  String acc;
  const uint32_t start = millis();
  while (millis() - start < 5000UL) {
    String line;
    _readLine(line, CLIENT_TIMEOUT_RW);
    if (line.length() == 0) { delay(0); continue; }
    if (!acc.isEmpty()) acc += SEP_TOKEN;
    acc += line;

    String a, b, c;
    if (_splitTripletCSV(acc, a, b, c)) {
      seed40 = a; target40 = b; diff = (uint16_t)c.toInt();
      return true;
    }
  }
  return false;
}

bool Pool::_handleSubmitJobResponse() {
  String resp;
  if (!_readLine(resp, CLIENT_TIMEOUT_RW)) return false;
  resp.trim();

  DEBUGPRINT("[POOL] Resp from share submit: ");
  DEBUGPRINT_LN(resp);  

  if (resp.equalsIgnoreCase(GOOD)) {
    _emit_text(POOLEVT_RESULT_GOOD, resp.c_str());
    return true;
  } else if (resp.equalsIgnoreCase(BLOCK)) {
    _emit_text(POOLEVT_RESULT_BLOCK, resp.c_str());
    return true;
  }
  else{
    // Treat anything else as BAD
    PoolEventData d; d.text = resp.c_str();
    _emit(POOLEVT_RESULT_BAD, d);
    return false;
  }
}

void Pool::_checkMiningKey(String new_mining_key, String ducouser)
{
    String url = String(urlMiningKeyStatus) + "?u=" + String(ducouser) + "&k=" + new_mining_key;
    String response = http_get_string(url);
    if (response == "")
      return;

    Serial.println("[POOL] CheckMiningKey " + response);
    DynamicJsonDocument doc(128);
    deserializeJson(doc, response);

    bool has_key = doc["has_key"];
    bool success = doc["success"];

    Serial.println("[POOL] mining_key has_key: " + String(has_key) + "  success: " + String(success));

    if (success && !has_key) {
        Serial.println("[POOL] Wallet does not have a mining key. Proceed..");
        setMiningKey("None");
    }
    else if (!success) {
        if (_miningKey == "None") {
            Serial.println("[POOL] Update mining_key to proceed. Halt..");
            //ws_send_all("Update mining_key to proceed. Halt..");
            for(;;);
        }
        else {
            Serial.println("[POOL] Invalid mining_key. Halt..");
            //ws_send_all("Invalid mining_key. Halt..");
            for(;;);
        }
    }
    else {
        Serial.println("[POOL] Updated mining_key..");
        setMiningKey(_miningKey);
    }
}