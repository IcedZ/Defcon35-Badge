# Battle protocol simulator

Runs the badge's real battle engine (`DC35_Badge/dc_*.cpp`) on a PC. It
simulates several badges advertising over a lossy radio and checks that every
battle ends with both badges agreeing on the result.

## Build and run

From this folder, with any C++17 compiler (g++ / clang++; on Windows use
WSL or MSYS2):

```
g++ -std=c++17 -O2 -Wall -o battle_sim battle_sim.cpp ../../DC35_Badge/dc_battle.cpp ../../DC35_Badge/dc_rules.cpp ../../DC35_Badge/dc_protocol.cpp ../../DC35_Badge/dc_sha256.cpp ../../DC35_Badge/dc_player.cpp
./battle_sim
```

Expected last line: `... checks, 0 failures`. The exit code is non-zero if
anything fails, so it can run in CI.

## What it covers

- rules symmetry (both badges must compute mirrored results)
- XP and leveling: level-gap multipliers, the repeat-opponent cooldown, the level cap, cleanup of old saves, and how many battles each level takes
- about 800 normal battles at 0%, 30% and 60% packet loss
- declined challenges, unanswered challenges, simultaneous challenges
- nobody choosing moves (pick timer), walking out of range mid-battle
- a third badge interfering
- a cheating badge that reveals a different move than it committed to (must be caught every time)

Run it after any change to `dc_battle`, `dc_rules`, `dc_protocol`, `dc_player` or the numbers in `dc_config.h`.
