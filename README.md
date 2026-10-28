# DC35 Badge — Dungeon Edition

Firmware and hardware notes for an interactive RPG-style badge for DEF CON 35,
built on the ESP32-S3.

> Early prototyping. Currently bringing up a 4.3" ESP32-S3 display board; the
> final display and hardware may change.

## Repository layout

```
DC35_Badge/              Badge firmware: game state, BLE proximity, battles
tools/sim/               PC simulator that stress-tests the battle protocol
JC4827W543_HelloWorld/   Display bring-up + self-test (text, gradients, uptime)
JC4827W543_Diag/         No-display diagnostic: blinks backlight, prints chip/PSRAM info
JC4827W543_BLE_Proximity/ BLE proximity test: unique DC35-XXXXXX name, lists nearby badges + RSSI
docs/
  architecture.md        Firmware layers, radio frame, battle handshake, rules
  hardware/
    JC4827W543.md        Prototype board pin map, Arduino setup, known gotchas
```

## Badge firmware (`DC35_Badge`)

Flash it with the same Tools settings as the test sketches; no extra libraries
are needed. Play with the touch screen (resistive, so press firmly):

- Tap a badge tile in the nearby grid, then **CHALLENGE**; the other badge taps **ACCEPT** or **DECLINE**
- Tap **STRIKE** / **BLOCK** / **CHARGE** each round (first to land 2 hits wins)
- **AUTO** (or holding BOOT 1.5 s) lets a second board play by itself
- **BRIGHT** cycles screen brightness 1–5 (saved; dimmer = longer battery life)
- Serial Monitor at 115200 offers the same actions plus dev commands: type `help`

How it works: [docs/architecture.md](docs/architecture.md). Simulator: [tools/sim/README.md](tools/sim/README.md).

## Quick start

1. Install **Arduino IDE 2.x** and the **esp32 by Espressif Systems** board package (3.x).
2. Install **GFX Library for Arduino** (Moon On Our Nation) from Library Manager.
3. Open `JC4827W543_HelloWorld/JC4827W543_HelloWorld.ino`.
4. Set Tools → Board **ESP32S3 Dev Module**, Flash Size **4MB**, PSRAM **OPI PSRAM**
   (full settings in [docs/hardware/JC4827W543.md](docs/hardware/JC4827W543.md)).
5. Upload, then open Serial Monitor at 115200.

If the screen stays dark, flash `JC4827W543_Diag` first: it confirms the board,
serial path, and PSRAM without touching the display.

## Prototype hardware

**Guition JC4827W543R** — ESP32-S3 (4 MB flash, 8 MB PSRAM), 4.3" 480×272 IPS
(NV3041A over QSPI), XPT2046 resistive touch. See [docs/hardware/JC4827W543.md](docs/hardware/JC4827W543.md).
