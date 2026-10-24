import AppKit
import AVFoundation
import UniformTypeIdentifiers

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
                guard let executable = Bundle.main.executableURL?.deletingLastPathComponent().appendingPathComponent("DiliyanEngineV2"),
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

struct Info: Decodable { let sampleRate: Double; let channels: Int; let seconds: Double }
struct Metric: Decodable {
 let frames: Int64; let seconds: Double; let lufs: Double; let shortMax: Double
 let truePeakEstimate: Double; let correlation: Double; let width: Double
 let bands: [Double]
}
struct Entry: Decodable { let file: String; let metrics: Metric; let matchedGainDB: Double }
struct Unmask: Decodable { let centers: [Double]; let maximum: [Double]; let mean: [Double]; let activeFraction: Double; let trace: [[Double]] }
struct MixReport: Decodable {
 let vocalGainDB: Double; let strength: Double; let sourcePaths: [String]; let sourceInfos: [Info]
 let files: [Entry]; let comparisonLUFS: Double; let originalMonitorTrimDB: Double; let unmask: Unmask; let warnings: [String]
}
struct Master: Decodable {
 let file: String; let metrics: Metric; let eqDB: [Double]; let sideEQ: [Double]; let ratio: Double
 let targetLUFS: Double; let compressorMaxDB: Double; let limiterMaxDB: Double
 let limiterMeanDB: Double; let driveDB: Double; let safetyDB: Double; let reason: String; let decision: String
}
struct Similarity: Decodable { let a: Int; let b: Int; let residualDB: Double; let nearIdentical: Bool }
struct MasterReport: Decodable {
 let selectedMix: String; let files: [Entry]; let masters: [Master]; let similarities: [Similarity]
 let comparisonLUFS: Double; let originalMonitorTrimDB: Double; let warnings: [String]
}

final class Monitor {
 private let engine = AVAudioEngine()
 private var source: AVAudioSourceNode?
 private var handle: UnsafeMutableRawPointer?
 private(set) var playing = false
 var selected = 0
 var position: Double { ds_position(handle) }
 var duration: Double { ds_duration(handle, Int32(selected)) }
 var underruns: Int64 { ds_underruns(handle) }
 func load(_ paths: [URL], matched: [Double], original: [Double], offline: Bool = false) throws {
  stop()
  if let old = source { engine.detach(old) }; source = nil
  if let old = handle { ds_destroy(old) }; handle = nil
  let strings = paths.map { strdup($0.path)! }; defer { strings.forEach { free($0) } }
  let pointers: [UnsafePointer<CChar>?] = strings.map { UnsafePointer($0) }
  let pointer = pointers.withUnsafeBufferPointer { ps in matched.withUnsafeBufferPointer { m in original.withUnsafeBufferPointer { o in
   ds_create(ps.baseAddress, m.baseAddress, o.baseAddress, Int32(paths.count))
  } } }
  guard let context = pointer else { throw NSError(domain: "Diliyan", code: 10, userInfo: [NSLocalizedDescriptionKey: String(cString: ds_error())]) }
  handle = context; selected = 0
  let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
  let node = AVAudioSourceNode(format: format) { silence, _, count, list in
   let buffers = UnsafeMutableAudioBufferListPointer(list)
   guard buffers.count == 2, let a = buffers[0].mData, let b = buffers[1].mData else { return -50 }
   ds_render(context, a.assumingMemoryBound(to: Float.self), b.assumingMemoryBound(to: Float.self), Int32(count))
   silence.pointee = false
   return noErr
  }
  source = node; engine.attach(node); engine.connect(node, to: engine.mainMixerNode, format: format)
  if offline { try engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 1024) }
  engine.prepare(); try engine.start()
 }
 func select(_ index: Int) { selected = index; ds_select(handle, Int32(index)) }
 func playPause() { playing.toggle(); ds_play(handle, playing ? 1 : 0) }
 func pause() { playing = false; ds_play(handle, 0) }
 func seek(_ seconds: Double) { ds_seek(handle, max(0, min(duration, seconds))) }
 func mode(_ original: Bool) { ds_mode(handle, original ? 1 : 0) }
 func mono(_ enabled: Bool) { ds_mono(handle, enabled ? 1 : 0) }
 func stop() { engine.stop(); playing = false }
 func nativeSmoke(_ path: URL) throws {
  try load([path], matched: [0.5], original: [1], offline: true)
  select(0); playPause(); Thread.sleep(forTimeInterval: 0.05)
  let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 1024)!
  var peak: Float = 0
  for _ in 0..<150 {
   let result = try engine.renderOffline(1024, to: buffer)
   if result == .success, let data = buffer.floatChannelData {
    for i in 0..<Int(buffer.frameLength) { guard data[0][i].isFinite else { throw NSError(domain: "Diliyan", code: 11) }; peak = max(peak, abs(data[0][i])) }
   }
   Thread.sleep(forTimeInterval: 0.001)
  }
  guard peak > 0.001, position > 0.5 else { throw NSError(domain: "Diliyan", code: 12, userInfo: [NSLocalizedDescriptionKey: "Native source-node playback test produced no audio."]) }
  stop(); engine.disableManualRenderingMode()
 }
 deinit { engine.stop(); if let h = handle { ds_destroy(h) } }
}

final class TraceView: NSView {
 var trace: [[Double]] = [] { didSet { needsDisplay = true } }
 var cursor: Double = 0 { didSet { needsDisplay = true } }
 override var isFlipped: Bool { true }
 override func draw(_ dirtyRect: NSRect) {
  NSColor.controlBackgroundColor.setFill(); bounds.fill()
  let text = "Gemessene EQ-Steuerhüllkurven · 800 Hz / 2 kHz / 4 kHz · 0 bis −2,5 dB"
  text.draw(at: NSPoint(x: 10, y: 5), withAttributes: [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor])
  guard let end = trace.last?.first, end > 0 else { return }
  let colors: [NSColor] = [.systemBlue, .systemOrange, .systemGreen]
  for k in 1...3 {
   let path = NSBezierPath(); var began = false
   for row in trace where row.count == 4 {
    let point = NSPoint(x: 10 + row[0] / end * (bounds.width - 20), y: 28 + row[k] / 2.5 * (bounds.height - 38))
    if began { path.line(to: point) } else { path.move(to: point); began = true }
   }
   colors[k - 1].setStroke(); path.lineWidth = 1.5; path.stroke()
  }
  NSColor.labelColor.withAlphaComponent(0.5).setStroke()
  let line = NSBezierPath(); let x = 10 + min(end, cursor) / end * (bounds.width - 20)
  line.move(to: NSPoint(x: x, y: 25)); line.line(to: NSPoint(x: x, y: bounds.height - 5)); line.stroke()
 }
}
final class FlippedView: NSView { override var isFlipped: Bool { true } }
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
 private var window: NSWindow!
 private var urls: [URL?] = [nil, nil, nil]
 private let labels = (0..<3).map { _ in NSTextField(labelWithString: "Keine Datei") }
 private let imports = (0..<3).map { _ in NSButton() }
 private let workflow = NSSegmentedControl(labels: ["AUTO", "REVIEW"], trackingMode: .selectOne, target: nil, action: nil)
 private let aligned = NSButton(checkboxWithTitle: "Vocal und Beat haben denselben Exportstart.", target: nil, action: nil)
 private let unmaskOn = NSButton(checkboxWithTitle: "Dynamisches Vocal-Unmasking verwenden", target: nil, action: nil)
 private let strength = NSSlider(value: 0.7, minValue: 0, maxValue: 1, target: nil, action: nil)
 private let strengthLabel = NSTextField(labelWithString: "Unmasking-Stärke: 70 %")
 private let run = NSButton(title: "AUTO STEM MIX & GLUE", target: nil, action: nil)
 private let masterButton = NSButton(title: "Mix bestätigen → REFERENCE MASTERING", target: nil, action: nil)
 private let cancel = NSButton(title: "Abbrechen", target: nil, action: nil)
 private let status = NSTextField(wrappingLabelWithString: "V2 Experimental · vollständig lokal · musikalische Qualität noch zu validieren")
 private let progress = NSProgressIndicator()
 private var choices = NSSegmentedControl(labels: ["Original", "Balance", "Mix & Glue"], trackingMode: .selectOne, target: nil, action: nil)
 private let levels = NSSegmentedControl(labels: ["Loudness Matched", "Original Level"], trackingMode: .selectOne, target: nil, action: nil)
 private let mono = NSButton(checkboxWithTitle: "Monosumme", target: nil, action: nil)
 private let play = NSButton(title: "▶ / Pause", target: nil, action: nil)
 private let seek = NSSlider(value: 0, minValue: 0, maxValue: 1, target: nil, action: nil)
 private let clock = NSTextField(labelWithString: "0:00 / 0:00")
 private let info = NSTextField(wrappingLabelWithString: "Noch keine Ergebnisse.")
 private let graph = TraceView()
 private let export = NSButton(title: "Master als 24-Bit-WAV exportieren …", target: nil, action: nil)
 private let folder = NSButton(title: "Projekt & Messberichte", target: nil, action: nil)
 private let monitor = Monitor()
 private var mix: MixReport?
 private var masters: MasterReport?
 private var project: URL?
 private var masterFolder: URL?
 private var job: ToolJob?
 private var busy = false
 private var audioReady = false
 private var changedStrength = false
 private var entries: [Entry] = []
 private var tick: Timer?
 private var autoPending = false
 func applicationDidFinishLaunching(_ notification: Notification) {
  let menu = NSMenu(), item = NSMenuItem(), submenu = NSMenu(); menu.addItem(item); item.submenu = submenu
  submenu.addItem(withTitle: "DILIYAN V2 beenden", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"); NSApp.mainMenu = menu
  window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1040, height: 830), styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false)
  window.title = "DILIYAN STEM MASTER ENGINE V2 · Experimental"; window.minSize = NSSize(width: 920, height: 700); window.delegate = self
  let scroll = NSScrollView(); scroll.hasVerticalScroller = true; scroll.translatesAutoresizingMaskIntoConstraints = false; window.contentView!.addSubview(scroll)
  let doc = FlippedView(); doc.translatesAutoresizingMaskIntoConstraints = false; scroll.documentView = doc
  let stack = NSStackView(); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 12; stack.translatesAutoresizingMaskIntoConstraints = false; doc.addSubview(stack)
  NSLayoutConstraint.activate([
   scroll.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor), scroll.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor), scroll.topAnchor.constraint(equalTo: window.contentView!.topAnchor), scroll.bottomAnchor.constraint(equalTo: window.contentView!.bottomAnchor),
   doc.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor), doc.topAnchor.constraint(equalTo: scroll.contentView.topAnchor), doc.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
   stack.leadingAnchor.constraint(equalTo: doc.leadingAnchor, constant: 22), stack.trailingAnchor.constraint(equalTo: doc.trailingAnchor, constant: -22), stack.topAnchor.constraint(equalTo: doc.topAnchor, constant: 20), stack.bottomAnchor.constraint(equalTo: doc.bottomAnchor, constant: -22)])
  func add(_ view: NSView, full: Bool = false) { stack.addArrangedSubview(view); if full { view.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true } }
  let title = NSTextField(labelWithString: "DILIYAN · STEM MASTER ENGINE V2"); title.font = .boldSystemFont(ofSize: 25); add(title)
  workflow.selectedSegment = 0; workflow.target = self; workflow.action = #selector(updateControls); add(workflow)
  for i in 0..<3 {
   imports[i].title = ["Vocal WAV auswählen …", "Beat WAV auswählen …", "Referenz WAV / FLAC …"][i]; imports[i].tag = i; imports[i].target = self; imports[i].action = #selector(importAudio(_:)); imports[i].widthAnchor.constraint(equalToConstant: 225).isActive = true
   labels[i].lineBreakMode = .byTruncatingMiddle
   let row = NSStackView(views: [imports[i], labels[i]]); row.spacing = 10; add(row, full: true)
  }
  aligned.target = self; aligned.action = #selector(updateControls); add(aligned)
  unmaskOn.state = .on; unmaskOn.target = self; unmaskOn.action = #selector(toggleUnmask); add(unmaskOn)
  strength.target = self; strength.action = #selector(changeStrength); strength.isContinuous = false
  let row = NSStackView(views: [strengthLabel, strength]); strength.widthAnchor.constraint(equalToConstant: 220).isActive = true; add(row)
  run.target = self; run.action = #selector(startMix); masterButton.target = self; masterButton.action = #selector(startMaster); cancel.target = self; cancel.action = #selector(cancelJob)
  add(NSStackView(views: [run, masterButton, cancel]))
  progress.isIndeterminate = false; progress.minValue = 0; progress.maxValue = 1; add(progress, full: true); add(status, full: true)
  choices.selectedSegment = 2; choices.target = self; choices.action = #selector(selectAudio); add(choices, full: true)
  levels.selectedSegment = 0; levels.target = self; levels.action = #selector(levelMode)
  mono.target = self; mono.action = #selector(monoMode); play.target = self; play.action = #selector(togglePlay)
  add(NSStackView(views: [play, levels, mono, clock]))
  seek.target = self; seek.action = #selector(seekAudio); seek.isContinuous = false; add(seek, full: true)
  graph.heightAnchor.constraint(equalToConstant: 105).isActive = true; add(graph, full: true); add(info, full: true)
  export.target = self; export.action = #selector(exportAudio); folder.target = self; folder.action = #selector(showFolder); add(NSStackView(views: [export, folder]))
  updateControls(); window.center(); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
  tick = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in self?.transport() }
  if CommandLine.arguments.contains("--smoke-test") {
   DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
    guard self.window.isVisible, let path = CommandLine.arguments.last, path != "--smoke-test" else { exit(1) }
    let task = ToolJob(); self.job = task
    task.execute(["--inspect", path], progress: { _ in }) { result in
     do { _ = try result.get(); try self.monitor.nativeSmoke(URL(fileURLWithPath: path)); print("PASS V2 native UI, bundled decoder and offline AVAudioSourceNode playback"); NSApp.terminate(nil) }
     catch { fputs(error.localizedDescription + "\n", stderr); exit(1) }
    }
   }
  }
 }
 private func alert(_ text: String) { let a = NSAlert(); a.messageText = "DILIYAN V2"; a.informativeText = text; a.runModal() }
 @objc private func updateControls() {
  imports.forEach { $0.isEnabled = !busy }; aligned.isEnabled = !busy; unmaskOn.isEnabled = !busy; strength.isEnabled = !busy; workflow.isEnabled = !busy
  run.isEnabled = !busy && urls[0] != nil && urls[1] != nil && aligned.state == .on && (workflow.selectedSegment == 1 || urls[2] != nil)
  masterButton.isEnabled = !busy && mix != nil && urls[2] != nil && !changedStrength
  cancel.isEnabled = busy; choices.isEnabled = !busy && audioReady; levels.isEnabled = !busy && audioReady; mono.isEnabled = !busy && audioReady; play.isEnabled = !busy && audioReady; seek.isEnabled = !busy && audioReady
  export.isEnabled = !busy && masters != nil && (3...5).contains(choices.selectedSegment); folder.isEnabled = project != nil
 }
 private func clear() { monitor.stop(); mix = nil; masters = nil; project = nil; masterFolder = nil; entries = []; audioReady = false; graph.trace = []; info.stringValue = "Noch keine aktuellen Ergebnisse. Frühere Projektdateien bleiben erhalten."; choices.segmentCount = 3; for (i,s) in ["Original", "Balance", "Mix & Glue"].enumerated() { choices.setLabel(s, forSegment: i) }; choices.selectedSegment = 2 }
 @objc private func importAudio(_ sender: NSButton) {
  let panel = NSOpenPanel(); panel.allowedContentTypes = sender.tag == 2 ? [.wav, UTType(filenameExtension: "flac")!] : [.wav]
  panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
  guard panel.runModal() == .OK, let url = panel.url else { return }
  let index = sender.tag
  if urls.enumerated().contains(where: { $0.offset != index && $0.element?.resolvingSymlinksInPath() == url.resolvingSymlinksInPath() }) { alert("Diese Datei hat bereits eine andere Rolle."); return }
  if index != 2 { clear(); aligned.state = .off } else {
   masters = nil; masterFolder = nil; monitor.stop(); audioReady = false
   choices.segmentCount = 3
   for (i,s) in ["Original", "Balance", "Mix & Glue"].enumerated() { choices.setLabel(s, forSegment: i) }
   choices.selectedSegment = 2
   if let report = mix, let root = project { loadEntries(report.files, root: root, monitorTrim: report.originalMonitorTrimDB); selectAudio() }
  }
  busy = true; updateControls(); let task = ToolJob(); job = task
  task.execute(["--inspect", url.path], progress: { _ in }) { [weak self] result in
   guard let self = self else { return }; self.busy = false; self.job = nil
   do { let data = try result.get(), m = try JSONDecoder().decode(Info.self, from: data); self.urls[index] = url
    self.labels[index].stringValue = String(format: "%@ · %.0f Hz · %d Kanal · %.2f s", url.lastPathComponent, m.sampleRate, m.channels, m.seconds); self.labels[index].toolTip = url.path
   } catch { self.urls[index] = nil; self.labels[index].stringValue = "Import fehlgeschlagen"; self.alert(error.localizedDescription) }
   self.updateControls()
  }
 }
 @objc private func changeStrength() {
  strengthLabel.stringValue = "Unmasking-Stärke: \(Int(strength.doubleValue * 100)) %"
  if mix != nil { changedStrength = true; status.stringValue = "Stärke geändert: Phase 1 erneut berechnen, bevor Mastering möglich ist." }; updateControls()
 }
 @objc private func toggleUnmask() {
  if mix != nil { choices.selectedSegment = unmaskOn.state == .on ? 2 : 1; selectAudio() }; updateControls()
 }
 @objc private func startMix() {
  guard run.isEnabled else { return }
  let panel = NSOpenPanel(); panel.canChooseFiles = false; panel.canChooseDirectories = true; panel.canCreateDirectories = true; panel.message = "Lokalen Projektordner wählen. Quellen bleiben unverändert."
  guard panel.runModal() == .OK, let parent = panel.url else { return }
  autoPending = workflow.selectedSegment == 0; let automatic = autoPending
  clear(); let out = parent.appendingPathComponent("DILIYAN-V2-" + UUID().uuidString); project = out; changedStrength = false
  let args = ["--mix", urls[0]!.path, urls[1]!.path, out.path, String(strength.doubleValue)]
  execute(args) { [weak self] in
   guard let self = self else { return }
   do { let report = try JSONDecoder().decode(MixReport.self, from: Data(contentsOf: out.appendingPathComponent("Mix.json"))); self.mix = report; self.graph.trace = report.unmask.trace
    self.loadEntries(report.files, root: out, monitorTrim: report.originalMonitorTrimDB)
    self.choices.selectedSegment = self.unmaskOn.state == .on ? 2 : 1; self.selectAudio()
    self.status.stringValue = "Phase 1 fertig. Original, Balance und Mix & Glue vergleichen."
    self.updateControls(); if automatic { self.startMaster() }
   } catch { self.alert(error.localizedDescription) }
  }
 }
 private func execute(_ args: [String], success: @escaping () -> Void) {
  monitor.pause(); busy = true; progress.doubleValue = 0; updateControls()
  let task = ToolJob(); job = task
  task.execute(args, progress: { [weak self] obj in
   if let p = obj["progress"] as? Double { self?.progress.doubleValue = p }; if let s = obj["message"] as? String { self?.status.stringValue = s }
  }) { [weak self] result in
   guard let self = self else { return }; self.busy = false; self.job = nil
   do { _ = try result.get(); success() } catch { self.status.stringValue = "Kein neues Ergebnis freigegeben: " + error.localizedDescription; self.alert(error.localizedDescription) }
   self.updateControls()
  }
 }
 @objc private func startMaster() {
  guard let root = project, mix != nil, let ref = urls[2], !changedStrength, !busy else { return }
  let index = choices.selectedSegment > 2 ? 2 : max(0, choices.selectedSegment)
  let selected = ["Original", "Balance", "MixGlue"][index == 2 && unmaskOn.state != .on ? 1 : index]
  let out = root.appendingPathComponent("Master-" + UUID().uuidString)
  execute(["--master", root.path, ref.path, selected, out.path]) { [weak self] in
   guard let self = self else { return }
   do { let report = try JSONDecoder().decode(MasterReport.self, from: Data(contentsOf: out.appendingPathComponent("Master.json"))); self.masters = report; self.masterFolder = out
    self.choices.segmentCount = 7; for (i,s) in ["Original", "Balance", "Mix & Glue", "A · Reference", "B · Warm", "C · Punch", "Referenz"].enumerated() { self.choices.setLabel(s, forSegment: i) }
    self.loadEntries(report.files, root: out, monitorTrim: report.originalMonitorTrimDB); self.choices.selectedSegment = 3; self.selectAudio()
    self.status.stringValue = "V2 fertig · 3 PCM24-Master · verwendeter Mix: \(report.selectedMix) · Klangqualität bitte prüfen."
   } catch { self.alert(error.localizedDescription) }
  }
 }
 private func loadEntries(_ list: [Entry], root: URL, monitorTrim: Double) {
  entries = list
  let files = list.map { $0.file.hasPrefix("/") ? URL(fileURLWithPath: $0.file) : root.appendingPathComponent($0.file) }
  do { try monitor.load(files, matched: list.map { pow(10, $0.matchedGainDB / 20) }, original: list.map { _ in pow(10, monitorTrim / 20) }); audioReady = true
   monitor.mode(levels.selectedSegment == 1); monitor.mono(mono.state == .on)
  } catch { audioReady = false; alert("Render vorhanden; Wiedergabeproblem: " + error.localizedDescription) }
 }
 @objc private func selectAudio() { guard !entries.isEmpty else { return }; let i = choices.selectedSegment; guard entries.indices.contains(i) else { return }; monitor.select(i); seek.maxValue = monitor.duration; describe(); updateControls() }
 @objc private func levelMode() { monitor.mode(levels.selectedSegment == 1); describe() }
 @objc private func monoMode() { monitor.mono(mono.state == .on) }
 @objc private func togglePlay() { monitor.playPause() }
 @objc private func seekAudio() { monitor.seek(seek.doubleValue) }
 private func describe() {
  guard entries.indices.contains(choices.selectedSegment) else { return }; let index = choices.selectedSegment, m = entries[index].metrics
  var text = String(format: "Datei: %.2f LUFS-I · Short-term max %.2f LUFS · TP≈ %.2f dBTP · Korrelation %.3f\n", m.lufs, m.shortMax, m.truePeakEstimate, m.correlation)
  let trim = masters?.originalMonitorTrimDB ?? mix?.originalMonitorTrimDB ?? 0, target = masters?.comparisonLUFS ?? mix?.comparisonLUFS ?? -18
  text += levels.selectedSegment == 0 ? String(format: "Loudness Matched: Ziel %.2f LUFS, nur Abhören; Exporte unverändert.\n", target) : String(format: "Original Level: keine individuelle Normalisierung; gemeinsamer Monitor-Headroom %+.2f dB für alle Dateien.\n", trim)
  if let mix = mix {
   text += String(format: "Vocal-Gain %+.2f dB · Unmasking max. %.2f / %.2f / %.2f dB · aktiv %.1f %%\n", mix.vocalGainDB, mix.unmask.maximum[0], mix.unmask.maximum[1], mix.unmask.maximum[2], mix.unmask.activeFraction * 100)
   if (mix.unmask.maximum.max() ?? 0) < 0.05 { text += "Kein relevanter Unmasking-Eingriff nötig nach den begrenzten Bandkriterien.\n" }
   if abs(mix.sourceInfos[0].seconds - mix.sourceInfos[1].seconds) > 0.2 { text += "Stem-Längen unterschiedlich: kürzerer Stem mit Stille ergänzt; Ende kontrollieren.\n" }
  }
  if let master = masters, (3...5).contains(index) {
   let result = master.masters[index - 3]
   text += String(format: "Referenzziel %.2f LUFS · erreicht %.2f LUFS · Compressor max %.2f dB · Limiter max %.2f dB\n", result.targetLUFS, result.metrics.lufs, result.compressorMaxDB, result.limiterMaxDB)
   text += String(format: "Seiten-EQ (600 / 2500 / 9000 Hz): %+.2f / %+.2f / %+.2f dB; Mitten bleiben erhalten.\n", result.sideEQ[0], result.sideEQ[1], result.sideEQ[2])
   text += result.reason + "\n"
  }
  if let report = masters {
   for pair in report.similarities where pair.nearIdentical { text += String(format: "Hinweis: %@/%@ nahezu identisch (lautheitsneutraler Rest %.1f dBr).\n", ["A", "B", "C"][pair.a], ["A", "B", "C"][pair.b], pair.residualDB) }
  }
  text += "Analyse aktiver FFT-Fenster; keine automatische Refrain-Zuordnung. Technische Tests ersetzen keinen Hörtest."
  info.stringValue = text
 }
 private func transport() {
  let position = monitor.position
  if NSApp.currentEvent?.type != .leftMouseDragged { seek.doubleValue = position }
  func time(_ s: Double) -> String { String(format: "%d:%02d", Int(s)/60, Int(s)%60) }
  clock.stringValue = "\(time(position)) / \(time(monitor.duration))"
  graph.cursor = position
  if monitor.playing && position >= monitor.duration { monitor.pause() }
  if monitor.underruns > 0 && !busy { status.stringValue = "Wiedergabe-Pufferunterlauf: \(monitor.underruns). Exporte bleiben unverändert; Ausgabegerät/Last prüfen." }
 }
 @objc private func exportAudio() {
  guard let report = masters, let root = masterFolder, (3...5).contains(choices.selectedSegment) else { return }
  let file = report.masters[choices.selectedSegment - 3].file
  let panel = NSSavePanel(); panel.allowedContentTypes = [.wav]; panel.nameFieldStringValue = "DILIYAN-V2-" + file
  guard panel.runModal() == .OK, let dest = panel.url else { return }
  let canonical = dest.resolvingSymlinksInPath().standardizedFileURL
  guard !urls.compactMap({ $0 }).contains(where: { $0.resolvingSymlinksInPath().standardizedFileURL == canonical }), !(project.map { canonical.path.hasPrefix($0.path + "/") } ?? false) else { alert("Quellen und interne Projektdateien sind geschützt."); return }
  do { let tmp = dest.deletingLastPathComponent().appendingPathComponent(".diliyan-v2-" + UUID().uuidString); defer { try? FileManager.default.removeItem(at: tmp) }
   try FileManager.default.copyItem(at: root.appendingPathComponent(file), to: tmp)
   if FileManager.default.fileExists(atPath: dest.path) { _ = try FileManager.default.replaceItemAt(dest, withItemAt: tmp) } else { try FileManager.default.moveItem(at: tmp, to: dest) }
   status.stringValue = "Exportiert: \(dest.lastPathComponent) · 48 kHz / 24 Bit PCM"
  } catch { alert(error.localizedDescription) }
 }
 @objc private func showFolder() { if let p = project { NSWorkspace.shared.open(p) } }
 @objc private func cancelJob() { job?.cancel(); status.stringValue = "Abbruch angefordert; unvollständiger Ordner bleibt zur Diagnose erhalten." }
 func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply { job?.cancel(); monitor.stop(); return .terminateNow }
 func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
