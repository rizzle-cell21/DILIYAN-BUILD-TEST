# DILIYAN STEM MASTER ENGINE V2 — 2.0.0 Experimental

Eigenständige native ARM64-App für macOS 13+, lokale Verarbeitung, keine KI-API,
kein Abo und keine zusätzlichen Werkzeuge auf dem Benutzer-Mac. Klangqualität
muss mit echten Songs und Hörtests validiert werden. V1 bleibt separat erhalten.

## Installation

Die V2-DMG öffnen, **DILIYAN STEM MASTER ENGINE V2.app** nach Programme ziehen.
V1 hat einen anderen Namen und eine andere Bundle-ID und wird nicht ersetzt.
Ad hoc signiert, nicht Developer-ID-signiert und nicht notarisiert. Wenn macOS
wegen des unbekannten Entwicklers blockiert: nach dem Öffnungsversuch unter
Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen bestätigen.
Keine globalen Sicherheitsfunktionen abschalten. Bei anderen Warnungen genauen
Text melden. Weder Terminal, Xcode, Homebrew noch Python sind auf dem Ziel-Mac nötig.

## AUTO und REVIEW

Vocal und Beat als WAV zuordnen. Referenz als WAV/FLAC. Formate, Kanäle und
Dauer werden geprüft; nur Mono oder Stereo, 8–192 kHz, bis 30 Minuten.
Gemeinsamen Stem-Exportstart bestätigen. Keine automatische Zeitausrichtung.
Kürzerer Stem wird mit Stille ergänzt, nicht der längere abgeschnitten.

**AUTO:** alle drei Dateien importieren, AUTO STEM MIX & GLUE drücken, neuen
lokalen Projekt-Elternordner wählen. Beide Phasen laufen nacheinander. Der
Standardmix ist Mix & Glue bzw. Balance, wenn Unmasking deaktiviert wurde.

**REVIEW:** zunächst nur Vocal und Beat nötig. Phase 1 rendern, Original,
Balance und Mix & Glue bei gemeinsamer Songposition vergleichen. Unmasking
aktivieren/deaktivieren oder einen Zustand auswählen. Dann Referenz wählen und
**Mix bestätigen → REFERENCE MASTERING** drücken. Erst dieser Schritt startet
Phase 2 mit dem gewählten Mix. Eine geänderte Unmasking-Stärke erfordert einen
neuen Phase-1-Render; alte Ausgaben bleiben erhalten.

Ergebnisse A/B/C auswählen und als Stereo-WAV mit 48 kHz / 24 Bit PCM exportieren.
Die exportierte Datei enthält keine Abhörnormalisierung. Quelldateien werden
nicht geändert. Jeder Durchlauf verwendet einen neuen Projektordner.
Mix.json und Master.json dokumentieren reale Messwerte, Entscheidungen und
Warnungen. Projekt öffnen führt zu diesen Berichten und allen Renderdateien.
Es gibt noch keinen GUI-Dialog zum Wiederöffnen früherer Projekte.

## Vergleichsmodi

**Loudness Matched:** ein gemeinsamer Zielwert (höchstens −18 LUFS, nötigenfalls
leiser für Headroom) für alle Vergleichsdateien. Nur Wiedergabe, keine Änderung
der Master oder der Referenzdatei. Die Verstärkung wird auf Samples angewendet;
auch Werte über 1 sind zulässig, sofern der berechnete Headroom sie erlaubt.

**Original Level:** keine individuelle Lautheitsanpassung. Ein gegebenenfalls
nötiger *gemeinsamer* Headroom-Trim gilt für sämtliche Dateien identisch und wird
in dB angezeigt. Er verhindert, dass beispielsweise der rohe Original-Stem-Summenmix
den Monitor übersteuert. Relative Dateilautheiten bleiben erhalten.

Original, Balance, Mix & Glue und A/B/C laufen auf derselben Sample-Zeitbasis.
Die Referenz hat ihren eigenen Songinhalt und ihre eigene Länge; gleiche Zeit
bedeutet keine musikalische Abschnittssynchronisation. Eine automatische
Refrain-/Strophen-Zuordnung wird nicht behauptet.

Wiedergabe: ein nativer AVAudioSourceNode mit C++-Streaming-Ringpuffer.
Verarbeitung und Überblendungen erfolgen sampleweise, nicht mit UI-Timern.
60-ms-Varianten-/Modus-Überblendung, 30-ms-Transport-Fades, synchrones Seeking.
Mono ist eine sampleweise Überblendung zur exakten Summe (L+R)/2.
Die UI zeigt den tatsächlich konsumierten Sample-Cursor. Pufferunterläufe werden
gezählt und angezeigt; problemlose Wiedergabe unter jeder Systemlast ist nicht garantiert.

## Phase 1: dynamisches Unmasking

- Original = unveränderte Stem-Pegel summiert.
- Balance = V1-Regel: Median aktiver 100-ms-RMS-Blöcke, Vocal-Ziel zunächst
  6 dB unter Beat, Änderung auf ±3 dB begrenzt. Kein neues statisches Beat-EQ.
- Mix & Glue = Balance plus stereo-verknüpfter dynamischer Beat-EQ bei
  800, 2000 und 4000 Hz. Keine zusätzliche versteckte Glue-Kompression.
- Echte Vocal-/Beat-Banddetektoren, Vollband-Aktivitätsschwelle aus Vocal-Pegel,
  relative Bandenergie und Konkurrenzverhältnis bestimmen die Absenkung.
- Je Band höchstens 2,5 dB × Stärke. Regler-Attack 20 ms, Release 180 ms;
  Filterkoeffizienten werden laufend geglättet. Vocal wird durch Unmasking
  nicht gefiltert; Kick-/Bassbereich wird nicht breitbandig heruntergeregelt.
- Keine Pflichtbearbeitung: Stärke 0 ist ein exakter Bypass. Ohne relevante
  Aktivität oder Überlappung kein anhaltendes Ducking.
- Die Grafik enthält die beim Rendern gespeicherten 100-ms-EQ-Steuerhüllkurven
  der drei Bänder. Sie ist keine Fake-Animation und keine Sprach-/Instrumenten-KI.
  Die Reglerabsenkungen sind nicht mit einer isoliert gemessenen Instrumentenlautheit gleichzusetzen.

Es handelt sich um erklärbare heuristische Bandverarbeitung, nicht um eine
psychoakustisch vollständige Sprachverständlichkeits- oder Maskierungsbewertung.
Bereits gemischte Doubles/Adlibs können nicht einzeln identifiziert werden.
In kurzen Pausen läuft die Release-Hüllkurve musikalisch aus; keine harte Gate-Schaltung.

## Phase 2: Referenz-Mastering

- K-gewichtete LUFS-I-/Short-term-Messung und 13 logarithmische FFT-Bänder aus
  aktiven Hann-Fenstern. Subbass, Bass, Mitten, Präsenz und Höhen getrennt
  erfassbar; keine erfundene semantische Abschnittserkennung.
- Acht begrenzte EQ-Zonen mit Referenzvergleich und materialabhängigen
  Richtungen für A/B/C. Maximale einzelne Korrektur ±1,6 dB.
- Adaptive sanfte RMS-Kompression: Ratio bis 1,6:1, Schwelle aus aktivem Pegel,
  Attack/Release je Charakter; höchstens 3 dB Kompressor-Reduktion.
- Frequenzabhängige Seitenenergie wird gemessen. Kleiner Seiten-EQ (maximal
  ±0,6 dB pro Zone bei 600/2500/9000 Hz) kann der Referenz folgen. Keine neuen
  Seitensignale, keine Änderung der Mid-Summe durch diesen Schritt. Bei
  niedriger Korrelation kein positives Seiten-EQ-Ziel. Keine automatische
  Phasenreparatur oder künstliche starke Verbreiterung.
- A berücksichtigt die tatsächliche Referenzlautheit, ohne die alte −12-LUFS-Grenze.
  B zielt 0,2 LU, C 0,5 LU darunter. C hat mehr Attack-Erhalt und ein geringeres
  Limiter-Budget; B priorisiert bedarfsabhängig Körper und weniger Schärfe.
- 8×-Oversampling (384 kHz), stereo-verknüpfter 5-ms-Lookahead-Limiter mit
  geglätteter Attack-/Release-Hüllkurve und zusätzlicher Sample-Schutzgrenze.
- Begrenzte iterative Lautheitskorrektur. Maximal 24 dB Drive-Anhebung und
  Profil-GR-Budgets von 6/5,5/4,5 dB. Bei anhaltender Reduktion wird der Drive
  zurückgenommen. Diese messbaren Grenzen ersetzen keine Hörbewertung.
- Nach Downsampling hochwertige 8×-Rekonstruktion, nötigenfalls globaler
  Sicherheitstrim auf −1,3 dBTP-Schätzung, PCM24 mit TPDF-Dither und erneute
  Messung des tatsächlichen Exports. Interne Freigabegrenze −1,15 dBTP.
- Im Build: unabhängige Gegenmessung mit FFmpeg/libsoxr bei 16×. Kein allgemeiner
  BS.1770-Konformitäts-/True-Peak-Zertifizierungsanspruch.
- Nahezu identische Varianten werden anhand des lautheitsneutralen RMS-Rests
  erkannt (unter −35 dBr) und in der UI angezeigt. Dies ist ein Näherungskriterium,
  kein universeller Hörbarkeitstest. Es werden keine Klangunterschiede erzwungen.

Sättigung, Tape und Soft Clipping sind absichtlich nicht enthalten; ihr Nutzen
ist für diese Version nicht nachgewiesen. Unveränderte Referenz-Originaldatei;
Analyse und Wiedergabe verwenden lediglich eine lokale dekodierte 48-kHz-Kopie.

## Grenzen und reale Qualität

Keine Original-Bluelights-Dateien waren bei der Entwicklung verfügbar.
Synthetische Prüfungen können keine professionelle Klangqualität bestätigen.
Die App kann nicht automatisch garantieren, dass eine Absenkung musikalisch
besser oder ein bestimmter LUFS-Wert unverzerrt erreichbar ist. Wenn das Ziel
verfehlt wird, nennt Master.json/UI die verwendeten Budgets und den Endwert.
Die Erkennung bereits geclippter/verzerrter Eingänge ist weiterhin begrenzt.

Auf dem M1 mit 8 GB sind DSP und Datei-I/O blockweise; Streaming-Ringpuffer
benötigt nur wenige MB. Keine vollständigen Songs für die Verarbeitung im RAM.
Mehrere Zwischen-/Ergebnisdateien brauchen lokalen SSD-Platz; freier Speicher
wird vorab geprüft. Renderzeit und Wiedergabe unter Last müssen auf dem echten
Mac gemessen werden. Abbruch hinterlässt einen klar unvollständigen Projektordner.

## Reproduzierbare Prüfungen und unveränderte V1

v2/Tests.cpp prüft Aktivitäts-/Überlappungsreaktion und Pausen, Bypass,
Oversampling-Länge und Ausrichtung, LUFS sowie den echten C++-Player mit
sampleweisen Fades, Seeking, Pause und beiden Pegelmodi.
v2/integration.py prüft vollständige Phasen, V1-Vergleich, Wiederholbarkeit,
PCM24, 13 Spektralbänder, FLAC/SRC auf Mac und unabhängige LUFS-/16×-Peak-Messungen
inklusive des V1-Hochfrequenz-Gegenbeispiels.
Ein nativer AVAudioEngine-Offline-Test prüft die tatsächliche Source-Node-Brücke.

V1-Basis: 683d785a9f6f373a8f0db8c96956e41123022a65.
V1-Quellcode, alte Workflows, Test-App, Sicherung und DMG werden nicht verändert.
V2 besitzt eigenen Branch, eigenen Workflow, eigenen App-Namen, eigene Bundle-ID
und separate Artefakte. Im öffentlichen Repository laufen nur Standard-macOS-Runner;
keine kostenpflichtige KI-API oder Apple-Mitgliedschaft wird aktiviert.
FFmpeg und eventuell Homebrew werden ausschließlich zum Testen auf dem CI-Mac
verwendet und nicht auf dem Benutzer-Mac installiert oder in die App eingebettet.

## Lizenz

r8brain-free-src wird unter MIT verwendet; Lizenz und Autorenhinweise liegen
im App-Bundle und im Quellcode. Upstream-Revision und Hinweise stehen in
THIRD-PARTY-NOTICES.txt. Alle zur App nötigen DSP-Abhängigkeiten sind eingebettet,
keine nachträglichen Downloads. Apple-Systemframeworks nach Apple-SDK-Bedingungen.
