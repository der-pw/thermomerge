#pragma once

#include <Arduino.h>

namespace config {

// SPI0 is shared by the MAX31855 converters and the ADS1220.
constexpr uint8_t SPI_MISO_GPIO = 16;
constexpr uint8_t SPI_SCK_GPIO = 18;
constexpr uint8_t SPI_MOSI_GPIO = 19;
constexpr uint8_t TC_CS_GPIOS[3] = {17, 20, 21};

// ADS1220 / MEDTHERM Schmidt-Boelter input. The dedicated DRDY line keeps
// conversion waiting non-blocking even though DOUT shares SPI MISO.
constexpr uint8_t ADS1220_CS_GPIO = 6;
constexpr int8_t ADS1220_DRDY_GPIO = 7;
constexpr uint32_t ADS1220_SPI_CLOCK_HZ = 1000000UL;
constexpr uint8_t ADS1220_PGA_GAIN = 32;
constexpr float ADS1220_INTERNAL_REFERENCE_V = 2.048f;
constexpr uint32_t ADS1220_CONVERSION_TIMEOUT_MS = 100;
constexpr uint32_t ADS1220_RETRY_INTERVAL_MS = 1000;

// Hardware defaults chosen from currently unused pins. Change them here only.
constexpr uint8_t SSR_GPIO = 22;
constexpr bool SSR_ACTIVE_HIGH = true;
constexpr uint8_t I2C_SDA_GPIO = 4;
constexpr uint8_t I2C_SCL_GPIO = 5;
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr bool LCD_ENABLED = true;
constexpr uint8_t ENCODER_A_GPIO = 10;
constexpr uint8_t ENCODER_B_GPIO = 11;
constexpr uint8_t ENCODER_BUTTON_GPIO = 12;

// The ADS1220 backend supplies differential voltage in V as its raw value.
// TODO: replace NAN with the measured MEDTHERM calibration in kW/m²/V. The
// value is intentionally not guessed; engineering values remain unavailable
// until a finite, positive factor is configured.
constexpr float SCHMIDT_BOELTER_SENSITIVITY_MV_PER_KW_M2 = NAN;
constexpr float HEAT_FLUX_CALIBRATION_FACTOR = NAN;
constexpr float HEAT_FLUX_CALIBRATION_OFFSET_KW_M2 = 0.0f;

constexpr uint32_t TC_INTERVAL_MS = 100;
constexpr uint32_t PID_SAMPLE_MS = 100;
constexpr uint32_t HEAT_FLUX_INTERVAL_MS = 150;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 250;
constexpr uint32_t LCD_INTERVAL_MS = 500;
constexpr uint32_t SSR_WINDOW_MS = 1000;
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 5000;
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 2000;
constexpr uint32_t TASK_HEALTH_TIMEOUT_MS = 750;

constexpr uint8_t TC_FAULT_TRIP_COUNT = 3;
constexpr uint8_t TC_RECOVERY_COUNT = 5;
constexpr float TC_MAX_DEVIATION_C = 30.0f;
constexpr float MAX_OUTPUT_SINGLE_TC_PERCENT = 25.0f;
constexpr float MAX_TEMPERATURE_C = 800.0f;
constexpr float MIN_SETPOINT_C = 0.0f;
constexpr float MAX_SETPOINT_C = 750.0f;

constexpr float DEFAULT_SETPOINT_C = 25.0f;
constexpr float DEFAULT_KP = 2.0f;
constexpr float DEFAULT_KI = 0.10f;
constexpr float DEFAULT_KD = 5.0f;
constexpr float ENCODER_STEP_C = 1.0f;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 1000;

constexpr uint8_t PROFILE_MAX_STEPS = 32;
constexpr uint16_t SERIAL_RX_BUFFER_SIZE = 192;
constexpr uint8_t SERIAL_RX_BUDGET_PER_LOOP = 64;
constexpr uint16_t SERIAL_TX_BUFFER_SIZE = 2048;
constexpr float PROFILE_MIN_RATE_K_PER_MIN = 0.1f;

constexpr float AUTOTUNE_RELAY_PERCENT = 15.0f;
constexpr float AUTOTUNE_NOISE_BAND_C = 1.0f;
constexpr uint8_t AUTOTUNE_REQUIRED_CYCLES = 5;
constexpr uint8_t AUTOTUNE_MIN_VALID_TC = 2;
constexpr uint32_t AUTOTUNE_TIMEOUT_MS = 45UL * 60UL * 1000UL;

}  // namespace config
