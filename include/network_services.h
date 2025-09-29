#ifndef NETWORK_SERVICES_H
#define NETWORK_SERVICES_H

#include <Arduino.h>

void wifi_setup();
void ota_setup();
void mdns_setup();

String http_get_string(String URL);

#endif // NETWORK_SERVICES_H
