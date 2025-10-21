#include "utils.h"

String _chipId = "";

// Get and set the chip ID from fuses
const char * getChipId() {
  if(_chipId.isEmpty()) {
    uint64_t chip_id = ESP.getEfuseMac();
    uint16_t chip = (uint16_t)(chip_id >> 32); // Prepare to print a 64 bit value into a char array
    char fullChip[23];
    snprintf(fullChip, 23, "%04x%08x", chip,
          (uint32_t)chip_id); // Store the (actually) 48 bit chip_id into a char array

    _chipId = String(fullChip);
    }

  return _chipId.c_str();
}

// https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TypeConversion.cpp
const char base36Chars[36] PROGMEM = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};

const uint8_t base36CharValues[75] PROGMEM{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,                                                                        // 0 to 9
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0, // Upper case letters
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35                    // Lower case letters
};

uint8_t* hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength) {
    assert(hexString.length() >= arrayLength * 2);
    const char *hexChars = hexString.c_str();
    for (uint32_t i = 0; i < arrayLength; ++i) {
        uint8Array[i] = (pgm_read_byte(base36CharValues + hexChars[i * 2] - '0') << 4) + pgm_read_byte(base36CharValues + hexChars[i * 2 + 1] - '0');
    }
    return uint8Array;
}
