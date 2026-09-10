#pragma once

#include <Arduino.h>

#include "control/controller.h"
#include "core/system_state.h"
#include "sensors/heatflux/heatflux.h"

class PersistentSettings {
 public:
  void begin(PIDController &pid, RuntimeState &state, HeatFluxSensor &heatFlux);
  void markDirty(uint32_t nowMs);
  void markSetpointDirty(uint32_t nowMs, float setpointC);
  void markHeatFluxDirty(uint32_t nowMs, const HeatFluxSensor &heatFlux);
  void update(uint32_t nowMs, const PIDController &pid, const RuntimeState &state,
              const HeatFluxSensor &heatFlux);

 private:
  bool dirty_ = false;
  bool available_ = false;
  uint32_t dirtySinceMs_ = 0;
  float normalSetpointC_ = 25.0f;
  float heatFluxFactor_ = NAN;
  float heatFluxOffsetKwM2_ = 0.0f;
};
