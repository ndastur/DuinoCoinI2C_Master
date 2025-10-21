#include "web.h"
#include "config.h"

#include <LittleFS.h>
#include <ESPAsyncWebServer.h>


AsyncWebServer server(80);
// If you use WS/SSE, declare them here and expose helpers in web.h as needed.
static AsyncWebSocket ws("/ws");

const char* http_username = "admin";
const char* http_password = "admin";

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);

static void register_routes() {
  // Serve everything from LittleFS root, defaulting to index.htm
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.htm");

  // Example JSON endpoint (adjust to your data structures)
  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"app\":\"" APP_NAME "\",\"ver\":\"" APP_VERSION "\"}";
    req->send(200, "application/json", json);
  });

  // If using WS/SSE, register handlers here
  server.addHandler(&ws);
}

void web_setup() {
  if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
    Serial.println("[FS] LittleFS mount failed, formatting...");
    LittleFS.begin(true, "/littlefs", 10, "littlefs");  // one-time rescue format
  }

  // if (!LittleFS.begin()) {
  //   Serial.println("[FS] LittleFS mount failed");
  //   return;
  // }
  register_routes();
  server.begin();
  Serial.println("[WEB] Server started");
}

void web_loop() {
  // If you use websockets, you might do periodic cleanup, e.g.:
  // ws.cleanupClients();
}
void web_end() {
  server.end();
  LittleFS.end();
  Serial.println("[WEB] Server stopped and FS unmounted");
}

void ws_send_all(String payload)
{
  if (ws.count() > 0)
  {
    ws.textAll(payload);
  }
}