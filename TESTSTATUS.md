# V1-Teststatus — tatsächlich ausgeführte Prüfungen

Umgebung: Debian 13 / x86-64, GCC; keine Apple-SDKs und kein Swift-Compiler.
Repository-Basis: 5e2fd839081843937cb2c1b7cdbbeb4141787625.
Die vier vorhandenen Build-Test-Dateien wurden unverändert belassen.

## Bestanden unter Linux

- Strikte C++17-Kompilierung von Engine und Tests mit -Wall -Wextra -Wpedantic -Werror.
- 18 portable DSP-Prüfungen; siehe Protokoll unten.
- Vollständiger synthetischer Renderlauf mit 3 unterschiedlichen PCM24-Dateien.
- Länge des längeren Stems, Quellschutz, Ablehnen doppelter Zuordnung,
  Überschreiben, Stille und beschädigter Datei.
- Hochpegeliger Transienten-Stresstest mit tatsächlicher Limiter-Reduktion.
- Exakte Monoarithmetik anhand des erzeugten Audios.
- Unabhängige FFmpeg-Lautheits-/Peak-Gegenmessung auf den synthetischen Masters.
- AddressSanitizer/UndefinedBehaviorSanitizer ohne Befund auf den DSP-Tests.
  LeakSanitizer musste deaktiviert werden, weil diese Umgebung unter ptrace läuft;
  eine Leak-Prüfung ist damit nicht nachgewiesen.
- Shell-Syntax, Python-Syntax und Workflow-Struktur geprüft.

## Noch nicht nachgewiesen

- Native V1-Kompilierung unter macOS; der neue Workflow wurde nicht gestartet.
- Swift-/AppKit-Typprüfung sowie Apple-FLAC-/Resampling-Backend.
- DMG-/ZIP-Erzeugung der V1, Gatekeeper und Start auf M1.
- A/B/C-Synchronität, Umschaltgeräusche, Audio-Geräteverhalten und Klang auf M1.
- BS.1770-/EBU-Konformität auf einem offiziellen vollständigen Testkorpus.
- Klangqualität an echten Songs; es liegen keine Benutzeraudiodateien vor.

Die App und die automatischen Mac-Tests sind vorbereitet, nicht als bestanden ausgegeben.
Der alte erfolgreiche Fenster-Test ist kein V1-Audio-Build-Nachweis.

## Portable DSP-Tests

```text
PASS Zero-dB EQ null test
PASS Peaking EQ center gain
PASS Low shelf DC response
PASS High shelf Nyquist response
PASS 48k stereo 1kHz sine loudness approximately -20 LUFS
PASS Three-second short-term loudness
PASS Identical stereo mono compatibility
PASS Anti-phase detection
PASS Silence gate and frame count
PASS PCM24 roundtrip length and headroom
PASS PCM24 TPDF error bounded below two LSB
PASS Intersample peak exceeds sample peak of phase-offset sine
PASS Limiter compensated length including first/last frame
PASS Lookahead limiter sample ceiling
PASS Boundary transients retained
PASS Limiter stereo linking
PASS Material-adaptive profile directions
PASS Plans change with material rather than fixed presets
All portable DSP tests passed. Not a perceptual quality certification.
```

## End-to-End-Protokoll

```text
INDEPENDENT Master-A.wav: FFmpeg -17.5 LUFS / -6.2 dBTP; engine -17.448 / -6.221
INDEPENDENT Master-B.wav: FFmpeg -17.5 LUFS / -6.2 dBTP; engine -17.448 / -6.220
INDEPENDENT Master-C.wav: FFmpeg -18.0 LUFS / -6.7 dBTP; engine -17.948 / -6.666
PASS synthetic end-to-end: 3 distinct PCM24 masters, length, peak checks, source preservation, duplicate/overwrite/silence/corruption rejection
PASS high-crest limiter stress, exported peak ceiling, exact L/R mono arithmetic
No listening validation or professional-quality certification has occurred.
```
