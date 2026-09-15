# FeralCat — app icon pack (for Gemini generation)

Regenerate the launcher app icons as one cohesive set matching the FeralCat
red brand. Generate each **large (1024×1024)**, PNG, **transparent background**;
they get downscaled to **70×70** and converted to LVGL `TRUE_COLOR_ALPHA`
C-arrays for the firmware.

## Hard requirements
- **Square, 1024×1024**, transparent background (alpha), one icon per image.
- The art must read at **70×70** — bold shapes, thick strokes, no fine detail
  or small text (it disappears when shrunk).
- **Safe margin:** keep art within the centre ~85% (tiles are circular; corners
  get clipped).
- Consistent silhouette weight, lighting, and palette across ALL icons so they
  look like one family.

## Style (put this in every prompt)
> Flat, modern, slightly chunky "kawaii-tech" app icon. Dark charcoal rounded
> background (#1A1214). Primary accent bright red (#FF2A3D) with deep-red shadow
> (#8A0016) and clean white (#FFFFFF) highlights. Subtle soft inner glow, minimal
> flat shading, no gradients-heavy look, no text, no drop shadow outside the
> badge, centered composition, thick confident strokes. Cohesive icon-set style.

Palette: red `#FF2A3D` · dark red `#8A0016` · charcoal `#1A1214` · white `#FFFFFF`.
(Keep semantic accents where useful: alert = same red, "ok/scan" can use a small
white/red mix — avoid greens so it stays on-brand.)

## Master prompt (paste, then swap the SUBJECT line per icon)
```
A single mobile app icon, 1024x1024, transparent background.
STYLE: flat modern kawaii-tech, dark charcoal rounded-square badge (#1A1214),
bright red (#FF2A3D) primary with deep-red (#8A0016) shadows and white (#FFFFFF)
highlights, bold thick strokes, centered, no text, no outside shadow, reads
clearly at small size, part of one cohesive icon set.
SUBJECT: <see each icon below>
```

## The icons

### Native security apps (signed SD apps)
| # | App | File | SUBJECT prompt |
|---|-----|------|----------------|
| 1 | WiFi Analyzer | `ui_img_wifianalyzer` | a red WiFi signal-arc symbol with a small magnifying glass over it |
| 2 | Deauth Detect | `ui_img_deauth` | a red WiFi arc with a white shield and a small alert spark, "attack detector" feel |
| 3 | Rogue Radar | `ui_img_rogueradar` | a red radar screen with a sweep line and a blip, evil-twin/rogue AP theme |
| 4 | BLE Spam Detect | `ui_img_blespam` | a red Bluetooth rune with radiating broadcast waves and a small shield |
| 5 | Probe Sniffer | `ui_img_probe` | a red magnifying glass over overlapping WiFi probe waves / device dots |
| 6 | Tracker Detect | `ui_img_tracker` | a round location/AirTag-style tag with a red pulse ring and a small alert dot |

### Built-in apps
| # | App | File | SUBJECT prompt |
|---|-----|------|----------------|
| 7 | MeowGotchi | `ui_img_meowgotchi` | a fierce cute red cat face with tiny fangs (the FeralCat mascot as a face icon) |
| 8 | Dino | `ui_img_dino` | a chunky red pixel/retro dinosaur (T-rex) side profile |
| 9 | Matrix Rain | `ui_img_matrix` | vertical falling code streams made of red glyphs |
| 10 | VU Meter | `ui_img_vu_meter` | red audio level bars of varying height (equalizer) |
| 11 | Retro TV | `ui_img_retro_tv` | a cute retro CRT television with antenna, red body |
| 12 | PC Monitor | `ui_img_pc_monitor` | a desktop monitor showing a small red performance graph |
| 13 | Air Mouse | `ui_img_air_mouse` | a computer mouse with motion arcs, red + white |
| 14 | BLE Spam | `ui_img_ble_spam` | a red Bluetooth rune shooting multiple broadcast bursts |
| 15 | Bad USB | `ui_img_badusb` | a red USB stick with a small skull or lightning mark |
| 16 | Infrared | `ui_img_infrared` | a red TV-remote emitting IR wave arcs |
| 17 | Script Runner | `ui_img_script` | a terminal window with a red ">_" prompt / code brackets |
| 18 | MeowPlayer | `ui_img_music` | a red music note with a small play triangle |
| 19 | Lua Apps | `ui_img_apps` | a red app-grid / puzzle-piece "app manager" symbol |
| 20 | Firmware Update | `ui_img_firmware` | a red microchip with a circular update arrow |

### Optional — settings action icons (small, 48×48 look)
Gear (settings), USB stick (USB mode), Bluetooth rune, "i" info circle — same
style, single red glyph on transparent. Only if you want the settings panel to
match too.

## Delivery
Name the files after the SUBJECT (e.g. `wifianalyzer.png`, `deauth.png`, …) and
drop them in `docs/branding/icons/`. I'll batch-convert to 70×70 LVGL
C-arrays, wire each app to its own icon (`APP_BUILTIN_ICONS` +
`native_icon_by_name`), rebuild, and show you the grid in the simulator before
we commit.
