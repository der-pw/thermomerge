#pragma once

#include <Arduino.h>

#include "control/profile.h"
#include "core/system_state.h"
#include "sensors/heatflux/heatflux.h"
#include "sensors/thermocouples.h"

enum class LocalUiMode : uint8_t {
  START_MENU, NORMAL, TC_VIEW, HEATFLUX_VIEW, HEATER_CAL_READY, HEATER_CAL_RUNNING
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
              bool confirmOk, LocalUiMode uiMode, uint8_t menuSelection,
              float calibrationTargetC, bool heatFluxShowWm2, float heatFluxKwM2,
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
