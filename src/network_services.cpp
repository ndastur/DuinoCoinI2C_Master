#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "config.h"
#include "network_services.h"

void wifi_setup() {
  SERIALPRINT_LN("[WIFI] Connecting to: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA); // Setup ESP in client mode

  if (String(WIFI_SSID) == "")
    WiFi.begin();
  else
    WiFi.begin(WIFI_SSID, WIFI_PASS);

  int connectTime = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    SERIALPRINT(".");
    if(connectTime++ > 15) {
    #if defined(WIFI_SSID2) && defined(WIFI_PASS2)
      SERIALPRINT_LN("[WIFI] Connecting to: " + String(WIFI_SSID2));
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID2, WIFI_PASS2);
    #endif
    }
    if(connectTime > 150) {
      SERIALPRINT_LN("Connection to WiFi failed.");
      for(;;);
      return;
    }
  }

  SERIALPRINT_LN("\n[WIFI] Connected to WiFi!");
  SERIALPRINT_LN("[WIFI] Local IP address: " + WiFi.localIP().toString());

  mdns_setup();
}

void mdns_setup() {
  if (!MDNS.begin(MDNS_RIG_IDENTIFIER)) {
    SERIALPRINT_LN("mDNS can't be configured without am identifier");
  }
  MDNS.addService("http", "tcp", 80);
  SERIALPRINT_LN("Configured mDNS for dashboard on http://" 
                + String(MDNS_RIG_IDENTIFIER)
                + ".local (or http://"
                + WiFi.localIP().toString()
                + ")\n");
}

void ota_setup() {
  ArduinoOTA.onStart([]() { // Prepare OTA stuff
    SERIALPRINT_LN("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    SERIALPRINT_LN("[OTA] End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  // Just support ESP32 for now
  // if you need ESP8266 support, please add it yourself, it's so old now :)
  char hostname[32];
  sprintf(hostname, "MinerESP32-Async-%06llx", ESP.getEfuseMac());
  ArduinoOTA.setHostname(hostname);

  ArduinoOTA.begin();
}

String http_get_string(String URL)
{
  String payload = "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (http.begin(client, URL))
  {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
      payload = http.getString();
    }
    else
    {
      SERIALPRINT_LN("[HTTP] GET... failed, error: ");
      SERIALPRINT_LN(http.errorToString(httpCode).c_str());
    }
    http.end();
  }
  return payload;
}
