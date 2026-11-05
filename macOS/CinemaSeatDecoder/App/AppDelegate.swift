import AppKit
import SwiftUI
import SystemConfiguration

@main
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let service = DecoderService()
    private let popover = NSPopover()
    private var statusItem: NSStatusItem?

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        item.button?.image = NSImage(
            systemSymbolName: "waveform", accessibilityDescription: "CinemaSeat Decoder"
        )
        item.button?.toolTip = "CinemaSeat Decoder"
        item.button?.target = self
        item.button?.action = #selector(togglePopover)
        statusItem = item
        popover.behavior = .transient
        popover.animates = false
        popover.contentViewController = NSHostingController(rootView: DecoderPanel(service: service))
        service.start()
    }

    @objc private func togglePopover() {
        if popover.isShown {
            popover.close()
        } else if let button = statusItem?.button {
            popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        let waiting = service.stop { sender.reply(toApplicationShouldTerminate: true) }
        return waiting ? .terminateLater : .terminateNow
    }
}

private struct DecoderPanel: View {
    @ObservedObject var service: DecoderService
    private let localHostname = SCDynamicStoreCopyLocalHostName(nil).map { "\($0).local" }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("CinemaSeat Decoder").font(.headline)
            Label {
                Text(service.status.label)
            } icon: {
                Image(systemName: statusIcon)
                    .foregroundStyle(service.status == .ready ? Color.green : Color.primary)
            }
            .fixedSize(horizontal: false, vertical: true)

            if let capabilities = service.capabilities {
                Divider()
                Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 7) {
                    GridRow { Text("Address"); Text(advertisedAddress) }
                    GridRow { Text("Port"); Text(service.port.formatted(.number.grouping(.never))) }
                    GridRow { Text("Uptime"); Text(formattedUptime).monospacedDigit() }
                    GridRow { Text("Service version"); Text(capabilities.serviceVersion) }
                    ForEach(0..<4) { index in
                        GridRow {
                            Text("Session \(index + 1)")
                            Text(capabilities.activityLabel(at: index))
                        }
                    }
                }
                .font(.callout)
                .textSelection(.enabled)
            }

            Divider()
            HStack {
                if case .failed = service.status {
                    Button("Restart") { service.start() }
                }
                Spacer()
                Button("Stop and Quit") { NSApplication.shared.terminate(nil) }
                    .disabled(service.status == .stopping)
            }
        }
        .padding(16)
        .frame(width: 330)
    }

    private var statusIcon: String {
        switch service.status {
        case .ready: "checkmark.circle.fill"
        case .failed, .unavailable: "exclamationmark.triangle"
        default: "clock"
        }
    }

    private var advertisedAddress: String {
        switch service.listenAddress {
        case "0.0.0.0", "::": localHostname ?? "Mac address on the local network"
        default: service.listenAddress
        }
    }

    private var formattedUptime: String {
        let formatter = DateComponentsFormatter()
        formatter.allowedUnits = [.hour, .minute, .second]
        formatter.unitsStyle = .positional
        formatter.zeroFormattingBehavior = .pad
        return formatter.string(from: service.uptime) ?? ""
    }

}
