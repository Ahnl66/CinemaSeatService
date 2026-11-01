# 0.1.0 — 2026-11-01 (development snapshot)

- Added headless DTS/TrueHD/MLP-to-FLAC conversion with bounded sessions,
  independent credits, cancellation, health checks and optional rotated logging.
- Added standalone host tests and a Docker deployment recipe.
- Native Docker images, DTS-HD MA, hardware benchmarks and CinemaSeat integration
  remain unaccepted. This is a private source snapshot, not a validated release.

# CinemaSeat Audio Decoder

Zelfstandige headless C++20-service voor gedemuxte DTS/TrueHD/MLP-pakketten naar
FLAC, uitsluitend audio in RAM. Ontwikkelversie **0.1.0**, vastgelegd als private ontwikkelsnapshot.

**Status: werkende native service/hosttests, gedeeltelijk geaccepteerd.**
Geen CinemaSeat-productie-integratie of Diagnose-route geactiveerd. Dockerbuilds
op amd64/arm64, DTS-HD MA en fysieke hardwareacceptatie ontbreken nog.

- [Implementatiestatus en bewijsmatrix](docs/IMPLEMENTATION_STATUS.md)
- [Deployment op je andere server](docs/DEPLOYMENT.md)
- [Hostbewijs](docs/evidence/HOST_2026-11-01.md)
- [Baseline](docs/BASELINE.md)
- [Technisch ontwerp en fasen](docs/TECHNICAL_DESIGN.md)
- [Normatief protocol v1](docs/PROTOCOL_V1.md)
- [Oorspronkelijke startopdracht](docs/SOL_HANDOFF.md)
- [Eigen changelog](CHANGELOG.md) en [dependency notices](THIRD_PARTY_NOTICES.md)

```sh
python3 scripts/run_checks.py --host --scratch /tmp/csad-host-checks
python3 scripts/run_checks.py --all --scratch /tmp/csad-all-checks
```

`--all` faalt met incomplete bewijsstatus zolang verplichte checks ontbreken.
De scripts installeren Docker nooit. Docker wordt op verzoek van de gebruiker
niet op de ontwikkel-Mac geïnstalleerd.

De service kent geen mediabestanden, bron-URL's, NAS-paden, credentials, video,
accounts of bibliotheek. Alleen settings en optionele geroteerde metadata-debuglogs
gebruiken disk; audio wordt niet gespould. Er is geen AAC/downmix/resamplingroute.
Fixed-point DTS en integer FLAC-packing worden tegen referentie-PCM getest.
Niet bewezen of niet exact representabele formaten falen expliciet.

## Projectgrens en versiebeleid

Alle servicecode, containers, fixtures en zelfstandige tests staan in deze
repositoryroot. De build haalt hash-gecontroleerde dependencies zelfstandig op en
gebruikt de CinemaSeat-app of app-Vendor niet. App en service hebben onafhankelijke
Git-historie en versienummering.

Service-SemVer, protocolmajor en appversie zijn onafhankelijk. Deze eerste service-
snapshot begint op 0.1.0; de appversie is niet verhoogd. De repository is privé.
Een publieke release, projectlicentie, imagepublicatie en apparaatinstallatie
vragen een afzonderlijke opdracht. GitHub Actions zijn niet toegevoegd of gestart.

## Klonen op de testserver

```sh
git clone https://github.com/Ahnl66/CinemaSeatService.git
cd CinemaSeatService
cp config/settings.example.json docker/settings.json
docker compose -f docker/compose.example.yaml up -d --build
```

Voer dit uit op de server waar Docker al beschikbaar is. Gebruik voor herhaalbare
metingen een specifieke commit via `git checkout <commit-sha>`. Bij een private
repository moet Git-toegang op die server afzonderlijk zijn geregeld; zet geen
tokens in Compose of in het settingsbestand. De volledige procedure en open
acceptatiepunten staan in [DEPLOYMENT.md](docs/DEPLOYMENT.md).
