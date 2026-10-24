import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var window: NSWindow?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let menu = NSMenu()
        let appItem = NSMenuItem()
        menu.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Beenden", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        NSApp.mainMenu = menu

        let frame = NSRect(x: 0, y: 0, width: 640, height: 280)
        let window = NSWindow(contentRect: frame, styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "DILIYAN STEM MASTER – Build Test"
        window.isReleasedWhenClosed = false
        let title = NSTextField(labelWithString: "DILIYAN STEM MASTER")
        title.font = .boldSystemFont(ofSize: 26)
        let detail = NSTextField(wrappingLabelWithString: "Build Test · Apple Silicon\n\nDiese native Test-App zeigt ausschließlich dieses Fenster.\nKeine Audioverarbeitung, kein Netzwerkzugriff.")
        detail.alignment = .center
        let stack = NSStackView(views: [title, detail])
        stack.orientation = .vertical
        stack.spacing = 20
        stack.translatesAutoresizingMaskIntoConstraints = false
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: window.contentView!.centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: window.contentView!.centerYAnchor),
            stack.widthAnchor.constraint(lessThanOrEqualToConstant: 580)
        ])
        self.window = window
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        if CommandLine.arguments.contains("--smoke-test") {
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                guard window.isVisible, window.contentView != nil else { exit(1) }
                print("PASS: AppKit event loop and visible window; \(ProcessInfo.processInfo.operatingSystemVersionString)")
                NSApp.terminate(nil)
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
