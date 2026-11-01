# Deployment — development snapshot 0.1.3

The service runs independently. CinemaSeat production integration and the Diagnose
connection are not implemented or accepted. The host test client can use the
service. Installing the stack does not yet enable external audio in CinemaSeat.

## Dockge on the ODROID-N2+

The intended test host is `odroidn2.local`, running Armbian and an existing Dockge
installation. Docker is not installed on the development Mac. These instructions
are for you to run on the ODROID; no device installation has been performed here.

### 1. Check the host and stacks directory

Connect to the ODROID using your existing SSH account:

```sh
ssh <your-user>@odroidn2.local
uname -m
docker compose version
```

The native arm64 test requires a 64-bit OS (`aarch64`), an accessible Docker daemon
and Compose V2. Use the same Docker host/agent that Dockge manages.

The commands below assume Dockge's default stacks directory, `/opt/stacks`. If
`DOCKGE_STACKS_DIR` is different, substitute that directory. Dockge must have the
same absolute stacks path mounted inside its container as on the host; this lets
local build contexts and bind mounts resolve correctly. Use an account with write
access to the stacks directory and permission to run Docker.

### 2. Clone and prepare the stack

The repository is private. Use the server's existing GitHub authentication; do
not put a token in the Compose file, settings file or repository URL.

```sh
cd /opt/stacks
git clone https://github.com/Ahnl66/CinemaSeatService.git cinemaseat-service
cd cinemaseat-service
git rev-parse HEAD
cp config/settings.example.json settings.json
chmod 644 settings.json
docker compose -f docker-compose.yml config
```

Keep the printed commit SHA with test results. For a specific test snapshot,
check it out before building: `git checkout <commit-sha>`.

The default service port is 9078. The required `settings.json` stays local and is
ignored by Git and the Docker build context. It contains no credentials. It is
mounted read-only at `/config/settings.json`; a missing file fails instead of
silently creating a directory. Do not copy an entire media folder into the stack.

### 3. Build once on the ODROID

```sh
docker compose -f docker-compose.yml build
```

The first build downloads pinned upstream sources and compiles FFmpeg and the
service natively. Allow it to finish before deploying. An isolated native arm64 candidate has now passed this build procedure and short
component checks on the ODROID. Record build errors and do not treat that result
as sustained playback or physical acceptance.

The root Compose file has `pull_policy: never`: Dockge uses the locally built
`cinemaseat-audio-decoder:0.1.3` image. It does not try to pull an unpublished
image, and it does not need access to the private Git repository itself.

### 4. Deploy in Dockge

1. Open your existing Dockge interface for the ODROID.
2. Open the top-right menu and select **Scan Stacks Folder**.
3. Open the discovered `cinemaseat-service` stack.
4. Deploy/start the stack and inspect its container state and terminal output.

The repository-root `docker-compose.yml` is recognized by Dockge. You do not need
to paste it into a separate stack, rename it or create a duplicate `compose.yaml`.
The complete cloned repository must remain beside it because `build.context` is
`.` and the Dockerfile lives at `docker/Dockerfile`.

### 5. Check startup and reachability

From the stack directory on the ODROID:

```sh
docker compose -f docker-compose.yml ps
docker compose -f docker-compose.yml exec audio-decoder /opt/csad/bin/cinemaseat-audio-decoder --healthcheck
```

The health command should exit successfully. If `curl` is available, test from
the ODROID or another LAN computer:

```sh
curl --fail http://odroidn2.local:9078/healthz
curl --fail http://odroidn2.local:9078/v1/capabilities
```

Expected health body: `{"protocol":1,"status":"ok"}` (JSON key order may differ).
There is no web page on port 9078. Healthy means startup/liveness passed; it does
not prove DTS-HD quality, continuous playback, channel routing or lip sync.
Default debug logging is off, so a healthy service can be quiet in container logs.

### Updates

For a checkout on `main`, stop the stack in Dockge, then run:

```sh
cd /opt/stacks/cinemaseat-service
git pull --ff-only
docker compose -f docker-compose.yml config
docker compose -f docker-compose.yml build
```

Start/deploy it again in Dockge. For a pinned checkout, explicitly select the next
commit instead of pulling. Keep your local settings. If Dockge has edited the
tracked Compose file, review those changes before updating; do not overwrite them
blindly. Rebuilding from Git, rather than Dockge's image-pull update, is required
because no service image is published. Active sessions stop during an update.

## Container configuration

The same Dockerfile is intended for native amd64 and arm64. An isolated native arm64 candidate has now been built and component-tested on
the ODROID; native amd64 and full hardware acceptance remain open. Do not use emulation for performance acceptance.
Record architecture, Docker version, image ID/digest, OS/kernel, RAM, limits, CPU,
temperature and background load for each benchmark. Another server's build does
not establish a sustained session on a DS923+ or ODROID-N2+.

Compose exposes TCP 9078 and uses UID/GID 10001, a read-only root filesystem,
`cap_drop: ALL`, `no-new-privileges`, a 512 MiB memory limit, a PID limit and a
15-second stop grace period. There are no media volumes or audio temporary files.
The memory limit is a proposed deployment limit, not a measured capacity claim.

Only use a trusted LAN. `ws://` has no authentication or encryption; do not expose
this port to the internet or configure port forwarding. The service has no
accounts, source URLs, NAS mounts, library, UI or remote-control feature.

The base-image digest and dependency sources are pinned. Apt build tools are not
yet pinned through a Debian snapshot; this is not a bit-reproducible image claim.
The older `docker/compose.example.yaml` remains available for manual deployment
with `docker/settings.json`; the root file is the recommended Dockge entrypoint.

## Settings and logging

`config/settings.example.json` contains all defaults. The minimum configuration
is `{"version":1}`. Unknown keys, types and invalid values fail startup. Settings
are read once; restart after changing them. Windows must contain at least
payload+64 bytes. Queue capacity must also allow one extra owned packet; current
validation is deliberately stricter than the design's minimum.

Debug logging is off by default; `/logs` need not exist. To enable it, set
`logging.debug_enabled` in the local settings file, create a local `logs` directory
writable by UID/GID 10001, and enable the commented log mount in Compose. Restart
the stack. Logs contain temporary session IDs, format properties, packet/sample
counts, queue high-water marks and wall time, never audio or source paths. The
log queue is bounded; `maximum_file_bytes` and `retained_files` control rotation.
A write failure disables file logging without stopping audio. CPU/RSS/temperature
still require external measurement; wall time is not CPU time.

## Standalone native build and checks

Requirements: Python 3.9+, a C++20 compiler, make, CMake and curl; on Linux, the
usual build-essential/ca-certificates/bzip2 packages. No app Vendor tree is needed.

```sh
python3 scripts/build_dependencies.py --scratch /tmp/csad-dependencies
cmake -S . -B /tmp/csad-build -DCSAD_DEPS=/tmp/csad-dependencies/deps -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/csad-build -j2
/tmp/csad-build/cinemaseat-audio-decoder --settings config/settings.example.json
```

Run independent checks in a second terminal:

```sh
python3 scripts/run_checks.py --host --scratch /tmp/csad-host-checks
python3 scripts/run_checks.py --all --scratch /tmp/csad-checks
```

`--host` runs native/protocol/codec/network tests. `--all` also checks available
Docker tooling and reports missing mandatory evidence with a non-zero exit.
Neither runner installs Docker. Host success is not full acceptance. Logs and
`checks.json` stay in scratch. Generated audio-packet fixtures are test input only,
not report contents. The Docker image intentionally contains no Python test runner.

## Current limitations and acceptance

- Host-proven: DTS core stereo, TrueHD stereo, MLP stereo and MLP 5.1(back), exact
  PCM roundtrip, streaming STREAMINFO, 4096-sample FLAC blocks and exact tail,
  source anchors, missing PTS after anchoring, negative PTS and skip samples.
- Only canonical FLAC layouts are accepted. Supported native side/back layouts retain their explicit speaker identities.
  Float, custom/height layouts and midstream format changes fail explicitly.
- DTS uses the pinned fixed-point reference decode. Lossy XLL recovery is rejected;
  this can block multichannel DTS-HD startup. No DTS-HD MA support claim is made
  until representative fixtures pass.
- Bounded leading-fragment recovery is unproven. Corrupt priming fails
  conservatively. TrueHD without a major sync needs preroll from the future CS
  reader; arbitrary decode errors must not be ignored.
- Still unproven: both native Linux images, DTS-HD MA/7.1, physical audio/lip sync/
  HDR/DV, preheader-to-fMP4-to-AVPlayerItem, one-hour realtime on both devices,
  seeks, track switches and concurrent-session capacity.
- The CS transport/native bridge, Diagnose evidence contract and production UI,
  route policy and backup format 6 remain open. Existing app behavior is unchanged.

Procedure and criteria: [technical design](TECHNICAL_DESIGN.md#10-test-and-acceptance-matrix).
New tvOS media experiments must remain source-gated in the existing Diagnose app.
Component success never establishes audible or visible presentation.

## Dockge references

- [Official installation and existing-stack discovery](https://github.com/louislam/dockge#can-i-manage-existing-stacks)
- [Official stacks-directory mount configuration](https://github.com/louislam/dockge/blob/master/compose.yaml)
- [Accepted Compose filenames in Dockge](https://github.com/louislam/dockge/blob/master/common/util-common.ts)

## Remote codec verification

See [REMOTE_TESTS.md](REMOTE_TESTS.md) for the reusable RAM-only audio checks and
[ODROID evidence](evidence/ODROID_2026-11-01.md) for actual target observations.
Version 0.1.2 was first tested in an isolated native arm64 container and then
deployed to the existing Dockge stack with explicit user approval. Settings were
preserved and eight public-endpoint PCM checks passed after the update. Version
0.1.3 adds host diagnostics and has not been deployed. Building from current main
uses the 0.1.3 image tag; updating an existing device is a separate action.
