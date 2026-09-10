#include "sensors/thermocouples.h"

#include <math.h>

#include "config/config.h"

Thermocouples::Thermocouples()
    : sensors_{Adafruit_MAX31855(config::TC_CS_GPIOS[0]),
               Adafruit_MAX31855(config::TC_CS_GPIOS[1]),
               Adafruit_MAX31855(config::TC_CS_GPIOS[2])} {}

void Thermocouples::begin() {
  SPI.setRX(config::SPI_MISO_GPIO);
  SPI.setSCK(config::SPI_SCK_GPIO);
  SPI.setTX(config::SPI_MOSI_GPIO);
  SPI.begin();
  for (uint8_t i = 0; i < 3; ++i) sensors_[i].begin();
}

void Thermocouples::readChannel(uint8_t index) {
  ThermocoupleChannel &channel = channels_[index];
  const float value = sensors_[index].readCelsius();
  const uint8_t faults = sensors_[index].readError();
  const bool goodSample = isfinite(value) && faults == 0;
  channel.faultFlags = faults;

  if (!goodSample) {
    if (channel.faultCount < UINT8_MAX) ++channel.faultCount;
    channel.recoveryCount = 0;
    channel.plausible = false;
    if (channel.faultCount >= config::TC_FAULT_TRIP_COUNT) {
      channel.electricallyValid = false;
      channel.status = SensorStatus::FAULT;
    } else {
      channel.status = SensorStatus::RECOVERING;
    }
    return;
  }

  channel.temperatureC = value;
  channel.faultCount = 0;
  if (channel.recoveryCount < config::TC_RECOVERY_COUNT) ++channel.recoveryCount;
  if (channel.recoveryCount >= config::TC_RECOVERY_COUNT) {
    channel.electricallyValid = true;
    channel.status = SensorStatus::OK;
  } else {
    channel.electricallyValid = false;
    channel.status = SensorStatus::RECOVERING;
  }
}

void Thermocouples::evaluatePlausibility() {
  uint8_t candidates[3];
  uint8_t count = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    channels_[i].plausible = false;
    if (channels_[i].electricallyValid) candidates[count++] = i;
  }

  if (count == 1) {
    channels_[candidates[0]].plausible = true;
  } else if (count == 2) {
    const uint8_t a = candidates[0], b = candidates[1];
    if (fabsf(channels_[a].temperatureC - channels_[b].temperatureC) <=
        config::TC_MAX_DEVIATION_C) {
      channels_[a].plausible = channels_[b].plausible = true;
    } else {
      channels_[a].status = channels_[b].status = SensorStatus::OUTLIER;
    }
  } else if (count == 3) {
    uint8_t order[3] = {0, 1, 2};
    for (uint8_t i = 0; i < 2; ++i) {
      for (uint8_t j = i + 1; j < 3; ++j) {
        if (channels_[order[j]].temperatureC < channels_[order[i]].temperatureC) {
          const uint8_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
      }
    }
    const float lowGap = channels_[order[1]].temperatureC - channels_[order[0]].temperatureC;
    const float highGap = channels_[order[2]].temperatureC - channels_[order[1]].temperatureC;
    const float range = channels_[order[2]].temperatureC - channels_[order[0]].temperatureC;
    if (range <= config::TC_MAX_DEVIATION_C) {
      for (uint8_t i = 0; i < 3; ++i) channels_[i].plausible = true;
    } else if (lowGap > config::TC_MAX_DEVIATION_C && highGap <= config::TC_MAX_DEVIATION_C) {
      channels_[order[0]].status = SensorStatus::OUTLIER;
      channels_[order[1]].plausible = channels_[order[2]].plausible = true;
    } else if (highGap > config::TC_MAX_DEVIATION_C && lowGap <= config::TC_MAX_DEVIATION_C) {
      channels_[order[2]].status = SensorStatus::OUTLIER;
      channels_[order[0]].plausible = channels_[order[1]].plausible = true;
    } else if (lowGap > config::TC_MAX_DEVIATION_C && highGap > config::TC_MAX_DEVIATION_C) {
      for (uint8_t i = 0; i < 3; ++i) channels_[i].status = SensorStatus::OUTLIER;
    } else {
      // Both adjacent pairs are close but the endpoints disagree. The median is
      // the only unambiguous robust estimate; single-sensor output limiting applies.
      channels_[order[1]].plausible = true;
      channels_[order[0]].status = channels_[order[2]].status = SensorStatus::OUTLIER;
    }
  }

  for (uint8_t i = 0; i < 3; ++i) {
    if (channels_[i].plausible)
      channels_[i].status = channels_[i].faultCount == 0 ? SensorStatus::OK : SensorStatus::RECOVERING;
  }
}

uint32_t Thermocouples::channelErrors(uint8_t index) const {
  const ThermocoupleChannel &channel = channels_[index];
  const uint8_t shift = index * 4;
  uint32_t result = ERROR_NONE;
  if (channel.faultFlags & MAX31855_FAULT_OPEN) result |= ERROR_TC1_OPEN << shift;
  if (channel.faultFlags & MAX31855_FAULT_SHORT_GND) result |= ERROR_TC1_SHORT_GND << shift;
  if (channel.faultFlags & MAX31855_FAULT_SHORT_VCC) result |= ERROR_TC1_SHORT_VCC << shift;
  if (channel.status == SensorStatus::OUTLIER) result |= ERROR_TC1_OUTLIER << shift;
  return result;
}

ThermocoupleResult Thermocouples::update() {
  for (uint8_t i = 0; i < 3; ++i) readChannel(i);
  evaluatePlausibility();

  ThermocoupleResult result;
  float sum = 0.0f;
  for (uint8_t i = 0; i < 3; ++i) {
    result.errorBits |= channelErrors(i);
    if (channels_[i].electricallyValid && channels_[i].plausible) {
      sum += channels_[i].temperatureC;
      ++result.validCount;
    }
  }
  if (result.validCount > 0) result.controlTemperatureC = sum / result.validCount;
  if (result.validCount == 0) result.errorBits |= ERROR_NO_VALID_TC;
  return result;
}

const char *Thermocouples::statusName(SensorStatus status) {
  switch (status) {
    case SensorStatus::OK: return "OK";
    case SensorStatus::FAULT: return "FAULT";
    case SensorStatus::OUTLIER: return "OUTLIER";
    case SensorStatus::RECOVERING: return "RECOVERING";
  }
  return "FAULT";
}
