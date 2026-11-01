# Remote codec checks

These reusable host checks send only selected compressed audio to the service,
decode returned FLAC in host RAM and compare it with the pinned reference decoder.
They do not play video/audio on Apple TV and do not establish perceptual acceptance.
They are host regression tools, not a separate tvOS diagnostic app. Any later
playback experiment must be source-gated in the existing Diagnose app with its
required report/schema/validator/UI/analysis-guide changes.

## Build the host tools

```sh
python3 scripts/build_dependencies.py --scratch /tmp/csad-dependencies --test-codecs
cmake -S . -B /tmp/csad-host -DCSAD_DEPS=/tmp/csad-dependencies/test-deps -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/csad-host -j2
/tmp/csad-host/core_tests tests/fixtures /tmp/csad-generated
```

Production dependencies still contain only dca/truehd/mlp decoders and the flac
encoder. The separate host build adds reference/test codecs, libavformat and
local-file demuxers/parsers. No runtime file access or extra production decoder
is authorized by this host tooling.

## Synthetic fixtures

```sh
python3 scripts/remote_checks.py --host <service-host> --fixture-dir /tmp/csad-generated --validator /tmp/csad-host/core_tests --sessions 1 --output /tmp/csad-remote-one.json
```

Repeat with `--sessions 2` or `--sessions 4` for short independent concurrency
checks. The service must have available capacity. Results include restored session
slots and per-session wall time/first output time. Fixture lengths are short;
throughput multipliers do not establish long-session capacity or thermal stability.

An older service may explicitly reject newer fixture layouts. Preserve that
failure; select an explicitly documented subset only when measuring older supported
behavior. Never label that subset as complete acceptance.

## Explicit authorized local source

```sh
python3 scripts/remote_checks.py --host <service-host> --source /absolute/path/to/authorized-source.mkv --audio-ordinal 0 --seconds 5 --exporter /tmp/csad-host/fixture_export --validator /tmp/csad-host/core_tests --output /tmp/csad-source-result.json
```

Source extraction is explicitly gated on DTS/TrueHD/MLP and a selected audio ordinal.
Only local-file access is enabled; no source URL or filename is sent to the service.
The reader has a 30-second deadline, bounded probing, at most 4096 selected packets,
a 24 MiB packet budget and a requested duration of 1–15 seconds. No video is sent.
Decode-affecting new extradata/parameter changes and corrupt/discard packets fail.
Skip-sample counts are preserved. Production remains unchanged by the extractor.

The extractor's stdout is transient fixture data passed to the test client in
RAM. Never redirect that stream into a report, logfile or Git. The checker keeps
returned compressed audio in a bounded 32 MiB observation buffer, and sends it to
the native validator through stdin with a 64 MiB request cap. Canonical decoded
PCM is bounded in the validator and is not written to disk.

The normal source test always retains original timestamps and the strict service
timeline policy. If a source exceeds tolerance, it fails. No automatic retiming,
missing-PTS substitution, sample dropping, gap filling or downgrade is allowed.

## Evidence contract

Stable host criterion IDs:

- `external_audio.remote_codec.v1`: execution, service build/revision, capacity,
  fixture coverage and per-session measurements.
- `external_audio.host_pcm_equivalence.v1`: equal canonical signed 32-bit
  little-endian PCM hashes, valid sample count, explicit speaker order, rate,
  precision and absolute first/end PTS.
- `external_audio.decoder_policy.v1`: fixed-marker XLL recovery observations,
  bounded full-HD format inspection and explicit missing leading coverage.
- `external_audio.host_source_timeline.v1`: independent reference format/PCM hash,
  original time base, aggregate timestamp deviation and bounded frame trace.

Reports contain derived properties only: source/fixture hashes, named codec/profile,
counts, format/layout, decimal timestamps, fixed error codes and missing acceptance.
No endpoint credentials, source paths, packet payloads, FLAC blocks or decoded media
are included. A validator binary SHA identifies the actual host checker.
The first 32 reference frames may be traced; `trace_truncated` explicitly records
missing trace coverage, while aggregate frame counts and maximum deviations cover
the bounded decoded fixture. A native validation failure includes only a fixed code
and optional numeric check line, never raw JSON/FFmpeg errors.

Partial output format, received packet/sample counts and incomplete coverage are
retained even if the service fails before end_output. Secondary producer errors
during teardown must not replace the original service error code.

The validator restores explicit `channel_order` before FLAC decoding because
STREAMINFO carries channel count, not side/back speaker identity. Tests deliberately
remove or change this metadata and corrupt returned audio/CRC/timeline; these must
fail. Decoder precision changes and real midstream TrueHD rate changes are rejected.

`physical_acceptance` is always false. Missing work includes one-hour continuous
realtime, CS preheader/fMP4/AVPlayerItem, physical channels/lip sync/HDR/DV, seeks,
track changes and measured CPU/RSS/thermals. Source-reference decode success is not
service timing success; these outcomes must remain separate.

## Credit pause and failure preservation

```sh
python3 scripts/pause_check.py --host <service-host> --fixture /tmp/csad-generated/dts-6ch.json --seconds 35 --output /tmp/csad-pause.json
```

This synthetic-only test streams valid repeated packets, deliberately grants no
new output credit, continues processing WebSocket pings and then cancels. It
reports bounded bytes, observed producer credit wait, ping count and cancellation
latency under stable criterion `external_audio.credit_pause_cancel.v1`. It does
not render media. The local runner uses a shorter equivalent with a shorter ping
interval. Primary service errors must survive secondary producer broken-pipe
errors during teardown; partial format/count evidence is retained without payloads.

Host source inspection also records up to 64 demux-position groups, with input
packet counts and original first/last PTS. `demux_groups_truncated` states missing
coverage. These values are evidence of container grouping, not permission to
retime packets or ignore known timestamps in production.

## Rejected XLL startup: metadata only

`core_tests --inspect-source` records a count when the pinned decoder requests
XLL recovery. Only the exact fixed upstream marker is recognized; raw decoder
logs are never formatted or retained. The regular checker preserves this sanitized
failure and initial probe hints even when reference decoding stops before any
service session. Probe hints can describe a core and must not be mistaken for the
full-HD layout.

For an explicitly authorized local DTS source, the existing RAM-only extractor
may feed `core_tests --inspect-after-recovery` through a subprocess pipe. Never
redirect the extractor's stdout to disk. This diagnostic rejects the first recovery
packet and inspects subsequent noncorrupt full-HD frames solely for format
metadata. It permits at most one such initial rejection, checks the full HD-MA
profile and stable format, and stops at eight frames, 256 packets or two seconds
of source time. Invocation/input buffers retain the normal 64 MiB bound.

This is not a service playback policy. It reports `diagnostic_only`,
`source_acceptance: false`, `physical_acceptance: false` and whether leading
coverage is missing. It emits no PCM or compressed payloads. It does not prove
complete lossless playback, repair timestamps, trim requested audio or permit
lossy recovery. Any playback adaptation must first be justified and source-gated
in the existing Diagnose app with its full report contract.
