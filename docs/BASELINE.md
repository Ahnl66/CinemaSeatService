# Implementation baseline — 2026-11-01

This records the initial implementation environment, not current acceptance.

- Implementation HEAD in the app repository: `f083bb1d6b40a6c8f33993b566ef8163bcde26a3`.
  The worktree and index were clean at implementation start. Design HEAD b69bcf0
  is historical; no existing code was reverted.
- App backup format 5, database schema 7. Settings use the existing draft and
  Save/Discard behavior in `SettingsView.swift`. CloudSync was unchanged.
- App FFmpeg SOURCE_REVISION: `946fcce07b6dcd0331c8cc609192aeff5e1924f8`.
  The service fetches this revision independently without linking to app Vendor.
- Native bridge: Standard rejects DTS/TrueHD/MLP before local decoder setup. The
  fMP4 header precedes the packet loop. `CPPlaybackRelativePTS` owns source-origin
  rebasing; `CPPlaybackWriteAudio` owns video bias. External audio must reuse these
  owners rather than introduce a second bias.
- Diagnose has a v1 report contract, source-gated DTS seek priming and native
  timeline evidence. A new external experiment needs service-core evidence first.
- Original host: arm64 macOS with full Xcode. Docker was absent. No DS923+,
  ODROID-N2+, physical Apple TV or private HD media was connected for testing.
  No hardware acceptance was established.
- The initial implementation request prohibited commits, pushes, installation,
  version bumps and publication without further instructions. The user later
  authorized the private service repository and its development commits; this
  does not authorize device installation, public releases or workflow dispatch.

Phase 0 source inventory was completed. Encoder extradata and layout properties
were subsequently measured against the independently built pinned dependency in
phase 1. Current evidence and missing acceptance are in
[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).
