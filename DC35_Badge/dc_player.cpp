#include "dc_player.h"
#include "dc_config.h"
#include "dc_rules.h"
#include <string.h>

// Recently-fought opponents (RAM only) - stops two friends farming XP.
struct RecentOpp { uint32_t id; uint32_t when; };
static RecentOpp recent[8];
static uint32_t lastRegen = 0;

void playerInit(Player &p)
{
  memset(&p, 0, sizeof(p));
  p.saveVersion = PLAYER_SAVE_VERSION;
  p.cls = CLASS_HACKER;
  p.level = 1;
  p.hp = playerMaxHp(p);
}

uint8_t playerMaxHp(const Player &p) { return levelMaxHp(p.level); }

static const uint16_t xpTable[] = XP_TO_NEXT_TABLE;
static const uint16_t gapPercent[] = XP_GAP_PERCENT;
static_assert(sizeof(xpTable) / sizeof(xpTable[0]) == MAX_LEVEL - 1, "XP_TO_NEXT_TABLE needs MAX_LEVEL - 1 entries");
static_assert(sizeof(gapPercent) / sizeof(gapPercent[0]) == 7, "XP_GAP_PERCENT needs 7 entries (gap -3..+3)");

uint32_t playerXpToNext(const Player &p)
{
  int i = p.level - 1;
  if (i < 0) i = 0;
  if (i >= (int)(sizeof(xpTable) / sizeof(xpTable[0]))) return 0xFFFFFFFF;   // max level
  return xpTable[i];
}

bool playerNormalize(Player &p)
{
  Player before = p;
  if (p.level < 1) p.level = 1;
  if (p.level > MAX_LEVEL) p.level = MAX_LEVEL;
  if (p.cls >= CLASS_COUNT) p.cls = CLASS_HACKER;   // classes from older builds
  if (p.hp > playerMaxHp(p)) p.hp = playerMaxHp(p);
  if (p.level >= MAX_LEVEL) p.xp = 0;
  else if (p.xp >= playerXpToNext(p)) p.xp = playerXpToNext(p) - 1;   // no free levels from old saves
  return memcmp(&before, &p, sizeof(p)) != 0;
}

bool playerCanBattle(const Player &p) { return !p.ko && p.hp > 0; }

bool playerRegen(Player &p, uint32_t now)
{
  if (lastRegen == 0) { lastRegen = now; return false; }
  if (now - lastRegen < HP_REGEN_MS) return false;
  lastRegen = now;

  bool changed = false;
  if (p.hp < playerMaxHp(p)) { p.hp++; changed = true; }
  if (p.ko && p.hp >= playerMaxHp(p) / 2) { p.ko = 0; changed = true; }
  return changed;
}

static bool foughtRecently(uint32_t id, uint32_t now)
{
  for (auto &r : recent)
    if (r.id == id && now - r.when < XP_REPEAT_COOLDOWN_MS) return true;
  return false;
}

static void rememberOpponent(uint32_t id, uint32_t now)
{
  int slot = 0;
  for (int i = 0; i < 8; i++)
  {
    if (recent[i].id == id) { slot = i; break; }
    if (now - recent[i].when > now - recent[slot].when) slot = i;   // oldest
  }
  recent[slot].id = id;
  recent[slot].when = now;
}

XpAward playerApplyBattle(Player &p, int8_t outcome, int16_t endHp,
                          uint8_t oppLevel, uint32_t oppId, uint32_t now)
{
  XpAward a = {0, 100, false, false};

  p.battles++;
  if (outcome > 0) p.wins++; else if (outcome < 0) p.losses++; else p.draws++;

  p.hp = endHp < 0 ? 0 : (uint8_t)endHp;
  if (p.hp == 0) p.ko = 1;
  lastRegen = now;   // regen clock restarts after a battle

  if (foughtRecently(oppId, now))
  {
    a.repeatOpponent = true;
  }
  else
  {
    int gap = (int)oppLevel - (int)p.level;
    if (gap < -3) gap = -3;
    if (gap > 3) gap = 3;
    a.percent = gapPercent[gap + 3];
    if (outcome < 0 && a.percent > 100) a.percent = 100;   // losing to a stronger player isn't rewarded extra
    uint32_t base = outcome > 0 ? XP_WIN : (outcome == 0 ? XP_DRAW : XP_LOSS);
    a.gained = base * a.percent / 100;
  }
  rememberOpponent(oppId, now);

  p.xp += a.gained;
  while (p.level < MAX_LEVEL && p.xp >= playerXpToNext(p))
  {
    p.xp -= playerXpToNext(p);
    p.level++;
    p.hp = playerMaxHp(p);   // level-up = full heal (and clears a KO)
    p.ko = 0;
    a.leveledUp = true;
  }
  if (p.level >= MAX_LEVEL) p.xp = 0;
  return a;
}
