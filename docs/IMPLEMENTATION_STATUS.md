# Implementation status

Updated: 2026-11-01. **Partially implemented; full acceptance is not established.**
The standalone native service converts tested codec packets into FLAC. CinemaSeat
external-audio integration is not implemented. No production route, settings or
backup export has changed. This is a private development source repository.

## Phases and gates

| Phase | Status | Evidence / next work |
| --- | --- | --- |
| 0 — Baseline | Complete | [BASELINE.md](BASELINE.md): implementation HEAD f083bb1, initially clean index/worktree; current backup 5 / DB 7 and bridge contracts inspected. |
| 1 — Protocol and codec core | Partially proven | Real DTS core/TrueHD/MLP roundtrips and pure protocol tests pass. DTS-HD MA, 7.1, layout coverage and additional failure fixtures are missing. The SVC-03 gate is not met. |
| 2 — Transport and Docker | Independent service/host implementation prepared; not accepted | Beast service, settings, logging, lifecycle, concurrency and sanitizer checks pass on arm64 macOS. Dockerfile and root Dockge Compose file exist; neither native Linux image is tested. This does not bypass missing phase-1 evidence. |
| 3 — Diagnose and hardware | Not started | Finish the phase-1 codec matrix. Reusable CS client/native bridge, preheader/mux integration, source-gated Diagnose test, report/schema/validator/UI/analysis guide are missing. The user reports an Armbian ODROID-N2+ at odroidn2.local and a physical Apple TV available; access and execution are not verified. |
| 4 — Standard production integration | Blocked by acceptance gate | Only after phase 3 passes. No settings/availability/fallback/HLS/export changes have been applied. |
| 5 — Full acceptance | Open | Standalone host build proven; historical app verification recorded separately. New APP-01 through APP-05 and physical acceptance remain open. |
| Independent repository | Private development snapshots | Initial service commit a2dcd2f, version 0.1.0. Version 0.1.1 adds Dockge deployment and English documentation. No app commit, nested repository, public release or published image. |

## Evidence matrix

Results and local log references: [host evidence](evidence/HOST_2026-11-01.md).
A passing host subset never makes an entire acceptance criterion pass.

| Requirement | Status / exact coverage |
| --- | --- |
| SVC-01 standalone amd64/arm64 container | Not executed. Native arm64 macOS build outside the app folder and exact runtime codec registry pass. |
| SVC-02 protocol/interoperability/limits | Host subset passes: golden vector, negative/64-bit values, strict JSON/base64, packet limits, sequences/generations, capacity, controls and credits. Swift interoperability and the complete adversarial matrix are missing. |
| SVC-03 PCM equivalence including DTS-HD MA | Host subset passes: DTS core stereo 48 kHz, TrueHD stereo 48 kHz, MLP stereo/5.1(back) 96 kHz. DTS-HD MA, 7.1 and broader rate/precision coverage are untested. |
| SVC-04 timing/priming/EOF/failures | Host subset passes: 30-second source anchor, negative PTS, missing PTS after anchoring, missing_timeline, discontinuity, decoder trims of 5+7 samples, actual encoder delay/EOF tail. Forced EAGAIN, midstream format and leading-fragment fixtures are missing. |
| SVC-05 backpressure/cancel/multisession/cleanup | Host subset passes: two concurrent sessions, full output queue/no new credit, controls/cancel, heartbeat across a short pause, start timeout, shutdown and slot recovery. TSAN reports no race. Long pause/half-open/endurance/RSS tests remain open. |
| APP-01 settings/Full/disabled | New feature not implemented; app unchanged. Baseline checks are separate. |
| APP-02 routes/availability/fallback | Not executed; no external route activated. |
| APP-03 preheader/FLAC/fMP4/player item | Not executed. Service STREAMINFO before output is host-proven, but this does not prove native fMP4 headers or AVPlayerItem acceptance. |
| APP-04 lifecycle/seek/track switch | Not executed for the new client. Service cancellation does not establish client-generation or CS-session lifetime correctness. |
| APP-05 configuration export/legacy import | Not executed. Backup remains format 5, DB schema remains 7. |
| HW-01 DS923+ sustained single session | Not executed. |
| HW-01 ODROID-N2+ sustained single session | Not executed. Dockge installation instructions are prepared, not hardware evidence. |
| HW-02 both devices, two/four sessions | Not executed. Two host sessions are not a hardware benchmark. |
| ATV-01 audible output/lip sync/HDR/DV | Not executed. |
| Full local tvOS check | Historical baseline passed, exit 0: full --all --app entrypoint with Xcode, all host runners/native TSAN and Full build/metadata validation. No new integration exists. |
| Standard build and codec exclusions | Historical unchanged app baseline passed with CinemaSeat - Standard, arm64 tvOS Simulator; codec exclusion check passed. |
| Standalone build outside app folder | Passed on arm64 macOS with source/hash-pinned dependencies and no app Vendor dependency. |
| Dockge Compose file | Root file and English instructions prepared. YAML/configuration inspected locally; Docker/Dockge execution on the ODROID is not yet performed. |

## Three critical integration questions

1. The service supplies real 34-byte STREAMINFO before its first FLAC packet;
   actual encoder tests prove this. **CS preheader buffering, a single demuxer,
   exactly-once replay and fMP4 header setup are not implemented or proven.**
2. Server read/control handling continues independently of codec/output credit;
   full output queue and cancellation are host-tested. **The CS receive/native
   mux queue and bidirectional client implementation are still missing.**
3. The service uses absolute source samples with one anchor and checks drift;
   negative times, trims and missing later timestamps are host-tested. **CS
   origin/bias, a new socket per seek/track switch and late client callbacks
   are unproven.**

## Concrete access and material still needed

- Target: ODROID-N2+ with Armbian/Dockge at `odroidn2.local`; physical Apple TV
  available according to the user. Confirm 64-bit OS, Docker status and access
  method. Docker must not be installed on this development Mac.
- DS923+ and ODROID-N2+ for one-hour tests with CPU/RSS/temperature, pause, seeks
  and track switches; subsequently two/four sessions.
- Suitable authorized DTS-HD MA/7.1 and TrueHD sources with reference decode.
  Do not commit commercial media or include media payloads in reports.
- Physical Apple TV/audio/display setup and human channel/lip-sync/HDR/DV review
  after Diagnose integration. Device installation requires separate instructions.

## Known implementation limits

Canonical FLAC speaker layouts are validated strictly; side-only 5.1 and float
are currently rejected. The pinned service build rejects lossy XLL recovery,
which can prevent DTS-HD priming. There is no silent DTS-core fallback, downmix,
AAC, resampling or disk spool, and no claim of complete DTS-HD support.

Debug events contain wall time and queue high-water marks. Server CPU/RSS and
thermal behavior still need external measurement. Container-runtime checks and
the component benchmark script are prepared but unexecuted on target Linux
hardware. Project licensing and public distribution remain open.
