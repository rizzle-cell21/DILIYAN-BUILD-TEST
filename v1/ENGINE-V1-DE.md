# DILIYAN STEM MASTER ENGINE — V1 0.1.0 Experimental

Diese Version enthält echte Audioverarbeitung. Sie ist keine nachgewiesen professionelle Mastering-Lösung. Der native V1-Build, Apple-Dateidekodierung, Benutzeroberfläche und Wiedergabe müssen erst durch den neuen macOS-CI-Lauf und auf dem Ziel-Mac bestätigt werden. Der frühere erfolgreiche Fenster-Build beweist diese neue Audio-V1 nicht.

## Installation ohne Entwicklungswerkzeuge

Nach grünem **DILIYAN ENGINE V1 - macOS ARM64**-Workflow das Artifact herunterladen und entpacken. `DILIYAN-ENGINE-V1-arm64.dmg` öffnen und die App nach Programme ziehen. Die andere Build-Test-App kann bestehen bleiben; Name und Bundle-ID der Engine sind eigenständig.

Die App unterstützt Apple Silicon und macOS 13 oder neuer. Sie ist ad hoc signiert, nicht Developer-ID-signiert und nicht notarisiert. Falls macOS beim ersten Start wegen unbekanntem Entwickler blockiert: nach dem Öffnungsversuch **Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen**, anschließend bestätigen. Keine globale Sicherheitsfunktion abschalten. Bei einer anderen Warnung den genauen Text festhalten. Ein verwalteter Mac kann diese Freigabe verhindern.

Auf deinem Mac sind weder Terminal, Xcode, Python noch Homebrew nötig. Python und Compiler werden nur auf dem kostenlosen Standard-GitHub-Runner verwendet. Keine KI-API, Lizenzaktivierung oder Netzwerkverarbeitung in der App. Systemseitige Gatekeeper-Prüfungen sind davon unabhängig.

## Bedienung

1. Vocal-WAV und Beat-WAV ihren ausdrücklich benannten Rollen zuordnen. Referenz als WAV oder FLAC wählen. Dateiname, Sample-Rate, Kanalzahl, Dauer und vollständiger Pfad als Tooltip werden angezeigt. Mehrkanaldateien und doppelte Dateizuordnungen werden abgelehnt.
2. Sicherstellen, dass Vocal und Beat mit demselben Songstart exportiert wurden, und dies im Kontrollkästchen bestätigen. Der erste Vocal-Einsatz muss nicht bei Sekunde null liegen. Die App verschiebt kein Material automatisch.
3. **AUTO MIX & MASTER** drücken und einen lokalen Projekt-Elternordner wählen. Für jeden Lauf entsteht ein neuer Ordner mit UUID. Niemals deine Musik in das öffentliche GitHub-Repository hochladen.
4. Die Hintergrundverarbeitung abwarten. **Abbrechen** beendet den Engine-Prozess; unvollständige Projektordner dürfen anschließend im Finder gelöscht werden. Sie werden nicht als fertige Ergebnisse angeboten.
5. Original und A/B/C vergleichen. Die Dateien laufen auf derselben Zeitbasis; Umschalten blendet die Pegel über 60 ms. Der Lautheitsabgleich verändert nur die Wiedergabe. Hörbare Knackfreiheit muss auf dem tatsächlichen Ausgabegerät überprüft werden.
6. **Monosumme anhören** nutzt eigene, blockweise erzeugte Vergleichsdateien mit exakt `(L+R)/2` auf beiden Kanälen. Es gibt keine automatische Phasenreparatur und keinen Pegelboost für Mono. Auslöschungen dürfen hörbar werden.
7. Die Referenz kann separat, pegelangepasst und ab ihrem Anfang abgespielt werden. Sie wird nicht mit einem anderen Song musikalisch synchronisiert. Abschnittszuordnung und Referenz-Seeking sind noch nicht implementiert. Ein zusätzlicher Peak-Schutz kann die Referenz gegenüber dem gemeinsamen Vergleichsziel weiter absenken.
8. A, B oder C wählen und **als WAV exportieren**. Export: Stereo, 48 kHz, 24 Bit PCM mit TPDF-Dither. Vorhandene Dateien werden nur nach dem Finder-Speicherdialog ersetzt; Quelldateien und interne Projektdateien sind geschützt.

Maximal 30 Minuten pro Eingangsdatei. Unter 0,4 Sekunden, stilles bzw. nicht messbares Material und nicht unterstützte Formate werden mit Fehler beendet. Der kürzere Stem wird bis zum längeren mit Stille ergänzt, nicht der längere abgeschnitten. Die Entscheidung ist im Bericht dokumentiert. Referenzlänge darf abweichen.

## Tatsächlich implementiert

### Import, Speicher und Projektdaten

- Mac: Core Audio/ExtAudioFile dekodiert und resampelt auf 48-kHz-Float-PCM, maximale Apple-SRC-Qualität angefordert. Mono wird explizit auf L/R dupliziert; kein versteckter Mehrkanal-Downmix.
- Blockgröße 4096 Frames. Kein ganzer Song wird für DSP in den RAM geladen. Pro 100-ms-Hopfpunkt werden kleine Messwerte gespeichert, keine Audiosamples.
- Peak-, Sample- und Längenprüfungen; NaN/Inf wird abgelehnt. Verschiedene Eingangs-Sample-Raten sind erlaubt und werden angegeben. Konvertierte Dauer wird auf unerwartete Abweichung geprüft.
- Neuer Projektordner pro Lauf. Quellen werden nicht verändert. `Session.json` enthält Quellpfade, Formate, Eingangs-/Ausgangsmesswerte, DSP-Parameter und Warnungen. Er wird erst nach erfolgreichen Exportprüfungen geschrieben. Die GUI lädt dieses Format für das aktuelle Ergebnis; das erneute Öffnen alter Projekte per GUI fehlt noch.
- Projekte enthalten Original, Balanced, Referenz-PCM, drei Master und vier Mono-Vergleichsdateien. Für fünf Minuten sind grob 1 GB Projektplatz möglich. Freier Platz wird vorab konservativ geprüft; Schreibfehler führen zum Abbruch. Projektdateien bleiben bis zum manuellen Löschen bestehen.

### Analyse

- K-Weighting mit 48-kHz-Koeffizienten, Stereoenergie nach BS.1770-Prinzip, 400-ms-Fenster/100-ms-Hop, absolutes −70-LUFS- und relatives −10-LU-Gate für LUFS-I.
- Maximale kurzfristige Lautheit über 3 Sekunden; für kürzere Dateien als nicht messbar angezeigt. Keine vollständige Lautheitszeitkurve oder LRA.
- Sample Peak und **True-Peak-Schätzung** über achtfache, 32-Taps-pro-Phase Blackman-Sinc-Interpolation einschließlich Randnachlauf. Keine zertifizierte True-Peak-Messung.
- RMS, aktive 100-ms-RMS-Blöcke, Crest-Faktor, einfache positive RMS-Flussmetrik als Transientenindikator.
- Breite Energiebänder: Bass unter ungefähr 180 Hz, Präsenz ungefähr 1,5–5 kHz, Höhen über ungefähr 8 kHz. Dies ist keine hochauflösende Spektral-/Resonanzanalyse.
- Globale L/R-Korrelation, Seitenenergie-Breitenindex und erster Pegel über −60 dBFS. Stereoindikatoren sind keine vollständige Phasenprüfung. Unterschiedliche erste Pegelereignisse beweisen keinen falschen Stem-Start.

### Stem-Mix und Master

- Konservative Pegelheuristik: Median aktiver RMS-Blöcke; Vocal-Ziel zunächst 6 dB unter aktivem Beat-RMS, Änderung maximal ±3 dB. Das ist ein experimenteller Ausgangspunkt, keine aus einem Stereo-Referenzmaster extrahierte Vocal-Balance.
- Bei gleichzeitig erheblicher Präsenzbandenergie in beiden Stems: breiter statischer Beat-EQ mit −0,6 dB bei 2,7 kHz. Globaler Energievergleich erkennt keine zeitabhängige Maskierung. Die Vocals werden zunächst nur im Pegel verändert; keine automatische De-Essing- oder Resonanzbearbeitung.
- A/B/C besitzen materialabhängige Zielparameter aus dem balancierten Mix und der Referenz. EQ-Änderungen maximal ±1,5 dB Bass, ±1,2 dB Präsenz und ±1 dB Höhen. B tendiert nur bei vorhandenem Spielraum zu Körper, C zu Präsenz und größerem Headroom. Keine Breitenänderung und kein künstliches Erzwingen großer Profilunterschiede.
- Optionaler verknüpfter Stereo-Kompressor nur bei hohem Crest-Faktor und deutlich dichterer Referenz: 1,15:1 bzw. 1,25:1, 25-ms-Attack/150-ms-Release, maximal 2 dB Reduktion. C vermeidet diese Kompression.
- Ziel-Lautheit aus der Referenz auf −18 bis −12 LUFS begrenzt; C erhält 0,5 LU mehr Headroom. Gain-Anhebung auf 6 dB begrenzt, zusätzliche Peak-Belastung konservativ begrenzt. Tatsächliche Ergebnisse können leiser sein als das Ziel und als die Referenz.
- Stereo-verknüpfter Sample-Lookahead-Limiter mit 5 ms Lookahead, unmittelbarer Sicherheitsabsenkung und 120-ms-Release. Länge wird kompensiert; erste und letzte Samples bleiben berücksichtigt. Kein als fertig behaupteter oversampelnder kommerzieller True-Peak-Limiter.
- Danach vollständige 8×-Peak-Messung, nötigenfalls globale Sicherheitsabsenkung auf −1,1 dBTP-Schätzung, PCM24-Export und erneute Prüfung des tatsächlichen Exports gegen −1,0 dBTP-Schätzung sowie 0 geclippte Frames. Eine fehlgeschlagene Prüfung gibt kein fertiges Ergebnis frei.
- Alle drei Master werden separat berechnet, nicht nur als Preset-Bezeichnung angezeigt. Bei manchen Eingängen können sie sich sehr ähnlich sein. Hörbare Qualität ist nicht durch Dateiunterschiede bewiesen.

## Noch nicht implementiert / nicht validiert

- Keine gelernte musikalische Bewertung, Sprachverständlichkeitsmessung oder echte Resonanzdiagnose.
- Kein dynamischer EQ, De-Esser, Multiband-Kompressor, Tape, Saturation, Soft Clipper oder frequenzabhängige Stereo-Verbreiterung.
- Keine automatische Synchronisierung, Stem-Separation oder Wiederherstellung schon geclippter Quellen.
- Keine individuellen Vorschlagsentscheidungen, frei bedienbaren Presence/Warmth/Punch-Regler oder Undo-Historie. Quellen und Projektberichte bleiben nichtdestruktiv erhalten; diese Ausbaustufen folgen erst nach Validierung.
- Keine vollständige BS.1770-/EBU-Zertifizierung, kein Nachweis für alle möglichen Intersample-Peaks oder Encoderfolgen. Für Veröffentlichung unabhängig nachmessen und hören.
- Keine bewiesene artefaktfreie Wiedergabe bei allen Gerätewechseln; keine umfangreichen realen Song-/Blindtests. Bei Problemen mit dem Audiogerät App neu starten; WAVs liegen unabhängig von der Wiedergabe im Projektordner.
- Keine Advanced-Stems und kein Kick/Bass-Sidechain. Die Trennung zwischen Decoder, DSP, Renderplan, Projektbericht und GUI ermöglicht spätere Erweiterungen.

## Reproduzierbare technische Tests

`Tests.cpp`: EQ-Nulltest und Filterantworten, K-gewichteter 1-kHz-Sinus, 3-s-Lautheit, Stille, Mono/Gegenphase, PCM24-Ditherfehler, Intersample-Schätzung, Limiter-Grenze und erste/letzte Frames, Stereo-Verknüpfung und adaptive Parameter.

`integration.py`: synthetische Vocal-/Beat-/Referenzdateien durch die tatsächliche Engine; drei unterschiedliche PCM24-Render, längerer Stem vollständig erhalten, Exporte auf Peak-/Clipping-Grenze geprüft, Quellen unverändert, doppelte Zuordnung/Überschreiben/Stille/kaputte Datei abgelehnt; zusätzlicher hochpegeliger Transienten-Stresstest und Monoarithmetik. Nur auf Mac zusätzlich: 44,1-kHz-Mono-Resampling, Ablehnen von Mehrkanal und echte FLAC-Dekodierung. Die mitgelieferte FLAC-Datei ist ein eigens erzeugter 1-kHz-Testton, kein kommerzielles Musikmaterial.

Optionale unabhängige FFmpeg-Gegenmessung ist unter Linux ausführbar. Bisherige Gegenmessungen betreffen ausschließlich den synthetischen Testkorpus; keine allgemeine Konformitäts- oder Klanggarantie.

Der neue macOS-Workflow muss zusätzlich Swift/AppKit kompilieren, beide ARM64-Binaries signieren/prüfen, den echten Engine-Renderpfad testen, die GUI öffnen und aus der GUI den gebündelten Decoder aufrufen. Erst danach erzeugt er ZIP/DMG und Build-Berichte. Ein grün gewordener CI-Lauf ersetzt keine Installation, A/B/C-Hörprüfung, RAM-/Laufzeitmessung und Gatekeeper-Prüfung auf deinem MacBook.

## Kostenloser Browser-Build im bestehenden Repository

Ausgangspunkt: Repository-Commit `5e2fd839081843937cb2c1b7cdbbeb4141787625` (`Update build-macos.yml`).

Die bestehenden `main.swift`, `build.sh`, `README-DE.md` und `.github/workflows/build-macos.yml` werden **nicht geändert**. Die neue Engine wird separat gebaut.

1. Die neun neuen Dateien im Upload-ZIP entpacken. In `rizzle-cell21/DILIYAN-BUILD-TEST` über **Add file → Upload files** hochladen und speichern. Du kannst sie direkt ins Hauptverzeichnis laden; ihre Namen überschneiden sich nicht mit dem alten Test. Alternativ den vollständigen `v1`-Ordner hochladen.
2. **Add file → Create new file**: `.github/workflows/engine-v1.yml` eingeben und den separat gelieferten Workflow-Text einsetzen. Nicht den alten Workflow überschreiben. Auf `main` speichern.
3. **Actions → DILIYAN ENGINE V1 - macOS ARM64 → Run workflow**. Der neue Workflow unterstützt Dateien im Hauptverzeichnis und im `v1`-Ordner. Er startet nur manuell, verwendet `macos-15` als Standard-Runner und ausschließlich lesende Repository-Berechtigung.
4. Nach grünem Abschluss Artifact `DILIYAN-ENGINE-V1-arm64` herunterladen. Es enthält DMG, App-ZIP, DSP-/Integrationstestprotokolle, BUILD-REPORT.txt und SHA256SUMS.txt. Aufbewahrung: drei Tage. Nach Ablauf neu manuell bauen.
5. Bei rotem Build den fehlgeschlagenen Schritt und vollständigen Fehlertext bereitstellen. Es liegt dann kein erfolgreicher nativer V1-Nachweis vor.

Öffentliche Repositories: GitHub dokumentiert kostenlose Standard-Runner. Keine großen/zusätzlich kostenpflichtigen Runner, kostenpflichtigen Pläne oder Apple-Mitgliedschaft werden eingerichtet. Artifact-/Dienstbedingungen bleiben von GitHub abhängig. Workflow-Runner und Action-Versionen werden vom Anbieter aktualisiert; reproduzierbarer Ablauf bedeutet hier nicht bitidentische Binaries. Der Build-Bericht speichert Toolchain und Quellrevision.

## Nächster Test auf dem M1

Zunächst mit einer Kopie eines repräsentativen Songs arbeiten und alle Ergebnisse über Kopfhörer/Monitore lautheitsangepasst vergleichen. Sample-Rate, Länge, richtigen Vocal-/Beat-Start und die drei Exportdateien unabhängig prüfen. Auffällige Stellen mit Zeitposition dokumentieren: Pumpen, verzerrte Transienten, S-Laute, Bassverlust, Mono-Auslöschungen, Umschaltgeräusche. Laufzeit und RAM-Verbrauch über die grafische Aktivitätsanzeige erfassen. Exportierte WAVs zusätzlich mit einem unabhängigen LUFS-/True-Peak-Meter messen.

Erst nach diesen Tests kann über Qualitätsverbesserung und weitere DSP-Module entschieden werden.

## Abhängigkeiten und Quellen

Eigener C++17-DSP-Code und eigene Swift/AppKit-Oberfläche; keine eingebundene JUCE-/GPL-/LGPL-DSP-Bibliothek, kein Drittmodell. Apple-Systemframeworks nach Apple-SDK-Bedingungen; Compiler/SDK ausschließlich auf dem Build-Mac. Eine Lizenz für die spätere öffentliche Weiterverteilung des eigenen Projektcodes wurde noch nicht festgelegt. Das Repository ist öffentlich lesbar.

- GitHub Standard-Runner: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- Apple ExtAudioFile Client-PCM: https://developer.apple.com/documentation/audiotoolbox/kextaudiofileproperty_clientdataformat
- Apple Gatekeeper-Freigabe: https://support.apple.com/102445
- ITU-R BS.1770: https://www.itu.int/rec/R-REC-BS.1770
