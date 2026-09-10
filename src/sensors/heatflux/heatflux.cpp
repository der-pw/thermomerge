#include "sensors/heatflux/heatflux.h"

#include <math.h>

#include "config/config.h"
HeatFluxSensor::HeatFluxSensor()
    : calibrationFactor_(config::HEAT_FLUX_CALIBRATION_FACTOR),
      calibrationOffsetKwM2_(config::HEAT_FLUX_CALIBRATION_OFFSET_KW_M2) {}

void HeatFluxSensor::begin() {
  engineeringValueKwM2_ = NAN;
  input_.begin();
}

void HeatFluxSensor::update(uint32_t nowMs) {
  input_.update(nowMs);
  if (nowMs - lastUpdateMs_ < config::HEAT_FLUX_INTERVAL_MS) return;
  lastUpdateMs_ = nowMs;

  const float raw = input_.rawValue();
  engineeringValueKwM2_ = input_.enabled() && input_.valid() &&
      calibrationValid() && isfinite(raw)
      ? raw * calibrationFactor_ + calibrationOffsetKwM2_
      : NAN;
}

bool HeatFluxSensor::enabled() const { return input_.enabled(); }
bool HeatFluxSensor::valid() const {
  return input_.enabled() && input_.valid() && isfinite(input_.rawValue());
}
bool HeatFluxSensor::calibrationValid() const {
  return isfinite(calibrationFactor_) && calibrationFactor_ > 0.0f;
}
bool HeatFluxSensor::fault() const { return input_.fault(); }
HeatFluxStatus HeatFluxSensor::status() const {
  if (!enabled()) return HeatFluxStatus::DISABLED;
  if (fault()) return HeatFluxStatus::FAULT;
  return valid() ? HeatFluxStatus::VALID : HeatFluxStatus::NOT_READY;
}
float HeatFluxSensor::rawValue() const { return input_.rawValue(); }

void HeatFluxSensor::setCalibration(float factor, float offsetKwM2) {
  calibrationFactor_ = factor;
  calibrationOffsetKwM2_ = offsetKwM2;
  engineeringValueKwM2_ = NAN;
}
