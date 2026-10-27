# DC35 Badge — Dungeon Edition

Firmware and hardware notes for an interactive RPG-style badge for DEF CON 35,
built on the ESP32-S3.

> Early prototyping. Currently bringing up a 4.3" ESP32-S3 display board; the
> final display and hardware may change.

## Repository layout

```
JC4827W543_HelloWorld/   Display bring-up + self-test (text, gradients, uptime)
JC4827W543_Diag/         No-display diagnostic: blinks backlight, prints chip/PSRAM info
docs/
  hardware/
    JC4827W543.md        Prototype board pin map, Arduino setup, known gotchas
```

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

**Guition JC4827W543** — ESP32-S3 (4 MB flash, 8 MB PSRAM), 4.3" 480×272 IPS
(NV3041A over QSPI). See [docs/hardware/JC4827W543.md](docs/hardware/JC4827W543.md).
