# DILIYAN ENGINE V3 – technischer Ergebnisbericht

## Auslieferungsstand

Echte native ARM64-App, Version 3.0.0 Experimental. Source-Branch `dev/audio-v3`, Commit `af2719332ddfa5b28dc4425094d90e4596e497bb`.

Erfolgreicher finaler Build: https://github.com/rizzle-cell21/DILIYAN-BUILD-TEST/actions/runs/40137595714

GitHub-Actions-Artefakt: `DILIYAN-ENGINE-V3-arm64`, ID 12577502490. Enthält DMG, App-ZIP, Prüfsummen und die tatsächlichen Test-/Build-Protokolle; reguläre Aufbewahrung 14 Tage. Die identische DMG und diese Protokolle sind zusätzlich im separaten Download-Branch gesichert. Kein Merge nach main, keine Änderung an V1/V2.

macOS 15.7.9, ARM64, Xcode 16.4, Swift 6.1.2. App-Mindestversion macOS 13.0. Kein echter M1-Hörtest in dieser Umgebung; der Nutzer-Mac bleibt der abschließende Praxistest.

DMG: 542299 Bytes. SHA-256:

`417d9571895452c26adf25668d68f3541a5798c9178e1c7b496df9808c84796b`

App-ZIP SHA-256:

`2cbb04c795e3eca9b35b30c1fc69a26bce0acd041f0b400b11d45f96b25bc44e`

## Implementiert

Referenzanalyse erfolgt jetzt vor dem Mischen. Sechs-Sekunden-Fenster werden nach relativer Energie je Song gruppiert. Mindestens zwei gemeinsame Energieklassen und vier gepaarte Fenster erforderlich; sonst sichtbarer globaler Fallback. Keine semantische Refrain-Erkennung, keine isolierten Referenzinstrumente.

Vocal-Tonalität/Vordergrund, Vocal-Dichte, Stereo-Beat-EQ, dynamische Tiefbasskontrolle, begrenzter Beat-Attack-Gain sowie konfliktabhängiges Unmasking bilden sechs einzeln abschaltbare Bearbeitungsgruppen. Unmasking-Zentren werden aus tatsächlicher eigener Stem-Überlappung gewählt. REVIEW zeigt Messgrundlage, Vorschlag und Aktivstatus; Änderungen erfordern Neurendering. AUTO führt beide Phasen aus. Jede Berechnung erzeugt einen neuen Projektordner.

Original, Balance, Reference Mix & Glue, Referenz sowie drei gerenderte Master sind separat hörbar. Unveränderter V2-Streaming-Player mit gemeinsamem Sample-Cursor, Überblendungen, Loudness Matched, Original Level und Mono. Monitor-Trim verändert keine exportierten WAVs.

Grafiken: reale gespeicherte Unmasking-Steuerhüllkurven, Mix-Dynamik-Kontrollwerte, Compressor-/Limiter-GR-Maxima und Frequenzbalken. Die Balken sind vor dem Mastering Referenz-minus-Balance-Analyse, bei Mastern angewendete EQ-Gains; keine behauptete finale Referenz-Übereinstimmung.

Mastering berücksichtigt bei ausreichenden Daten ebenfalls Energieklassen. Bestehende konservative 8x-Limiter-/Peak-Schutzarchitektur bleibt erhalten. Zielverfehlungen nennen Rest-LU, Drive, maximale Reduktion, Anteil über 3 dB und Sicherheits-Trim. Keine zusätzliche Sättigung, Tape-Simulation, Soft-Clipping- oder Multiband-Stufe implementiert.

## Tatsächliche Tests

Alle nachfolgenden Build-Gates im finalen macOS-Lauf bestanden:

- 16 unveränderte V2-DSP-/Player-Verhaltenstests: Aktivität/Release, Bass ohne Überlappung, Null-Bypass, Oversampling-Länge/Passband, LUFS-Baseline, samplebasierte Crossfades, Seek/Pause/Monitor-Gain und Mono-Erhalt bei Side-EQ.
- 10 V3-Verhaltenstests: referenzabhängige Stem-Entscheidungen, Gegenteil bei geänderter Referenz, kompletter Bypass, echte begrenzte Vocal-/Bass-Dynamik, ausgewählte Unmasking-Frequenz, Energieklassen, identische Abschnitte ohne erfundene Differenz sowie ausdrücklicher Fallback.
- Integration: Referenzeinfluss schon auf den Mix, sechs Entscheidungen, WAV-Längen und echte Spektraldaten; drei endliche PCM24-Master; bitidentische Wiederholungen; bitidentischer Balance-Bypass nach Abwahl bzw. Unmasking-Stärke null; REVIEW-Auswahl „Balance“ wird berücksichtigt; falsche/geänderte Referenz wird abgelehnt; Quellen unverändert; vorhandene Ausgabe und doppelte Rollen werden abgelehnt.
- Unveränderte V2 und V3 unter identischen synthetischen Bedingungen gerendert.
- Unabhängige Integrated- und Short-Term-LUFS-Gegenmessung mit FFmpeg ebur128. Akzeptanzgrenze 0,15 LU im Testkorpus.
- Unabhängige 16x-libsoxr-Peak-Gegenmessung. Messabweichung im Testkorpus höchstens 0,12 dB; finale unabhängige Peaks höchstens −1 dBTP. Diese Tests ersetzen keine allgemeine Zertifizierung.
- Kurze 20-kHz-Bursts als Regression gegen den früheren V1-Fehler: alle drei V3-Master bestehen die unabhängige Peak-Prüfung.
- Native Core-Audio-Importe: 44,1-kHz-Mono-Vocal, 48-kHz-Stereo-Beat mit anderer Länge und FLAC-Referenz, danach vollständiges Mastering.
- Native App gestartet, alle V3-JSON-Berichte decodiert, sieben Vergleichsansichten und Grafiken durchlaufen; geänderte REVIEW-Entscheidung sperrt Mastering und Export alter Ergebnisse. AVAudioSourceNode offline mit realem Audiopuffer geprüft. Kein physischer Lautsprecher-/Kopfhörertest.
- Beide Programme per `lipo` als ARM64 geprüft; ad-hoc-Signaturprüfung bestanden; `hdiutil verify` meldet DMG **VALID**.
- Heruntergeladenes Actions-ZIP gegen GitHub-SHA-256 geprüft, beide inneren Datei-Prüfsummen und ZIP-Integrität geprüft. Mach-O-Header beider Programme sind ARM64; Bundle-ID `local.diliyan.stemmaster.engine.v3`, Version 3.0.0. DMG-UDIF-Footer vorhanden.

Zusätzlich lokal: GCC/Linux-Verhaltenstests und Integration erfolgreich; AddressSanitizer/UndefinedBehaviorSanitizer für V3-Verhaltenstests ohne Fehler. LeakSanitizer war in der eingeschränkten Laufzeit deaktiviert, daher keine Aussage über vollständige Leckfreiheit.

Der erste native V3-Lauf scheiterte korrekt am strengen bitgenauen Bypass-Test. Die ARM64-Abweichung beim erneuten Berechnen von „Balance“ wurde durch direkte Verwendung der bereits gerenderten Balance-Samples im vollständigen Bypass behoben. Die Prüfung wurde nicht gelockert. Anschließend zwei native Builds erfolgreich, final zusätzlich lineare GR-Grafikzuordnung und REVIEW-Invalidierung geprüft.

## Gemessener V2/V3-Vergleich – ausschließlich synthetischer 24-Sekunden-Test

Vier Abschnitte mit verschiedenen Energien, fest definierte Vocal-/Beat-Testsignale, dieselbe Referenz bei −8,39 LUFS-I. Kein Bluelights-/PARFUM-Audiomaterial.

| Messgröße | V2 | V3 |
|---|---:|---:|
| Mix & Glue LUFS-I | −19,77 | −21,37 |
| Mix Crest | 10,86 dB | 11,20 dB |
| Mix Bassenergie-Proxy | 82,33 % | 78,63 % |
| Mix Präsenzenergie-Proxy | 7,24 % | 8,24 % |
| Master A LUFS-I | −8,67 | −8,56 |
| Master B LUFS-I | −8,82 | −8,85 |
| Master C LUFS-I | −9,08 | −9,10 |
| Master A Limiter maximal | 3,48 dB | 3,97 dB |
| Master A Limiter mittel | 0,54 dB | 0,70 dB |

Die Änderung der Frequenzbalance ist real messbar. Die V3-Mixlautheit ist hier niedriger und der Limiter bei Master A stärker belastet. Daraus folgt **kein** Nachweis besseren Klangs oder höherer Transparenz. Lautheitsangepasstes Hören ist entscheidend.

Unabhängige finale V3-Peaks des 24-Sekunden-Tests: A −1,298, B −1,298, C −1,299 dBTP. Unabhängige Peaks des 20-kHz-Burst-Tests: A −1,296, B −1,292, C −1,241 dBTP. Exportlängen exakt erhalten, keine Samples bei/über 0 dBFS.

Im selben synthetischen Test V2 A/B als nahezu identisch erkannt (−35,91 dBr lautheitsneutraler Rest), V3 A/B −30,83 dBr; V3 A/C −25,09 und B/C −29,34 dBr. Diese Differenzen belegen unterschiedliche Samples, keine musikalisch bessere Profilwahl.

Die vollständigen maschinenlesbaren Render-Messungen stehen in den jeweiligen Mix.json/Master.json der Testläufe; die CI-Protokolle enthalten die Vergleichsdaten. Unter „Projekt & Messberichte“ speichert die Nutzer-App die Berichte jeder eigenen Berechnung.

## Erhaltene Stände

Remote-Branch-Köpfe zusätzlich geprüft:

- main: `5e2fd839081843937cb2c1b7cdbbeb4141787625`
- dev/audio-v1: `683d785a9f6f373a8f0db8c96956e41123022a65`
- dev/audio-v2: `5576a5ac8bdcacd5385f86bfe792cf0be5f27128`
- download-audio-v1-0.1.0: `4ec2b7ecef1a8e43d7677ff2b73e16cbe091d569`
- download-audio-v2-2.0.0: `e54811bc8701732e3ce720d8f9d3903c15c42c8b`
- backup-audio-v1-2026-10-24: `d02636a84a3b4ac934bedeeefbd47935e9e169cf`

Die finale Quellcodeänderung gegenüber V2 enthält ausschließlich neue Dateien in v3/ sowie engine-v3.yml. Der CI-Schutz prüft V1/V2/Test-App/bisherige Workflows bytegenau. Keine bestehenden Dateien, Releases oder DMGs gelöscht oder ersetzt.

## Grenzen und nächster Praxistest

Regelbasierte experimentelle Engine, keine KI-basierte Verständlichkeits- oder Stimmtrennung. Eine Referenz-Stereo-WAV erlaubt keine sichere isolierte Vocal-/Beat-/Kick-/Snare-Messung. Abschnittszuordnung ist relative Energieheuristik, keine verlässliche Refrainzuordnung. Bearbeitungen werden pro sechs Gruppen bestätigt, noch nicht pro EQ-Band. Keine automatische Zeitreparatur, Separation, Advanced Stems oder Kick/Bass-Sidechain. Keine automatisierte Erkennung aller hörbaren Verzerrungen, Pumpen oder musikalischen Fehlentscheidungen. Globale Clipping-/Korrelationswerte können bereits beschädigte Quellen oder lokale Phasenprobleme nicht vollständig diagnostizieren oder reparieren.

Die tatsächlichen Bluelights- und PARFUM-Dateien fehlen hier weiterhin. Die vom Nutzer berichteten V2-Werte werden nicht als eigene Messung oder V3-Ergebnis ausgegeben. Professionelle musikalische Qualität bleibt unbewiesen.

Installation: DMG öffnen, V3-App nach Programme ziehen, starten. Private ad-hoc-Testversion ohne Notarisierung. Bei Gatekeeper-Blockade nach dem Startversuch **Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen**, anschließend bestätigen. Auf dem Mac sind keine Entwicklungswerkzeuge oder Terminalbefehle erforderlich.

Für die Abnahme: zuerst REVIEW mit denselben Bluelights-Stems/PARFUM. Original, Balance und Reference Mix lautheitsangepasst sowie mono hören, auffällige Gruppen abwählen und neu rendern. Erst dann den bevorzugten Mix bestätigen und A/B/C gegen Mix und Referenz prüfen. Original Level separat für reale Lautheitsverhältnisse verwenden. Stimme, Bass, Kick-/Snare-Attack, S-Laute, Pumpen und Verzerrungen an mehreren Stellen bewerten. Die App kann das Lautheitsziel weiter verfehlen, wenn sichere Budgets es begrenzen; sie soll es nicht um jeden Preis erzwingen.
