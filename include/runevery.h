// RunEvery.h
#ifndef RUNEVERY_H
#define RUNEVERY_H

#include <Arduino.h>

class RunEvery {
  public:
    RunEvery(unsigned long intervalMs)
      : _interval(intervalMs), _previousMillis(0) {}

    // Call this in loop(), returns true if interval has passed
    bool shouldRun() {
      unsigned long currentMillis = millis();
      if (currentMillis - _previousMillis >= _interval) {
        _previousMillis = currentMillis;
        return true;
      }
      return false;
    }

    // Change the interval at runtime
    void setInterval(unsigned long intervalMs) {
      _interval = intervalMs;
    }

    unsigned long getInterval() const {
      return _interval;
    }

    void reset() {
      _previousMillis = millis();
    }

  private:
    unsigned long _interval;
    unsigned long _previousMillis;
};

/*
  Usage:
  RunEvery blink(500); // ~500 ms

  void loop() {
    if (blink.due()) {
      // toggle LED
      }
  }
*/
struct RunEveryStruct {
  unsigned long last = 0;   // 4 bytes
  unsigned long interval;   // 4 bytes

  explicit RunEveryStruct(unsigned long ms) : interval(ms) {}

  inline bool due() {
    unsigned long now = millis();
    if (now - last >= interval) {
      last = now;
      return true;
    }
    return false;
  }
};

#endif // RUNEVERY_H
