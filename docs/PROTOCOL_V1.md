# CinemaSeat Audio Decoder — wire protocol v1

Normative contract dated 2026-11-01. The native server is partially host-proven;
the CS client is not implemented. See [implementation status](IMPLEMENTATION_STATUS.md).
The protocol major is unchanged by the documentation translation. MUST and MUST
NOT express requirements. Deviations require an explicit protocol change.

Publish identical wire fixtures in the service and client repositories. This
protocol never carries filenames, source URLs, server authentication or video.

## 1. Endpoints and WebSocket

- `GET /healthz`: HTTP 200 JSON `{"status":"ok","protocol":1}` while the event
  loop responds and startup configuration/codec self-checks have passed. No
  session details are exposed.
- `GET /v1/capabilities`: HTTP 200 JSON containing `protocol`, `service_version`,
  `build_id`, `ffmpeg_revision`, `input_codecs:["dts","truehd","mlp"]`,
  `output_codecs:["flac"]`, effective limits and `available_sessions`.
- `GET /v1/session`: RFC6455 upgrade with subprotocol `cs-audio.v1`. Wrong
  subprotocol/version returns HTTP 400/426; exhausted capacity returns HTTP 503
  with fixed code `capacity`. Each upgrade reserves one session slot.
- Do not follow redirects or use cookies, credentials, wildcard CORS or general
  web resources. Reject browser `Origin` headers; native CS sends none. Disable
  permessage-deflate; compressed audio should not be recompressed by WebSocket.
- IPv4 by default; an IPv6 listen address can be configured explicitly. Publish
  one TCP port.
- Maximum HTTP header: 16 KiB; control message: 64 KiB; audio payload: 1 MiB.
  Set the WebSocket message parser to at most `1 MiB + 64 bytes`. Frame
  fragmentation MUST NOT bypass the assembled-message limit.
- Unknown paths/methods return 404/405 without a framework stack trace.

HTTP response bodies are at most 64 KiB. The app checks both Content-Length and
actual streamed bytes. Text messages MUST be UTF-8 JSON objects, with no duplicate
keys, NaN or Infinity and at most eight nesting levels. Unknown optional keys may
be ignored. An unknown message type or unknown required feature is `protocol_error`.

## 2. Session lifecycle

```text
upgrade → WAIT_START → PRIMING → RUNNING → DRAINING → ENDED → CLOSED
                         └──────── error/cancel/disconnect ─────→ CLOSED
```

After upgrade the service sends `hello` and expects `start` within five seconds.
There is exactly one start per connection, one input track, one output codec and
one session generation. Heartbeat operates in every non-closed state.

`hello`, service to client:

```json
{
  "type":"hello", "protocol":1,
  "session_id":"random-uuid", "service_version":"0.1.2",
  "limits":{"packet_payload_bytes":1048576,"control_message_bytes":65536},
  "input_limit_bytes":"4194304"
}
```

`session_id` is temporary technical correlation, not a device identity. All
unsigned 64-bit counters and signed 64-bit timestamps in JSON are **decimal
strings**, preventing JSON-double precision loss. Other bounded small integers
are JSON integers. No locale separators, leading plus sign or whitespace.

`start`, client to service:

```json
{
  "type":"start", "protocol":1, "generation":"1",
  "input":{
    "codec":"truehd",
    "time_base":{"num":1,"den":1000},
    "extradata_base64":"",
    "sample_rate_hint":48000,
    "bits_per_sample_hint":24,
    "channel_order_hint":["FL","FR","FC","LFE","BL","BR","SL","SR"]
  },
  "output":{"codec":"flac"},
  "timeline":{"audible_from_us":"0"},
  "output_limit_bytes":"4194304"
}
```

Hints may be null; unknown does not imply stereo or 48 kHz. A known channel order
is an ordered list of FFmpeg/FLAC-supported speaker names, not a local bitmask
enum. See format validation below. Decoded extradata is at most 64 KiB and gets
FFmpeg input padding. Time-base numerator/denominator are positive and at most
one billion. `generation` is 1 through INT64_MAX and unique for each new client
playback generation. `audible_from_us` is the desired source start, not a bias.

The service validates codec/start configuration, opens the decoder and replies
`started` with the generation. Only then may the client send binary input. The
service cannot promise FLAC parameters until actual frames are decoded.

## 3. Binary packet messages

One binary WebSocket message contains one 64-byte header and one complete
demuxed codec-packet payload. No batching inside a v1 message. Separate messages
may be sent consecutively without waiting for a per-packet response; TCP may
coalesce writes. All integers use network byte order (big endian); signed values
use two's complement. Parse explicit loads, never cast a wire buffer to a C struct.

| Offset | Bytes | Field | Contract |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `CSAD` |
| 4 | 1 | version | 1 |
| 5 | 1 | kind | 1=input, 2=output |
| 6 | 2 | flags | bit0 PTS-valid, bit1 DTS-valid, bit2 key; all other bits zero |
| 8 | 8 | generation | Unsigned; equal to the start generation |
| 16 | 8 | sequence | Unsigned; starts at 0 independently in each direction, increases exactly by 1 |
| 24 | 8 | pts | Signed, in the direction's time base; zero if flag is off |
| 32 | 8 | dts | Signed; same rules as pts |
| 40 | 8 | duration | Signed, >=0; input zero means unknown, output is an exact valid sample count >0 |
| 48 | 4 | payload_bytes | Unsigned, 1 through the effective payload limit |
| 52 | 4 | skip_start | Unsigned input decoder skip-sample count |
| 56 | 4 | skip_end | Unsigned input discard-padding count |
| 60 | 4 | reserved | Zero |

Message length MUST equal `64 + payload_bytes`. Reject length/sequence errors
before allocation/decode. Input MUST NOT contain AVPacket CORRUPT/DISCARD flags;
the client handles them explicitly as unusable input. The key flag is informative.
Copy/reference packet payloads with `AV_INPUT_BUFFER_PADDING_SIZE` zero padding.

Input side-data allowlist: transport `AV_PKT_DATA_SKIP_SAMPLES` as skip_start/end,
with v1 reason bytes set to zero. Reconstruct FFmpeg side data only when either
count is nonzero. Apply documented FFmpeg trimming exactly once. Streaming
`NEW_EXTRADATA`, `PARAM_CHANGE` or other decode-affecting side data results in
`format_changed`/`unsupported_side_data`. Do not transport arbitrary AVPacket
side-data structs or enums. Irrelevant container/presentation side data stays in CS.

Output consists of FLAC codec frames, not a FLAC file or container. Both PTS and
DTS are mandatory and equal. Duration counts valid samples. skip_start/end are
always zero because output is already trimmed. Encoder padding MUST NOT be
reported as extra valid audio. Encode the FIFO tail exactly or fail explicitly.

Golden vector for a minimal input header:

```text
magic CSAD, version 1, kind 1, flags 0003
generation 1, sequence 0, pts -1, dts -1, duration 1024
payload_bytes 3, skip_start 0, skip_end 0, reserved 0, payload 01 02 03
```

The exact 67-byte hex fixture is
[`tests/fixtures/input-negative-pts.hex`](../tests/fixtures/input-negative-pts.hex).
Share it with Swift/C++ clients. Negative times must not become UINT64 or
JSON-double values accidentally.

## 4. Output format and absolute timeline

The service sends exactly one `output_format` before its first output packet:

```json
{
  "type":"output_format", "generation":"1", "codec":"flac",
  "sample_rate":48000, "bits_per_sample":24,
  "channel_order":["FL","FR","FC","LFE","BL","BR","SL","SR"],
  "time_base":{"num":1,"den":48000},
  "extradata_base64":"...",
  "source_profile":"truehd", "object_metadata_preserved":false
}
```

Wire codec v1 defines extradata as FFmpeg FLAC codec parameters: a raw **34-byte
STREAMINFO body**, without a `fLaC` marker or four-byte metadata-block header.
Normalize/validate this at the service boundary rather than copying a
backend-specific header blindly. STREAMINFO sample rate, channel count and
significant bits MUST agree with JSON. Total samples and MD5 may be unknown for
streaming. The client uses these parameters to create AVCodecParameters for its
existing fMP4 muxer.

Map the actual decoder layout to FLAC's fixed channel order and report it
explicitly. Only representable layouts are allowed; channel count alone is not
enough. STREAMINFO does not distinguish side from back speaker identities. A client
MUST restore the explicit `channel_order` in its decoder/mux codec parameters;
it must not infer speaker positions from the STREAMINFO channel count. Supported
native side/back variants retain their speaker identities without relabeling. A deterministic permutation is allowed if channel-impulse fixtures prove
it. Never use a mix matrix or default to stereo.

Timeline contract:

- Input pts/dts use the original stream time base, including negative times.
- Use decoded `AVFrame.pts`/best-effort timestamp and valid sample counts to anchor
  the first valid sample to source media. Configure pkt_timebase.
- Output `pts = av_rescale_q(first_valid_source_timestamp, input_tb, {1,rate})`,
  then advance by exactly the number of valid samples. Keep absolute source time.
- Round only at the first anchor, not once per input packet; otherwise drift accumulates.
- After a proven anchor, missing PTS may be reconstructed by counting samples.
  Without a reliable first anchor before the format deadline, fail `missing_timeline`.
- For each new reliable timestamp, compare the expected next sample time. A
  difference greater than `max(2 samples, one input tick rescaled to samples)` is
  `timeline_discontinuity`. No silent gap filling or drops.
- Frames before `audible_from_us` may be output as timestamped preroll. The
  service does not cut them off at packet boundaries; CS controls visible start.
- Existing source-origin, video timestamp bias and AVPlayer seek are applied
  only in CS, exactly once. FLAC PTS must not restart at zero when video already
  uses an absolute seek position.
- Fixtures must establish how `initial_padding`/skip-sample effects locate the
  first valid anchor. Do not add a heuristic constant delay.

If a backend cannot establish a reliable sample timeline, the variant fails.
Do not weaken the check to label a source playable. Decoding with incorrect
timing does not satisfy this contract.

## 5. Credit flow and bounded buffering

Each direction has an independent cumulative byte budget. A sent binary message
costs exactly header+payload bytes; JSON/control/ping messages do not consume it.

- The service advertises its input limit in hello; the client advertises its
  output limit in start.
- A producer starts `sent_bytes` at zero. It may send only if
  `sent_bytes + message_size <= advertised_limit_bytes`, with overflow checks.
- A consumer tracks actually consumed bytes and advertises the new absolute
  ceiling `consumed_bytes + window_bytes` through `credit`.
- Input is consumed when the codec worker owns the packet and the network queue
  buffer is free. If the worker retains it, count it against the codec queue cap.
- Output is consumed when CS admits an owned packet into its bounded native mux
  queue. If that queue is full, no new credit may be granted.
- Credit limits are monotonic. A smaller limit is `protocol_error`. Counter
  overflow causes an explicit session error, never wrapping.

```json
{"type":"credit","direction":"input","limit_bytes":"8388608"}
```

`direction` names the direction the receiver may produce: the service grants
input credit and the client grants output credit. Update after releasing roughly
one quarter of the window, or after 100 ms for smaller releases. Coalesce credit
updates rather than growing control queues indefinitely. Count output/in-flight
buffers against queue caps; URLSession send tasks must not hide another queue.

Send and receive loops operate independently. A producer may wait while the
consumer handles output, errors and cancellation. Initial format/control traffic
never waits for audio credit. The codec worker may wait for output capacity;
socket read/control handling MUST NOT. Frames may be decoded after start before
output credit is spent, but encoder output remains bounded.

Pause: CS stops producing input at its playback-buffer budget. No protocol pause
message is needed. Heartbeats continue and queues do not grow. Do not close a
healthy paused connection for data idleness. Measure starvation without progress
only when both work and credit are available.

## 6. EOF, cancellation, errors and heartbeat

The client sends `end_input` only after all input messages have been sent in order:

```json
{"type":"end_input","generation":"1","input_packets":"1250"}
```

`input_packets` MUST match the received count. No more input follows. The service
drains and, after all output packets, sends `end_output` with input/output packet
counts, valid total sample count, first_pts and end_pts_exclusive. It must not
overtake output. Counters establish integrity, not codec quality. The client
validates total samples/contiguity and closes with code 1000. A drain timeout
runs only for codec work with output credit; heartbeat detects a missing consumer.
No file-header fixup or audio temporary file is used.

The client may send `{"type":"cancel","generation":"1"}` or close the socket
at any time. Cancellation discards buffers without EOF draining. If possible the
service replies `cancelled` and closes with 1000. Seek/track switch creates a new
socket. Late callbacks from an old generation never affect new playback.

Error object:

```json
{
  "type":"error", "generation":"1", "code":"unsupported_format",
  "retryable":false, "message":"Selected audio format is not supported."
}
```

Fixed service codes: `protocol_error`, `unsupported_version`, `unsupported_codec`,
`unsupported_format`, `unsupported_side_data`, `format_changed`, `invalid_packet`,
`decode_failed`, `encode_failed`, `missing_timeline`, `timeline_discontinuity`,
`limit_exceeded`, `timeout`, `server_shutdown`, `internal_error`.
Client-local codes: `unreachable`, `capacity`, `format_probe_limit`, `cancelled`,
`output_validation_failed`.

Retryable is advisory only. CS route policy determines one permitted fallback;
there are no autonomous service retries. A public message is fixed and
translatable, not a raw FFmpeg error, input dump, local path or stack trace.

Close codes: protocol error 1002, oversized payload 1009, normal EOF/cancel 1000,
internal error 1011. Send an error before closing if transport state allows it;
unusable transport tears down directly. Do not reconnect within a session.
The client bounds handshake/send/receive waits and their cancellation.

The service sends a WebSocket ping every 15 seconds with a 10-second pong deadline.
Use Beast/URLSession RFC6455 facilities, not custom audio heartbeat messages.
A missing pong also closes a paused session. Health monitoring opens no conversion
session and consumes no conversion slot.

## 7. Required interoperability fixtures

1. Start, null hints, LE/BE header parsing, negative PTS, 64-bit limits,
   fragmented WebSocket messages and payload-length mismatch.
2. Wrong sequence/generation/type, duplicate JSON keys, unknown type and format
   received after data.
3. No remaining output credit with working control/cancel, full input queue,
   overflow-safe credit limits, paused reader and a peer ignoring its budget.
4. Decoder producing zero/two frames, delayed encoder packet, end_input with a
   FIFO tail, cancellation during priming and drain.
5. STREAMINFO body versus header, 5.1/7.1 channel order, changed sample rate,
   skip samples, missing timestamps, nonzero seek start and discontinuity.
6. App output through fMP4 into an actual Apple player item; JSON alone is not proof.

Protocol major v1 changes only through an explicit version bump. Service SemVer
and app version can advance independently while wire compatibility is retained.
