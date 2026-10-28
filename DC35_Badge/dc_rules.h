// dc_rules.h - combat rules. Pure functions: no hardware, no randomness.
// Both badges run resolveRound() on the same inputs and must get mirrored
// results, so everything here has to be deterministic and symmetric.
#pragma once
#include <stdint.h>

enum Move : uint8_t
{
  MOVE_NONE   = 0,
  MOVE_STRIKE = 1,   // beats Charge
  MOVE_BLOCK  = 2,   // beats Strike (blocks + counters)
  MOVE_CHARGE = 3,   // beats Block, builds meter
};

// One class for now. The class field stays in the save file and the radio
// frame so more classes can be added later without a format change.
enum PlayerClass : uint8_t
{
  CLASS_HACKER = 0,
  CLASS_COUNT  = 1,
};

enum RoundEffect : uint8_t
{
  FX_STANDOFF,       // Block vs Block - nothing happens
  FX_CLASH,          // Strike vs Strike - both hits land
  FX_BOTH_CHARGE,    // Charge vs Charge - both build meter
  FX_HIT,            // Strike beat Charge
  FX_COUNTER,        // Block beat Strike
  FX_CHARGE_BREAK,   // Charge beat Block
  FX_UNBLOCKABLE,    // full-meter Strike went through a Block
};

struct Fighter
{
  uint8_t cls;
  uint8_t level;
  int16_t hp;
  uint8_t meter;
  int16_t dealt;     // total damage dealt this battle (tiebreaker)
};

struct RoundResult
{
  int8_t  winner;    // +1 = fighter a, -1 = fighter b, 0 = nobody
  uint8_t effect;    // RoundEffect
  int16_t dmgToA;
  int16_t dmgToB;
};

const char *moveName(uint8_t m);
const char *className(uint8_t c);
const char *effectText(uint8_t fx);

// Level scaling: every LEVELS_PER_TIER levels is a new tier.
//   hit damage = tier (1 at levels 1-3, 2 at 4-6, ...)
//   max HP     = HITS_TO_KO x hit damage
uint8_t levelTier(uint8_t level);
uint8_t levelHitDamage(uint8_t level);
uint8_t levelMaxHp(uint8_t level);

RoundResult resolveRound(Fighter &a, uint8_t moveA, Fighter &b, uint8_t moveB);
bool battleOver(const Fighter &a, const Fighter &b, uint8_t round);
int8_t battleWinner(const Fighter &a, const Fighter &b);   // +1 a, -1 b, 0 draw
