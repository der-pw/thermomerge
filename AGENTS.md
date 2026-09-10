# AGENTS.md

## Projektüberblick

ThermoMerge ist eine nichtblockierende Firmware für den Raspberry Pi Pico 2
(RP2350) mit Arduino-Pico-Core. Die Firmware liest drei Typ-K-
Thermoelemente über MAX31855, bildet daraus eine plausibilisierte
Regeltemperatur und steuert einen Heizer über ein SSR. Zusätzlich gibt es PID,
Autotune, Temperaturprofile, LCD/Encoder-Bedienung und serielle Telemetrie.

Die Regelung muss auch ohne USB-Verbindung autonom laufen. Sicherheit und
fail-safe-Verhalten haben Vorrang vor Komfortfunktionen.

## Architektur und Ordnerstruktur

```text
src/
├── main.cpp
├── config/                 zentrale Hardware- und Regelparameter
├── core/                   Runtime-Zustand und Fehlerstatus
├── control/                PID, Autotune und Temperaturprofile
├── sensors/                Thermoelemente und Wärmestromsensor
│   └── heatflux/
│       └── backends/       austauschbare Analog-/ADC-Backends
├── actuators/              SSR-/Heizerausgang
├── user_interface/         Display und Drehencoder
├── communication/          serielles Protokoll und Telemetrie
└── persistence/            EEPROM-Einstellungen
```

Neue Quelldateien werden dem fachlich passenden Unterordner zugeordnet. Für
projektinterne Includes werden Pfade relativ zum `src`-Verzeichnis verwendet,
beispielsweise:

```cpp
#include "config/config.h"
#include "control/controller.h"
#include "sensors/heatflux/heatflux.h"
```

`main.cpp` koordiniert nur die Module und enthält keine hardwareabhängige
Implementierung, die in ein eigenes Modul gehört.

## Build und Verifikation

Voraussetzung ist PlatformIO. Der definierte Build ist:

```text
pio run -e pico2
```

Alternativ kann der lokal installierte PlatformIO-Aufruf verwendet werden:

```text
platformio run -e pico2
```

Vor dem Abschluss einer Änderung mindestens den Build ausführen. Zusätzlich
sollten whitespace-/Patch-Prüfungen ohne Fehler durchlaufen. Ein Upload wird
nicht automatisch durchgeführt; dafür muss ein echter Pico angeschlossen sein
und der Nutzer den Upload ausdrücklich beauftragen.

## Implementierungsregeln

- Die Hauptschleife bleibt kooperativ und nichtblockierend.
- Keine `delay()`-Aufrufe für Prozesssteuerung.
- Periodische Aufgaben werden mit `millis()`-Differenzen geplant.
- Keine dynamische Speicherverwaltung in zyklischen Regelungs- oder
  Sensorroutinen.
- Sicherheitsabschaltungen müssen den Heizer zuverlässig auf AUS setzen.
- Änderungen an PID, Autotune, Display, Protokoll oder Hauptlogik dürfen die
  Thermoelement-Sicherheitslogik nicht umgehen.
- Hardwareparameter gehören nach `src/config/config.h` und nicht als magische
  Werte in die Implementierung.
- Änderungen an Pinbelegung oder Geräten müssen zusätzlich in der README im
  Abschnitt „Textbasierte Anschlusspläne“ dokumentiert werden.

## Bediengrenzen von Display und Encoder

- Das 1602-LCD bleibt zunächst die vorgesehene lokale Anzeige; ein Wechsel auf
  ein größeres oder grafisches Display ist nicht erforderlich.
- PID-Tuningwerte und Autotune-Steuerung bleiben seriell. Display und Encoder
  dürfen dafür keine lokale Parametrierungs- oder Autotune-Menüführung
  voraussetzen oder einführen.
- Lokale Anzeigeergänzungen dürfen Temperatur, Sollwert, Ausgang, Modus,
  Profilfortschritt und Fehlerstatus darstellen, ohne die serielle
  Steuerungsarchitektur zu umgehen.
- Ein lokaler Sollwertdialog ist zulässig: Longpress öffnet ihn, Änderungen
  bleiben bis zur Anzeigeauswahl `OK`/`ABBR.` vorläufig. PID-/Autotune-Befehle
  und PID-Parameter bleiben davon getrennt und seriell.
- Eine serielle Verbindung sperrt den Encoder nicht implizit. Der PC muss den
  UI-Besitz ausdrücklich mit `UI,CONNECT` übernehmen; danach sind sämtliche
  Encoder-Aktionen blockiert. `UI,DISCONNECT` gibt die lokale Bedienung wieder
  frei. Der Besitzstatus muss auf der normalen LCD-Statusseite neben der
  gemittelten Temperatur sichtbar sein (`PC` beziehungsweise `LOC`). Dieser
  Besitzstatus wird nicht persistent gespeichert.
- Die Detailinformationen des Autotunings bleiben auf der seriellen
  Schnittstelle. Das 1602 muss während `AUTOTUNE` weder Zyklusfortschritt noch
  berechnete PID-Werte anzeigen. `EVENT,AUTOTUNE=SUCCESS,KP=...,KI=...,KD=...`
  wird erst nach Übernahme und Speicherungsauslösung der neuen Parameter
  ausgegeben und gilt für den PC als Übernahmebestätigung.

## Wärmestromsensor / AnalogInput

Der Schmidt-Boelter-Wärmestromsensor ist bewusst hardwareabstrahiert:

- `sensors/heatflux/analog_input.h` definiert das nichtblockierende
  `AnalogInput`-Interface.
- `HeatFluxSensor` ist die hardwareunabhängige Fassade für Rohwert,
  Engineering-Wert, Status, Gültigkeit, Fehler und Kalibrierung.
- `backends/heatflux_backend_none.*` ist das aktuell ausgewählte Backend.
- Das `NONE`-Backend darf keine ADC-, GPIO- oder I²C-Hardware initialisieren.
- Der definierte Stub-Zustand ist `enabled=false`, `valid=false`,
  `fault=false`, `rawValue=NAN` und Engineering-Wert `NAN`.
- Die Telemetrie darf `HF` nur ausgeben, wenn das Backend aktiviert und der
  Messwert gültig ist.
- Die Umrechnung lautet immer:

  ```text
  engineering_value = raw_value * calibration_factor + calibration_offset
  ```

- Kalibrierfaktor und Offset stehen zentral in `config/config.h`; die Einheit
  des Engineering-Werts ist kW/m².
- Für spätere Backends (`HEATFLUX_BACKEND_PICO_ADC` oder
  `HEATFLUX_BACKEND_I2C_ADC`) dürfen keine Änderungen am PID-Regelkreis oder
  an der übergeordneten Steuerungsarchitektur erforderlich werden.
- Solange die ADC-Hardware nicht festgelegt ist, dürfen keine konkrete
  ADC-Bibliothek, kein ADC-Pin und keine elektrische Signalannahme ergänzt
  werden.
- Der Wärmestromwert darf niemals als Eingang der normalen PID-Regelung
  verwendet werden. Er ist ausschließlich für Anzeige, serielle Ausgabe und
  Heizsystemkalibrierung vorgesehen.

## Hardware- und Sicherheitsregeln

- Die Pico-GPIOs sind nicht 5-V-tolerant. Externe Signale müssen
  3,3-V-kompatibel sein.
- I²C-LCD-Backpacks mit 5-V-Pull-ups benötigen ein 3,3-V-taugliches Backpack
  oder einen geeigneten Pegelwandler.
- `GP22` ist nur ein SSR-Steuersignal. Netzspannung und Heizlast dürfen nicht
  am Pico angeschlossen werden.
- Ein realer Heizer benötigt zusätzlich unabhängigen Übertemperaturschutz,
  Sicherungen, geeignete Leistungsschütze/SSR-Auslegung und eine
  hardwareseitige Abschaltkette.
- Änderungen an SSR-Polarität, MAX31855-Versorgung oder LCD-Pegel müssen vor
  dem Einsatz anhand der konkreten Hardware-Datenblätter geprüft werden.

## Änderungen dokumentieren

Bei strukturellen oder hardwarebezogenen Änderungen sind mindestens folgende
Stellen zu prüfen:

1. betroffene Moduldateien unter `src/`;
2. zentrale Parameter in `src/config/config.h`;
3. Anschlusspläne und Architekturhinweise in `README.md`;
4. Build mit `pio run -e pico2`.

Bestehende Änderungen im Arbeitsbaum gehören dem Nutzer. Unzusammenhängende
Änderungen dürfen nicht zurückgesetzt oder überschrieben werden.
