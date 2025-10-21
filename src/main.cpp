#include "config.h"
#include "network_services.h"
#include "web.h"
#include "minerClient.h"
#include "pool.h"
#include "I2CMaster.h"
#include "runevery.h"
#include "led.h"

#include <ArduinoOTA.h>
#include <Arduino.h>

#define REPORT_INTERVAL 60000
#define REPEATED_WIRE_SEND_COUNT 1      // 1 for AVR, 8 for RP2040

I2CMaster I2C;

RunEvery reportTimer(REPORT_INTERVAL);
RunEvery scanTimer(50000);
RunEvery slaveStatusTimer(50);

#if defined(MINE_ON_MASTER)
MinerClient *masterMiner;
#endif

void restart_esp(String msg);
void poolEventSink(PoolEvent ev, const PoolEventData& d);

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

void poolEventSink(PoolEvent ev, const PoolEventData& d) {
  switch (ev)
  {
  case POOLEVT_CONNECTED:
    SERIALPRINT_LN("[MAIN] POOLEVT_CONNECTED");
    SERIALPRINT_LN(d.text);
    break;
  case POOLEVT_DISCONNECTED:     // payload: text = reason
    SERIALPRINT_LN("[MAIN] POOLEVT_DISCONNECTED");
    break;
  case POOLEVT_MOTD:             // payload: text = motd
    SERIALPRINT_LN("[MAIN] POOLEVT_MOTD");
    SERIALPRINT_LN(d.text);
    break;
  case POOLEVT_JOB_REQUESTED:
    SERIALPRINT_LN("[MAIN] POOLEVT_REQUESTED");
    break;
  case POOLEVT_JOB_RECEIVED:     // payload: seed40 / target40 / diff
    SERIALPRINT_LN("[MAIN] POOLEVT_JOB_RECEIVED");
    SERIALPRINT("  prevHash: ");
    SERIALPRINT_LN(d.jobDataPtr->prevHash);
    SERIALPRINT("  exptHash: ");
    SERIALPRINT_LN(d.jobDataPtr->expectedHash);
    SERIALPRINT("      Diff: ");
    SERIALPRINT_LN(d.jobDataPtr->difficulty);
    break;
  case POOLEVT_ERROR:
    SERIALPRINT("[MAIN] POOLEVT_ERROR: ");
    SERIALPRINT_LN(d.text);
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
      SERIALPRINT_F("[DUCO] Solved: nonce=%lu, HR=%.2f kH/s\n", (unsigned long)d.nonce, d.hashrate_khs);
      blinkStatus(BLINK_SHARE_SOLVED);
      break;
    case ME_SOLVE_FAILED:
      SERIALPRINT_LN("[DUCO] Failed to solve hash.\n");
      break;
    case ME_RESULT_GOOD:
      SERIALPRINT_LN("[DUCO] Share accepted");
      blinkStatus(BLINK_SHARE_GOOD);
      break;
    case ME_RESULT_BAD:
      SERIALPRINT_F("[DUCO] Share rejected: %s\n", d.text ? d.text : "");
      blinkStatus(BLINK_SHARE_ERROR);
      break;
    case ME_RESULT_BLOCK:
      SERIALPRINT_LN("[DUCO] Found a BLOCK ... Whoa!");
      blinkStatus(BLINK_SHARE_BLOCKFOUND);
      break;
    case ME_ERROR:
      SERIALPRINT_F("[DUCO] Error: %s\n", d.text ? d.text : "");
      break;
    default:
      break;
  }
}
#endif

uint8_t testSendBytes = 0;
uint8_t job_state = 1;

// SETUP the mining master
void setup() {

  Serial.begin(115200);
  while(!Serial) { delay(10); }   // harmless on ESP32; guarantees attach

  SERIALPRINT("\nDuino-Coin Master Setup: ");
  SERIALPRINT_LN("Flash: " + String(ESP.getFlashChipSize()) + " bytes.");
  
  ledInit();

  if (String(MINING_KEY) == "None" || String(MINING_KEY) == "") {
    restart_esp("Please set a valid MINING_KEY in config.h");
  }

  wifi_setup();
  ota_setup();

  I2C.begin();
  
  web_setup();

  Serial.printf("[%s] v%s ready for action!\n", APP_NAME, APP_VERSION);
  delay(200);
  
  I2C.scan(true);

  #if defined(MINE_ON_MASTER)
    masterMiner = new MinerClient(DUCO_USER, DEVICE_ESP32 );
    masterMiner->onEvent(minerEventSink);
    masterMiner->setMinerName("ESP32MasterMiner");
    masterMiner->getAttachedPool()->onEvent(poolEventSink);

    masterMiner->setMasterMiner(true);    // Setup this one as a miner on this device
    masterMiner->setMining(true);         // Start mining once connected
  #endif

  ledSetupUpFinished();

  // if(I2C.newJobRequest(0x30)) {
  //   uint8_t lastHashStr[] = "bf55bad9a75c5b375a1457b0a252d75d60abce13";
  //   // uint8_t lh[20] = {0xbf,0x55,0xba,0xd9,0xa7,0x5c,0x5b,0x37,0x5a,0x14,0x57,0xb0,0xa2,0x52,0xd7,0x5d,0x60,0xab,0xce,0x13};
  //   uint8_t eh[20] = {0xe6,0xa9,0x7a,0x92,0x27,0xad,0x70,0x21,0x9a,0x95,0x32,0x3a,0x82,0x2a,0x70,0x74,0xd8,0x13,0x24,0x8b};

  //   if(!I2C.sendJobData(0x30, lastHashStr, eh, 10)) {
  //     SERIALPRINT_LN("Sending job failed :(");
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
    I2C.scan(true);

    // Ping & query heap metrics
    
    for (uint8_t idx = 0; idx < I2C.getFoundSlaveCount(); idx++) {
      uint8_t addr = I2C.getFoundSlave(idx);
      if (!I2C.probe(addr)) continue;

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
      //         SERIALPRINT("Test send of ");
      //         SERIALPRINT(testSendBytes);
      //         SERIALPRINT_LN(" bytes FAILED");
      //       }
      //     if(testSendBytes > 15) testSendBytes = 4;
      //     }
      //   #endif
      // }
      
      switch (job_state)
      {
      case 0:
        SERIALPRINT_LN(F("LOOP JOB STATE:: not running."));
        break;

      case 1:
        SERIALPRINT_LN(F("LOOP JOB STATE:: new job request"));
        if(!I2C.getSlaveIsIdle(addr)) break;

        if(I2C.newJobRequest(addr)) {
          job_state = 2;  // ok to start sending data
        }
        else {
          SERIALPRINT_LN("Request to start new job failed :(");
        }
        break;

      case 2: {
        SERIALPRINT_LN(F("LOOP JOB STATE:: sending data"));
        uint8_t lastHashStr[] = "bf55bad9a75c5b375a1457b0a252d75d60abce13";
        uint8_t eh[20] = {0xe6,0xa9,0x7a,0x92,0x27,0xad,0x70,0x21,0x9a,0x95,0x32,0x3a,0x82,0x2a,0x70,0x74,0xd8,0x13,0x24,0x8b};

        if(!I2C.sendJobData(addr, lastHashStr, eh, 10)) {
          SERIALPRINT_LN("Sending job failed :(");
        }
        //I2C.testDumpData(addr);

        job_state = 3;
        break;

      }

      case 3:
        if(slaveStatusTimer.shouldRun()) {
          uint16_t nonce = 0;
          uint8_t timeTakenMs = 0;
          if(!I2C.getJobStatus(addr, nonce, timeTakenMs)) break;
          else {
            SERIALPRINT("Job finished and nonce; ");
            SERIALPRINT(nonce);
            SERIALPRINT(" found in ");
            SERIALPRINT(timeTakenMs);
            SERIALPRINT_LN("ms");
            job_state = 1;          // send the next
          }
        }
        break;
      default:
        break;
      }

     delay(20);
    }

  }

  #if defined(MINE_ON_MASTER)
    masterMiner->loop();
  #endif

  // Small delay to keep CPU cool; adjust as needed
  delay(5);
}
