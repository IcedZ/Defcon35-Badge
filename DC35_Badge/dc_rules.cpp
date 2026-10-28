#include "dc_rules.h"
#include "dc_config.h"

const char *moveName(uint8_t m)
{
  switch (m)
  {
    case MOVE_STRIKE: return "STRIKE";
    case MOVE_BLOCK:  return "BLOCK";
    case MOVE_CHARGE: return "CHARGE";
    default:          return "-";
  }
}

const char *className(uint8_t c)
{
  (void)c;
  return "HACKER";   // single class for now
}

const char *effectText(uint8_t fx)
{
  switch (fx)
  {
    case FX_CLASH:        return "Clash! Both hit";
    case FX_BOTH_CHARGE:  return "Both charge up";
    case FX_HIT:          return "Hit while charging";
    case FX_COUNTER:      return "Blocked and countered";
    case FX_CHARGE_BREAK: return "Charge breaks the block";
    case FX_UNBLOCKABLE:  return "UNBLOCKABLE strike";
    default:              return "Standoff";
  }
}

uint8_t levelTier(uint8_t level)
{
  if (level < 1) level = 1;
  return 1 + (level - 1) / LEVELS_PER_TIER;
}

uint8_t levelHitDamage(uint8_t level) { return BASE_HIT_DMG * levelTier(level); }

uint8_t levelMaxHp(uint8_t level) { return HITS_TO_KO * levelHitDamage(level); }

static bool beats(uint8_t x, uint8_t y)
{
  return (x == MOVE_STRIKE && y == MOVE_CHARGE) ||
         (x == MOVE_BLOCK  && y == MOVE_STRIKE) ||
         (x == MOVE_CHARGE && y == MOVE_BLOCK);
}

static void hit(Fighter &attacker, Fighter &target, int16_t &dmgOut)
{
  int16_t d = levelHitDamage(attacker.level);
  target.hp -= d;
  if (target.hp < 0) target.hp = 0;
  attacker.dealt += d;
  dmgOut = d;
}

static uint8_t addMeter(uint8_t m) { return m < METER_MAX ? m + 1 : METER_MAX; }

RoundResult resolveRound(Fighter &a, uint8_t ma, Fighter &b, uint8_t mb)
{
  RoundResult r = {0, FX_STANDOFF, 0, 0};

  // A full meter turns a Strike into an unblockable one (it beats Block)
  bool aUnblockable = ma == MOVE_STRIKE && mb == MOVE_BLOCK && a.meter >= METER_MAX;
  bool bUnblockable = mb == MOVE_STRIKE && ma == MOVE_BLOCK && b.meter >= METER_MAX;

  int8_t w;
  if (aUnblockable)      w = +1;
  else if (bUnblockable) w = -1;
  else if (ma == mb)     w = 0;
  else                   w = beats(ma, mb) ? +1 : -1;

  if (w == 0)
  {
    if (ma == MOVE_STRIKE)
    {
      r.effect = FX_CLASH;   // both hits land
      hit(a, b, r.dmgToB);
      hit(b, a, r.dmgToA);
    }
    else if (ma == MOVE_CHARGE)
    {
      r.effect = FX_BOTH_CHARGE;
      a.meter = addMeter(a.meter);
      b.meter = addMeter(b.meter);
    }
    return r;
  }

  Fighter &W = (w > 0) ? a : b;
  Fighter &L = (w > 0) ? b : a;
  uint8_t mw = (w > 0) ? ma : mb;
  bool unblockable = (w > 0) ? aUnblockable : bUnblockable;

  if (unblockable)
  {
    r.effect = FX_UNBLOCKABLE;
    W.meter = 0;
  }
  else if (mw == MOVE_STRIKE) r.effect = FX_HIT;
  else if (mw == MOVE_BLOCK)  r.effect = FX_COUNTER;
  else
  {
    r.effect = FX_CHARGE_BREAK;
    W.meter = addMeter(W.meter);
  }

  hit(W, L, (w > 0) ? r.dmgToB : r.dmgToA);
  r.winner = w;
  return r;
}

bool battleOver(const Fighter &a, const Fighter &b, uint8_t round)
{
  return a.hp <= 0 || b.hp <= 0 || round >= MAX_ROUNDS;
}

int8_t battleWinner(const Fighter &a, const Fighter &b)
{
  bool aKO = a.hp <= 0, bKO = b.hp <= 0;
  if (aKO && bKO) return 0;
  if (bKO) return +1;
  if (aKO) return -1;
  if (a.dealt != b.dealt) return a.dealt > b.dealt ? +1 : -1;
  return 0;
}
