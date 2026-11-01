# 0.1.1 — 2026-11-01 (development snapshot)

- Added a root `docker-compose.yml` and installation instructions for Dockge on
  the ODROID-N2+ with Armbian, using a local build and read-only settings.
- Converted all repository documentation to English.
- Docker images, DTS-HD MA, hardware benchmarks and CinemaSeat integration remain
  unaccepted. This is a private source snapshot, not a validated release.

# CinemaSeat Audio Decoder

A standalone headless C++20 service that converts demuxed DTS/TrueHD/MLP packets
into FLAC. Audio stays in RAM. Development version **0.1.1**.

**Status: working native service and host tests; partial acceptance only.**
CinemaSeat production integration and the Diagnose route are not implemented.
Native amd64/arm64 Docker builds, DTS-HD MA and physical acceptance remain open.

- [Install with Dockge on the ODROID](docs/DEPLOYMENT.md#dockge-on-the-odroid-n2)
- [Root Compose file](docker-compose.yml)
- [Implementation status and evidence matrix](docs/IMPLEMENTATION_STATUS.md)
- [Host evidence](docs/evidence/HOST_2026-11-01.md)
- [Implementation baseline](docs/BASELINE.md)
- [Technical design and phases](docs/TECHNICAL_DESIGN.md)
- [Normative protocol v1](docs/PROTOCOL_V1.md)
- [Implementation handoff](docs/SOL_HANDOFF.md)
- [Changelog](CHANGELOG.md) and [dependency notices](THIRD_PARTY_NOTICES.md)

## Dockge quick start

Run these commands on the ODROID, using its existing GitHub access. Replace
`/opt/stacks` if Dockge uses a different stacks directory. Use your normal account
with access to that directory and the Docker daemon.

```sh
cd /opt/stacks
git clone https://github.com/Ahnl66/CinemaSeatService.git cinemaseat-service
cd cinemaseat-service
cp config/settings.example.json settings.json
chmod 644 settings.json
docker compose -f docker-compose.yml config
docker compose -f docker-compose.yml build
```

In Dockge, choose **Scan Stacks Folder**, open `cinemaseat-service`, and deploy it.
Dockge recognizes `docker-compose.yml`. The image is built locally; no published
image or private Git credentials inside Dockge are required. `pull_policy: never`
keeps deployment local, so the initial build above is required. See the full
[deployment guide](docs/DEPLOYMENT.md) for health checks and updates.

## Standalone checks

```sh
python3 scripts/run_checks.py --host --scratch /tmp/csad-host-checks
python3 scripts/run_checks.py --all --scratch /tmp/csad-all-checks
```

`--all` returns a non-zero status while required evidence is missing. The scripts
never install Docker. Docker must not be installed on the development Mac.

The service does not read source files, URLs, NAS paths, credentials, video or a
media library. It has no accounts or UI. Only settings and optional rotated
metadata logs use disk. Audio is never spooled. There is no AAC, downmix or
resampling route. Fixed-point DTS and integer FLAC packing are compared against
reference PCM; unsupported or unproven formats fail explicitly.

## Repository and version policy

Service code, containers, fixtures and standalone tests live in this repository.
The build fetches hash-verified dependencies independently and does not use the
CinemaSeat app or its Vendor directory. App and service have independent Git
history and version numbers. Service SemVer, protocol major and app version are
separate; the app version is not changed by a service-only commit.

This repository is private. Public releases, project licensing, published images
and device installation require separate instructions. No GitHub Actions workflow
is included or dispatched. Keep runtime settings, logs, build outputs and private
media outside Git. All repository documentation is maintained in English.
