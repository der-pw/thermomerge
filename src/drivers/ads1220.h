#pragma once

#include <Arduino.h>
#include <SPI.h>

class ADS1220 {
 public:
  enum class Mux : uint8_t {
    AIN0_AIN1 = 0x0,
    AIN0_AIN2 = 0x1,
    AIN0_AIN3 = 0x2,
    AIN1_AIN2 = 0x3,
    AIN1_AIN3 = 0x4,
    AIN2_AIN3 = 0x5,
    AIN1_AIN0 = 0x6,
    AIN3_AIN2 = 0x7,
  };

  enum class Gain : uint8_t {
    GAIN_1 = 0x0, GAIN_2 = 0x1, GAIN_4 = 0x2, GAIN_8 = 0x3,
    GAIN_16 = 0x4, GAIN_32 = 0x5, GAIN_64 = 0x6, GAIN_128 = 0x7,
  };

  enum class DataRate : uint8_t {
    SPS_20 = 0x0, SPS_45 = 0x1, SPS_90 = 0x2, SPS_175 = 0x3,
    SPS_330 = 0x4, SPS_600 = 0x5, SPS_1000 = 0x6,
  };

  enum class Reference : uint8_t {
    INTERNAL_2V048 = 0x0,
    EXTERNAL_PINS = 0x1,
    EXTERNAL_AIN0_AIN3 = 0x2,
    ANALOG_SUPPLY = 0x3,
  };

  enum class ConversionMode : uint8_t { SINGLE_SHOT, CONTINUOUS };
  enum class LineFilter : uint8_t {
    DISABLED = 0x0,
    REJECT_50_AND_60_HZ = 0x1,
    REJECT_50_HZ = 0x2,
    REJECT_60_HZ = 0x3,
  };
  enum class Error : uint8_t { NONE, NOT_INITIALIZED, INVALID_CONFIGURATION, SPI_READBACK };

  struct Settings {
    Mux mux = Mux::AIN0_AIN1;
    Gain gain = Gain::GAIN_32;
    DataRate dataRate = DataRate::SPS_20;
    Reference reference = Reference::INTERNAL_2V048;
    ConversionMode conversionMode = ConversionMode::SINGLE_SHOT;
    LineFilter lineFilter = LineFilter::REJECT_50_AND_60_HZ;
    float referenceVoltage = 2.048f;
  };

  ADS1220(SPIClass &spi, uint8_t chipSelectPin, int8_t dataReadyPin = -1,
          uint32_t spiClockHz = 1000000UL);

  void begin();
  bool reset();
  bool configure(const Settings &settings);
  bool writeRegister(uint8_t address, uint8_t value);
  bool readRegister(uint8_t address, uint8_t &value);
  bool startSync();
  bool conversionReady() const;
  bool readRaw(int32_t &raw);
  bool readVoltage(float &voltage, int32_t *raw = nullptr);

  bool initialized() const { return initialized_; }
  Error error() const { return error_; }
  const Settings &settings() const { return settings_; }
  static constexpr int32_t signExtend24(uint32_t value) {
    value &= 0x00FFFFFFUL;
    return value & 0x00800000UL ? static_cast<int32_t>(value | 0xFF000000UL)
                                 : static_cast<int32_t>(value);
  }
  static float rawToVoltage(int32_t raw, float referenceVoltage, Gain gain);
  static uint16_t gainValue(Gain gain);

 private:
  static constexpr uint8_t kCommandReset = 0x06;
  static constexpr uint8_t kCommandStartSync = 0x08;
  static constexpr uint8_t kCommandReadData = 0x10;
  static constexpr uint8_t kCommandReadRegister = 0x20;
  static constexpr uint8_t kCommandWriteRegister = 0x40;
  static constexpr uint8_t kRegisterCount = 4;

  SPIClass &spi_;
  uint8_t chipSelectPin_;
  int8_t dataReadyPin_;
  uint32_t spiClockHz_;
  Settings settings_{};
  bool initialized_ = false;
  Error error_ = Error::NOT_INITIALIZED;

  void select() const;
  void deselect() const;
  void beginTransaction() const;
  void endTransaction() const;
  bool sendCommand(uint8_t command);
  bool readRegisters(uint8_t address, uint8_t *values, uint8_t count);
  bool writeRegisters(uint8_t address, const uint8_t *values, uint8_t count);
  bool validSettings(const Settings &settings) const;
};

static_assert(ADS1220::signExtend24(0x000000UL) == 0, "24-bit zero must remain zero");
static_assert(ADS1220::signExtend24(0x007FFFFFUL) == 8388607, "positive 24-bit limit");
static_assert(ADS1220::signExtend24(0x00800000UL) == -8388608, "negative 24-bit limit");
static_assert(ADS1220::signExtend24(0x00FFFFFFUL) == -1, "24-bit negative one");
