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
