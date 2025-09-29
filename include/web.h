#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Expose the server object and (optionally) a WebSocket if you use one
extern AsyncWebServer server;

void web_setup();     // mount FS, register routes, start server
void web_loop();      // call if you have websockets/SSE housekeeping
void web_end();       // stop server, unmount FS

void ws_send_all(String payload);

#endif // WEB_H