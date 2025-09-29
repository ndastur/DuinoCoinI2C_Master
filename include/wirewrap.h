#ifndef WIREWRAP_H
#define WIREWRAP_H
#include <Arduino.h>

// Avoid naming this header/file "Wire.h/cpp" to not clash with Arduino core.
void wirewrap_setup();
void wirewrap_loop();

void wire_start();

void wire_read_all();
void wire_send_all(String message);

boolean wire_exists(byte address);

void wire_send_job(byte address, String lastblockhash, String newblockhash, int difficulty);
void wire_sendln(byte address, String message);
void wire_send_cmd(byte address, String message);
void wire_send(byte address, String message);

String wire_readline(int address);

boolean wire_run_every(unsigned long interval);
boolean wire_run_every_micro(unsigned long interval);

#endif // WIREWRAP_H