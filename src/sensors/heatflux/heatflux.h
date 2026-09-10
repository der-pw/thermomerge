#pragma once

#include <Arduino.h>

#include "sensors/heatflux/backends/heatflux_backend_ads1220.h"

enum class HeatFluxStatus : uint8_t { DISABLED, NOT_READY, VALID, FAULT };

class HeatFluxSensor {
 public:
  HeatFluxSensor();

  void begin();
  void update(uint32_t nowMs);
  bool enabled() const;
  bool valid() const;
  bool calibrationValid() const;
  bool fault() const;
  HeatFluxStatus status() const;
  float rawValue() const;
  float engineeringValue() const { return engineeringValueKwM2_; }
  float valueKwM2() const { return engineeringValue(); }

  float calibrationFactor() const { return calibrationFactor_; }
  float calibrationOffset() const { return calibrationOffsetKwM2_; }
  void setCalibration(float factor, float offsetKwM2);

 private:
  HeatFluxBackendAds1220 input_;
  uint32_t lastUpdateMs_ = 0;
  float engineeringValueKwM2_ = NAN;
  float calibrationFactor_ = 1.0f;
  float calibrationOffsetKwM2_ = 0.0f;
};
