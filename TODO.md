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

## Mehrpunkt-Kalibrierung für Wärmestromdichte und Temperatur

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
  verwerfen und die Auswahl eines Tabellen-/WSD-Ziels sperren.
- [ ] Den späteren Betrieb ausschließlich über Temperatur-Sollwerte aus der
  gespeicherten Lookup-Tabelle auslegen. Im reinen Betriebsmodus wird kein
  WSD-Sensorwert vorausgesetzt und kein WSD-Regelkreis betrieben.
- [ ] Für einen gewünschten Tabellen-/WSD-Punkt den daraus berechneten
  Temperatur-Sollwert und den verwendeten Kalibrierbereich in der seriellen
  Telemetrie ausgeben; ein live gemessener WSD-Wert ist dafür nicht erforderlich.
- [ ] README, Protokolldokumentation und Flash-Speicherlayout aktualisieren.
- [ ] Tests für Interpolation, Grenzwerte, Flash-Prüfsumme und -Migration,
  ungültige Tabellen, fehlende WSD-Messwerte sowie Neustart ergänzen.

### Vor der Implementierung klären

- Soll die Kennlinie `Temperatur -> WSD` abbilden und für einen WSD-Zielwert
  invertiert werden, oder werden die Punkte direkt als `WSD -> Temperatur`
  gespeichert?
- Wird die Haltezeit eines Programmschritts sofort nach Setzen des Sollwerts
  gezählt oder erst, nachdem die Isttemperatur ein definiertes Toleranzband
  erreicht hat?
- Welcher konkrete persistente Flash-Datenbereich und welches Speicherformat
  sollen verwendet werden? Eine EEPROM-Emulation im Flash ist möglich, aber
  nicht zwingend erforderlich.

### Akzeptanzkriterien

- Während der Kalibrierung ergeben mindestens zwei gültige Kalibrierpunkte
  reproduzierbar einen Temperatur-Sollwert für eine Ziel-WSD innerhalb des
  Kalibrierbereichs.
- Nach abgeschlossener Kalibrierung kann der reine Betrieb ohne verfügbaren
  WSD-Messwert ausschließlich anhand der Lookup-Tabelle und der
  Temperaturregelung gefahren werden.
- Die Kalibriertabelle bleibt nach einem Neustart vollständig und durch eine
  Prüfsumme abgesichert im persistenten Flash erhalten.
- Ohne gültige Tabelle ist die Auswahl eines Tabellen-/WSD-Ziels nicht möglich;
  ein normaler temperaturgeführter Betrieb bleibt davon getrennt.
- Ein Fehler der Wärmestrommessung oder der Kalibrierdaten umgeht niemals die
  Thermoelement-Plausibilisierung, Übertemperaturabschaltung oder sichere
  Heizerabschaltung.