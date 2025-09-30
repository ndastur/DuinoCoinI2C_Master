#include "config.h"
#include "network_services.h"
#include "web.h"
#include "miner_client.h"
#include "pool.h"
#include "wirewrap.h"
#include "runevery.h"

#include <ArduinoOTA.h>
#include <Arduino.h>

#define REPORT_INTERVAL 60000
#define REPEATED_WIRE_SEND_COUNT 1      // 1 for AVR, 8 for RP2040

#define BLINK_SHARE_FOUND    1
#define BLINK_SETUP_COMPLETE 2
#define BLINK_CLIENT_CONNECT 3
#define BLINK_RESET_DEVICE   5

#define LED_RED     25
#define LED_BLUE    26
#define LED_GREEN   27
#define LED_YELLOW  32

#ifndef LED_BUILTIN
  #define LED_BUILTIN 25
#endif

RunEvery reportTimer(REPORT_INTERVAL);
MinerClient *masterMiner;

void blink(uint8_t count, uint8_t pin);
void restart_esp(String msg);

void blink(uint8_t count, uint8_t pin = LED_BUILTIN) {
  uint8_t state = HIGH;
  for (int x = 0; x < (count << 1); ++x) {
    digitalWrite(pin, state ^= HIGH);
    delay(50);
  }
  digitalWrite(pin, LOW);
}

void restart_esp(String msg) {
  SERIALPRINT_LN(msg);
  SERIALPRINT_LN("Resetting ESP...");
  #ifndef ESP01
    blink(BLINK_RESET_DEVICE);
  #endif
  #if ESP8266
    ESP.reset();
  #endif
}

// Example: bridge events to your I2C master, WS, or Serial
void miner_event_sink(MinerEvent ev, const MinerEventData& d, void* user) {
  (void)user; // if unused
  switch (ev) {
    case ME_CONNECTED:
      SERIALPRINT_F("[DUCO] Connected: %s\n", d.text);
      // Once we are connected get the MOTD
      masterMiner->request_motd();
      break;
    case ME_MOTD:
      SERIALPRINT_F("[DUCO] MOTD: %s\n", d.text);
      break;
    case ME_JOB_REQUESTED:
      SERIALPRINT_LN("[DUCO] Job requested from pool...");
      break;
    case ME_JOB_RECEIVED:
      SERIALPRINT_F("[DUCO] Job received\n - diff=%u - seed=%s - target=%s\n",
                    d.diff, d.seed40, d.target40);
      // If you want to notify another MCU over I2C/UART, do it here
      break;
    case ME_SOLVED:
      SERIALPRINT_F("[DUCO] Solved: nonce=%lu, HR=%.2f kH/s\n", (unsigned long)d.nonce, d.hashrate_khs);
      blink(2, LED_YELLOW);
      break;
    case ME_SOLVE_FAILED:
      SERIALPRINT_LN("[DUCO] Failed to solve hash.\n");
      break;
    case ME_RESULT_GOOD:
      SERIALPRINT_LN("[DUCO] Share accepted");
      blink(2, LED_GREEN);
      break;
    case ME_RESULT_BAD:
      SERIALPRINT_F("[DUCO] Share rejected: %s\n", d.text ? d.text : "");
      blink(2, LED_RED);
      break;
    case ME_RESULT_BLOCKED:
      SERIALPRINT_LN("[DUCO] Share blocked by pool");
      break;
    case ME_ERROR:
      SERIALPRINT_F("[DUCO] Error: %s\n", d.text ? d.text : "");
      break;
    case ME_DISCONNECTED:
      SERIALPRINT_F("[DUCO] Disconnected: %s\n", d.text ? d.text : "");
      break;
    default:
      break;
  }
}

// SETUP the mining master
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);

  Serial.begin(115200);
  while(!Serial) { delay(10); }   // harmless on ESP32; guarantees attach

  SERIALPRINT("\nDuino-Coin Master Setup: ");
  SERIALPRINT_LN("Flash: " + String(ESP.getFlashChipSize()) + " bytes.");

  wirewrap_setup();
  wifi_setup();
  ota_setup();
  pool_setup();
  web_setup();

  if (String(MINING_KEY) == "None" || String(MINING_KEY) == "") {
    restart_esp("Please set a valid MINING_KEY in config.h");
  }

  blink(BLINK_SETUP_COMPLETE);
  blink(2, LED_RED);
  blink(2, LED_BLUE);
  blink(2, LED_GREEN);
  blink(2, LED_YELLOW);

  Serial.printf("[%s] v%s ready for action!\n", APP_NAME, APP_VERSION);
  delay(1000);
  
  masterMiner = new MinerClient(DUCO_USER, "ESP32MasterMiner");
  masterMiner->setMasterMiner(true);    // Setup this one as a miner on this device
  masterMiner->setMining(true);
  // register callback
  masterMiner->onEvent(miner_event_sink, nullptr);

  #if defined (TEST_FIRST_HASH)
    uint32_t nonce = 0;
    uint32_t elapsed_time = 0;
    SERIALPRINT_LN("Starting testing nonce find ...");
    if ( masterMiner->findNonce(
      String("d860af6413f39bc0b81da43f7de2d0eb4c015b83"),
      String("015929005720943aef1dd22eea0b988e06b1abe1"),
      8200 * 100 + 1, nonce, elapsed_time) ){

        Serial.println("Found the test Nonce - " + String(nonce));
        Serial.println("In " + String(elapsed_time/1000000.0f) + " secs.");
        Serial.println("HR " + String(nonce / (elapsed_time * 0.000001f) ));

        if(nonce == 279490) { Serial.println("Found expected nonce ..."); }
        else { Serial.println("Didn't find expected nonce :( "); }
      }
    else {
      Serial.println("Find failed :()");
    }
  #endif

  if(!masterMiner->connect()) {
    SERIALPRINT_LN("Failed to connect to mining pool :(");
  }
}

void loop() {
  ArduinoOTA.handle();

  // if (reportTimer.shouldRun()) {
  //   Serial.print("[ ]");
  //   Serial.println("FreeRam: " + String(ESP.getFreeHeap()) + " " + clients_string());
  //   ws_send_all("FreeRam: " + String(ESP.getFreeHeap()) + " - " + clients_string());
  //   clients_report(REPORT_INTERVAL);
  // }

  pool_loop();
  wirewrap_loop();
  web_loop();

  masterMiner->loop();

  // Small delay to keep CPU cool; adjust as needed
  delay(5);
}
