#include <ArduinoJson.h>

#include "config.h"
#include "network_services.h"
#include "web.h"
#include "pool.h"

const char * urlPool = "https://server.duinocoin.com/getPool";
const char * urlMiningKeyStatus = "https://server.duinocoin.com/mining_key";

String _host = "162.55.103.174";
int _port = 6000;
String _mining_key = "None";

static bool connected = false;
static uint32_t lastAttempt = 0;

static void _check_mining_key(String response);

void pool_setup() {
  pool_update();
}

void pool_update() {
  String input = http_get_string(urlPool);
  if (input == "")
    return;
  DynamicJsonDocument doc(256);
  deserializeJson(doc, input); 

  const char* name = doc["name"];
  const char* ip = doc["ip"];
  int port = doc["port"];

  Serial.println("[Pool]: " + String(name) + " (" + String(ip) + ":" + String(port) + ")");
  _host = String(ip);
  _port = port;
}

void pool_loop() {
  // connection management & miner protocol
  if (!connected && millis() - lastAttempt > 3000) {
    lastAttempt = millis();
    // try connect...
    // connected = try_connect_to_pool();
  }
}

bool pool_connected() { return connected; }

String get_pool_host() {
  return _host;
}

int get_pool_port() {
  return _port;
}

String get_mining_key() {
  return _mining_key;
}

void set_mining_key(String new_mining_key) {
    _mining_key = new_mining_key;
    Serial.println("[ ] Using mining_key: " + _mining_key);
}

void update_mining_key(String new_mining_key, String ducouser) {
    String url = String(urlMiningKeyStatus) + "?u=" + String(ducouser) + "&k=" + new_mining_key;
    String res = http_get_string(url);
    if (res == "")
      return;

    _check_mining_key(res);
}

void _check_mining_key(String response) 
{
    Serial.println("[ ] CheckMiningKey " + response);
    DynamicJsonDocument doc(128);
    deserializeJson(doc, response);

    //input::{"has_key":false,"success":true}
    bool has_key = doc["has_key"];
    bool success = doc["success"];

    Serial.println("[ ] mining_key has_key: " + String(has_key) + "  success: " + String(success));

    if (success && !has_key) {
        Serial.println("[ ] Wallet does not have a mining key. Proceed..");
        set_mining_key("None");
    }
    else if (!success) {
        if (_mining_key == "None") {
            Serial.println("[ ] Update mining_key to proceed. Halt..");
            ws_send_all("Update mining_key to proceed. Halt..");
            for(;;);
        }
        else {
            Serial.println("[ ] Invalid mining_key. Halt..");
            ws_send_all("Invalid mining_key. Halt..");
            for(;;);
        }
    }
    else {
        Serial.println("[ ] Updated mining_key..");
        set_mining_key(_mining_key);
    }
}
