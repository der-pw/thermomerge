#include "user_interface/display.h"

#include <Wire.h>
#include <stdio.h>

#include "config/config.h"

namespace {
constexpr uint8_t LCD_RS = 0x01;
constexpr uint8_t LCD_ENABLE = 0x04;
constexpr uint8_t LCD_BACKLIGHT = 0x08;
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
  command(row == 0 ? 0x80 : 0xC0);
  for (uint8_t i = 0; i < 16; ++i) writeCharacter(padded[i]);
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
  // Calls are separated by the 500 ms display task, exceeding all HD44780
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
  initialized_ = true;
  bootScreenActive_ = true;
  bootScreenUntilMs_ = nowMs + config::LCD_BOOT_SCREEN_MS;
  return false;
}

void Display::update(uint32_t nowMs, const RuntimeState &state,
                     const TemperatureProfile &profile, bool setpointEditActive,
                     float editSetpointC, bool setpointConfirming,
                     bool confirmOk, LocalUiMode uiMode, uint8_t menuSelection,
                     float calibrationTargetC, float heatFluxKwM2) {
  if (!config::LCD_ENABLED) return;
  if (!initialized_) {
    initializeStep(nowMs);
    return;
  }
  if (bootScreenActive_) {
    if (static_cast<int32_t>(nowMs - bootScreenUntilMs_) < 0) {
      writeLine(0, "      CONE      ");
      writeLine(1, "     HEATER     ");
      return;
    }
    bootScreenActive_ = false;
  }
  char line1[24], line2[24];
  if (state.status == SystemStatus::FAULT) {
    snprintf(line1, sizeof(line1), "FAULT %08lX", static_cast<unsigned long>(state.errors));
    snprintf(line2, sizeof(line2), "%s", (state.errors & ERROR_OVERTEMP) ? "OVERTEMPERATURE" : "TC SENSOR ERROR");
  } else if (uiMode == LocalUiMode::START_MENU) {
    snprintf(line1, sizeof(line1), "%cSETPOINT", menuSelection == 0 ? '>' : ' ');
    snprintf(line2, sizeof(line2), "%cHEIZER KAL.", menuSelection == 1 ? '>' : ' ');
  } else if (uiMode == LocalUiMode::HEATER_CAL_READY ||
             uiMode == LocalUiMode::HEATER_CAL_RUNNING) {
    snprintf(line1, sizeof(line1), "KAL T:%5.1f SP:%3.0f", state.controlTemperatureC,
             calibrationTargetC);
    if (uiMode == LocalUiMode::HEATER_CAL_READY)
      snprintf(line2, sizeof(line2), "START: KURZ DR.");
    else if (isfinite(heatFluxKwM2))
      snprintf(line2, sizeof(line2), "HF:%6.2f KURZ=STOP", heatFluxKwM2);
    else
      snprintf(line2, sizeof(line2), "HF:-- KURZ=STOP");
  } else if (setpointEditActive) {
    snprintf(line1, sizeof(line1), "SET SP:%5.1f C", editSetpointC);
    if (setpointConfirming) {
      snprintf(line2, sizeof(line2), confirmOk ? "OK<    ABBR." : "OK     ABBR.<");
    } else {
      snprintf(line2, sizeof(line2), "DREHEN KURZ=OK");
    }
  } else if (state.mode == OperatingMode::PROFILE && profile.currentStep()) {
    const ProfileStep *step = profile.currentStep();
    snprintf(line1, sizeof(line1), "T:%5.1f SP:%3.0f", state.controlTemperatureC, state.setpointC);
    if (step->type == ProfileStepType::RAMP)
      snprintf(line2, sizeof(line2), "B%02u RAMP %3.1fK/m", profile.currentStepNumber(), step->rateKPerMin);
    else
      snprintf(line2, sizeof(line2), "B%02u %-4s %02lu:%02lu", profile.currentStepNumber(),
               TemperatureProfile::typeName(step->type),
               static_cast<unsigned long>(profile.remainingSeconds(nowMs) / 60),
               static_cast<unsigned long>(profile.remainingSeconds(nowMs) % 60));
  } else if (page_ == 0) {
    snprintf(line1, sizeof(line1), "T:%5.1f %s", state.controlTemperatureC,
             state.serialUiConnected ? "PC" : "LOC");
    snprintf(line2, sizeof(line2), "SP:%3.0f O:%3.0f %s", state.setpointC,
             state.outputPercent,
             state.status == SystemStatus::DEGRADED ? "DEG" : "OK");
  } else {
    snprintf(line1, sizeof(line1), "MODE:%s", modeName(state.mode));
    snprintf(line2, sizeof(line2), "TC:%u ERR:%04lX", state.validTcCount,
             static_cast<unsigned long>(state.errors & 0xFFFF));
  }
  writeLine(0, line1);
  writeLine(1, line2);
}
