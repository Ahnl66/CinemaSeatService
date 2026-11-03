# 0.1.5 — 2026-11-03

- Support DTS 5.1(side) in native FLAC/fMP4 playback by preserving the six source speakers in a 7.1 carrier with silent back left/right channels.
- Add negotiated automatic DTS layout handling: stereo and other supported native layouts retain their original speaker positions and channel count.
- Validate and report the measured source/output mapping; reject mismatched hints or unconfirmed negotiation. Sample precision, rate and timing are preserved.

# 0.1.4 — 2026-11-02 (diagnostic service update)

- Bug fix: give a completed error frame a bounded 100 ms delivery window before
  initiating WebSocket close, so fast URLSession close callbacks do not overtake
  the failure details. Existing close deadline and immediate peer cleanup remain.
- Functional enhancement: timeline rejection can include bounded numeric context,
  the unchanged tolerance, decoded-frame count and most recent submitted packet
  metadata/hash. Reports contain no compressed or decoded media.
- Existing error codes, strict acceptance checks, channel policy and the
  configured teardown deadline remain unchanged. The approved ODROID update
  is verified; full hardware acceptance remains open.

# 0.1.3 — 2026-11-01 (host diagnostic snapshot)

- Bug fix: preserve source format hints and sanitized decoder-failure evidence
  when reference decoding fails before a service session starts.
- Add bounded, metadata-only DTS-HD inspection after rejected XLL recovery,
  explicitly retaining missing leading coverage and failed source acceptance.
- Record the authorized ODROID 0.1.2 deployment and the observed DTS-HD MA
  7.1/48 kHz/24-bit source startup limitation. Runtime codec policy is unchanged.

# 0.1.2 — development candidate, 2026-11-01

- Support explicit native side/back FLAC layouts, including DTS 5.1(side), while
  preserving channel identity and avoiding downmix or remapping.
- Add remote codec checks that compare returned FLAC against reference PCM,
  validate layout/rate/precision/absolute timing and reject bad CRC/data/metadata.
- Add bounded host-only extraction of selected DTS/TrueHD/MLP packets from an
  authorized local source, with no media payloads in evidence reports.
- Add real TrueHD format-change and source-timestamp discrepancy regressions.

No timestamp tolerance is relaxed. No CinemaSeat route or device deployment is
included. The authorized source still fails the strict source-clock requirement.

# 0.1.1 — development, 2026-11-01

- Added a root Compose file for Dockge with local image builds, read-only settings
  and explicit failure when the settings file is missing.
- Added ODROID-N2+/Armbian installation, health-check and update instructions.
- Converted all repository documentation to English.

No codec or playback behavior changes. Hardware acceptance remains open.

# 0.1.0 — development, 2026-11-01

- Added a standalone headless DTS/TrueHD/MLP-to-FLAC service with audio in RAM,
  bounded sessions/queues, protocol v1, independent byte credits and cancellation.
- Added strict settings, optional rotated metadata logging, health/capabilities,
  pinned dependency builds, codec registry checks and a server Docker recipe.
- Added real codec PCM roundtrips and WebSocket lifecycle tests. Hardware,
  DTS-HD MA, Apple TV integration and both native Linux images remain unaccepted.

Initial private source snapshot in Ahnl66/CinemaSeatService. No public release,
image publication, app version bump or production route activation.
