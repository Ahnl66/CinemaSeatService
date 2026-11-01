# Implementation handoff — external audio decoder

This is the English maintenance version of the original handoff for one Sol at
medium reasoning effort. The service now lives in this independent repository;
its initial implementation is partial. Read current status before continuing.
Do not treat this document as authorization to repeat completed work or activate
an unaccepted production route. Repository documentation must remain in English;
chat communication follows the user's requested language.

## Required context and order

For service work, read this repository's README, technical design, protocol v1
and implementation status in full. For app integration, also read the current
root AGENTS.md in the separate CinemaPlexTV app checkout, originally located at
`/Users/arthur/Documents/Projects/CinemaPlexTV`.

Follow technical-design phases 0 through 5 in order. Update status and evidence
references after each phase. Use no subagents or delegation. Give short progress
updates that describe findings and unresolved acceptance.

## Approved scope

- A small headless Docker service, native amd64 and arm64, with multiple sessions.
- Test targets: Synology DS923+ Ryzen R1600 and ODROID-N2+. One sustained session
  must work on each; higher concurrency is determined by measurements.
- Input is compressed audio from CS only: DTS/DTS-HD, TrueHD and MLP.
- Output is channel-based FLAC only, without additional quality loss, downmix,
  rate reduction, normalization or silent DTS-core fallback.
- No source files/URLs, NAS shares, media library, UI or accounts.
- Docker settings file; optional rotated file debug logging, off by default.
  Audio stays in RAM; disk spooling is not an acceptable workaround.
- Only tvOS Standard gets external-decoder enabled/address/port in general
  Playback settings / Audio, using existing Save/Discard. Everything else is
  automatic. Add no quality, codec or buffer options to CS.
- Preserve Full and disabled behavior. No iPad feature, build or test work.
- Only the new external FLAC integration is authorized. Existing ALAC choices
  and blocked local decoders are out of scope. Codec exclusions remain mandatory.

## Repository and baseline

Service code, standalone tests, containers and protocol fixtures belong in this
repository and must build without the app checkout. Swift/Objective-C integration
belongs in the app repository. Do not add a nested repository or submodule there.
The initial service version was 0.1.0; service VERSION/changelog are independent
of app version and protocol major. Version 0.1.1 adds Dockge deployment and English
documentation without changing codec behavior.

The original design used app HEAD b69bcf0 plus its 2026-11-01 worktree. Actual
implementation started from f083bb1 with a clean worktree. Always inspect current
paths/contracts, including existing video-server/cloud/layout/iPad work. Preserve
all unrelated staged and unstaged changes. Historical scheme names and backup
versions never justify reverting newer code. The planned backup migration is
5 to 6 with DB schema 7; if current versions have advanced, update consistently
and retain legacy-read tests.

## Implementation priorities

Build and prove the service core, protocol and containers first, then Diagnose.
Every new playback experiment must be source-gated in the existing Diagnose app,
with stable IDs, schema/validator/UI/analysis-guide changes and evidence saved
before teardown. Do not create a separate diagnostic tvOS app. Reports must not
contain packets or decoded media. Component success is not physical audio,
lip-sync or visible HDR/DV evidence.

Explicitly prove these three integration problems:

1. Prime enough audio before the fMP4 header to obtain actual FLAC STREAMINFO and
   layout, with a bounded local preheader buffer and no duplicate demuxer.
2. Handle decoder/encoder buffering and independent bidirectional credits without
   packet-request/response deadlock. Receive/control/cancel must run with full queues.
3. Use one absolute source timeline, decoder trims once and CS origin/bias once;
   create a new generation/socket for every seek and track switch. Never conceal drift.

Activate production only after the Diagnose/hardware phase succeeds. Reuse the
same transport/native bridge components. If physical access prevents completing
a phase, finish independent service/host work and identify exact missing tests.
Do not claim hardware acceptance. Ask only for concrete missing access/test data,
not functional decisions that are already approved. The user reports an Armbian
ODROID with Dockge at odroidn2.local and a physical Apple TV available; no target
measurement has yet been performed. Never install Docker on the development Mac.

Verify Standard session/endpoint lifetimes, cancellation at every await, late
generation callbacks, out-of-range packets, cleanup and failure paths. Fallback
must not silently introduce AAC, downmix or another track. Preserve existing
server settings and Full. Add every new Standard tvOS source to relevant Xcode
and host harnesses; do not add an iPad gate.

## Verification

For the service, run the independent entrypoint on an absolute scratch path and
perform native Docker checks on both architectures:

```sh
python3 scripts/run_checks.py --all --scratch /absolute/scratch/path
```

For app changes, use the existing full entrypoint with the full Xcode toolchain:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 Tests/run_checks.py --all --app --scratch /absolute/resolved/scratch/path
```

Also build tvOS Standard explicitly with the current scheme and validate codec
exclusions. Report host tests, container checks, local app verification, physical
Apple TV acceptance and both hardware benchmarks separately. Missing mandatory
checks are not green, and regressions must not be weakened.

## Publication boundaries and delivery

The user authorized private source commits/pushes to Ahnl66/CinemaSeatService.
App commits follow the separate app release policy; service commits follow their
independent version/changelog policy. Do not infer authorization for installation
on devices, a public release, image publication or a new remote repository.
GitHub Verify stays disabled; do not dispatch workflows.

Finish with concrete working behavior, tests, actual hardware measurements, exact
open work and links to implementation status and deployment instructions. A stub
or compiling-only route is not a successful delivery.
