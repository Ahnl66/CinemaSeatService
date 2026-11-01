# Implementatiebaseline — 2026-11-01

- Uitvoerings-HEAD: `f083bb1d6b40a6c8f33993b566ef8163bcde26a3`; werkboom en index waren schoon bij aanvang. Ontwerp-HEAD b69bcf0 is historisch; geen bestaande code teruggedraaid.
- App-back-upformaat 5, databaseschema 7. Settings gebruiken de bestaande draft/Save/Discard in `SettingsView.swift`. CloudSync blijft ongewijzigd.
- App-FFmpeg SOURCE_REVISION: `946fcce07b6dcd0331c8cc609192aeff5e1924f8`. Service haalt deze revision zelfstandig op; geen koppeling naar app-Vendor.
- Native brug: Standard weigert DTS/TrueHD/MLP vóór lokale decoderinitialisatie. fMP4-header gaat vóór packetloop. `CPPlaybackRelativePTS` bezit source-origin; `CPPlaybackWriteAudio` bezit videobias. Externe audio moet diezelfde eigenaren gebruiken.
- Diagnose heeft v1 rapportcontract, source-gated DTS-seekpriming en native timeline-evidence. Nieuwe externe proef mag pas na corebewijs worden aangesloten.
- Host arm64 macOS; volledige Xcode aanwezig. Docker ontbreekt. Geen DS923+, ODROID-N2+, fysieke Apple TV of private HD-testmedia gekoppeld aan deze chat. Geen hardwareacceptatie bewezen.
- Geen commit, push, installatie, versiebump, workflowdispatch of publicatie toegestaan.

Fase 0: broninventarisatie afgerond; encoder-extradata/layout-eigenschappen worden in fase 1 op de afzonderlijk gebouwde gepinde dependency gemeten.
