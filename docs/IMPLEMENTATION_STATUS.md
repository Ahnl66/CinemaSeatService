# Implementation status

Updated: 2026-11-01; host diagnostic snapshot 0.1.3; ODROID deployed 0.1.2. **Partially implemented; full acceptance is not established.**
The standalone native service converts tested codec packets into FLAC. CinemaSeat
external-audio integration is not implemented. No production route, settings or
backup export has changed. This is a private development source repository.

## Phases and gates

| Phase | Status | Evidence / next work |
| --- | --- | --- |
| 0 — Baseline | Complete | [BASELINE.md](BASELINE.md): implementation HEAD f083bb1, initially clean index/worktree; current backup 5 / DB 7 and bridge contracts inspected. |
| 1 — Protocol and codec core | Partially proven | Real DTS core/TrueHD/MLP roundtrips and pure protocol tests pass. DTS-HD MA, 7.1, layout coverage and additional failure fixtures are missing. The SVC-03 gate is not met. |
| 2 — Transport and Docker | Independent service/host implementation prepared; not accepted | Beast service, settings, logging, lifecycle, concurrency and sanitizer checks pass on arm64 macOS. Dockerfile and root Dockge Compose file exist; an isolated native arm64 candidate has been built/tested; amd64 remains open. This does not bypass missing phase-1 evidence. |
| 3 — Diagnose and hardware | Not started | Finish the phase-1 codec matrix. Reusable CS client/native bridge, preheader/mux integration, source-gated Diagnose test, report/schema/validator/UI/analysis guide are missing. The user reports an Armbian ODROID-N2+ at odroidn2.local and a physical Apple TV available; service access and short native component checks are verified; app integration is not. |
| 4 — Standard production integration | Blocked by acceptance gate | Only after phase 3 passes. No settings/availability/fallback/HLS/export changes have been applied. |
| 5 — Full acceptance | Open | Standalone host build proven; historical app verification recorded separately. New APP-01 through APP-05 and physical acceptance remain open. |
| Independent repository | Private development snapshots | Initial service commit a2dcd2f, version 0.1.0. Version 0.1.1 adds Dockge deployment and English documentation. No app commit, nested repository, public release or published image. |

## Evidence matrix

Results and local log references: [host evidence](evidence/HOST_2026-11-01.md).
A passing host subset never makes an entire acceptance criterion pass.

| Requirement | Status / exact coverage |
| --- | --- |
| SVC-01 standalone amd64/arm64 container | Partially proven on native arm64; full two-architecture acceptance remains open. Native arm64 macOS build and exact runtime registry pass. Running ODROID 0.1.1 is independently confirmed Linux/aarch64 with an arm64 image, read-only root, UID/GID 10001, dropped capabilities and the exact runtime codec registry. A fresh 0.1.2 native arm64 image and isolated runtime now pass component checks. Native amd64 remains untested. |
| SVC-02 protocol/interoperability/limits | Host subset passes: golden vector, negative/64-bit values, strict JSON/base64, packet limits, sequences/generations, capacity, controls and credits. Swift interoperability and the complete adversarial matrix are missing. |
| SVC-03 PCM equivalence including DTS-HD MA | Host subset passes: DTS core stereo 48 kHz, TrueHD stereo 48 kHz, MLP stereo/5.1(back) 96 kHz. Short ODROID 0.1.1 PCM checks also pass for supported fixtures. The authorized DTS-HD MA reference decodes fully at 5.1(side)/48 kHz/16-bit, but its original timestamps fail the strict timeline; a complete service roundtrip for that source and 7.1 remain unaccepted. |
| SVC-04 timing/priming/EOF/failures | Host subset passes: 30-second source anchor, negative PTS, missing PTS after anchoring, missing_timeline, discontinuity, decoder trims of 5+7 samples, actual encoder delay/EOF tail. A real TrueHD rate-change regression and the observed Matroska discrepancy are now covered. Forced EAGAIN and leading-fragment fixtures remain missing. |
| SVC-05 backpressure/cancel/multisession/cleanup | Host subset passes: two concurrent sessions, full output queue/no new credit, controls/cancel, heartbeat across a short pause, start timeout, shutdown and slot recovery. TSAN reports no race. Short ODROID one/two/four-session tests pass with slot recovery. Long pause/half-open/endurance/peak-RSS tests remain open. |
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
| Dockge Compose file | Root file and English instructions prepared. YAML/configuration inspected locally; native 0.1.2 deployment on the ODROID is verified; amd64 and full acceptance remain open. |

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

Canonical FLAC speaker layouts are validated strictly; supported native side/back layouts are preserved explicitly in the local 0.1.2 candidate; float remains rejected. The pinned service build rejects lossy XLL recovery,
which can prevent DTS-HD priming. There is no silent DTS-core fallback, downmix,
AAC, resampling or disk spool, and no claim of complete DTS-HD support.

Debug events contain wall time and queue high-water marks. Server CPU/RSS and
thermal behavior still need external measurement. The running 0.1.1 arm64 container and short component tests are now observed on the ODROID. The explicitly approved update to 0.1.2 is complete; a long target benchmark is still unexecuted. Project licensing and public distribution remain open.

## ODROID follow-up

See [ODROID_2026-11-01.md](evidence/ODROID_2026-11-01.md). One/two/four short-session
PCM checks are component evidence, not HW-01/HW-02 acceptance. The local 0.1.2
candidate fixes explicit side-layout support and strengthens host verification;
it was tested in an isolated native arm64 container, then deployed with explicit approval. The supplied source shows a 64-sample timing
discrepancy against a 48-sample allowed tick at 116 ms. The strict rejection remains
in place. No source rebias, gap fill, drop or timing-tolerance relaxation is applied.

The isolated native arm64 0.1.2 candidate passes 8/16/32 short checks at one/two/four
sessions, plus 35-second credit starvation with two heartbeats and ~7.4 ms cancel.
The temporary container is removed. Actual Matroska block anchors remain within
±16 samples while inferred lace timestamps reach an 80-sample difference. Diagnose
must investigate that distinction before any external-route timing adaptation.

## 7.1 source investigation and current deployment

The ODROID now runs the explicitly approved 0.1.2 image at commit `31a2978`;
settings were preserved and eight post-deployment PCM checks passed. The 0.1.3
host diagnostic snapshot has not been deployed. No app functionality changed.

The second authorized DTS-HD MA source provides 7.1/48 kHz/24-bit full-HD frames,
but strict reference decoding rejects initial XLL recovery before a service
session begins. A bounded metadata-only observation confirms the layout after
that rejection and explicitly reports missing leading coverage and no source or
physical acceptance. See [source evidence](evidence/DTSHD_7_1_2026-11-01.json).
This does not meet SVC-03 or authorize a runtime recovery/trim policy. A real
TrueHD 7.1 source, complete DTS-HD service pass and physical acceptance remain open.
