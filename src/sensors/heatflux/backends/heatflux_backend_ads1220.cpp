#include "sensors/heatflux/backends/heatflux_backend_ads1220.h"

#include <math.h>

#include "config/config.h"

HeatFluxBackendAds1220::HeatFluxBackendAds1220()
    : adc_(SPI, config::ADS1220_CS_GPIO, config::ADS1220_DRDY_GPIO,
           config::ADS1220_SPI_CLOCK_HZ) {}

void HeatFluxBackendAds1220::begin() {
  adc_.begin();
  sampleValid_ = false;
  responded_ = false;
  voltageV_ = NAN;
  adc_.reset();
  deadlineMs_ = millis() + 1;
  state_ = State::WAIT_AFTER_RESET;
}

void HeatFluxBackendAds1220::update(uint32_t nowMs) {
  switch (state_) {
    case State::WAIT_AFTER_RESET:
      if (static_cast<int32_t>(nowMs - deadlineMs_) < 0) return;
      state_ = State::CONFIGURE;
      [[fallthrough]];
    case State::CONFIGURE:
      if (!adc_.configure(settings())) {
        enterFault(nowMs);
        return;
      }
      responded_ = true;
      state_ = State::IDLE;
      [[fallthrough]];
    case State::IDLE:
      if (!adc_.startSync()) {
        enterFault(nowMs);
        return;
      }
      deadlineMs_ = nowMs + config::ADS1220_CONVERSION_TIMEOUT_MS;
      state_ = State::WAITING;
      return;
    case State::WAITING:
      if (adc_.conversionReady()) {
        if (!adc_.readVoltage(voltageV_, &rawCode_)) {
          enterFault(nowMs);
          return;
        }
        sampleValid_ = isfinite(voltageV_);
        state_ = State::IDLE;
      } else if (static_cast<int32_t>(nowMs - deadlineMs_) >= 0) {
        enterFault(nowMs);
      }
      return;
    case State::FAULT:
      if (static_cast<int32_t>(nowMs - deadlineMs_) >= 0) {
        sampleValid_ = false;
        voltageV_ = NAN;
        adc_.reset();
        deadlineMs_ = nowMs + 1;
        state_ = State::WAIT_AFTER_RESET;
      }
      return;
  }
}

bool HeatFluxBackendAds1220::enabled() const { return true; }
bool HeatFluxBackendAds1220::valid() const { return sampleValid_; }
bool HeatFluxBackendAds1220::fault() const { return state_ == State::FAULT; }
float HeatFluxBackendAds1220::rawValue() const { return sampleValid_ ? voltageV_ : NAN; }
ADS1220::Settings HeatFluxBackendAds1220::settings() const {
  ADS1220::Settings result;
  result.mux = ADS1220::Mux::AIN0_AIN1;
  result.gain = config::ADS1220_PGA_GAIN == 64 ? ADS1220::Gain::GAIN_64 : ADS1220::Gain::GAIN_32;
  result.dataRate = ADS1220::DataRate::SPS_20;
  result.reference = ADS1220::Reference::INTERNAL_2V048;
  result.conversionMode = ADS1220::ConversionMode::SINGLE_SHOT;
  result.lineFilter = ADS1220::LineFilter::REJECT_50_AND_60_HZ;
  result.referenceVoltage = config::ADS1220_INTERNAL_REFERENCE_V;
  return result;
}

void HeatFluxBackendAds1220::enterFault(uint32_t nowMs) {
  sampleValid_ = false;
  voltageV_ = NAN;
  deadlineMs_ = nowMs + config::ADS1220_RETRY_INTERVAL_MS;
  state_ = State::FAULT;
}
