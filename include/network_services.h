#ifndef NETWORK_SERVICES_H
#define NETWORK_SERVICES_H

#include <Arduino.h>

void wifi_setup();
void ota_setup();
void mdns_setup();

String http_get_string(String URL);

// Returns true if it's time to retry
inline bool shouldTryConnect(uint32_t lastTry, uint8_t tryCount) {
    uint32_t now = millis();
    if (tryCount == 0) return true;

    // Calculate backoff delay: 3 tries per delay group
    // 250, 250, 250, 500, 500 ... 1000, 1000, 1000, 2000...
    uint32_t delayMs = min(2000UL, 250UL * (1UL << ((tryCount - 1) / 3)));
    if (now - lastTry >= delayMs) return true;

    return false;
}

#endif // NETWORK_SERVICES_H
