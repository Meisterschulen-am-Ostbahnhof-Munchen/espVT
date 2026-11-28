# Offene Punkte (Board-Test JC8012P4, 2026-11-28)

Firmware: Fork-`main` 5d51d0f (vendored AgIsoStack), Board Guition JC8012P4 (10,1"),
Steuergerät NAME 0xa00a80008b9ffed0, SA 140, VT-Version 6.

## 1. Touch-Task-Stack läuft über bei Maskenwechsel per Button – behoben (Branch `fix/touch-task-stack`)

- Symptom: "Go To DataMask_M1/M2" → `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)` in Task `gsl3680`.
- Ursache: `on_touch_released()` führt das On-Key-Release-Makro (Change Active Mask) über
  `process_macro()` direkt in der Touch-Task aus. `VirtualTerminalServer::process_connection_dependent_messages()`
  hat allein ~6,9 KB Stack-Frame, die Touch-Task hatte 4 KB. `gt911` (Waveshare) ist identisch gebaut.
- Fix: `touch::TASK_STACK_SIZE = 16384` in `main/touch.hpp`, für `gsl3680` und `gt911`. Auf dem JC8012P4 geflasht,
  seitdem kein Absturz.
- Später sauberer: Makros nicht in der Touch-Task, sondern im VT-Update-Thread ausführen.

## 2. Neuer Pool wird ignoriert, VT lädt den alten gespeicherten Pool – später

- Symptom: Auf dem Steuergerät ist ein anderer Pool, das VT zeigt aber weiter den alten. Die Version
  (Store Version / Versionslabel) scheint ignoriert zu werden.
- Log-Ablauf (gekürzt):
  ```
  I (17289) vt_view: Object pool loaded: client=0xa00a80008b9ffed0 bytes=0      <- Load Version aus dem Speicher
  I (17559) isovt: Active working set changed to 0xa00a80008b9ffed0 (SA 140)
  W (17748) [VT Server]: Ignoring retransmitted object pool data from client 140 because its object pool is already active.
  W (17794) [VT Server]: Client 140 retransmitted an already active object pool. Re-sending the successful end-of-pool response.
  ```
- Beobachtung: Das Steuergerät überträgt seinen Pool nach dem Load Version erneut, der Server verwirft
  die Daten (`isobus_virtual_terminal_server.cpp:543`) und bestätigt das End of Object Pool trotzdem
  (`:733`). Damit bleibt der alte Pool aktiv.
- **Ursache gefunden (2026-11-28, VT-Fehler):** Das Steuergerät hat nachweislich den DIDO-Pool in `/data`
  (SHA-256 da06c37d9b6c…). Ablauf im Log:
  ```
  W (110150) can: Machine ECU SA 140 inactive for 5 seconds; hiding its working set
  I (110182) isovt: Hiding inactive working set ... without deleting its cached pool
  W (169483) [VT Server]: Ignoring retransmitted object pool data from client 140 because its object pool is already active.
  W (169663) [VT Server]: Client 140 retransmitted an already active object pool. Re-sending the successful end-of-pool response.
  I (169998) pool_store: Stored 22986 object-pool bytes in /spiffs/8b9ffed0_4c4f4745414332.iop
  ```
  `4c4f4745414332` = "LOGEAC2" (DIDO-Label), aber 22986 Bytes = alter Workspace-Pool.
  1. Steuergerät startet neu → espVT behält das Working Set samt Pool im RAM (lokale Änderung).
  2. Steuergerät kommt mit neuem Pool zurück → altes Working Set wird weiterverwendet, Pool gilt als aktiv.
  3. Neue Übertragung wird als "Wiederholung" verworfen, End of Object Pool trotzdem positiv beantwortet
     (lokale Änderung im vendored Stack, `:543`/`:733`).
  4. Store Version "LOGEAC2" speichert den alten Pool unter dem neuen Label → Speicher vergiftet.
  - "Alle Pools löschen" leert nur SPIFFS, nicht das Working Set im RAM.
  - Workaround: Pools löschen, dann VT sofort neu starten.
  - Fix: Bei Neuanmeldung des Steuergeräts (Working Set Maintenance mit Initiating-Bit) bzw. neuer
    Übertragung nach Get Memory den gecachten Pool verwerfen; eine neue Übertragung nie ignorieren und
    trotzdem bestätigen. Mit dem AgIsoStack-Submodule (ohne lokale Änderungen) gegenprüfen.
- Frühere Vermutung (widerlegt, Steuergerät hat den richtigen Pool):
  - Das Steuergerät (logiBUS `App_VTClient.c`) bildet das Label als `"LOG" + CRC16(.iop)` und nimmt die
    **alphabetisch erste** `*.iop` in `/data` (`FindPoolFile()`).
  - Erwartete Labels: Workspace (alt, 22986 Bytes) = `LOGB2B8`, Workspace_DIDO (1529 Bytes) = `LOGEAC2`.
  - Angezeigt wird der Workspace-Pool (Button_A1/L1, SoftKey_F1–F12). Der DIDO-Pool hätte Button_I01–I08,
    Button_Q01–Q12. Auch nach "alle Pools löschen" am VT kommt er per neuer Übertragung → das Steuergerät
    schickt den alten Pool. Prüfen: Inhalt von `/data` auf dem Steuergerät und dessen Konsole
    (`VT  **resulting Pool Label *** (LOG....)`, `Found N object pools`).
  - Offen bleibt: warum überträgt das Steuergerät den Pool nach dem End of Object Pool ein zweites Mal,
    und ist das Verwerfen (`:543`/`:733`) richtig?
- Zu klären:
  - Welches Versionslabel schickt das Steuergerät bei Get Versions / Load Version, und hat der neue
    Pool ein anderes Label? Gleiches Label mit anderem Inhalt → VT lädt zu Recht den alten Stand.
  - Die Logik "already active → ignorieren" ist eine lokale espVT-Änderung im vendored Stack
    (prüfen, ob es sie upstream gibt). Eine Übertragung nach einem aktiven Pool ist laut ISO 11783-6
    eine gültige Pool-Aktualisierung und darf nicht verworfen werden.
  - Verhalten mit dem AgIsoStack-Submodule (`trial/agisostack-submodule`) gegenprüfen.
  - CAN-Mitschnitt (candump) von Get Versions / Load Version / Object Pool Transfer.

## 3. Display startet auf Polnisch, schaltet erst dann auf Deutsch – später

- Symptom: Nach dem Booten erscheinen die UI-Texte zuerst polnisch, danach deutsch.
- Im Log sichtbar: im einen Boot `vt_view: ISOBUS VT i przypisania AUX-N gotowe` bzw.
  `Przetwarzanie puli obiektów ISOBUS` (polnisch), in einem anderen `ISOBUS-VT und AUX-N-Zuordnung bereit`.
- Vermutung: `ui_text` nutzt bis zum Laden der gespeicherten `TerminalSettings` (Sprache) eine
  polnische Vorgabe, oder die Sprache wird erst mit dem Start des VT / der Sprachmeldung gesetzt.
  Prüfen: Reihenfolge in `app_main.cpp` (Status-View vor dem Laden der Settings?) und Default-Sprache in
  `ui_text.hpp` / `vt_scene_renderer.cpp`.

## 4. Menü "Maschinen": Versionslabel der gespeicherten Pools anzeigen – später

- Heute zeigt das Menü pro Steuergerät nur "SA", "NAME" und "gespeicherte Versionen: N" sowie
  "Speicher: N Pools".
- Gewünscht: jedes gespeicherte Versionslabel im Klartext (z. B. `LOGEAC2`), am besten mit Größe und
  Datum, und welches davon gerade aktiv ist. Dann wäre Problem 2 sofort sichtbar gewesen
  (Label `LOGEAC2`, aber 22986 Bytes). Optional einzelne Versionen löschen.
- Außerdem den Hersteller des Steuergeräts zeigen: Manufacturer Code aus dem NAME (Bits 21–31), wenn
  möglich mit Herstellernamen aus der ISOBUS-Herstellerliste (isobus.net), sonst nur die Nummer.
  Beispiel: NAME A00A80008B9FFED0 → Manufacturer Code 1116. Gleiches gilt für Funktion/Geräteklasse.
- Quelle: `object_pool_store` (Dateiname `<NAME low 32 bit>_<Label hex>.iop`) → Label aus dem Dateinamen
  zurückwandeln, Größe per `stat`.

## 5. Buttons ohne Fläche und Rahmen → Change Background Colour unsichtbar – umgesetzt, Test am Board offen

- Symptom (DIDO-Pool): Ein-/Ausgänge sind Buttons 80x80 (Hintergrund 8 grau, Rahmen 15 navy, Q latchable)
  mit transparenter Output Number darin. Auf espVT fehlen Fläche und Rahmen; Change Background Colour
  vom Steuergerät (Zustandsanzeige) ist nicht zu sehen. Touch geht, Ausgang schaltet.
- Ursache: `main/vt_scene_renderer.cpp` (`case Type::Button`, ~Zeile 1191) zeichnet absichtlich nichts außer den
  Kindern; Kommentar (jaworeA, 1f240216): "AgIsoVirtualTerminal leaves the button face and its proprietary
  border transparent".
- Soll nach ISO 11783-6 (Button-Objekt, Options-Bits): Fläche in Hintergrundfarbe, außer Bit 3
  TransparentBackground; Rahmen in Rahmenfarbe, außer Bit 2 SuppressBorder bzw. Bit 5 NoBorder (dann Fläche
  über die ganze Größe); Kinder im Inneren der Fläche; latched/gedrückt sichtbar (Bit 1); Disabled (Bit 4)
  proprietär, z. B. ausgegraut.
- Bei der Umsetzung: mateo (jaworeA) als Autor seines Codes respektieren, Änderung mit Co-Authored-By.

## 6. Darstellung mit AgIsoVirtualTerminal (halb)automatisch abgleichen – später

espVT enthält keinen AgIsoVT-Code (AgIsoVT zeichnet mit JUCE, espVT mit `vt_scene_renderer.cpp`); einziges
Submodule bleibt AgIsoStack++. Abgleichen lässt sich das Ergebnis:

1. **Referenzbilder (zuerst):** `vt_scene_renderer.cpp` auf dem Host übersetzen, `lcd_panel` zeichnet in einen
   RGB565-Puffer → PNG. Pool-Korpus: logiBUS-Workspaces (DIDO, PWM, Joystick, Scroll, …) und AgIsoStack-Testpools.
   Dieselben Pools in AgIsoVT rendern (Linux, `xvfb`, Screenshot-Modus) und ISO-Designer-Vorschau dazu.
   Skript vergleicht und erzeugt eine Übersicht (Pool, Maske, 3 Bilder, Abweichung in %).
   Hätte Punkt 5 (Buttons ohne Fläche/Rahmen) sofort gezeigt; hilft auch beim Debuggen ohne Board.
2. **Gemeinsame Regeln upstream:** Reine Regeln (Button-Fläche/Rahmen, Transparenz laut Optionen, sichtbare
   Maske #340) als kleine Helfer mit Unit-Tests in AgIsoStack++, beide VTs nutzen sie.
3. **Änderungen beobachten:** Wöchentliche Prüfung der AgIsoVT-Commits an Render-Komponenten
   (z. B. `ButtonComponent`, `OutputNumberComponent`), Liste der passenden Stellen in `vt_scene_renderer.cpp`,
   Entscheidung von Hand.

## 7. Hardware-Notiz Adapterboard

- Kein Signal an RX (`PIN:1 EDGE:0`): VIO am Transceiver fehlte, Ursache war eine schlechte Lötstelle
  am ZIF-Pin 13/14 (VDDA 3,3 V). Nach Nachlöten läuft CAN.
