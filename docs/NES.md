# MeowKit NES — v0.11.2-nes.2

Native SD-App mit überarbeiteter Tonausgabe auf FeralCat v0.11.2. Die vorherige Version `nes.1` wurde vom Nutzer auf dem MeowKit getestet: flüssige Grafik, aber Tonaussetzer bei Mario und Castlevania III. Diese Fassung behebt die gefundenen Probleme in Pufferung und Taktsteuerung. **Die neue Audioqualität und das Laden der NES-App von SD müssen noch am Gerät bestätigt werden.**

## Installation

Für ein MeowKit mit vorhandener FeralCat-Installation und der bisherigen Dual-OTA-Partitionierung:

1. Den Inhalt des SD-ZIP auf die SD-Karte kopieren. `firmware.bin` liegt anschließend direkt im Hauptverzeichnis.
2. Eigene `.nes`-Dateien unter `/roms/nes/` ablegen. Unterordner werden unterstützt.
3. Auf dem MeowKit **Settings → System → Update → Update from SD** wählen und das Update ausführen. Während des Schreibens die Stromversorgung beibehalten.
4. Nach dem Neustart die Kachel **NES** im Apps-Menü öffnen und ein Spiel auswählen.

Die native App liegt unter `/apps/nes/`: `app.elf`, `app.elf.sig` und `manifest.ini`. Sie enthält ROM-Browser, Ordnernavigation und Pausenmenü. Die Firmware stellt über die versionierte NES-Schnittstelle den Emulator-Kern, Grafik, Audio, Eingaben und Spielstanddienst bereit. App und ROMs werden von SD geladen; ausgeführt wird aus Arbeitsspeicher. Der Kern selbst bleibt in dieser Ausbaustufe Teil der Firmware.

**Zuerst die mitgelieferte Firmware aktualisieren**, dann die NES-Kachel starten. `nes_api=1` kennzeichnet die benötigte Schnittstelle. Das Apps-Menü übernimmt NES aus dem SD-Verzeichnis; es gibt keine fest eingebaute NES-Kachel mehr. Ohne `/apps/nes` ist NES nicht installiert. Nach Änderungen am App-Verzeichnis neu starten, damit der Katalog neu eingelesen wird.

Die App ist signiert. „Allow unsigned apps“ muss nicht aktiviert werden. Der bisherige FeralCat-Schlüssel bleibt gültig; diese lokale Firmware vertraut zusätzlich dem lokalen Entwicklungsschlüssel für die NES-App. Dessen privater Schlüssel ist in keinem Paket enthalten. Ein Internet-Update lädt weiterhin die offizielle FeralCat-Firmware, die diesen lokalen NES-Dienst und zusätzlichen Schlüssel noch nicht enthält.

Das Paket enthält Firmware, signierte native App, Anleitung, leere ROM-/Save-Struktur und Prüfsummen. Spiele sind nicht enthalten. Der unveränderte Ausgangsstand liegt separat in `C:\Projekte\Meowkit\FeralCat-0.11.2`, die Entwicklung in `C:\Projekte\Meowkit\FeralCat-NES`.

## Bedienung

| Aktion | Eingabe |
|---|---|
| Ordner/Spiel auswählen | Joystick hoch/runter; links/rechts blättert seitenweise |
| Öffnen/starten | A oder Eintrag antippen |
| Übergeordneten Ordner / Launcher | B im Browser |
| NES-Steuerkreuz | Physischer Joystick |
| NES A / B | Physische A- / B-Taste, auch dauerhaft gehalten |
| Select | Linker Bildschirmrand oben: `SEL` |
| Start | Rechter Bildschirmrand oben: `START` |
| Pause | Rechter Bildschirmrand unten: `MENU` |
| Fortsetzen / Sound / Reset / Speichern / Schlaf / Beenden | Pausenmenü |

Das NES-Bild bleibt vollständig in 256×240 Pixeln sichtbar. Die beiden 32-Pixel-Ränder enthalten die Touch-Tasten. Eine physische Taste kann zusammen mit Touch gehalten werden. Langes B beendet ein Spiel nicht. Der Power-Kurzdruck schläft bei eingeschaltetem FeralCat-Schlafmodus; im Pausenmenü lässt sich Schlaf ausdrücklich auswählen. Vor Schlaf werden Ton angehalten und Cartridge-SRAM gespeichert. Ein Speicherfehler bricht den Schlafvorgang sichtbar ab.

Der Browser zeigt bis zu 512 Einträge je Ordner. Er prüft höchstens 4.096 Verzeichniseinträge pro Scan und meldet eine Begrenzung sichtbar. ROM-Pfade dürfen insgesamt 255 Bytes lang sein.

## Dateiformate und Spielstände

Unterstützt werden geprüfte iNES-Dateien bis 2 MiB mit Mapper **0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 23, 24 und 66**. Mapper 5, 23 und 24 sind experimentell. Eine Mapper-Implementierung garantiert noch nicht jedes Spiel dieses Typs. NES 2.0, ZIP, FDS, NSF, VS/PlayChoice und andere Mapper werden mit einer Meldung abgewiesen.

Spielstände sind das Batterie-RAM eines Moduls, keine frei setzbaren Save States. Es wird bei Pause, kontrolliertem Beenden, vor Schlaf/Abschalten und bei Änderungen zusätzlich nach 30 Sekunden gesichert. Die laufende Emulation erstellt dafür eine RAM-Kopie; ein Hintergrundauftrag schreibt und prüft diese Kopie auf SD. Vor Beenden, Reset, Schlaf und Freigabe des Speichers wird dieser Auftrag abgeschlossen. Ein Speicherfehler führt zurück ins Pausenmenü. Spiele ohne Batterie-RAM verwenden ihre ursprünglichen Passwort-/Fortschrittsregeln.

Dateien liegen unter `/saves/nes/<ROM-CRC>.sav`. Sie enthalten eine Versionskennung, ROM-Identität und Prüfsumme. Schreiben erfolgt über eine überprüfte `.tmp` und eine vorherige `.bak`. Beim Laden werden gültige Hauptdatei, temporäre Datei und Backup in dieser Reihenfolge versucht. Beschädigte Dateien werden erhalten. Ist keine der drei Generationen gültig, wird der Spielstart mit einer Meldung abgebrochen, damit ein vorhandener Spielstand nicht versehentlich ersetzt wird. Ein hartes Ausschalten kann noch nicht geschriebenen Fortschritt verlieren; FAT ist nicht transaktionssicher.

USB-Speichermodus und Emulator greifen nicht gleichzeitig auf die SD-Karte zu. Während der Spielsession wird WLAN vorübergehend ausgeschaltet und danach wiederhergestellt. Die Lautstärke stammt aus den Geräteeinstellungen; Sound lässt sich im Pausenmenü aus-/einschalten.

## Änderungen an der Tonausgabe

PCM-Ton wird vollständig vor der LCD-Übertragung eingereiht. Ein nur teilweise angenommener Tonblock wird vervollständigt, bevor der Kern den nächsten Frame berechnet. Bei eingeschaltetem Ton bestimmt der Verbrauch der I2S-Ausgabe den Takt; die bisherige zusätzliche Frame-Wartezeit entfällt. Vor dem ersten Ton und nach einer Versorgungslücke wird ein kleiner Vorrat aufgebaut. Der Audiotreiber wird nach seiner Installation nicht erneut gestartet.

Grafik bleibt bei 256×240 Pixeln. Reicht das Zeitbudget nicht, werden einzelne LCD-Aktualisierungen ausgelassen, während CPU und Ton weiterlaufen. Ein einzelner langsamer Display-Transfer darf die Bildausgabe nicht dauerhaft unterdrücken. Bei ausgeschaltetem Ton übernimmt weiterhin die 60/50-Hz-Zeitsteuerung.

## Bisher geprüft

- Vollständiger ESP32-S3-Firmware-Build in der unveränderten OTA-Partitionierung.
- ROM-/Speicherformat: 128 Prüfungen, einschließlich defekter/abgeschnittener Header, Größen, Mapper 5/66, DiskDude, CRC und überlappungsfreiem Inplace-Speichern.
- Emulator-Vertrag: CPU-Schritte, Controller-Latch, Speicherknappheit, wiederholtes Öffnen/Schließen, Reset und 64 KiB Batterie-RAM; zusätzlicher AddressSanitizer-Lauf.
- Bild-/Tonadapter: DMA-Pufferlebensdauer, Pitch 272, RGB565-Reihenfolge, PCM-Kopie, Lautstärke/Stumm, Teilwrites, begrenzte Warteschlangen, Stop-Retry und Initialisierungsfehler. Ein getaktetes 44,1-kHz-Ausgabemodell prüft die durchgehende Reihenfolge auch bei wechselnden Display-Verzögerungen; ein absichtlich zu langsam belieferter Vergleich erzeugt hörbare Versorgungslücken im Modell.
- Eingabe: länger als zehn Sekunden gehaltenes B mit Richtung, A+B+Richtung, Entprellung und Timer-Überlauf.
- Bestehende Lua-/Medien-/Player-UI-Tests: 12 Suites bestanden.
- Bestehender Tracker-Finder-Test und zehn Infrarot-Vertragsprüfungen bestanden.

| Bereitgestellte ROM | Konkreter PC-Nachweis |
|---|---|
| Super Mario Bros. + Duck Hunt (USA), Mapper 66 | 1.800 Frames / 30 emulierte Sekunden; Mario in World 1-1; 1.323.000 PCM-Samples |
| Castlevania III – Dracula's Curse (USA), Mapper 5 | 3.600 Frames / 60 emulierte Sekunden unter AddressSanitizer; Stage 1-1 und 1-2 mit laufendem Timer; 2.646.000 PCM-Samples |

Die Originaldateien blieben unverändert. Das belegt frühes Gameplay, keinen vollständigen Spieldurchlauf. Duck Hunt benötigt eine Lichtpistole; Zapper-Eingabe ist noch nicht implementiert. MMC5 ist weiterhin unvollständig, VRC6-Erweiterungston fehlt, Save States sind nicht verfügbar. Zelda, Mega Man 2, Contra, Kirby und Ninja Gaiden sind noch nicht mit konkreten ROMs getestet.

Nach den letzten CPU-Korrekturen wurden beide ROMs jeweils zweimal im selben Prozess ausgeführt. Die Bild- und Tonfolgen stimmten zwischen den Sessions überein; AddressSanitizer meldete keine Speicherfehler. Größen, Hashes und Laufdaten stehen in `docs/NES-VALIDATION-2026-09-20.json`.

## Geräteprüfung

Für diese Version besonders: beide Spiele mindestens zehn Minuten mit Sound spielen und auf Aussetzer achten; prüfen, dass die zuvor flüssige Grafik erhalten bleibt. Sound aus/ein, Pause/Resume und Sleep/Wake mehrfach ausführen. Im neuen SD-Browser Ordner, Touch und Rückkehr zum Launcher prüfen. B lange halten, A+B+Richtung und Touch-Start/Select testen sowie fünfmal starten und beenden. Anschließend MeowPlayer, TrackerDetect und die Firmware-Oberfläche prüfen. Mit einer Batterie-RAM-ROM zusätzlich Speichern, Neustart, volle/entfernte SD und beschädigtes Save testen.

Serielle Ausgabe mit 115200 Baud: `[NES perf]` zeigt emulierte Frames, angezeigte Frames, ausgelassene LCD-Updates, CPU-/LCD-Zeit und Audio-Versorgungslücken. Die Emulation zielt auf die vom Core verwendeten 60/50 Hz. Falls CPU+Display länger brauchen, reduziert sie die Bildausgabe. PC-Laufzeiten ersetzen diese Messung nicht.

Zusätzliche Prüfungen dieser Fassung: echte Service-Funktionen mit simuliertem SD/Core/AV (asynchrone SRAM-Kopie bei weiterlaufendem Spiel, Backup-Rotation, Schreibfehler/Wiederholung und Freigabe bei Speicherknappheit); native Browser-/Pausen-Interaktionen mit derselben C-App, Pfadgrenzen, Signaturprüfung mit bestehender und neuer Vertrauenswurzel sowie fehlerhafte Allokationen und wiederholte Bereinigung im echten ELF-Loader. Die Prüfläufe ersetzen keine Ausführung des Xtensa-ELF auf dem Gerät. Aktuelle Ergebnisse: `docs/NES-VALIDATION-2026-09-22.json`.

## Entwicklung und Quellen

Basis: FeralCat Commit `0a6d2abcf2d8696d5e66da33b58f66535f9b76dc`, Branch `feature/nes-emulator`. Emulator: Nofrendo aus Retro-Go Commit `4ced120669750ca7228fd0414211430c1d923166`, mit lokalem Adapter und dokumentierten Korrekturen. Siehe `lib/nes_core/PROVENANCE.md` und `UPSTREAM_MANIFEST.json` für Herkunft und Lizenztexte.

Firmware: PlatformIO 6.1.19, Espressif32 6.0.1, Arduino-ESP32 2.0.6, ESP-IDF 4.4.3. Die gepinnten Git-Submodule müssen rekursiv vorhanden sein. Unter Windows kurze Build-/Toolchain-Pfade verwenden, um die Befehlszeilengrenze älterer GCC-Werkzeuge einzuhalten.

```powershell
./tools/build-nes.ps1 -PythonPath <python-mit-platformio> -CoreDir <kurzer-platformio-cache>
cmake -S test/nes_core -B .build/nes-core-host
cmake --build .build/nes-core-host --config Release
ctest --test-dir .build/nes-core-host -C Release --output-on-failure
python tools/build-native-app.py 'sd files/apps/nes' --toolchain <xtensa-bin> --output .build/native-nes/app.elf
# Danach FINALEN ELF signieren; Anleitung: tools/app_signing/README.md
python tools/package-nes.py --output <neuer-ausgabeordner>
```

Weitere unabhängige CMake-Testprojekte liegen in `test/nes_storage`, `test/nes_av`, `test/nes_input`, `test/nes_path`, `test/nes_app`, `test/nes_service`, `test/elf_loader`, `test/app_sign` und `test/lua_apps`. Der Hostrunner `nes_core_host ROM OUTPUT_DIRECTORY [frames] [play|idle]` legt lokale BMP-/WAV-Nachweise ab. Nutzer-ROMs werden weder versioniert noch paketiert.
