#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---- Wi-Fi ----
#ifndef WIFI_SSID
  #error "WIFI_SSID is not defined! Did you forget to set it in platformio_override.ini?"
#endif

#ifndef WIFI_PASS
  #error "WIFI_PASS is not defined! Did you forget to set it in platformio_override.ini?"
#endif

// ---- Duino-Coin ----
// You can also set these in your build environment
// e.g., in platformio.ini:
// build_flags =
//   -DDUCO_USER=\"YourUsername\"
//   -DRIG_IDENTIFIER=\"MyMiner\"
//   -DMDNS_RIG_IDENTIFIER=\"mymdnsname\"
//   -DMINING_KEY=\"MyMiningKey\"
#ifndef DUCO_USER
  #error "DUCO username is not defined! Did you forget to set it in platformio_override.ini?"
#endif
#ifndef RIG_IDENTIFIER
  #define RIG_IDENTIFIER "None"             // Change this if you want a custom miner name
#endif
#ifndef MDNS_RIG_IDENTIFIER
  #define MDNS_RIG_IDENTIFIER "esp32master"     // Change this if you want a custom local mDNS miner name
#endif
#ifndef MINING_KEY
  #define MINING_KEY "None"            // Change this if wallet is protected with mining key
#endif 


// ---- App settings ----
#define APP_NAME        "Official ESP32-S2 Miner" //      "ESP32S ND Master"
#define APP_VERSION     "4.3"

// Optional: pool/port, pins, etc.
// #define DUCO_POOL "server.duinocoin.com"
// #define DUCO_PORT 2813

#ifdef SERIAL_PRINT
  #define SERIALBEGIN()             Serial.begin(115200)
  #define SERIALPRINT(x)            Serial.print(x)
  #define SERIALPRINT_LN(x)         Serial.println(x)
  #define SERIALPRINT_HEX(x)        Serial.print(x, HEX)
  #define SERIALPRINT_F(...)        Serial.printf(__VA_ARGS__)
#else
  #define SERIALBEGIN()
  #define SERIALPRINT(x)
  #define SERIALPRINT_LN(x)
  #define SERIALPRINT_HEX(x)
  #define SERIALPRINT_F(...)
#endif

#if defined(DEBUG_PRINT)
  #define DEBUGPRINT(x)         SERIALPRINT(x)
  #define DEBUGPRINT_LN(x)      SERIALPRINT_LN(x)
  #define DEBUGPRINT_HEX(x)     SERIALPRINT_HEX(x)
#else
  #define DEBUGPRINT(x)
  #define DEBUGPRINT_LN(x)
  #define DEBUGPRINT_HEX(x)
#endif

#endif // CONFIG_H