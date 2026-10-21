# ThermoMerge – autarke Temperaturregelung mit Raspberry Pi Pico 2

ThermoMerge ist eine nichtblockierende Firmware für den Raspberry Pi Pico 2 (RP2350). Sie erfasst drei Typ-K-Thermoelemente über MAX31855, bildet daraus eine plausibilisierte Regeltemperatur und steuert einen elektrischen Heizer über ein SSR. PID-Regelung, Autotune und vollständig auf dem Pico ausgeführte Temperaturprofile funktionieren ohne angeschlossenen Rechner.

Die Firmware startet grundsätzlich in `MODE=OFF`. Ein gespeicherter Sollwert aktiviert den Heizer niemals automatisch.

> **Sicherheit:** Die Softwareabschaltung ist keine normgerechte Sicherheitseinrichtung. Für einen realen Heizer sind ein unabhängiger Sicherheitstemperaturbegrenzer (STB), ein passend ausgelegtes Leistungsschütz, Sicherungen und eine hardwareseitige SSR-Pulldown-Beschaltung erforderlich.

## Hardware und Pinbelegung

| Funktion | Pico-2-GPIO | Hinweise |
| --- | ---: | --- |
| SPI0 RX / MISO | GP16 | DO aller MAX31855 und MISO des ADS1220; 3,3-V-kompatibel |
| MAX31855 T1 CS | GP17 | bestehende Belegung |
| SPI0 SCK | GP18 | CLK aller MAX31855 |
| SPI0 TX / MOSI | GP19 | MOSI des ADS1220 |
| MAX31855 T2 CS | GP20 | bestehende Belegung |
| MAX31855 T3 CS | GP21 | bestehende Belegung |
| ADS1220 CS | GP6 | Wärmestrom-ADC, aktiv LOW |
| ADS1220 DRDY | GP7 | dedizierter Daten-bereit-Ausgang, aktiv LOW |
| SSR-Ausgang | GP22 | aktiv HIGH; externe Pulldown-Beschaltung vorsehen |
| I²C1 SDA | GP2 | 1602-LCD mit PCF8574 (`Wire1`) |
| I²C1 SCL | GP3 | 1602-LCD mit PCF8574 (`Wire1`) |
| Encoder A | GP10 | interner Pull-up |
| Encoder B | GP11 | interner Pull-up |
| Encoder-Taster | GP12 | aktiv LOW, interner Pull-up |
| Wärmestrom-ADC | ADS1220 an GP6/7, SPI0 | fest integrierter ADS1220-Treiber |
| Status-LED | `LED_BUILTIN` | OK langsam, DEGRADED mittel, FAULT schnell |

Alle Pins, Zeitraster, Grenzen und Kalibrierwerte stehen zentral in [`src/config/config.h`](src/config/config.h). Die noch nicht hardwareseitig festgelegten Pins GP22, GP2/3 und GP10/11/12 sind bewusst leicht änderbare Defaults.

Die drei MAX31855 und der ADS1220 teilen MISO und SCK; GP19/MOSI wird nur vom ADS1220 benötigt. Jeder Wandler besitzt eine eigene CS-Leitung. Das LCD verwendet standardmäßig die I²C-Adresse `0x27`. Der integrierte, nichtblockierend initialisierte PCF8574/HD44780-Treiber benötigt keine zusätzliche LCD-Bibliothek.

## Softwarearchitektur

| Modul | Aufgabe |
| --- | --- |
| `main.cpp` | kooperativer Scheduler, Sicherheitspriorität und Modulverdrahtung |
| `config/config.h` | zentrale Hardware- und Regelparameter |
| `sensors/thermocouples.*` | MAX31855-Erfassung, Fehlerqualifizierung und Ausreißerlogik |
| `core/system_state.*` | getrennte Betriebsmodi, Systemstatus und Fehlerbits |
| `actuators/heater.*` | fail-safe SSR-Ausgang und Burst-Window |
| `control/controller.*` | PID mit Anti-Windup sowie Relay-Autotune |
| `control/profile.*` | statisch allokierter Profil-Zustandsautomat |
| `communication/protocol.*` | begrenzter, zeilenorientierter USB-Serial-Parser und Telemetrie |
| `user_interface/display.*` | 1602-I²C-LCD und kompakte Statusseiten |
| `user_interface/encoder.*` | Quadraturauswertung und entprellter Taster |
| `sensors/heatflux/*` | Schmidt-Boelter-Erfassung über den fest angeschlossenen ADS1220 |
| `persistence/settings.*` | verzögertes Speichern von PID-Werten und normalem Sollwert |

Es gibt keine `delay()`-basierte Prozesssteuerung und keine blockierenden Serial-String-Funktionen. Die periodischen Aufgaben werden mit überlaufsicheren `millis()`-Differenzen geplant:

| Aufgabe | Intervall |
| --- | ---: |
| Serial RX, Encoder, SSR, Watchdog | jeder `loop()`-Durchlauf |
| Thermoelemente und Plausibilität | 100 ms |
| PID / Autotune | feste 100 ms |
| optionaler Wärmestrom-ADC | 150 ms |
| Telemetrie | 500 ms |
| LCD-Menü | 100 ms |
| LCD-Wert-/Statusseiten | 500 ms |
| SSR-Burst-Window | 1000 ms |

Der Serial-Parser verarbeitet höchstens 64 Zeichen pro Schleifendurchlauf. Ausgaben laufen über einen statischen 2048-Byte-TX-Ringpuffer und werden nur bis zum aktuell freien USB-CDC-Puffer übertragen; automatische Telemetrie wird bei Rückstau verworfen statt die Regelung anzuhalten. Auch ein dauerhaft sendender oder nicht mehr lesender Host kann dadurch die Regelaufgaben nicht verdrängen. Profile und Regelung benötigen nach dem Start keine USB-Verbindung.

## Thermoelement-Redundanz

Jeder Kanal verwaltet Temperatur, MAX31855-Fehlerbits, Fehler- und Recovery-Zähler, elektrische Gültigkeit, Plausibilität und einen der Zustände `OK`, `FAULT`, `OUTLIER` oder `RECOVERING`.

- Ein Kanal wird nach drei aufeinanderfolgenden elektrischen Fehlern gesperrt.
- Nach fünf aufeinanderfolgenden guten Messungen wird er wieder freigegeben.
- Kurzzeitige Fehler werden angezeigt; der letzte qualifizierte Wert darf maximal zwei Messzyklen überbrücken.
- Bei drei Sensoren wird ein einzelner abweichender Sensor gegenüber einem nah beieinanderliegenden Paar als Ausreißer verworfen.
- Sind nur zwei Sensoren verfügbar und weichen diese um mehr als `TC_MAX_DEVIATION_C` (30 K) voneinander ab, ist keine sichere Zuordnung möglich: beide werden verworfen und der Heizer geht aus.
- Bei einer mehrdeutigen Dreierverteilung wird ausschließlich der Median verwendet.

Regeltemperatur und Systemreaktion:

| plausible Sensoren | Regeltemperatur | Status / Reaktion |
| ---: | --- | --- |
| 3 | Mittelwert aus 3 | `OK` |
| 2 | Mittelwert aus 2 | `DEGRADED` |
| 1 | Wert dieses Sensors | `DEGRADED`, Ausgang auf 25 % begrenzt |
| 0 | ungültig | `FAULT`, SSR sofort AUS |

`TC_MAX_DEVIATION_C`, Trip-/Recovery-Zähler und `MAX_OUTPUT_SINGLE_TC_PERCENT` sind konfigurierbar.

## Betriebsmodi und Systemstatus

Betriebsmodus und Systemzustand sind voneinander unabhängig:

- `OFF`: Heizer aus; einziger Modus nach dem Booten.
- `MANUAL`: feste Ausgangsleistung, weiterhin durch alle Sicherheitsgrenzen überwacht.
- `PID`: Regelung auf den zentralen Sollwert.
- `AUTOTUNE`: kontrollierte Relay-Oszillation zur Parameterbestimmung.
- `PROFILE`: autonomer Rampen-/Halteprogramm-Ablauf.

Der Systemstatus ist `OK`, `DEGRADED` oder `FAULT`. `FAULT` hat immer Vorrang vor dem Modus und setzt die SSR-Anforderung auf 0 %. Ein Profil oder Autotune wird bei einem sicherheitsrelevanten Fehler abgebrochen.

## SSR und Sicherheitskonzept

Der SSR-Ausgang wird als erste Hardwareaktion auf AUS gesetzt. Das 1000-ms-Burst-Window setzt beispielsweise 25 % Leistung in 250 ms EIN und 750 ms AUS um; es gibt keine schnelle PWM.

Der Ausgang bleibt AUS:

- beim Booten, Reset und in `MODE=OFF`;
- solange keine qualifizierte Temperatur verfügbar ist;
- bei `STATUS=FAULT`;
- bei 0 plausiblen Thermoelementen;
- ab `MAX_TEMPERATURE_C` (standardmäßig 800 °C);
- wenn das Profil pausiert oder beendet ist;
- bei Autotune-Abbruch oder internem Watchdog-Reset.

Die Übertemperaturprüfung betrachtet jeden elektrisch fehlerfreien Einzelkanal, auch wenn dieser durch die Plausibilitätslogik als Ausreißer gelten würde. `OVERTEMP` wird verriegelt. Mit `FAULT,CLEAR` kann die Verriegelung erst unterhalb einer 20-K-Hysterese und nur bewusst zurückgesetzt werden; der Modus bleibt danach `OFF`.

Der RP2350-Hardware-Watchdog läuft mit 2 s Timeout. Er wird nur gefüttert, wenn der 100-ms-Sensortask nachweislich kürzlich abgeschlossen wurde. Eine hängende Hauptschleife oder Sensoraufgabe kann ihn daher nicht blind weiterfüttern.

## PID und Autotune

Der PID arbeitet mit fester 100-ms-Abtastzeit und einem Ausgang von 0 bis 100 %. Er verwendet Ableitung auf dem Messwert, bedingte Integration gegen Windup und eine initialisierte Integralkomponente für möglichst stoßarme Moduswechsel. Der zentrale Sollwert wird über den lokal bestätigten Sollwertdialog oder mit `SET,SP=...` geändert; während `PROFILE` gehört er ausschließlich dem Profilgenerator.

Autotune wird vom Rechner gestartet. Ein Relay-Verfahren erzeugt kontrollierte Schwingungen um die Zieltemperatur, misst Periodendauer und Amplitude über fünf Zyklen und berechnet konservative, Tyreus-Luyben-artige Parameter. Autotune stoppt bei Sensorverlust unter zwei Kanäle, Übertemperatur, Timeout, `FAULT` oder `AUTOTUNE,STOP`. Bei Erfolg werden die Werte übernommen, verzögert gespeichert und als `EVENT,AUTOTUNE=SUCCESS,KP=...,KI=...,KD=...` gemeldet. Dieses Event wird erst nach Übernahme der Parameter ausgegeben und bestätigt dem PC damit die erfolgreiche Anwendung; die Detailwerte müssen nicht zusätzlich auf dem 1602 erscheinen. Das thermische System muss Autotune sicher vertragen; Autotune ersetzt keine Leistungs- und Temperaturbegrenzung.

## Lokale Bedienung

- Der Splashscreen zeigt zunächst zwei Sekunden lang nur `CONE HEIZER`, dann
  nacheinander den ADS1220 und die drei MAX31855-
  Thermoelementwandler. Jeder Eintrag bleibt drei Sekunden sichtbar; während
  der ersten Sekunde bleibt der vierstellige Ergebnisplatz frei, danach
  erscheint `PASS` oder `FAIL`. Der Gerätename behält dabei dieselbe
  Zentrierung. `PASS` beim ADS1220 bestätigt den Register-Readback über SPI,
  bei den MAX31855 eine endliche Temperatur ohne gesetzte Fehlerflags. Danach
  erscheint die normale Statusseite mit gemittelter Temperatur und
  Bedienstatus (`LOC`).
- Die Statusseite erscheint direkt nach dem Start. Kurzer Tastendruck wechselt
  zwischen den beiden Statusseiten. Langer Tastendruck öffnet außerhalb von
  `PROFILE` und `AUTOTUNE` das lokale Menü.
- Das Menü enthält in dieser Reihenfolge `SET SP`, `TC VIEW`, `HEATFLUX`
  und `HEIZER KAL.`. Das Display zeigt den markierten Eintrag und den folgenden
  Eintrag; Drehen verschiebt die Auswahl zyklisch durch alle vier Punkte.
  Kurzer Tastendruck öffnet den markierten Punkt. Langer Tastendruck im Menü
  kehrt zur Statusseite zurück.
- `TC VIEW` zeigt die drei plausibilisierten Thermoelementwerte; nicht
  verfügbare Kanäle erscheinen als `--`. `HEATFLUX` startet in `RAW` und zeigt
  die Rohspannung in mV. Drehen wechselt zwischen `RAW` und `W/m2`; der
  gewählte Modus steht in der Überschrift. `W/m2` wird aus dem kalibrierten
  Engineering-Wert berechnet. Ohne gültige Kalibrierung erscheint dort
  `UNCALIBRATED`. Bei deaktiviertem Backend, Sensorfehler oder ungültiger
  Messung wird der entsprechende Sensorstatus angezeigt. Kurzer oder langer
  Tastendruck schließt die Ansichten und kehrt zur Statusseite zurück.
- `HEIZER KAL.` besitzt einen zusätzlichen Startschritt: Zuerst wird die
  Zieltemperatur gewählt, anschließend startet ein kurzer Tastendruck den
  PID-Betrieb. Während des Anfahrens kann die Zieltemperatur weiter mit dem
  Encoder verändert werden. Ein kurzer Tastendruck beendet den Vorgang und
  schaltet den Heizer aus; ein langer Tastendruck bricht ebenfalls ab.
- Während der Kalibrieransicht werden Isttemperatur, Zieltemperatur und –
  sobald kalibriert – die Wärmestromdichte angezeigt. Die Messwerte erscheinen
  weiterhin auch in der seriellen Telemetrie.
- `SET SP` öffnet den Sollwertdialog. Änderungen bleiben zunächst vorläufig;
  kurzer Tastendruck öffnet `OK`/`ABBR.`. Drehen wählt die Aktion, kurzer
  Tastendruck bestätigt sie. `OK` übernimmt den Wert, `ABBR.` verwirft ihn.
  Langer Tastendruck bricht den Dialog ab. Er bleibt außerhalb von `PROFILE`
  und `AUTOTUNE` verfügbar.
- Der bestätigte Sollwert wird verzögert gespeichert. PID- und Autotune-
  Steuerung sowie PID-Parameter bleiben seriell.
- Kurzer Tastendruck auf der normalen Statusseite wechselt die LCD-Statusseite.

Profile werden nicht lokal editiert.

### Display-Mockups (1602)

Die folgenden Beispiele zeigen die vorgesehenen 16×2-Anzeigen. Das Menü wird
alle 100 ms, Wert- und Statusseiten werden alle 500 ms aktualisiert. Die
serielle Telemetrie läuft unabhängig davon ebenfalls alle 500 ms. `PC` bedeutet,
dass `UI,CONNECT` aktiv ist und
der Encoder gesperrt wurde, `LOC` steht für lokale Bedienfreigabe.

Splashscreen, jeweils mittig ausgerichtet. Nach zwei Sekunden Intro folgt der
ADS1220. Der Status erscheint eine Sekunde nach dem Gerätenamen; nach drei
Sekunden folgt der nächste Eintrag:

```text
CONE HEIZER

... zwei Sekunden ...
ADS1220
... eine Sekunde ...
ADS1220 PASS
... drei Sekunden insgesamt ...
MAX31855 1
... eine Sekunde ...
MAX31855 1 PASS
... anschließend MAX31855 2 und 3 ...
```

Bei fehlgeschlagener Prüfung steht anstelle von `PASS` jeweils `FAIL`; vor
dem Ergebnis bleibt dessen vierstelliger Platz leer.

Lokaler Grundzustand (`OFF`):

```text
T:123.4 LOC
SP:500 O:  0 OK
```

PC hat die UI übernommen (`UI,CONNECT`):

```text
T:123.4 PC
SP:500 O: 42 OK
```

Die Firmware springt beim Verbinden automatisch auf diese Hauptseite. Der
Encoder verändert dann weder Sollwert noch LCD-Seite.

Zweite Statusseite, beispielsweise im PID-Betrieb:

```text
MODE:PID
TC:3 ERR:0000
```

Menü nach Longpress (Auswahl `SET SP`; der nächste Eintrag steht darunter):

```text
>SET SP
 TC VIEW
```

Thermoelementansicht:

```text
1: 23.0 2: 22.5
3: 23.0 N:3
```

HEATFLUX in `RAW` bei gültiger Rohmessung (keine Wärmestromkalibrierung nötig):

```text
HEATFLUX RAW
RAW:  1.25 mV
```

HEATFLUX in `W/m2` nach Kalibrierung:

```text
HEATFLUX W/m2
VAL:1250.00 W/m2
```

Sollwertdialog nach Auswahl von `SET SP`:

```text
SET SP:500.0 C
DREHEN KURZ=OK
```

Nach kurzem Druck wird die Auswahl bestätigt:

```text
SET SP:500.0 C
OK<    ABBR.
```

Durch Drehen kann `ABBR.` ausgewählt werden:

```text
SET SP:500.0 C
OK     ABBR.<
```

Profilbetrieb (`PROFILE`):

```text
T:300.0 SP:500
B01 RAMP 5.0K/m
```

Bei einer Haltestufe wird die Restzeit angezeigt:

```text
T:500.0 SP:500
B02 HOLD 12:34
```

Autotune bleibt bewusst kompakt; die Detailwerte und das Ergebnis kommen über
Serial:

```text
T:500.0 PC
SP:500 O: 65 OK
```

Auf der zweiten Seite ist der Modus sichtbar:

```text
MODE:AUTOTUNE
TC:3 ERR:0000
```

Bei reduzierter Sensorqualität wird der Status als `DEG` ausgegeben:

```text
T:499.2 LOC
SP:500 O: 25 DEG
```

Auf der normalen Statusseite haben Sicherheitsfehler Vorrang vor
Betriebsanzeigen. Die Diagnoseansichten `TC VIEW` und `HEATFLUX` bleiben
für die Fehlersuche aufrufbar:

```text
FAULT 00002000
OVERTEMPERATURE
```

## Temperaturprofile

Bis zu 32 Blöcke werden statisch gespeichert. Unterstützt werden:

- `RAMP`: kontinuierliche Sollwertrampe auf ein Ziel in K/min;
- `HOLD`: aktuellen Sollwert für eine Dauer halten;
- `OFF`: Profil sicher beenden und Heizer ausschalten.

Eine Rampe erzeugt nur den Sollwert; sie wartet nicht darauf, dass die reale Temperatur exakt folgt. Ein Profil ist erst nach einem erfolgreichen `PROFILE,END` gültig. Die letzte Stufe muss `OFF` sein. Unvollständige Übertragungen sind nicht startbar. Profile werden derzeit nicht über einen Power-Cycle gespeichert.

Beispiel:

```text
PROFILE,BEGIN,ID=12
PROFILE,STEP,1,RAMP,TARGET=300.0,RATE=5.0
PROFILE,STEP,2,HOLD,TIME=600
PROFILE,STEP,3,RAMP,TARGET=500.0,RATE=2.0
PROFILE,STEP,4,HOLD,TIME=1800
PROFILE,STEP,5,OFF
PROFILE,END
PROFILE,START
```

Der Pico arbeitet dieses Profil anschließend vollständig autonom ab. `PROFILE,PAUSE`, `PROFILE,RESUME`, `PROFILE,STOP`, `PROFILE,GET`, `PROFILE,STATUS` und `PROFILE,CLEAR` stehen ebenfalls zur Verfügung.

## Serielles Protokoll

USB CDC läuft mit 115200 Baud. Jeder Befehl endet mit `\n` und erhält genau eine Antwort `OK,...` oder `ERR,...`. Der RX-Puffer ist auf 192 Zeichen begrenzt; ein Überlauf liefert `ERR,RX_OVERFLOW`. Asynchrone Zustandsmeldungen beginnen mit `EVENT,...`, zyklische Messdaten mit `STAT,...`.

Die lokale Bedienung wird nicht allein durch das Vorhandensein einer USB-Verbindung gesperrt. Der PC muss zuerst `UI,CONNECT` senden; dann meldet die Firmware `OK,UI=CONNECTED`, zeigt auf der normalen LCD-Seite neben der gemittelten Temperatur `PC` an und ignoriert alle Encoder-Aktionen einschließlich der LCD-Seitenumschaltung. Im lokalen Zustand steht dort `LOC`. Mit `UI,DISCONNECT` und `OK,UI=LOCAL` wird die lokale Bedienung wieder freigegeben. Der UI-Besitz wird nicht über einen Power-Cycle gespeichert.

Unterstützte Befehle:

```text
SET,SP=500.0
SET,KP=2.0
SET,KI=0.1
SET,KD=5.0
SET,OUT=25.0

GET,STATUS
GET,PID
GET,HEATFLUX

SET,HF_SENS=...       # Sondenempfindlichkeit in mV/(kW/m²)
SET,HF_FACTOR=...     # alternativ direkt kW/m²/V
SET,HF_OFFSET=...    # Offset in kW/m²

UI,CONNECT
UI,DISCONNECT

MODE,OFF
MODE,PID
MODE,MANUAL

AUTOTUNE,START,TARGET=500.0
AUTOTUNE,STOP

PROFILE,BEGIN,ID=12
PROFILE,STEP,1,RAMP,TARGET=300.0,RATE=5.0
PROFILE,STEP,2,HOLD,TIME=600
PROFILE,STEP,3,OFF
PROFILE,END
PROFILE,START
PROFILE,PAUSE
PROFILE,RESUME
PROFILE,STOP
PROFILE,GET
PROFILE,STATUS
PROFILE,CLEAR

FAULT,CLEAR
```

Beispieldialog:

```text
> SET,SP=500.0
< OK,SP=500.0
> MODE,PID
< OK,MODE=PID
> GET,PID
< OK,KP=2.000000,KI=0.100000,KD=5.000000
> SET,HF_SENS=0.250000
< OK,HF_SENS=0.250000
> GET,HEATFLUX
< OK,HF_FACTOR=4000.000000,HF_OFFSET=0.000000,HF_CAL=VALID
```

Beispieltelemetrie:

```text
STAT,SEQ=18452,T1=499.2,T1S=OK,T2=500.1,T2S=OK,T3=499.8,T3S=OK,CTRL=499.7,SP=500.0,OUT=37.4,MODE=PID,STATUS=OK,ERRORS=0x0,PID=ON
```

Bei einem aktiven Profil kommen `PROFILE`, `PSTEP`, `PTYPE`, `PELAPSED` und `PREMAIN` hinzu. Ein gültiger ADS1220-Sample ergänzt immer `HFV` als differentielle Spannung in Volt. `HF` in kW/m² wird erst bei hinterlegter Kalibrierung ausgegeben.

## Fehlerbits

Die Telemetrie überträgt mehrere gleichzeitige Diagnosen als Hex-Bitmaske in `ERRORS`. Definiert sind offene Thermoelemente, Kurzschlüsse gegen GND/VCC und Ausreißer je Kanal sowie `NO_VALID_TC`, `OVERTEMP`, `PROFILE_INVALID`, `AUTOTUNE_FAILED` und `INTERNAL_ERROR`. Kanalfehler führen zu `DEGRADED`, solange noch mindestens ein plausibler Sensor verfügbar ist; sicherheitsrelevante Fehler führen zu `FAULT`.

## Schmidt-Boelter-Wärmestromsensor

`HeatFluxSensor` verwendet direkt den fest integrierten ADS1220-Treiber. Er startet Einzelmessungen und wertet den dedizierten DRDY-Pin ohne Warteschleifen aus. Nach Reset schreibt er die vier Konfigurationsregister und prüft sie per SPI-Readback. Bei einem Timeout oder fehlgeschlagenen Readback wird der Wert ungültig und nach einer Sekunde ein Initialisierungsversuch wiederholt. Die Heizer-Sicherheitslogik bleibt davon vollständig getrennt.

Der ADS1220-Rohcode wird als vorzeichenbehafteter 24-Bit-Zweierkomplementwert gelesen. Die Umrechnung lautet `U_diff[V] = raw * VREF / (gain * 2^23)`. Bei der Standardreferenz 2,048 V und Gain 32 ist der Vollbereich näherungsweise ±64 mV. `rawValue()` und `HFV` sind diese Rohspannung in V; damit ist die Sondeninbetriebnahme ohne Kalibrierwert möglich.

Für Wärmestromwerte gilt weiterhin `engineering_value = raw_value * calibration_factor + calibration_offset` in kW/m². `HEAT_FLUX_CALIBRATION_FACTOR` ist bewusst `NAN`, ebenso der Platzhalter `SCHMIDT_BOELTER_SENSITIVITY_MV_PER_KW_M2`: Der echte MEDTHERM-Kalibrierwert darf erst nach Ablesen des Kalibrierzertifikats eingetragen werden. Solange der Faktor nicht endlich und positiv ist, liefert `engineeringValue()` kein Ergebnis und die Telemetrie enthält kein `HF`-Feld. Der Wärmestromwert dient ausschließlich Anzeige, serieller Ausgabe und Kalibrierung des Heizsystems; er ist kein Eingang des PID-Regelkreises.

Der Kalibrierwert kann nach dem Booten seriell gesetzt werden. `SET,HF_SENS=...` erwartet die Empfindlichkeit vom Zertifikat in mV/(kW/m²) und speichert den daraus berechneten Faktor persistent. Mit `SET,HF_FACTOR=...` kann der Faktor direkt in kW/m²/V gesetzt werden; `SET,HF_OFFSET=...` setzt den Offset. Änderungen werden – wie PID-Werte – nach fünf Sekunden ohne weitere Änderung im EEPROM gespeichert. `GET,HEATFLUX` liest den aktuell geladenen Wert zurück. Ein Neustart übernimmt den gespeicherten Wert automatisch.

## Persistente Einstellungen

Kp, Ki, Kd und der letzte normale Sollwert werden über die EEPROM-Emulation des Arduino-Pico-Core gespeichert. Eine Prüfsumme schützt gegen ungültige Daten. Flash wird nicht bei jeder Encoder-Rastung beschrieben, sondern erst fünf Sekunden nach der letzten Änderung. Sollwerte aus Profil oder Autotune überschreiben den gespeicherten normalen Sollwert nicht.

## Build, Upload und Testgrenzen

Voraussetzungen sind PlatformIO, die Umgebung `pico2` und der Earle-Philhower-Arduino-Pico-Core. Externe Bibliothek ist nur:

```ini
adafruit/Adafruit MAX31855 library@^1.4.0
```

Build und Upload:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Der Build erzeugt unter `.pio/build/pico2/` unter anderem `firmware.elf`, `firmware.bin` und `firmware.uf2`.

Ohne angeschlossene Hardware lassen sich SPI-Signalqualität, reale MAX31855-Fehlerzustände, SSR-Polarität, LCD-Backpack-Belegung, Encoder-Drehrichtung, PID-Parameter und Autotune-Verhalten nicht abschließend testen. Vor dem Anschluss eines Leistungsteils zuerst den SSR-Ausgang mit LED oder Oszilloskop prüfen, die tatsächliche Active-High-Polarität bestätigen und alle Abschaltpfade mit ungefährlicher Last validieren.
