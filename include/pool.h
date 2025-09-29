#pragma once
#include <Arduino.h>

void pool_setup();     // connect/register with DuinoCoin pool(s)
void pool_loop();      // poll, submit shares, handle responses
void pool_update();   // fetch new pool info from server

String get_pool_host();
int get_pool_port();
String get_mining_key();
void set_mining_key(String new_mining_key);

// Optional helpers
bool pool_connected();
