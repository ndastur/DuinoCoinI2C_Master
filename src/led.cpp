#include "led.h"

// Neo Pixels
#if defined(LED_MODE) && LED_MODE == 2
  #define RGB_LED_DATA  18
  Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, 15, NEO_GRB + NEO_KHZ800);
#endif

/// Initate the LED
void ledInit() {
#if defined(LED_MODE) && LED_MODE == 1
  pinMode(LED_BUILTIN, OUTPUT);
#endif

#if defined(LED_MODE) && LED_MODE == 2
    // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
    // Any other board, you can remove this part (but no harm leaving it):
    #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
        clock_prescale_set(clock_div_1);
    #endif
  pixels.begin();
  pixels.show();
  #ifdef BRIGHTNESS
    pixels.setBrightness(BRIGHTNESS);
  #endif
#endif

#if defined(LED_MODE) && LED_MODE == 3
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
#endif

#if defined(LED_MODE) && LED_MODE == 4
  // FastLED.addLeds<APA106, RGB_LED_DATA, RGB>(leds, NUM_LEDS);
  // leds[0] = CRGB::Red; FastLED.show();  delay(500);
  // leds[0] = CRGB::Blue; FastLED.show();  delay(500);
  
  // // Now turn the LED off, then pause
  // leds[0] = CRGB::Black; FastLED.show(); delay(500);
#endif
}

void ledSetupUpFinished() {
#if defined(LED_MODE) && LED_MODE == 1
    blink(BLINK_SETUP_COMPLETE);
#endif

#if defined(LED_MODE) && LED_MODE == 2
  pixels.setPixelColor(0, pixels.Color(150, 0, 0)); pixels.show(); delay(200);
  pixels.setPixelColor(0, pixels.Color(0, 150, 0)); pixels.show(); delay(200);
  pixels.setPixelColor(0, pixels.Color(0, 0, 150)); pixels.show(); delay(200);
  pixels.setPixelColor(0, pixels.Color(150, 0, 0)); pixels.show(); delay(200);
  pixels.setPixelColor(0, pixels.Color(150, 0, 0)); pixels.show(); delay(200);
  pixels.clear(); pixels.show();
#endif

#if defined(LED_MODE) && LED_MODE == 3
  // blink(2, LED_RED);
  // blink(2, LED_BLUE);
  // blink(2, LED_GREEN);
  // blink(2, LED_YELLOW);
#endif
}

void blink(uint8_t count, uint32_t color) {
#if defined(LED_MODE) && LED_MODE == 1
uint8_t pin = LED_BUILTIN   // TODO base on colour
uint8_t state = HIGH;
  for (int x = 0; x < (count << 1); ++x) {
    digitalWrite(pin, state ^= HIGH);
    delay(60);
  }
  digitalWrite(pin, LOW);    
#endif

#if defined(LED_MODE) && LED_MODE == 2
  for (int x = 0; x < (count << 1); ++x) {
    pixels.setPixelColor(0, color);
    pixels.show();
    delay(60);
    pixels.clear(); pixels.show();
    delay(60);
  }
#endif
}

void blinkStatus(BLINK_STATUSES b) {
  switch (b)
  {
  case BLINK_FLASH:
    blink(1);
    break;
  case BLINK_SETUP_COMPLETE:
    blink(2);
    break;
  case BLINK_CLIENT_CONNECT:
    blink(2);
    break;
  case BLINK_RESET_DEVICE:
    blink(5);
    break;
  case BLINK_SHARE_SOLVED:
    #if defined(LED_MODE) && LED_MODE == 2
      blink(2, 0x00FFFF00);  // yellow
    #endif
    break;
  case BLINK_SLAVE_SHARE_GOOD:
    #if defined(LED_MODE) && LED_MODE == 2
      blink(2, 0x000000FF);  // green
    #endif
    break;
  case BLINK_SHARE_GOOD:
    #if defined(LED_MODE) && LED_MODE == 2
      blink(2, 0x000000FF);  // green
    #endif
    break;
  case BLINK_SHARE_BLOCKFOUND:
    #if defined(LED_MODE) && LED_MODE == 2
      blink(1, 0x000000FF);  // blue
      blink(1, 0x0000FF00);  // green
      blink(1, 0x00FF0000);  // red
      blink(1, 0x000000FF);  // blue
      blink(1, 0x0000FF00);  // green
    #endif
    break;
  case BLINK_SHARE_ERROR:
    #if defined(LED_MODE) && LED_MODE == 2
      blink(5, 0x00FF0000);  // red
    #endif
    break;
  
  default:
    break;
  }
}

