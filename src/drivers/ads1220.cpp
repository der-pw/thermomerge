#include "drivers/ads1220.h"

#include <math.h>

ADS1220::ADS1220(SPIClass &spi, uint8_t chipSelectPin, int8_t dataReadyPin,
                 uint32_t spiClockHz)
    : spi_(spi), chipSelectPin_(chipSelectPin), dataReadyPin_(dataReadyPin),
      spiClockHz_(spiClockHz) {}

void ADS1220::begin() {
  pinMode(chipSelectPin_, OUTPUT);
  deselect();
  if (dataReadyPin_ >= 0) pinMode(static_cast<uint8_t>(dataReadyPin_), INPUT);
}

bool ADS1220::reset() {
  initialized_ = false;
  error_ = Error::NOT_INITIALIZED;
  return sendCommand(kCommandReset);
}

bool ADS1220::configure(const Settings &settings) {
  if (!validSettings(settings)) {
    error_ = Error::INVALID_CONFIGURATION;
    return false;
  }

  const uint8_t registers[kRegisterCount] = {
      static_cast<uint8_t>((static_cast<uint8_t>(settings.mux) << 4) |
                           (static_cast<uint8_t>(settings.gain) << 1)),
      static_cast<uint8_t>((static_cast<uint8_t>(settings.dataRate) << 5) |
                           (settings.conversionMode == ConversionMode::CONTINUOUS ? 0x04 : 0x00)),
      static_cast<uint8_t>((static_cast<uint8_t>(settings.reference) << 6) |
                           (static_cast<uint8_t>(settings.lineFilter) << 4)),
      0x02,  // DRDYM: also drive DOUT/DRDY low while data is ready.
  };
  if (!writeRegisters(0, registers, kRegisterCount)) return false;

  uint8_t readback[kRegisterCount]{};
  if (!readRegisters(0, readback, kRegisterCount)) return false;
  for (uint8_t i = 0; i < kRegisterCount; ++i) {
    if (readback[i] != registers[i]) {
      error_ = Error::SPI_READBACK;
      return false;
    }
  }
  settings_ = settings;
  initialized_ = true;
  error_ = Error::NONE;
  return true;
}

bool ADS1220::writeRegister(uint8_t address, uint8_t value) {
  return writeRegisters(address, &value, 1);
}

bool ADS1220::readRegister(uint8_t address, uint8_t &value) {
  return readRegisters(address, &value, 1);
}

bool ADS1220::startSync() {
  if (!initialized_) {
    error_ = Error::NOT_INITIALIZED;
    return false;
  }
  return sendCommand(kCommandStartSync);
}

bool ADS1220::conversionReady() const {
  return dataReadyPin_ < 0 || digitalRead(static_cast<uint8_t>(dataReadyPin_)) == LOW;
}

bool ADS1220::readRaw(int32_t &raw) {
  if (!initialized_) {
    error_ = Error::NOT_INITIALIZED;
    return false;
  }
  beginTransaction();
  select();
  spi_.transfer(kCommandReadData);
  const uint32_t value = (static_cast<uint32_t>(spi_.transfer(0x00)) << 16) |
                         (static_cast<uint32_t>(spi_.transfer(0x00)) << 8) |
                         spi_.transfer(0x00);
  deselect();
  endTransaction();
  raw = signExtend24(value);
  error_ = Error::NONE;
  return true;
}

bool ADS1220::readVoltage(float &voltage, int32_t *raw) {
  int32_t sample;
  if (!readRaw(sample)) return false;
  voltage = rawToVoltage(sample, settings_.referenceVoltage, settings_.gain);
  if (raw) *raw = sample;
  return isfinite(voltage);
}

float ADS1220::rawToVoltage(int32_t raw, float referenceVoltage, Gain gain) {
  const uint16_t gainMultiplier = gainValue(gain);
  if (!isfinite(referenceVoltage) || referenceVoltage <= 0.0f || gainMultiplier == 0) return NAN;
  // ADS1220 full scale is +/- VREF / gain; codes are signed 24-bit two's complement.
  return static_cast<float>(raw) * referenceVoltage /
         (static_cast<float>(gainMultiplier) * 8388608.0f);
}

uint16_t ADS1220::gainValue(Gain gain) {
  return static_cast<uint16_t>(1U << static_cast<uint8_t>(gain));
}

void ADS1220::select() const { digitalWrite(chipSelectPin_, LOW); }
void ADS1220::deselect() const { digitalWrite(chipSelectPin_, HIGH); }
void ADS1220::beginTransaction() const { spi_.beginTransaction(SPISettings(spiClockHz_, MSBFIRST, SPI_MODE1)); }
void ADS1220::endTransaction() const { spi_.endTransaction(); }

bool ADS1220::sendCommand(uint8_t command) {
  beginTransaction();
  select();
  spi_.transfer(command);
  deselect();
  endTransaction();
  return true;
}

bool ADS1220::readRegisters(uint8_t address, uint8_t *values, uint8_t count) {
  if (!values || count == 0 || address + count > kRegisterCount) return false;
  beginTransaction();
  select();
  spi_.transfer(static_cast<uint8_t>(kCommandReadRegister | address));
  spi_.transfer(static_cast<uint8_t>(count - 1));
  for (uint8_t i = 0; i < count; ++i) values[i] = spi_.transfer(0x00);
  deselect();
  endTransaction();
  return true;
}

bool ADS1220::writeRegisters(uint8_t address, const uint8_t *values, uint8_t count) {
  if (!values || count == 0 || address + count > kRegisterCount) return false;
  beginTransaction();
  select();
  spi_.transfer(static_cast<uint8_t>(kCommandWriteRegister | address));
  spi_.transfer(static_cast<uint8_t>(count - 1));
  for (uint8_t i = 0; i < count; ++i) spi_.transfer(values[i]);
  deselect();
  endTransaction();
  return true;
}

bool ADS1220::validSettings(const Settings &settings) const {
  if (!isfinite(settings.referenceVoltage) || settings.referenceVoltage <= 0.0f) return false;
  if (settings.dataRate != DataRate::SPS_20 && settings.lineFilter != LineFilter::DISABLED) return false;
  return true;
}
