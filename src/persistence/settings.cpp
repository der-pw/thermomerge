#include "persistence/settings.h"

#include <EEPROM.h>
#include <math.h>

#include "config/config.h"

namespace {
constexpr uint32_t SETTINGS_MAGIC = 0x54484D47UL;  // THMG
constexpr uint16_t SETTINGS_VERSION = 2;
constexpr size_t EEPROM_SIZE = 128;

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  float kp;
  float ki;
  float kd;
  float setpointC;
  float heatFluxFactorKwM2PerV;
  float heatFluxOffsetKwM2;
  uint32_t checksum;
};

uint32_t checksum(const StoredSettings &settings) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&settings);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(StoredSettings, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool valid(const StoredSettings &s) {
  return s.magic == SETTINGS_MAGIC && s.version == SETTINGS_VERSION &&
         s.checksum == checksum(s) && isfinite(s.kp) && isfinite(s.ki) && isfinite(s.kd) &&
         s.kp >= 0.0f && s.ki >= 0.0f && s.kd >= 0.0f &&
         isfinite(s.setpointC) && s.setpointC >= config::MIN_SETPOINT_C &&
         s.setpointC <= config::MAX_SETPOINT_C &&
         isfinite(s.heatFluxFactorKwM2PerV) && s.heatFluxFactorKwM2PerV > 0.0f &&
         isfinite(s.heatFluxOffsetKwM2);
}
}  // namespace

void PersistentSettings::begin(PIDController &pid, RuntimeState &state,
                               HeatFluxSensor &heatFlux) {
  EEPROM.begin(EEPROM_SIZE);
  available_ = true;
  heatFluxFactor_ = config::HEAT_FLUX_CALIBRATION_FACTOR;
  heatFluxOffsetKwM2_ = config::HEAT_FLUX_CALIBRATION_OFFSET_KW_M2;
  StoredSettings stored{};
  EEPROM.get(0, stored);
  if (valid(stored)) {
    pid.setTunings(stored.kp, stored.ki, stored.kd);
    state.setpointC = stored.setpointC;
    heatFluxFactor_ = stored.heatFluxFactorKwM2PerV;
    heatFluxOffsetKwM2_ = stored.heatFluxOffsetKwM2;
  }
  heatFlux.setCalibration(heatFluxFactor_, heatFluxOffsetKwM2_);
  normalSetpointC_ = state.setpointC;
}

void PersistentSettings::markDirty(uint32_t nowMs) {
  dirty_ = true;
  dirtySinceMs_ = nowMs;
}

void PersistentSettings::markSetpointDirty(uint32_t nowMs, float setpointC) {
  normalSetpointC_ = setpointC;
  markDirty(nowMs);
}

void PersistentSettings::markHeatFluxDirty(uint32_t nowMs,
                                            const HeatFluxSensor &heatFlux) {
  heatFluxFactor_ = heatFlux.calibrationFactor();
  heatFluxOffsetKwM2_ = heatFlux.calibrationOffset();
  markDirty(nowMs);
}

void PersistentSettings::update(uint32_t nowMs, const PIDController &pid,
                                const RuntimeState &state,
                                const HeatFluxSensor &heatFlux) {
  if (!available_ || !dirty_ || nowMs - dirtySinceMs_ < config::SETTINGS_SAVE_DELAY_MS) return;
  StoredSettings stored{};
  stored.magic = SETTINGS_MAGIC;
  stored.version = SETTINGS_VERSION;
  pid.getTunings(stored.kp, stored.ki, stored.kd);
  (void)state;
  stored.setpointC = normalSetpointC_;
  stored.heatFluxFactorKwM2PerV = heatFlux.calibrationFactor();
  stored.heatFluxOffsetKwM2 = heatFlux.calibrationOffset();
  stored.checksum = checksum(stored);
  EEPROM.put(0, stored);
  if (EEPROM.commit()) dirty_ = false;
}
