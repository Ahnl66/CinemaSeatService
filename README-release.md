# CinemaSeat Audio Decoder 0.1.6 — 2026-11-05

- Added CinemaSeat Decoder 0.1.1 for macOS. Opening the app starts the decoder and puts a waveform icon in the menu bar. Its English panel shows the address, uptime, service version and four live decoding sessions. **Stop and Quit** shuts down the service.
- Added downloadable Docker build files and service source for Docker Compose and Dockge, including the two-file Synology Container Manager setup.

## macOS

Download `CinemaSeat-Decoder-0.1.1-macOS-arm64.zip`. It requires Apple Silicon
and macOS 13 or later. Unzip it, move **CinemaSeat Decoder.app** to
**Applications**, and open it. The app includes the decoder and FFmpeg libraries.

This build is signed ad hoc. If macOS blocks it, an exception may be available
under **System Settings → Privacy & Security → Open Anyway** after the first
attempt. [Apple’s instructions](https://support.apple.com/en-us/102445)

## Docker

Download and extract `CinemaSeatService-0.1.6-docker.tar.gz` on a Docker host
with Compose V2. From the extracted directory run:

```sh
cp config/settings.example.json settings.json
docker compose build
docker compose up -d
curl --fail http://localhost:9078/healthz
```

The initial build needs internet access for the base image and pinned
dependencies. The archive includes `docker-compose.yml` for Dockge and
`docker/synology/` for [Synology Container Manager](https://github.com/Ahnl66/CinemaSeatService/blob/v0.1.6/docs/SYNOLOGY.md).

## Connect CinemaSeat Standard

Enable **Use external audio decoder** in player defaults and enter the host's
LAN address and port **9078**. On macOS, the address appears in the menu bar
panel. Keep the host awake and the endpoint on a trusted local network.

The release also includes `FFmpeg-source-0.1.6.tar.gz` with the exact patched
FFmpeg source used to build the bundled libraries.
