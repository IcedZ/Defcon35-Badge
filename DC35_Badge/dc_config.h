// dc_config.h - all tunable constants for the DC35 badge firmware.
// Gameplay numbers here are placeholders: tune them during playtesting.
#pragma once

// ---------------------------------------------------------------------------
// Hardware: Guition JC4827W543R (ESP32-S3 N4R8, NV3041A 480x272 QSPI, XPT2046 resistive touch)
// ---------------------------------------------------------------------------
#define PIN_TFT_BL        1
#define PIN_LCD_CS        45
#define PIN_LCD_SCK       47
#define PIN_LCD_D0        21
#define PIN_LCD_D1        48
#define PIN_LCD_D2        40
#define PIN_LCD_D3        39
#define PIN_BOOT_BTN      0      // only user button on the dev board (active low)

// Backlight brightness (PWM on PIN_TFT_BL). User picks level 1..5; the
// duty values follow a rough perceptual curve. The backlight is the biggest
// power draw, so lower levels noticeably extend battery life.
#define BACKLIGHT_PWM_HZ      2000
#define BACKLIGHT_DUTY        {10, 30, 70, 140, 255}   // 8-bit duty for levels 1..5
#define BRIGHTNESS_LEVELS     5
#define BRIGHTNESS_DEFAULT    5

// XPT2046 resistive touch (JC4827W543R). It shares one SPI bus (HSPI) with the
// microSD slot; each has its own chip select. The display is on a separate
// QSPI bus. Pins verified by the emu8 project (github.com/hexadevti/emu8).
#define PIN_SPI2_SCK      12     // shared touch + SD bus
#define PIN_SPI2_MISO     13
#define PIN_SPI2_MOSI     11
#define PIN_TOUCH_CS      38
#define PIN_TOUCH_IRQ     3      // pen-down IRQ (also a strapping pin) - not used
#define PIN_SD_CS         10     // microSD (not used yet, kept deselected)

// Touch calibration: raw 12-bit readings at the panel edges (emu8's values for
// this panel). If taps land in the wrong place, run "touchtest" in the Serial
// Monitor, tap the four corners and adjust these.
#define TOUCH_RAW_X_MIN   205
#define TOUCH_RAW_X_MAX   3870
#define TOUCH_RAW_Y_MIN   400
#define TOUCH_RAW_Y_MAX   3725
#define TOUCH_SWAP_XY     0      // if 1, the X_* / Y_* ranges apply after the swap
#define TOUCH_MIRROR_X    1      // raw X runs right -> left at rotation 0
#define TOUCH_MIRROR_Y    1      // raw Y runs bottom -> top at rotation 0
#define TOUCH_PRESSURE_MIN 400   // z1 + (4095 - z2); higher = firmer press

#define SCREEN_W          480
#define SCREEN_H          272

// ---------------------------------------------------------------------------
// Radio / proximity
// ---------------------------------------------------------------------------
#define ADV_IDLE_MIN      0xA0   // x 0.625 ms = 100 ms
#define ADV_IDLE_MAX      0xF0   // 150 ms
#define ADV_BATTLE_MIN    0x30   // 30 ms  - faster while battling
#define ADV_BATTLE_MAX    0x50   // 50 ms
#define PEER_STALE_MS     10000  // forget a badge not heard for this long
#define MAX_PEERS         32

#define RSSI_CLOSE        -60    // smoothed RSSI >= this = CLOSE
#define RSSI_NEAR         -75    // >= this = NEAR, else FAR
#define RSSI_CHALLENGE    RSSI_NEAR   // minimum signal to send a challenge

// ---------------------------------------------------------------------------
// Battle protocol timing
// ---------------------------------------------------------------------------
#define CHALLENGE_TIMEOUT_MS     20000  // challenger gives up waiting for an answer
#define DECLINE_BROADCAST_MS     3000   // how long a decline is advertised
#define PICK_TIMEOUT_MS          15000  // no move chosen -> random move is played
#define OPP_SILENT_ABORT_MS      8000   // opponent not heard -> abort
#define OPP_DISENGAGED_ABORT_MS  3000   // opponent left this battle -> abort
#define PHASE_STALL_ABORT_MS     30000  // no progress in one phase -> abort
#define DONE_LINGER_MS           4000   // keep advertising the final reveal
#define MAX_ROUNDS               3

// ---------------------------------------------------------------------------
// Combat rules (Strike beats Charge, Block beats Strike, Charge beats Block)
// Every winning move deals one hit. Strike vs Strike: both hits land.
// ---------------------------------------------------------------------------
#define METER_MAX           2   // full meter (2 Charges) -> next Strike is unblockable

// ---------------------------------------------------------------------------
// Level scaling - in tiers so HP and damage always grow together:
//   tier       = 1 + (level - 1) / LEVELS_PER_TIER   (L1-3: 1, L4-6: 2, L7-9: 3, L10: 4)
//   hit damage = BASE_HIT_DMG x tier                 (1, 2, 3, 4)
//   max HP     = HITS_TO_KO x hit damage             (2, 4, 6, 8)
// Every battle is "first to land HITS_TO_KO hits", so the knockout odds are
// the same at every level (~3 in 4 equal-level battles end in a KO).
// Set HITS_TO_KO to 3 for longer fights (~1 in 6 end in a KO).
// ---------------------------------------------------------------------------
#define BASE_HIT_DMG        1
#define HITS_TO_KO          2
#define LEVELS_PER_TIER     3

// ---------------------------------------------------------------------------
// Progression
// ---------------------------------------------------------------------------
#define MAX_LEVEL           10
// XP needed to go from level N to N+1 (index 0 = level 1). Cheap early levels
// hook new players; later levels get steep. (Pace per level: see tools/sim.)
#define XP_TO_NEXT_TABLE    {20, 40, 70, 110, 160, 230, 320, 440, 600}
// Base XP for an equal-level battle
#define XP_WIN              20
#define XP_DRAW             8
#define XP_LOSS             4
// XP multiplier (percent) by level gap = opponent level - your level,
// from -3 (or lower) to +3 (or higher). Beating weaker players is nearly
// worthless; beating stronger players pays well. Losses are never boosted.
#define XP_GAP_PERCENT      {10, 25, 50, 100, 150, 200, 250}
#define XP_REPEAT_COOLDOWN_MS 300000 // no XP vs the same badge within 5 min
#define HP_REGEN_MS         60000    // +1 HP per minute when not battling
// A knocked-out (0 HP) badge can't battle until it regenerates to half HP.
