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

RunEvery reportTimer(REPORT_INTERVAL);
RunEvery scanTimer(50000);

#if defined(MINE_ON_MASTER)
  MinerClient *masterMiner;
#endif
MinerClient *slaveMiner;

void restart_esp(String msg);
void poolEventSink(PoolEvent ev, const PoolEventData& d);

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

void poolEventSink(PoolEvent ev, const PoolEventData& d) {
  switch (ev)
  {
  case POOLEVT_CONNECTED:
    DEBUGPRINT_LN("[MAIN] POOLEVT_CONNECTED");
    DEBUGPRINT_LN(d.text);
    break;
  case POOLEVT_DISCONNECTED:     // payload: text = reason
    DEBUGPRINT_LN("[MAIN] POOLEVT_DISCONNECTED");
    break;
  case POOLEVT_MOTD:             // payload: text = motd
    DEBUGPRINT_LN("[MAIN] POOLEVT_MOTD");
    DEBUGPRINT_LN(d.text);
    break;
  case POOLEVT_JOB_REQUESTED:
    DEBUGPRINT_LN("[MAIN] POOLEVT_REQUESTED");
    break;
  case POOLEVT_JOB_RECEIVED:     // payload: seed40 / target40 / diff
    DEBUGPRINT_LN("[MAIN] POOLEVT_JOB_RECEIVED");
    DEBUGPRINT("  prevHash: ");
    DEBUGPRINT_LN(d.jobDataPtr->prevHash);
    DEBUGPRINT("  exptHash: ");
    DEBUGPRINT_LN(d.jobDataPtr->expectedHash);
    DEBUGPRINT("      Diff: ");
    DEBUGPRINT_LN(d.jobDataPtr->difficulty);
    break;
  case POOLEVT_ERROR:
    DEBUGPRINT("[MAIN] POOLEVT_ERROR: ");
    DEBUGPRINT_LN(d.text);
    break;

  default:
    break;
  }
}

#if defined(MINE_ON_MASTER)
// Example: bridge events to your I2C master, WS, or Serial
void minerEventSink(MinerEvent ev, const MinerEventData& d) {
  switch (ev) {
    case ME_SOLVED:
      DEBUGPRINT("[DUCO] Solved: nonce=");
      DEBUGPRINT((unsigned long)d.nonce);
      DEBUGPRINT(", HR=");
      DEBUGPRINT(d.hashrate_khs);
      DEBUGPRINT(" kH/s\n");
      blinkStatus(BLINK_SHARE_SOLVED);
      break;
    case ME_SOLVE_FAILED:
      DEBUGPRINT_LN("[DUCO] Failed to solve hash.\n");
      break;
    case ME_RESULT_GOOD:
      DEBUGPRINT_LN("[DUCO] Share accepted");
      blinkStatus(BLINK_SHARE_GOOD);
      break;
    case ME_RESULT_BAD:
      DEBUGPRINT("[DUCO] Share rejected: ");
      DEBUGPRINT_LN(d.text ? d.text : "");
      blinkStatus(BLINK_SHARE_ERROR);
      break;
    case ME_RESULT_BLOCK:
      DEBUGPRINT_LN("[DUCO] Found a BLOCK ... Whoa!");
      blinkStatus(BLINK_SHARE_BLOCKFOUND);
      break;
    case ME_ERROR:
      DEBUGPRINT("[DUCO] Error: ");
      DEBUGPRINT_LN(d.text ? d.text : "");
      break;
    default:
      break;
  }
}
#endif

uint8_t testSendBytes = 0;

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
  delay(200);
  
  #if defined(MINE_ON_MASTER)
    masterMiner = new MinerClient(DUCO_USER, true);
    masterMiner->onEvent(minerEventSink);
    masterMiner->getAttachedPool()->onEvent(poolEventSink);
    masterMiner->setMining(true);         // Start mining once connected
  #endif

  //slaveMiner = new MinerClient(DUCO_USER, false);
  //slaveMiner->onEvent(minerEventSink);
  //slaveMiner->setupSlaves();
  //slaveMiner->setMining(true);
  ledSetupUpFinished();

  // if(I2C.newJobRequest(0x30)) {
  //   uint8_t lastHashStr[] = "bf55bad9a75c5b375a1457b0a252d75d60abce13";
  //   // uint8_t lh[20] = {0xbf,0x55,0xba,0xd9,0xa7,0x5c,0x5b,0x37,0x5a,0x14,0x57,0xb0,0xa2,0x52,0xd7,0x5d,0x60,0xab,0xce,0x13};
  //   uint8_t eh[20] = {0xe6,0xa9,0x7a,0x92,0x27,0xad,0x70,0x21,0x9a,0x95,0x32,0x3a,0x82,0x2a,0x70,0x74,0xd8,0x13,0x24,0x8b};

  //   if(!I2C.sendJobData(0x30, lastHashStr, eh, 10)) {
  //     DEBUGPRINT_LN("Sending job failed :(");
  //   }
  // }
  // job_state = 3;
}

void loop() {
  ArduinoOTA.handle();

  // if (reportTimer.shouldRun()) {
  //   Serial.print("[ ]");
  //   Serial.println("FreeRam: " + String(ESP.getFreeHeap()) + " " + clients_string());
  //   ws_send_all("FreeRam: " + String(ESP.getFreeHeap()) + " - " + clients_string());
  //   clients_report(REPORT_INTERVAL);
  // }

  web_loop();

  if(scanTimer.shouldRun()) {
    //I2C.scan(true);

    // Ping & query heap metrics
    
    // for (uint8_t idx = 0; idx < I2C.getFoundSlaveCount(); idx++) {
    //   uint8_t addr = I2C.getFoundSlave(idx);
    //   if (!I2C.probe(addr)) continue;

      // if (I2C.ping(addr)) {
      //   Serial.printf("Addr 0x%02X: ping OK\n", addr);

      //   // uint32_t heap;
      //   // if (I2C.queryHeap(addr, heap)) {
      //   //   Serial.printf("  Heap: %u bytes\n", heap);
      //   // }

      //   uint32_t uptime;
      //   if (I2C.queryUptime(addr, uptime)) {
      //     Serial.printf("  Uptime: %lu ms. %u mins %u secs\n",
      //         (unsigned long)uptime, (unsigned int)uptime/60000, (unsigned int)(uptime%60000)/1000 );
      //   }

      //   #if defined(TEST_FUNCS)
      //     if(testSendBytes > 0) {
      //       if (!I2C.testSend(addr, testSendBytes++)) {
      //         DEBUGPRINT("Test send of ");
      //         DEBUGPRINT(testSendBytes);
      //         DEBUGPRINT_LN(" bytes FAILED");
      //       }
      //     if(testSendBytes > 15) testSendBytes = 4;
      //     }
      //   #endif
      // }
      
    //  delay(20);
    // }
  }

  #if defined(MINE_ON_MASTER)
    masterMiner->loop();
  #endif

  //slaveMiner->loop();

  // Small delay to keep CPU cool; adjust as needed
  delay(5);
}
