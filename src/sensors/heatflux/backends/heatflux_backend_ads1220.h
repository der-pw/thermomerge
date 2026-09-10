#pragma once

#include "drivers/ads1220.h"
class HeatFluxBackendAds1220 final {
 public:
  HeatFluxBackendAds1220();

  void begin();
  void update(uint32_t nowMs);
  bool enabled() const;
  bool valid() const;
  bool fault() const;
  float rawValue() const;

  int32_t rawCode() const { return rawCode_; }
  ADS1220::Error adcError() const { return adc_.error(); }

 private:
  enum class State : uint8_t { WAIT_AFTER_RESET, CONFIGURE, IDLE, WAITING, FAULT };

  ADS1220 adc_;
  State state_ = State::WAIT_AFTER_RESET;
  uint32_t deadlineMs_ = 0;
  int32_t rawCode_ = 0;
  float voltageV_ = NAN;
  bool sampleValid_ = false;

  ADS1220::Settings settings() const;
  void enterFault(uint32_t nowMs);
};
