# DILIYAN STEM MASTER – Build Test

Nur Phase 1: native AppKit-Fenster-App ohne Audio-Engine, Drittbibliotheken oder Netzwerkfunktionen. Ziel: Apple Silicon, macOS 13 oder neuer.

## Tatsächlicher Status

In Linux vorbereitet, noch NICHT mit dem macOS-SDK kompiliert. Keine fertige .app oder .dmg liegt in diesem Vorbereitungspaket. Erst ein erfolgreicher Actions-Lauf erzeugt die Binärdateien. Auch danach bleibt der Download-/Gatekeeper-Test auf dem Ziel-Mac erforderlich.

## Kostenloser Build ausschließlich im Browser

1. Bei GitHub anmelden (kostenloses Konto genügt). Neues leeres Repository anlegen, beispielsweise `diliyan-build-test`. Für den hier vorgesehenen kostenlosen Standard-Runner-Weg ein **öffentliches** Repository wählen. Dadurch werden nur diese Testdateien öffentlich, keine Audiodateien. Wenn du das nicht möchtest, erst eine private Alternative mit gesperrter kostenpflichtiger Nutzung abstimmen.
2. Vorbereitungspaket im Finder entpacken. Mit **Umschalt + Befehl + Punkt** versteckte Dateien anzeigen, damit `.github` sichtbar ist.
3. Im Repository **Add file → Upload files** wählen. Den Inhalt des Ordners `diliyan-build-test` einschließlich `.github`, `Sources`, `scripts` und dieser Anleitung hochladen, nicht den umschließenden Ordner. Änderungen speichern (**Commit changes**). Es müssen unter anderem `Sources/main.swift` und `.github/workflows/build-macos.yml` direkt unter der Repository-Wurzel liegen.
4. Unter **Actions** gegebenenfalls Workflows aktivieren. **macOS Apple Silicon Build Test → Run workflow → Run workflow** wählen. Keine kostenpflichtigen Runner, Pläne oder Zahlungsoptionen aktivieren. In Organisationen können Administratorregeln Actions blockieren.
5. Auf grünen Abschluss warten. Bei Rot liegt noch kein erfolgreicher Build vor; den fehlgeschlagenen Schritt bzw. das Protokoll zur Diagnose bereitstellen.
6. Im abgeschlossenen Lauf unter **Artifacts** `DILIYAN-Build-Test-arm64` herunterladen. GitHub verlangt dafür normalerweise eine Anmeldung. Das äußere Artifact-ZIP entpacken. Darin befinden sich nach Erfolg DMG, App-ZIP, SHA256-Prüfsummen und BUILD-REPORT.txt. Die Aufbewahrung ist auf drei Tage begrenzt; anschließend erneut manuell bauen.

Es werden keine Repository-Geheimnisse oder Apple-Zertifikate benötigt. Workflow wird nur manuell gestartet, hat ausschließlich lesenden Repository-Zugriff und fordert keinen kostenpflichtigen großen Runner an. Private Repositories haben andere Minuten-/Speicherkontingente; dieser Workflow erzwingt selbst keine Kontokostensperre.

## Auf dem Mac installieren und öffnen

1. DMG per Doppelklick öffnen.
2. App auf `Programme` ziehen. DMG auswerfen.
3. App in `Programme` doppelklicken.
4. Falls macOS wegen unbekanntem Entwickler oder fehlender Notarisierung blockiert: **Systemeinstellungen → Datenschutz & Sicherheit → Sicherheit → Dennoch öffnen**. Gegebenenfalls mit Passwort/Touch ID bestätigen und erneut **Öffnen** wählen. Die Option erscheint erst nach dem Öffnungsversuch; Wortlaut hängt von macOS ab.
5. Erwartet: Fenster `DILIYAN STEM MASTER – Build Test` mit erklärendem Text. Fenster schließen oder **Befehl + Q** beendet die App.

Alternative: Das innere `DILIYAN-Build-Test-arm64.zip` entpacken und die enthaltene .app nach Programme ziehen.

Keine globalen Sicherheitseinstellungen deaktivieren. Bei Meldungen über Beschädigung, Schadsoftware, fehlender Freigabeoption oder auf verwalteten Macs den genauen Text melden. Ein grafischer Start ist vorgesehen, aber noch nicht am heruntergeladenen Build auf dem Zielgerät nachgewiesen.

## Signatur und Nachweis

Die private Test-App wird technisch **ad hoc signiert**. Dies ist keine durch Apple bestätigte Entwickleridentität und keine Notarisierung. Umgangssprachlich ist sie eine unsignierte private Testversion. Ein Apple-Developer-Konto und kostenpflichtige Dienste werden nicht verwendet.

Der Build prüft die ARM64-Architektur, Bundle-Plist und lokale Signatur. Er startet das echte Programm mit einem zeitbegrenzten Smoke-Test, der AppKit-Ereignisschleife und Sichtbarkeit des Fensters prüft. Danach erzeugt und prüft er ein DMG sowie ein ZIP. Dies ersetzt weder visuelle Bedienprüfung noch Gatekeeper-Test nach Browserdownload.

Der Ablauf ist reproduzierbar beschrieben, nicht bitidentisch garantiert: GitHub aktualisiert Runner/Xcode und die verwendeten Action-Hauptversionen. BUILD-REPORT.txt dokumentiert Toolchain und Quellrevision jedes erfolgreichen Laufs.

## Rückmeldung für den Build-Nachweis

Bitte macOS-Version, erfolgreichen/fehlgeschlagenen Start, angezeigten Fenstertext und eventuelle Sicherheitsmeldung festhalten. Erst dann kann der End-to-End-Build-Nachweis auf deinem M1 als bestanden gelten. Audio-Entwicklung bleibt ausdrücklich außerhalb dieser Phase.

## Offizielle Quellen

- GitHub Standard-Runner und kostenlose öffentliche Builds: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- Apple Sicherheitsfreigabe: https://support.apple.com/102445
