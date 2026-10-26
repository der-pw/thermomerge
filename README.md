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
| LCD-Menü, SETPOINT-Dialog und PID HEATER | 100 ms |
| LCD-Status- und Messwertansichten | 500 ms |
| SSR-Burst-Window | 1000 ms |

Der Serial-Parser verarbeitet höchstens 64 Zeichen pro Schleifendurchlauf. Ausgaben laufen über einen statischen 2048-Byte-TX-Ringpuffer und werden nur bis zum aktuell freien USB-CDC-Puffer übertragen; automatische Telemetrie wird bei Rückstau verworfen statt die Regelung anzuhalten. Auch ein dauerhaft sendender oder nicht mehr lesender Host kann dadurch die Regelaufgaben nicht verdrängen. Profile und Regelung benötigen nach dem Start keine USB-Verbindung.

## Thermoelement-Redundanz

Jeder Kanal verwaltet den zuletzt gelesenen Temperaturwert, MAX31855-Fehlerbits, Fehler- und Recovery-Zähler, elektrische Gültigkeit und Plausibilität. Ein plausibler Messwert ist nicht automatisch elektrisch fehlerfrei: Die Plausibilitätsprüfung vergleicht die gültigen Sensoren miteinander und kann einen elektrisch einwandfreien, aber stark abweichenden Kanal als `OUTLIER` verwerfen.

### Elektrische Gültigkeit

- Ein MAX31855-Kanal gilt bei einem nicht endlichen Messwert oder gesetzten Fehlerbit (offenes Thermoelement oder Kurzschluss) als fehlerhaft.
- Drei aufeinanderfolgende fehlerhafte Messungen sperren den Kanal (`FAULT`). Ein Fehler wird sofort in den Fehlerstatus aufgenommen; vor der Sperrung bleibt der Kanal im Zustand `RECOVERING`.
- Nach einem Fehler sind fünf aufeinanderfolgende gute Messungen nötig, bevor der Kanal wieder elektrisch gültig ist. Ein weiterer Fehler setzt den Recovery-Zähler zurück.
- Elektrisch ungültige Kanäle gehen nicht in die Regeltemperatur ein. Ihr letzter Rohwert kann weiterhin in der seriellen Telemetrie erscheinen. Die zweite Dashboard-Seite zeigt dagegen die aktuellen Kanalwerte unabhängig von elektrischer Freigabe und Plausibilität; bei einem MAX31855-Fehler, fehlendem Messwert oder Übertemperatur erscheint `FAIL` im jeweiligen Vier-Zeichen-Feld.

### Temperaturvergleich und Ausreißer

Die Differenzschwelle `TC_MAX_DEVIATION_C` beträgt derzeit **60 °C**. Für die Entscheidung werden nur elektrisch gültige Kanäle betrachtet:

- **Ein gültiger Sensor:** Sein Wert wird als Regeltemperatur verwendet. Der Systemstatus ist `DEGRADED`; die Heizleistung ist auf `MAX_OUTPUT_SINGLE_TC_PERCENT` begrenzt.
- **Zwei gültige Sensoren:** Bei höchstens 60 °C Differenz werden beide verwendet und gemittelt. Bei mehr als 60 °C Differenz lässt sich nicht entscheiden, welcher Wert falsch ist: beide werden verworfen und der Heizer wird abgeschaltet (`FAULT`).
- **Drei gültige Sensoren:** Liegen alle drei innerhalb einer Gesamtspanne von höchstens 60 °C, werden alle verwendet und gemittelt. Liegt ein Sensor mehr als 60 °C vom benachbarten Messwert entfernt, während die beiden anderen höchstens 60 °C auseinanderliegen, wird der abweichende Sensor als `OUTLIER` verworfen; die beiden übrigen werden gemittelt.
- **Mehrdeutige Dreierverteilung:** Wenn keine eindeutige nahe Zweiergruppe vorliegt, werden alle drei verworfen. Wenn beide benachbarten Differenzen höchstens 60 °C betragen, aber die Gesamtspanne über 60 °C liegt, wird nur der Median verwendet; die beiden äußeren Werte gelten als Ausreißer.
- **Keine plausible Messung:** Die Regeltemperatur ist ungültig, der Status wird `FAULT` und der SSR wird sofort ausgeschaltet.

Beispiel: Bei `22 °C`, `25 °C` und `400 °C` bilden die ersten beiden Werte ein nahes Paar. Der dritte Kanal wird als `OUTLIER` von Regeltemperatur und Mittelwert ausgeschlossen, bleibt aber auf der zweiten Dashboard-Seite sichtbar. Die Konsole meldet beispielsweise `T3=400.0` und `T3S=OUTLIER`; die zweite Seite zeigt weiterhin den aktuellen Wert `400`. Sobald sich die Werte wieder innerhalb der Plausibilitätsregeln befinden, wird der Kanal automatisch erneut für die Regelung berücksichtigt.

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
- `SETPOINT`: Temperaturregelung auf den zentralen Sollwert (der Regler verwendet intern die PID-Parameter Kp, Ki und Kd).
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

Der PID arbeitet mit fester 100-ms-Abtastzeit und einem Ausgang von 0 bis 100 %. Er verwendet Ableitung auf dem Messwert, bedingte Integration gegen Windup und eine initialisierte Integralkomponente für möglichst stoßarme Moduswechsel. Der zentrale Sollwert wird über `SETPOINT` oder seriell mit `SET,SP=...` geändert; während `PROFILE` gehört er ausschließlich dem Profilgenerator.

Der lokale Menüpunkt `PID HEATER` bietet `AUTOTUNE` und `MANUAL`. `MANUAL` lädt die aktuell aktiven PID-Werte. Drehen wählt Kp, Ki, Kd oder SAVE; kurzer Druck auf Kp/Ki/Kd öffnet bzw. beendet die Bearbeitung. Langsame Rastungen ändern Kp/Kd um 0,1 und Ki um 0,01; bei Rastungsabständen bis 120 ms gelten Schritte von 1,0 bzw. 0,1. Die Werte sind auf 0 bis 1000 begrenzt. Kurzer Druck auf SAVE öffnet `SAVE PID VALUES` mit `YES`/`NO`. Beide Optionen übernehmen die Werte in den laufenden PID-Regler und kehren ins Hauptmenü zurück; YES veranlasst die verzögerte EEPROM-Speicherung, NO behält die Werte nur bis zum Neustart. Longpress in Kp/Ki/Kd-Bearbeitung geht zur Parameterauswahl zurück; Longpress dort zur `PID HEATER`-Auswahl. Longpress in der Speicherabfrage geht zurück zu `SAVE`, ohne die Änderungen zu übernehmen. Longpress im Hauptmenü führt zurück zum Dashboard. Ohne gespeicherte Werte gelten Kp=2,0, Ki=0,10 und Kd=5,0.

`AUTOTUNE` kann lokal oder seriell gestartet werden. Der lokale Autotune-Sollwert wird beim ersten Aufruf aus `SETPOINT` vorbelegt (bei SP=0 mit dem Standardwert 25 °C), danach bis zum Neustart separat gehalten und nicht persistent gespeichert. Das Relay-Verfahren erzeugt kontrollierte Schwingungen um diesen Sollwert, misst Periodendauer und Amplitude über fünf Zyklen und berechnet konservative, Tyreus-Luyben-artige Parameter. Mindestens zwei gültige Thermoelemente sind erforderlich. Sensorverlust unter zwei Kanälen, Übertemperatur, Timeout, `FAULT` oder `AUTOTUNE,STOP` brechen ab. Bei Erfolg werden die neuen Werte unmittelbar übernommen und zur verzögerten Speicherung vorgemerkt; anschließend meldet die Firmware `EVENT,AUTOTUNE=SUCCESS,KP=...,KI=...,KD=...`. Das Event bestätigt die Übernahme, die Detailwerte bleiben seriell. Der lokale Bildschirm zeigt Status und Ergebnis, aber keine Zyklen oder PID-Werte. Autotune ersetzt keine Leistungs- und Temperaturbegrenzung.
## Lokale Bedienung

Der vollständige lokale Menübaum mit Kurzbeschreibungen und Ausstiegen steht
in [Menübaum.md](Menübaum.md).

- Der Splashscreen zeigt zunächst zwei Sekunden lang nur `CONE HEATER`, dann
  nacheinander den ADS1220 und die drei MAX31855-
  Thermoelementwandler. Jeder Geräteeintrag bleibt 1,5 Sekunden sichtbar:
  Nach 0,5 Sekunden erscheint `PASS` oder `FAIL` und bleibt eine Sekunde stehen.
  Der Gerätename behält dabei dieselbe
  Zentrierung. `PASS` beim ADS1220 bestätigt den Register-Readback über SPI,
  bei den MAX31855 eine endliche Temperatur ohne gesetzte Fehlerflags. Danach
  erscheint die normale Statusseite mit gemittelter Temperatur und
  Bedienstatus (`LOC`).
- Menü, SETPOINT-Dialog und PID-HEATER-Einstellungen werden alle 100 ms aktualisiert; Status- und
  Messwertansichten werden alle 500 ms aktualisiert. Das betrifft die
  LCD-Anzeige, nicht die davon unabhängigen Sensor-Polling-Intervalle.
- Die Statusseite erscheint direkt nach dem Start. Drehen wechselt zwischen
  den beiden Statusseiten, im Leerlauf wie bei laufendem `SETPOINT`. Longpress
  öffnet außerhalb von `PROFILE` und `AUTOTUNE` das lokale Menü.
- Das Menü enthält `SETPOINT`, `HEATFLUX`, `PID HEATER`, `SSR TEST` und `FAULT CLEAR`. Drehen wählt zyklisch; kurzer Druck öffnet den Eintrag. Langer Druck kehrt aus dem Hauptmenü zum Dashboard zurück.
- `FAULT CLEAR` setzt eine verriegelte Störung nach Beseitigung ihrer Ursache zurück und lässt den Betriebsmodus auf `OFF`. Eine verriegelte Übertemperatur kann erst unter `MAX_TEMPERATURE_C - 20 °C` (standardmäßig unter 780 °C) zurückgesetzt werden. Bei fehlenden gültigen Thermoelementen erscheint `CHECK TC INPUTS`; bei noch zu hoher Temperatur `COOL BELOW 780C`. Kurzer oder langer Druck kehrt vom Ergebnis zum Hauptmenü zurück. Der Punkt startet den Heizer nicht.
- `PID HEATER` zeigt `AUTOTUNE` und `MANUAL`. Longpress in dieser Auswahl führt ins Hauptmenü zurück. In `AUTOTUNE` wird der Sollwert beim ersten Aufruf aus `SETPOINT` übernommen und danach bis zum Neustart unabhängig gehalten. Langsames Drehen ändert ihn in 1-°C-Schritten, Rastungen innerhalb von 120 ms in 10-°C-Schritten. Kurzer Druck öffnet die Startabfrage `YES`/`CANCEL`; Drehen wählt, kurzer Druck bestätigt. Longpress im Zielwert führt zurück zu `PID HEATER`, Longpress in der Abfrage zurück zum Zielwert; `CANCEL` führt zurück zu `PID HEATER`. Der Start erfordert mindestens zwei gültige Thermoelemente und keinen `FAULT`; andernfalls erscheint `2+ TC REQUIRED`. Im Lauf zeigt das Display `AUTOTUNE`/`PRESS=STOP`; kurzer oder langer Tastendruck stoppt. Erfolg, Fehler oder Stopp werden angezeigt; kurzer Druck führt zum Dashboard, Longpress zurück zu `PID HEATER`. Zyklen und PID-Werte bleiben seriell.
- In `MANUAL` werden die aktuell aktiven Werte geladen. Drehen wählt zyklisch `Kp`, `Ki`, `Kd` und `SAVE`. Kurzer Druck auf Kp/Ki/Kd öffnet bzw. beendet die Bearbeitung. Langsame Rastungen ändern Kp/Kd um 0,1 und Ki um 0,01; bei Abständen bis 120 ms sind es 1,0 bzw. 0,1. Die Werte sind auf 0 bis 1000 begrenzt. Kurzer Druck auf `SAVE` öffnet `YES`/`NO`; Drehen wählt, kurzer Druck bestätigt. Beide Antworten übernehmen die Werte in den laufenden PID-Regler und kehren ins Hauptmenü zurück; `YES` speichert sie verzögert persistent, `NO` übernimmt sie nur bis zum Neustart. Longpress in Kp/Ki/Kd-Bearbeitung geht zur Parameterauswahl zurück; Longpress dort zur `PID HEATER`-Auswahl. Longpress in der Speicherabfrage geht zurück zu `SAVE`, ohne Änderungen zu übernehmen. Longpress im Hauptmenü kehrt zum Dashboard zurück.
- `SSR TEST` ist nur bei Betriebsmodus `OFF`, mindestens einem gültigen
  Thermoelement und ohne `FAULT` verfügbar. Der Test schaltet den SSR dreimal
  je 500 ms ein, mit jeweils 500 ms AUS zwischen den Pulsen, und endet AUS.
  Während des Tests zeigt das LCD `SSR TEST` und `3x500ms ON/OFF`; Kurzdruck
  bricht den Test ab und kehrt zum Dashboard zurück, Longpress bricht ab und
  kehrt ins Hauptmenü zurück. Nach automatischem Ende geht es ebenfalls ins
  Hauptmenü. Bei fehlender Freigabe erscheint `OFF + TC OK`; Kurzdruck kehrt
  zum Dashboard zurück, Longpress ins Hauptmenü. Der Test
  schaltet eine angeschlossene Last tatsächlich ein.
- Die zweite Statusseite zeigt stets die aktuellen Messwerte aller drei Kanäle
  in der Form `TC1:  TC2:  TC3:` mit den Werten darunter. Jedes Wertefeld
  belegt vier Zeichen, mit zwei Leerzeichen Abstand zum nächsten Feld. Sie zeigt auch Werte
  von `OUTLIER`-Kanälen, selbst wenn diese nicht in die Regeltemperatur eingehen.
  Bei MAX31855-Fehler, fehlendem Messwert oder Übertemperatur erscheint `FAIL`
  nur im Feld des betroffenen Kanals. Auch bei globalem Systemstatus `FAULT`
  bleibt die Anzeige aktiv: Die Hauptseite zeigt `FLT`, die zweite Seite zeigt
  weiterhin die drei Kanäle. Einfaches
  Drehen wechselt zurück zur ersten Statusseite. `HEATFLUX` startet in `RAW` und zeigt
  die Rohspannung in mV. Drehen wechselt zwischen `RAW` und `W/m2`; der
  gewählte Modus steht in der Überschrift. `W/m2` wird aus dem kalibrierten
  Engineering-Wert berechnet. Ohne gültige Kalibrierung erscheint dort
  `UNCALIBRATED`. Bei deaktiviertem Backend, Sensorfehler oder ungültiger
  Messung wird der entsprechende Sensorstatus angezeigt. Kurzdruck schließt
  die Ansicht und kehrt zum Dashboard zurück; Longpress schließt sie und kehrt
  ins Hauptmenü zurück.
- `SETPOINT` öffnet zuerst die Sollwertwahl. Langsames Drehen ändert den Wert in 1-°C-Schritten, Rastungen innerhalb von 120 ms in 10-°C-Schritten. Kurzer Druck führt zur Abfrage `HEATER ON?` mit `YES`/`NO`. `YES` startet bei gültiger Temperatur und ohne `FAULT` den Regelkreis und zeigt im Dashboard `RUN`. `NO` setzt den Sollwert auf 0, schaltet den Heizer aus und kehrt ins Hauptmenü zurück. Langer Druck während Auswahl oder Abfrage bricht ab, setzt den Sollwert auf 0 und kehrt ins Hauptmenü zurück.
- Bei laufendem `SETPOINT` öffnet Kurzdruck im Dashboard die SP-Bearbeitung. `SP:` blinkt abwechselnd mit `__:` an derselben Position; Drehen ändert den angezeigten Wert vorläufig mit denselben langsamen 1-°C- und schnellen 10-°C-Schritten. Der nächste Kurzdruck übernimmt den Wert sofort in den Regler. Longpress in der Vorschau verwirft den Entwurf und setzt den bisherigen Regelkreis fort. Longpress im Dashboard außerhalb der Vorschau stoppt den Regelkreis, setzt SP auf 0 und bleibt auf dem Dashboard; ein weiterer Longpress öffnet das Hauptmenü.
- Der normale Sollwert startet nach jedem Neustart auf 0 und wird nicht persistent gespeichert. PID-Parameter können lokal unter `PID HEATER` oder seriell geändert werden; Autotune-Details bleiben seriell.
- Während der vorläufigen SP-Bearbeitung wechselt Drehen nicht die Statusseite;
  der Kurzdruck übernimmt den angezeigten Sollwert.

Profile werden nicht lokal editiert.

### Display-Mockups (1602)

Die folgenden Beispiele zeigen die vorgesehenen 16×2-Anzeigen. Menü und
SETPOINT-Dialog werden alle 100 ms, Status- und Messwertansichten alle 500 ms
aktualisiert. Die serielle Telemetrie läuft unabhängig davon ebenfalls alle
500 ms. `PC` bedeutet,
dass `UI,CONNECT` aktiv ist und
der Encoder gesperrt wurde, `LOC` steht für lokale Bedienfreigabe.

Splashscreen, mittig ausgerichtet. Nach zwei Sekunden Intro folgen ADS1220
und die drei MAX31855. Jeder Eintrag bleibt 1,5 Sekunden: Nach 0,5 Sekunden
erscheint `PASS`/`FAIL` und bleibt eine Sekunde stehen.

```text
CONE HEATER

... zwei Sekunden ...
ADS1220
... 0,5 Sekunden bis zum Ergebnis ...
ADS1220 PASS
... PASS / FAIL bleibt eine Sekunde; nach 1,5 Sekunden folgt MAX31855 1 ...
MAX31855 1
... 0,5 Sekunden bis zum Ergebnis ...
MAX31855 1 PASS
... anschließend MAX31855 2 und 3 ...
```

Bei fehlgeschlagener Prüfung steht anstelle von `PASS` jeweils `FAIL`; vor
dem Ergebnis bleibt dessen vierstelliger Platz leer.

Lokaler Grundzustand (`OFF`):

```text
T:123.4 LOC
SP:500 P:  0 OK
```

PC hat die UI übernommen (`UI,CONNECT`):

```text
T:123.4 PC
SP:500 P: 42 OK
```

Die Firmware springt beim Verbinden automatisch auf diese Hauptseite. Der
Encoder verändert dann weder Sollwert noch LCD-Seite.

Zweite Statusseite mit den aktuellen Messwerten:

```text
TC1:  TC2:  TC3:
 123   125   400
```

Bei einem Übertemperatur- oder Sensorfehler wird nur der betroffene Kanal als
`FAIL` markiert; die anderen Messwerte bleiben sichtbar:

```text
TC1:  TC2:  TC3:
 123  FAIL   400
```

Menü nach Longpress (Auswahl `SETPOINT`; der nächste Eintrag steht darunter):

```text
>SETPOINT
 HEATFLUX
```

Menüeintrag `SSR TEST` und laufende Impulsfolge:

```text
>SSR TEST
 SETPOINT
```

```text
SSR TEST
3x500ms ON/OFF
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

SETPOINT auswählen; kurzer Druck wechselt zur Startfrage:

```text
SETPOINT:500.0 C
TURN PRESS=NEXT
```

Heizkreis ausdrücklich starten:

```text
HEATER ON?
YES<    NO
```

Dashboard während des laufenden Regelkreises:

```text
T:123.4 LOC RUN
SP:500 P: 42 OK
```

Vorläufige SP-Änderung nach Kurzdruck bei laufendem SETPOINT-Regelkreis
(`SP:` blinkt; der gezeigte Wert ist noch nicht übernommen):

```text
T:123.4 LOC RUN
SP:510 P: 42 OK
```

Menüauswahl `PID HEATER`:

```text
>PID HEATER
 SSR TEST
```

Auswahl zwischen Autotune und manueller Einstellung:

```text
>AUTOTUNE
 MANUAL
```

MANUAL-Auswahl. Drehen wechselt zwischen Kp, Ki, Kd und SAVE; bei Kp/Ki/Kd zeigt das Display den gewählten Parameter samt Wert:

```text
>Kp:  2.000
TURN PRESS=EDIT
```

SAVE ist der vierte Menüpunkt und öffnet mit kurzem Druck die Speicherfrage:

```text
>SAVE
PRESS=SELECT
```

```text
SAVE PID VALUES
YES<    NO
```
AUTOTUNE: Zielwert bearbeiten und Start bestätigen:

```text
AT SP:500.0 C
TURN PRESS=OK
```

```text
START AT:500.0C
YES<    CANCEL
```

Autotune läuft; kurzer oder langer Tastendruck stoppt es:

```text
AUTOTUNE
PRESS=STOP
```

Bei fehlender Startfreigabe:

```text
AUTOTUNE
2+ TC REQUIRED
```

Ergebnisanzeige; die Detailwerte bleiben seriell:

```text
AT SUCCESS
DETAILS SERIAL
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

Autotune bleibt bewusst kompakt. Das LCD zeigt nur den Status; Zyklusdetails
und berechnete PID-Werte kommen über Serial:

```text
T:500.0 PC
SP:500 P: 65 OK
```

Die zweite Dashboard-Seite zeigt die drei aktuellen Thermoelementwerte:

```text
TC1:  TC2:  TC3:
 123   125   400
```

Bei reduzierter Sensorqualität wird der Status als `DEG` ausgegeben:

```text
T:499.2 LOC
SP:500 P: 25 DEG
```

Bei globalem Systemstatus `FAULT` läuft die Dashboard-Aktualisierung weiter.
Die Hauptseite zeigt `FLT`, die zweite Seite markiert fehlerhafte Kanäle einzeln;
Fehlerdetails bleiben seriell verfügbar. Bei Übertemperatur sieht die Anzeige
zum Beispiel so aus:

```text
T:800.1 LOC
SP:500 P:  0 FLT
```

Auf der zweiten Seite wird der betroffene Kanal markiert:

```text
TC1:  TC2:  TC3:
 123  FAIL   400
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

USB CDC läuft mit 115200 Baud. Jeder Befehl endet mit `
` und erhält genau eine Antwort `OK,...` oder `ERR,...`. Der RX-Puffer ist auf 192 Zeichen begrenzt; ein Überlauf liefert `ERR,RX_OVERFLOW`. Asynchrone Zustandsmeldungen beginnen mit `EVENT,...`, zyklische Messdaten mit `STAT,...`.

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
MODE,SETPOINT
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
> MODE,SETPOINT
< OK,MODE=SETPOINT
> GET,PID
< OK,KP=2.000000,KI=0.100000,KD=5.000000
> SET,HF_SENS=0.250000
< OK,HF_SENS=0.250000
> GET,HEATFLUX
< OK,HF_FACTOR=4000.000000,HF_OFFSET=0.000000,HF_CAL=VALID
```

Beispieltelemetrie:

```text
STAT,SEQ=18452,T1=499.2,T1S=OK,T2=500.1,T2S=OK,T3=499.8,T3S=OK,CTRL=499.7,SP=500.0,OUT=37.4,MODE=SETPOINT,STATUS=OK,ERRORS=0x0,PID=ON
```

Bei einem aktiven Profil kommen `PROFILE`, `PSTEP`, `PTYPE`, `PELAPSED` und `PREMAIN` hinzu. Ein gültiger ADS1220-Sample ergänzt immer `HFV` als differentielle Spannung in Volt. `HF` in kW/m² wird erst bei hinterlegter Kalibrierung ausgegeben.

## Fehlerbits

Die Telemetrie überträgt mehrere gleichzeitige Diagnosen als Hex-Bitmaske in `ERRORS`. Definiert sind offene Thermoelemente, Kurzschlüsse gegen GND/VCC und Ausreißer je Kanal sowie `NO_VALID_TC`, `OVERTEMP`, `PROFILE_INVALID`, `AUTOTUNE_FAILED` und `INTERNAL_ERROR`. Kanalfehler führen zu `DEGRADED`, solange noch mindestens ein plausibler Sensor verfügbar ist; sicherheitsrelevante Fehler führen zu `FAULT`.

## Schmidt-Boelter-Wärmestromsensor

`HeatFluxSensor` verwendet direkt den fest integrierten ADS1220-Treiber. Er startet Einzelmessungen und wertet den dedizierten DRDY-Pin ohne Warteschleifen aus. Nach Reset schreibt er die vier Konfigurationsregister und prüft sie per SPI-Readback. Bei einem Timeout oder fehlgeschlagenen Readback wird der Wert ungültig und nach einer Sekunde ein Initialisierungsversuch wiederholt. Die Heizer-Sicherheitslogik bleibt davon vollständig getrennt.

Der ADS1220-Rohcode wird als vorzeichenbehafteter 24-Bit-Zweierkomplementwert gelesen. Die Umrechnung lautet `U_diff[V] = raw * VREF / (gain * 2^23)`. Bei der Standardreferenz 2,048 V und Gain 32 ist der Vollbereich näherungsweise ±64 mV. `rawValue()` und `HFV` sind diese Rohspannung in V; damit ist die Sondeninbetriebnahme ohne Kalibrierwert möglich.

Für Wärmestromwerte gilt weiterhin `engineering_value = raw_value * calibration_factor + calibration_offset` in kW/m². `HEAT_FLUX_CALIBRATION_FACTOR` ist bewusst `NAN`, ebenso der Platzhalter `SCHMIDT_BOELTER_SENSITIVITY_MV_PER_KW_M2`: Der echte MEDTHERM-Kalibrierwert darf erst nach Ablesen des Kalibrierzertifikats eingetragen werden. Solange der Faktor nicht endlich und positiv ist, liefert `engineeringValue()` kein Ergebnis und die Telemetrie enthält kein `HF`-Feld. Der Wärmestromwert ist ein zusätzlicher Kennwert des Heizsystems. Die Regelung verwendet immer die Temperatur der Thermoelemente; Heatflux wird weder als PID-Eingang noch für eine automatische Leistungsregelung verwendet. Nach Festlegung der Heatflux-Messparameter wird der Wärmestrom ausschließlich manuell der jeweiligen Temperatur zugeordnet und zur Heizsystem-Kalibrierung dokumentiert. Der Heizer wird weiterhin ausschließlich temperaturgeführt betrieben.

Der Kalibrierwert kann nach dem Booten seriell gesetzt werden. `SET,HF_SENS=...` erwartet die Empfindlichkeit vom Zertifikat in mV/(kW/m²) und speichert den daraus berechneten Faktor persistent. Mit `SET,HF_FACTOR=...` kann der Faktor direkt in kW/m²/V gesetzt werden; `SET,HF_OFFSET=...` setzt den Offset. Änderungen werden – wie PID-Werte – nach fünf Sekunden ohne weitere Änderung im EEPROM gespeichert. `GET,HEATFLUX` liest den aktuell geladenen Wert zurück. Ein Neustart übernimmt den gespeicherten Wert automatisch.

## Persistente Einstellungen

Kp, Ki, Kd und Wärmestromkalibrierung werden über die EEPROM-Emulation des Arduino-Pico-Core gespeichert. Der normale Sollwert ist bewusst nicht persistent und startet nach jedem Neustart auf 0. Eine Prüfsumme schützt gegen ungültige Daten. Eine nicht gesetzte Wärmestromkalibrierung (`NAN`) macht die übrigen gespeicherten Einstellungen nicht ungültig. Flash wird nicht bei jeder Encoder-Rastung beschrieben, sondern erst fünf Sekunden nach der letzten Änderung. Sollwerte aus Profil oder Autotune überschreiben keinen gespeicherten normalen Sollwert.

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
