#pragma once

#include <Arduino.h>

#include "control/profile.h"
#include "core/system_state.h"
#include "sensors/heatflux/heatflux.h"
#include "sensors/thermocouples.h"

enum class LocalUiMode : uint8_t {
  START_MENU, NORMAL, HEATFLUX_VIEW, SSR_TEST, SSR_TEST_BLOCKED,
  AUTOTUNE_SETPOINT, AUTOTUNE_CONFIRM, AUTOTUNE_RUNNING, AUTOTUNE_RESULT,
  AUTOTUNE_BLOCKED, SETPOINT_BLOCKED, FAULT_CLEAR_DONE, FAULT_CLEAR_BLOCKED,
  FAULT_CLEAR_CHECK_TC, FAULT_CLEAR_NO_FAULT, PID_HEATER_MENU,
  PID_MANUAL_SELECT, PID_MANUAL_EDIT, PID_MANUAL_SAVE_CONFIRM
};

class Display {
 public:
  Display();
  void begin();
  void nextPage();
  void showMainPage();
  void update(uint32_t nowMs, const RuntimeState &state,
              const TemperatureProfile &profile, bool setpointEditActive,
              float editSetpointC, bool setpointConfirming,
              bool confirmOk, bool autotuneSucceeded, bool autotuneStopped,
              LocalUiMode uiMode,
              uint8_t menuSelection, float autotuneTargetC,
              uint8_t pidHeaterSelection, uint8_t pidManualSelection,
              float manualKp, float manualKi, float manualKd,
              bool dashboardSetpointEditActive, float dashboardEditSetpointC,
              bool heatFluxShowWm2, float heatFluxKwM2,
              const Thermocouples &thermocouples, const HeatFluxSensor &heatFlux);

 private:
  uint8_t page_ = 0;
  uint8_t initPhase_ = 0;
  bool initialized_ = false;
  bool bootScreenActive_ = false;
  uint32_t bootScreenStartedMs_ = 0;
  uint32_t bootScreenUntilMs_ = 0;
  char rowCache_[2][16]{};
  bool rowCacheValid_[2] = {false, false};
  void writeLine(uint8_t row, const char *text);
  void writeCenteredLine(uint8_t row, const char *text);
  void expanderWrite(uint8_t value);
  void writeNibble(uint8_t nibble, bool data);
  void send(uint8_t value, bool data);
  void command(uint8_t value);
  void writeCharacter(char value);
  bool initializeStep(uint32_t nowMs);
};
