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

- Menue, SETPOINT-Dialog und Autotune-Setup werden alle 100 ms aktualisiert; Status- und
  Messwertansichten werden alle 500 ms aktualisiert. Diese LCD-Intervalle
  aendern die davon unabhaengigen Sensor-Polling-Intervalle nicht. Im SETPOINT-
  Dialog zaehlt eine Rastung bei langsamer Drehung 1 °C; bei Rastungsabstaenden
  bis 120 ms zaehlt sie 10 °C.

- Das 1602-LCD bleibt zunächst die vorgesehene lokale Anzeige; ein Wechsel auf
  ein größeres oder grafisches Display ist nicht erforderlich.
- PID-Tuningparameter und Autotune-Detailwerte bleiben seriell. Der lokale
  Menüpunkt `PID HEATER` bietet `AUTOTUNE` und `MANUAL`; Autotune darf einen
  eigenen, nicht persistenten Sollwert und Start/Stop in einem mehrstufigen,
  auf 16x2 lesbaren Ablauf anbieten. Der Sollwert ist beim ersten Aufruf aus
  `SETPOINT` vorbelegt, wird danach bis zum
  Neustart beibehalten und mit 1-/10-Grad-Rastungen geändert. Eine
  Startauswahl ist erforderlich. Der Status `NUR 2 TC + OK` zeigt fehlende
  Startfreigabe; im Lauf darf `KURZ=STOP` erscheinen. Ergebnis und Stoppstatus
  dürfen angezeigt werden, jedoch keine Zyklen oder berechneten PID-Werte.
- Lokale Anzeigeergänzungen dürfen Temperatur, Sollwert, Ausgang, Modus,
  Profilfortschritt und Fehlerstatus darstellen, ohne die serielle
  Steuerungsarchitektur zu umgehen.
- Der lokale Bedienfluss startet nach dem Splash auf dem Dashboard. Außerhalb
  von `PROFILE` und `AUTOTUNE` öffnet Longpress das lokale Menü mit
  `SETPOINT`, `HEATFLUX`, `PID HEATER` und `SSR TEST`.
  Dashboard-Drehen wechselt zwischen Hauptseite und Thermoelement-Messwerten.
  Im Menü dreht die
  Auswahl, kurzer Tastendruck öffnet den markierten Eintrag. In `HEATFLUX` dreht
  der Encoder zwischen Rohspannung `RAW` in mV und kalibriertem `W/m2` um.
  Longpress geht in Untermenüs jeweils eine Ebene zurück; am Hauptmenü führt
  er zum Dashboard.
- `SETPOINT` führt zuerst zur Sollwertwahl und danach zur Bestätigung
  `HEATER ON? YES/NO`. Nur `YES` startet den Temperaturregelkreis und wechselt
  zum Dashboard mit `RUN`. `NO` sowie Longpress im Dialog setzen Sollwert und
  Ausgang auf 0/AUS und kehren ins Hauptmenü zurück. Kurzdruck im laufenden
  `SETPOINT`-Dashboard startet die vorläufige SP-Bearbeitung (`SP:` blinkt);
  Drehen ändert den Vorschauwert mit 1-/10-°C-Schritten und Kurzdruck übernimmt
  ihn sofort. Der normale Sollwert wird nicht persistent gespeichert und
  startet nach jedem Neustart auf 0. Longpress in der Vorschau verwirft
  den Entwurf und lässt den Regelkreis unverändert weiterlaufen. Longpress im
  Dashboard außerhalb der Vorschau stoppt den Regelkreis, setzt SP auf 0 und
  bleibt auf dem Dashboard; ein weiterer Longpress öffnet das Hauptmenü.
  Longpress im Hauptmenü kehrt zum Dashboard zurück.
  Die zweite Dashboard-Seite zeigt die aktuellen Werte aller drei Kanäle,
  auch `OUTLIER`; MAX31855-Fehler, fehlende Werte und Übertemperatur erscheinen
  im betroffenen Vier-Zeichen-Feld als `FAIL`. Bei globalem `FAULT` bleibt die
  zweite Seite sichtbar, während die Hauptseite `FLT` anzeigt.
  Der Autotune-Sollwert ist davon unabhängig.
- Jede Änderung an Display-, Encoder- oder Menüverhalten muss im selben
  Arbeitsgang im README-Abschnitt „Lokale Bedienung“ dokumentiert werden.
  Dazu gehören Menüeinträge, Navigation, Ein-/Ausstieg sowie angezeigte
  Werte und Zustände; Display-Mockups sind bei betroffenen Anzeigen ebenfalls
  nachzuführen.
- Eine serielle Verbindung sperrt den Encoder nicht implizit. Der PC muss den
  UI-Besitz ausdrücklich mit `UI,CONNECT` übernehmen; danach sind sämtliche
  Encoder-Aktionen blockiert. `UI,DISCONNECT` gibt die lokale Bedienung wieder
  frei. Der Besitzstatus muss auf der normalen LCD-Statusseite neben der
  gemittelten Temperatur sichtbar sein (`PC` beziehungsweise `LOC`). Dieser
  Besitzstatus wird nicht persistent gespeichert.
- Der Start-Splash zeigt jedes Gerät 1,5 Sekunden: Nach 0,5 Sekunden erscheint
  `PASS`/`FAIL` und bleibt eine Sekunde sichtbar.
- Das lokale Menue fuehrt zusaetzlich `SSR TEST`. Der Test ist nur bei Modus
  `OFF`, mindestens einem gueltigen Thermoelement und ohne `FAULT` zulaessig.
  Er schaltet den SSR dreimal 500 ms EIN, jeweils mit 500 ms AUS dazwischen,
  und endet sicher AUS. Kurzdruck bricht ab und führt zum Dashboard;
  Longpress bricht ab und führt zum Hauptmenü. Automatisches Ende führt zum
  Hauptmenü. Die README dokumentiert Menuefuehrung, Freigabe und Anzeige.
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
