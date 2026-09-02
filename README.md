# Pico 2: 3-Kanal-Typ-K-Messung mit MAX31855

Dieses PlatformIO-Projekt liest drei MAX31855-Thermoelement-Wandler am Raspberry Pi Pico 2 (RP2350) aus. Es gibt die drei Temperaturen und den Mittelwert einmal pro Sekunde über USB-Serial aus. Die Ausgabe wird auf ganze Grad Celsius gerundet.

## Hardware und Verdrahtung

Alle drei MAX31855-Breakouts teilen sich den SPI0-Takt und die Datenleitung. Jeder Wandler braucht eine eigene CS-Leitung.

| Pico-2-Pin | MAX31855-Signal |
| --- | --- |
| GP16 (SPI0 RX / MISO) | DO aller drei Module |
| GP18 (SPI0 SCK) | CLK aller drei Module |
| GP17 | CS Kanal 1 |
| GP20 | CS Kanal 2 |
| GP21 | CS Kanal 3 |
| 5 V (VBUS) | VIN aller Breakouts |
| GND | GND aller Breakouts |

Die interne Pico-LED (`LED_BUILTIN`) dient als Statusanzeige und benötigt keine externe Verdrahtung.

Die DO-Ausgaenge der verwendeten Breakouts muessen 3,3-V-logikkompatibel sein. Ein MAX31855 ist ein SPI-Read-only-Geraet; der MOSI-Pin wird nicht benoetigt.

## Build und Serial Monitor

PlatformIO erkennt die Umgebung `pico2`. Build und Upload:

```sh
pio run -t upload
```

Den Monitor mit 115200 Baud starten:

```sh
pio device monitor
```

Beispielausgabe:

```text
T1: 123 °C | T2: 124 °C | T3: 123 °C | AVG: 123 °C
```

Der MAX31855 misst intern mit einer Auflösung von 0,25 °C; für die serielle Ausgabe werden die Werte auf ganze Grad gerundet.

## Verhalten bei Fehlern

Die Adafruit MAX31855 Library liefert bei einem Fehler `NaN`; zusaetzlich werden die MAX31855-Statusbits ausgewertet. Pro fehlerhaftem Kanal erscheint `ERROR` samt Fehlerursache (Thermoelement offen, Kurzschluss gegen GND oder Kurzschluss gegen VCC). Der Mittelwert verwendet ausschliesslich gueltige Kanaele:

- drei oder zwei gueltige Kanaele: Mittelwert dieser Kanaele;
- ein gueltiger Kanal: dessen Temperatur als `AVG`, mit Warnung;
- kein gueltiger Kanal: `AVG: ERROR`.

Bei fehlerfreien Messungen pulsiert die interne LED einmal pro Messzyklus kurz. Sobald mindestens ein Thermoelement fehlerhaft oder ungueltig ist, blinkt die LED schnell.

Die Messung ist mit `millis()` getaktet und verwendet im Messzyklus keine dynamische Speicherallokation. Dadurch bleiben in `loop()` Reserven fuer ein spaeteres 1602-I2C-Display und einen DAC fuer den 0-10-V-Ausgang.
