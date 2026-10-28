# DC35 Badge — Firmware Architecture

Status: prototype on the Guition JC4827W543R dev board (resistive touch).
Covers game state, BLE proximity, the battle handshake and touch input.
Meshtastic, SAOs, NFC and physical buttons come later and plug into the
layers described below.

## Layers

```
            +-------------------------------------------------+
            |                DC35_Badge.ino                   |
            |   main loop: input -> engine -> events -> UI    |
            +-------------------------------------------------+
   device   |  dc_input     dc_ui        dc_radio    dc_storage |
   layer    |  Serial+BOOT  480x272 LCD  BLE adv/scan NVS flash |
            |  dc_touch     XPT2046 resistive touch (HSPI)      |
            +-------------------------------------------------+
   portable |  dc_battle    dc_rules     dc_player   dc_protocol|
   core     |  handshake    combat math  XP/HP/levels 24B frame |
            |  dc_sha256    dc_config (all tunable numbers)     |
            +-------------------------------------------------+
```

* **Portable core**: plain C++ with no Arduino or ESP-IDF calls. The same files
  build on a PC, where `tools/sim` stress-tests the battle protocol.
* **Device layer**: the only code that touches hardware. New hardware
  (buttons, touch, LEDs, LoRa) means a new device module, not changes to the core.
* **dc_config.h** holds every gameplay and timing number, plus pins and touch calibration.

## Input

Touch, Serial and the BOOT button all produce the same `Command` values
(`dc_input.h`), so the game logic doesn't care where a command came from.

* **Touch:** `dc_touch` reports a tap, `dc_ui` turns it into a command with
  `uiHitTest()`. The UI owns the button layout, so a button's position and its
  action are defined in one place.
* **On-screen buttons:**
  * Home: tap a badge row to select it, then **CHALLENGE**. **AUTO** toggles
    auto-play. **BRIGHT n** cycles display brightness 1–5 (PWM backlight). The
    level is saved and survives power-off and `factory-reset`. Lower levels
    noticeably extend battery life, since the backlight is the biggest draw.
  * Challenge: **ACCEPT** / **DECLINE**.
  * Battle: **STRIKE** / **BLOCK** / **CHARGE**.
  * Result: tap anywhere to continue.
* **Serial** (115200): the same actions plus dev commands. `touchtest` prints
  raw and mapped tap coordinates for calibration.
* **BOOT:** a short press accepts a challenge or dismisses the result screen;
  hold it 1.5 s to toggle auto-play.

## Identity

Each badge's ID is the last 3 bytes of its Bluetooth MAC, shown as
`DC35-XXXXXX`. That gives 16.7 million IDs, so collisions are very unlikely at
badge-run scale (a few hundred). The ID is self-declared on the air; see
*Security* below.

## Radio: everything is a broadcast

Badges never connect to each other. Every badge advertises one 24-byte frame
and listens to everyone else's frames with a passive scan.

| Bytes | Field |
|---|---|
| 0 | magic `0xDC` |
| 1 | protocol version (2) |
| 2–4 | badge ID |
| 5 | class (low nibble) · flags (high nibble: BUSY, KO, BOT, ACK) |
| 6 | level |
| 7 | HP |
| 8 | battle state: IDLE, CHALLENGE, PICK, COMMIT, REVEAL, DONE, DECLINE |
| 9–11 | opponent ID |
| 12–13 | battle session ID |
| 14 | round |
| 15–22 | state-dependent data (nonce, commitment, or move + salt) |
| 23 | reserved |

The frame sits in manufacturer data (company ID `0xFFFF`, the Bluetooth
SIG's test ID). Flags, manufacturer header and frame fill the 31-byte legacy
advertisement exactly. The readable name goes in the scan response, so phone
apps like nRF Connect show `DC35-XXXXXX`.

Each frame is a full snapshot of state, not a one-off message, so a lost
packet costs nothing: the next one, 30–150 ms later, carries the same
information. During a battle the advertising interval drops from about 125 ms
to about 40 ms.

Why not BLE connections? Connections are slow to set up, limited in number,
and fragile in a crowded convention hall. Broadcasting also lets any number of
battles run side by side, and a spectator badge can watch a fight.

## Battle handshake

```
 Badge A (challenger)                         Badge B
 --------------------                         -------
 CHALLENGE(B, session, nonceA)   ───────►     prompt: accept? (y / n / BOOT)
                                 ◄───────     PICK(1, nonceB)        [accepted]
 PICK(1, nonceA)                 ───────►
   ...each round r...
 COMMIT(r, H(move,salt,...))     ◄──────►     COMMIT(r, H(...))      sealed moves
 COMMIT + ACK flag               ◄──────►     COMMIT + ACK           "I have yours"
 REVEAL(r, move, salt)           ◄──────►     REVEAL(r, move, salt)
   both verify the other's hash, resolve the round identically
 PICK(r+1, my reveal of r)       ◄──────►     PICK(r+1, ...)         next round
   ...
 DONE(final reveal)              ◄──────►     DONE                   -> IDLE
```

Key properties, all exercised by the simulator:

* **Battles are opt-in.** Nothing happens unless B accepts. No answer within
  20 s counts as a decline.
* **Sealed moves (commit-reveal).** A badge first advertises
  `SHA-256(move, salt, both nonces, round, its ID)` and only reveals once both
  sides hold each other's commitment (the ACK flag). Nobody can see the other's
  move before locking in their own, and a badge that reveals a different move
  than it committed to is caught and forfeits.
* **Loss-tolerant.** Each state's data repeats in the next state's frame:
  PICK(r+1) carries the round-r reveal, DONE carries the final one. A badge
  only advances once the opponent has what it needs.
* **Deterministic.** Both badges run the same pure `resolveRound()` on the same
  inputs (stats frozen at battle start), so they always agree on the result.
* **Timeouts.** Pick a move within 15 s or a random one is played. If the
  opponent goes silent for 8 s, or leaves the battle for 3 s, the battle is
  cancelled with no XP or HP change.
* **Simultaneous challenges.** If two badges challenge each other at once, the
  lower ID accepts the other's challenge.

## Rules (placeholders, tune in `dc_config.h`)

One class for now: **HACKER**. The class field stays in the save file and the
radio frame, so more classes can come back later without a format change.

| | beats |
|---|---|
| **Strike** | Charge |
| **Block** | Strike (blocks and counters) |
| **Charge** | Block; builds 1 meter |

* **Every winning move lands one hit.** Strike vs Strike: both hits land.
  Charge vs Charge: both gain meter. Block vs Block: nothing.
* **Full meter (2):** your next Strike is unblockable. It beats Block, and the meter empties.
* A battle lasts **3 rounds** or until someone hits 0 HP. Winner: whoever is still
  standing, otherwise whoever dealt more damage.

## Level scaling (tiers)

Every 3 levels is a new tier, and HP and damage grow together:

| Level | 1–3 | 4–6 | 7–9 | 10 |
|---|---|---|---|---|
| Hit damage | 1 | 2 | 3 | 4 |
| Max HP | 2 | 4 | 6 | 8 |

Max HP is always 2 hits (`HITS_TO_KO`), so every battle is "first to land two
hits" at any level. Simulated with the real engine (random moves, 300 battles
each), equal-level battles go the same way at every level: 78% end in a
knockout, 19% are draws, about 2.6 rounds and 8 s each. Across tiers the
higher tier wins about 82%, because one of its hits takes out a lower-tier
player's whole health bar. So the jump at a tier boundary (level 3 → 4) is a
big one. Set `HITS_TO_KO` to 3 for longer fights (about 1 in 6 end in a KO).

## Progression

* HP lost in battle **persists** and regenerates 1 per minute. **Leveling up is a full heal.**
* At 0 HP you're knocked out (KO). You can't battle until you're back to half HP.
* **Fast start, slow finish.** XP to the next level: 20, 40, 70, 110, 160, 230,
  320, 440, 600 (levels 1→10). Fighting players at your own level, that's level 2
  after the first battle, level 4 after about 14, and level 10 after about 170.
* **Base XP** (equal levels): win 20, draw 8, loss 4.
* **Level gap multiplier** (opponent level − yours): −3 or lower 10%, −2 25%,
  −1 50%, equal 100%, +1 150%, +2 200%, +3 or higher 250%. Beating much weaker
  players is nearly worthless (a win is 2 XP), and beating stronger ones pays up
  to 50 XP. Losses are never boosted. The result screen shows the multiplier.
* No XP for fighting the same badge again within 5 minutes (anti-farming).
* The cap is level 10 for now.
* Player state is saved to NVS flash after every battle and HP change.

## Security notes (prototype)

The commit-reveal scheme stops the obvious cheat: watching the other badge's
move. Known gaps, tracked on the shelved list:

* IDs are self-declared and frames are unsigned, so a modified badge could
  impersonate another ID.
* The commitment is truncated to 64 bits to fit the advertisement.
* Leaving a battle you're losing just cancels it, with no penalty.

## Testing

**On a PC** (any C++17 compiler; build command in [tools/sim/README.md](../tools/sim/README.md)):

It runs about 1,000 simulated battles at 0%, 30% and 60% packet loss, plus
declines, no-answers, simultaneous challenges, walk-aways, no-input timeouts,
a third badge interfering, and a cheating badge. All runs must end with both
badges agreeing on the result.

**On hardware** (two boards):

1. Flash `DC35_Badge` to both boards.
2. Board 2: tap **AUTO** (or hold BOOT for 1.5 s). The button shows `AUTO:ON`,
   and the board then accepts challenges and plays random moves.
3. Board 1: tap board 2 in the nearby list, tap **CHALLENGE**, then play with
   the move buttons. Serial (`list`, `c 1`, `1`/`2`/`3`) works too.
4. Reverse roles by turning auto-play off and challenging from board 2.

## Where future hardware plugs in

| Feature | Plugs into |
|---|---|
| Physical Strike/Block/Charge buttons | `dc_input`: emit the same `Command`s |
| microSD (sprites, logs, settings) | shares the touch SPI bus (`SPIClass(HSPI)`, CS 10); `dc_touch` already keeps it deselected |
| LED heart health bar, level-up patterns | new `dc_leds` module driven from `Player` |
| SAO sensor buffs/debuffs | new `dc_sao` module feeding modifiers into `dc_rules` |
| Meshtastic (leaderboard, monster nodes) | new `dc_mesh` module; battle results already arrive as events |
| Laser tag hits | new event source that calls a "hit" path in `dc_player` |
