# Startopdracht voor Sol gemiddeld — externe audiodecoder

Kopieer onderstaande opdracht naar een bouwchat met Sol op gemiddeld. Het maken
van dit bestand start geen implementatie. De huidige technische documenten zijn
verplicht te lezen context; voer geen ontbrekende productkeuzes op eigen gezag in.

---

Je werkt in `/Users/arthur/Documents/Projects/CinemaPlexTV`. Communiceer in het
Nederlands. Bouw de goedgekeurde externe audiodecoder volgens:

1. Actuele root-`AGENTS.md`.
2. `services/cinemaseat-audio-decoder/README.md`.
3. `services/cinemaseat-audio-decoder/docs/TECHNICAL_DESIGN.md`.
4. `services/cinemaseat-audio-decoder/docs/PROTOCOL_V1.md`.
5. `services/cinemaseat-audio-decoder/docs/IMPLEMENTATION_STATUS.md`.

Lees die documenten volledig vóór wijzigingen. Doe fase 0 t/m 5 uit het
technisch ontwerp in volgorde. Werk per fase de status en bewijsverwijzingen bij.
Gebruik geen subagents: de opdracht vraagt een uitvoerbaar ontwerp voor één Sol,
geen delegatie. Geef korte inhoudelijke voortgangsupdates.

De gebruiker heeft de volgende scope goedgekeurd:

- Een kleine headless Docker-service, native amd64 en arm64, meerdere sessies.
- Testhardware: Synology DS923+ Ryzen R1600 en ODROID-N2+. Op beide moet één
  sessie duurzaam goed spelen; meer sessies zijn afhankelijk van de meetresultaten.
- Invoer uitsluitend gecomprimeerde audio uit CS: DTS/DTS-HD, TrueHD, MLP.
- Uitvoer uitsluitend FLAC, kanaalgebaseerd, zonder extra kwaliteitsverlies,
  downmix, samplerateverlaging, normalisatie of stille DTS-corefallback.
- Geen bronbestanden/-URL's, NAS-shares, mediabibliotheek, UI of accounts.
- Settingsbestand in Docker; optionele geroteerde debuglogging naar bestand,
  standaard uit. Audio uitsluitend RAM; geen disk-spool als oplossing.
- Alleen tvOS Standard krijgt Externe audiodecoder enabled/adres/poort in de
  algemene Spelersinstellingen/Audio, met bestaande Save/Discard. Overige werking
  is automatisch. Geen extra kwaliteit-, codec- of bufferopties in CS.
- Full/disabled gedrag blijft behouden. Geen iPad-feature, -build of -testopdracht.
- Alleen de nieuwe externe FLAC-integratie is geautoriseerd; geen aanpassingen
  aan bestaande ALAC-keuzes of lokale geblokkeerde decoders. Bestaande codec-
  exclusiecheck blijft hard gelden.

Alle servicecode, standalone tests, containers en protocolfixtures komen onder
`services/cinemaseat-audio-decoder/`. Deze folder moet zelfstandig te bouwen zijn
en wordt na ontwikkeling naar een nieuwe Git-repository verplaatst. Initialiseer
nu geen nested repository/submodule en publiceer niets. Serviceversie begint als
`0.1.0`, eigen VERSION/changelog, onafhankelijk van app en protocolmajor.

Er bestaat veel ander uncommitted/staged werk in deze repository, waaronder
videoserver-, cloud-, layout- en iPad-wijzigingen. Bewaar dat exact. Revert/stage/
commit geen andermans wijzigingen. De ontwerpbaseline is HEAD b69bcf0 plus de
werkboom van 1 november 2026. Controleer actuele bronpaden en contracts; historische
schemenamen/back-upversies uit oudere docs zijn geen reden om recente code terug
te draaien. Het ontwerp gaat uit van back-upformaat 5→6 en DB-schema 7; als de
actuele versie verder is, pas de nieuwe versie consequent aan met legacyreadtests.

Maak eerst servicecore/protocol/containers en het Diagnose-bewijs. Nieuw
media-experiment altijd source-gated in bestaande Diagnose met stabiele ID's,
schema/validator/UI/analysehandleiding en bewijs vóór teardown. Geen aparte
diagnostische tvOS-app, geen payloads/decoded media in rapporten. Componentpass
is geen bewijs voor fysieke audio, lipsync of zichtbaar HDR/DV.

De drie belangrijkste integratieproblemen moeten expliciet worden bewezen:

1. Vóór de fMP4-header voldoende audio primen om echte FLAC-STREAMINFO/layout te
   krijgen, met begrensde lokale preheaderbuffer en geen dubbele demuxer.
2. Decoder/encoder buffering en bidirectionele credits zonder packetrequest/
   response-deadlock. Network receive/control/cancel blijft lopen bij volle queues.
3. Eén absolute brontijdlijn, decodertrims eenmaal, CS-origin/bias eenmaal,
   nieuwe generatie/socket bij seek en trackwissel. Geen drift verhullen.

Pas na succesvolle Diagnose-/hardwarefase de productieroute activeren. Gebruik
dezelfde herbruikbare transport/bridgecomponenten, geen tweede implementatie.
Als die fase zonder fysieke toegang niet voltooid kan worden, voltooi al het
onafhankelijke service-/hostwerk en rapporteer precies welke acceptatie ontbreekt.
Zeg niet dat de hardware-eis bewezen is. Vraag alleen ontbrekende concrete
toegang/testgegevens, niet opnieuw de reeds besloten functionele keuzes.

Verifieer eigen standaardsessie-/endpointlifetimes, cancellation op ieder await,
late generation callbacks, out-of-range packets, resourcecleanup en foutpaden.
Fallback mag geen stille AAC/downmix/andere audiotrack introduceren. Houd server-
instellingen en Full buiten deze route intact. Nieuw standaard tvOS-bronbestand
moet in de relevante Xcode-/hostharnesses staan; geen iPad-gate toevoegen.

Voor service: standalone `scripts/run_checks.py --all --scratch <absolute-path>`
maken en uitvoeren; run Docker/native checks op beide architecturen. Voor app:
de volledige bestaande entrypoint met geselecteerde volledige Xcode-toolchain:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 Tests/run_checks.py --all --app --scratch <absolute-resolved-scratch-path>
```

Bouw en valideer daarnaast tvOS Standard expliciet met de actuele scheme en
codec-exclusies. Rapporteer hosttests, containerchecks, lokale appverificatie,
fysieke Apple TV-acceptatie en beide hardwarebenchmarks afzonderlijk. Maak
overgeslagen verplichte checks niet groen en zwak geen bestaande regressie af.

Geen commit, push, install op apparaten, nieuwe remote repository of publicatie
zonder afzonderlijke opdracht. Als een latere commit/push gevraagd wordt, volg
het app-releasebeleid voor appwijzigingen en het onafhankelijke servicebeleid
voor de service. GitHub Verify blijft uit; geen workflow dispatchen.

Eindig met de concrete werkende functies, tests, hardwaremetingen, exacte open
punten en links naar implementatiestatus en deploymenthandleiding. Lever geen
succesmelding voor een stub of een uitsluitend compilerende route.

---
