import AppKit
import AVFoundation
import UniformTypeIdentifiers

struct SourceMetadata: Decodable {
    let sampleRate: Double
    let channels: Int
    let seconds: Double
    let bits: Int
}
struct AudioMetrics: Decodable {
    let frames: Int64
    let seconds: Double
    let lufs: Double
    let shortMax: Double
    let samplePeak: Double
    let truePeakEstimate: Double
    let correlation: Double
    let width: Double
    let leadingSilenceSeconds: Double
}
struct MasterResult: Decodable {
    let file: String
    let metrics: AudioMetrics
    let lowDB: Double
    let presenceDB: Double
    let highDB: Double
    let compressionRatio: Double
    let maxLimiterReductionDB: Double
}
struct SourceResult: Decodable {
    let path: String
    let info: SourceMetadata
    let metrics: AudioMetrics
}
struct Session: Decodable {
    let comparisonLUFS: Double
    let referenceComparisonLUFS: Double
    let vocalGainDB: Double
    let beatPresenceDB: Double
    let original: AudioMetrics
    let masters: [MasterResult]
    let sources: [SourceResult]
    let warnings: [String]
}

// Child process keeps analysis/rendering off the UI/audio thread. No shell used.
final class ToolJob {
    private let lock = NSLock()
    private var process: Process?
    private var cancelled = false
    func cancel() {
        lock.lock(); defer { lock.unlock() }
        cancelled = true
        if let p = process, p.isRunning { p.terminate() }
    }
    func execute(_ arguments: [String], progress: @escaping ([String: Any]) -> Void,
                 completion: @escaping (Result<Data, Error>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            let errorURL = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            defer { try? FileManager.default.removeItem(at: errorURL) }
            do {
                guard let executable = Bundle.main.executableURL?.deletingLastPathComponent().appendingPathComponent("DiliyanEngine"),
                      FileManager.default.isExecutableFile(atPath: executable.path) else {
                    throw NSError(domain: "Diliyan", code: 1, userInfo: [NSLocalizedDescriptionKey: "Audio-Engine fehlt im App-Bundle."])
                }
                FileManager.default.createFile(atPath: errorURL.path, contents: nil)
                let errors = try FileHandle(forWritingTo: errorURL)
                defer { try? errors.close() }
                let p = Process(), pipe = Pipe()
                p.executableURL = executable; p.arguments = arguments
                p.standardOutput = pipe; p.standardError = errors
                self.lock.lock()
                if self.cancelled {
                    self.lock.unlock()
                    throw NSError(domain: "Diliyan", code: 2, userInfo: [NSLocalizedDescriptionKey: "Abgebrochen."])
                }
                self.process = p
                do { try p.run(); self.lock.unlock() }
                catch { self.lock.unlock(); throw error }
                var all = Data(), pending = Data()
                while true {
                    let data = pipe.fileHandleForReading.availableData
                    if data.isEmpty { break }
                    all.append(data); pending.append(data)
                    while let end = pending.firstIndex(of: 10) {
                        let line = pending.subdata(in: pending.startIndex..<end)
                        pending.removeSubrange(pending.startIndex...end)
                        if let obj = try? JSONSerialization.jsonObject(with: line) as? [String: Any] {
                            DispatchQueue.main.async { progress(obj) }
                        }
                    }
                }
                p.waitUntilExit()
                self.lock.lock(); let stopped = self.cancelled; self.process = nil; self.lock.unlock()
                if stopped || p.terminationStatus != 0 {
                    let detail = (try? String(contentsOf: errorURL, encoding: .utf8)) ?? "Unbekannter Engine-Fehler."
                    throw NSError(domain: "Diliyan", code: Int(p.terminationStatus),
                                  userInfo: [NSLocalizedDescriptionKey: stopped ? "Abgebrochen. Unvollständiger Projektordner bleibt zur Diagnose erhalten." : detail])
                }
                DispatchQueue.main.async { completion(.success(all)) }
            } catch {
                DispatchQueue.main.async { completion(.failure(error)) }
            }
        }
    }
}

// File-streaming players share the same host-time start. Switching only
// ramps volumes (60ms); transport/seek fades down before rescheduling.
final class ComparisonPlayer {
    private let engine = AVAudioEngine()
    private var nodes: [AVAudioPlayerNode] = []
    private var files: [AVAudioFile] = []
    private var gains: [Float] = []
    private var timer: Timer?
    private var transition: UInt64 = 0
    private var hostStart: UInt64 = 0
    private var startFrame: AVAudioFramePosition = 0
    private var pausing = false
    private(set) var playing = false
    private(set) var selected = 1
    var duration: Double { files.first.map { Double($0.length) / 48000 } ?? 0 }
    var position: Double {
        guard playing else { return Double(startFrame) / 48000 }
        let now = mach_absolute_time()
        let elapsed = now > hostStart ? AVAudioTime.seconds(forHostTime: now - hostStart) : 0
        return min(duration, Double(startFrame) / 48000 + elapsed)
    }
    func load(urls: [URL], levels: [Double]) throws {
        stopImmediately()
        for node in nodes { engine.detach(node) }
        nodes.removeAll(); files.removeAll(); gains.removeAll()
        let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
        for (index, url) in urls.enumerated() {
            let file = try AVAudioFile(forReading: url)
            guard file.processingFormat.sampleRate == 48000, file.processingFormat.channelCount == 2 else {
                throw NSError(domain: "Diliyan", code: 3, userInfo: [NSLocalizedDescriptionKey: "Vergleichsdatei hat unerwartetes Format."])
            }
            let node = AVAudioPlayerNode(); engine.attach(node)
            engine.connect(node, to: engine.mainMixerNode, format: format)
            node.volume = 0
            nodes.append(node); files.append(file); gains.append(Float(pow(10, levels[index] / 20)))
        }
        if let length = files.first?.length, files.contains(where: { $0.length != length }) {
            throw NSError(domain: "Diliyan", code: 5, userInfo: [NSLocalizedDescriptionKey: "Vergleichsdateien sind nicht gleich lang."])
        }
        selected = min(selected, nodes.count - 1)
        engine.prepare(); try engine.start(); startFrame = 0
    }
    func select(_ index: Int) { selected = index; if playing && !pausing { ramp(up: true) } }
    private func ramp(up: Bool, completion: (() -> Void)? = nil) {
        timer?.invalidate()
        let initial = nodes.map { $0.volume }, began = Date.timeIntervalSinceReferenceDate
        timer = Timer.scheduledTimer(withTimeInterval: 0.01, repeats: true) { [weak self] timer in
            guard let self = self else { timer.invalidate(); return }
            let fraction = min(1, (Date.timeIntervalSinceReferenceDate - began) / 0.06)
            for i in self.nodes.indices {
                let target: Float = up && i == self.selected ? self.gains[i] : 0
                self.nodes[i].volume = initial[i] + Float(fraction) * (target - initial[i])
            }
            if fraction >= 1 { timer.invalidate(); completion?() }
        }
    }
    func play() {
        guard !files.isEmpty, !playing, !pausing else { return }
        if startFrame >= files[0].length { startFrame = 0 }
        transition &+= 1
        hostStart = mach_absolute_time() + AVAudioTime.hostTime(forSeconds: 0.15)
        let when = AVAudioTime(hostTime: hostStart)
        for i in nodes.indices {
            nodes[i].volume = 0
            let count = AVAudioFrameCount(files[i].length - startFrame)
            nodes[i].scheduleSegment(files[i], startingFrame: startFrame, frameCount: count, at: nil)
            nodes[i].play(at: when)
        }
        playing = true
        ramp(up: true)
    }
    func pause() {
        guard playing, !pausing else { return }
        pausing = true
        let position = self.position
        transition &+= 1; let current = transition
        ramp(up: false) { [weak self] in
            guard let self = self, self.transition == current else { return }
            self.nodes.forEach { $0.stop() }; self.playing = false; self.pausing = false
            self.startFrame = AVAudioFramePosition(position * 48000)
        }
    }
    func seek(_ seconds: Double) {
        let resume = playing
        transition &+= 1; let current = transition
        ramp(up: false) { [weak self] in
            guard let self = self, self.transition == current else { return }
            self.nodes.forEach { $0.stop() }; self.playing = false; self.pausing = false
            self.startFrame = AVAudioFramePosition(max(0, min(self.duration, seconds)) * 48000)
            if resume { self.play() }
        }
    }
    func stopImmediately() {
        transition &+= 1; timer?.invalidate(); nodes.forEach { $0.stop() }
        engine.stop(); playing = false; pausing = false; startFrame = 0
    }
    deinit { timer?.invalidate() }
}

final class FlippedView: NSView { override var isFlipped: Bool { true } }

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private let paths = (0..<3).map { _ in NSTextField(labelWithString: "Keine Datei gewählt") }
    private let imports = (0..<3).map { _ in NSButton() }
    private var urls: [URL?] = [nil, nil, nil]
    private var metadata: [SourceMetadata?] = [nil, nil, nil]
    private let aligned = NSButton(checkboxWithTitle: "Vocal und Beat wurden mit demselben Songstart exportiert.", target: nil, action: nil)
    private let run = NSButton(title: "AUTO MIX & MASTER", target: nil, action: nil)
    private let cancel = NSButton(title: "Abbrechen", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "Experimentelle V1 · Lokal · Keine nachgewiesene professionelle Klangqualität")
    private let progress = NSProgressIndicator()
    private let choice = NSSegmentedControl(labels: ["Original", "A · Reference", "B · Warm & Full", "C · Punch & Clear"], trackingMode: .selectOne, target: nil, action: nil)
    private let play = NSButton(title: "Wiedergabe / Pause", target: nil, action: nil)
    private let seek = NSSlider(value: 0, minValue: 0, maxValue: 1, target: nil, action: nil)
    private let time = NSTextField(labelWithString: "0:00 / 0:00")
    private let mono = NSButton(checkboxWithTitle: "Monosumme anhören", target: nil, action: nil)
    private let export = NSButton(title: "Gewählten Master als WAV exportieren …", target: nil, action: nil)
    private let folder = NSButton(title: "Projekt & Messbericht im Finder", target: nil, action: nil)
    private let reference = NSButton(title: "Referenz separat anhören", target: nil, action: nil)
    private let details = NSTextField(wrappingLabelWithString: "Noch keine Ergebnisse. WAV-Export: 48 kHz / 24 Bit PCM. Quellen bleiben unverändert.")
    private var job: ToolJob?
    private var busy = false
    private var audioReady = false
    private let player = ComparisonPlayer()
    private var referencePlayer: ComparisonPlayer?
    private var referenceGeneration: UInt64 = 0
    private var session: Session?
    private var project: URL?
    private var tick: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let menu = NSMenu(), item = NSMenuItem(), appMenu = NSMenu()
        menu.addItem(item); item.submenu = appMenu
        appMenu.addItem(withTitle: "DILIYAN beenden", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        NSApp.mainMenu = menu
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 760),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "DILIYAN STEM MASTER ENGINE · V1 Experimental"
        window.minSize = NSSize(width: 850, height: 730); window.delegate = self
        let scroll = NSScrollView(); scroll.hasVerticalScroller = true; scroll.translatesAutoresizingMaskIntoConstraints = false
        window.contentView!.addSubview(scroll)
        let document = FlippedView(); document.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = document
        NSLayoutConstraint.activate([
            scroll.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor),
            scroll.topAnchor.constraint(equalTo: window.contentView!.topAnchor),
            scroll.bottomAnchor.constraint(equalTo: window.contentView!.bottomAnchor),
            document.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            document.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            document.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor)
        ])
        let stack = NSStackView(); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false
        document.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: document.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: document.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: document.topAnchor, constant: 22),
            stack.bottomAnchor.constraint(equalTo: document.bottomAnchor, constant: -24)
        ])
        let title = NSTextField(labelWithString: "DILIYAN STEM MASTER ENGINE")
        title.font = .boldSystemFont(ofSize: 25); stack.addArrangedSubview(title)
        let names = ["VOCAL.wav auswählen …", "BEAT.wav auswählen …", "REFERENCE WAV / FLAC …"]
        for i in 0..<3 {
            imports[i].title = names[i]; imports[i].tag = i; imports[i].target = self; imports[i].action = #selector(importFile(_:))
            imports[i].widthAnchor.constraint(equalToConstant: 235).isActive = true
            paths[i].lineBreakMode = .byTruncatingMiddle
            let row = NSStackView(views: [imports[i], paths[i]]); row.spacing = 12
            stack.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        }
        aligned.target = self; aligned.action = #selector(updateControls)
        stack.addArrangedSubview(aligned)
        let warning = NSTextField(wrappingLabelWithString: "Keine automatische Synchronisierung. Unterschiedliche Längen werden mit Stille ergänzt. Mono wird auf beide Kanäle kopiert; Verarbeitung und Export erfolgen mit 48 kHz.")
        warning.font = .systemFont(ofSize: 11); stack.addArrangedSubview(warning)
        run.target = self; run.action = #selector(render); run.bezelStyle = .rounded
        run.font = .boldSystemFont(ofSize: 18)
        cancel.target = self; cancel.action = #selector(cancelJob)
        stack.addArrangedSubview(NSStackView(views: [run, cancel]))
        progress.isIndeterminate = false; progress.minValue = 0; progress.maxValue = 1
        stack.addArrangedSubview(progress); progress.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        stack.addArrangedSubview(status); status.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        choice.selectedSegment = 1; choice.target = self; choice.action = #selector(selectVersion)
        stack.addArrangedSubview(choice)
        play.target = self; play.action = #selector(togglePlay)
        mono.target = self; mono.action = #selector(toggleMono)
        stack.addArrangedSubview(NSStackView(views: [play, reference, time]))
        reference.target = self; reference.action = #selector(playReference)
        seek.target = self; seek.action = #selector(seekAudio)
        seek.isContinuous = false
        stack.addArrangedSubview(seek); seek.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        stack.addArrangedSubview(mono)
        stack.addArrangedSubview(details); details.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        export.target = self; export.action = #selector(exportWav)
        folder.target = self; folder.action = #selector(showProject)
        stack.addArrangedSubview(NSStackView(views: [export, folder]))
        updateControls(); window.center(); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
        tick = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in self?.updateTransport() }
        if CommandLine.arguments.contains("--smoke-test") {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
                guard self.window.isVisible, self.imports.count == 3, !self.run.isEnabled else { exit(1) }
                guard let argument = CommandLine.arguments.last, argument != "--smoke-test" else { exit(1) }
                let task = ToolJob(); self.job = task
                task.execute(["--inspect", argument], progress: { _ in }) { result in
                    guard let data = try? result.get(), let info = try? JSONDecoder().decode(SourceMetadata.self, from: data), info.channels == 2 else { exit(1) }
                    print("PASS: native UI visible, render guarded, bundled engine executed and metadata decoded.")
                    NSApp.terminate(nil)
                }
            }
        }
    }
    private func alert(_ text: String) { let a = NSAlert(); a.messageText = "DILIYAN"; a.informativeText = text; a.runModal() }
    @objc private func updateControls() {
        imports.forEach { $0.isEnabled = !busy }; aligned.isEnabled = !busy
        run.isEnabled = !busy && urls.allSatisfy { $0 != nil } && metadata.allSatisfy { $0 != nil } && aligned.state == .on
        cancel.isEnabled = busy
        let ready = !busy && session != nil
        choice.isEnabled = ready; play.isEnabled = ready && audioReady; seek.isEnabled = ready && audioReady
        export.isEnabled = ready && choice.selectedSegment > 0; folder.isEnabled = ready
        reference.isEnabled = ready; mono.isEnabled = ready && audioReady
    }
    @objc private func importFile(_ sender: NSButton) {
        let panel = NSOpenPanel(); panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
        panel.allowedContentTypes = sender.tag == 2 ? [UTType.wav, UTType(filenameExtension: "flac")!]:[UTType.wav]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let index = sender.tag
        let canonical = url.resolvingSymlinksInPath().standardizedFileURL
        if urls.enumerated().contains(where: { $0.offset != index && $0.element?.resolvingSymlinksInPath().standardizedFileURL == canonical }) {
            alert("Dieselbe Datei darf nicht mehreren Rollen zugeordnet werden."); return
        }
        invalidateResults()
        aligned.state = .off; busy = true; updateControls(); status.stringValue = "Prüfe Format: \(url.lastPathComponent)"
        let task = ToolJob(); job = task
        task.execute(["--inspect", url.path], progress: { _ in }) { [weak self] result in
            guard let self = self else { return }; self.busy = false; self.job = nil
            do {
                let data = try result.get(), info = try JSONDecoder().decode(SourceMetadata.self, from: data)
                self.urls[index] = url; self.metadata[index] = info
                self.paths[index].stringValue = String(format: "%@ · %.0f Hz · %d Kanal · %.2f s", url.lastPathComponent, info.sampleRate, info.channels, info.seconds)
                self.paths[index].toolTip = url.path
                self.status.stringValue = "Format geprüft. Bitte Zuordnung und gemeinsamen Stem-Start kontrollieren."
            } catch { self.urls[index] = nil; self.metadata[index] = nil; self.paths[index].stringValue = "Import fehlgeschlagen"; self.alert(error.localizedDescription) }
            self.updateControls()
        }
    }
    private func invalidateResults() {
        referenceGeneration &+= 1
        player.stopImmediately(); referencePlayer?.stopImmediately(); referencePlayer = nil
        session = nil; project = nil; progress.doubleValue = 0
        audioReady = false
        details.stringValue = "Noch keine aktuellen Ergebnisse. Frühere Projektdateien bleiben erhalten."
    }
    @objc private func render() {
        guard run.isEnabled else { return }
        let panel = NSOpenPanel(); panel.canChooseDirectories = true; panel.canChooseFiles = false
        panel.canCreateDirectories = true; panel.message = "Ordner für ein neues lokales Projekt wählen. Audiodateien niemals in das öffentliche GitHub-Repository hochladen."
        guard panel.runModal() == .OK, let parent = panel.url else { return }
        let out = parent.appendingPathComponent("DILIYAN-" + UUID().uuidString)
        invalidateResults(); busy = true; updateControls()
        let task = ToolJob(); job = task
        let arguments = ["--render", urls[0]!.path, urls[1]!.path, urls[2]!.path, out.path]
        task.execute(arguments, progress: { [weak self] obj in
            if let p = obj["progress"] as? Double { self?.progress.doubleValue = p }
            if let message = obj["message"] as? String { self?.status.stringValue = message }
        }) { [weak self] result in
            guard let self = self else { return }; self.busy = false; self.job = nil
            do {
                _ = try result.get()
                let data = try Data(contentsOf: out.appendingPathComponent("Session.json"))
                let session = try JSONDecoder().decode(Session.self, from: data)
                guard session.masters.count == 3, session.sources.count == 3 else { throw NSError(domain: "Diliyan", code: 4) }
                var files = [out.appendingPathComponent("Original.wav")]
                files += session.masters.map { out.appendingPathComponent($0.file) }
                files += [out.appendingPathComponent("Mono-Original.wav")]
                files += session.masters.map { out.appendingPathComponent("Mono-" + $0.file) }
                let metrics = [session.original] + session.masters.map { $0.metrics }
                let levels = metrics.map { $0.lufs > -100 ? session.comparisonLUFS - $0.lufs : -120 }
                self.session = session; self.project = out
                do {
                    try self.player.load(urls: files, levels: levels + levels)
                    self.audioReady = true
                } catch {
                    self.audioReady = false
                    self.alert("Render und WAV-Export sind fertig, aber die Audiowiedergabe konnte nicht gestartet werden: " + error.localizedDescription)
                }
                self.choice.selectedSegment = 1; self.player.select(1); self.mono.state = .off
                self.seek.maxValue = self.player.duration; self.seek.doubleValue = 0
                self.status.stringValue = "Drei WAV-Master gerendert. TP = 8×-Schätzung, LUFS noch unabhängig zu validieren."
                self.updateDetails()
            } catch {
                self.invalidateResults(); self.status.stringValue = "Kein freigegebenes Ergebnis. Fehler: \(error.localizedDescription)"
                self.alert(error.localizedDescription + "\n\nEin eventuell unvollständiger Projektordner liegt unter:\n" + out.path)
            }
            self.updateControls()
        }
    }
    @objc private func cancelJob() { job?.cancel(); status.stringValue = "Verarbeitung wird abgebrochen …" }
    @objc private func selectVersion() {
        referenceGeneration &+= 1
        referencePlayer?.pause()
        player.select(choice.selectedSegment + (mono.state == .on ? 4 : 0))
        updateDetails(); updateControls()
    }
    private func updateDetails() {
        guard let s = session else { return }
        let index = choice.selectedSegment
        let m = index == 0 ? s.original : s.masters[index - 1].metrics
        let number: (Double) -> String = { $0 < -100 ? "nicht messbar" : String(format: "%.2f", $0) }
        var text = "LUFS-I: \(number(m.lufs)) · Short-term max: \(number(m.shortMax)) LUFS · TP≈ \(number(m.truePeakEstimate)) dBTP\n"
        text += "Referenz: \(number(s.sources[2].metrics.lufs)) LUFS-I · TP≈ \(number(s.sources[2].metrics.truePeakEstimate)) dBTP\n"
        text += String(format: "Korrelation: %.3f · Breitenindex: %.3f · Vergleichsziel: %.2f LUFS\nVocal-Pegel: %+.2f dB · Beat-Präsenz-EQ: %+.2f dB", m.correlation, m.width, s.comparisonLUFS, s.vocalGainDB, s.beatPresenceDB)
        if index > 0 {
            let p = s.masters[index - 1]
            text += String(format: "\nMaster EQ: Bass %+.2f / Präsenz %+.2f / Höhen %+.2f dB · Limiter max. %.2f dB", p.lowDB, p.presenceDB, p.highDB, p.maxLimiterReductionDB)
        }
        text += "\nMono/Phasenprobleme werden gemeldet, nicht automatisch repariert. Details: Session.json."
        text += String(format: "\nErster Pegel > −60 dBFS: Vocal %.2f s / Beat %.2f s. Unterschied kann musikalisch sein.", s.sources[0].metrics.leadingSilenceSeconds, s.sources[1].metrics.leadingSilenceSeconds)
        let inputWarnings = s.warnings.filter { $0.contains("Längen") || $0.contains("Pegelereignisse") || $0.contains("Stereokorrelation") || $0.contains("0 dBFS") }
        if !inputWarnings.isEmpty { text += "\nHinweise: " + inputWarnings.joined(separator: " ") }
        details.stringValue = text
    }
    @objc private func togglePlay() {
        referenceGeneration &+= 1
        referencePlayer?.pause()
        if player.playing { player.pause() } else { player.play() }
    }
    @objc private func seekAudio() {
        referenceGeneration &+= 1
        referencePlayer?.pause(); player.seek(seek.doubleValue)
    }
    @objc private func toggleMono() { player.select(choice.selectedSegment + (mono.state == .on ? 4 : 0)) }
    @objc private func playReference() {
        guard let out = project, let s = session else { return }
        referenceGeneration &+= 1; let generation = referenceGeneration
        if let ref = referencePlayer, ref.playing { ref.pause(); return }
        player.pause()
        do {
            let ref: ComparisonPlayer
            if let existing = referencePlayer { ref = existing }
            else {
                ref = ComparisonPlayer()
                try ref.load(urls: [out.appendingPathComponent("reference-48k.wav")],
                             levels: [s.referenceComparisonLUFS - s.sources[2].metrics.lufs])
                ref.select(0); referencePlayer = ref
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) { [weak self, weak ref] in
                guard let self = self, let ref = ref, self.referencePlayer === ref, self.referenceGeneration == generation else { return }
                ref.play()
            }
        } catch { alert(error.localizedDescription) }
    }
    private func updateTransport() {
        let p = player.position
        seek.doubleValue = p
        func stamp(_ value: Double) -> String { String(format: "%d:%02d", Int(value)/60, Int(value)%60) }
        time.stringValue = "\(stamp(p)) / \(stamp(player.duration))"
        if player.playing && p >= player.duration { player.pause() }
    }
    @objc private func exportWav() {
        guard let out = project, let s = session, choice.selectedSegment > 0 else { return }
        let file = s.masters[choice.selectedSegment - 1].file
        let panel = NSSavePanel(); panel.allowedContentTypes = [.wav]; panel.nameFieldStringValue = "DILIYAN-" + file
        guard panel.runModal() == .OK, let dest = panel.url else { return }
        let canonical = dest.resolvingSymlinksInPath().standardizedFileURL
        if urls.compactMap({ $0 }).contains(where: { $0.resolvingSymlinksInPath().standardizedFileURL == canonical }) || canonical.path.hasPrefix(out.path + "/") {
            alert("Quellen und interne Projektdateien dürfen nicht überschrieben werden."); return
        }
        do {
            // Atomic write avoids a partial export on replacement failure.
            let staged = dest.deletingLastPathComponent().appendingPathComponent(".diliyan-" + UUID().uuidString + ".wav")
            defer { try? FileManager.default.removeItem(at: staged) }
            try FileManager.default.copyItem(at: out.appendingPathComponent(file), to: staged)
            if FileManager.default.fileExists(atPath: dest.path) {
                _ = try FileManager.default.replaceItemAt(dest, withItemAt: staged)
            } else { try FileManager.default.moveItem(at: staged, to: dest) }
            status.stringValue = "Exportiert: \(dest.lastPathComponent) · 48 kHz / 24 Bit PCM"
        } catch { alert(error.localizedDescription) }
    }
    @objc private func showProject() { if let p = project { NSWorkspace.shared.open(p) } }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        job?.cancel(); player.stopImmediately(); referencePlayer?.stopImmediately(); return .terminateNow
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
