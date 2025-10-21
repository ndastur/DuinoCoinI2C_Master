#ifndef _UTILS_H
#define _UTILS_H

#include <Arduino.h>

const char * getChipId();
uint8_t* hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength);

#endif  // _UTILS_H
