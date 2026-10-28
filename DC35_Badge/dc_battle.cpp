#include "dc_battle.h"
#include "dc_config.h"
#include "dc_sha256.h"
#include <string.h>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint8_t stateRank(uint8_t s)
{
  switch (s)
  {
    case W_PICK:   return 1;
    case W_COMMIT: return 2;
    case W_REVEAL: return 3;
    case W_DONE:   return 4;
    default:       return 0;   // W_CHALLENGE
  }
}

void BattleEngine::begin(uint32_t id, RandomFn r)
{
  myId = id;
  rng = r;
  ph = Phase::IDLE;
}

void BattleEngine::setSelf(uint8_t cls, uint8_t level, uint8_t hp, bool canBattle, uint8_t extraFlags)
{
  selfCls = cls;
  selfLevel = level;
  selfHp = hp;
  selfCanBattle = canBattle;
  selfFlags = extraFlags;
}

const PeerInfo *BattleEngine::find(const PeerInfo *peers, int n, uint32_t id) const
{
  for (int i = 0; i < n; i++)
    if (peers[i].id == id) return &peers[i];
  return nullptr;
}

void BattleEngine::push(const BattleEvent &e)
{
  uint8_t next = (qHead + 1) % 16;
  if (next == qTail) return;   // full: drop
  q[qHead] = e;
  qHead = next;
}

bool BattleEngine::pollEvent(BattleEvent &e)
{
  if (qTail == qHead) return false;
  e = q[qTail];
  qTail = (qTail + 1) % 16;
  return true;
}

BattleEvent BattleEngine::ev(uint8_t type) const
{
  BattleEvent e;
  memset(&e, 0, sizeof(e));
  e.type = type;
  e.oppId = oppId;
  e.round = rnd;
  return e;
}

void BattleEngine::enter(Phase p, uint32_t now, uint32_t timeout)
{
  ph = p;
  phaseStart = now;
  phaseDeadline = now + timeout;
}

void BattleEngine::reset(uint32_t now)
{
  enter(Phase::IDLE, now, 0);
  oppId = 0;
  session = 0;
  rnd = 0;
  myMove = MOVE_NONE;
  havePeerCommit = false;
  disengagedSince = 0;
}

void BattleEngine::abort(uint8_t reason, uint32_t now)
{
  BattleEvent e = ev(EV_ABORTED);
  e.reason = reason;
  push(e);
  reset(now);
}

// ---------------------------------------------------------------------------
// commitment = first 8 bytes of SHA-256 over everything that makes this
// move unique: both nonces (so nothing can be precomputed), the round, who
// is moving, the move and a random salt (so the 3 possible moves can't be
// guessed from the hash).
// ---------------------------------------------------------------------------
void BattleEngine::makeCommit(uint8_t out[8], uint32_t whoId, uint8_t move, const uint8_t salt[7]) const
{
  uint8_t buf[26];
  buf[0] = 'D'; buf[1] = 'C'; buf[2] = '3'; buf[3] = '5';
  buf[4] = session & 0xFF; buf[5] = session >> 8;
  put32(buf + 6, nonceA);
  put32(buf + 10, nonceB);
  buf[14] = rnd;
  buf[15] = whoId >> 16; buf[16] = whoId >> 8; buf[17] = whoId;
  buf[18] = move;
  memcpy(buf + 19, salt, 7);
  uint8_t h[32];
  dcSha256(buf, sizeof(buf), h);
  memcpy(out, h, 8);
}

// The opponent's frame is about *this* battle with us.
bool BattleEngine::engaged(const PeerInfo *op) const
{
  if (!op || op->f.opp != myId || op->f.session != session) return false;
  uint8_t s = op->f.state;
  return s == W_CHALLENGE || s == W_PICK || s == W_COMMIT || s == W_REVEAL || s == W_DONE;
}

// Opponent has reached (state, round) or later.
bool BattleEngine::peerAtLeast(const PeerInfo *op, uint8_t state, uint8_t round) const
{
  if (!engaged(op)) return false;
  if (op->f.round != round) return op->f.round > round;
  return stateRank(op->f.state) >= stateRank(state);
}

// The opponent's reveal of the current round can arrive in three frame types:
// REVEAL(r), PICK(r+1) (carries the previous reveal) or DONE(r).
bool BattleEngine::takePeerReveal(const PeerInfo *op, uint8_t &move, uint8_t salt[7]) const
{
  if (!engaged(op)) return false;
  const AdvFrame &f = op->f;
  bool has = (f.state == W_REVEAL && f.round == rnd) ||
             (f.state == W_PICK   && f.round == rnd + 1) ||
             (f.state == W_DONE   && f.round == rnd);
  if (!has) return false;
  move = f.data[0];
  memcpy(salt, f.data + 1, 7);
  return true;
}

// ---------------------------------------------------------------------------
// user actions
// ---------------------------------------------------------------------------
bool BattleEngine::challenge(uint32_t peerId, const PeerInfo *peers, int n, uint32_t now)
{
  if (ph != Phase::IDLE || !selfCanBattle) return false;
  const PeerInfo *op = find(peers, n, peerId);
  if (!op || (op->f.flags & (FLAG_BUSY | FLAG_KO))) return false;

  oppId = peerId;
  challenger = true;
  session = (uint16_t)(rng() | 1);
  nonceA = rng();
  nonceB = 0;
  fMe = {selfCls, selfLevel, selfHp, 0, 0};   // stats frozen for the battle
  startHpMe = selfHp;
  enter(Phase::CHALLENGING, now, CHALLENGE_TIMEOUT_MS);
  push(ev(EV_CHALLENGE_SENT));
  return true;
}

bool BattleEngine::respond(bool accept, uint32_t now)
{
  if (ph != Phase::CHALLENGED) return false;
  ignoreId = oppId;
  ignoreSession = session;

  if (!accept)
  {
    enter(Phase::DECLINING, now, DECLINE_BROADCAST_MS);
    return true;
  }

  challenger = false;
  nonceB = rng();
  fMe = {selfCls, selfLevel, selfHp, 0, 0};
  startHpMe = selfHp;
  startBattle(pending, now);
  return true;
}

bool BattleEngine::chooseMove(uint8_t move, uint32_t now)
{
  (void)now;
  if (ph != Phase::PICK || myMove != MOVE_NONE) return false;
  if (move < MOVE_STRIKE || move > MOVE_CHARGE) return false;
  myMove = move;
  for (int i = 0; i < 7; i++) mySalt[i] = (uint8_t)rng();
  return true;
}

// ---------------------------------------------------------------------------
// battle progression
// ---------------------------------------------------------------------------
void BattleEngine::startBattle(const AdvFrame &opp, uint32_t now)
{
  fOpp = {opp.cls, opp.level, opp.hp, 0, 0};
  startHpOpp = opp.hp;
  rnd = 0;
  disengagedSince = 0;
  push(ev(EV_BATTLE_START));
  startRound(now);
}

void BattleEngine::startRound(uint32_t now)
{
  rnd++;
  myMove = MOVE_NONE;
  havePeerCommit = false;
  enter(Phase::PICK, now, PICK_TIMEOUT_MS);
  push(ev(EV_ROUND_START));
}

void BattleEngine::finishRound(uint8_t peerMove, uint32_t now)
{
  RoundResult rr = resolveRound(fMe, myMove, fOpp, peerMove);
  BattleEvent e = ev(EV_ROUND_RESULT);
  e.myMove = myMove;
  e.oppMove = peerMove;
  e.rr = rr;
  push(e);

  lastReveal[0] = myMove;
  memcpy(lastReveal + 1, mySalt, 7);

  if (battleOver(fMe, fOpp, rnd))
  {
    BattleEvent o = ev(EV_BATTLE_OVER);
    o.outcome = battleWinner(fMe, fOpp);
    push(o);
    enter(Phase::DONE, now, DONE_LINGER_MS);
  }
  else
  {
    startRound(now);
  }
}

void BattleEngine::tick(uint32_t now, const PeerInfo *peers, int n)
{
  const PeerInfo *op = oppId ? find(peers, n, oppId) : nullptr;

  switch (ph)
  {
    case Phase::IDLE:
    {
      if (!selfCanBattle) break;
      for (int i = 0; i < n; i++)
      {
        const PeerInfo &p = peers[i];
        if (p.f.state != W_CHALLENGE || p.f.opp != myId) continue;
        if (now - p.lastSeen > 2000) continue;
        if (p.id == ignoreId && p.f.session == ignoreSession) continue;
        oppId = p.id;
        session = p.f.session;
        nonceA = get32(p.f.data);
        pending = p.f;
        enter(Phase::CHALLENGED, now, CHALLENGE_TIMEOUT_MS);
        push(ev(EV_CHALLENGE_RECEIVED));
        break;
      }
      break;
    }

    case Phase::CHALLENGING:
    {
      if (op && op->f.opp == myId && op->f.session == session &&
          op->f.state == W_PICK && op->f.round == 1)
      {
        nonceB = get32(op->f.data);          // they accepted
        startBattle(op->f, now);
      }
      else if (op && op->f.opp == myId && op->f.session == session && op->f.state == W_DECLINE)
      {
        push(ev(EV_CHALLENGE_DECLINED));
        reset(now);
      }
      else if (op && op->f.state == W_CHALLENGE && op->f.opp == myId && myId < oppId)
      {
        // We challenged each other at the same time: the lower ID accepts theirs.
        session = op->f.session;
        nonceA = get32(op->f.data);
        nonceB = rng();
        challenger = false;
        ignoreId = oppId;
        ignoreSession = session;
        startBattle(op->f, now);
      }
      else if ((int32_t)(now - phaseDeadline) >= 0)
      {
        push(ev(EV_CHALLENGE_TIMEOUT));
        reset(now);
      }
      break;
    }

    case Phase::CHALLENGED:
    {
      bool stillChallenging = op && now - op->lastSeen < 3000 && op->f.state == W_CHALLENGE &&
                              op->f.opp == myId && op->f.session == session;
      if (!stillChallenging)
      {
        ignoreId = oppId;
        ignoreSession = session;
        push(ev(EV_CHALLENGE_CANCELLED));
        reset(now);
      }
      else if ((int32_t)(now - phaseDeadline) >= 0)
      {
        respond(false, now);   // no answer = decline
      }
      break;
    }

    case Phase::DECLINING:
      if ((int32_t)(now - phaseDeadline) >= 0) reset(now);
      break;

    case Phase::PICK:
    case Phase::COMMIT:
    case Phase::REVEAL:
    {
      // --- liveness checks ---
      if (!op || now - op->lastSeen > OPP_SILENT_ABORT_MS) { abort(AB_SILENT, now); break; }
      if (!engaged(op))
      {
        if (!disengagedSince) disengagedSince = now;
        else if (now - disengagedSince > OPP_DISENGAGED_ABORT_MS) { abort(AB_LEFT, now); break; }
      }
      else disengagedSince = 0;
      if (now - phaseStart > PHASE_STALL_ABORT_MS) { abort(AB_STALLED, now); break; }

      // Remember the opponent's commitment (first one seen is binding).
      if (!havePeerCommit && engaged(op) && op->f.state == W_COMMIT && op->f.round == rnd)
      {
        memcpy(peerCommit, op->f.data, 8);
        havePeerCommit = true;
      }

      if (ph == Phase::PICK)
      {
        if (myMove == MOVE_NONE && (int32_t)(now - phaseDeadline) >= 0)
        {
          chooseMove(MOVE_STRIKE + rng() % 3, now);
          push(ev(EV_AUTO_MOVE));
        }
        // Commit only once the opponent is in this round too, so it has
        // everything it needs (our nonce / previous reveal).
        if (myMove != MOVE_NONE && peerAtLeast(op, W_PICK, rnd))
        {
          makeCommit(myCommit, myId, myMove, mySalt);
          enter(Phase::COMMIT, now, 0);
        }
      }
      else if (ph == Phase::COMMIT)
      {
        // Reveal only when BOTH commitments have been exchanged: we hold
        // theirs, and they hold ours (ACK flag, or they already revealed -
        // which they only do once they hold ours). This guarantees every
        // reveal can be checked against its commitment.
        bool theyHaveMine = engaged(op) &&
                            ((op->f.state == W_COMMIT && op->f.round == rnd && (op->f.flags & FLAG_ACK)) ||
                             peerAtLeast(op, W_REVEAL, rnd));
        if (havePeerCommit && theyHaveMine) enter(Phase::REVEAL, now, 0);
      }
      else   // REVEAL
      {
        uint8_t pm, ps[7];
        if (takePeerReveal(op, pm, ps))
        {
          bool valid = havePeerCommit && pm >= MOVE_STRIKE && pm <= MOVE_CHARGE;
          if (valid)
          {
            uint8_t check[8];
            makeCommit(check, oppId, pm, ps);
            valid = memcmp(check, peerCommit, 8) == 0;
          }
          if (!valid)
          {
            BattleEvent c = ev(EV_CHEAT_DETECTED);
            c.oppMove = pm;
            push(c);
            BattleEvent o = ev(EV_BATTLE_OVER);
            o.outcome = +1;   // cheater forfeits
            push(o);
            reset(now);
            break;
          }
          finishRound(pm, now);
        }
      }
      break;
    }

    case Phase::DONE:
      // Stay until the opponent has our final reveal (it reached DONE or left).
      if ((int32_t)(now - phaseDeadline) >= 0 || !engaged(op) || op->f.state == W_DONE) reset(now);
      break;
  }
}

// ---------------------------------------------------------------------------
// what we advertise
// ---------------------------------------------------------------------------
void BattleEngine::buildFrame(AdvFrame &f) const
{
  memset(&f, 0, sizeof(f));
  f.version = PROTO_VERSION;
  f.id = myId;

  bool frozen = ph == Phase::CHALLENGING || inBattle();
  f.cls   = frozen ? fMe.cls : selfCls;
  f.level = frozen ? fMe.level : selfLevel;
  f.hp    = frozen ? startHpMe : selfHp;
  f.flags = selfFlags | (busy() ? FLAG_BUSY : 0) | (selfCanBattle ? 0 : FLAG_KO);

  uint32_t myNonce = challenger ? nonceA : nonceB;

  switch (ph)
  {
    case Phase::IDLE:
    case Phase::CHALLENGED:
      f.state = W_IDLE;
      return;
    case Phase::CHALLENGING:
      f.state = W_CHALLENGE;
      put32(f.data, nonceA);
      break;
    case Phase::DECLINING:
      f.state = W_DECLINE;
      break;
    case Phase::PICK:
      f.state = W_PICK;
      if (rnd == 1) put32(f.data, myNonce);
      else memcpy(f.data, lastReveal, 8);
      break;
    case Phase::COMMIT:
      f.state = W_COMMIT;
      memcpy(f.data, myCommit, 8);
      if (havePeerCommit) f.flags |= FLAG_ACK;
      break;
    case Phase::REVEAL:
      f.state = W_REVEAL;
      f.data[0] = myMove;
      memcpy(f.data + 1, mySalt, 7);
      break;
    case Phase::DONE:
      f.state = W_DONE;
      memcpy(f.data, lastReveal, 8);
      break;
  }
  f.opp = oppId;
  f.session = session;
  f.round = rnd;
}
