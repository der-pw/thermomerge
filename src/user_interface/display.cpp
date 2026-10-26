#include "user_interface/display.h"

#include <Wire.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/config.h"

namespace {
constexpr uint8_t LCD_RS = 0x01;
constexpr uint8_t LCD_ENABLE = 0x04;
constexpr uint8_t LCD_BACKLIGHT = 0x08;

void formatFloat(char *buffer, size_t size, float value, uint8_t width,
                 uint8_t precision) {
  if (!isfinite(value)) {
    snprintf(buffer, size, "%*s", width, "--");
    return;
  }
  dtostrf(value, width, precision, buffer);
}
}

Display::Display() = default;

void Display::begin() {
  if (!config::LCD_ENABLED) return;
  Wire1.setSDA(config::I2C_SDA_GPIO);
  Wire1.setSCL(config::I2C_SCL_GPIO);
  Wire1.begin();
}

void Display::nextPage() { page_ = (page_ + 1) % 2; }
void Display::showMainPage() { page_ = 0; }

void Display::writeLine(uint8_t row, const char *text) {
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16.16s", text);
  uint8_t i = 0;
  while (i < 16) {
    while (i < 16 && rowCacheValid_[row] && padded[i] == rowCache_[row][i]) ++i;
    if (i == 16) break;
    command(static_cast<uint8_t>((row == 0 ? 0x80 : 0xC0) + i));
    while (i < 16 && (!rowCacheValid_[row] || padded[i] != rowCache_[row][i])) {
      writeCharacter(padded[i]);
      rowCache_[row][i] = padded[i];
      ++i;
    }
  }
  rowCacheValid_[row] = true;
}

void Display::writeCenteredLine(uint8_t row, const char *text) {
  char centered[17];
  const size_t length = strnlen(text, 16);
  const size_t left = (16 - length) / 2;
  memset(centered, ' ', 16);
  memcpy(centered + left, text, length);
  centered[16] = '\0';
  writeLine(row, centered);
}

void Display::expanderWrite(uint8_t value) {
  Wire1.beginTransmission(config::LCD_ADDRESS);
  Wire1.write(value | LCD_BACKLIGHT);
  Wire1.endTransmission();
}

void Display::writeNibble(uint8_t nibble, bool data) {
  const uint8_t value = (nibble & 0x0F) << 4 | (data ? LCD_RS : 0);
  expanderWrite(value | LCD_ENABLE);
  expanderWrite(value);
}

void Display::send(uint8_t value, bool data) {
  writeNibble(value >> 4, data);
  writeNibble(value, data);
}

void Display::command(uint8_t value) { send(value, false); }
void Display::writeCharacter(char value) { send(static_cast<uint8_t>(value), true); }

bool Display::initializeStep(uint32_t nowMs) {
  // Calls are separated by the display task, exceeding all HD44780
  // power-up timings without blocking the control loop.
  if (initPhase_ < 3) {
    writeNibble(0x03, false);
    ++initPhase_;
    return false;
  }
  if (initPhase_ == 3) {
    writeNibble(0x02, false);
    ++initPhase_;
    return false;
  }
  command(0x28);  // 4 bit, 2 rows, 5x8 font
  command(0x0C);  // display on, cursor off
  command(0x06);  // increment, no shift
  command(0x01);  // clear; next write is one scheduler period later
  rowCacheValid_[0] = false;
  rowCacheValid_[1] = false;
  initialized_ = true;
  bootScreenActive_ = true;
  bootScreenStartedMs_ = nowMs;
  bootScreenUntilMs_ = nowMs + config::LCD_BOOT_SCREEN_MS;
  return false;
}

void Display::update(uint32_t nowMs, const RuntimeState &state,
                     const TemperatureProfile &profile, bool setpointEditActive,
                     float editSetpointC, bool setpointConfirming,
                     bool confirmOk, bool autotuneSucceeded, bool autotuneStopped,
                     LocalUiMode uiMode,
                     uint8_t menuSelection, float autotuneTargetC,
                     uint8_t pidHeaterSelection, uint8_t pidManualSelection,
                     float manualKp, float manualKi, float manualKd,
                     bool heatFluxShowWm2,
                     float heatFluxKwM2,
                     const Thermocouples &thermocouples,
                     const HeatFluxSensor &heatFlux) {
  if (!config::LCD_ENABLED) return;
  if (!initialized_) {
    initializeStep(nowMs);
    return;
  }
  if (bootScreenActive_) {
    if (static_cast<int32_t>(nowMs - bootScreenUntilMs_) < 0) {
      const uint32_t elapsedMs = nowMs - bootScreenStartedMs_;
      writeCenteredLine(0, "CONE HEATER");
      if (elapsedMs < config::LCD_SPLASH_INTRO_MS) {
        writeCenteredLine(1, "");
        return;
      }
      const uint32_t sequenceElapsedMs = elapsedMs - config::LCD_SPLASH_INTRO_MS;
      const uint8_t deviceIndex = sequenceElapsedMs / config::LCD_SPLASH_DEVICE_MS;
      const uint32_t deviceElapsedMs = sequenceElapsedMs % config::LCD_SPLASH_DEVICE_MS;
      char label[16];
      char status[20];
      bool deviceOk = false;
      if (deviceIndex == 0) {
        snprintf(label, sizeof(label), "ADS1220");
        deviceOk = heatFlux.responsive();
      } else {
        const uint8_t tcIndex = deviceIndex - 1;
        const ThermocoupleChannel &channel = thermocouples.channel(tcIndex);
        snprintf(label, sizeof(label), "MAX31855 %u", tcIndex + 1);
        deviceOk = channel.faultFlags == 0 && isfinite(channel.temperatureC);
      }
      if (deviceElapsedMs >= config::LCD_SPLASH_DEVICE_MS -
                                 config::LCD_SPLASH_RESULT_DISPLAY_MS)
        snprintf(status, sizeof(status), "%s %s", label, deviceOk ? "PASS" : "FAIL");
      else
        snprintf(status, sizeof(status), "%s %s", label, "    ");
      writeCenteredLine(1, status);
      return;
    }
    bootScreenActive_ = false;
  }
  char line1[24], line2[24];
  char temperature[8], setpoint[8], output[8], value[12];
  if (state.status == SystemStatus::FAULT &&
      uiMode == LocalUiMode::AUTOTUNE_RESULT) {
    snprintf(line1, sizeof(line1), "FAULT %08lX", static_cast<unsigned long>(state.errors));
    snprintf(line2, sizeof(line2), "%s", (state.errors & ERROR_OVERTEMP)
                 ? "OVERTEMPERATURE" : "TC SENSOR ERROR");
  } else if (uiMode == LocalUiMode::START_MENU) {
    static const char *const items[] = {
        "SETPOINT", "THERMOCOUPLES", "HEATFLUX", "PID HEATER", "SSR TEST"};
    const uint8_t count = sizeof(items) / sizeof(items[0]);
    snprintf(line1, sizeof(line1), ">%-.15s", items[menuSelection % count]);
    snprintf(line2, sizeof(line2), " %-15s", items[(menuSelection + 1) % count]);
  } else if (uiMode == LocalUiMode::SSR_TEST) {
    snprintf(line1, sizeof(line1), "SSR TEST");
    snprintf(line2, sizeof(line2), "3x500ms ON/OFF");
  } else if (uiMode == LocalUiMode::SSR_TEST_BLOCKED) {
    snprintf(line1, sizeof(line1), "SSR TEST");
    snprintf(line2, sizeof(line2), "OFF + TC OK");
  } else if (uiMode == LocalUiMode::PID_HEATER_MENU) {
    snprintf(line1, sizeof(line1), ">%-15s", pidHeaterSelection == 0 ? "AUTOTUNE" : "MANUAL");
    snprintf(line2, sizeof(line2), " %-15s", pidHeaterSelection == 0 ? "MANUAL" : "AUTOTUNE");
  } else if (uiMode == LocalUiMode::PID_MANUAL_SELECT ||
             uiMode == LocalUiMode::PID_MANUAL_EDIT) {
    if (uiMode == LocalUiMode::PID_MANUAL_SELECT && pidManualSelection == 3) {
      snprintf(line1, sizeof(line1), ">SAVE");
      snprintf(line2, sizeof(line2), "PRESS=SELECT");
    } else {
      const float values[] = {manualKp, manualKi, manualKd};
      const char *names[] = {"Kp", "Ki", "Kd"};
      const uint8_t index = pidManualSelection % 3;
      formatFloat(value, sizeof(value), values[index], 7, 3);
      if (uiMode == LocalUiMode::PID_MANUAL_SELECT)
        snprintf(line1, sizeof(line1), ">%s:%s", names[index], value);
      else
        snprintf(line1, sizeof(line1), "%s:%s", names[index], value);
      if (uiMode == LocalUiMode::PID_MANUAL_EDIT)
      snprintf(line2, sizeof(line2), "TURN PRESS=OK");
      else
        snprintf(line2, sizeof(line2), "TURN PRESS=EDIT");
    }
  } else if (uiMode == LocalUiMode::PID_MANUAL_SAVE_CONFIRM) {
    snprintf(line1, sizeof(line1), "SAVE PID VALUES");
    snprintf(line2, sizeof(line2), confirmOk ? "YES<    NO" : "YES     NO<");
  } else if (uiMode == LocalUiMode::AUTOTUNE_SETPOINT) {
    formatFloat(setpoint, sizeof(setpoint), autotuneTargetC, 5, 1);
    snprintf(line1, sizeof(line1), "AT SP:%s C", setpoint);
    snprintf(line2, sizeof(line2), "TURN PRESS=OK");
  } else if (uiMode == LocalUiMode::AUTOTUNE_CONFIRM) {
    formatFloat(setpoint, sizeof(setpoint), autotuneTargetC, 5, 1);
    snprintf(line1, sizeof(line1), "START AT:%sC", setpoint);
    snprintf(line2, sizeof(line2), confirmOk ? "YES<    CANCEL" : "YES     CANCEL<");
  } else if (uiMode == LocalUiMode::AUTOTUNE_RUNNING) {
    snprintf(line1, sizeof(line1), "AUTOTUNE");
    snprintf(line2, sizeof(line2), "PRESS=STOP");
  } else if (uiMode == LocalUiMode::AUTOTUNE_RESULT) {
    const char *resultText = autotuneSucceeded ? "AT SUCCESS"
        : autotuneStopped ? "AT STOPPED" : "AT FAILED";
    snprintf(line1, sizeof(line1), "%s", resultText);
    snprintf(line2, sizeof(line2), "DETAILS SERIAL");
  } else if (uiMode == LocalUiMode::AUTOTUNE_BLOCKED) {
    snprintf(line1, sizeof(line1), "AUTOTUNE");
    snprintf(line2, sizeof(line2), "2+ TC REQUIRED");
  } else if (uiMode == LocalUiMode::SETPOINT_BLOCKED) {
    snprintf(line1, sizeof(line1), "HEATER BLOCKED");
    snprintf(line2, sizeof(line2), "CHECK TC/FAULT");
  } else if (uiMode == LocalUiMode::TC_VIEW) {
    const ThermocoupleChannel &tc1 = thermocouples.channel(0);
    const ThermocoupleChannel &tc2 = thermocouples.channel(1);
    const ThermocoupleChannel &tc3 = thermocouples.channel(2);
    formatFloat(temperature, sizeof(temperature),
                tc1.electricallyValid && tc1.plausible ? tc1.temperatureC : NAN, 5, 1);
    formatFloat(value, sizeof(value),
                tc2.electricallyValid && tc2.plausible ? tc2.temperatureC : NAN, 5, 1);
    snprintf(line1, sizeof(line1), "1:%s 2:%s", temperature, value);
    formatFloat(temperature, sizeof(temperature),
                tc3.electricallyValid && tc3.plausible ? tc3.temperatureC : NAN, 5, 1);
    snprintf(line2, sizeof(line2), "3:%s N:%u", temperature, state.validTcCount);
  } else if (uiMode == LocalUiMode::HEATFLUX_VIEW) {
    snprintf(line1, sizeof(line1), "HEATFLUX %s", heatFluxShowWm2 ? "W/m2" : "RAW");
    if (!heatFlux.enabled()) snprintf(line2, sizeof(line2), "DISABLED");
    else if (heatFlux.fault()) snprintf(line2, sizeof(line2), "SENSOR FAULT");
    else if (!heatFlux.valid()) snprintf(line2, sizeof(line2), "WAITING");
    else if (heatFluxShowWm2 && !heatFlux.calibrationValid())
      snprintf(line2, sizeof(line2), "UNCALIBRATED");
    else {
      if (heatFluxShowWm2) {
        formatFloat(value, sizeof(value), heatFluxKwM2 * 1000.0f, 6, 2);
        snprintf(line2, sizeof(line2), "VAL:%s W/m2", value);
      } else {
        formatFloat(value, sizeof(value), heatFlux.rawValue() * 1000.0f, 6, 2);
        snprintf(line2, sizeof(line2), "RAW:%s mV", value);
      }
    }
  } else if (setpointEditActive) {
    formatFloat(setpoint, sizeof(setpoint), editSetpointC, 5, 1);
    if (setpointConfirming) {
      snprintf(line1, sizeof(line1), "HEATER ON?");
      snprintf(line2, sizeof(line2), confirmOk ? "YES<    NO" : "YES     NO<");
    } else {
      snprintf(line1, sizeof(line1), "SETPOINT:%s C", setpoint);
      snprintf(line2, sizeof(line2), "TURN PRESS=NEXT");
    }
  } else if (state.status == SystemStatus::FAULT) {
    snprintf(line1, sizeof(line1), "FAULT %08lX", static_cast<unsigned long>(state.errors));
    snprintf(line2, sizeof(line2), "%s", (state.errors & ERROR_OVERTEMP) ? "OVERTEMPERATURE" : "TC SENSOR ERROR");
  } else if (state.mode == OperatingMode::PROFILE && profile.currentStep()) {
    const ProfileStep *step = profile.currentStep();
    formatFloat(temperature, sizeof(temperature), state.controlTemperatureC, 5, 1);
    formatFloat(setpoint, sizeof(setpoint), state.setpointC, 3, 0);
    snprintf(line1, sizeof(line1), "T:%s SP:%s", temperature, setpoint);
    if (step->type == ProfileStepType::RAMP) {
      formatFloat(value, sizeof(value), step->rateKPerMin, 3, 1);
      snprintf(line2, sizeof(line2), "B%02u RAMP %sK/m", profile.currentStepNumber(), value);
    } else
      snprintf(line2, sizeof(line2), "B%02u %-4s %02lu:%02lu", profile.currentStepNumber(),
               TemperatureProfile::typeName(step->type),
               static_cast<unsigned long>(profile.remainingSeconds(nowMs) / 60),
               static_cast<unsigned long>(profile.remainingSeconds(nowMs) % 60));
  } else if (page_ == 0) {
    formatFloat(temperature, sizeof(temperature), state.controlTemperatureC, 5, 1);
    formatFloat(setpoint, sizeof(setpoint), state.setpointC, 3, 0);
    formatFloat(output, sizeof(output), state.outputPercent, 3, 0);
    snprintf(line1, sizeof(line1), state.mode == OperatingMode::SETPOINT
                 ? "T:%s %s RUN" : "T:%s %s", temperature,
             state.serialUiConnected ? "PC" : "LOC");
    snprintf(line2, sizeof(line2), "SP:%s O:%s %s", setpoint, output,
             state.status == SystemStatus::DEGRADED ? "DEG" : "OK");
  } else {
    snprintf(line1, sizeof(line1), "MODE:%s", modeName(state.mode));
    snprintf(line2, sizeof(line2), "TC:%u ERR:%04lX", state.validTcCount,
             static_cast<unsigned long>(state.errors & 0xFFFF));
  }
  writeLine(0, line1);
  writeLine(1, line2);
}
