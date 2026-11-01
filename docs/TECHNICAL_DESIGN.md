# Technical design — CinemaSeat Audio Decoder

Design date: 2026-11-01, Europe/Amsterdam. Intended implementer: one Sol at medium
reasoning effort. This is the approved design, not a statement that every part is
implemented. The service is now in its own private repository; consult current
[implementation status](IMPLEMENTATION_STATUS.md), the normative
[protocol](PROTOCOL_V1.md) and [handoff](SOL_HANDOFF.md) before making changes.
Repository documentation is maintained in English.

## 1. Authorization and boundaries

The user approved a lean Docker audio converter with no UI or source-file access,
a settings file, optional file debug logging, FLAC output and transparent
integration in **Apple TV Standard**. General Playback settings / Audio gets
only enabled, address and port. The service supports concurrent sessions. One
sustained working session is required on both DS923+ and ODROID-N2+; practical
higher concurrency is a measured result, not a promised number.

The initial task created this design; subsequent implementation authorization is
limited to the external FLAC route described here. Do not redesign existing ALAC,
add blocked local DTS/Dolby decoders to Standard, perform iPad work or change Full
behavior. New media experiments use the existing Diagnose app, never a separate
tvOS probe app. Private service Git commits/pushes have since been authorized;
public releases, images and device installation have not.

Non-goals: Atmos/DTS:X object rendering, video conversion, server library,
accounts, source URLs, file mounts, AAC transcoding, normalization, downmix,
resampling, autodiscovery, runtime internet access or an administration website.

## 2. Inspected app baseline

Design HEAD in the app repository was
`b69bcf0a21becef3350632764aa4cf287c010bea`, with a substantial existing partially
staged worktree. That inspected worktree, not HEAD alone, defined the design
baseline. Preserve unrelated work and reread relevant sources before each phase.
Actual implementation later started at f083bb1 with a clean tree; see BASELINE.md.
Historical names are not a reason to roll back newer code.

| Source in the separate app repository | Role and integration consequence |
| --- | --- |
| `CinemaPlexTV/Core/Models/MediaItem.swift`, `AudioAvailability` | `requiresServer` selects DTS/DCA/TrueHD/MLP; availability already has server-scoped generations. External decoding needs an independent availability route without overwriting server state. |
| `scripts/check_audio_variant.py` | Standard forbids DCA/TRUEHD/MLP/AC3/EAC3 decoder/encoder symbols. Preserve this hard exclusion; parsing/demuxing is already permitted. |
| `docs/AUDIO_BUILD_VARIANTS.md` | AC-3/E-AC-3 are locally copied to Apple's player and do not need this service. FLAC item validation and server audio already exist. |
| `Player/FFmpeg/Bridge/FFmpegPlaybackExecution.m` | Selected audio stream, Standard guard, local codec setup, header, packet loop and EOF drain. Add an explicit external branch before the existing Standard rejection. |
| `Player/FFmpeg/Bridge/FFmpegPlaybackPacketIO.h` and timing helpers | Existing bias, pending audio and interleaved writes each have one owner. Do not introduce a second timestamp bias. |
| `Player/FFmpeg/FFmpegTypes.swift`, command builder and engine | Output profile and HLS codec metadata must say FLAC when external audio is active. Leave the ALAC profile intact. |
| `PlayerViewModel+Preparation.swift`, `PlayerViewModel.swift` | Server selection, seek/track changes, cancellation and readiness. External sessions follow the same lifetimes. |
| `UI/Views/SettingsView.swift` | General Playback/Audio settings and the Save/Discard draft. `PlaybackSettingsView.swift` is the playback overlay and must not receive connection settings. |
| `NAS/Database/NASDatabaseBackup.swift` | Inspected full backup format 5, database schema 7. Use current versions rather than historical format-3 assumptions. |
| `CloudSync/SyncContract.swift` | Existing preference sync; external decoder settings stay local per device, without extending the sync contract. |
| `Diagnostics/Core/DiagnosticReport.swift`, `docs/diagnostics/schemas`, `scripts/validate_diagnostics.py` | Extend these together for experimental evidence and stable criterion IDs. |
| `Vendor/FFmpeg/SOURCE_REVISION` and headers | Inspect effective APIs and revision before code. Historical app docs mention 9.0.2; pin the service explicitly, never use an arbitrary system FFmpeg. |

Paths starting `Player/...`, `UI/...` or `NAS/...` are below `CinemaPlexTV/` in the
app checkout. This is a map of integration points, not permission for unnecessary
changes to every listed file.

## 3. Architecture and project boundary

```text
CS: open source → demux → selected DTS/TrueHD/MLP packets
                              │ WebSocket, audio only
                              ▼
service: protocol → decoder → PCM/FIFO → FLAC encoder → protocol
                              │ FLAC packets + format/timing
                              ▼
CS: native audio bridge → existing fMP4/HLS mux → existing AVPlayer
    video packets stay local in the existing video path
```

Use C++20, CMake, Boost.Asio/Beast for HTTP/WebSocket, nlohmann/json for JSON and
FFmpeg libavcodec/libavutil. libswresample is optional only for exact sample-format
transport without rate/layout changes. Runtime libavformat is unnecessary: CS
already demuxes input. No subprocess per packet or shell conversion pipeline.
Pin sources by revision/hash with a license manifest. Do not include the full
FFmpeg CLI in the runtime image.

The independent service repository contains VERSION, CHANGELOG, dependency
notices, CMake, src, tests, config, dependencies, Dockerfiles, scripts and docs.
A root `docker-compose.yml` provides Dockge deployment; the older example remains
under docker/. Session implementation may share server.cpp rather than require
one file per originally proposed class. A public project LICENSE must be selected
before public release. Swift/Objective-C integration stays in the app repository;
the wire protocol is the project boundary. Share identical protocol fixtures.

Service builds/tests must not need the parent app. Host fixtures may read files;
the production converter may not. Use generated/license-free PCM or authorized
user sources for test media. Never commit commercial film excerpts. Do not create
a nested Git repository or submodule in the app checkout.

## 4. Tasks, ownership and concurrency

- One process, one acceptor and two network I/O threads by default.
- Each socket has a Beast strand, with at most one read and one write in flight.
  One bounded scheduler owns every write, including close/control messages.
- Each admitted session has one codec worker and its own decoder, encoder and PCM
  FIFO. Never access one codec context from two threads concurrently.
- Default to one codec thread per decoder/encoder. Do not use auto threading
  that claims every core for each session. Configuration allows a small bounded count.
- Network callbacks do not decode audio; codec workers do not block I/O strands.
- Queues retain owned buffers or refcounted AVPackets, never pointers into Swift
  Data or reused demux packets.
- Use RAII for every AV object. Cancellation wakes credit, queue and format waits.
  Complete exactly once; never kill a worker unsafely. Shutdown closes I/O and
  waits within bounds. A genuinely stuck codec is a process failure, not cleanup success.
- One codec crash may affect all sessions: an accepted v1 single-process limit,
  not process isolation. Ordinary session errors remain local.

## 5. Formats and quality contract

Wire input codec IDs are `dts`, `truehd` and `mlp`; map only to corresponding
FFmpeg IDs and never send numeric FFmpeg enums. The first interoperable version
targets 1–8 channels, 8–192 kHz and integer audio up to 24 significant bits. An
unsupported DTS profile is not a DTS-core success. Compare DTS-HD MA fixtures
against full local HD decode, not core-only output.

Actual decoded frames establish rate, layout and precision. Source metadata may
be incomplete, but a known conflicting claim must be reported. No silent
truncation, precision loss or stereo default. Unsupported layout/float conversion
is `unsupported_format` until exact conversion is proven independently. A midstream
format change is `format_changed` and ends the session; do not silently reconfigure.

Initialize FLAC with global header, time base `1/sample_rate`, target block size
4096 and compression level 1. Check effective values after opening; defaults can
vary across FFmpeg versions. Permit only integer packing/planar interleaving that
preserves values, rate and channels. The FIFO has a hard cap and fully drains at EOF.

The decoder send/receive loop handles EAGAIN by draining the opposite side rather
than busy waiting. One packet may produce zero or multiple frames. Apply official
input padding. Decoder trimming/skip samples happen exactly once; output timing
already describes valid samples. Drain decoder, any sample-format buffer, FIFO
and encoder in that order.

Seek priming has a finite budget: at most 256 input packets or two seconds of
anchored source duration, whichever occurs first. Within that budget, skip only
proven leading fragments before first codec sync, recording counts/reasons in
evidence. Reuse the existing Diagnose DTS seek-priming investigation. Do not
ignore arbitrary decode errors. Corruption after first valid sync/frame is fatal.
No usable sync within budget is `decode_failed`. A TrueHD seek without major sync
needs source preroll from the existing reader, not invented samples.

Streaming FLAC STREAMINFO may have unknown total samples/MD5. The client uses live
extradata for fMP4 without rewriting a final header. Verify that the pinned encoder
provides usable extradata after opening. Replace any backend that requires seekable
output. Current limitations, including rejected lossy XLL recovery, are documented
in IMPLEMENTATION_STATUS.md; design targets are not blanket support claims.

## 6. Timing, seek and mux integration

The protocol defines the absolute sample timeline. Required invariants:

1. The service knows audio source time only, with no video offset or CS-relative bias.
2. The client applies source-to-local output time and existing video bias exactly once.
3. Derive decoder delay/trimming from frame timing, sample counts and official side
   data, not a one-packet-in/one-packet-out assumption.
4. Each seek creates a new socket, session and client generation. Do not flush a
   network session that can still produce old results.
5. CS preserves necessary preroll/timestamps and uses existing seek/playhead logic
   at the target. Do not arbitrarily cut FLAC frames.

### 6.1 Prove preheader startup first

`avformat_write_header` needs actual FLAC parameters before mux start. They may
become known only after several input packets. Add a bounded preheader phase:
read ahead with the same demuxer, submit selected audio, retain necessary video/
audio packet references and wait for `output_format`. Retain at most 32 MiB or
five seconds of source media, whichever occurs first. Exceeding the limit is
`format_probe_limit`; never allow an unbounded buffer.

After format validation, create the output stream, write the header and replay
retained packets exactly once. No second source reader and no media file sent to
the service. Prove this in Diagnose before production.

### 6.2 Reusable client/native bridge

Proposed app components:

- `Core/Models/ExternalAudioDecoderSettings.swift`: version-1 local configuration.
- `Player/ExternalAudio/ExternalAudioRoutePolicy.swift`: pure route decisions.
- `ExternalAudioSession.swift`: actor using URLSessionWebSocketTask, bounded
  queues, protocol codec, timeouts and generation checks.
- `ExternalAudioWire.swift`: pure binary/JSON parser without UI/AVPlayer.
- `FFmpeg/Bridge/FFmpegExternalAudio.{h,m}`: explicit contract between the native
  packet loop and Swift transport; no Objective-C block per byte.

Pass an owned route configuration/session handle through typed request fields,
not FFmpeg command text or global defaults. The native guard permits a blocked
source only with a valid external-FLAC route object; otherwise retain rejection.
Never open a blocked local decoder.

| Operation | Required semantics |
| --- | --- |
| `open(config, generation, cancellation)` | Asynchronous Swift transport; returns one owned session handle. Hold no native mux lock across await. |
| `submitInput(packet)` | Ref/copy packet and allowlisted side data; accepted/wouldBlock/failure. On wouldBlock, pump receive/control; never drop input. |
| `pollFormat()` | notReady/validatedFormat/failure; usable during preheader. |
| `pollOutput()` | notReady/ownedPacket/end/failure; nonblocking for the mux owner. |
| `finishInput()` | Exactly once after the last accepted input; no implicit cancellation. |
| `cancel()` | Idempotent, wakes all waits; late completions remain scoped to the old generation. |
| `close()` | Release ownership exactly once after worker/network completion. |

The Objective-C handle strongly retains its Swift owner until close. Callbacks
must not use unprotected raw pointers. One cancellable wait primitive signals new
credit, format, output or failure. Do not use periodic busy polling. Existing
FFmpeg interrupt callbacks must also terminate these waits.

One native mux owner writes all streams. Swift receive validates/enqueues FLAC
packets and never writes directly into AVFormatContext. The main loop pumps remote
audio between demux steps and at EOF, preserving interleaving and production
budgets. Do not serialize send-one-packet/wait-for-one-response: codec buffering
would deadlock. During credit waits, receive/control/cancel continue. If video
must wait for interleaving, its queues/waits remain bounded and cancellable.

Only allowlisted AVPacket side data leaves CS. New codec extradata in a session
is `format_changed`; do not mix old/new samples in a reconfigured pipeline. Add
production sources to relevant Xcode/host harnesses and preserve existing public
test contracts.

## 7. Route selection and settings

| Situation | Behavior |
| --- | --- |
| Full, or external option disabled | Preserve existing behavior exactly. |
| Standard, supported/native codec | Existing route; no converter connection. |
| Standard DTS/TrueHD/MLP, external enabled, original source reachable | Try external FLAC first. |
| External preparation fails | Try at most one existing permitted route; no retry loop. |
| Failure during playback | Controlled restart at current source position through a permitted fallback, otherwise stop with a clear error. |
| Server offers only its conversion, not original bytes | The service cannot retrieve an unreachable original track. Use existing server policy or explicitly report no route. |

Fallback must not silently change track, language, layout or quality. An existing
configured FLAC server route is usable only if actual output satisfies the same
channel/quality contract. Do not automatically fall back from external lossless
audio to AAC, even when an explicit AAC server setting exists elsewhere. Without
an external route, retain existing server policies.

Browsing availability: enabled plus valid configuration makes a blocked track a
candidate, not a guaranteed live pass. Do not request per movie tile. Cache
reachability/capabilities for at most 30 seconds, scoped to endpoint/configuration
generation. Connection failures do not establish permanent codec unavailability.
An endpoint change invalidates old cache/pending completions. Active sessions
retain their configuration snapshot; edits affect subsequent sessions.

UI appears only in tvOS Standard's general Playback settings / Audio:

- Use external audio decoder: off by default.
- Address: hostname/IPv4/IPv6 only, without scheme, path, userinfo or query.
- Port: integer 1–65535, proposed default 9078.
- Show address/port only while enabled; no test button or additional status page.
- Follow existing Save/Discard/validation. Use URLComponents, including IPv6,
  rather than string concatenation to normalize host/port.

Persist locally under `audio.externalDecoder.configuration.v1`: version, enabled,
host, port. No secrets or iCloud sync. Include it in manual full configuration
export: migrate current backup format 5 to 6, requiring a valid new field in 6.
Read formats 1–5 with disabled defaults, retaining legacy validation and DB schema
7. Older apps must reject 6 clearly. Shared import/export on iPad/Full may preserve
the field losslessly, while route/UI stay disabled. This authorizes no iPad build
or feature work. Reconcile current versions if they advance before integration.

Inspect current local-network/privacy/ATS settings during implementation. Add
only necessary local HTTP/WebSocket permission, never an unrestricted internet
ATS exception. LAN-only is a deployment boundary; no port forwarding and no claim
of authentication/encryption over `ws://`.

## 8. Settings, limits and logging

Read `/config/settings.json` once on startup, mounted read-only with no media
volume. Changes take effect after restart. Unknown keys, wrong types, invalid
values or a missing file fail startup with a safe concrete message. Missing
optional fields use the defaults below. The current example file is authoritative
for values; this block records the approved settings contract.

```json
{
  "version": 1,
  "listen": {
    "address": "0.0.0.0",
    "port": 9078
  },
  "sessions": {
    "maximum": 4,
    "codec_threads": 1
  },
  "limits": {
    "control_message_bytes": 65536,
    "packet_payload_bytes": 1048576,
    "input_window_bytes": 4194304,
    "output_window_bytes": 4194304,
    "queued_input_bytes": 8388608,
    "queued_output_bytes": 8388608,
    "pcm_fifo_samples": 65536
  },
  "timeouts": {
    "handshake_seconds": 5,
    "start_seconds": 5,
    "format_seconds": 10,
    "ping_seconds": 15,
    "pong_seconds": 10,
    "drain_seconds": 10,
    "close_seconds": 2
  },
  "logging": {
    "debug_enabled": false,
    "file": "/logs/decoder.log",
    "maximum_file_bytes": 10485760,
    "retained_files": 3
  }
}
```

Hard ranges: sessions 1–32 and codec_threads 1–4. Message limits may decrease but
never exceed protocol maxima. Windows must hold at least one maximum message
(payload + 64) and stay within queue capacity. Current code additionally reserves
capacity for an extra owned packet. FIFO is 8192–262144 samples per channel;
check total allocation for overflow. Audio range is 8–192 kHz, at most eight
channels. Timeouts are 1–120 seconds; ping interval is 5–60 seconds. Do not timeout
intentional data pause or credit wait; heartbeat monitors connection health.

With debug off, never open a logfile or require a log directory. Emit only minimal
sanitized startup/fatal stderr messages. With debug on, write rotated JSONL events,
not arbitrary FFmpeg log strings, user input or packet dumps. Evidence targets:
random temporary session ID, codec/profile, effective formats, counts, CPU/wall
time, queue high-water marks, error code and lifecycle. IP addresses are not
needed. Summaries at most once per second, no per-packet logging. A write failure
disables file logging and produces one stderr warning without stopping audio.
Bound the log queue and count dropped events. Current CPU/RSS measurement gaps
are documented in status; wall time is not CPU time.

PCM/wire queues are bounded, but libavcodec may allocate internal memory. Measure
RSS; queue caps are not a hard total-RSS guarantee. A proposed Docker memory limit
is 512 MiB for four sessions, subject to measurements.

## 9. Docker and standalone builds

- Multistage Linux build. Runtime contains the service, required shared libraries,
  notices and version/build manifest, not compilers, Python or FFmpeg CLI.
- Native linux/amd64 and linux/arm64; no production emulation. Test arm64 on the
  ODROID itself. A QEMU build is not performance proof.
- Pin base-image digest and dependency source versions/hashes. Start with the
  app's validated FFmpeg revision, fetched independently from upstream rather
  than linked to app Vendor. Do not mutate the app's vendor build.
- Minimal FFmpeg configuration disables everything/autodetect/programs/docs/
  network/avdevice/avfilter/postproc; enable avcodec/avutil, needed helpers,
  dca/truehd/mlp decoders, flac encoder and only parsers proven necessary. Inspect
  the actual registry/symbols; configure flags in docs alone are not evidence.
- Dynamically link LGPL libraries, excluding GPL/nonfree components. Distribute
  matching exact sources/build instructions/notices when publishing binaries.
  This design makes no codec-patent determination.
- Non-root UID/GID, read-only root, cap_drop ALL, no-new-privileges, no privileged
  mode, Docker socket or host network. Publish one TCP port.
- Only settings and optional logs mounts. No media tmpfs is needed. Any runtime
  tmpfs must be explicitly small and must never act as audio spool.
- Restart unless-stopped. Use the service's `--healthcheck`; do not add curl just
  for health. Health is liveness; exhausted session capacity is not unhealthy.
- On SIGTERM, close the acceptor, report server_shutdown to active sessions,
  allow at most ten seconds of cleanup and free resources. Docker grace is 15 seconds.

The root Dockge Compose file uses a local checkout and image build, with read-only
settings and `pull_policy: never`. Build explicitly before first deployment. The
settings bind uses `create_host_path: false` so a missing file fails safely.
Dockge setup is in DEPLOYMENT.md. Do not install Docker on the development Mac.

Standalone verification entrypoint:
`python3 scripts/run_checks.py --all --scratch <absolute-path>`.
Python is build/test tooling only. Run unit/protocol/codec/container checks. Missing
Docker or fixtures are incomplete and fail full release verification; do not mark
skipped mandatory checks green. Current runner limitations/acceptance remain explicit.

## 10. Test and acceptance matrix

| ID | Required evidence |
| --- | --- |
| SVC-01 | Independent amd64/arm64 build, registry/version/provenance; no media mounts, read-only runtime, debug off without a writable logs directory. |
| SVC-02 | Wire golden vectors, length/overflow/NaN/duplicate-key rejection, state machine, unknown version/codec, invalid side data, credits and races. |
| SVC-03 | DTS core, DTS-HD MA, TrueHD, MLP: full reference decode compared with decoded FLAC, equal samples/layout/rate/precision and no HD-to-core-only fallback. |
| SVC-04 | Decoder/encoder EAGAIN, EOF tail, missing PTS after anchor, unusable first anchor, skip samples/priming, negative time, discontinuities and midstream format changes. |
| SVC-05 | Slow reader/writer, no credit, full queues, long pause, half-open socket, cancel/seek/track switch/EOF/shutdown, capacity and resource recovery. |
| APP-01 | Only Standard tvOS exposes three fields; Save/Discard/defaults/IPv6; Full and disabled match baseline. |
| APP-02 | Original track ID/language retained, route precedence, server fallback, no AAC/downmix/silent fallback, availability and generation invalidation. |
| APP-03 | Bounded preheader, FLAC extradata through fMP4 into actual AVPlayerItem with correct codec/rate/channels, no duplicate packets/bias. |
| APP-04 | Stop/cancel at every await, superseded pending seeks, ignored old results, track-change rollback, paused track switch, EOF and restart. |
| APP-05 | Backup 6 roundtrip, valid legacy 1–5 with disabled default, corrupt-field rejection, no secrets, DB migration or new sync. |
| HW-01 | One session on DS923+ and ODROID with representative demanding 7.1/24-bit DTS-HD MA/TrueHD, 48/96/192 kHz where supported by codec/source. |
| HW-02 | Same material at two/four sessions: throughput, RSS, CPU, thermals, queues and degradation. Higher concurrency may be the hardware limit. |
| ATV-01 | Diagnose on physical Apple TV: correct audible channel routing, lip sync and existing video/DV/HDR behavior. Host success is not proof. |

Compare reference PCM after identical valid decoder trims, with canonical integer
packing and explicit channel order. Lossy DTS equality means equality to decoded
reference, not an unavailable original studio source. Explicitly exclude
unsupported rate/layout combinations; do not lower precision just to pass a test.

Per-device performance procedure: record OS/kernel, architecture, RAM, Docker
limits, build hash, throttling/temperature and background load. Measure unpaced
conversion, then 60 minutes of end-to-end realtime with pause, ten distributed
seeks and ten track changes. Single-session target is at least 2x realtime; the
hard requirement is sustained error-free realtime with bounded memory on both
devices. Report 2x headroom separately. CS buffer target is 3–5 seconds; additional
network/conversion startup target is at most two seconds on wired LAN, with total
startup measured separately. Lip sync: no audible drift, measurable AV offset
where source allows, preferably within 40 ms with no increasing trend. Targets
are not measured results.

## 11. Diagnose contract

Add source-gated test `external_audio_decode.v1` to existing Diagnose, with variants
`external.flac.dts.v1`, `external.flac.truehd.v1` and `external.flac.mlp.v1`.
Required stable criteria:

- `external_audio.protocol.v1`
- `external_audio.format_preservation.v1`
- `external_audio.timeline.v1`
- `external_audio.throughput.v1`
- `external_audio.cleanup.v1`
- `external_audio.rendering.v1` (human judgment required)

Evidence includes desired/applied endpoint settings, sanitized without a private
hostname in shareable reports; service/protocol/FFmpeg revision; source profile;
format/layout; counts; canonical PCM hashes for host fixtures; timing series;
queues; explicit missing/truncated coverage and cleanup result. Never include
packets, FLAC blocks, PCM, playlists, init files or segments in reports. Preserve
evidence before player teardown. Update Swift report contract, JSON schema,
validator, Diagnose UI and analysis guide together. Do not silently change an
existing schema version incompatibly.

## 12. Implementation phases and stop criteria

Work sequentially and maintain the matrix. No broad refactors or parallel agents
without an explicit request. Each phase ends with evidence and remaining
uncertainty, not compilation alone.

**Phase 0 — inventory.** Read current instructions, these documents, bridge/timing,
settings/backup and Diagnose contracts. Record versions and worktree scope without
secrets/build outputs. Inspect actual codec/layout/extradata properties.

**Phase 1 — protocol and core.** CMake, pins, settings, golden vectors, bounded
queues, state machine and standalone codec fixtures. Prove SVC-02/03/04 before
accepting networking or app integration. No success stub.

**Phase 2 — service and containers.** Beast transport, credits, heartbeat, cleanup,
rotated logging, both native images and independent SVC-01/05 checks. Build context
is only this repository. Provide Synology/ODROID deployment instructions. Preparing
independent host/container work does not bypass missing phase-1 acceptance.

**Phase 3 — Diagnose feasibility.** Reusable native FLAC preheader, transport and
mux integration, initially source-gated in Diagnose with production unchanged.
Establish physical format/layout/timing and single-session performance on both
devices. Report missing access/tests without claiming approval. If one session
fails the requirement, optimize within scope or present the concrete limit for a
decision. Do not compensate with downmix, AAC or disk spool.

**Phase 4 — Standard production integration.** Only after phase 3 passes: route
policy, three settings, availability, lifecycle/fallback, HLS metadata and backup
format 6, using the same components and preserving ALAC/Full.

**Phase 5 — final acceptance.** APP-01 through APP-05, concurrent performance,
long physical acceptance, docs and an independent build outside the app folder.
For app changes run full tvOS verification with the toolchain selected in the
actual command environment:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 Tests/run_checks.py --all --app --scratch /absolute/resolved/scratch/path
```

Build/test Standard separately with its current scheme; the generic --app check
does not itself establish distribution-variant acceptance. Preserve codec
exclusions. Full regressions can use existing local checks, never an iPad gate.
Never weaken mandatory app verification to conceal a service/Standard failure.

Private service commits/pushes are authorized by the subsequent user request.
Use independent service versions/changelog. App commits still require root app
release policy, shared patch/build metadata, top README and full final verification.
No public release, image publication, device installation or workflow dispatch
is inferred. GitHub Verify remains disabled.

## 13. Technical references

These references support API choices, not unmeasured performance:

- [FFmpeg send/receive contract](https://ffmpeg.org/doxygen/7.1/group__lavc__encdec.html)
  and [decoder padding](https://www.ffmpeg.org/doxygen/7.1/group__lavc__decoding.html).
  Build against the actual pinned revision's headers/documentation.
- [Boost.Beast WebSocket](https://www.boost.org/latest/libs/beast/doc/html/beast/using_websocket.html):
  use an existing RFC6455 implementation, not a handwritten handshake/framing layer.
- [Apple URLSessionWebSocketTask](https://developer.apple.com/documentation/foundation/urlsessionwebsockettask).
- [Apple HLS authoring](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html):
  FLAC in fMP4 still requires actual player-item validation.
- [FFmpeg licensing](https://ffmpeg.org/legal.html): provide dependency compliance
  without inventing an unexamined codec-patent blocker.
