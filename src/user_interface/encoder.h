#pragma once

#include <Arduino.h>

struct EncoderEvents {
  int16_t detents = 0;
  bool shortPress = false;
  bool longPress = false;
};

class Encoder {
 public:
  void begin();
  EncoderEvents update(uint32_t nowMs);

 private:
  int32_t processedPosition_ = 0;
  bool rawButton_ = false;
  bool stableButton_ = false;
  bool longReported_ = false;
  uint32_t rawChangedMs_ = 0;
  uint32_t pressedMs_ = 0;
};
