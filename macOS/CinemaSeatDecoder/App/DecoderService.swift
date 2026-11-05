import AppKit
import Combine
import Darwin
import Foundation

@MainActor
final class DecoderService: ObservableObject {
    enum Status: Equatable {
        case stopped, starting, ready, unavailable, stopping, failed(String)

        var label: String {
            switch self {
            case .stopped: "Stopped"
            case .starting: "Starting…"
            case .ready: "Available"
            case .unavailable: "Decoder status unavailable"
            case .stopping: "Stopping…"
            case .failed(let message): message
            }
        }
    }

    struct Capabilities: Decodable {
        struct Activity: Decodable {
            let sourceProfile: String
            let sourceChannels: Int
            let outputCodec: String

            var label: String {
                let codec = switch sourceProfile {
                case "truehd": "TrueHD"
                case "dts-hd-ma": "DTS-HD MA"
                default: sourceProfile.uppercased()
                }
                return "\(codec) \(sourceChannels) → \(outputCodec.uppercased())"
            }
        }

        let serviceVersion: String
        let sessionActivity: [Activity?]

        func activityLabel(at index: Int) -> String {
            guard sessionActivity.indices.contains(index) else { return "Not active" }
            return sessionActivity[index]?.label ?? "Not active"
        }
    }

    private struct Settings: Decodable {
        struct Listen: Decodable {
            var address = "0.0.0.0"
            var port = 9078

            enum CodingKeys: String, CodingKey { case address, port }

            init(from decoder: Decoder) throws {
                let values = try decoder.container(keyedBy: CodingKeys.self)
                address = try values.decodeIfPresent(String.self, forKey: .address) ?? address
                port = try values.decodeIfPresent(Int.self, forKey: .port) ?? port
            }

            init() {}
        }

        let listen: Listen?
    }

    @Published private(set) var status: Status = .stopped
    @Published private(set) var capabilities: Capabilities?
    @Published private(set) var uptime: TimeInterval = 0
    private(set) var port = 9078
    private(set) var listenAddress = "0.0.0.0"

    private var child: Process?
    private var lifetime: Pipe?
    private var polling: Task<Void, Never>?
    private var stopCompletion: (() -> Void)?
    private let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 2
        configuration.timeoutIntervalForResource = 2
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: configuration)
    }()

    func start() {
        guard child == nil else { return }
        let settingsURL: URL
        let endpoint: URL
        do {
            settingsURL = try Self.settingsFile()
            let settings = try JSONDecoder().decode(Settings.self, from: Data(contentsOf: settingsURL))
            let listen = settings.listen ?? Settings.Listen()
            port = listen.port
            listenAddress = listen.address
            endpoint = try Self.capabilitiesURL(for: listen)
        } catch {
            status = .failed("Could not read decoder settings. Check settings.json.")
            return
        }

        let process = Process()
        let pipe = Pipe()
        process.executableURL = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Helpers/cinemaseat-audio-decoder")
        process.arguments = ["--settings", settingsURL.path]
        var environment = ProcessInfo.processInfo.environment
        environment["CINEMASEAT_DECODER_PARENT_PIPE"] = "1"
        process.environment = environment
        process.standardInput = pipe
        process.terminationHandler = { [weak self] finished in
            let code = finished.terminationStatus
            // AppKit waits in its modal run loop after .terminateLater.
            RunLoop.main.perform(inModes: [.common, .modalPanel]) {
                MainActor.assumeIsolated { self?.didExit(code: code) }
            }
        }

        do {
            try process.run()
        } catch {
            status = .failed("Could not start the bundled decoder.")
            return
        }

        child = process
        lifetime = pipe
        status = .starting
        let started = ProcessInfo.processInfo.systemUptime
        polling = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refresh(endpoint, started: started)
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }

    /// Returns whether application termination must wait for the decoder to exit.
    func stop(completion: @escaping () -> Void) -> Bool {
        guard child != nil else { return false }
        stopCompletion = completion
        status = .stopping
        polling?.cancel()
        // EOF lets the helper request the service's regular SIGTERM shutdown.
        lifetime?.fileHandleForWriting.closeFile()
        lifetime = nil
        return true
    }

    private func didExit(code: Int32) {
        polling?.cancel()
        polling = nil
        child = nil
        lifetime = nil
        capabilities = nil
        uptime = 0
        if let completion = stopCompletion {
            stopCompletion = nil
            status = .stopped
            completion()
        } else {
            status = .failed("Decoder exited (code \(code)). Check the listen address and port.")
        }
    }

    private func refresh(_ url: URL, started: TimeInterval) async {
        let current: Capabilities?
        do {
            let (data, response) = try await session.data(from: url)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else {
                throw URLError(.badServerResponse)
            }
            let decoder = JSONDecoder()
            decoder.keyDecodingStrategy = .convertFromSnakeCase
            current = try decoder.decode(Capabilities.self, from: data)
        } catch {
            current = nil
        }
        guard !Task.isCancelled else { return }

        uptime = ProcessInfo.processInfo.systemUptime - started
        capabilities = current
        if current != nil {
            status = .ready
        } else if status != .starting || uptime >= 8 {
            status = .unavailable
        }
    }

    private static func settingsFile() throws -> URL {
        if let override = ProcessInfo.processInfo.environment["CINEMASEAT_DECODER_SETTINGS"] {
            return URL(fileURLWithPath: override)
        }
        let support = try FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true
        )
        let folder = support.appendingPathComponent("CinemaSeat Decoder", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let url = folder.appendingPathComponent("settings.json")
        if !FileManager.default.fileExists(atPath: url.path) {
            try Data("""
            {
              "version": 1,
              "listen": { "address": "0.0.0.0", "port": 9078 },
              "sessions": { "maximum": 4 },
              "logging": { "debug_enabled": false }
            }

            """.utf8).write(to: url, options: .atomic)
        }
        return url
    }

    private static func capabilitiesURL(for listen: Settings.Listen) throws -> URL {
        let host: String
        switch listen.address {
        case "0.0.0.0": host = "127.0.0.1"
        case "::": host = "::1"
        default: host = listen.address
        }
        guard (1...65535).contains(listen.port) else { throw URLError(.badURL) }
        var ipv4 = in_addr()
        var ipv6 = in6_addr()
        let unscopedHost = host.components(separatedBy: "%")[0]
        let urlHost: String
        if inet_pton(AF_INET, host, &ipv4) == 1 {
            urlHost = host
        } else if inet_pton(AF_INET6, unscopedHost, &ipv6) == 1 {
            urlHost = "[\(host.replacingOccurrences(of: "%", with: "%25"))]"
        } else {
            throw URLError(.badURL)
        }
        guard let url = URL(string: "http://\(urlHost):\(listen.port)/v1/capabilities") else {
            throw URLError(.badURL)
        }
        return url
    }
}
