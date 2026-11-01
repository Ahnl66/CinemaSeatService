# Deployment — development 0.1.0

Deze service draait zelfstandig. De CinemaSeat-productieroute en Diagnose-koppeling
zijn nog niet gebouwd/geaccepteerd; alleen de beschreven hostclient kan momenteel
de service gebruiken. Dit is geen afgeronde tvOS-feature of hardwaregoedkeuring.

## Andere server: native Dockerbuild

Testdoel is de ODROID-N2+ met Armbian op `odroidn2.local`. Docker wordt niet op de
ontwikkel-Mac geïnstalleerd. Kloon de private servicerepository op de testserver:

```sh
git clone https://github.com/Ahnl66/CinemaSeatService.git
cd CinemaSeatService
git checkout <commit-sha>
```

Gebruik de commit-SHA van de te testen snapshot; dit maakt metingen reproduceerbaar.
Regel toegang tot de private repository via de bestaande Git-authenticatie van de
server, zonder tokens in Compose of settings te zetten. Controleer eerst `uname -m`
en `docker compose version`; de arm64-test vereist een 64-bit OS en native Docker.
Voer vervolgens vanuit de repositoryroot uit:

```sh
cp config/settings.example.json docker/settings.json
docker build -f docker/Dockerfile -t cinemaseat-audio-decoder:0.1.0 .
docker compose -f docker/compose.example.yaml up -d
docker compose -f docker/compose.example.yaml ps
docker compose -f docker/compose.example.yaml exec audio-decoder /opt/csad/bin/cinemaseat-audio-decoder --healthcheck
```

Dezelfde Dockerfile is bedoeld voor native amd64 en arm64. Beide builds zijn hier
**niet uitgevoerd**. Gebruik geen emulatie voor performanceacceptatie. Noteer
`uname -m`, `docker version`, image-ID/digest, OS/kernel, RAM, containerlimieten,
CPU, temperatuur en achtergrondbelasting bij elke meting. Een build op een andere
server bewijst nog geen duurzame sessie op DS923+ of ODROID-N2+.

Compose publiceert alleen TCP 9078. De container gebruikt UID/GID 10001,
read-only root, geen capabilities, geen mediavolumes en geen audiotempbestanden.
Het settingsbestand wordt bij startup éénmaal ingelezen. Herstart na wijzigingen.
Een ongeldige configuratie stopt startup. De basisimagedigest en dependencybronnen
zijn gepind; apt buildtools zijn nog niet via een Debian-snapshot gepind.

Synology DS923+: gebruik Container Manager/Compose met bovenstaande folder als
projectcontext. ODROID-N2+: gebruik een native 64-bit Linux/arm64 Dockeromgeving.
Installeer niets op apparaten vanuit deze chat; deze stappen worden door de
gebruiker uitgevoerd. Er wordt geen image gepubliceerd; de private bronrepository is op verzoek van de gebruiker vastgelegd.

Gebruik alleen een vertrouwd LAN. `ws://` heeft geen authenticatie of encryptie;
geen portforwarding of publieke internetpoort. Er zijn geen accounts, bron-URL's,
NAS-mounts, library, UI of remotecontrolfunctie.

## Configuratie en logging

`config/settings.example.json` bevat alle defaults. Minimaal vereist is
`{"version":1}`; onbekende keys/types/waarden worden geweigerd. Windows moeten
minstens payload+64 bytes bevatten. De queuecap moet tevens één extra owned packet
kunnen bevatten; de service valideert dit conservatiever dan het ontwerpminimum.

Debuglogging staat uit; `/logs` hoeft dan niet te bestaan. Met debug aan: maak op
de server een logdirectory schrijfbaar door UID/GID 10001 en activeer alleen de
optionele logmount in Compose. JSONL-events bevatten tijdelijke sessie-ID's,
formaten, packet-/sampletellingen, queuehighwaters en walltime, geen audio of
bronpaden. De logqueue is begrensd; rotatie gebruikt maximum_file_bytes en
retained_files. Een schrijffout schakelt filelogging uit zonder audioteardown.
CPU/RSS/temperatuur worden nog extern gemeten; walltime is geen CPU-meting.

## Zelfstandige native build en checks

Vereist: Python 3.9+, C++20-compiler, make, CMake, curl en op Linux de gebruikelijke
build-essential/ca-certificates/bzip2. Geen app-Vendor of bovenliggende repo nodig.

```sh
python3 scripts/build_dependencies.py --scratch /tmp/csad-dependencies
cmake -S . -B /tmp/csad-build -DCSAD_DEPS=/tmp/csad-dependencies/deps -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/csad-build -j2
/tmp/csad-build/cinemaseat-audio-decoder --settings config/settings.example.json
```

In een tweede terminal, voor de volledige controle:

```sh
python3 scripts/run_checks.py --all --scratch /tmp/csad-checks
```

`--host` voert de onafhankelijke native/protocol/codec/netwerktests uit.
`--all` voert die ook uit en rapporteert ontbrekende releasebewijzen met een
non-zero exit. Docker wordt nooit door de runner geïnstalleerd. Een groene
`--host` is geen volledige acceptatie. Logs en `checks.json` blijven in scratch;
gegenereerde audiopacketfixtures blijven uitsluitend testinput, geen rapportinhoud.

## Huidige beperkingen / acceptatie

- Hostbewezen: DTS core stereo, TrueHD stereo, MLP stereo en MLP 5.1(back),
  exacte PCM-roundtrip, streaming STREAMINFO, 4096-sample FLAC plus exacte tail,
  bronankers, ontbrekende PTS na anker, negatieve PTS en skip_samples.
- Alleen vaste FLAC-layouts geaccepteerd; side-only 5.1 wordt momenteel expliciet
  geweigerd. Float, custom/height layouts en midstream wijzigingen falen expliciet.
- DTS gebruikt de gepinde fixed-point referentiedecode. XLL-recovery die lossy
  coreaudio zou opleveren wordt hard geweigerd. Dit kan multichannel DTS-HD-start
  blokkeren; geen bewezen DTS-HD MA-supportclaim voordat echte fixtures slagen.
- Geen begrensde leading-fragmentrecovery bewezen; corrupte priming faalt
  conservatief. TrueHD zonder major sync heeft bronpreroll van de toekomstige
  CS-reader nodig. Geen willekeurige decode-errors overslaan.
- Niet bewezen: native Linuximages op beide architecturen, DTS-HD MA/7.1,
  fysieke audio/lipsync/HDR/DV, preheader→fMP4→AVPlayerItem, een uur realtime op
  beide apparaten, seek/trackwissel en concurrentieschaal.
- De CS-transport/native bridge, Diagnose-test/evidencecontract en productie-UI,
  routepolicy en exportformaat 6 blijven open. Bestaande appwerking blijft intact.

Meetprocedure en criteria: [TECHNICAL_DESIGN.md](TECHNICAL_DESIGN.md#10-test--en-acceptatiematrix).
Alleen source-gated Diagnose mag de volgende nieuwe tvOS-media-experimenten doen.
Geen componentpass vertalen naar hoorbare/zichtbare acceptatie.
