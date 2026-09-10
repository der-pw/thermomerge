#pragma once

#include <Adafruit_MAX31855.h>
#include <Arduino.h>

#include "core/system_state.h"

enum class SensorStatus : uint8_t { OK, FAULT, OUTLIER, RECOVERING };

struct ThermocoupleChannel {
  float temperatureC = NAN;
  uint8_t faultFlags = 0;
  bool electricallyValid = false;
  bool plausible = false;
  uint8_t faultCount = 0;
  uint8_t recoveryCount = 0;
  SensorStatus status = SensorStatus::RECOVERING;
};

struct ThermocoupleResult {
  float controlTemperatureC = NAN;
  uint8_t validCount = 0;
  uint32_t errorBits = ERROR_NONE;
};

class Thermocouples {
 public:
  Thermocouples();
  void begin();
  ThermocoupleResult update();
  const ThermocoupleChannel &channel(uint8_t index) const { return channels_[index]; }
  static const char *statusName(SensorStatus status);

 private:
  Adafruit_MAX31855 sensors_[3];
  ThermocoupleChannel channels_[3];

  void readChannel(uint8_t index);
  void evaluatePlausibility();
  uint32_t channelErrors(uint8_t index) const;
};
