#pragma once

#include <Arduino.h>

class Heater {
 public:
  void begin();
  void setDemand(float percent);
  void update(uint32_t nowMs, bool permitted);
  void forceOff();
  float demand() const { return demandPercent_; }
  bool isOn() const { return on_; }

 private:
  float demandPercent_ = 0.0f;
  uint32_t windowStartMs_ = 0;
  bool on_ = false;
  void write(bool on);
};
