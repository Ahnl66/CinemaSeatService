# Implementatiestatus

Bijgewerkt: 2026-11-01. **Gedeeltelijk geïmplementeerd; geen volledige acceptatie.**
Een echte zelfstandige native service converteert geteste codecpackets naar FLAC.
Dit is nog geen werkende externe-audiofeature in CinemaSeat. Geen productiepad,
instellingen of configuratie-export aangepast. Private servicebron-snapshot in Ahnl66/CinemaSeatService; geen publieke release.

## Fasen en stopcriteria

| Fase | Status | Bewijs / vervolg |
| --- | --- | --- |
| 0 — Implementatiebaseline | Gereed | [BASELINE.md](BASELINE.md): HEAD f083bb1, schone index/werkboom; actuele back-up 5 / DB 7 en brugcontracten geïnspecteerd. |
| 1 — Protocol en codecservicecore | Gedeeltelijk bewezen | Echte DTS core/TrueHD/MLP-roundtrips en pure protocoltests slagen. DTS-HD MA, 7.1, layoutbreedte en aanvullende foutfixtures ontbreken. SVC-03-stopcriterium nog niet gehaald. |
| 2 — Transport en Docker | Onafhankelijke service-/hostimplementatie voorbereid; niet geaccepteerd | Beast-service, settings, logging, lifecycle, concurrency en sanitizerchecks op arm64 macOS geslaagd. Dockerrecept aanwezig; beide native Linuximages niet gebouwd/getest. Dit doorloopt geen ontbrekend fase-1-bewijs. |
| 3 — Diagnose / hardware | Niet gestart | Eerst fase-1-codecmatrix afronden. Herbruikbare CS-client/native bridge, preheader/mux, source-gated Diagnose-test, rapport/schema/validator/UI/analysehandleiding ontbreken. Daarna fysieke hardwaretesten; ODROID-N2+ met Armbian op odroidn2.local en een fysieke Apple TV zijn door de gebruiker beschikbaar gemeld. Uitvoering/toegang nog niet geverifieerd. |
| 4 — Standard productie-integratie | Geblokkeerd door acceptatiegate | Pas na succesvolle fase 3. Geen settings/availability/fallback/HLS/exportwijzigingen toegepast. |
| 5 — Volledige acceptatie | Open | Onafhankelijke hostbuild bewezen; volledige bestaande appcheck en expliciete Standard-check worden afzonderlijk vastgelegd. Nieuwe APP-01 t/m 05 en fysieke acceptatie ontbreken. |
| Eigen repository | Private ontwikkelsnapshot 0.1.0 | Service los van de app in Ahnl66/CinemaSeatService. Geen nested Git/submodule, appcommit of publieke release. |

## Bewijsmatrix

Resultaten en lokale logverwijzingen: [HOST_2026-11-01.md](evidence/HOST_2026-11-01.md).
Geslaagde hostsubsets maken een volledige criteriumrij niet automatisch groen.

| Vereiste | Status / exact bereik |
| --- | --- |
| SVC-01 zelfstandige amd64/arm64-container | Niet uitgevoerd. Wel native arm64 macOS-build buiten appfolder en exacte runtime-codecregistry. |
| SVC-02 protocol/interoperabiliteit/limieten | Hostsubset geslaagd: golden vector, negatieve/64-bit waarden, strikte JSON/base64, packetlimieten, sequences/generaties, capacity, controls en credits. Swift-interoperabiliteit en volledige adversariële matrix ontbreken. |
| SVC-03 PCM-equivalentie incl. DTS-HD MA | Hostsubset geslaagd: DTS core stereo 48 kHz, TrueHD stereo 48 kHz, MLP stereo/5.1(back) 96 kHz. DTS-HD MA, 7.1, bredere rates/precisies niet uitgevoerd. |
| SVC-04 timing/priming/EOF/foutpaden | Hostsubset geslaagd: 30s bronanker, negatieve PTS, ontbrekende PTS na anker, missing_timeline, discontinuïteit, decodertrims 5+7 samples, echte encodervertraging/EOF-tail. Geforceerde EAGAIN-/midstream-format-/leading-fragmentfixtures ontbreken. |
| SVC-05 backpressure/cancel/multisession/cleanup | Hostsubset geslaagd: twee gelijktijdige sessies, volle outputqueue/geen nieuw credit, control/cancel, heartbeat tijdens korte pauze, starttimeout, shutdown, herstelde sessieslots. TSAN zonder melding. Langdurige pauze/halfopen/endurance/RSS-meting open. |
| APP-01 settings/Full/disabled | Nieuwe feature niet uitgevoerd; app onaangepast. Baselinechecks apart. |
| APP-02 routes/availability/fallback | Niet uitgevoerd; geen externe route geactiveerd. |
| APP-03 preheader/FLAC/fMP4/playeritem | Niet uitgevoerd; STREAMINFO vóór serviceoutput is wel hostbewezen, maar geen bewijs voor native fMP4-header of AVPlayerItem. |
| APP-04 lifecycle/seek/trackwissel | Niet uitgevoerd voor nieuwe client. Servicecancel is geen bewijs van clientgeneraties of CS-sessielifetimes. |
| APP-05 configuratie-export/legacy import | Niet uitgevoerd; formaat blijft 5, schema blijft 7. |
| HW-01 DS923+ één duurzame sessie | Niet uitgevoerd. |
| HW-01 ODROID-N2+ één duurzame sessie | Niet uitgevoerd. |
| HW-02 beide apparaten twee/vier sessies | Niet uitgevoerd. Twee hostsessies zijn geen hardwarebenchmark. |
| ATV-01 hoorbare output/lipsync/HDR/DV | Niet uitgevoerd. |
| Volledige lokale tvOS-check | Geslaagd, exit 0: volledige --all --app-entrypoint met Xcode, alle hostrunners/native TSAN en Full-appbuild/metadata. Bestaande appbasis; nieuwe integratie niet aanwezig. |
| Standard-build en codec-exclusies | Geslaagd voor ongewijzigde appbasis, actuele scheme CinemaSeat - Standard, arm64 tvOS Simulator; codec-exclusiecheck geslaagd. |
| Standalone build buiten appfolder | Geslaagd op arm64 macOS; dependencies source/hash-pinned zonder app-Vendor. |

## Drie cruciale integratievragen

1. Service levert echte 34-byte STREAMINFO vóór het eerste FLAC-packet; bewezen
   met echte encoders. **CS-preheaderbuffer, één demuxer, replay zonder dubbels en
   fMP4-header zijn nog niet geïmplementeerd of bewezen.**
2. Serverread/control loopt onafhankelijk van codecworker/outputcredit; volle
   outputqueue en cancel zijn hostgetest. **CS-ontvangst/native muxqueue en de
   bidirectionele clientkant ontbreken nog.**
3. Service voert absolute bronsamples met één anker en controleert drift; negatieve
   tijden, trims en ontbrekende vervolgtimestamps zijn hostgetest. **CS-origin/bias,
   nieuwe socket per seek/trackwissel en late clientcallbacks zijn niet bewezen.**

## Nog concrete toegang / materiaal nodig

- Testdoel: ODROID-N2+ met Armbian op `odroidn2.local`; fysieke Apple TV beschikbaar
  volgens gebruiker. Dockerstatus, 64-bit OS/architectuur en toegangsmethode nog
  verifiëren. Docker wordt niet op deze Mac geïnstalleerd.
- DS923+ en ODROID-N2+ voor de voorgeschreven één-uurtests met CPU/RSS/temperatuur,
  pauze, seeks en trackwissels; daarna twee/vier sessies.
- Geschikte DTS-HD MA/7.1 en TrueHD-testbronnen, met toestemming en referentiedecode.
  Geen commerciële media committen of opnemen in rapporten.
- Fysieke Apple TV plus audio-/beeldopstelling en menselijke kanaal/lipsync/HDR/DV-
  beoordeling na de Diagnose-implementatie. Geen apparaatinstallatie zonder opdracht.

## Bekende implementatiegrenzen

Canonical FLAC-speakerlayouts worden strikt gevalideerd; side-only 5.1 en float
worden momenteel geweigerd. XLL lossy recovery wordt in de gepinde servicebuild
hard afgewezen en kan DTS-HD-priming blokkeren. Geen stille corefallback, downmix,
AAC, resampling of disk-spool. Geen claim van volledige DTS-HD-ondersteuning.
Debugevents bevatten walltime/queuewatermarks; server CPU/RSS en thermiek moeten
nog extern worden gemeten. Docker-runtimechecks en benchmarkscript zijn voorbereid
maar niet op Linux/hardware uitgevoerd. Projectlicentie/distributie blijft open.
