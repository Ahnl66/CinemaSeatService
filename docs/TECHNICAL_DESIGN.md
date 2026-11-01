# Technisch ontwerp — CinemaSeat Audio Decoder

Ontwerpdatum: 2026-11-01, Europe/Amsterdam. Status: klaar voor implementatieplanning.
Doeluitvoerder: Sol, redeneerniveau gemiddeld. Lees ook het normatieve
[protocolcontract](PROTOCOL_V1.md) en de [startopdracht](SOL_HANDOFF.md).

## 1. Opdracht, toestemming en grenzen

De gebruiker keurde goed: een lean Docker-audioconverter, geen UI of toegang tot
bronbestanden, configuratie in een settingsbestand, optionele debuglogs naar een
bestand, FLAC-uitvoer en transparante integratie in **Apple TV Standard**. In de
audiosectie van de algemene spelersinstellingen komen uitsluitend enabled,
adres en poort. De service ondersteunt gelijktijdige sessies; één goed werkende
sessie op zowel DS923+ als ODROID-N2+ is vereist. Het maximale praktische aantal
parallelle sessies is een meetresultaat, geen vooraf beloofd getal.

Deze opdracht maakt het ontwerp, geen implementatie. De latere bouwopdracht geeft
uitsluitend toestemming voor de hier beschreven externe FLAC-route. Geen
herontwerp van de bestaande ALAC-route, geen lokale DTS/Dolby-decoder toevoegen
aan Standard, geen iPad-werk en geen gedragsverandering in Full. Nieuwe
media-experimenten lopen via de bestaande Diagnose-app. Geen aparte tvOS-probeapp.

Non-goals: Atmos/DTS:X-objectweergave, video op de converter, serverlibrary,
accounts, bron-URL's, bestandsmounts, transcoding naar AAC, normalisatie,
downmix, resampling, autodiscovery, internettoegang of beheerwebsite.

## 2. Geïnspecteerde appbasis

Git HEAD bij ontwerp: `b69bcf0a21becef3350632764aa4cf287c010bea`, met een omvangrijke
bestaande, deels staged werkboom. Die werkboom is de gelezen bron van waarheid;
HEAD alleen reproduceert de toestand niet. Bewaar bestaand werk. Lees vóór elke
fase opnieuw de relevante bronnen; pas aansluitpunten aan wanneer namen wijzigen.

| Actuele bron in apprepository | Rol en consequentie |
| --- | --- |
| `CinemaPlexTV/Core/Models/MediaItem.swift`, `AudioAvailability` | `requiresServer` selecteert DTS/DCA/TrueHD/MLP; availability gebruikt al servergebonden generaties. Externe decoder krijgt een eigen beschikbaarheidsroute, zonder bestaande serverstatus te overschrijven. |
| `scripts/check_audio_variant.py` | Standard verbiedt DCA/TRUEHD/MLP/AC3/EAC3-decoder-/encodersymbolen. Dit verbod blijft intact. Parsen/demuxen is al toegestaan. |
| `docs/AUDIO_BUILD_VARIANTS.md` | AC-3/E-AC-3 worden lokaal gekopieerd naar Apple; deze service is daarvoor niet nodig. FLAC-speleritemvalidatie en serveraudio bestaan al. |
| `Player/FFmpeg/Bridge/FFmpegPlaybackExecution.m` | Geselecteerde audiostream, Standard-codecguard, lokale decode/encode, header schrijven, packetloop en EOF-drain. Nieuwe expliciete externe tak komt vóór de bestaande Standard-afwijzing. |
| `Player/FFmpeg/Bridge/FFmpegPlaybackPacketIO.h` en timinghelpers | Bestaande bias/pending-audio/interleaved-write hebben één eigenaar. Geen tweede timestampbias invoeren. |
| `Player/FFmpeg/FFmpegTypes.swift`, command builder en engine | Outputprofiel en HLS-codecmetadata moeten FLAC weerspiegelen wanneer de externe route actief is. Bestaand ALAC-profiel ongemoeid laten. |
| `PlayerViewModel+Preparation.swift`, `PlayerViewModel.swift` | Bestaande serverkeuze, seek/trackwissel, cancellation en readiness. Externe sessie moet dezelfde levensduur volgen. |
| `UI/Views/SettingsView.swift` | Algemene Playback/Audio-instellingen en Save/Discard-draft. `PlaybackSettingsView.swift` is juist het afspeeloverlay; plaats de nieuwe verbindingsinstellingen daar niet. |
| `NAS/Database/NASDatabaseBackup.swift` | Huidige volledige back-up heeft formaat 5, databaseschema 7. Gebruik de actuele versie, geen historische formaat-3-aanname. |
| `CloudSync/SyncContract.swift` | Bestaande voorkeurensync; nieuwe decoderinstelling blijft voor deze feature lokaal per apparaat. Geen synccontractuitbreiding nodig. |
| `Diagnostics/Core/DiagnosticReport.swift`, `docs/diagnostics/schemas`, `scripts/validate_diagnostics.py` | Verplichte uitbreiding voor nieuwe experimentele evidence en stabiele criterium-ID's. |
| `Vendor/FFmpeg/SOURCE_REVISION`, vendorheaders | Effectieve FFmpeg-API en bronrevision controleren vóór code. Appdocumentatie noemt 9.0.2; pin de service expliciet, gebruik geen willekeurige systeem-FFmpeg. |

Paden vanaf `Player/...` zijn onder `CinemaPlexTV/`. Bovenstaande is een
aansluitkaart, geen opdracht om al die bestanden zonder noodzaak te wijzigen.

## 3. Architectuur en projectindeling

```text
CS: bron openen → demux → geselecteerde DTS/TrueHD/MLP-pakketten
                              │ WebSocket, uitsluitend audio
                              ▼
service: protocol → decoder → PCM/FIFO → FLAC-encoder → protocol
                              │ FLAC-pakketten + configuratie/timing
                              ▼
CS: native audiobrug → bestaande fMP4/HLS-mux → bestaande AVPlayer
    videopakketten blijven lokaal in de bestaande videoroute
```

Kies C++20, CMake, Boost.Asio/Beast voor HTTP/WebSocket, nlohmann/json voor JSON,
FFmpeg `libavcodec` + `libavutil` + eventueel `libswresample` voor exact
sampleformaattransport zonder samplerate/layoutwijziging. Geen `libavformat`
nodig in de runtimeservice: CS levert reeds gedemuxte pakketten. Geen subprocess
per pakket of shellcommando's. Dependencies vastpinnen met revision/hash en
licentiemanifest. Geen volledige FFmpeg-cli in het runtime-image.

Geplande structuur, pas aanmaken in implementatiefase:

```text
VERSION, CHANGELOG.md, LICENSE, THIRD_PARTY_NOTICES.md
CMakeLists.txt, CMakePresets.json
src/{main,config,server,session,codec_pipeline,wire,logging}.{cpp,hpp}
tests/{unit,protocol,integration,fixtures,performance}/
config/settings.example.json
docker/{Dockerfile,compose.example.yaml}
scripts/{build_dependencies,check_codec_registry,run_checks,benchmark}.*
dependencies/manifest.json
docs/{deze documenten,DEPLOYMENT.md}
```

Servicebuild en tests mogen de bovenliggende app niet nodig hebben. Hostfixtures
mogen bestanden lezen; productieconverter niet. Testmedia worden lokaal uit
gegenereerde/licentievrije PCM of door gebruiker verstrekte bronnen gemaakt;
geen commerciële filmfragmenten committen.

## 4. Taken, eigendom en gelijktijdigheid

- Eén proces, één acceptor, twee netwerk-I/O-threads standaard.
- Per socket een Beast-strand: maximaal één read en één write tegelijk; alle
  writeverzoeken inclusief close/control door één begrensde scheduler.
- Per toegelaten sessie één codecworker met eigen decoder, encoder en PCM-FIFO.
  Codeccontexten worden nooit door twee threads tegelijk gebruikt.
- Start standaard met één codec-thread per decoder/encoder. Geen `auto` dat alle
  cores voor elke sessie claimt. Configureer een klein begrensd threadaantal.
- Netwerkcallbacks decoderen geen audio; codecworker blokkeert geen I/O-strand.
- Queues bewaren uitsluitend owned buffers/refcounted AVPackets, geen pointers
  naar Swift-Data of een hergebruikte demuxpacket.
- RAII voor alle AV-objecten. Annulering wekt credit-, queue- en formatwaits.
  Completion wordt exact eenmaal afgehandeld. Stop nooit een worker met een
  onveilige thread kill. Shutdown sluit I/O en wacht begrensd; een daadwerkelijk
  vastgelopen codec wordt als processtoring gerapporteerd, niet als opgeruimd.
- Codec-crash kan alle sessies raken: bewust geaccepteerde eerste-versiegrens
  van één proces; geen claim van procesisolatie. Normale sessiefouten blijven lokaal.

## 5. Formaten en kwaliteitscontract

Invoercodec-ID's op de wire: `dts`, `truehd`, `mlp`. Map uitsluitend naar de
bijbehorende FFmpeg-IDs; stuur geen numerieke FFmpeg-enums over het netwerk.
Accepteer 1–8 kanalen, 8–192 kHz en integer-audio tot 24 significante bits voor
de eerste interoperabele versie. Een DTS-profiel dat niet gedecodeerd kan worden
is niet automatisch een DTS-core-success. DTS-HD MA-fixtures vergelijken PCM
met de volledige lokale HD-decode, niet alleen de core.

De eerste echte gedecodeerde frames bepalen samplefrequentie/layout/precisie.
Bronmetadata kan onvolledig zijn, maar een bekende conflicterende bronclaim moet
worden gemeld. Geen stille truncatie, ongemerkte precision loss of default stereo.
Niet ondersteunde layout/floatconversie geeft `unsupported_format`, totdat een
exacte conversie afzonderlijk met fixtures bewezen is. Midstream formaatwijziging
geeft `format_changed` en beëindigt de sessie; niet stiekem herconfigureren.

Initialiseer FLAC met global-header, sample-timebase `1/sample_rate`, beoogde
blocksize 4096 en compression level 1. Controleer effectieve waarden na open;
defaults kunnen per FFmpeg-versie verschillen. Alleen integer packing/
planarinterleaving zonder waarde-, samplerate- of kanaalwijziging is toegestaan.
FIFO heeft een harde bovengrens en wordt na EOF volledig gedraineerd.

De decoder-send/receive-lus behandelt EAGAIN door de tegenovergestelde kant te
drainen, niet door busy wachten. Eén packet kan nul of meerdere frames geven.
Pas officiële inputbufferpadding toe. Decodertrim/skip-samplegedrag wordt precies
eenmaal toegepast; de outputtimeline beschrijft reeds geldige samples. Drain
decoder, eventuele sampleformaatbuffer, FIFO en encoder in die volgorde.

Zoekpriming heeft een expliciet eindig budget: maximaal 256 inputpackets of twee
seconden geankerde bronduur, wat eerst bereikt wordt. Binnen dit budget mogen
alleen aantoonbare leading fragments vóór de eerste codec-sync worden overgeslagen,
met telling/reden in evidence. Hergebruik het bestaande Diagnose-onderzoek naar
DTS-seekpriming als referentie; laat niet elk willekeurig decode-error door.
Na eerste geldige sync/frame is decodecorruptie fataal. Zonder bruikbare sync
binnen budget: `decode_failed`. Een TrueHD-seek zonder major sync vraagt voldoende
bronpreroll uit de bestaande reader, niet een decoder die samples verzint.

FLAC-STREAMINFO kan streaming-onbekende total samples/MD5 hebben. De client gebruikt
live extradata voor fMP4 en hoeft geen eindheader terug te schrijven. Verifieer
dat de gepinde encoder reeds bruikbare extradata na open levert. Een backend die
een seekbare uitgang vereist voldoet niet aan de eis en moet vervangen worden.

## 6. Timing, seek en mux-integratie

Zie protocol voor de absolute sampletijdlijn. Cruciale invarianten:

1. Service kent alleen audio-brontijd; geen video-offset of CS-relatieve tijdbias.
2. De client past source→lokale outputtijd en bestaande videobias exact één keer toe.
3. Decodervertraging en trimming worden afgeleid uit gedecodeerde frametiming,
   sampleaantallen en officiële side data; niet uit één packet-in/packet-uit-aanname.
4. Een zoekopdracht krijgt een nieuwe socket, sessie en clientgeneratie. Geen
   netwerkflush op een sessie die nog oude resultaten kan produceren.
5. CS houdt benodigde preroll en timestamps aan. De afspeelstart op het doel
   gebruikt de bestaande seek/playheadlogica; niet willekeurig FLAC-frames afknippen.

### 6.1 Header-/startupvalkuil eerst oplossen

`avformat_write_header` heeft de werkelijke FLAC-parameters nodig vóór muxstart.
Die kunnen pas na enkele bronpakketten bekend zijn. Voeg daarom een begrensde
preheaderfase toe: lees met dezelfde demuxer vooruit, stuur geselecteerde audio,
bewaar noodzakelijke video-/audiopakketten met refs en wacht op `output_format`.
Bewaar hoogstens 32 MiB of 5 seconden bronmedia, wat eerst bereikt wordt; bij
overschrijding expliciet `format_probe_limit`, geen onbegrensde buffer.

Na formatvalidatie maak je de outputstream, schrijf je de header en replay je de
bewaarde packets exact eenmaal. Geen tweede bronreader naast de bestaande
demuxer, geen bronbestand naar de service. Bewijs dit eerst in Diagnose.

### 6.2 Client/native bridge

Geplande nieuwe appcomponenten (namen zijn voorgeschreven voorstel):

- `Core/Models/ExternalAudioDecoderSettings.swift`: versie-1 lokale configuratie.
- `Player/ExternalAudio/ExternalAudioRoutePolicy.swift`: pure beslislogica.
- `ExternalAudioSession.swift`: actor met URLSessionWebSocketTask, bounded queues,
  protocolcodec, timeouts en generatiecontrole.
- `ExternalAudioWire.swift`: pure binaire/JSON-parser, geen UI of AVPlayer.
- `FFmpeg/Bridge/FFmpegExternalAudio.{h,m}`: expliciet sessiecontract tussen
  bestaande native packetloop en Swift transport; geen Objective-C-block per byte.

De native bridge krijgt een owned routeconfig/sessiehandle via getypeerde request-
velden; nooit via extra tekst in het FFmpeg-commandargument of globale defaults.
Native codecguard staat de geblokkeerde bron uitsluitend toe bij een geldig
external-FLAC-routeobject; zonder object blijft de bestaande afwijzing gelden.
Er wordt lokaal géén geblokkeerde decoder geopend.

Voorgesteld expliciet bridgecontract, uit te werken naast de bestaande executor:

| Operatie | Vereiste semantiek |
| --- | --- |
| `open(config, generation, cancellation)` | Asynchroon naar Swift transport; geeft één owned sessiehandle. Geen native muxlock over await houden. |
| `submitInput(packet)` | Ref/copy van packet en allowlisted side data; accepted/wouldBlock/failure. Bij wouldBlock eerst receive/control pompen; niet packet droppen. |
| `pollFormat()` | notReady/validatedFormat/failure; bruikbaar tijdens preheaderfase. |
| `pollOutput()` | notReady/ownedPacket/end/failure; niet-blokkerend voor muxowner. |
| `finishInput()` | Exact eenmaal na laatste accepted input; geen impliciet cancel. |
| `cancel()` | Idempotent, wekt alle waits; late completions blijven bij oude generatie. |
| `close()` | Ownership vrijgeven na worker/networkcompletion, exact eenmaal. |

De Objective-C-sessiehandle bevat een sterke referentie naar zijn Swift-owner
tot close; callbacks gebruiken geen onbeschermde raw pointers. Eén wachtprimitive
met cancellation signaleert nieuwe credit, format, output of failure. Gebruik
geen periodieke busy-poll om de native loop 'responsief' te houden. De bestaande
FFmpeg-interruptcallback moet ook deze waits kunnen beëindigen.

Eén native muxowner schrijft alle outputstreams. Swift receive valideert en
enqueued FLAC-pakketten; schrijft nooit rechtstreeks in AVFormatContext. Mainloop
pompt remote audio tussen demuxstappen en bij EOF, bewaart bestaande interleaving
en productiebudgetten. Geen sequentieel 'stuur packet, wacht op precies één antwoord':
dat veroorzaakt deadlocks bij decoder-/encoderbuffering. Tijdens creditwait moet
receive/control/cancel blijven lopen. Als video door interleaving tijdelijk moet
wachten, gelden dezelfde bounded queues en cancellable waits.

AVPacket side data verlaat de app alleen volgens protocolallowlist. Nieuw
codec-extradata tijdens een sessie wordt geweigerd met `format_changed`;
geen nieuwe pipeline met half oude samples. Voeg productiecode alleen toe aan
relevante Xcode-/hostharnesses en behoud bestaande publieke testcontracten.

## 7. Routeselectie en instellingen

| Situatie | Gedrag |
| --- | --- |
| Full, of externe optie uit | Huidig gedrag exact behouden. |
| Standard, ondersteunde/native codec | Huidige route; geen converterverbinding. |
| Standard, DTS/TrueHD/MLP, external enabled, originele bron bereikbaar | Eerst externe FLAC-route. |
| Externe voorbereiding mislukt | Hoogstens één bestaande toegestane route proberen; geen retryloop. |
| Uitval tijdens afspelen | Gecontroleerde herstart op actuele bronpositie via toegestane fallback; anders stoppen met heldere fout. |
| Server levert alleen bestaande serverconversie, geen originele bytes | Service kan niets uit een onbereikbare originele track halen; gebruik bestaande serverpolicy of meld expliciet ontbrekende route. |

Fallback mag geen audiotrack, taal, kanaalindeling of kwaliteit stilzwijgend
veranderen. Een bestaande ingestelde FLAC-serverroute is alleen bruikbaar als de
feitelijke output aan dit kanaal-/kwaliteitscontract voldoet. Geen automatische
AAC-fallback vanuit externe lossless-audio, ook al bestaat elders een expliciete
AAC-serverinstelling. Zonder externe route blijven alle bestaande serverpolicies
ongewijzigd. Dit voorkomt dat de nieuwe functie een impliciete downgrade invoert.

Browsebeschikbaarheid: enabled plus geldige configuratie maakt een geblokkeerde
track kandidaat, geen gegarandeerde live pass. Geen request per filmtegel.
Bereikbaarheid/capabilities cachen hoogstens 30 seconden, scoped op endpoint en
configuratiegeneratie. Verbindingsfouten zijn geen permanente codec-onbeschikbaarheid.
Een endpointwissel maakt oude cache en pending completions ongeldig. Een lopende
sessie houdt zijn configuratiesnapshot; wijzigingen gelden voor volgende sessies.

UI uitsluitend tvOS Standard in algemene Spelersinstellingen/Audio:

- `Externe audiodecoder gebruiken`, standaard uit.
- `Adres`, alleen hostname/IPv4/IPv6; geen scheme, URL-pad, userinfo of query.
- `Poort`, integer 1–65535, voorgesteld standaard 9078.
- Adres/poort alleen zichtbaar bij enabled; geen testknop of extra statuspagina.
- Bestaande Save/Discard en validatie volgen; port/host niet normaliseren met
  stringconcatenatie maar via URLComponents, inclusief IPv6.

Bewaar lokaal onder `audio.externalDecoder.configuration.v1`: version, enabled,
host, port. Geen secrets. Geen iCloud-sync voor deze nieuwe apparaatinstelling.
Neem hem wel op in handmatige volledige configuratie-export. Verhoog daartoe
het huidige back-upformaat 5 naar 6, met verplicht valide nieuw veld voor formaat
6. Lees formaten 1–5 met default disabled, behoud hun bestaande validatie; behoud
databaseschema 7. Oudere apps moeten formaat 6 duidelijk afwijzen. Op iPad/Full
mag gedeelde import/export het veld verliesvrij bewaren, maar de nieuwe route
en UI blijven uitgeschakeld; hiervoor geen iPad-build of feature uitvoeren.

Controleer bij implementatie actuele local-network/privacy-/ATS-instellingen.
Voeg alleen noodzakelijke lokale HTTP/WebSocket-toestemming toe, geen algemene
onbeperkte internet-ATS-uitzondering. Thuisnetwerk-only is een deploymentgrens:
geen portforwarding. Geen claim van encryptie/authenticatie op `ws://`.

## 8. Settingsbestand, limieten en logging

Lees `/config/settings.json` eenmaal bij start. Mount als read-only file; geen
mediavolume. Wijzigingen worden actief na containerrestart. Onbekende keys,
verkeerde types, ongeldige waarden of ontbrekend bestand geven startup failure
met veilige, concrete melding. Ontbrekende optionele velden krijgen hieronder
gedocumenteerde defaults. Voorbeeld van de beoogde configuratie, geen executable:

```json
{
  "version": 1,
  "listen": {"address": "0.0.0.0", "port": 9078},
  "sessions": {"maximum": 4, "codec_threads": 1},
  "limits": {
    "control_message_bytes": 65536,
    "packet_payload_bytes": 1048576,
    "input_window_bytes": 4194304,
    "output_window_bytes": 4194304,
    "queued_input_bytes": 8388608,
    "queued_output_bytes": 8388608,
    "pcm_fifo_samples": 65536
  },
  "timeouts": {
    "handshake_seconds": 5,
    "start_seconds": 5,
    "format_seconds": 10,
    "ping_seconds": 15,
    "pong_seconds": 10,
    "drain_seconds": 10,
    "close_seconds": 2
  },
  "logging": {
    "debug_enabled": false,
    "file": "/logs/decoder.log",
    "maximum_file_bytes": 10485760,
    "retained_files": 3
  }
}
```

Harde configuratieranges: sessions 1–32, codec_threads 1–4; protocolberichtlimieten
mogen naar beneden, niet boven protocolmax. Windows moeten minstens één maximaal
bericht (payload + 64 bytes) kunnen dragen en niet boven de queuecapaciteit komen.
FIFO 8192–262144 samples per kanaal, totale allocatie vooraf overflow-checken.
Service accepteert sample-rate 8–192 kHz en maximaal acht kanalen. Alle timeouts
1–120 seconden, pinginterval 5–60 seconden. Geen timing-timeout voor bewust
gepauzeerde data of wachten op credit; heartbeat bewaakt de verbinding.

Debug uit: geen logfile openen of logmap vereisen. Alleen minimale gesaniteerde
startup-/fatalmelding naar stderr. Debug aan: JSONL-events naar logfile, roteren
op grootte; geen dump van willekeurige FFmpeg-logstrings, userinput of packets.
Events: random tijdelijke sessie-ID, codec/profiel, effectieve sampleformaten,
tellingen, CPU-/walltime, queue-high-watermarks, foutcode en lifecycle. Geen IP's
nodig in het bestand. Samenvattingen maximaal eenmaal per seconde, geen regel
per audiopakket. Schrijffout stopt filelogging en meldt één waarschuwing op stderr;
audio blijft werken. Logqueue begrenzen en dropped-log-count bijhouden.

PCM-FIFO en wirequeues zijn begrensd, maar libavcodec kan intern extra geheugen
alloceren. Rapporteer RSS; presenteer queuecaps niet als harde totale RSS-garantie.
Voorgestelde Docker-memorylimiet 512 MiB voor vier sessies, na meting afstellen.

## 9. Docker en standalone bouw

- Multistage Linuxbuild. Runtime bevat service, noodzakelijke gedeelde libraries,
  dependency notices en versie/buildmanifest; geen compiler, Python of FFmpeg-cli.
- Native `linux/amd64` en `linux/arm64`, geen emulatie in productie. Test arm64
  daadwerkelijk op ODROID; een QEMU-build is geen performancebewijs.
- Pin basisimage-digest en alle sourceversions in manifest. Begin met dezelfde
  gevalideerde FFmpeg-bronrevision als app, maar servicebuild haalt die zelfstandig
  op uit upstream; niet relatief linken naar app-Vendor. Geen appvendormutatie.
- Minimale FFmpeg-config: disable-everything/autodetect/programs/docs/network/
  avdevice/avfilter/postproc; enable avcodec/avutil en noodzakelijke helpers,
  decoders dca/truehd/mlp, encoder flac, benodigde parsers indien tests dat aantonen.
  Voer registry/symbolcheck uit; daadwerkelijke configuratie is testbaar bewijs,
  een lijst flags in documentatie is dat niet.
- Gebruik dynamische LGPL-libraries en houd GPL/nonfree-componenten uit deze
  build. Publiceer bij distributie passende exacte bronnen/buildinstructies en
  notices. Dit ontwerp doet geen uitspraak over specifieke codecpatenten.
- UID/GID zonder root, read_only, cap_drop ALL, no-new-privileges, geen privileged,
  geen Docker-socket, geen host-network nodig. Eén TCP-poort publiceren.
- Alleen settingsfile en optionele `/logs` mount. Geen tmpfs nodig voor media;
  eventuele runtime-tmpfs expliciet klein en niet als audio-spool gebruiken.
- Dockerrestart `unless-stopped`. Healthcheck via eigen `--healthcheck`-modus;
  geen curl toevoegen uitsluitend voor health. Health is liveness, capaciteit
  vol is niet automatisch unhealthy.
- SIGTERM: acceptor dicht, actieve sessies `server_shutdown`, maximaal 10 seconden
  nette afbouw, resources vrijgeven. Docker stop_grace_period 15 seconden.

Geplande standalone verificatie-entrypoint:
`python3 scripts/run_checks.py --all --scratch <absolute-path>`.
Python is alleen build/testgereedschap. Testscript draait unit/protocol/codec/
containerchecks; bij ontbrekende Docker of fixture meldt het incomplete en faalt
de volledige releasecheck. Geen groene status op overgeslagen verplichte tests.

## 10. Test- en acceptatiematrix

| ID | Vereist bewijs |
| --- | --- |
| SVC-01 | Zelfstandige amd64/arm64-build; registry/versie/provenance; runtime zonder mediavolumes, met read-only root en zonder beschrijfbare logmap als debug uit staat. |
| SVC-02 | Wire golden vectors, lengte-/overflow-/NaN/duplicate-key-rejects, state machine, onbekende versie/codec, ongeldige side data, credits en races. |
| SVC-03 | DTS core, DTS-HD MA, TrueHD, MLP: volledige decode naar referentie-PCM versus teruggedecodeerde FLAC; gelijke samples/layout/rate/precisie en geen HD-core-only-fallback. |
| SVC-04 | Decoder/encoder EAGAIN, EOF-tail, ontbrekende PTS na anker, onbruikbaar eerste anker, skip_samples/priming, negatieve tijden, discontinue tijd en midstream formatwijziging. |
| SVC-05 | Trage reader/writer, nulcredit, volle queues, lange pauze, halfopen socket, cancel/seek/trackwissel/EOF/shutdown, maxsessies en resourceherstel. |
| APP-01 | Alleen Standard tvOS toont drie velden; Save/Discard/defaults/IPv6; Full en disabled-route gelijk aan baseline. |
| APP-02 | Originele track-ID en taal behouden; routeprecedentie; server-fallbackbeleid; geen AAC-/downmix-/stille fallback; availability en generatie-invalidatie. |
| APP-03 | Preheaderbuffer begrensd; FLAC-extradata→fMP4→werkelijk AVPlayerItem met juiste codec/rate/kanalen; geen dubbele packets/bias. |
| APP-04 | Stop/cancel bij ieder await, pending seek vervangen, oude sessieresultaten genegeerd, audiowissel rollback, paused trackwissel, EOF en herstart. |
| APP-05 | Back-upformaat 6 roundtrip; legacy 1–5 blijven geldig, nieuwe default uit; corrupte velden geweigerd, geen secrets, geen DB-schemawijziging of nieuwe sync. |
| HW-01 | Eén sessie op DS923+ én ODROID: representative zware 7.1/24-bit DTS-HD MA en TrueHD, 48/96/192 kHz voor zover codec/bron dat ondersteunt. |
| HW-02 | Zelfde materiaal twee/vier sessies; throughput, RSS, CPU, thermisch gedrag, queueverloop en degradatie. Meer sessies mogen de praktische hardwaregrens zijn. |
| ATV-01 | Diagnose op fysieke Apple TV: hoorbaar juiste kanaalrouting, lipsync en behoud van bestaande video/DV/HDR-route. Host-componentpass bewijst dit niet. |

Referentie-PCM vergelijken na dezelfde geldige decodertrims, met canonical integer
packing en expliciete channel order. Voor lossy DTS betekent gelijkheid aan
gedecodeerde referentie, niet aan een onbeschikbare originele studiobron.
Codec-unsupported rate/layout expliciet buiten de ondersteunde matrix melden;
geen lager sampleformaat kiezen om een test groen te krijgen.

Performanceprocedure per hardware: noteer OS/kernel, arch, RAM, Dockerlimieten,
buildhash, throttling/temperatuur en achtergrondbelasting. Eerst ongepacete
conversiesnelheid meten, daarna 60 minuten end-to-end realtime met pauze, tien
verspreide seeks en tien trackwissels. Streefwaarde single-session throughput
minimaal 2x realtime; harde eis is duurzaam foutloze realtimeweergave met begrensd
geheugen op beide apparaten. 2x is reserve en wordt apart gerapporteerd.
CS-bufferstreefwaarde 3–5 seconden; extra netwerk/conversiestart streefwaarde
hoogstens 2 seconden op bedraad LAN, totale mediastart apart meten. Lipsync:
geen hoorbare drift, met meetbare AV-offset waar bron dat toelaat; gewenst binnen
40 ms en geen oplopende trend. Targets zijn nog niet gemeten resultaten.

## 11. Diagnose-contract

Voeg source-gated test `external_audio_decode.v1` toe aan bestaande Diagnose.
Varianten `external.flac.dts.v1`, `external.flac.truehd.v1`,
`external.flac.mlp.v1`. Criteria:

- `external_audio.protocol.v1`
- `external_audio.format_preservation.v1`
- `external_audio.timeline.v1`
- `external_audio.throughput.v1`
- `external_audio.cleanup.v1`
- `external_audio.rendering.v1` (human judgement required)

Evidence: desired/applied endpointconfig (gesaniteerd, geen privéhost nodig in
deelbaar rapport), service/protocol/FFmpeg-revision, bronprofiel, format/layout,
tellingen, hashes van canonical PCM voor hostfixtures, timingreeksen, queues,
ontbrekende/truncated coverage en cleanupresultaat. Geen packets, FLAC-blokken,
PCM, playlists/init/segments in rapporten. Bewaar evidence vóór player teardown.
Werk Swift-reportcontract, JSON-schema, validator, Diagnose-UI en analysehandleiding
samen bij. Bestaande schemaversie niet onzichtbaar incompatibel wijzigen.

## 12. Uitvoeringsfasen en stopcriteria

Werk sequentieel; houd de statusmatrix bij. Geen brede refactors of parallelle
agents zonder expliciete opdracht. Elke fase eindigt met bewijs en resterende
onzekerheid, niet met alleen compilatie.

**Fase 0 — inventarisatie.** Lees AGENTS, deze documenten, actuele brug/timing/
settings/back-up en Diagnosecontracten. Leg baselineversies/werkomvang vast zonder
geheimen of buildoutputs. Controleer werkelijke codec/layout-extradata-eigenschappen.

**Fase 1 — protocol en pure servicecore.** CMake, dependency pinning, config,
wire/golden vectors, bounded queues, state machine, standalone codecfixtures.
Bewijs SVC-02/03/04 vóór netwerk of appinterface. Geen successtub.

**Fase 2 — service en containers.** Beast transport, credits/heartbeat/cleanup,
logging/rotation, beide native images, standalone checks SVC-01/05. Buildcontext
uitsluitend deze folder. Maak deploymenthandleiding voor Synology/ODROID.

**Fase 3 — Diagnose-integratie, vroeg haalbaarheidsbewijs.** Native FLAC-preheader,
transport en muxaansluiting in herbruikbare componenten; toegang eerst source-gated
in Diagnose. Behoud productiegedrag. Fysieke codec/layout/timing- en single-session
performance op beide apparaten vaststellen. Als hardware/toegang ontbreekt, meld
welke tests onuitgevoerd zijn; geen performancegoedkeuring claimen. Als één sessie
de eis niet haalt, optimaliseer binnen scope, of leg concrete beperking ter besluit
voor. Niet compenseren met downmix/AAC/schijfspool.

**Fase 4 — Standard productie-integratie.** Pas na bewezen fase 3: routepolicy,
drie instellingen, availability, lifecycles/fallbacks, HLS-codecmetadata en
configuratie-export 6. Zelfde transport/native component, geen Diagnosefork.
Specifieke geautoriseerde externe route; oude ALAC/Full-paden behouden.

**Fase 5 — afronding.** APP-01 t/m 05, performanceconcurrentie, lange fysieke
acceptatie, documentatie en onafhankelijke servicebuild uit gekopieerde folder.
Voer volledige tvOS-verificatie uit:

```text
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 Tests/run_checks.py --all --app --scratch <absolute-resolved-path>
```

Selecteer die toolchain daadwerkelijk in de commandomgeving. Bouw/test Standard
ook expliciet met actuele schemanaam; de algemene `--app`-check bewijst niet
vanzelf de distributievariant. Behoud codec-exclusiecheck. Full-regressies mogen
de bestaande lokale checks gebruiken; geen iPad-gate. Verander nooit de verplichte
appcheck om een service-/Standard-fout te maskeren.

Nu geen commit/push/versiebump. Later: service eigen versie/tag/changelog; een
appcommit volgt root-AGENTS met gezamenlijke patch/buildmetadata, top-README en
volledige definitieve verificatie. GitHub Verify blijft uit, geen workflowdispatch.

## 13. Technische referenties

De onderstaande bronnen onderbouwen APIkeuzes, niet de onbewezen performance:

- [FFmpeg send/receive-contract](https://ffmpeg.org/doxygen/7.1/group__lavc__encdec.html)
  en [decoderbufferpadding](https://www.ffmpeg.org/doxygen/7.1/group__lavc__decoding.html).
  Gebruik bij bouw de headers en documentatie van de werkelijk gepinde versie.
- [Boost.Beast WebSocket](https://www.boost.org/latest/libs/beast/doc/html/beast/using_websocket.html):
  bestaande RFC6455-implementatie, geen zelfgeschreven handshake/framing.
- [Apple URLSessionWebSocketTask](https://developer.apple.com/documentation/foundation/urlsessionwebsockettask):
  native apptransport.
- [Apple HLS authoring](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html):
  FLAC in fMP4; echte player-itemvalidatie blijft vereist.
- [FFmpeg-licentievoorwaarden](https://ffmpeg.org/legal.html): kies passende
  dependencycompliance, zonder een ononderzochte patentblokkade te veronderstellen.
