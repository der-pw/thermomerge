#include <Arduino.h>
#include <hardware/watchdog.h>

#include "config/config.h"
#include "control/controller.h"
#include "user_interface/display.h"
#include "user_interface/encoder.h"
#include "actuators/heater.h"
#include "sensors/heatflux/heatflux.h"
#include "control/profile.h"
#include "communication/protocol.h"
#include "persistence/settings.h"
#include "core/system_state.h"
#include "sensors/thermocouples.h"

RuntimeState systemState;
Thermocouples thermocouples;
PIDController pid;
PIDAutotune autotune;
TemperatureProfile profile;
Heater heater;
Encoder encoder;
Display display;
HeatFluxSensor heatFlux;
PersistentSettings settings;
SerialProtocol protocol(systemState, thermocouples, pid, autotune, profile, settings, heater, heatFlux);

namespace {
uint32_t lastThermocoupleMs = 0;
uint32_t lastPidMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastSensorTaskCompletedMs = 0;
uint32_t lastLedMs = 0;
bool ledState = false;
bool autotuneReported = false;
bool setpointEditActive = false;
bool setpointConfirming = false;
bool setpointConfirmOk = true;
float editSetpointC = config::DEFAULT_SETPOINT_C;
bool lastSerialUiConnected = false;
LocalUiMode localUiMode = LocalUiMode::START_MENU;
uint8_t menuSelection = 0;
float heaterCalibrationTargetC = config::DEFAULT_SETPOINT_C;

constexpr uint32_t TC_ERROR_MASK = 0x00000FFFUL;
constexpr uint32_t SAFETY_LATCH_MASK = ERROR_OVERTEMP | ERROR_INTERNAL;

void stopActiveProcesses() {
  const bool autotuneWasRunning = systemState.mode == OperatingMode::AUTOTUNE;
  systemState.mode = OperatingMode::OFF;
  systemState.outputPercent = 0.0f;
  heater.forceOff();
  profile.stop();
  autotune.stop();
  if (autotuneWasRunning) {
    systemState.errors |= ERROR_AUTOTUNE_FAILED;
    systemState.latchedErrors |= ERROR_AUTOTUNE_FAILED;
    protocol.writer().println(F("EVENT,AUTOTUNE=FAILED,REASON=SAFETY"));
    autotuneReported = true;
  }
}

void updateSystemFromSensors(const ThermocoupleResult &result) {
  systemState.controlTemperatureC = result.controlTemperatureC;
  systemState.validTcCount = result.validCount;
  systemState.temperatureReady = result.validCount > 0;
  systemState.errors &= ~static_cast<uint32_t>(TC_ERROR_MASK | ERROR_NO_VALID_TC);
  systemState.errors |= result.errorBits | systemState.latchedErrors;

  if (result.validCount == 0) {
    systemState.status = SystemStatus::FAULT;
    stopActiveProcesses();
    return;
  }
  bool anyOvertemperature = false;
  for (uint8_t i = 0; i < 3; ++i) {
    const ThermocoupleChannel &channel = thermocouples.channel(i);
    if (channel.faultFlags == 0 && isfinite(channel.temperatureC) &&
        channel.temperatureC >= config::MAX_TEMPERATURE_C) anyOvertemperature = true;
  }
  if (anyOvertemperature) {
    enterFault(systemState, ERROR_OVERTEMP);
    stopActiveProcesses();
    return;
  }
  if (systemState.latchedErrors & SAFETY_LATCH_MASK) {
    systemState.status = SystemStatus::FAULT;
    stopActiveProcesses();
    return;
  }
  systemState.status = result.validCount == 3 && (result.errorBits & TC_ERROR_MASK) == 0
      ? SystemStatus::OK : SystemStatus::DEGRADED;
}

void updateEncoder(uint32_t nowMs) {
  const EncoderEvents events = encoder.update(nowMs);
  if (systemState.serialUiConnected) {
    // Remote ownership blocks all local encoder actions, including page changes.
    setpointEditActive = false;
    setpointConfirming = false;
    return;
  }
  if (localUiMode == LocalUiMode::START_MENU) {
    if (events.detents != 0) menuSelection = events.detents > 0 ? 1 : 0;
    if (events.shortPress) {
      if (menuSelection == 0) {
        localUiMode = LocalUiMode::NORMAL;
        editSetpointC = systemState.setpointC;
        setpointEditActive = true;
        setpointConfirming = false;
        setpointConfirmOk = true;
        display.showMainPage();
      } else {
        heaterCalibrationTargetC = systemState.setpointC;
        localUiMode = LocalUiMode::HEATER_CAL_READY;
      }
    }
    return;
  }
  if (localUiMode == LocalUiMode::HEATER_CAL_READY ||
      localUiMode == LocalUiMode::HEATER_CAL_RUNNING) {
    if (events.detents != 0) {
      heaterCalibrationTargetC = constrain(
          heaterCalibrationTargetC + events.detents * config::ENCODER_STEP_C,
          config::MIN_SETPOINT_C, config::MAX_SETPOINT_C);
      if (localUiMode == LocalUiMode::HEATER_CAL_RUNNING)
        systemState.setpointC = heaterCalibrationTargetC;
    }
    if (events.shortPress) {
      if (localUiMode == LocalUiMode::HEATER_CAL_READY) {
        if (systemState.status != SystemStatus::FAULT && systemState.temperatureReady) {
          systemState.setpointC = heaterCalibrationTargetC;
          pid.reset(systemState.controlTemperatureC, 0.0f, systemState.setpointC);
          systemState.mode = OperatingMode::PID;
          localUiMode = LocalUiMode::HEATER_CAL_RUNNING;
        }
      } else {
        systemState.mode = OperatingMode::OFF;
        systemState.outputPercent = 0.0f;
        heater.forceOff();
        localUiMode = LocalUiMode::START_MENU;
      }
    }
    if (events.longPress) {
      systemState.mode = OperatingMode::OFF;
      systemState.outputPercent = 0.0f;
      heater.forceOff();
      localUiMode = LocalUiMode::START_MENU;
    }
    return;
  }
  if (setpointEditActive) {
    if (events.detents != 0) {
      if (setpointConfirming) {
        setpointConfirmOk = events.detents > 0;
      } else {
        editSetpointC = constrain(editSetpointC + events.detents * config::ENCODER_STEP_C,
                                  config::MIN_SETPOINT_C, config::MAX_SETPOINT_C);
      }
    }
    if (events.shortPress) {
      if (!setpointConfirming) {
        setpointConfirming = true;
      } else if (setpointConfirmOk) {
        systemState.setpointC = editSetpointC;
        settings.markSetpointDirty(nowMs, systemState.setpointC);
        setpointEditActive = false;
        setpointConfirming = false;
      } else {
        setpointEditActive = false;
        setpointConfirming = false;
      }
    }
    if (events.longPress) {
      setpointEditActive = false;
      setpointConfirming = false;
    }
    return;
  }
  if (events.longPress && systemState.status != SystemStatus::FAULT &&
      systemState.mode != OperatingMode::PROFILE &&
      systemState.mode != OperatingMode::AUTOTUNE) {
    editSetpointC = systemState.setpointC;
    setpointEditActive = true;
    setpointConfirming = false;
    setpointConfirmOk = true;
  } else if (events.shortPress) {
    display.nextPage();
  }
}

void updateProfile(uint32_t nowMs) {
  if (systemState.mode != OperatingMode::PROFILE) return;
  if (profile.state() == ProfileRunState::PAUSED) {
    systemState.outputPercent = 0.0f;
    return;
  }
  bool requestOff = false;
  systemState.setpointC = profile.update(nowMs, requestOff);
  if (requestOff || profile.state() == ProfileRunState::COMPLETED) {
    systemState.mode = OperatingMode::OFF;
    systemState.outputPercent = 0.0f;
  }
}

void reportAutotuneSuccess(float kp, float ki, float kd) {
  Print &writer = protocol.writer();
  writer.print(F("EVENT,AUTOTUNE=SUCCESS,KP=")); writer.print(kp, 6);
  writer.print(F(",KI=")); writer.print(ki, 6);
  writer.print(F(",KD=")); writer.println(kd, 6);
}

void updateControl(uint32_t nowMs) {
  if (nowMs - lastPidMs < config::PID_SAMPLE_MS) return;
  lastPidMs = nowMs;
  if (!heatingAllowed(systemState)) {
    systemState.outputPercent = 0.0f;
    return;
  }

  switch (systemState.mode) {
    case OperatingMode::OFF:
      systemState.outputPercent = 0.0f;
      break;
    case OperatingMode::MANUAL:
      systemState.outputPercent = systemState.manualOutputPercent;
      break;
    case OperatingMode::PID:
      systemState.outputPercent = pid.compute(systemState.setpointC, systemState.controlTemperatureC);
      break;
    case OperatingMode::PROFILE:
      systemState.outputPercent = profile.state() == ProfileRunState::PAUSED
          ? 0.0f : pid.compute(systemState.setpointC, systemState.controlTemperatureC);
      break;
    case OperatingMode::AUTOTUNE: {
      if (systemState.validTcCount < config::AUTOTUNE_MIN_VALID_TC ||
          systemState.controlTemperatureC >= config::MAX_TEMPERATURE_C) autotune.stop();
      systemState.outputPercent = autotune.update(systemState.controlTemperatureC, nowMs);
      if (autotune.result() == AutotuneResult::SUCCEEDED) {
        float kp, ki, kd; autotune.tunings(kp, ki, kd);
        pid.setTunings(kp, ki, kd);
        settings.markDirty(nowMs);
        systemState.mode = OperatingMode::OFF;
        systemState.outputPercent = 0.0f;
        heater.forceOff();
        if (!autotuneReported) reportAutotuneSuccess(kp, ki, kd);
        autotuneReported = true;
      } else if (autotune.result() == AutotuneResult::FAILED) {
        systemState.errors |= ERROR_AUTOTUNE_FAILED;
        systemState.latchedErrors |= ERROR_AUTOTUNE_FAILED;
        systemState.mode = OperatingMode::OFF;
        systemState.outputPercent = 0.0f;
        heater.forceOff();
        if (!autotuneReported) protocol.writer().println(F("EVENT,AUTOTUNE=FAILED"));
        autotuneReported = true;
      } else {
        autotuneReported = false;
      }
      break;
    }
  }
  if (systemState.validTcCount == 1) {
    systemState.outputPercent = min(systemState.outputPercent,
                                    config::MAX_OUTPUT_SINGLE_TC_PERCENT);
  }
}

void updateStatusLed(uint32_t nowMs) {
  const uint32_t interval = systemState.status == SystemStatus::FAULT ? 125 :
                            systemState.status == SystemStatus::DEGRADED ? 500 : 1000;
  if (nowMs - lastLedMs >= interval) {
    lastLedMs = nowMs;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }
}
}  // namespace

void setup() {
  // This is intentionally the first hardware action.
  heater.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);

  pid.setTunings(config::DEFAULT_KP, config::DEFAULT_KI, config::DEFAULT_KD);
  systemState.setpointC = config::DEFAULT_SETPOINT_C;
  settings.begin(pid, systemState, heatFlux);
  thermocouples.begin();
  encoder.begin();
  display.begin();
  heatFlux.begin();

  systemState.mode = OperatingMode::OFF;
  systemState.outputPercent = 0.0f;
  watchdog_enable(config::WATCHDOG_TIMEOUT_MS, true);
  protocol.writer().println(F("EVENT,BOOT=READY,MODE=OFF"));
}

void loop() {
  const uint32_t nowMs = millis();
  protocol.serviceTx();
  protocol.poll(nowMs);
  if (systemState.serialUiConnected != lastSerialUiConnected) {
    display.showMainPage();
    if (systemState.serialUiConnected) localUiMode = LocalUiMode::NORMAL;
    lastSerialUiConnected = systemState.serialUiConnected;
  }
  updateEncoder(nowMs);
  heatFlux.update(nowMs);

  if (nowMs - lastThermocoupleMs >= config::TC_INTERVAL_MS) {
    lastThermocoupleMs = nowMs;
    updateSystemFromSensors(thermocouples.update());
    lastSensorTaskCompletedMs = millis();
  }

  updateProfile(nowMs);
  updateControl(nowMs);
  heater.setDemand(systemState.outputPercent);
  heater.update(nowMs, heatingAllowed(systemState));
  updateStatusLed(nowMs);

  if (nowMs - lastTelemetryMs >= config::TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = nowMs;
    protocol.telemetry(nowMs, heatFlux);
  }
  if (nowMs - lastDisplayMs >= config::LCD_INTERVAL_MS) {
    lastDisplayMs = nowMs;
    display.update(nowMs, systemState, profile, setpointEditActive,
                   editSetpointC, setpointConfirming, setpointConfirmOk,
                   localUiMode, menuSelection, heaterCalibrationTargetC,
                   heatFlux.valid() && heatFlux.calibrationValid()
                       ? heatFlux.engineeringValue() : NAN);
  }
  settings.update(nowMs, pid, systemState, heatFlux);
  protocol.serviceTx();

  // Feed only after the sensor task has demonstrably completed recently.
  if (lastSensorTaskCompletedMs != 0 &&
      millis() - lastSensorTaskCompletedMs < config::TASK_HEALTH_TIMEOUT_MS) watchdog_update();
}
