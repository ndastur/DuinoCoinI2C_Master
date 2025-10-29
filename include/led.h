#ifndef _LED_H
#define _LED_H

#if defined(LED_MODE) && LED_MODE == 1
  #ifndef LED_BUILTIN
    #define LED_BUILTIN LED_RED
  #endif
#endif

#if defined(LED_MODE) && LED_MODE == 2
  #include <Adafruit_NeoPixel.h>
  #define BRIGHTNESS 64 // Set BRIGHTNESS 1 to 255
#endif

// My own 4 led individual board
#if defined(LED_MODE) && LED_MODE == 3
  #define LED_RED     32
  #define LED_BLUE    33
  #define LED_GREEN   25
  #define LED_YELLOW  26
#endif

#if defined(LED_MODE) && LED_MODE == 4
  #include <FastLed.h>
#endif

enum BLINK_STATUSES : byte {
  BLINK_FLASH,
  BLINK_SETUP_COMPLETE,
  BLINK_CLIENT_CONNECT,
  BLINK_RESET_DEVICE,
  BLINK_SHARE_SOLVED,
  BLINK_SHARE_GOOD,
  BLINK_SLAVE_SHARE_GOOD,
  BLINK_SHARE_BLOCKFOUND,
  BLINK_SHARE_ERROR
};

void ledInit();
void ledSetupUpFinished();
void blink(uint8_t count, uint32_t color = 0x00A00000);
void blinkStatus(BLINK_STATUSES b);
#endif