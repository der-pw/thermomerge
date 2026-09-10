#pragma once

#include <Arduino.h>

#include "config/config.h"
#include "control/controller.h"
#include "sensors/heatflux/heatflux.h"
#include "actuators/heater.h"
#include "control/profile.h"
#include "persistence/settings.h"
#include "core/system_state.h"
#include "sensors/thermocouples.h"

class BufferedSerialWriter : public Print {
 public:
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  void service();
  uint16_t freeSpace() const { return config::SERIAL_TX_BUFFER_SIZE - count_; }

 private:
  uint8_t buffer_[config::SERIAL_TX_BUFFER_SIZE]{};
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;
};

class SerialProtocol {
 public:
  SerialProtocol(RuntimeState &state, Thermocouples &thermocouples, PIDController &pid,
                 PIDAutotune &autotune, TemperatureProfile &profile,
                 PersistentSettings &settings, Heater &heater, HeatFluxSensor &heatFlux);
  void poll(uint32_t nowMs);
  void serviceTx() { tx_.service(); }
  void telemetry(uint32_t nowMs, const HeatFluxSensor &heatFlux);
  Print &writer() { return tx_; }

 private:
  RuntimeState &state_;
  Thermocouples &thermocouples_;
  PIDController &pid_;
  PIDAutotune &autotune_;
  TemperatureProfile &profile_;
  PersistentSettings &settings_;
  Heater &heater_;
  HeatFluxSensor &heatFlux_;
  BufferedSerialWriter tx_;
  char buffer_[config::SERIAL_RX_BUFFER_SIZE]{};
  uint16_t length_ = 0;
  bool overflow_ = false;

  void handleLine(char *line, uint32_t nowMs);
  void handleSet(char **tokens, uint8_t count, uint32_t nowMs);
  void handleUi(char **tokens, uint8_t count);
  void handleMode(char **tokens, uint8_t count);
  void handleAutotune(char **tokens, uint8_t count, uint32_t nowMs);
  void handleProfile(char **tokens, uint8_t count, uint32_t nowMs);
  void printStatus(uint32_t nowMs, bool telemetry, const HeatFluxSensor *heatFlux = nullptr);
  void replyError(const __FlashStringHelper *message);
  void printFloatOrNan(float value, uint8_t decimals = 1);
};
