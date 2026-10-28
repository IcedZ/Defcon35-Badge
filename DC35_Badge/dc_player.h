// dc_player.h - persistent player state and progression (XP, levels, HP regen).
// Pure logic; saving/loading lives in dc_storage (device only).
#pragma once
#include <stdint.h>

#define PLAYER_SAVE_VERSION 1

struct Player
{
  uint8_t  saveVersion;
  uint8_t  cls;        // PlayerClass
  uint8_t  level;      // 1..MAX_LEVEL
  uint8_t  hp;
  uint8_t  ko;         // 1 = knocked out until regenerated to half HP
  uint8_t  reserved[3];
  uint32_t xp;         // XP toward the next level
  uint16_t wins, losses, draws;
  uint16_t battles;
};

struct XpAward
{
  uint32_t gained;
  uint16_t percent;          // level-gap multiplier applied (100 = equal level)
  bool     repeatOpponent;   // no XP: fought this badge too recently
  bool     leveledUp;
};

void     playerInit(Player &p);
uint8_t  playerMaxHp(const Player &p);
uint32_t playerXpToNext(const Player &p);
bool     playerCanBattle(const Player &p);
// Bring a loaded save in line with the current rules (HP / XP caps).
// Returns true if anything changed.
bool     playerNormalize(Player &p);

// +1 HP every HP_REGEN_MS; clears KO at half HP. Returns true if HP changed.
bool     playerRegen(Player &p, uint32_t now);

// Apply a finished battle: outcome +1 win / 0 draw / -1 loss, endHp = HP left.
XpAward  playerApplyBattle(Player &p, int8_t outcome, int16_t endHp,
                           uint8_t oppLevel, uint32_t oppId, uint32_t now);
