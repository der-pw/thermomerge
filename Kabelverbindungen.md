# ThermoMerge – Kabelverbindungen

Diese Übersicht beschreibt die in `src/config/config.h` festgelegte Standardbelegung für den Raspberry Pi Pico 2. Abweichende Anschlüsse müssen sowohl in der Firmware als auch hier angepasst werden.

## Gemeinsame elektrische Regeln

- Pico-GPIOs sind **nicht 5-V-tolerant**. Alle Signale zu GPIOs müssen 3,3-V-kompatibel sein.
- Pico und digitale Module benötigen eine gemeinsame Masse (`GND`).
- Vor dem Verdrahten die Pinbeschriftung des konkreten Breakout-Boards prüfen; Pinreihenfolge und Versorgung unterscheiden sich je nach Hersteller.
- Heizer und Netzspannung niemals direkt mit dem Pico verbinden. Die Netz-/Lastseite ist nach Datenblatt und durch eine qualifizierte Elektrofachkraft zu verdrahten.

## Drei Typ-K-Thermoelemente mit MAX31855

Alle MAX31855-Module teilen sich MISO und SCK. Jedes Modul erhält eine eigene CS-Leitung.

```text
Pico 2 3V3  ----------------------> VIN/VCC aller drei MAX31855-Module*
Pico 2 GND  ----------------------> GND aller drei MAX31855-Module
Pico 2 GP16 (SPI0 MISO/RX) -------> DO  von MAX31855 #1, #2 und #3
Pico 2 GP18 (SPI0 SCK) -----------> CLK von MAX31855 #1, #2 und #3
Pico 2 GP17 ----------------------> CS  von MAX31855 #1 (Thermoelement 1)
Pico 2 GP20 ----------------------> CS  von MAX31855 #2 (Thermoelement 2)
Pico 2 GP21 ----------------------> CS  von MAX31855 #3 (Thermoelement 3)

Typ-K-Fühler 1 (+/-) -------------> T+ / T- von MAX31855 #1
Typ-K-Fühler 2 (+/-) -------------> T+ / T- von MAX31855 #2
Typ-K-Fühler 3 (+/-) -------------> T+ / T- von MAX31855 #3

* Versorgung nur dann direkt mit 3,3 V, wenn das Breakout-Board dafür ausgelegt ist.
```

Die Thermoelementpolarität muss stimmen. Die Modulversorgung ist anhand des jeweiligen Breakout-Datenblatts zu prüfen. GP19/MOSI wird von den MAX31855 nicht verwendet.

## ADS1220 und MEDTHERM-Schmidt-Boelter-Sonde

### ADS1220 zum Pico 2

```text
Pico 2 3V3  ----------------------> ADS1220 DVDD
Pico 2 3V3  ----------------------> ADS1220 AVDD
Pico 2 GND  ----------------------> ADS1220 DGND
Pico 2 GND  ----------------------> ADS1220 AVSS / AGND
Pico 2 GP18 (SPI0 SCK) -----------> ADS1220 SCLK
Pico 2 GP19 (SPI0 MOSI/TX) -------> ADS1220 DIN
Pico 2 GP16 (SPI0 MISO/RX) <------- ADS1220 DOUT/DRDY
Pico 2 GP6  ----------------------> ADS1220 CS (aktiv LOW)
Pico 2 GP7  <---------------------- ADS1220 DRDY (aktiv LOW)
ADS1220 CLK ----------------------> nicht verbinden (interner Oszillator)
```

### MEDTHERM-Eingangsbeschaltung

Der Sensor wird differentiell zwischen AIN0 und AIN1 gemessen. GTW+ und GTW− dürfen nicht direkt miteinander oder mit GND verbunden werden.

```text
                         3.3 V
                           |
                         10 MΩ
                           |
MEDTHERM GTW+ -------------+---- 1 kΩ ----+---- AIN0 (ADS1220)
                                             |
                                           100 nF
                                             |
MEDTHERM GTW− -------------+---- 1 kΩ ----+---- AIN1 (ADS1220)
                           |
                         10 MΩ
                           |
                          GND
```

Der 100-nF-Kondensator liegt direkt zwischen AIN0 und AIN1. Die beiden 10-MΩ-Widerstände setzen den Common-Mode-Arbeitspunkt; die beiden 1-kΩ-Widerstände und der Kondensator bilden den differentiellen Eingangsfilter. Optionale 10-nF-Kondensatoren von AIN0 und AIN1 nach AGND sind nur bei Bedarf für zusätzliche Common-Mode-Filterung vorgesehen.

## I²C-LCD 1602 mit PCF8574

```text
Pico 2 3V3  ----------------------> LCD-PCF8574 VCC (nur 3,3-V-taugliches Backpack)
Pico 2 GND  ----------------------> LCD-PCF8574 GND
Pico 2 GP4 (I²C SDA) -------------> LCD-PCF8574 SDA
Pico 2 GP5 (I²C SCL) -------------> LCD-PCF8574 SCL
```

Die Firmware erwartet standardmäßig die I²C-Adresse `0x27`. Viele Backpacks arbeiten mit 5 V und ziehen SDA/SCL über Widerstände auf 5 V. Solche Backpacks nicht direkt an den Pico anschließen: ein 3,3-V-taugliches Backpack oder ein geeigneter bidirektionaler I²C-Pegelwandler ist erforderlich. Kontrast und Hintergrundbeleuchtung werden am LCD-Backpack eingestellt.

## Drehencoder mit Taster

```text
Pico 2 3V3  ----------------------> Encoder VCC (falls vorhanden und 3,3-V-tauglich)
Pico 2 GND  ----------------------> Encoder GND / COM
Pico 2 GP10 ----------------------> Encoder A / CLK
Pico 2 GP11 ----------------------> Encoder B / DT
Pico 2 GP12 ----------------------> Encoder SW (Taster)
Pico 2 GND  ----------------------> anderer Tasterkontakt (falls separat herausgeführt)
```

Die Firmware aktiviert interne Pull-ups. A, B und SW werden beim Betätigen gegen GND geschaltet. Externe Pull-ups sind daher normalerweise nicht nötig. Falls die Drehrichtung verkehrt ist, A und B tauschen.

## SSR-Steuereingang und Heizer

```text
Pico 2 GP22 (SSR-Signal, aktiv HIGH) ----> IN+ / Steuereingang+
Pico 2 GND ------------------------------> IN− / Steuereingang−

SSR-Leistungsseite: gemäß SSR-Datenblatt und Anlagenplanung.
```

GP22 ist ausschließlich ein Logik-Steuersignal. Ein SSR mit zu hohem Eingangsstrom oder ein nacktes SSR kann eine geeignete Treiberstufe benötigen; den Pico-GPIO nicht überlasten. Eine Pulldown-Beschaltung muss den SSR-Eingang beim Start/Reset sicher AUS halten. Die Netz- und Heizlastseite gehört nicht an den Pico. Für einen realen Heizer sind zusätzlich unabhängiger Sicherheitstemperaturbegrenzer (STB), passend ausgelegtes Leistungsschütz, Sicherungen und eine hardwareseitige Abschaltkette erforderlich. Das SSR-Datenblatt und die tatsächliche Steuereingangspolarität prüfen.

## USB und Status-LED

- Pico-USB an den PC anschließen: darüber läuft USB-CDC-Serial (Firmware-Konfiguration und Telemetrie, 115200 Baud).
- `LED_BUILTIN` ist die interne Status-LED des Pico; dafür ist keine externe Kabelverbindung vorgesehen.

## Vor dem Einschalten prüfen

1. Versorgungspins und Polarität jedes Moduls mit dessen Datenblatt abgleichen.
2. Sicherstellen, dass kein Pico-GPIO über 3,3 V angehoben wird, insbesondere durch LCD-I²C-Pull-ups.
3. Gemeinsame GND-Verbindung der Logikmodule prüfen.
4. SSR zunächst ohne angeschlossene Heizlast mit ungefährlicher Prüflast bzw. Messgerät testen und die AUS-Polarität bei Boot/Reset verifizieren.
5. Die Leistungsseite des Heizers und alle Netzspannungsverbindungen unabhängig von der Pico-Logik fachgerecht absichern und prüfen lassen.

## Nicht aus dem Repository ableitbare Details

Die GPIO-Zuordnung ist in der Firmware festgelegt. Nicht festgelegt sind konkrete Kabel-/Steckerfarben, die exakte Pinreihenfolge der verwendeten Breakout-Boards, die tatsächliche Versorgungstoleranz der Module sowie die Stromaufnahme und Polarität des konkreten SSR-Eingangs. Dafür gelten die Datenblätter der tatsächlich eingesetzten Bauteile.
