#include "config.h"
#include "network_services.h"
#include "web.h"
#include "minerClient.h"
#include "pool.h"
#include "I2CMaster.h"
#include "runevery.h"
#include "led.h"
#include "display.h"

#include <ArduinoOTA.h>
#include <Arduino.h>

#define REPORT_INTERVAL 60000
#define REPEATED_WIRE_SEND_COUNT 1      // 1 for AVR, 8 for RP2040

#if defined(MINE_ON_MASTER)
  MinerClient *masterMiner;
#endif
MinerClient *slaveMiner;

void restart_esp(String msg);

void restart_esp(String msg) {
  DEBUGPRINT_LN(msg);
  DEBUGPRINT_LN("Resetting ESP...");
  #ifndef ESP01
    blink(BLINK_RESET_DEVICE);
  #endif
  #if ESP8266
    ESP.reset();
  #endif
}

// Example: bridge events to your I2C master, WS, or Serial
void minerEventSink(MinerEvent ev, const MinerEventData& d) {
  switch (ev) {
    case ME_SOLVED:
      DEBUGPRINT("[DUCO] Solved: nonce=");
      DEBUGPRINT((unsigned long)d.nonce);
      DEBUGPRINT(", HR=");
      DEBUGPRINT(d.hashrate_khs);
      DEBUGPRINT(" kH/s\n");
      break;
    case ME_SOLVE_FAILED:
      DEBUGPRINT_LN("[DUCO] Failed to solve hash.\n");
      break;
    case ME_ERROR:
      DEBUGPRINT("[DUCO] Error: ");
      DEBUGPRINT_LN(d.text ? d.text : "");
      break;
    default:
      break;
  }
}

// SETUP
void setup() {
  Serial.begin(115200);
  while(!Serial) { delay(10); }   // harmless on ESP32; guarantees attach

  DEBUGPRINT("\nDuino-Coin Master Setup: ");
  DEBUGPRINT_LN("Flash: " + String(ESP.getFlashChipSize()) + " bytes.");
  
  ledInit();

  if (String(MINING_KEY) == "None" || String(MINING_KEY) == "") {
    restart_esp("Please set a valid MINING_KEY in config.h");
  }

  display_setup();

  wifi_setup();
  showWiFi();

  ota_setup();
  
  web_setup();

  SERIALPRINT_LN("Ready for action!");
  
  #if defined(MINE_ON_MASTER)
    masterMiner = new MinerClient(DUCO_USER, true);
    masterMiner->onEvent(minerEventSink);
    masterMiner->getAttachedPool()->onEvent(poolEventSink);
    masterMiner->setMining(true);         // Start mining once connected
  #endif

  slaveMiner = new MinerClient(DUCO_USER, false);
  slaveMiner->onEvent(minerEventSink);
  slaveMiner->setupSlaves();
  slaveMiner->setMining(true);
  ledSetupUpFinished();
}

void loop() {
  ArduinoOTA.handle();

  web_loop();

  slaveMiner->loop();

  #if defined(MINE_ON_MASTER)
    masterMiner->loop();
    // Small delay to keep CPU cool; adjust as needed
    delay(5);
  #endif
}
