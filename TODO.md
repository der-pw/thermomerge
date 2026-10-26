# TODO

## Temperaturprogramme vereinfachen

- [ ] Die bisherige Rampensteuerung entfernen. Ein Programmschritt besteht nur
  noch aus einem Temperatur-Sollwert und einer Haltezeit.
- [ ] Beim Start eines Schritts den neuen Sollwert unmittelbar übernehmen; die
  Firmware erzeugt keine zeitabhängige Sollwertrampe mehr.
- [ ] `Haltezeit = 0` als unbegrenztes Halten definieren. Der Schritt endet dann
  nur durch einen expliziten Stopp, einen Moduswechsel oder eine
  Sicherheitsabschaltung.
- [ ] Serielles Profilprotokoll, Profilstatus und README an das vereinfachte
  Schrittformat anpassen; alte `RAMP`-/`HOLD`-Befehle entweder eindeutig
  ablehnen oder eine dokumentierte Migration vorsehen.
- [ ] Sicherstellen, dass Pause/Fortsetzen, Profilende, Fehlerfälle und
  `millis()`-Überlauf weiterhin nichtblockierend und fail-safe funktionieren.
- [ ] Tests für endliche Haltezeiten, unbegrenztes Halten, Stopp, Pause/Fortsetzen
  und Sicherheitsabschaltung ergänzen.

### Akzeptanzkriterien

- Ein Schritt benötigt ausschließlich Solltemperatur und Haltezeit.
- Es gibt im Profilgenerator keine Berechnung einer Rampe oder Rampenrate mehr.
- Eine Haltezeit größer null wechselt nach Ablauf zum nächsten Schritt.
- Eine Haltezeit von null läuft ohne Zeitlimit und schaltet den Heizer niemals
  aufgrund eines Zeitablaufs ab.

## Manuelle Zuordnung von Wärmestromdichte zu Temperatur

Heatflux ist ein zusätzlicher Kennwert des Heizsystems, kein Eingang der Regelung. Für die Kalibrierung wird der Heizer manuell auf Temperatur-Sollwerte gebracht; nach Stabilisierung werden Temperatur und Wärmestrom händisch als Wertepaar erfasst. Der spätere Heizbetrieb bleibt immer temperaturgeführt und benötigt keinen laufenden Wärmestromwert.

- [ ] Datenmodell für mehrere Kalibrierpunkte aus Temperatur und
  Wärmestromdichte (WSD, kW/m²) festlegen, einschließlich maximaler Punktzahl,
  Wertebereichen und erforderlicher Mindestpunktzahl. WSD steht ausschließlich
  während der Kalibrierung zur Verfügung.
- [ ] Serielle Befehle zum Hinzufügen, Ändern, Löschen, Auflisten und Speichern
  der Kalibrierpunkte definieren.
- [ ] Kalibrierpunkte validieren und sortieren; Duplikate, nicht endliche Werte
  und nicht monotone beziehungsweise physikalisch unplausible Tabellen
  eindeutig behandeln.
- [ ] Eine für den Pico geeignete Kennlinie berechnen, vorzugsweise
  stückweise-lineare Interpolation ohne dynamische Speicherverwaltung.
- [ ] Verhalten außerhalb des kalibrierten Bereichs festlegen. Extrapolation
  nicht stillschweigend zulassen; standardmäßig auf den gültigen Bereich
  begrenzen oder den Zielwert ablehnen.
- [ ] Tabelle samt Version, Anzahl der Punkte und Prüfsumme in einem persistenten
  Flash-Datenbereich ablegen. Das Speicherlayout muss Flash-Schreibzyklen,
  gebündelte Schreibvorgänge und eine spätere Erweiterung berücksichtigen.
- [ ] Beim Start ungültige, unvollständige oder inkompatible Flash-Daten sicher
  verwerfen und die Kalibrierreferenz als nicht verfügbar kennzeichnen.
- [ ] Eine optionale Zuordnungstabelle als manuelle Referenz führen, die
  Temperatur und Wärmestromdichte (WSD) paarweise abbildet. Sie dient nur dazu,
  WSD-Werte Temperaturen zuzuordnen; die Regelung verwendet weiterhin
  ausschließlich Temperatur und benötigt im Heizbetrieb keinen Live-WSD-Wert.
- [ ] README, Protokolldokumentation und Flash-Speicherlayout aktualisieren.
- [ ] Tests für Interpolation, Grenzwerte, Flash-Prüfsumme und -Migration,
  ungültige Tabellen, fehlende WSD-Messwerte sowie Neustart ergänzen.

### Vor der Implementierung klären

- Wird die Haltezeit eines Programmschritts sofort nach Setzen des Sollwerts
  gezählt oder erst, nachdem die Isttemperatur ein definiertes Toleranzband
  erreicht hat?
- Welcher konkrete persistente Flash-Datenbereich und welches Speicherformat
  sollen verwendet werden? Eine EEPROM-Emulation im Flash ist möglich, aber
  nicht zwingend erforderlich.

### Akzeptanzkriterien

- Manuell erfasste Kalibrierpaare zeigen die Zuordnung von Temperatur zu WSD
  innerhalb des gemessenen Bereichs nachvollziehbar an.
- Temperaturgeführter Heizbetrieb bleibt auch ohne Kalibrierpunkte oder
  verfügbaren WSD-Messwert vollständig möglich.
- Die Kalibriertabelle bleibt nach einem Neustart vollständig und durch eine
  Prüfsumme abgesichert im persistenten Flash erhalten.
- Ohne gültige Tabelle steht nur die Kalibrierreferenz nicht zur Verfügung;
  normaler temperaturgeführter Betrieb bleibt davon unberührt.
- Ein Fehler der Wärmestrommessung oder der Kalibrierdaten umgeht niemals die
  Thermoelement-Plausibilisierung, Übertemperaturabschaltung oder sichere
  Heizerabschaltung.
