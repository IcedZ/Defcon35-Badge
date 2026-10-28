// battle_sim.cpp - runs the real badge battle engine on a PC.
//
// Simulates badges advertising frames over a lossy "radio" and checks that
// battles always finish consistently on both sides, under packet loss,
// declines, simultaneous challenges, cheating and walk-aways.
//
// Build & run: see README.md in this folder.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <random>
#include <vector>
#include <functional>
#include "../../DC35_Badge/dc_battle.h"
#include "../../DC35_Badge/dc_sha256.h"
#include "../../DC35_Badge/dc_config.h"
#include "../../DC35_Badge/dc_player.h"

static std::mt19937 engineRng(1);
static uint32_t rngFn() { return engineRng(); }

struct SimBadge
{
  uint32_t id;
  BattleEngine eng;
  std::vector<PeerInfo> peers;
  std::vector<BattleEvent> events;
  uint32_t nextTx = 0;
  bool present = true;

  // behaviour
  int  acceptPolicy = 1;        // 1 accept, 0 decline, -1 never answer
  bool picksMoves = true;       // false = let the pick timer expire
  uint32_t maxDelay = 3000;     // human reaction time (ms)
  uint32_t actAt = 0;           // pending action time
  int pending = 0;              // 1 = answer challenge, 2 = pick move

  uint8_t level = 3, cls = CLASS_HACKER;
  uint8_t hp = levelMaxHp(3);
};

struct Sim
{
  std::vector<SimBadge> b;
  std::mt19937 rnd;
  double loss = 0.0;
  uint32_t now = 1000;
  // optional in-flight frame tamper: (from, to, frame&)
  std::function<void(SimBadge &, SimBadge &, AdvFrame &)> tamper;

  explicit Sim(uint32_t seed) : rnd(seed)
  {
    engineRng.seed(seed * 7919 + 1);
    b.reserve(8);   // keep SimBadge references stable
  }

  SimBadge &add(uint32_t id)
  {
    b.emplace_back();
    SimBadge &x = b.back();
    x.id = id;
    return x;
  }

  void init()
  {
    for (auto &x : b) x.eng.begin(x.id, rngFn);
  }

  uint32_t rand(uint32_t n) { return n ? rnd() % n : 0; }

  void deliver(SimBadge &from, SimBadge &to, const AdvFrame &frame)
  {
    AdvFrame f = frame;
    if (tamper) tamper(from, to, f);
    for (auto &p : to.peers)
      if (p.id == from.id) { p.f = f; p.lastSeen = now; p.rssi = -50; p.rssiAvg = -50; return; }
    PeerInfo p{};
    p.id = from.id; p.f = f; p.lastSeen = p.firstSeen = now; p.rssi = -50; p.rssiAvg = -50;
    to.peers.push_back(p);
  }

  void step(uint32_t dt = 5)
  {
    now += dt;
    for (auto &x : b)
    {
      // keep self stats fresh while idle
      x.eng.setSelf(x.cls, x.level, x.hp, x.hp > 0, 0);

      // scripted human behaviour
      if (x.pending && now >= x.actAt)
      {
        if (x.pending == 1 && x.eng.phase() == Phase::CHALLENGED) x.eng.respond(x.acceptPolicy == 1, now);
        if (x.pending == 2 && x.eng.needsMove()) x.eng.chooseMove(MOVE_STRIKE + rand(3), now);
        x.pending = 0;
      }

      x.eng.tick(now, x.peers.data(), (int)x.peers.size());

      BattleEvent e;
      while (x.eng.pollEvent(e))
      {
        x.events.push_back(e);
        if (e.type == EV_CHALLENGE_RECEIVED && x.acceptPolicy >= 0) { x.pending = 1; x.actAt = now + rand(x.maxDelay); }
        if (e.type == EV_ROUND_START && x.picksMoves) { x.pending = 2; x.actAt = now + rand(x.maxDelay); }
      }

      // transmit
      if (x.present && now >= x.nextTx)
      {
        AdvFrame f;
        x.eng.buildFrame(f);
        for (auto &y : b)
          if (&y != &x && y.present && (rnd() / (double)rnd.max()) >= loss) deliver(x, y, f);
        uint32_t base = x.eng.inBattle() ? 40 : 125;
        x.nextTx = now + base + rand(10);   // BLE adds random jitter
      }
    }
  }

  void runFor(uint32_t ms) { uint32_t end = now + ms; while (now < end) step(); }

  // Run until badge i has heard badge j (or give up after maxMs).
  void waitHeard(size_t i, size_t j, uint32_t maxMs = 20000)
  {
    uint32_t end = now + maxMs;
    auto heard = [&] { for (auto &p : b[i].peers) if (p.id == b[j].id) return true; return false; };
    while (now < end && !heard()) step();
  }

  bool allIdle() const
  {
    for (auto &x : b) if (x.eng.phase() != Phase::IDLE) return false;
    return true;
  }

  void runUntilIdle(uint32_t maxMs)
  {
    uint32_t end = now + maxMs;
    runFor(200);
    while (now < end && !allIdle()) step();
    runFor(500);
  }
};

// ---------------------------------------------------------------------------
static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures <= 25) { printf("  FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static int countEv(const SimBadge &x, uint8_t t)
{
  int n = 0;
  for (auto &e : x.events) if (e.type == t) n++;
  return n;
}

static const BattleEvent *lastEv(const SimBadge &x, uint8_t t)
{
  const BattleEvent *r = nullptr;
  for (auto &e : x.events) if (e.type == t) r = &e;
  return r;
}

// Both sides finished the same battle with mirrored results.
static void checkConsistent(SimBadge &A, SimBadge &B, const char *tag, uint32_t seed)
{
  const BattleEvent *oa = lastEv(A, EV_BATTLE_OVER), *ob = lastEv(B, EV_BATTLE_OVER);
  CHECK(oa && ob, "%s seed %u: battle did not finish on both (A=%d B=%d, aborts A=%d B=%d)",
        tag, seed, oa != nullptr, ob != nullptr, countEv(A, EV_ABORTED), countEv(B, EV_ABORTED));
  if (!oa || !ob) return;
  CHECK(oa->outcome == -ob->outcome, "%s seed %u: outcomes not mirrored (%d vs %d)", tag, seed, oa->outcome, ob->outcome);
  CHECK(A.eng.me().hp == B.eng.them().hp && A.eng.them().hp == B.eng.me().hp,
        "%s seed %u: HP mismatch A(%d,%d) B(%d,%d)", tag, seed,
        A.eng.me().hp, A.eng.them().hp, B.eng.me().hp, B.eng.them().hp);

  // round-by-round moves mirrored
  std::vector<const BattleEvent *> ra, rb;
  for (auto &e : A.events) if (e.type == EV_ROUND_RESULT) ra.push_back(&e);
  for (auto &e : B.events) if (e.type == EV_ROUND_RESULT) rb.push_back(&e);
  CHECK(ra.size() == rb.size(), "%s seed %u: round count %zu vs %zu", tag, seed, ra.size(), rb.size());
  for (size_t i = 0; i < ra.size() && i < rb.size(); i++)
    CHECK(ra[i]->myMove == rb[i]->oppMove && ra[i]->oppMove == rb[i]->myMove &&
          ra[i]->rr.winner == -rb[i]->rr.winner,
          "%s seed %u: round %zu differs", tag, seed, i + 1);
  CHECK(countEv(A, EV_CHEAT_DETECTED) == 0 && countEv(B, EV_CHEAT_DETECTED) == 0,
        "%s seed %u: false cheat detection", tag, seed);
}

// ---------------------------------------------------------------------------
static void testSha()
{
  uint8_t h[32];
  dcSha256((const uint8_t *)"abc", 3, h);
  const uint8_t want[4] = {0xba, 0x78, 0x16, 0xbf};
  CHECK(memcmp(h, want, 4) == 0, "sha256(\"abc\") wrong");
  char big[200];
  memset(big, 'a', sizeof(big));
  dcSha256((const uint8_t *)big, 200, h);   // multi-block path; just must not crash
}

static void testRulesSymmetry()
{
  for (int ma = 1; ma <= 3; ma++)
    for (int mb = 1; mb <= 3; mb++)
      for (int c1 = 0; c1 < CLASS_COUNT; c1++)
        for (int c2 = 0; c2 < CLASS_COUNT; c2++)
          for (int m1 = 0; m1 <= METER_MAX; m1++)
            for (int m2 = 0; m2 <= METER_MAX; m2++)
            {
              Fighter a1 = {(uint8_t)c1, 3, 14, (uint8_t)m1, 0}, b1 = {(uint8_t)c2, 7, 9, (uint8_t)m2, 0};
              Fighter a2 = a1, b2 = b1;
              RoundResult r1 = resolveRound(a1, ma, b1, mb);
              RoundResult r2 = resolveRound(b2, mb, a2, ma);
              CHECK(r1.winner == -r2.winner && r1.dmgToA == r2.dmgToB && r1.dmgToB == r2.dmgToA &&
                    a1.hp == a2.hp && b1.hp == b2.hp && a1.meter == a2.meter && b1.meter == b2.meter,
                    "rules not symmetric: %s vs %s", moveName(ma), moveName(mb));
            }

  // spot checks of the triangle
  Fighter a = {CLASS_HACKER, 1, 12, 0, 0}, b = {CLASS_HACKER, 1, 12, 0, 0};
  CHECK(resolveRound(a, MOVE_STRIKE, b, MOVE_CHARGE).winner == +1, "Strike should beat Charge");
  CHECK(resolveRound(a, MOVE_BLOCK, b, MOVE_STRIKE).winner == +1, "Block should beat Strike");
  CHECK(resolveRound(a, MOVE_CHARGE, b, MOVE_BLOCK).winner == +1, "Charge should beat Block");
  Fighter c = {CLASS_HACKER, 1, 12, METER_MAX, 0}, d = {CLASS_HACKER, 1, 12, 0, 0};
  RoundResult u = resolveRound(c, MOVE_STRIKE, d, MOVE_BLOCK);
  CHECK(u.winner == +1 && u.effect == FX_UNBLOCKABLE && c.meter == 0, "full-meter Strike should be unblockable");

  // one hit = tier damage; HP = HITS_TO_KO hits at every level
  for (int lv = 1; lv <= MAX_LEVEL; lv++)
    CHECK(levelMaxHp(lv) == HITS_TO_KO * levelHitDamage(lv) && levelHitDamage(lv) == BASE_HIT_DMG * (1 + (lv - 1) / LEVELS_PER_TIER),
          "level %d scaling wrong", lv);
  CHECK(levelHitDamage(1) == 1 && levelMaxHp(1) == HITS_TO_KO, "level 1 should hit for 1");
  Fighter e = {CLASS_HACKER, 1, 2, 0, 0}, f = {CLASS_HACKER, 7, 6, 0, 0};
  RoundResult cl = resolveRound(e, MOVE_STRIKE, f, MOVE_STRIKE);
  CHECK(cl.effect == FX_CLASH && cl.dmgToA == levelHitDamage(7) && cl.dmgToB == levelHitDamage(1),
        "clash: both hits should land with each attacker's damage");
}

static void testNormal(double loss, int runs, const char *tag)
{
  for (int s = 1; s <= runs; s++)
  {
    Sim sim(s);
    sim.loss = loss;
    SimBadge &A = sim.add(0x100001 + s);
    SimBadge &B = sim.add(0x200002 + s);
    A.level = 1 + s % 10; B.level = 1 + (s / 10) % 10;
    A.hp = levelMaxHp(A.level); B.hp = levelMaxHp(B.level);
    sim.init();
    sim.waitHeard(0, 1);
    sim.waitHeard(1, 0);
    CHECK(sim.b[0].eng.challenge(sim.b[1].id, sim.b[0].peers.data(), (int)sim.b[0].peers.size(), sim.now),
          "%s seed %d: challenge refused", tag, s);
    sim.runUntilIdle(200000);
    checkConsistent(sim.b[0], sim.b[1], tag, s);
  }
}

static void testDecline()
{
  for (int s = 1; s <= 50; s++)
  {
    Sim sim(s);
    sim.loss = 0.3;
    sim.add(0xA1).acceptPolicy = 0;
    sim.add(0xB2);
    sim.init();
    sim.runFor(1000);
    SimBadge &B = sim.b[1], &A = sim.b[0];
    B.eng.challenge(A.id, B.peers.data(), (int)B.peers.size(), sim.now);
    sim.runUntilIdle(60000);
    CHECK(countEv(B, EV_CHALLENGE_DECLINED) == 1, "decline seed %d: challenger didn't see decline", s);
    CHECK(countEv(A, EV_CHALLENGE_RECEIVED) == 1, "decline seed %d: re-prompted %d times", s, countEv(A, EV_CHALLENGE_RECEIVED));
    CHECK(countEv(A, EV_BATTLE_START) == 0 && countEv(B, EV_BATTLE_START) == 0, "decline seed %d: battle started", s);
  }
}

static void testNoAnswer()
{
  Sim sim(5);
  sim.add(0xA1).acceptPolicy = -1;
  sim.add(0xB2);
  sim.init();
  sim.runFor(1000);
  SimBadge &A = sim.b[0], &B = sim.b[1];
  B.eng.challenge(A.id, B.peers.data(), (int)B.peers.size(), sim.now);
  sim.runUntilIdle(60000);
  CHECK(countEv(B, EV_CHALLENGE_DECLINED) + countEv(B, EV_CHALLENGE_TIMEOUT) == 1, "no-answer: challenger not released");
  CHECK(countEv(A, EV_BATTLE_START) == 0, "no-answer: battle started without consent");
}

static void testSimultaneous()
{
  for (int s = 1; s <= 100; s++)
  {
    Sim sim(s);
    sim.loss = 0.2;
    sim.add(0x300 + s);
    sim.add(0x900 + s);
    sim.init();
    sim.runFor(1000);
    SimBadge &A = sim.b[0], &B = sim.b[1];
    A.eng.challenge(B.id, A.peers.data(), (int)A.peers.size(), sim.now);
    B.eng.challenge(A.id, B.peers.data(), (int)B.peers.size(), sim.now);
    sim.runUntilIdle(200000);
    checkConsistent(A, B, "simultaneous", s);
  }
}

static void testNoInput()
{
  // Nobody touches the badge: pick timers play random moves; battle still ends.
  Sim sim(9);
  sim.add(0xAA).picksMoves = false;
  sim.add(0xBB).picksMoves = false;
  sim.init();
  sim.runFor(1000);
  SimBadge &A = sim.b[0], &B = sim.b[1];
  A.eng.challenge(B.id, A.peers.data(), (int)A.peers.size(), sim.now);
  sim.runUntilIdle(300000);
  checkConsistent(A, B, "no-input", 9);
  CHECK(countEv(A, EV_AUTO_MOVE) > 0, "no-input: auto move never fired");
}

static void testWalkAway()
{
  for (int s = 1; s <= 30; s++)
  {
    Sim sim(s);
    sim.add(0xC1);
    sim.add(0xD2);
    sim.init();
    sim.runFor(1000);
    SimBadge &A = sim.b[0], &B = sim.b[1];
    A.eng.challenge(B.id, A.peers.data(), (int)A.peers.size(), sim.now);
    sim.runFor(3000 + sim.rand(5000));
    bool wasInBattle = B.eng.inBattle();
    B.present = false;   // B walks out of range
    sim.runFor(15000);
    CHECK(A.eng.phase() == Phase::IDLE, "walk-away seed %d: A stuck in phase %d", s, (int)A.eng.phase());
    if (wasInBattle && !lastEv(A, EV_BATTLE_OVER))
      CHECK(countEv(A, EV_ABORTED) == 1, "walk-away seed %d: no abort", s);
  }
}

static void testCheat()
{
  // B lies: after committing, its reveal shows a different move than it committed to.
  int detected = 0, battles = 0;
  for (int s = 1; s <= 50; s++)
  {
    Sim sim(s);
    sim.add(0xE1);
    sim.add(0xF2);
    sim.tamper = [](SimBadge &from, SimBadge &, AdvFrame &f) {
      // swap the revealed move wherever it travels: REVEAL, PICK(r>1) and DONE frames
      bool carriesReveal = f.state == W_REVEAL || f.state == W_DONE || (f.state == W_PICK && f.round > 1);
      if (from.id == 0xF2 && carriesReveal) f.data[0] = (f.data[0] % 3) + 1;
    };
    sim.init();
    sim.runFor(1000);
    SimBadge &A = sim.b[0], &B = sim.b[1];
    A.eng.challenge(B.id, A.peers.data(), (int)A.peers.size(), sim.now);
    sim.runUntilIdle(120000);
    battles++;
    // A is the honest badge: it always holds B's commitment before any reveal,
    // so the lie must be caught every time.
    if (countEv(A, EV_CHEAT_DETECTED)) detected++;
  }
  CHECK(detected == battles, "cheat detected only %d/%d times", detected, battles);
  printf("  cheat detection: %d/%d battles caught\n", detected, battles);
}

static void testThreeBadges()
{
  // A battles B while C wanders around and challenges A (should be ignored: A is busy).
  for (int s = 1; s <= 50; s++)
  {
    Sim sim(s);
    sim.loss = 0.25;
    sim.add(0x111);
    sim.add(0x222);
    sim.add(0x333);
    sim.init();
    sim.runFor(1000);
    SimBadge &A = sim.b[0], &B = sim.b[1], &C = sim.b[2];
    A.eng.challenge(B.id, A.peers.data(), (int)A.peers.size(), sim.now);
    sim.runFor(2000);
    C.eng.challenge(A.id, C.peers.data(), (int)C.peers.size(), sim.now);   // likely refused: A busy
    sim.runUntilIdle(200000);
    checkConsistent(A, B, "three-badges", s);
    CHECK(countEv(C, EV_BATTLE_START) == 0, "three-badges seed %d: C joined a busy badge", s);
  }
}

static void testProgression()
{
  uint32_t t = 1000;
  Player p;
  playerInit(p);
  CHECK(p.level == 1 && p.hp == playerMaxHp(p), "new player not level 1 at full HP");

  // first equal-level win levels you up immediately
  XpAward a = playerApplyBattle(p, +1, p.hp, 1, 0x1001, t);
  CHECK(a.gained == XP_WIN && a.leveledUp && p.level == 2, "first win should reach level 2 (got L%u, +%lu)", p.level, (unsigned long)a.gained);

  // level-gap multipliers (fresh opponent IDs so the cooldown doesn't apply)
  p.level = 5; p.xp = 0;
  a = playerApplyBattle(p, +1, 10, 2, 0x2001, t);   // gap -3
  CHECK(a.gained == XP_WIN * 10 / 100 && a.percent == 10, "win vs much weaker: +%lu", (unsigned long)a.gained);
  a = playerApplyBattle(p, +1, 10, 4, 0x2002, t);   // gap -1
  CHECK(a.gained == XP_WIN / 2, "win vs 1 lower: +%lu", (unsigned long)a.gained);
  p.xp = 0;
  a = playerApplyBattle(p, +1, 10, 9, 0x2003, t);   // gap +4 -> capped at +3
  CHECK(a.gained == XP_WIN * 250 / 100, "win vs much stronger: +%lu", (unsigned long)a.gained);
  p.level = 5; p.xp = 0;
  a = playerApplyBattle(p, -1, 10, 8, 0x2004, t);   // loss vs stronger: not boosted
  CHECK(a.gained == XP_LOSS && a.percent == 100, "loss vs stronger should be base XP: +%lu", (unsigned long)a.gained);
  a = playerApplyBattle(p, 0, 10, 6, 0x2005, t);    // draw vs 1 higher: boosted
  CHECK(a.gained == XP_DRAW * 150 / 100, "draw vs 1 higher: +%lu", (unsigned long)a.gained);

  // repeat opponent within the cooldown: no XP
  a = playerApplyBattle(p, +1, 10, 5, 0x2005, t + 1000);
  CHECK(a.repeatOpponent && a.gained == 0, "repeat opponent should give no XP");
  a = playerApplyBattle(p, +1, 10, 5, 0x2005, t + XP_REPEAT_COOLDOWN_MS + 2000);
  CHECK(!a.repeatOpponent && a.gained == XP_WIN, "cooldown should expire");

  // old saves: HP and XP clamped, no free levels
  Player old;
  playerInit(old);
  old.level = 3; old.hp = 30; old.xp = 500;
  CHECK(playerNormalize(old) && old.hp == playerMaxHp(old) && old.level == 3 && old.xp == playerXpToNext(old) - 1,
        "normalize: L%u HP %u XP %lu", old.level, old.hp, (unsigned long)old.xp);

  // level cap
  Player top;
  playerInit(top);
  top.level = MAX_LEVEL;
  playerApplyBattle(top, +1, 10, MAX_LEVEL, 0x3001, t);
  CHECK(top.level == MAX_LEVEL && top.xp == 0, "level cap exceeded");

  // progression pace: equal-level battles with the odds measured in the battle sim
  std::mt19937 r(7);
  Player q;
  playerInit(q);
  uint32_t now = 0;
  int battles = 0, reached[MAX_LEVEL + 1] = {0};
  while (q.level < MAX_LEVEL && battles < 5000)
  {
    int x = r() % 100;
    int8_t out = x < 43 ? +1 : (x < 62 ? 0 : -1);   // equal-level odds measured with the engine
    now += XP_REPEAT_COOLDOWN_MS + 1;   // a new opponent each time
    playerApplyBattle(q, out, playerMaxHp(q), q.level, 0x4000 + battles, now);
    battles++;
    if (!reached[q.level]) reached[q.level] = battles;
  }
  printf("  battles to reach level:");
  for (int l = 2; l <= MAX_LEVEL; l++) printf(" L%d=%d", l, reached[l]);
  printf("\n");
  CHECK(reached[2] <= 4 && reached[4] <= 20, "early levels too slow (L2 %d, L4 %d)", reached[2], reached[4]);
  CHECK(reached[MAX_LEVEL] >= 100, "max level too fast (%d battles)", reached[MAX_LEVEL]);
}

int main()
{
  printf("DC35 battle protocol simulation\n");
  testSha();
  testRulesSymmetry();
  printf("- rules symmetry ok\n");
  testProgression();
  printf("- progression / XP done\n");
  testNormal(0.0, 300, "loss0%");
  testNormal(0.3, 300, "loss30%");
  testNormal(0.6, 200, "loss60%");
  printf("- normal battles (0/30/60%% loss) done\n");
  testDecline();
  testNoAnswer();
  printf("- decline / no answer done\n");
  testSimultaneous();
  printf("- simultaneous challenges done\n");
  testNoInput();
  testWalkAway();
  printf("- timeouts / walk-away done\n");
  testCheat();
  testThreeBadges();
  printf("- cheat detection / third badge done\n");
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
