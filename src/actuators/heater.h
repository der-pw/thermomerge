#pragma once

#include <Arduino.h>

class Heater {
 public:
  void begin();
  void setDemand(float percent);
  bool startTest(uint32_t nowMs, bool permitted);
  void cancelTest();
  bool testActive() const { return testActive_; }
  void update(uint32_t nowMs, bool permitted, bool testPermitted);
  void forceOff();
  float demand() const { return demandPercent_; }
  bool isOn() const { return on_; }

 private:
  float demandPercent_ = 0.0f;
  uint32_t windowStartMs_ = 0;
  bool on_ = false;
  uint32_t testPhaseStartMs_ = 0;
  uint8_t testPulsesCompleted_ = 0;
  bool testActive_ = false;
  bool testOn_ = false;
  void write(bool on);
};
