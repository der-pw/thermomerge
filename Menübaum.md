# Lokaler Menüfluss

Das Menü wird mit dem Encoder bedient: Drehen wählt oder ändert einen Wert,
Kurzdruck öffnet oder bestätigt, Longpress geht im Menü eine Ebene zurück.
Wenn `UI,CONNECT` aktiv ist, ist der Encoder gesperrt, bis `UI,DISCONNECT`
gesendet wird.

Im Dashboard wechselt Drehen zwischen den Statusseiten. Während der
vorläufigen Setpoint-Bearbeitung ändert Drehen stattdessen den angezeigten
Sollwert. Die zweite Seite zeigt `TC1:  TC2:  TC3:` mit je vier Zeichen
reserviertem Wertefeld und zwei Leerzeichen Abstand zwischen den Feldern.
Sie zeigt auch `OUTLIER`-Werte. Bei Sensorfehler oder Übertemperatur steht
`FAIL` im jeweiligen Wertefeld; der Rest der Anzeige aktualisiert weiter.

## Baumpfad

```text
Dashboard
└─ Longpress (außerhalb PROFILE und AUTOTUNE)
   └─ Hauptmenü
      ├─ SETPOINT
      │  ├─ Sollwert wählen (drehen: langsam 1 °C, schnell 10 °C)
      │  │  ├─ Longpress → Sollwert auf 0, Heizer AUS → Hauptmenü
      │  │  └─ Kurzdruck → HEATER ON?
      │  │     ├─ YES → SETPOINT-Regelkreis starten → Dashboard mit RUN
      │  │     ├─ NO → Sollwert auf 0, Heizer AUS → Hauptmenü
      │  │     └─ Longpress → Sollwert auf 0, Heizer AUS → Hauptmenü
      │  └─ Bei fehlender Temperaturfreigabe/FAULT → HEATER BLOCKED
      │     ├─ Kurzdruck → Dashboard
      │     └─ Longpress → Hauptmenü
      ├─ HEATFLUX
      │  ├─ RAW (mV) ↔ W/m2 (drehen)
      │  ├─ Kurzdruck → Dashboard
      │  └─ Longpress → Hauptmenü
      ├─ PID HEATER
      │  ├─ AUTOTUNE
      │  │  ├─ Zieltemperatur einstellen → Kurzdruck → Startabfrage
      │  │  ├─ YES → Autotune läuft
      │  │  │  ├─ Kurz- oder Longpress → stoppen → Ergebnis
      │  │  │  ├─ Ergebnis: Kurzdruck → Dashboard
      │  │  │  └─ Ergebnis: Longpress → PID HEATER-Auswahl
      │  │  ├─ CANCEL → PID HEATER-Auswahl
      │  │  ├─ Longpress in Startabfrage → Zielwertbearbeitung
      │  │  ├─ Longpress in Zielwertbearbeitung → PID HEATER-Auswahl
      │  │  └─ fehlende Freigabe → AUTOTUNE BLOCKED
      │  │     ├─ Kurzdruck → Dashboard
      │  │     └─ Longpress → PID HEATER-Auswahl
      │  ├─ MANUAL
      │  │  ├─ Kp / Ki / Kd auswählen und bearbeiten
      │  │  ├─ SAVE → YES / NO
      │  │  │  ├─ YES → Werte übernehmen und speichern → Hauptmenü
      │  │  │  └─ NO → Werte übernehmen, nicht speichern → Hauptmenü
      │  │  ├─ Longpress in Bearbeitung → Parameterauswahl
      │  │  ├─ Longpress in Parameterauswahl → PID HEATER-Auswahl
      │  │  └─ Longpress in SAVE-Abfrage → SAVE-Auswahl
      │  └─ Longpress in AUTOTUNE/MANUAL-Auswahl → Hauptmenü
      ├─ SSR TEST
      │  ├─ Freigabe vorhanden → drei 500-ms-EIN-Impulse mit AUS-Pausen
      │  │  ├─ automatisch fertig → Hauptmenü
      │  │  ├─ Kurzdruck bricht ab → Dashboard
      │  │  └─ Longpress bricht ab → Hauptmenü
      │  └─ keine Freigabe → SSR TEST BLOCKED
      │     ├─ Kurzdruck → Dashboard
      │     └─ Longpress → Hauptmenü
      └─ Longpress im Hauptmenü → Dashboard

Dashboard während SETPOINT-Regelung:
├─ Kurzdruck → SP-Bearbeitung (`SP:` blinkt)
│  ├─ Drehen → Vorschauwert ändern
│  ├─ Kurzdruck → Vorschau sofort übernehmen, Speicherung vormerken → Dashboard
│  └─ Longpress → Vorschau verwerfen, Regelkreis unverändert fortsetzen
└─ Longpress außerhalb der Bearbeitung → Regelkreis stoppen, Sollwert 0,
   Heizer AUS → gestopptes Dashboard
   └─ weiterer Longpress → Hauptmenü
```

## Kurzbeschreibungen und Ausstieg

| Punkt | Funktion | So verlässt man ihn |
| --- | --- | --- |
| `SETPOINT` | Sollwert wählen und mit `HEATER ON?` ausdrücklich den temperaturgeführten Regelkreis starten. Nur `YES` startet; `NO` verwirft den Sollwert auf 0. SP startet nach jedem Neustart auf 0 und wird nicht gespeichert. Bei laufendem Regelkreis öffnet Kurzdruck im Dashboard die vorläufige SP-Bearbeitung. | `NO` oder Longpress im Dialog setzt SP auf 0 und führt ins Hauptmenü. Im Dashboard übernimmt Kurzdruck den Vorschauwert sofort; Longpress während der Vorschau verwirft sie. Longpress außerhalb der Vorschau stoppt den Kreis und bleibt auf dem Dashboard; ein weiterer Longpress öffnet das Hauptmenü. |
| `HEATFLUX` | Zeigt RAW in mV oder den kalibrierten Wert in W/m². Drehen wechselt die Anzeige. | Kurzdruck führt zum Dashboard, Longpress ins Hauptmenü. |
| `PID HEATER → AUTOTUNE` | Separater Autotune-Zielwert, Startabfrage und kompakte Status-/Ergebnisanzeige. Detailwerte bleiben seriell. | Longpress in der Zielbearbeitung geht zurück zu PID HEATER; in der Startabfrage zurück zur Zielbearbeitung. `CANCEL` führt zu PID HEATER. Im Lauf stoppt jeder Tastendruck; vom Ergebnis führt Kurzdruck zum Dashboard und Longpress zu PID HEATER. |
| `PID HEATER → MANUAL` | Kp, Ki und Kd bearbeiten. `SAVE` fragt YES/NO; beide Antworten übernehmen die Werte, nur YES speichert sie dauerhaft. | Longpress in Bearbeitung geht zur Parameterauswahl, von dort zu PID HEATER und aus der SAVE-Abfrage zurück zur SAVE-Auswahl. YES/NO führt ins Hauptmenü. |
| `SSR TEST` | Bei Modus OFF, gültiger Temperatur und ohne FAULT schaltet er den SSR dreimal 500 ms ein, mit 500-ms-AUS-Pausen. | Kurzdruck bricht ab und führt zum Dashboard; Longpress bricht ab und führt ins Hauptmenü. Automatisches Ende führt ebenfalls ins Hauptmenü. |

Im Hauptmenü bringt Longpress zurück zum Dashboard. Im laufenden SETPOINT-
Dashboard beendet Longpress außerhalb der Vorschau den Regelkreis und setzt
SP auf 0; das gestoppte Dashboard bleibt sichtbar. Ein weiterer Longpress
öffnet das Hauptmenü. `PROFILE` und `AUTOTUNE` sperren das
normale Öffnen des Menüs.
