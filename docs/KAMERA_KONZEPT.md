# Konzept: Kamerabilder im espVT

Stand: 14.11.2026 · Status: **Entwurf zur Diskussion**

Grundlage sind die Demos auf dem Guition JC8012P4A1C_I_W_Y (ESP32-P4). Alle Zahlen in
diesem Dokument sind dort gemessen, sofern nicht anders gekennzeichnet:

| Demo | Repo | Ergebnis |
|---|---|---|
| USB-Kamera (UVC) | `JC8012P4A1C_I_W_Y_UVC_Camera` | 1280x720 MJPEG, 15–20 fps |
| Eingebaute MIPI-CSI-Kamera | `JC8012P4A1C_I_W_Y_CSI_Camera` | 1080p 15 fps oder 1288x728 30 fps |
| WLAN über ESP32-C6 | `JC8012P4A1C_I_W_Y_ESP_Hosted` | ESP-Hosted 2.11.7, C6-Update vom P4 aus |
| Funkkamera (Freenove S3) | `JC8012P4A1C_I_W_Y_WiFi_Camera` | 800x600 MJPEG, 25 fps, 5,4 Mbit/s |

---

## 1. Ziele und Abgrenzung

**Ziele**

- Kamerabilder am Terminal zeigen, typischerweise Heck/Anhänger beim Rückwärtsfahren und
  Arbeitsbereiche am Gerät (Ballenkammer, Verteiler, Bunker).
- Kamera und ISOBUS-VT gleichzeitig sehen.
- Automatisches Umschalten auf die Rückfahrkamera.

**Nicht-Ziele (vorerst)**

- **Kein Spiegelersatz** im Sinne von UN ECE R46 / ISO 16505. Das Kamerabild ist eine
  Fahrhilfe. Die Latenz messen wir trotzdem (Abschnitt 8).
- Keine Videoaufzeichnung (später ggf. Standbild auf SD-Karte).
- Keine IP-Kameras mit H.264/RTSP. Der ESP32-P4 hat einen H.264-Encoder, aber keinen
  Hardware-Decoder.
- Kein Kamera-Objekt im VT-Objektpool. ISO 11783-6 kennt kein Video. Kamera ist eine
  **terminal-lokale Funktion**, wie das MENU.

---

## 2. Kamera-Varianten

### 2.1 Übersicht

| # | Variante | Anschluss am JC8012 | Format | Status | Priorität |
|---|---|---|---|---|---|
| A | **USB-Kamera (UVC, MJPEG)**: Webcams, Kameramodule | USB-HS (Typ-C „High speed USB“) | MJPEG bis 1280x720 | getestet: Logitech C270 (isochron), „ESP32 CAM“-Modul (Bulk, Full Speed) | **hoch** |
| B | **Analoge Fahrzeugkamera (CVBS/PAL, AHD, TVI, CVI)** über Wandler + USB-Grabber | USB-HS | MJPEG vom Grabber | getestet: AHD 720p25 und PAL, jeweils über HDMI-Wandler + UGREEN-Grabber | **hoch** |
| C | **WLAN-Kamera (MJPEG über HTTP)** | WLAN über ESP32-C6 | MJPEG | getestet: Freenove ESP32-S3 mit OV2640 bzw. GC0308 | **mittel** |
| D | **Eingebaute MIPI-CSI-Kamera** (OV02C10, Front) | intern | RAW → ISP | getestet | niedrig |
| E | IP-Kamera (H.264/RTSP, Ethernet) | – | H.264 | nicht machbar (kein HW-Decoder, kein Ethernet) | – |

### 2.2 Bewertung

**A: USB-UVC** ist der Kern. Es gibt viele Kameras dafür, die Bildqualität ist gut, und die
Kette ist einfach. Voraussetzungen:

- MJPEG muss angeboten werden. YUY2 allein reicht nicht, die Bandbreite wäre zu hoch.
- Isochrone und Bulk-Kameras laufen beide. Für Bulk ist die korrigierte `usb_host_uvc`
  nötig (gemeldet als [esp-usb#651](https://github.com/espressif/esp-usb/issues/651)).
- Format und Bildrate wählt die Software aus der Liste, die die Kamera meldet:
  größtes MJPEG ≤ Zielgröße, höchstens 30 fps.
- Mehrere USB-Kameras über einen USB-2.0-Hub sind **noch nicht getestet**.
  `usb_host_uvc` kann mehrere Streams. Begrenzend sind die Isochron-Bandbreite und die
  FIFO-Größe des P4 (vgl. esp-usb#538). Bulk-Kameras sind dafür unkritischer.

**B: Analoge Kameras** sind in der Landtechnik verbreitet (12 V, robust, vorhandene
Verkabelung). Es gibt sie in mehreren untereinander inkompatiblen Formaten: CVBS
(PAL/NTSC), AHD 720p/1080p, TVI und CVI. Die getestete Kette aus Kamera,
AHD-zu-HDMI-Wandler und HDMI-zu-USB-Grabber funktioniert, ist aber umständlich. Besser
wäre ein einzelner **Analog-zu-USB(UVC)-Wandler** ohne HDMI dazwischen; ein solches Gerät
muss noch beschafft und getestet werden. Erfahrungen bisher:

- Der Wandler muss das Kameraformat können. Sonst blendet er „Video Format Not Supported“
  ein.
- Wenn Kamera und Wandler gleichzeitig einschalten, erkennt der Wandler das Format
  manchmal nicht. Einmal Aus- und Einstecken half. Im Fahrzeug braucht es dafür eine
  Lösung, z. B. die Kamera zuerst bestromen.
- PAL hat nur 720x576 bei 25 Bildern pro Sekunde, im Zeilensprungverfahren. Das Bild ist
  weicher als bei AHD.

**C: WLAN-Kameras** brauchen keine Kabel zum Anhänger. Die Grenzen:

- Der ESP32-C6 kann Wi-Fi 6 nur als Station. Als Access Point funkt er mit 802.11n.
  Laut Espressif sind real etwa 20–30 Mbit/s TCP möglich, also 3–5 Streams mit
  800x600@25 (je ~5,4 Mbit/s).
- **Bei mehreren Funkkameras muss das Terminal der Access Point sein.** Eine Station kann
  nur mit einem AP verbunden sein. In der Demo ist es noch umgekehrt: die Kamera ist AP.
- Ergebnisse aus der Demo: Ohne `TCP_NODELAY` auf der Kameraseite brach die Bildrate auf
  2–14 fps ein. Nach dem Verbinden dauert es etwa 20 s, bis die WLAN-Strecke ihre volle
  Rate erreicht.
- Kameraseitig bieten sich ESP32-S3 mit OV2640/OV5640 an (JPEG vom Sensor). Später eine
  P4-Kamera mit Hardware-JPEG für 720p (Waveshare ESP32-P4-WIFI6, vorgemerkt).

**D: Eingebaute Kamera** schaut zum Fahrer. Als Maschinenkamera ist sie ungeeignet. Denkbar
wäre sie später für QR-Codes oder Dokumentation.

### 2.3 Boards

| | JC8012 (ESP32-P4) | Waveshare 7B (ESP32-S3) |
|---|---|---|
| JPEG-Decoder | Hardware | nur Software (`esp_new_jpeg`, ~18 fps bei 640x480 laut Espressif, belegt einen Kern) |
| Skalieren/Drehen | PPA (Hardware) | Software |
| USB-Kamera | USB-HS-Port | **nein**: USB teilt sich GPIO19/20 mit CAN |
| WLAN | ESP32-C6 (ESP-Hosted) | intern (wie in der NTRIP-Variante) |
| **Empfehlung** | volle Kamerafunktion | höchstens eine WLAN-Kamera, klein (QVGA/VGA), Vollbild oder Bild-im-Bild |

---

## 3. Layouts

### 3.1 Heute (JC8012, 1280x800, Datenmaske 480)

```
┌──────┬───────────────────────────────┬──────────┬────┐
│  WS  │                               │ Softkeys │ M  │
│Leiste│        Datenmaske             │  2 x 6   │ E  │
│ 123  │        800 x 800              │   267    │ N  │
│      │     (480 logisch x 5/3)       │          │ U  │
└──────┴───────────────────────────────┴──────────┴────┘  83
```

Der Bildschirm ist voll belegt; neben der VT ist kein Platz frei.

**Wichtig:** Die Skalierung ist eine reine Anzeige-Sache. Die VT meldet den Geräten weiter
480 bzw. 600 Pixel Maskengröße und 2x6 Softkeys. Ein Layoutwechsel braucht also **kein
Neuladen der Objektpools** und ist für die Geräte unsichtbar. `current_layout()` rechnet
schon mit Skalierungsfaktor und Ursprung; das muss nur pro Layout einstellbar werden.

### 3.2 Layout-Katalog

**L0: ISOBUS Vollbild** (wie heute)

**L1: Kamera Vollbild**

```
┌───────────────────────────────────────────────────┬────┐
│                                                   │ M  │
│                Kamera 1197 x 673 (16:9)           │ E  │
│                bzw. 1067 x 800 (4:3)              │ N  │
│                                                   │ U  │
└───────────────────────────────────────────────────┴────┘
```

- Die MENU-Leiste bleibt sichtbar, damit es sicher zurück geht.
- Die VT läuft im Hintergrund weiter, inklusive Alarmen (Abschnitt 5).

**L2: ISOBUS + 1 Kamera** (empfohlenes Standard-Split)

```
┌─────────────────────┬──────┬────────────────────────┬────┐
│                     │ Soft │                        │ M  │
│  Datenmaske 1:1     │ keys │   Kamera 557 x 313     │ E  │
│  480 x 480          │ 160  │   (16:9)               │ N  │
│                     │      │   oder 557 x 418 (4:3) │ U  │
├─────────────────────┴──────┤                        │    │
│ frei 640 x 320:            │                        │    │
│ WS-Auswahl / Status        │                        │    │
└────────────────────────────┴────────────────────────┴────┘
```

- Die VT wird **1:1** angezeigt (480 logische Pixel = 480 Bildpunkte). Die Datenmaske ist
  dann etwa 8 cm breit. Schrift und Bedienelemente bleiben lesbar, die Touch-Ziele werden
  kleiner. Am Gerät prüfen.
- Die Working-Set-Leiste fällt hier weg. Die Auswahl läuft über MENU oder die freie Fläche
  unter der Maske.
- Bei Datenmaske 600 wird es enger. Die VT bräuchte 800x600, für die Kamera blieben 397 px
  Breite. Für Split-Layouts deshalb Maske 480 empfehlen oder die VT auf 0,8 skalieren.

**L3: ISOBUS + 2 Kameras**: wie L2, aber zwei Kameras rechts übereinander, je 557x313
(16:9). Zusammen 626 px hoch, das passt.

**L4: 2 Kameras nebeneinander** (ohne VT): je 598x336 (16:9) bzw. 598x449 (4:3)

**L5: 4 Kameras (2x2)**: je 598x336, zusammen 1197x673

**L6: Bild-im-Bild über der VT**: nur als **vorübergehendes Popup**, z. B. ein kleines
Rückfahrbild, wenn L1 nicht gewünscht ist. Es verdeckt VT-Inhalt und ist deshalb nicht als
Dauerzustand gedacht.

### 3.3 Bedienung

- **MENU-Taste „Kamera“**: schaltet durch L0 → L1 → L2 (wählbar, welche Layouts im Zyklus
  sind).
- **Tippen auf ein Kamerabild**: dieses Bild als Vollbild (L1), nochmal tippen: zurück.
- **Langer Druck auf ein Kamerabild**: nächste Kamera in diesem Fenster.
- Layout, Kamerazuordnung und Einstellungen werden in den `TerminalSettings` (NVS)
  gespeichert.

---

## 4. Automatisches Umschalten

### 4.1 Rückwärtsfahrt

espVT hört den Bus ohnehin mit. Die Fahrtrichtung steht in den TECU-Nachrichten nach
ISO 11783-7, sofern der Traktor sie sendet. **Bitte am realen Traktor prüfen.**

- Wheel-based Speed and Distance (PGN 65096): „machine direction“
- Ground-based Speed and Distance (PGN 65097): „machine direction“
- Machine Selected Speed (PGN 61474): Richtung

Ablauf:

1. Rückwärts erkannt → Rückfahrkamera als L1, oder als Popup L6 (einstellbar).
2. Wieder vorwärts → nach einer einstellbaren Verzögerung (z. B. 3 s) zurück zum vorigen
   Layout.
3. Ohne TECU-Daten gibt es keine Automatik. Alternativ ein Schalteingang am Terminal,
   etwa das Rückfahrlicht über einen Optokoppler; dafür ist auf FPC3 noch ein GPIO frei.

### 4.2 Auslösung durch das Gerät

Dafür gibt es **keinen Standardweg**: Weder VT noch AUX-N haben ein Kommando „zeige
Kamera“. Denkbar wäre eine Zuordnung im Terminal, z. B. „wenn Working Set X Datenmaske Y
aktiviert, zeige Kamera Z im Split“. Das ist herstellerspezifisch und später zu bewerten.

---

## 5. ISOBUS-Verträglichkeit

- **Alarme haben Vorrang.** Aktiviert ein Working Set eine Alarmmaske, verlässt das Terminal
  L1, L4 und L5 und zeigt die VT (L0, oder L2 mit der Alarmmaske). Akustische Alarme laufen
  unabhängig vom Layout.
- **Die VT-Kommunikation darf nie leiden.** Status-Nachrichten, Antworten auf Kommandos und
  das Laden von Objektpools haben Vorrang vor der Kamera. Die Kamera läuft in eigenen Tasks
  mit niedrigerer Priorität. Achtung bei der Lock-Reihenfolge: `render_request_mutex` und
  die Working-Set-Liste haben schon einmal einen Deadlock verursacht; dafür gibt es den Test
  `test_render_lock_order.py`.
- **Gemeldete VT-Eigenschaften bleiben unverändert** (Maskengröße, Softkeys, Farben), siehe
  3.1. Eine AEF-Konformität der VT wird von der Kamera nicht berührt.
- **Touch:** Im Split-Layout gilt die Treffer-Tabelle der VT nur im VT-Bereich, die
  Kamerafenster bekommen eigene Ziele.

---

## 6. Technische Architektur (JC8012)

```
 Quellen                       Dienste                        Anzeige
┌──────────────┐
│ UVC (USB-HS) │──┐
├──────────────┤  │   ┌───────────────┐   ┌──────────────┐   ┌──────────────────┐
│ WLAN-MJPEG   │──┼──▶│ camera_manager│──▶│ JPEG-Decoder │──▶│ Compositor (PPA) │──▶ DSI
├──────────────┤  │   │ (Quellen,     │   │ (Hardware,   │   │ Layout, Skalieren│
│ CSI (später) │──┘   │  Format, fps) │   │  1 Engine)   │   │ Drehen 270°      │
└──────────────┘      └───────────────┘   └──────────────┘   └──────────────────┘
                                                                     ▲
                                        VT-Renderer (wie heute) ─────┘
```

- **`camera_manager`**: verwaltet die Quellen. Pro Quelle wählt er das Format passend zur
  Fenstergröße des aktuellen Layouts. Das kann die UVC-Demo schon: größtes MJPEG ≤ Ziel,
  ≤ 30 fps. Beispiel: Für 557x313 wird 640x360 oder 640x480 angefordert, nicht 1280x720.
  Es wird immer nur das neueste Bild verarbeitet.
- **JPEG-Decoder**: eine Hardware-Engine für alle Quellen, nacheinander.
- **Compositor**: Die DSI-Ausgabe hat keine Hardware-Ebenen; die P4-DPI-Schnittstelle kann
  keine Bilder überlagern. Alles wird mit der PPA in den hochkant liegenden Panel-Puffer
  geschrieben:
  - VT-Bereich: wie heute, nur bei Änderung neu gedreht.
  - Kamerafenster: pro Kamerabild direkt mit der PPA an ihre Position im Panel-Puffer
    (skalieren + 270° drehen), wie in den Demos.
  - Die beiden Panel-Puffer werden über Dirty-Rects synchron gehalten. Ein volles Neudrehen
    von 1280x800 kostet etwa 34 ms und ist nur beim Layoutwechsel nötig.
  - Ein Bildwechsel gibt es nur, wenn sich gegenüber dem **sichtbaren** Bild etwas
    geändert hat (neues Kamerabild oder VT-Änderung). Der zweite Puffer holt seine Liste
    erst beim nächsten Bildwechsel nach.
  - **Im Proof of Concept bestätigt**, siehe 6.2.
- **Kconfig**: `ISOVT_ENABLE_CAMERA`, nur für esp32p4. Bei abgeschalteter Option bleiben
  Stubs, wie bei `ISOVT_ENABLE_AGISOSTACK`.

### 6.1 Leistungsbudget (gemessen)

| Engpass | Messwert | Folge |
|---|---|---|
| PPA skalieren/drehen | ~30 Mpx/s (Eingangspixel zählen, Drehen kostet nichts extra) | 1280x720 pro Bild ≈ 31 ms; 2 x 640x480 ≈ 20 ms |
| Ein Kanal 720p MJPEG (Decoder + PPA + Bildwechsel nacheinander) | 20 fps | ein Vollbild-Kanal ≥ 20 fps |
| WLAN (C6, ein Stream) | 25 fps bei 800x600, 5,4 Mbit/s | 3–5 Streams theoretisch |
| USB Full Speed (z. B. „ESP32 CAM“-Modul) | 720p 15 fps | Kamera-Grenze |

Abschätzung für L3/L4: zwei Kameras mit je 640x480 bei 20 fps ergeben 12 Mpx/s PPA-Eingang.
Das ist machbar. Der Decoder braucht 3,8 ms pro 640x480-Bild (gemessen, 6.2), also rund
150 ms pro Sekunde für 40 Bilder.

### 6.2 Proof of Concept: Compositor (14.11.2026)

Projekt `1-Demo/compositor_poc`. Eine VT-Attrappe (Querformat-Puffer wie in espVT) mit
animierten Balken und Zähler erzeugt 40 Dirty-Rects pro Sekunde. Dazu kommt eine
USB-Kamera (Logitech C270, 640x480 MJPEG bei 30 fps). Die Layouts L0, L2 und L1 wechseln
alle 10 s.

| Layout | Bildwechsel/s | Kamera | PPA-Zeit pro Bildwechsel |
|---|---|---|---|
| L0 VT Vollbild | 10 (= VT-Takt) | – | 6–7 ms VT |
| L2 VT 1:1 + Kamera 557x418 | 40 (30 Kamera + 10 VT) | 30 fps (volle Kamerarate) | 7,1 ms Kamera + 0,8 ms VT |
| L1 Kamera Vollbild 1067x800 | 30 | 30 fps | 13 ms |

- JPEG-Decoder: 3,8 ms pro 640x480-Bild.
- Die PPA ist zu 30–40 % ausgelastet. Für eine zweite Kamera ist Luft.
- Volles Neudrehen nur beim Layoutwechsel.
- Die Abbildung der 270°-Drehung (Querformat → Panel) ermittelt der PoC beim Start aus
  einem Testpixel.
- Optisch am Gerät geprüft: alle drei Layouts richtig, keine Reste alter Inhalte auf den
  Fotos.

### 6.3 Proof of Concept 2b: USB- und WLAN-Kamera gleichzeitig (14.11.2026)

Projekt `1-Demo/compositor_2cam`. Es nutzt den Compositor aus 6.2 mit zwei getrennten
Quellen und einem gemeinsamen Hardware-Decoder:

- **USB:** Logitech C270, 640x480 MJPEG
- **WLAN:** Freenove ESP32-S3 mit OV2640, 800x600 MJPEG über HTTP. Das JC8012 verbindet
  sich über den C6 (ESP-Hosted 2.11.7) mit dem Access Point der Kamera.

Neu sind die Layouts L3 (VT 1:1 + zwei Kameras übereinander) und L4 (zwei Kameras
nebeneinander).

| Layout | USB 640x480 | WLAN 800x600 | Bildwechsel/s | PPA pro Bildwechsel |
|---|---|---|---|---|
| L3 VT + 2 Kameras | 15 fps (Kamera-Grenze) | 25 fps (volle Rate) | 30 | 7,4 + 10,4 ms |
| L4 2 Kameras | 15 fps | 25 fps | 30 | 7,4 + 10,9 ms |
| L1 WLAN Vollbild | – | 25 fps | 25 | 17,8 ms |

- Decoder: 3,9 ms (640x480) bzw. 5,8 ms (800x600) pro Bild. Bei 15 + 25 fps ist er zu
  etwa 20 % belegt.
- PPA bei zwei Kameras: etwa 55 %. Zwei Kameras passen gut, für eine dritte wird es eng.
  Dann kleinere Formate anfordern.
- Beide Kameras werden auch dekodiert, wenn sie gerade nicht sichtbar sind. Das ließe sich
  einsparen.

Erkenntnisse:

- **WLAN hängt stark von der Umgebung ab.** Während eines starken WLAN-Uploads im selben
  Raum sank die Funkkamera auf 1–4 fps, bei sehr gutem Signal (RSSI −11 dBm). Sendezeit,
  nicht Signalstärke, ist der Engpass. Konsequenzen:
  - Sicherheitsrelevante Bilder (Rückfahrkamera) **per Kabel**.
  - Bei Funk 5 GHz prüfen (ESP32-C5).
  - Das Terminal sollte die Bildrate jeder Kamera überwachen und einen Einbruch anzeigen.
- **Hochlaufzeit WLAN:** Nach dem Verbinden dauert es 20–60 s, bis die volle Bildrate
  erreicht ist.
- **Die C270 halbiert ihre Bildrate bei wenig Licht selbst** (30 → 15 fps). Das ist kein
  Engpass im Terminal.
- **USB-Host auf Core 0 oder Core 1** macht keinen messbaren Unterschied.

---

## 7. Einstellungen pro Kamera

- Name (z. B. „Heck“, „Ballenkammer“)
- Quelle: USB-Port/Seriennummer bzw. WLAN-Adresse
- Spiegeln (Rückfahrkameras liefern oft ein gespiegeltes Bild), Drehen 0/90/180/270
- Hilfslinien für Rückfahrt (einblendbar, Position einstellbar)
- Bevorzugte Auflösung / maximale Bildrate
- Rolle: „Rückfahrkamera“ (für die Automatik)

---

## 8. Test und Abnahme

- **Latenz** von der Kamera bis zum Display messen: z. B. Stoppuhr am Bildschirm abfilmen.
  Richtwert für Fahrhilfen ≤ 200 ms.
- **Dauerlauf** 8 h mit Kamera und VT gleichzeitig: Speicher, Temperatur, keine
  VT-Timeouts.
- **Kamera abziehen und wieder anstecken** während des Betriebs, WLAN-Abriss.
- **Alarmmaske** während Vollbild-Kamera.
- **Spannungseinbruch beim Starten** (Kamera, Wandler und Terminal gemeinsam).

---

## 9. Vorschlag für Phasen

| Phase | Inhalt | Ergebnis |
|---|---|---|
| 1 | `camera_manager` mit einer USB-UVC-Quelle; L1 Vollbild über MENU; Alarm-Durchbruch | erste Kamera im espVT |
| 2 | Compositor mit Dirty-Rects (PoC erfolgreich, 6.2); L2 ISOBUS + Kamera; Rückfahr-Automatik über TECU | Kernfunktion |
| 3 | WLAN-Kameras, Terminal als Access Point; mehrere Quellen (USB + WLAN im PoC erfolgreich, 6.3); L3/L4/L5 | mehrere Kameras |
| 4 | Hilfslinien, Spiegeln, Einstellungsmenü; Analog-zu-USB-Wandler testen | Feldtauglichkeit |
| 5 (optional) | Standbild auf SD-Karte; CSI-Kamera; Waveshare 7B mit kleiner WLAN-Kamera | Extras |

---

## 10. Offene Punkte und Risiken

**Noch zu prüfen (Proof of Concept)**

| # | Thema | Frage | Status |
|---|---|---|---|
| 2a | Zwei USB-Kameras über einen Hub | Hält der P4-Host mehrere UVC-Streams? Geht ein Full-Speed-Gerät hinter einem High-Speed-Hub (Split-Transaktionen)? Gemischt isochron/Bulk? | offen, Hub wird besorgt |
| 2b | USB + WLAN gleichzeitig | Decoder und PPA bei zwei Quellen | **erledigt**, siehe 6.3 |
| 3 | Terminal als Access Point | C6 als SoftAP über ESP-Hosted, mehrere Funkkameras als Station | offen |
| 4 | Latenz | ms von der Kamera bis zum Display, pro Kameratyp | offen |
| 5 | Analog-zu-USB in einem Gerät | ohne HDMI-Zwischenstufe | offen, Hardware fehlt |

**Einbau in espVT**

- espVT hat noch keine Dirty-Rects: `present()` dreht heute jedes Mal den ganzen
  Bildschirm. Renderer und `lcd_panel` müssen geänderte Bereiche melden. Das ist der
  größte Umbau.
- `current_layout()` braucht Maßstab und Ursprung pro Layout; Treffertabelle und
  Touch-Abbildung müssen mitgehen.
- Touch auf Kamerafenstern (Vollbild, Kamera wechseln).
- Bedienung im lokalen Menü, Texte in `ui_text.hpp` für alle Sprachen.
- Speicherbudget im PSRAM: pro Quelle Eingangs- und Decoder-Puffer (2 x 1280x960x2 Byte).

**Fahrzeug und Praxis**

- Stromversorgung: Wie viel Strom liefert der USB-Port des JC8012 (5 V aus `USB5V_IN`)
  für Kameras und Hub? Im Fahrzeug: 12/24 V auf 5 V, Kaltstart, Einschaltreihenfolge
  (Analog-Wandler erkennen das Format sonst nicht).
- Stecker und Kabel: USB ist im Fahrzeug empfindlich. Kameras mit USB-C ohne
  CC-Widerstände brauchen fest anliegende 5 V.
- WLAN: Kameras koppeln, Passwörter verwalten, fremde Geräte abweisen. Störungen durch
  andere Netze (siehe 6.3).

---

## 11. Offene Fragen

1. Welche Kameras sind an den Maschinen tatsächlich verbaut? Analog (welches Format?) oder
   schon digital?
2. Wie viele Kameras gleichzeitig sind realistisch? Davon hängt ab, ob L5 (4 Kameras)
   gebraucht wird.
3. Senden die Traktoren die Fahrtrichtung über TECU-Nachrichten, oder braucht es einen
   Schalteingang?
4. Reicht das Split-Layout L2 mit der VT 1:1 in der Bedienung, oder muss die VT größer
   bleiben (dann Kamera als Popup)?
5. Soll der Waveshare 7B überhaupt Kameras bekommen?
6. Funkkameras: eigenes Netz des Terminals (Terminal als AP) oder ein vorhandenes WLAN am
   Fahrzeug?
