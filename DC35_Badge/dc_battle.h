// dc_battle.h - connectionless battle handshake between two badges.
//
// Hardware independent: the engine is fed the latest frame heard from each
// nearby badge (tick) and produces the frame this badge should advertise
// (buildFrame). It can be compiled and stress-tested on a PC - see tools/sim.
//
// Flow (A challenges B):
//   A: CHALLENGE(B, session, nonceA)          B: user is prompted
//   B accepts -> PICK(1, nonceB)               A sees it -> PICK(1)
//   each round r:
//     PICK(r)   choose a move (data = my reveal of round r-1)
//     COMMIT(r) advertise hash(move, salt, both nonces, round, my id)
//     REVEAL(r) once the opponent has committed, advertise move + salt
//     both badges verify the other's reveal and resolve the round identically
//   DONE: final reveal advertised for a few seconds, then back to IDLE.
//
// Commit-reveal means neither side can see the other's move before locking
// in its own, even though every frame is public.
#pragma once
#include <stdint.h>
#include "dc_protocol.h"
#include "dc_rules.h"

struct PeerInfo
{
  uint32_t id;
  AdvFrame f;          // latest frame heard
  int8_t   rssi;       // latest raw RSSI
  float    rssiAvg;    // smoothed RSSI
  uint32_t firstSeen;
  uint32_t lastSeen;
};

enum class Phase : uint8_t { IDLE, CHALLENGING, CHALLENGED, DECLINING, PICK, COMMIT, REVEAL, DONE };

enum BattleEventType : uint8_t
{
  EV_CHALLENGE_SENT,
  EV_CHALLENGE_RECEIVED,
  EV_CHALLENGE_CANCELLED,   // challenger withdrew / went away
  EV_CHALLENGE_DECLINED,    // opponent said no
  EV_CHALLENGE_TIMEOUT,     // no answer
  EV_BATTLE_START,
  EV_ROUND_START,
  EV_AUTO_MOVE,             // pick timer ran out, random move played
  EV_ROUND_RESULT,
  EV_BATTLE_OVER,
  EV_ABORTED,
  EV_CHEAT_DETECTED,        // opponent's reveal didn't match its commitment
};

enum AbortReason : uint8_t { AB_NONE, AB_SILENT, AB_LEFT, AB_STALLED };

struct BattleEvent
{
  uint8_t     type;
  uint32_t    oppId;
  uint8_t     round;
  uint8_t     myMove, oppMove;
  RoundResult rr;          // winner +1 = this badge
  int8_t      outcome;     // EV_BATTLE_OVER: +1 win, 0 draw, -1 loss
  uint8_t     reason;      // EV_ABORTED: AbortReason
};

typedef uint32_t (*RandomFn)();

class BattleEngine
{
public:
  void begin(uint32_t myId, RandomFn rng);

  // Current player stats; frozen while a battle is in progress.
  void setSelf(uint8_t cls, uint8_t level, uint8_t hp, bool canBattle, uint8_t extraFlags);

  bool challenge(uint32_t peerId, const PeerInfo *peers, int n, uint32_t now);
  bool respond(bool accept, uint32_t now);
  bool chooseMove(uint8_t move, uint32_t now);

  void tick(uint32_t now, const PeerInfo *peers, int n);
  void buildFrame(AdvFrame &f) const;
  bool pollEvent(BattleEvent &e);

  Phase    phase() const { return ph; }
  bool     inBattle() const { return ph == Phase::PICK || ph == Phase::COMMIT || ph == Phase::REVEAL || ph == Phase::DONE; }
  bool     busy() const { return ph != Phase::IDLE; }
  bool     needsMove() const { return ph == Phase::PICK && myMove == MOVE_NONE; }
  uint8_t  chosenMove() const { return myMove; }
  uint32_t opponent() const { return oppId; }
  uint8_t  round() const { return rnd; }
  uint32_t deadline() const { return phaseDeadline; }
  const Fighter &me() const { return fMe; }
  const Fighter &them() const { return fOpp; }
  uint8_t  myMaxHpAtStart() const { return startHpMe; }
  uint8_t  oppMaxHpAtStart() const { return startHpOpp; }

private:
  // identity / self
  uint32_t myId = 0;
  RandomFn rng = nullptr;
  uint8_t  selfCls = 0, selfLevel = 1, selfHp = 0, selfFlags = 0;
  bool     selfCanBattle = true;

  // battle
  Phase    ph = Phase::IDLE;
  bool     challenger = false;
  uint32_t oppId = 0;
  uint16_t session = 0;
  uint32_t nonceA = 0, nonceB = 0;   // challenger / acceptor nonces
  uint8_t  rnd = 0;
  uint8_t  myMove = MOVE_NONE;
  uint8_t  mySalt[7];
  uint8_t  myCommit[8];
  uint8_t  lastReveal[8];            // my reveal of the previous round
  uint8_t  peerCommit[8];
  bool     havePeerCommit = false;
  Fighter  fMe, fOpp;
  uint8_t  startHpMe = 0, startHpOpp = 0;
  AdvFrame pending;                  // challenger's frame while we decide

  // timers
  uint32_t phaseStart = 0, phaseDeadline = 0, disengagedSince = 0;

  // ignore a challenge we already answered (it keeps being advertised briefly)
  uint32_t ignoreId = 0;
  uint16_t ignoreSession = 0;

  // event queue
  BattleEvent q[16];
  uint8_t qHead = 0, qTail = 0;

  void push(const BattleEvent &e);
  BattleEvent ev(uint8_t type) const;
  void enter(Phase p, uint32_t now, uint32_t timeout);
  void reset(uint32_t now);
  void abort(uint8_t reason, uint32_t now);
  void startBattle(const AdvFrame &opp, uint32_t now);
  void startRound(uint32_t now);
  void makeCommit(uint8_t out[8], uint32_t whoId, uint8_t move, const uint8_t salt[7]) const;
  bool engaged(const PeerInfo *op) const;
  bool peerAtLeast(const PeerInfo *op, uint8_t state, uint8_t round) const;
  bool takePeerReveal(const PeerInfo *op, uint8_t &move, uint8_t salt[7]) const;
  void finishRound(uint8_t peerMove, uint32_t now);
  const PeerInfo *find(const PeerInfo *peers, int n, uint32_t id) const;
};
