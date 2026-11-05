# CinemaSeat Decoder for macOS

CinemaSeat Decoder is a standalone macOS 13+ menu bar app for CinemaSeat Standard.
Opening the app starts the native CinemaSeatService decoder. Click the waveform
icon in the menu bar to see the host and port, uptime, service version and
activity in the first four session slots. Active rows show the detected input
codec and channel count, followed by the output codec. **Stop and Quit** shuts down the decoder and
removes the icon. Closing the panel leaves the decoder running. Open the app
again to restart it.

## Build on the Mac

Use Xcode, CMake and Python 3.9 or later. The first preparation step needs
network access to fetch the hash-verified dependencies for this service checkout.

```sh
cd macOS/CinemaSeatDecoder
./prepare-decoder.sh
xcodebuild -project CinemaSeatDecoder.xcodeproj -scheme CinemaSeatDecoder \
  -configuration Release -destination 'platform=macOS' -derivedDataPath "$PWD/build/xcode" build
open "build/xcode/Build/Products/Release/CinemaSeat Decoder.app"
```

After preparation you can also open `CinemaSeatDecoder.xcodeproj` and run the
shared scheme in Xcode. The build is native to the build Mac's architecture
and uses local ad hoc signing by default. The app bundles the decoder and its
FFmpeg libraries. Docker, Homebrew FFmpeg and a separate service installation
are unnecessary at runtime.

Run `./prepare-decoder.sh` again after changes to the Mac helper. This step
adds session activity to local capability requests in the bundled decoder.

## Connect from CinemaSeat Standard

Enable **Use external audio decoder** in the player defaults. Enter the Mac's
address shown in the panel, or its LAN IP address, and port **9078**. Keep the
Mac awake while decoding. The default configuration listens on all IPv4
interfaces. Use it on a trusted local network and allow incoming connections
if the macOS firewall asks. The decoder protocol has no authentication or TLS.

The app creates `~/Library/Application Support/CinemaSeat Decoder/settings.json`
on first launch. Stop the app before editing settings, then reopen it to apply
the changes. The file follows the existing service settings format. For
isolated testing, set `CINEMASEAT_DECODER_SETTINGS` to the absolute path of an
existing settings file.
