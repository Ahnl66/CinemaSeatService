# CinemaSeat Audio Decoder — wireprotocol v1

Status: normatief v1-contract, 2026-11-01. Native server gedeeltelijk hostbewezen;
CS-client ontbreekt nog. Zie IMPLEMENTATION_STATUS.md; protocolmajor ongewijzigd.
MUST/MOET zijn vereisten; afwijkingen vereisen expliciete protocolwijziging.
Wirefixtures worden in beide repositories identiek gepubliceerd. Dit protocol
bevat nooit bestandsnamen, bron-URL's, serverauthenticatie of videopakketten.

## 1. Endpoints en WebSocket

- `GET /healthz`: 200 JSON `{"status":"ok","protocol":1}` zolang eventloop
  reageert en startupconfig/codecselfcheck is geslaagd. Geen sessiedetails.
- `GET /v1/capabilities`: 200 JSON met `protocol`, `service_version`, `build_id`,
  `ffmpeg_revision`, `input_codecs:["dts","truehd","mlp"]`,
  `output_codecs:["flac"]`, effectieve limieten en `available_sessions`.
- `GET /v1/session`: RFC6455-upgrade met subprotocol `cs-audio.v1`.
  Verkeerd subprotocol/versie → HTTP 400/426; capaciteit vol → HTTP 503 met
  vaste foutcode `capacity`. Eén upgrade reserveert één sessieslot.
- Geen redirects volgen, cookies, credentials, CORS-wildcards of algemene
  webresources. Reject browser `Origin`-headers (native CS stuurt geen Origin).
  Geen permessage-deflate; gecomprimeerde audio opnieuw comprimeren is onnodig.
- IPv4 default, expliciet configureerbaar IPv6-listenadres. Eén gepubliceerde poort.
- Maximaal 16 KiB HTTP-header, 64 KiB control message, 1 MiB audiopayload. Stel
  tevens de WebSocket-messageparser in op `1 MiB + 64 bytes`; fragmentatie van
  WebSocket-frames mag de messagegrens niet omzeilen.
- Niet-bekende paden/methoden → 404/405, zonder frameworkstacktrace.

HTTP-antwoordbody maximaal 64 KiB. App controleert Content-Length én streamed
bytes. Alleen JSON-objecten voor tekstberichten; UTF-8, geen duplicate keys,
geen NaN/Infinity, maximaal nestingdiepte 8. Unknown optional keys mogen genegeerd;
unknown message type of onbekend verplicht feature geeft `protocol_error`.

## 2. Session lifecycle

```text
upgrade → WAIT_START → PRIMING → RUNNING → DRAINING → ENDED → CLOSED
                         └──────── error/cancel/disconnect ─────→ CLOSED
```

Na upgrade stuurt service `hello`, daarna verwacht hij `start` binnen vijf
seconden. Er is precies één start per verbinding, één inputtrack, één outputcodec
en één sessiegeneratie. Heartbeat werkt in alle niet-gesloten toestanden.

`hello`:

```json
{
  "type":"hello", "protocol":1,
  "session_id":"random-uuid", "service_version":"0.1.0",
  "limits":{"packet_payload_bytes":1048576,"control_message_bytes":65536},
  "input_limit_bytes":"4194304"
}
```

`session_id` is tijdelijk, voor technische correlatie; geen device identity.
Alle unsigned 64-bit counters en signed 64-bit timestamps in JSON zijn **decimale
strings**, om verlies via JSON-double te voorkomen. Overige begrensde kleine
integers zijn gewone JSON-integers. Geen locale separators/leading plus/whitespace.

`start`, client → service:

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

Hints mogen null zijn; onbekend is niet stereo/48k. Een bekende channel order is
een geordende lijst namen uit FFmpeg/FLAC-ondersteunde speakerposities; geen lokale
bitmaskenum op de wire. Zie formatvalidatie verderop. Extradata maximaal 64 KiB
gedecodeerd, met FFmpeg-padding toegewezen. Ratio num/den beide positief,
maximaal 1 miljard. `generation` 1..INT64_MAX en door client uniek per nieuwe
playbackgeneratie. `audible_from_us` is gewenste bronstart, geen timestampbias.

Service valideert codec en startconfig, opent decoder en antwoordt `started`
met generation. Daarna pas mag client binaire input sturen. Service kan nog geen
FLAC-parameters beloven totdat de eerste echte frames gedecodeerd zijn.

## 3. Binaire packetmessages

Eén binaire WebSocket-message = één header van 64 bytes + één volledige
gedemuxte codecpacketpayload. Geen batch binnen een message in v1; afzonderlijke
messages mogen zonder request/responsewacht direct na elkaar worden verzonden.
Transport kan TCP-writes samenvoegen. Alle integers network byte order (big endian),
signed values two's complement. Parse met expliciete loads, niet C struct casting.

| Offset | Bytes | Veld | Contract |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `CSAD` |
| 4 | 1 | version | 1 |
| 5 | 1 | kind | 1=input, 2=output |
| 6 | 2 | flags | bit0 PTS-valid, bit1 DTS-valid, bit2 key; overige bits nul |
| 8 | 8 | generation | unsigned, moet gelijk zijn aan start |
| 16 | 8 | sequence | unsigned, per richting vanaf 0, exact +1 |
| 24 | 8 | pts | signed, in richting-timebase; 0 als flag uit |
| 32 | 8 | dts | signed, idem |
| 40 | 8 | duration | signed, >=0; input 0=onbekend, output exact samples >0 |
| 48 | 4 | payload_bytes | unsigned, 1..effectieve payloadlimiet |
| 52 | 4 | skip_start | unsigned; input decoder skip-samples count |
| 56 | 4 | skip_end | unsigned; input discard-padding count |
| 60 | 4 | reserved | nul |

Message length MOET exact 64+payload_bytes zijn. Reject lengte-/sequencefouten
vóór allocatie/decode. Invoer staat geen AVPacket flag CORRUPT/DISCARD toe; de
client moet dat expliciet als onbruikbare input afhandelen. Keyflag is informatief.
Packetpayload kopiëren/refcounten met `AV_INPUT_BUFFER_PADDING_SIZE` nulpadding.

Inputside-data allowlist: `AV_PKT_DATA_SKIP_SAMPLES` wordt als skip_start/end
vervoerd, redenen worden in v1 op nul gezet. Reconstructie naar FFmpeg gebeurt
uitsluitend indien niet nul. Gedocumenteerde FFmpeg-trimming wordt eenmaal
toegepast. `NEW_EXTRADATA`, `PARAM_CHANGE` of andere decode-beïnvloedende side
data tijdens streaming → `format_changed`/`unsupported_side_data`. Encapsuleer
geen willekeurige AVPacket-side-data-structs of enums in het netwerkprotocol.
Irrelevante container-/presentatieside-data blijft lokaal in CS.

Output: FLAC codecframes, geen FLAC-bestand of container. PTS en DTS zijn beide
verplicht en gelijk, duration is geldig sampleaantal. skip_start/end altijd nul:
output bevat reeds geldige samples. Encoderpadding mag niet als extra geldige
audio worden uitgegeven. FIFO-tail exact encoderen of expliciet falen.

Golden vector minimale inputheader:

```text
magic CSAD, version 1, kind 1, flags 0003
generation 1, sequence 0, pts -1, dts -1, duration 1024
payload_bytes 3, skip_start 0, skip_end 0, reserved 0, payload 01 02 03
```

Maak bij implementatie de exacte 67-byte hexfixture en deel deze met Swift/C++.
Negatieve tijden mogen niet per ongeluk UINT64 of JSON-double worden.

## 4. Outputformat en absolute tijdlijn

Service stuurt precies één `output_format` vóór het eerste outputpacket:

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

Wirecodec v1 definieert extradata als de FFmpeg FLAC-codecparameters: een raw
34-byte STREAMINFO-body, zonder `fLaC` marker of 4-byte metadata-blockheader.
Normaliseer/valideer op de servicegrens; kopieer geen backend-afhankelijke header
blind. Sample rate, channels en significant bits in STREAMINFO moeten overeenkomen
met de JSONvelden. Total samples en MD5 mogen streaming-onbekend zijn. De client
maakt hiermee AVCodecParameters voor de bestaande fMP4-muxer.

Channel order wordt uit de werkelijke decoderlayout naar de vaste FLAC-order
gemapt en expliciet vermeld. Alleen representabele layouts toestaan; count-only
is onvoldoende. Een deterministische kanaalpermutatie is toegestaan en wordt
door channelimpulsfixtures bewezen; nooit een mixmatrix of stereodefault.

Tijdcontract:

- Input pts/dts in oorspronkelijke streamtimebase, inclusief negatieve tijden.
- Gebruik decoded `AVFrame.pts`/best-effort timestamp en geldige samplecounts om
  eerste geldige sample aan bronmedia te koppelen. Configureer pkt_timebase.
- Output `pts = av_rescale_q(first_valid_source_timestamp, input_tb, {1,rate})`
  en daarna exact +aantal geldige samples; referentie blijft absolute brontijd.
- Afronding alleen bij het eerste anker, niet per inputpacket; anders ontstaat drift.
- Ontbrekende PTS mag na bewezen anker via samples worden doorgeteld. Zonder
  enig betrouwbaar eerste anker vóór de formatdeadline: `missing_timeline`.
- Bij nieuwe betrouwbare timestamp vergelijk je verwachte volgende sampletijd;
  verschil >max(2 samples, één inputtijdtick omgerekend naar samples) is een
  discontinuïteit en geeft `timeline_discontinuity`. Geen stille gapfill of drops.
- Frames vóór `audible_from_us` mogen als getimestampte preroll worden uitgevoerd;
  de service knipt die niet op packetgrenzen weg. CS bepaalt de zichtbare start.
- Bestaande source-origin, video-timestampbias en AVPlayer-seek worden uitsluitend
  in CS toegepast, exact eenmaal. Een FLAC-PTS mag niet nogmaals vanaf nul beginnen
  als de video al op een absolute seekpositie staat.
- `initial_padding`/skip_sample-effecten moeten via fixtures aantonen waar het
  eerste geldige anker ligt; geen heuristische constante delay toevoegen.

Indien backend geen betrouwbare sampletijdlijn kan bewijzen, faalt de variant.
Verlaag de check niet om een bron toch 'werkend' te noemen. Audio die wel decodeert
maar verkeerd getimed is, voldoet niet aan het contract.

## 5. Creditflow en begrensde buffering

Beide richtingen hebben een onafhankelijk cumulatief bytesbudget. Een verzonden
binary message kost exact header+payload; JSON/control/ping telt niet mee.

- Service adverteert inputlimiet in hello; client outputlimiet in start.
- Producer houdt `sent_bytes` bij vanaf nul. Hij mag een binary message pas sturen
  wanneer `sent_bytes + message_size <= advertised_limit_bytes` zonder overflow.
- Consumer houdt werkelijk geconsumeerde bytes bij en adverteert nieuw absoluut
  plafond `consumed_bytes + window_bytes` via `credit`.
- Voor input betekent consumed: packet heeft codecworkerownership bereikt en de
  netwerkqueuebuffer is vrij; als worker het nog bewaart telt dat in codecqueuecap.
- Voor output betekent consumed: CS heeft een owned packet toegelaten tot de
  begrensde native muxqueue; als die vol is mag geen nieuw credit worden gegeven.
- Monotone `credit`-limieten; een kleiner plafond is protocol_error. Counters
  overflow → expliciete sessiefout, nooit wraparound.

```json
{"type":"credit","direction":"input","limit_bytes":"8388608"}
```

`direction` is de richting die deze ontvanger mag produceren: service verleent
inputcredit, client outputcredit. Herhaal bij vrijgeven van circa kwart window,
of na 100 ms bij kleinere hoeveelheden. Creditwijzigingen worden samengevoegd,
niet onbeperkt in controlqueues gestapeld. Output/in-flight buffers tellen mee in
queuecaps; geen verborgen queue in URLSession-sendtasks.

Stuur- en ontvangloops werken onafhankelijk. Producer kan wachten terwijl
consumer outputs verwerkt, errors ontvangt en cancel uitvoert. Initial format
en controlverkeer wachten nooit op audiocredit. De codecworker mag op outputruimte
wachten, maar de socketread/controlhandler niet. Na start kan de service frames
decoderen vóór outputcredit gebruikt wordt; encoderoutput blijft begrensd.

Pause: CS stopt inputproductie bij zijn playbackbufferbudget. Geen extra protocol-
pause nodig. Heartbeats blijven lopen, queues groeien niet. Een gezonde gepauzeerde
verbinding wordt niet na een data-idle-timeout gesloten. Bufferstarvation zonder
progress wordt alleen actief gemeten wanneer er werk én credit beschikbaar is.

## 6. EOF, cancel, fouten en heartbeat

Client stuurt `end_input` pas nadat alle inputmessages geordend verzonden zijn:

```json
{"type":"end_input","generation":"1","input_packets":"1250"}
```

`input_packets` moet gelijk zijn aan ontvangen packetcount. Geen nieuwe input
hierna. Service draint en stuurt na alle outputs `end_output` met input/output-
packetcount, geldig totalsamplecount, first_pts en end_pts_exclusive. `end_output`
mag outputmessages niet inhalen. Counters dienen integriteitscontrole, geen
codec-kwaliteitbewijs. Client valideert totaalsamples/contiguïteit, sluit met 1000.
Drain-timeout loopt alleen voor codecwerk met outputcredit; heartbeat bewaakt een
weggevallen consumer. Geen fileheaderfixup of audiotempbestand.

Client kan op elk moment `{"type":"cancel","generation":"1"}` sturen of de
socket sluiten. Annulering discardt buffers, geen EOF-drain. Service antwoordt
indien mogelijk `cancelled` en sluit 1000. Seek/trackwissel maakt een nieuwe socket;
late callbacks van de oude generatie hebben geen effect op nieuwe playback.

Errorobject:

```json
{
  "type":"error", "generation":"1", "code":"unsupported_format",
  "retryable":false, "message":"Selected audio format is not supported."
}
```

Vaste codes: `protocol_error`, `unsupported_version`, `unsupported_codec`,
`unsupported_format`, `unsupported_side_data`, `format_changed`, `invalid_packet`,
`decode_failed`, `encode_failed`, `missing_timeline`, `timeline_discontinuity`,
`limit_exceeded`, `timeout`, `server_shutdown`, `internal_error`.
Clientlokale codes: `unreachable`, `capacity`, `format_probe_limit`,
`cancelled`, `output_validation_failed`.
Retryable is alleen adviserend; CS-routepolicy bepaalt één toegestane fallback,
geen autonome service-retries. Public message is een vaste vertaalbare boodschap,
geen FFmpeg raw error, inputdump, lokale pathname of stacktrace.

Closecodes: protocolfout 1002, payload te groot 1009, normale EOF/cancel 1000,
interne fout 1011. Eerst error sturen indien de transportstate dat veilig toelaat,
daarna close. Bij onbruikbare transport direct teardown. Geen reconnect in
dezelfde sessie. Client stelt eigen bounded handshake/send/receive-cancellation in.

Service verstuurt WebSocket ping elke 15 s met pongdeadline 10 s. Gebruik de
RFC6455-faciliteiten van Beast/URLSession, geen zelfbedachte audioheartbeats.
Een uitblijvende pong sluit ook een pauzesessie. Healthmonitoring opent geen
conversiesessie en neemt geen maxsessieslot in.

## 7. Verplichte interopfixtures

1. Start, null hints, LE/BE-headerparsing, negatieve PTS, 64-bit grenzen,
   gefragmenteerde WebSocket-message en payloadlengtemismatch.
2. Verkeerde sequence/generation/type/JSONduplicate/unknowntype en format na data.
3. Nul outputcredit met functionerende control/cancel, volle inputqueue,
   creditlimiet zonder overflow, paused reader en peer die budget negeert.
4. Decoder produceert nul/twee frames, encoder produceert uitgesteld packet,
   end_input met open FIFO-tail, cancel tijdens priming en drain.
5. STREAMINFO/body versus header, channelorder 5.1/7.1, gewijzigde sample rate,
   skip_samples, missing timestamps, start op niet-nul seekpositie en discontinuïteit.
6. Appoutput → fMP4 → werkelijk Apple-playeritem, geen vertrouwen op JSON alleen.

Protocolmajor v1 verandert alleen met expliciete version bump. Service-SemVer en
appversie mogen onafhankelijk doorlopen zolang de wirecompatibiliteit gelijk blijft.
