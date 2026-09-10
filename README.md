# ThermoMerge – autarke Temperaturregelung mit Raspberry Pi Pico 2

ThermoMerge ist eine nichtblockierende Firmware für den Raspberry Pi Pico 2 (RP2350). Sie erfasst drei Typ-K-Thermoelemente über MAX31855, bildet daraus eine plausibilisierte Regeltemperatur und steuert einen elektrischen Heizer über ein SSR. PID-Regelung, Autotune und vollständig auf dem Pico ausgeführte Temperaturprofile funktionieren ohne angeschlossenen Rechner.

Die Firmware startet grundsätzlich in `MODE=OFF`. Ein gespeicherter Sollwert aktiviert den Heizer niemals automatisch.

> **Sicherheit:** Die Softwareabschaltung ist keine normgerechte Sicherheitseinrichtung. Für einen realen Heizer sind ein unabhängiger Sicherheitstemperaturbegrenzer (STB), ein passend ausgelegtes Leistungsschütz, Sicherungen und eine hardwareseitige SSR-Pulldown-Beschaltung erforderlich.

## Hardware und Pinbelegung

| Funktion | Pico-2-GPIO | Hinweise |
| --- | ---: | --- |
| SPI0 RX / MISO | GP16 | DO aller MAX31855 und ADS1220 DOUT/DRDY; 3,3-V-kompatibel |
| MAX31855 T1 CS | GP17 | bestehende Belegung |
| SPI0 SCK | GP18 | CLK aller MAX31855 |
| SPI0 TX / MOSI | GP19 | ADS1220 DIN |
| MAX31855 T2 CS | GP20 | bestehende Belegung |
| MAX31855 T3 CS | GP21 | bestehende Belegung |
| ADS1220 CS | GP6 | Wärmestrom-ADC, aktiv LOW |
| ADS1220 DRDY | GP7 | dedizierter Daten-bereit-Ausgang, aktiv LOW |
| SSR-Ausgang | GP22 | aktiv HIGH; externe Pulldown-Beschaltung vorsehen |
| I²C SDA | GP4 | 1602-LCD mit PCF8574 |
| I²C SCL | GP5 | 1602-LCD mit PCF8574 |
| Encoder A | GP10 | interner Pull-up |
| Encoder B | GP11 | interner Pull-up |
| Encoder-Taster | GP12 | aktiv LOW, interner Pull-up |
| Wärmestrom-ADC | ADS1220 an GP6/7, SPI0 | fest integrierter ADS1220-Treiber |
| Status-LED | `LED_BUILTIN` | OK langsam, DEGRADED mittel, FAULT schnell |

Alle Pins, Zeitraster, Grenzen und Kalibrierwerte stehen zentral in [`src/config/config.h`](src/config/config.h). Die noch nicht hardwareseitig festgelegten Pins GP22, GP4/5 und GP10/11/12 sind bewusst leicht änderbare Defaults.

Die drei MAX31855 und der ADS1220 teilen MISO und SCK; GP19/MOSI wird nur vom ADS1220 benötigt. Jeder Wandler besitzt eine eigene CS-Leitung. Das LCD verwendet standardmäßig die I²C-Adresse `0x27`. Der integrierte, nichtblockierend initialisierte PCF8574/HD44780-Treiber benötigt keine zusätzliche LCD-Bibliothek.

## Textbasierte Anschlusspläne

Die folgenden Pläne zeigen die aktuell in `config/config.h` hinterlegte Belegung. Alle Signalspannungen am Pico müssen 3,3-V-kompatibel sein; die GPIOs des Pico 2 sind nicht 5-V-tolerant. Für alle Logikgeräte müssen Pico und Gerät eine gemeinsame Masse (`GND`) haben.

### Drei MAX31855 für Typ-K-Thermoelemente

```text
Pico 2                         MAX31855 #1       MAX31855 #2       MAX31855 #3
-------------------------------------------------------------------------------
3V3  ------------------------> VIN/VCC          VIN/VCC          VIN/VCC
GND  ------------------------> GND              GND              GND
GP16 (SPI0 RX / MISO) --------> DO              DO               DO
GP18 (SPI0 SCK) --------------> CLK             CLK              CLK
GP17 ------------------------> CS              (nicht verbinden) (nicht verbinden)
GP20 ------------------------> (nicht verbinden) CS             (nicht verbinden)
GP21 ------------------------> (nicht verbinden) (nicht verbinden) CS

Typ-K-Fühler 1 ---------------> T+ / T- an MAX31855 #1
Typ-K-Fühler 2 --------------------------------> T+ / T- an MAX31855 #2
Typ-K-Fühler 3 ------------------------------------------------> T+ / T- an MAX31855 #3
```

`DO` und `CLK` werden als gemeinsamer SPI-Bus parallel zu allen drei Modulen geführt. `CS` bleibt pro Modul getrennt. GP19/MOSI ist auf dem gemeinsamen Bus vorhanden, wird von den MAX31855 aber nicht ausgewertet. Die Polarität des Typ-K-Thermoelements (`T+`/`T-`) ist einzuhalten. Die konkrete Versorgung der Breakout-Boards ist anhand deren Datenblatt zu prüfen; für die Logik ist eine 3,3-V-kompatible Konfiguration erforderlich.

### 1602-LCD mit I²C-PCF8574-Backpack

```text
Pico 2                         LCD-PCF8574
------------------------------------------
3V3  ------------------------> VCC
GND  ------------------------> GND
GP4  (I²C SDA) --------------> SDA
GP5  (I²C SCL) --------------> SCL
```

Die Firmware erwartet standardmäßig die I²C-Adresse `0x27`. Viele LCD-Backpacks sind für 5 V ausgelegt und besitzen 5-V-Pull-ups an `SDA`/`SCL`; solche Module dürfen nicht direkt an den Pico angeschlossen werden. Dann ist entweder ein 3,3-V-taugliches Backpack oder ein geeigneter bidirektionaler I²C-Pegelwandler erforderlich. Die LCD-Beleuchtung und der Kontrast werden vom Backpack versorgt bzw. eingestellt.

### Drehencoder mit Taster

```text
Pico 2                         Drehencoder-Modul
------------------------------------------------
3V3  ------------------------> VCC (falls vorhanden)
GND  ------------------------> GND / COM
GP10 (Encoder A) ------------> A / CLK
GP11 (Encoder B) ------------> B / DT
GP12 (Taster) ---------------> SW
GND  ------------------------> Taster-Gegenkontakt (falls separat)
```

Die Firmware aktiviert interne Pull-ups. Deshalb werden `A`, `B` und der Taster jeweils gegen `GND` geschaltet; externe Pull-up-Widerstände sind normalerweise nicht erforderlich. Die Drehrichtung kann bei abweichender mechanischer Belegung durch Vertauschen von `A` und `B` oder in der Software angepasst werden.

### SSR-Steuereingang und Heizer

```text
Pico 2                         DC-Steuereingang eines SSR-Moduls
-----------------------------------------------------------------
GP22 (SSR-Ausgang, aktiv HIGH) ------------------------------> IN+
GND  --------------------------------------------------------> IN-

Netz-/Lastseite des SSR: ausschließlich nach SSR-Datenblatt,
mit Sicherung, Schutzmaßnahmen, unabhängiger Abschaltung und
geeigneter Verdrahtung durch eine qualifizierte Elektrofachkraft.
```

`GP22` ist nur ein Steuersignal. Bei einem nackten SSR oder einem Modul mit höherem Eingangsstrom ist eine passende Transistor-/Treiberstufe erforderlich; der GPIO darf nicht direkt mit der Netz- oder Heizlast verbunden werden. Die Software setzt den Ausgang beim Start und in allen Sicherheitszuständen auf AUS. Das ersetzt keine Hardware-Sicherheitskette.

### Status-LED und USB

```text
Pico 2                         Funktion
-----------------------------------------
LED_BUILTIN -----------------> interne Status-LED (keine externe Leitung)
USB --------------------------> USB-CDC-Serial für Protokoll/Telemetrie
```

### Wärmestromsensor / Schmidt-Boelter

```text
MEDTHERM Schmidt-Boelter -> ADS1220 (differentiell AIN0 - AIN1)
ADS1220 -> gemeinsamer SPI0-Bus des Pico 2
```

#### ADS1220 <-> Pico 2

| ADS1220 | Pico 2 | Hinweis |
| --- | --- | --- |
| DVDD | 3.3 V | digitale Versorgung, niemals 5 V am Pico-SPI |
| AVDD | 3.3 V | analoge Versorgung |
| DGND | GND | gemeinsame Masse |
| AVSS / AGND | GND | analoge Masse |
| SCLK | GP18 (SPI0 SCK) | gemeinsamer SPI-Takt |
| DIN | GP19 (SPI0 MOSI) | Pico -> ADS1220 |
| DOUT/DRDY | GP16 (SPI0 MISO) | ADS1220 -> Pico, mit MAX31855-DO geteilt |
| CS | GP6 | separat, aktiv LOW |
| DRDY | GP7 | separat, aktiv LOW; ermöglicht nichtblockierendes Warten |
| CLK | unbenutzt | interner ADS1220-Oszillator |

Der ADS1220 arbeitet standardmäßig mit AIN0 minus AIN1, interner 2,048-V-Referenz, PGA-Gain 32, normalem Single-Shot-Modus bei 20 SPS und simultaner 50-/60-Hz-Unterdrückung. Für Gain 64 ist ausschließlich `ADS1220_PGA_GAIN` in `src/config/config.h` anzupassen. Die SPI-Konfiguration ist Mode 1 bei 1 MHz. Der ADC ist keine Sicherheitsvoraussetzung: ein ADC-Fehler stoppt die Wärmestromtelemetrie, beeinflusst aber weder die Thermoelement-Plausibilisierung noch die Heizerabschaltung.

#### Analoge Eingangsbeschaltung der MEDTHERM-Sonde

```text
                         3.3 V
                           |
                         10 MOhm
                           |
MEDTHERM GTW+ -------------+---- 1 kOhm ----+---- AIN0 (ADS1220)
                                             |
                                           100 nF
                                             |
MEDTHERM GTW- -------------+---- 1 kOhm ----+---- AIN1 (ADS1220)
                           |
                         10 MOhm
                           |
                          GND

ADS1220: AVDD/DVDD -> 3.3 V, AGND/DGND -> GND
         SCLK -> GP18, DIN -> GP19, DOUT/DRDY -> GP16,
         CS -> GP6, DRDY -> GP7, CLK unbeschaltet
```

Die MEDTHERM-Sonde wird differentiell gemessen; GTW+ und GTW- werden nicht direkt auf Masse bezogen. Die beiden 10-MOhm-Widerstände sind Bias-Widerstände für die potentialfreie Thermopile. Sie erzeugen einen definierten Common-Mode-Arbeitspunkt von ungefähr AVDD/2. Die 1-kOhm-Serienwiderstände und der 100-nF-Kondensator **direkt zwischen AIN0 und AIN1** bilden den differentiellen RC-Tiefpass. Mit `R_eff = 1 kOhm + 1 kOhm = 2 kOhm` gilt näherungsweise `fc = 1 / (2*pi*2kOhm*100nF) = 796 Hz`. Der Filter begrenzt primär HF-Störungen; weitere Glättung kann digital erfolgen. Der 100-nF-Kondensator kann ein X7R-Keramikkondensator sein. Optional dürfen AIN0 und AIN1 jeweils über 10 nF nach AGND geführt werden, falls eine zusätzliche Common-Mode-Filterung erforderlich ist.

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
| Telemetrie | 250 ms |
| LCD | 500 ms |
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

- Nach dem Start erscheint zunächst ein Auswahlmenü mit `SETPOINT` und
  `HEIZER KAL.`. Drehen wählt, kurzer Tastendruck öffnet den Menüpunkt.
- `HEIZER KAL.` besitzt einen zusätzlichen Startschritt: Zuerst wird die
  Zieltemperatur gewählt, anschließend startet ein kurzer Tastendruck den
  PID-Betrieb. Während des Anfahrens kann die Zieltemperatur weiter mit dem
  Encoder verändert werden. Ein kurzer Tastendruck beendet den Vorgang und
  schaltet den Heizer aus; ein langer Tastendruck bricht ebenfalls ab.
- Während der Kalibrieransicht werden Isttemperatur, Zieltemperatur und –
  sobald kalibriert – die Wärmestromdichte angezeigt. Die Messwerte erscheinen
  weiterhin auch in der seriellen Telemetrie.
- Langer Tastendruck öffnet außerhalb von `PROFILE` und `AUTOTUNE` den
  Sollwertdialog.
- Drehen verändert einen vorläufigen Sollwert; ein kurzer Tastendruck öffnet
  die Bestätigung `OK`/`ABBR.`. Drehen wählt die Aktion, kurzer Tastendruck
  bestätigt sie. Ein langer Tastendruck bricht den Dialog ab.
- Der bestätigte Sollwert wird verzögert gespeichert. PID- und Autotune-
  Steuerung sowie PID-Parameter bleiben seriell.
- Kurzer Tastendruck außerhalb des Dialogs wechselt die LCD-Statusseite.

Profile werden nicht lokal editiert.

### Display-Mockups (1602)

Die folgenden Beispiele zeigen die vorgesehenen 16×2-Anzeigen. Die LCD-Seite
wird alle 500 ms aktualisiert; `PC` bedeutet, dass `UI,CONNECT` aktiv ist und
der Encoder gesperrt wurde, `LOC` steht für lokale Bedienfreigabe.

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

Sollwertdialog nach Longpress:

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

Sicherheitsfehler haben Vorrang vor allen Betriebsanzeigen:

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
