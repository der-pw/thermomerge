#include <Arduino.h>
#include <Adafruit_MAX31855.h>
#include <math.h>

// SPI0 bus shared by all three MAX31855 modules.
// Do not use the core-defined PIN_SPI0_* macro names for local constants.
constexpr uint8_t SPI0_MISO_GPIO = 16;
constexpr uint8_t SPI0_SCK_GPIO = 18;

// A separate chip-select line is required for each converter.
constexpr uint8_t PIN_CS_T1 = 17;
constexpr uint8_t PIN_CS_T2 = 20;
constexpr uint8_t PIN_CS_T3 = 21;

constexpr uint32_t MEASUREMENT_INTERVAL_MS = 500;
constexpr uint32_t NORMAL_LED_PULSE_MS = 100;
constexpr uint32_t FAULT_LED_TOGGLE_MS = 100;

struct ChannelReading {
  float temperatureC;
  uint8_t fault;
  bool valid;
};

// The CS-only constructor uses the hardware SPI bus (SPI) configured below.
Adafruit_MAX31855 max31855T1(PIN_CS_T1);
Adafruit_MAX31855 max31855T2(PIN_CS_T2);
Adafruit_MAX31855 max31855T3(PIN_CS_T3);

uint32_t lastMeasurementMs = 0;
uint32_t lastLedChangeMs = 0;
bool thermocoupleFault = false;
bool ledOn = false;

void setStatusLed(bool on, uint32_t now) {
  ledOn = on;
  lastLedChangeMs = now;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void updateStatusLed(uint32_t now) {
  const uint32_t interval = thermocoupleFault ? FAULT_LED_TOGGLE_MS : NORMAL_LED_PULSE_MS;
  if (now - lastLedChangeMs >= interval) {
    setStatusLed(thermocoupleFault ? !ledOn : false, now);
  }
}

ChannelReading readChannel(Adafruit_MAX31855 &sensor) {
  const float temperatureC = sensor.readCelsius();
  const uint8_t fault = sensor.readError();

  ChannelReading reading = {
      temperatureC,
      fault,
      !isnan(temperatureC) && fault == 0,
  };
  return reading;
}

float calculateAverage(const ChannelReading readings[], uint8_t &validCount) {
  float sum = 0.0f;
  validCount = 0;

  for (uint8_t i = 0; i < 3; ++i) {
    if (readings[i].valid) {
      sum += readings[i].temperatureC;
      ++validCount;
    }
  }

  return validCount > 0 ? sum / validCount : NAN;
}

void printFault(uint8_t fault) {
  if (fault == 0) {
    Serial.print(F("unbekannter MAX31855-Fehler"));
    return;
  }

  bool separatorRequired = false;
  if (fault & MAX31855_FAULT_OPEN) {
    Serial.print(F("Thermoelement offen"));
    separatorRequired = true;
  }
  if (fault & MAX31855_FAULT_SHORT_GND) {
    if (separatorRequired) Serial.print(F(", "));
    Serial.print(F("Kurzschluss gegen GND"));
    separatorRequired = true;
  }
  if (fault & MAX31855_FAULT_SHORT_VCC) {
    if (separatorRequired) Serial.print(F(", "));
    Serial.print(F("Kurzschluss gegen VCC"));
  }
}

void printChannel(const __FlashStringHelper *label, const ChannelReading &reading) {
  Serial.print(label);
  Serial.print(F(": "));

  if (reading.valid) {
    Serial.print(reading.temperatureC, 0);
    Serial.print(F(" \xC2\xB0" "C"));
  } else {
    Serial.print(F("ERROR ("));
    printFault(reading.fault);
    Serial.print(F(")"));
  }
}

void printMeasurements(const ChannelReading readings[]) {
  uint8_t validCount = 0;
  const float averageC = calculateAverage(readings, validCount);

  printChannel(F("T1"), readings[0]);
  Serial.print(F(" | "));
  printChannel(F("T2"), readings[1]);
  Serial.print(F(" | "));
  printChannel(F("T3"), readings[2]);
  Serial.print(F(" | AVG: "));

  if (validCount == 0) {
    Serial.print(F("ERROR"));
  } else {
    Serial.print(averageC, 0);
    Serial.print(F(" \xC2\xB0" "C"));
  }

  if (validCount == 1) {
    Serial.print(F(" | WARNUNG: Nur ein gueltiger Messkanal"));
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Map SPI0 to the requested Pico 2 GPIOs before the library accesses SPI.
  SPI.setRX(SPI0_MISO_GPIO);
  SPI.setSCK(SPI0_SCK_GPIO);
  SPI.begin();

  const bool t1Started = max31855T1.begin();
  const bool t2Started = max31855T2.begin();
  const bool t3Started = max31855T3.begin();

  Serial.println(F("MAX31855 3-Kanal-Messung gestartet"));
  if (!t1Started || !t2Started || !t3Started) {
    Serial.println(F("WARNUNG: MAX31855-Initialisierung fehlgeschlagen"));
  }
}

void loop() {
  const uint32_t now = millis();
  updateStatusLed(now);

  if (now - lastMeasurementMs < MEASUREMENT_INTERVAL_MS) {
    return;
  }
  lastMeasurementMs = now;

  const ChannelReading readings[] = {
      readChannel(max31855T1),
      readChannel(max31855T2),
      readChannel(max31855T3),
  };
  thermocoupleFault = !readings[0].valid || !readings[1].valid || !readings[2].valid;

  // A healthy measurement produces one short pulse per measurement cycle.
  if (!thermocoupleFault) {
    setStatusLed(true, now);
  }
  printMeasurements(readings);
}
