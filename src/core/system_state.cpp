#include "core/system_state.h"

const char *modeName(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::OFF: return "OFF";
    case OperatingMode::MANUAL: return "MANUAL";
    case OperatingMode::SETPOINT: return "SETPOINT";
    case OperatingMode::AUTOTUNE: return "AUTOTUNE";
    case OperatingMode::PROFILE: return "PROFILE";
  }
  return "OFF";
}

const char *statusName(SystemStatus status) {
  switch (status) {
    case SystemStatus::OK: return "OK";
    case SystemStatus::DEGRADED: return "DEGRADED";
    case SystemStatus::FAULT: return "FAULT";
  }
  return "FAULT";
}

void enterFault(RuntimeState &state, uint32_t error) {
  state.errors |= error;
  state.latchedErrors |= error;
  state.status = SystemStatus::FAULT;
  state.outputPercent = 0.0f;
}

bool heatingAllowed(const RuntimeState &state) {
  return state.status != SystemStatus::FAULT && state.temperatureReady &&
         (state.mode == OperatingMode::MANUAL || state.mode == OperatingMode::SETPOINT ||
          state.mode == OperatingMode::AUTOTUNE || state.mode == OperatingMode::PROFILE);
}
