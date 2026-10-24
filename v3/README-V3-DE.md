# DILIYAN STEM MASTER ENGINE V3 · Experimental

Eigenständige ARM64-App, macOS 13 oder neuer, lokal und offline. V1 und V2 bleiben unverändert; V3 nutzt ihre unveränderten Decoder-, Meter-, WAV- und Player-Bausteine. Keine Cloud-KI, Abonnements oder zusätzlichen Installationen auf dem Mac. Kostenlose GitHub-Actions-Runner für das öffentliche Repository; keine kostenpflichtigen Runner aktiviert.

## Installation und erster Hörtest

DMG öffnen, **DILIYAN STEM MASTER ENGINE V3.app** nach **Programme** ziehen. V3 hat einen eigenen Namen und eine eigene Bundle-ID und ersetzt V1/V2 nicht. Die private Testversion ist ad hoc signiert, aber weder mit einer Apple-Developer-ID signiert noch notarisiert. Falls macOS den Start blockiert: Start versuchen, dann **Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen**, erneut bestätigen. Keine Terminalbefehle nötig. Auf verwalteten Macs können lokale Sicherheitsrichtlinien dies verhindern.

Vocal-WAV, Beat-WAV und Referenz-WAV/FLAC auswählen. Gemeinsamen Exportstart der beiden Stems bestätigen: Die App kann einen falschen Startpunkt nicht zuverlässig automatisch reparieren. Kürzere Stems werden bis zur längeren Datei mit Stille ergänzt. Referenz und eigener Song müssen nicht gleich lang sein. Maximal 30 Minuten pro Datei; intern 48 kHz, Mono wird auf beide Kanäle abgebildet, keine unbemerkten Mehrkanal-Downmixes.

**REVIEW empfohlen für den ersten V3-Test:** Reference Mix berechnen. Original, Balance, Reference Mix & Glue sowie Referenz vergleichen. Empfehlungen lesen. Einzelne Bearbeitungsgruppen per Checkbox abwählen und erneut berechnen. Erst danach den gewünschten Mix auswählen und mit „Mix bestätigen → REFERENCE MASTERING“ freigeben. Jede Neuberechnung erhält einen eigenen Projektordner; nichts wird in Quelldateien geschrieben. Für A/B/C-Auswahl und Export nach dem Mastering nach unten scrollen.

**AUTO:** Referenz wird vor dem Mix analysiert; anschließend Mix & Glue und alle drei Master berechnet. Aktivierte Gruppen erlauben begrenzte, signalabhängige Eingriffe; ein aktivierter Kompressor oder Transientenschritt kann mangels Kriterien tatsächlich 0 dB bearbeiten. Keine fest erzwungene Klangänderung.

**Loudness Matched** normalisiert nur die Wiedergabe. **Original Level** erhält die relativen Dateipegel, mit einem gemeinsamen angezeigten Sicherheits-Trim. Exporte sind immer die gerenderten 48-kHz-/24-Bit-PCM-WAVs mit deterministischem TPDF-Dither, unabhängig vom Monitor-Modus. Samplebasierte Überblendungen und gemeinsamer Abspielcursor stammen unverändert aus V2. Der Referenzsong läuft an gleicher Zeitposition, ist aber nicht automatisch musikalisch ausgerichtet.

## Tatsächlich implementierte Referenz-Mix-Phase

1. Analyse der bekannten eigenen Vocal-/Beat-Stems und des gesamten Referenzsongs: LUFS-I, Short-term-Maximum, 8x rekonstruierter Peak, Crest, 13 FFT-Energiezonen, Stereo-Korrelation, Breite.
2. Sechs-Sekunden-Fenster, aktive Fenster oberhalb −55 dBFS. Relative Energieklassen niedrig/mittel/hoch je Song. Dauergewichtete Spektren innerhalb jeder Klasse; gemeinsame Klassen gleich gewichtet. Mindestens zwei gemeinsame Klassen und vier gepaarte Fenster für die Abschnittsmethode, sonst ausdrücklich globaler Fallback mit schwächeren Korrekturen. Keine semantische Refrain-, Instrumenten- oder Spracherkennung. Die angezeigte „confidence“ ist eine feste konservative Regelstufe, keine statistisch kalibrierte KI-Wahrscheinlichkeit.
3. Referenz-Lücken werden über die bekannten eigenen Stem-Energieanteile konservativ verteilt. Vocal: drei EQ-Zonen 180/2800/9000 Hz (max. ±1,2 dB), zusätzlicher Vordergrund-Gain max. ±1 dB. Vocal-RMS-Kompression nur nach Crest-Heuristik, Ratio höchstens 1,4:1, Reduktion höchstens 2 dB. **Der Crest-Vergleich mit der Gesamt-Referenz ist kein verlässlicher Vergleich isolierter Stimmen.**
4. Stereo-Beat-EQ 85/350/2700/8500 Hz, maximal 1,5 dB; dynamische Tiefbass-Absenkung bis 1,5 dB bei Überschuss. Stereo-gekoppelter Attack-Gain bis 0,8 dB ausschließlich bei ausreichenden Abschnittsdaten und passendem Onsetproxy. Dies bearbeitet den gesamten Beat; keine unabhängige Kick-/Snare-/Bass-Bearbeitung.
5. Unmasking: aus sieben Kandidaten werden drei ausreichend getrennte Zonen nach tatsächlicher eigener Vocal-/Beat-Überlappung gewählt. Vocal-Aktivität, relative Bandenergie und Konkurrenz steuern den zeitabhängigen EQ. Max. 2,5 dB je Zone mal Stärke, 20-ms-Attack und 180-ms-Release; Bass ohne Überlappung wird nicht pauschal geduckt. Keine KI-Verständlichkeitsmessung.
6. Sechs separat abschaltbare Gruppen: Vocal-Tonalität/Vordergrund, Vocal-Dichte, Beat-EQ, dynamischer Tiefbass, Beat-Attack, Unmasking. Bericht enthält Messgrundlage, Vorschlag, Stärke und Aktivstatus. Abwahl wirkt erst nach Neuberechnung; Bestätigung und Export alter Master werden bei geänderten Entscheidungen gesperrt.

Eine fertige Referenz enthält keine isolierte Vocal oder Drums. Ohne Separation lässt sich ihr echtes Vocal/Beat-Verhältnis nicht bestimmen. Die V3 behauptet solche Messungen ausdrücklich nicht. Mid-Presence-, Crest- und Onset-Werte sind begrenzte Stereo-Proxys; die Zuordnung zu Instrumenten bleibt unsicher.

## Mastering und Visualisierungen

Der bestätigte Mix wird erneut gemessen. Bei hinreichenden Daten wird auch für die Master-EQ-Planung die Energieklassen-Methode verwendet; sonst globaler Fallback. Begrenzter Acht-Zonen-EQ, optionale Breitbandkompression, begrenzter Side-only-EQ, echtes stereo-gekoppeltes 8x-Lookahead-Limiting. Keine neue Sättigung, Tape-Simulation, Multibandkompression oder Soft-Clipping-Stufe ohne Qualitätsnachweis hinzugefügt.

A orientiert sich an der Referenz, B an vorsichtiger Körper-/Wärmegewichtung, C an Präsenz und Transientenerhalt. Die Parameter hängen vom Eingang ab; Unterschiede werden nicht künstlich erzwungen. Lautheitsneutraler Residualvergleich meldet nahezu identische Versionen. Dies ist keine Garantie musikalisch gut unterscheidbarer Master.

Ziel bleibt die echte Referenzlautheit (B −0,2 LU, C −0,5 LU), ohne −12-LUFS-Kappung. Bestehende Reduktionsbudgets bleiben absichtlich erhalten. Bei Zielverfehlung nennt der Bericht Restabstand in LU, Antrieb, maximale Limiter-Reduktion, Anteil über 3 dB und zusätzliche Peak-Sicherheitsabsenkung. Es gibt keinen Algorithmus, der daraus hörbare Verzerrungsfreiheit beweist. Finaler Export wird erneut auf Peak und Clipping geprüft; konservatives −1,3-dBTP-internes Ziel.

Grafiken zeigen reale gespeicherte Renderdaten: drei Unmasking-EQ-Steuerhüllkurven; Vocal-Kompressor, Beat-Attack-Gain und Bass-Kontrollhüllkurve; beim Master 100-ms-Maxima von Compressor-/Limiter-GR. EQ-Hüllkurven sind Steuerdaten, keine Messung eines isolierten Instruments. Balken zeigen vor dem Mastering die gemessene Referenz-minus-Balance-Bandabweichung, beim Master die tatsächlich eingestellten EQ-Gains. Die Master-EQ-Grafik ist keine finale spektrale Differenzmessung. Alle finalen Spektren und Dynamikmetriken stehen in JSON.

## Architektur und Grenzen

Blockweises Dekodieren/Rendern mit 4096 Frames, kleine FFTs und kurze Hüllkurven; keine vollständigen Songs im RAM. Zwischendateien brauchen lokal Platz; Vorabprüfung mit Reserve. Original/Balance bleiben separat. Referenz wird vor dem Mix gespeichert; ein Fingerabdruck aus Inhalt und Länge verhindert Mastering mit einer inzwischen geänderten Referenz. Dies ist eine Konsistenzprüfung, kein kryptografischer Sicherheitsmechanismus.

Die Stem-Prozessoren erhalten Vocal und Beat getrennt. Spätere Kick/Bass/Music-Prozessoren können an derselben Grenze ergänzt werden. V3 beinhaltet keine Separation, Advanced Stems, Kick/Bass-Sidechain, Stem-Rekonstruktionsprüfung, automatische Zeitkorrektur oder verlässliche Refrainzuordnung. Einzelne EQ-Bänder können noch nicht unabhängig bestätigt werden; abschaltbar sind die sechs dokumentierten Gruppen. Ein Projekt-Neuladedialog und automatische Versionsvergleiche über mehrere Projekte fehlen; Dateien und Berichte bleiben im gewählten Ordner erhalten.

## Qualitätssicherung und Bericht

`build-v3.sh` läuft ausschließlich auf einem ARM64-macOS-Runner. Der Workflow prüft bytegenau, dass V1, V2, Test-App und bestehende Workflows unverändert sind. Die unveränderten 16 V2-DSP/Player-Tests werden erneut ausgeführt, zusätzlich zehn V3-Verhaltenstests. V3-Integration prüft Referenzeinfluss **vor** dem Mastering, Energieklassen/Fallback, Abwahl/Bypass, Wiederholbarkeit, Quellenintegrität, falsche Referenzen, REVIEW-Mix-Auswahl, PCM24/Dateilängen, finale Peaks und native WAV/FLAC/SRC-Importe. Unveränderte V2 und V3 werden mit denselben synthetischen Stems und derselben Referenz verglichen.

Unabhängige Gegenmessung: FFmpeg ebur128 für LUFS und separater 16x-libsoxr-Peak-Messer. Zulässige Abweichung 0,15 LU / 0,12 dBTP im Testkorpus, unabhängige finale Peaks höchstens −1 dBTP. Tests enthalten auch kurze 20-kHz-Bursts. Native UI-/Report-Decoder-/Graph-/AVAudioSourceNode-Offline-Smoke-Tests, ARM64-Binärprüfung, Ad-hoc-Signaturprüfung und `hdiutil verify` sind verbindliche Build-Gates. Testwerkzeuge laufen nur in CI, nicht als Voraussetzung auf dem Nutzer-Mac.

Die jeweilige erfolgreiche CI-Ausführung und ihre `DSP-TESTS.txt`, `INTEGRATION-TESTS.txt`, `BUILD-REPORT.txt`, `SHA256SUMS.txt` sind die tatsächlichen Nachweise. Ein vorhandener Quellcode allein ist kein erfolgreicher nativer Build. Synthetische Tests belegen keine professionelle musikalische Qualität. Die originalen Bluelights-/PARFUM-Dateien wurden nicht bereitgestellt; die vom Nutzer genannten V2-Werte sind Hörtest-Ausgangsdaten, keine hier nachgemessenen V3-Ergebnisse.

Für den musikalischen Abnahmetest: gleiche Bluelights-Stems/Referenz in V2 und V3, zuerst REVIEW; Original/Balance/Reference Mix lautheitsangepasst und in Mono hören. Empfehlungen einzeln abwählen und neu rendern. Danach A/B/C gegen den bestätigten Mix und PARFUM, jeweils Loudness Matched und Original Level. Auf Stimme, Drum-Attack, Bass, S-Laute, Pumpen und Verzerrungen achten; mehrere repräsentative Passagen verwenden. Erst solche echten Hörtests erlauben eine Aussage über die Verbesserung.

## Lizenzen

Unverändertes r8brain-free-src unter MIT, Ooura-FFT-Autorenhinweise erhalten. Apple-Systemframeworks sind Bestandteil von macOS. Libsoxr/FFmpeg dienen nur unabhängigen CI-Tests und sind nicht in der ausgelieferten App enthalten. Details in THIRD-PARTY-NOTICES.txt und dem unveränderten Vendor-Verzeichnis unter v2/vendor/r8brain. Keine kostenpflichtigen Modelle oder API-Dienste.
