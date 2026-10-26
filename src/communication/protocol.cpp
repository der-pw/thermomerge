#include "communication/protocol.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config/config.h"

namespace {
bool parseFloatValue(const char *text, float &value) {
  if (!text || !*text) return false;
  char *end = nullptr;
  errno = 0;
  value = strtof(text, &end);
  return errno == 0 && end != text && *end == '\0' && isfinite(value);
}

bool parseUnsignedValue(const char *text, uint32_t &value) {
  if (!text || !*text || *text == '-') return false;
  char *end = nullptr;
  errno = 0;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return false;
  value = parsed;
  return true;
}

const char *valueFor(const char *token, const char *key) {
  const size_t length = strlen(key);
  return token && strncmp(token, key, length) == 0 && token[length] == '='
             ? token + length + 1 : nullptr;
}

}  // namespace

size_t BufferedSerialWriter::write(uint8_t value) {
  return write(&value, 1);
}

size_t BufferedSerialWriter::write(const uint8_t *buffer, size_t size) {
  size_t written = 0;
  while (written < size && count_ < config::SERIAL_TX_BUFFER_SIZE) {
    buffer_[tail_] = buffer[written++];
    tail_ = (tail_ + 1) % config::SERIAL_TX_BUFFER_SIZE;
    ++count_;
  }
  return written;
}

void BufferedSerialWriter::service() {
  if (!Serial || count_ == 0) return;
  const int available = Serial.availableForWrite();
  if (available <= 0) return;
  const uint16_t untilWrap = config::SERIAL_TX_BUFFER_SIZE - head_;
  const uint16_t contiguous = count_ < untilWrap ? count_ : untilWrap;
  const uint16_t availableCount = static_cast<uint16_t>(available);
  const uint16_t amount = contiguous < availableCount ? contiguous : availableCount;
  const size_t written = Serial.write(buffer_ + head_, amount);
  head_ = (head_ + written) % config::SERIAL_TX_BUFFER_SIZE;
  count_ -= written;
}

SerialProtocol::SerialProtocol(RuntimeState &state, Thermocouples &thermocouples,
                               PIDController &pid, PIDAutotune &autotune,
                               TemperatureProfile &profile, PersistentSettings &settings,
                               Heater &heater, HeatFluxSensor &heatFlux)
    : state_(state), thermocouples_(thermocouples), pid_(pid), autotune_(autotune),
      profile_(profile), settings_(settings), heater_(heater), heatFlux_(heatFlux) {}

void SerialProtocol::replyError(const __FlashStringHelper *message) {
  tx_.print(F("ERR,"));
  tx_.println(message);
}

void SerialProtocol::printFloatOrNan(float value, uint8_t decimals) {
  if (isfinite(value)) tx_.print(value, decimals);
  else tx_.print(F("nan"));
}

void SerialProtocol::poll(uint32_t nowMs) {
  uint8_t budget = config::SERIAL_RX_BUDGET_PER_LOOP;
  while (budget-- > 0 && Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow_) replyError(F("RX_OVERFLOW"));
      else if (length_ == 0) replyError(F("EMPTY"));
      else {
        buffer_[length_] = '\0';
        handleLine(buffer_, nowMs);
      }
      length_ = 0;
      overflow_ = false;
    } else if (!overflow_) {
      if (length_ < sizeof(buffer_) - 1) buffer_[length_++] = c;
      else overflow_ = true;
    }
  }
}

void SerialProtocol::handleLine(char *line, uint32_t nowMs) {
  char *tokens[12];
  uint8_t count = 0;
  char *save = nullptr;
  for (char *token = strtok_r(line, ",", &save); token && count < 12;
       token = strtok_r(nullptr, ",", &save)) tokens[count++] = token;
  if (count == 0) { replyError(F("EMPTY")); return; }

  if (strcmp(tokens[0], "SET") == 0) handleSet(tokens, count, nowMs);
  else if (strcmp(tokens[0], "UI") == 0) handleUi(tokens, count);
  else if (strcmp(tokens[0], "MODE") == 0) handleMode(tokens, count);
  else if (strcmp(tokens[0], "AUTOTUNE") == 0) handleAutotune(tokens, count, nowMs);
  else if (strcmp(tokens[0], "PROFILE") == 0) handleProfile(tokens, count, nowMs);
  else if (strcmp(tokens[0], "GET") == 0 && count == 2 && strcmp(tokens[1], "STATUS") == 0)
    printStatus(nowMs, false);
  else if (strcmp(tokens[0], "GET") == 0 && count == 2 && strcmp(tokens[1], "PID") == 0) {
    float kp, ki, kd; pid_.getTunings(kp, ki, kd);
    tx_.print(F("OK,KP=")); tx_.print(kp, 6);
    tx_.print(F(",KI=")); tx_.print(ki, 6);
    tx_.print(F(",KD=")); tx_.println(kd, 6);
  } else if (strcmp(tokens[0], "GET") == 0 && count == 2 && strcmp(tokens[1], "HEATFLUX") == 0) {
    tx_.print(F("OK,HF_FACTOR=")); printFloatOrNan(heatFlux_.calibrationFactor(), 6);
    tx_.print(F(",HF_OFFSET=")); printFloatOrNan(heatFlux_.calibrationOffset(), 6);
    tx_.print(F(",HF_CAL=")); tx_.println(heatFlux_.calibrationValid() ? F("VALID") : F("UNSET"));
  } else if (strcmp(tokens[0], "FAULT") == 0 && count == 2 && strcmp(tokens[1], "CLEAR") == 0) {
    if ((state_.latchedErrors & ERROR_OVERTEMP) &&
        (!isfinite(state_.controlTemperatureC) || state_.controlTemperatureC >= config::MAX_TEMPERATURE_C - 20.0f)) {
      replyError(F("FAULT_STILL_ACTIVE"));
    } else {
      state_.latchedErrors = ERROR_NONE;
      state_.mode = OperatingMode::OFF;
      tx_.println(F("OK,FAULT_CLEARED"));
    }
  } else replyError(F("UNKNOWN_COMMAND"));
}

void SerialProtocol::handleUi(char **tokens, uint8_t count) {
  if (count != 2) { replyError(F("BAD_ARGUMENTS")); return; }
  if (strcmp(tokens[1], "CONNECT") == 0) {
    state_.serialUiConnected = true;
    tx_.println(F("OK,UI=CONNECTED"));
  } else if (strcmp(tokens[1], "DISCONNECT") == 0) {
    state_.serialUiConnected = false;
    tx_.println(F("OK,UI=LOCAL"));
  } else {
    replyError(F("BAD_UI_COMMAND"));
  }
}

void SerialProtocol::handleSet(char **tokens, uint8_t count, uint32_t nowMs) {
  if (count != 2) { replyError(F("BAD_ARGUMENTS")); return; }
  const char *equals = strchr(tokens[1], '=');
  if (!equals) { replyError(F("BAD_ARGUMENTS")); return; }
  float value;
  if (!parseFloatValue(equals + 1, value)) { replyError(F("BAD_VALUE")); return; }

  if (strncmp(tokens[1], "SP=", 3) == 0) {
    if (state_.mode == OperatingMode::PROFILE) { replyError(F("PROFILE_OWNS_SETPOINT")); return; }
    if (value < config::MIN_SETPOINT_C || value > config::MAX_SETPOINT_C) { replyError(F("OUT_OF_RANGE")); return; }
    state_.setpointC = value;
    settings_.markSetpointDirty(nowMs, state_.setpointC);
    tx_.print(F("OK,SP=")); tx_.println(state_.setpointC, 1);
    return;
  }
  if (strncmp(tokens[1], "OUT=", 4) == 0) {
    if (value < 0.0f || value > 100.0f) { replyError(F("OUT_OF_RANGE")); return; }
    state_.manualOutputPercent = value;
    if (value == 0.0f) heater_.forceOff();
    tx_.print(F("OK,OUT=")); tx_.println(value, 1);
    return;
  }
  if (strncmp(tokens[1], "HF_SENS=", 8) == 0) {
    if (value <= 0.0f) { replyError(F("OUT_OF_RANGE")); return; }
    // Certificate values are conventionally specified in mV/(kW/m²).
    heatFlux_.setCalibration(1000.0f / value, heatFlux_.calibrationOffset());
    settings_.markHeatFluxDirty(nowMs, heatFlux_);
    tx_.print(F("OK,HF_SENS=")); tx_.println(value, 6);
    return;
  }
  if (strncmp(tokens[1], "HF_FACTOR=", 10) == 0) {
    if (value <= 0.0f) { replyError(F("OUT_OF_RANGE")); return; }
    heatFlux_.setCalibration(value, heatFlux_.calibrationOffset());
    settings_.markHeatFluxDirty(nowMs, heatFlux_);
    tx_.print(F("OK,HF_FACTOR=")); tx_.println(value, 6);
    return;
  }
  if (strncmp(tokens[1], "HF_OFFSET=", 10) == 0) {
    heatFlux_.setCalibration(heatFlux_.calibrationFactor(), value);
    settings_.markHeatFluxDirty(nowMs, heatFlux_);
    tx_.print(F("OK,HF_OFFSET=")); tx_.println(value, 6);
    return;
  }

  float kp, ki, kd; pid_.getTunings(kp, ki, kd);
  if (strncmp(tokens[1], "KP=", 3) == 0) kp = value;
  else if (strncmp(tokens[1], "KI=", 3) == 0) ki = value;
  else if (strncmp(tokens[1], "KD=", 3) == 0) kd = value;
  else { replyError(F("UNKNOWN_PARAMETER")); return; }
  if (value < 0.0f || value > 10000.0f) { replyError(F("OUT_OF_RANGE")); return; }
  pid_.setTunings(kp, ki, kd);
  settings_.markDirty(nowMs);
  tx_.println(F("OK,PID_UPDATED"));
}

void SerialProtocol::handleMode(char **tokens, uint8_t count) {
  if (count != 2) { replyError(F("BAD_ARGUMENTS")); return; }
  if (strcmp(tokens[1], "OFF") == 0) {
    profile_.stop(); autotune_.stop(); state_.mode = OperatingMode::OFF;
    state_.outputPercent = 0.0f; heater_.forceOff(); tx_.println(F("OK,MODE=OFF")); return;
  }
  if (state_.status == SystemStatus::FAULT || !state_.temperatureReady) {
    replyError(F("HEATING_NOT_SAFE")); return;
  }
  if (strcmp(tokens[1], "SETPOINT") == 0) {
    profile_.stop(); autotune_.stop();
    pid_.reset(state_.controlTemperatureC, state_.outputPercent, state_.setpointC);
    state_.mode = OperatingMode::SETPOINT; tx_.println(F("OK,MODE=SETPOINT"));
  } else if (strcmp(tokens[1], "MANUAL") == 0) {
    profile_.stop(); autotune_.stop(); state_.mode = OperatingMode::MANUAL;
    tx_.println(F("OK,MODE=MANUAL"));
  } else replyError(F("BAD_MODE"));
}

void SerialProtocol::handleAutotune(char **tokens, uint8_t count, uint32_t nowMs) {
  if (count == 2 && strcmp(tokens[1], "STOP") == 0) {
    autotune_.stop(); state_.mode = OperatingMode::OFF; state_.outputPercent = 0.0f;
    heater_.forceOff(); tx_.println(F("OK,AUTOTUNE=STOPPED")); return;
  }
  if (count != 3 || strcmp(tokens[1], "START") != 0) { replyError(F("BAD_ARGUMENTS")); return; }
  float target;
  const char *text = valueFor(tokens[2], "TARGET");
  if (!parseFloatValue(text, target) || target < config::MIN_SETPOINT_C || target > config::MAX_SETPOINT_C) {
    replyError(F("BAD_TARGET")); return;
  }
  if (state_.status == SystemStatus::FAULT || state_.validTcCount < config::AUTOTUNE_MIN_VALID_TC ||
      !autotune_.start(target, state_.controlTemperatureC, nowMs)) {
    replyError(F("AUTOTUNE_NOT_SAFE")); return;
  }
  state_.errors &= ~static_cast<uint32_t>(ERROR_AUTOTUNE_FAILED);
  state_.latchedErrors &= ~static_cast<uint32_t>(ERROR_AUTOTUNE_FAILED);
  profile_.stop(); state_.setpointC = target; state_.mode = OperatingMode::AUTOTUNE;
  tx_.print(F("OK,AUTOTUNE=STARTED,TARGET=")); tx_.println(target, 1);
}

void SerialProtocol::handleProfile(char **tokens, uint8_t count, uint32_t nowMs) {
  if (count < 2) { replyError(F("BAD_ARGUMENTS")); return; }
  if (strcmp(tokens[1], "BEGIN") == 0 && count == 3) {
    uint32_t id;
    if (!parseUnsignedValue(valueFor(tokens[2], "ID"), id) || id > UINT16_MAX || !profile_.beginLoad(id))
      replyError(F("PROFILE_BEGIN_FAILED"));
    else {
      state_.errors &= ~static_cast<uint32_t>(ERROR_PROFILE_INVALID);
      tx_.println(F("OK,PROFILE=LOADING"));
    }
    return;
  }
  if (strcmp(tokens[1], "STEP") == 0 && count >= 4) {
    uint32_t index;
    if (!parseUnsignedValue(tokens[2], index) || index == 0 || index > config::PROFILE_MAX_STEPS) {
      replyError(F("BAD_STEP_INDEX")); return;
    }
    ProfileStep step;
    if (strcmp(tokens[3], "RAMP") == 0 && count == 6) {
      step.type = ProfileStepType::RAMP;
      if (!parseFloatValue(valueFor(tokens[4], "TARGET"), step.targetC) ||
          !parseFloatValue(valueFor(tokens[5], "RATE"), step.rateKPerMin) ||
          step.targetC < config::MIN_SETPOINT_C || step.targetC > config::MAX_SETPOINT_C ||
          step.rateKPerMin < config::PROFILE_MIN_RATE_K_PER_MIN || step.rateKPerMin > 1000.0f) {
        replyError(F("BAD_RAMP")); return;
      }
    } else if (strcmp(tokens[3], "HOLD") == 0 && count == 5) {
      step.type = ProfileStepType::HOLD;
      if (!parseUnsignedValue(valueFor(tokens[4], "TIME"), step.holdSeconds) ||
          step.holdSeconds == 0 || step.holdSeconds > 7UL * 24UL * 3600UL) {
        replyError(F("BAD_HOLD")); return;
      }
    } else if (strcmp(tokens[3], "OFF") == 0 && count == 4) step.type = ProfileStepType::OFF;
    else { replyError(F("BAD_STEP")); return; }
    if (!profile_.addStep(index, step)) replyError(F("STEP_REJECTED"));
    else { tx_.print(F("OK,STEP=")); tx_.println(index); }
    return;
  }
  if (strcmp(tokens[1], "END") == 0 && count == 2) {
    if (!profile_.endLoad()) {
      state_.errors |= ERROR_PROFILE_INVALID;
      replyError(F("PROFILE_INVALID"));
    } else {
      state_.errors &= ~static_cast<uint32_t>(ERROR_PROFILE_INVALID);
      tx_.print(F("OK,PROFILE=LOADED,STEPS=")); tx_.println(profile_.stepCount());
    }
    return;
  }
  if (strcmp(tokens[1], "START") == 0 && count == 2) {
    if (state_.status == SystemStatus::FAULT || !state_.temperatureReady ||
        !profile_.start(state_.controlTemperatureC, nowMs)) replyError(F("PROFILE_START_FAILED"));
    else {
      autotune_.stop(); state_.setpointC = state_.controlTemperatureC;
      pid_.reset(state_.controlTemperatureC, 0.0f, state_.setpointC); state_.mode = OperatingMode::PROFILE;
      tx_.println(F("OK,PROFILE=RUNNING"));
    }
    return;
  }
  if (strcmp(tokens[1], "PAUSE") == 0 && count == 2) {
    if (!profile_.pause(nowMs)) replyError(F("PROFILE_NOT_RUNNING"));
    else { state_.outputPercent = 0.0f; heater_.forceOff(); tx_.println(F("OK,PROFILE=PAUSED")); }
    return;
  }
  if (strcmp(tokens[1], "RESUME") == 0 && count == 2) {
    if (!profile_.resume(nowMs)) replyError(F("PROFILE_NOT_PAUSED"));
    else tx_.println(F("OK,PROFILE=RUNNING"));
    return;
  }
  if (strcmp(tokens[1], "STOP") == 0 && count == 2) {
    profile_.stop(); state_.mode = OperatingMode::OFF; state_.outputPercent = 0.0f;
    heater_.forceOff(); tx_.println(F("OK,PROFILE=ABORTED")); return;
  }
  if (strcmp(tokens[1], "CLEAR") == 0 && count == 2) {
    if (state_.mode == OperatingMode::PROFILE) { replyError(F("PROFILE_ACTIVE")); return; }
    profile_.clear(); tx_.println(F("OK,PROFILE=CLEARED")); return;
  }
  if ((strcmp(tokens[1], "GET") == 0 || strcmp(tokens[1], "STATUS") == 0) && count == 2) {
    tx_.print(F("OK,PROFILE=")); tx_.print(profile_.id());
    tx_.print(F(",STATE=")); tx_.print(TemperatureProfile::runStateName(profile_.state()));
    tx_.print(F(",STEPS=")); tx_.print(profile_.stepCount());
    tx_.print(F(",PSTEP=")); tx_.println(profile_.currentStepNumber()); return;
  }
  replyError(F("UNKNOWN_PROFILE_COMMAND"));
}

void SerialProtocol::printStatus(uint32_t nowMs, bool telemetry, const HeatFluxSensor *heatFlux) {
  tx_.print(telemetry ? F("STAT,SEQ=") : F("OK,SEQ="));
  if (telemetry) tx_.print(state_.sequence++); else tx_.print(state_.sequence);
  for (uint8_t i = 0; i < 3; ++i) {
    const ThermocoupleChannel &tc = thermocouples_.channel(i);
    tx_.print(F(",T")); tx_.print(i + 1); tx_.print('='); printFloatOrNan(tc.temperatureC);
    tx_.print(F(",T")); tx_.print(i + 1); tx_.print(F("S="));
    tx_.print(Thermocouples::statusName(tc.status));
  }
  tx_.print(F(",CTRL=")); printFloatOrNan(state_.controlTemperatureC);
  tx_.print(F(",SP=")); printFloatOrNan(state_.setpointC);
  tx_.print(F(",OUT=")); printFloatOrNan(state_.outputPercent);
  tx_.print(F(",MODE=")); tx_.print(modeName(state_.mode));
  tx_.print(F(",STATUS=")); tx_.print(statusName(state_.status));
  tx_.print(F(",ERRORS=0x")); tx_.print(state_.errors, HEX);
  tx_.print(F(",PID=")); tx_.print((state_.mode == OperatingMode::SETPOINT || state_.mode == OperatingMode::PROFILE) ? F("ON") : F("OFF"));
  tx_.print(F(",UI=")); tx_.print(state_.serialUiConnected ? F("REMOTE") : F("LOCAL"));
  if (profile_.state() == ProfileRunState::RUNNING || profile_.state() == ProfileRunState::PAUSED) {
    tx_.print(F(",PROFILE=")); tx_.print(profile_.id());
    tx_.print(F(",PSTEP=")); tx_.print(profile_.currentStepNumber());
    const ProfileStep *step = profile_.currentStep();
    tx_.print(F(",PTYPE=")); tx_.print(step ? TemperatureProfile::typeName(step->type) : "OFF");
    tx_.print(F(",PELAPSED=")); tx_.print(profile_.elapsedSeconds(nowMs));
    tx_.print(F(",PREMAIN=")); tx_.print(profile_.remainingSeconds(nowMs));
  }
  if (heatFlux && heatFlux->enabled() && heatFlux->valid()) {
    tx_.print(F(",HFV=")); printFloatOrNan(heatFlux->rawValue(), 6);
    if (heatFlux->calibrationValid() && isfinite(heatFlux->engineeringValue())) {
      tx_.print(F(",HF=")); printFloatOrNan(heatFlux->engineeringValue(), 3);
    }
  }
  tx_.println();
}

void SerialProtocol::telemetry(uint32_t nowMs, const HeatFluxSensor &heatFlux) {
  if (!Serial || tx_.freeSpace() < 512) return;
  printStatus(nowMs, true, &heatFlux);
}
