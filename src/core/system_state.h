#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class OperatingMode : uint8_t { OFF, MANUAL, SETPOINT, AUTOTUNE, PROFILE };
enum class SystemStatus : uint8_t { OK, DEGRADED, FAULT };

enum ErrorBit : uint32_t {
  ERROR_NONE = 0,
  ERROR_TC1_OPEN = 1UL << 0,
  ERROR_TC1_SHORT_GND = 1UL << 1,
  ERROR_TC1_SHORT_VCC = 1UL << 2,
  ERROR_TC1_OUTLIER = 1UL << 3,
  ERROR_TC2_OPEN = 1UL << 4,
  ERROR_TC2_SHORT_GND = 1UL << 5,
  ERROR_TC2_SHORT_VCC = 1UL << 6,
  ERROR_TC2_OUTLIER = 1UL << 7,
  ERROR_TC3_OPEN = 1UL << 8,
  ERROR_TC3_SHORT_GND = 1UL << 9,
  ERROR_TC3_SHORT_VCC = 1UL << 10,
  ERROR_TC3_OUTLIER = 1UL << 11,
  ERROR_NO_VALID_TC = 1UL << 12,
  ERROR_OVERTEMP = 1UL << 13,
  ERROR_PROFILE_INVALID = 1UL << 14,
  ERROR_AUTOTUNE_FAILED = 1UL << 15,
  ERROR_INTERNAL = 1UL << 16,
};

struct RuntimeState {
  OperatingMode mode = OperatingMode::OFF;
  SystemStatus status = SystemStatus::FAULT;
  float controlTemperatureC = NAN;
  float setpointC = 25.0f;
  float outputPercent = 0.0f;
  float manualOutputPercent = 0.0f;
  uint8_t validTcCount = 0;
  uint32_t errors = ERROR_NO_VALID_TC;
  uint32_t latchedErrors = ERROR_NONE;
  uint32_t sequence = 0;
  bool temperatureReady = false;
  // Explicit serial UI ownership; USB presence alone never changes this.
  bool serialUiConnected = false;
};

const char *modeName(OperatingMode mode);
const char *statusName(SystemStatus status);
void enterFault(RuntimeState &state, uint32_t error);
bool heatingAllowed(const RuntimeState &state);
