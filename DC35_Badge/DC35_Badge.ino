/*******************************************************************************
 * DC35_Badge.ino - DEF CON 35 "Dungeon Edition" badge firmware (prototype)
 *
 * Board:  Guition JC4827W543 (ESP32-S3 N4R8, 4.3" 480x272 NV3041A QSPI)
 * Needs:  esp32 board package 3.x, "GFX Library for Arduino" 1.4.x+
 * Tools:  ESP32S3 Dev Module, Flash 4MB, Partition "Default 4MB with spiffs",
 *         PSRAM "OPI PSRAM", USB CDC On Boot as for the other sketches
 *
 * What works now:
 *   - unique badge identity (DC35-XXXXXX from the Bluetooth MAC)
 *   - player state saved to flash: level, XP, HP, win/loss record (class: HACKER)
 *   - nearby badge list with range (CLOSE / NEAR / FAR)
 *   - optional battles: challenge -> accept -> up to 5 rounds of
 *     Strike / Block / Charge with sealed (commit-reveal) moves
 *   - HP loss persists, regenerates over time; 0 HP = knocked out
 *
 * Input:
 *   Touch screen (resistive - press firmly): tap a nearby badge then CHALLENGE;
 *     ACCEPT / DECLINE;
 *     STRIKE / BLOCK / CHARGE buttons during a battle.
 *   Serial Monitor at 115200: type "help" (same actions, plus dev commands).
 *   BRIGHT button (home screen) cycles display brightness 1-5 (saved).
 *   BOOT button: short press accepts a challenge / dismisses the result screen;
 *     hold 1.5 s toggles auto-play (handy for a second board).
 *   If taps land in the wrong place: "touchtest", then TOUCH_* in dc_config.h.
 *
 * Module map:
 *   dc_config.h          all tunable numbers
 *   dc_protocol.*        24-byte advertised frame          (portable)
 *   dc_rules.*           combat rules                      (portable)
 *   dc_player.*          XP / levels / HP regen            (portable)
 *   dc_battle.*          battle handshake state machine    (portable)
 *   dc_sha256.*          move commitments                  (portable)
 *   dc_radio.*           BLE advertise + scan              (device)
 *   dc_ui.*              display screens                   (device)
 *   dc_input.*           Serial commands + BOOT button     (device)
 *   dc_touch.*           XPT2046 resistive touch driver    (device)
 *   dc_storage.*         NVS save/load                     (device)
 * The portable modules also build on a PC: see tools/sim.
 ******************************************************************************/
#include <Arduino.h>
#include "esp_random.h"
#include "dc_config.h"
#include "dc_protocol.h"
#include "dc_rules.h"
#include "dc_player.h"
#include "dc_battle.h"
#include "dc_radio.h"
#include "dc_storage.h"
#include "dc_touch.h"
#include "dc_input.h"
#include "dc_ui.h"

static BattleEngine engine;
static Player       player;
static uint32_t     myId;
static char         myName[16];

static PeerInfo     peers[MAX_PEERS];
static int          peerCount = 0;

static bool         autoMode = false;
static uint32_t     selectedId = 0;      // badge picked in the on-screen list
static bool         touchDebug = false;
static uint8_t      brightness = BRIGHTNESS_DEFAULT;   // 1..BRIGHTNESS_LEVELS, saved
static uint32_t     autoActAt = 0;

static char         status[48] = "Ready. Type help in Serial Monitor";
static uint16_t     statusColor = UI_GREY;

// battle screen / result screen state
static bool         haveLastRound = false;
static uint8_t      lastMyMove, lastOppMove;
static RoundResult  lastRound;
static bool         showResult = false;
static int8_t       resultOutcome = 0;
static uint32_t     resultOpp = 0;
static XpAward      resultAward;
static bool         resultCheat = false;
static uint32_t     resultShownAt = 0;

static uint32_t rngFn() { return esp_random(); }

static void setStatus(uint16_t color, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(status, sizeof(status), fmt, ap);
  va_end(ap);
  statusColor = color;
  Serial.printf("> %s\n", status);
}

static const char *nameOf(uint32_t id)
{
  static char buf[16];
  badgeName(id, buf);
  return buf;
}

// ---------------------------------------------------------------------------
// Serial command handling
// ---------------------------------------------------------------------------
static void printHelp()
{
  Serial.println(F(
    "\n--- DC35 badge commands ---\n"
    "  list | l            nearby badges (numbered)\n"
    "  c <#|ID>            challenge badge # from the list, or by ID (e.g. c 2, c A1B2C3)\n"
    "  y / n               accept / decline a challenge (BOOT button = accept)\n"
    "  1 / 2 / 3           STRIKE / BLOCK / CHARGE during a battle\n"
    "  stats               your player stats\n"
    "  auto                toggle auto-play (accepts + random moves; or hold BOOT 1.5s)\n"
    "  heal                (dev) refill HP\n"
    "  factory-reset       (dev) erase saved player\n"
    "  touchtest           (dev) show tap coordinates to check touch calibration\n"
    "  bright [1-5|+]      display brightness: show, set 1 (dim) to 5 (full), or + for next\n"
    "Rules: Strike beats Charge, Block beats Strike, Charge beats Block.\n"
    "       Each win lands one hit; Strike vs Strike both hit. 3 rounds max.\n"
    "       A full meter (2 Charges) makes your next Strike unblockable.\n"));
}

static void printList()
{
  Serial.printf("\n%d badge(s) nearby:\n", peerCount);
  for (int i = 0; i < peerCount; i++)
  {
    const PeerInfo &q = peers[i];
    Serial.printf("  %d  %s  %-5s L%-2u HP%-3u %4.0f dBm %s%s\n", i + 1, nameOf(q.id),
                  className(q.f.cls), q.f.level, q.f.hp, q.rssiAvg,
                  (q.f.flags & FLAG_BUSY) ? " BUSY" : "", (q.f.flags & FLAG_KO) ? " KO" : "");
  }
}

static void printStats()
{
  Serial.printf("\n%s  %s  level %u  XP %lu/%lu  HP %u/%u%s\n  record W%u L%u D%u (%u battles)\n",
                myName, className(player.cls), player.level,
                (unsigned long)player.xp, (unsigned long)playerXpToNext(player),
                player.hp, playerMaxHp(player), player.ko ? " (KO)" : "",
                player.wins, player.losses, player.draws, player.battles);
}

static void doChallenge(int32_t arg, uint32_t now)
{
  const PeerInfo *target = nullptr;
  if (arg & CMD_ID_FLAG)
  {
    uint32_t id = arg & 0xFFFFFF;
    for (int i = 0; i < peerCount; i++) if (peers[i].id == id) target = &peers[i];
  }
  else if (arg >= 1 && arg <= peerCount) target = &peers[arg - 1];

  if (!target)                         { setStatus(UI_RED, "No such badge in range"); return; }
  if (!playerCanBattle(player))        { setStatus(UI_RED, "You're knocked out - rest first"); return; }
  if (engine.busy())                   { setStatus(UI_RED, "Busy - finish the current battle"); return; }
  if (target->f.flags & FLAG_KO)       { setStatus(UI_RED, "%s is knocked out", nameOf(target->id)); return; }
  if (target->f.flags & FLAG_BUSY)     { setStatus(UI_RED, "%s is busy", nameOf(target->id)); return; }
  if (target->rssiAvg < RSSI_CHALLENGE) { setStatus(UI_RED, "%s is too far away", nameOf(target->id)); return; }

  if (engine.challenge(target->id, peers, peerCount, now))
    setStatus(UI_YELLOW, "Challenging %s...", nameOf(target->id));
  else
    setStatus(UI_RED, "Couldn't challenge %s", nameOf(target->id));
}

static void handleCommand(const Command &c, uint32_t now)
{
  // any input dismisses the result screen
  if (showResult && c.type != CMD_NONE && engine.phase() == Phase::IDLE)
  {
    showResult = false;
    if (c.type == CMD_BUTTON) return;
  }

  switch (c.type)
  {
    case CMD_HELP:  printHelp(); break;
    case CMD_LIST:  printList(); break;
    case CMD_STATS: printStats(); break;
    case CMD_CHALLENGE: doChallenge(c.arg, now); break;

    case CMD_BUTTON:
    case CMD_ACCEPT:
      if (engine.phase() == Phase::CHALLENGED) engine.respond(true, now);
      else if (c.type == CMD_ACCEPT) setStatus(UI_GREY, "No challenge to accept");
      break;

    case CMD_DECLINE:
      if (engine.phase() == Phase::CHALLENGED)
      {
        engine.respond(false, now);
        setStatus(UI_GREY, "Challenge declined");
      }
      break;

    case CMD_MOVE:
      if (engine.needsMove())
      {
        engine.chooseMove(c.arg, now);
        setStatus(UI_GREEN, "You chose %s", moveName(c.arg));
      }
      else setStatus(UI_GREY, "Not choosing a move right now");
      break;

    case CMD_AUTO:
      autoMode = !autoMode;
      setStatus(UI_YELLOW, "Auto-play %s", autoMode ? "ON" : "OFF");
      break;

    case CMD_HEAL:
      player.hp = playerMaxHp(player);
      player.ko = 0;
      storageSave(player);
      setStatus(UI_GREEN, "HP restored");
      break;

    case CMD_RESET:
      storageErase();
      playerInit(player);
      storageSave(player);
      setStatus(UI_YELLOW, "Player reset");
      break;

    case CMD_SELECT:
      selectedId = (selectedId == (uint32_t)c.arg) ? 0 : (uint32_t)c.arg;   // tap again to unselect
      if (selectedId) setStatus(UI_WHITE, "Selected %s - tap CHALLENGE", nameOf(selectedId));
      break;

    case CMD_BRIGHT:
      if (c.arg < 0)
      {
        setStatus(UI_WHITE, "Brightness %u of %u", brightness, BRIGHTNESS_LEVELS);
        break;
      }
      if (c.arg == 0) brightness = brightness % BRIGHTNESS_LEVELS + 1;   // cycle 1..5
      else if (c.arg >= 1 && c.arg <= BRIGHTNESS_LEVELS) brightness = c.arg;
      else { setStatus(UI_RED, "Brightness is 1 to %u", BRIGHTNESS_LEVELS); break; }
      uiSetBrightness(brightness);
      storageSaveBrightness(brightness);
      setStatus(UI_WHITE, "Brightness %u of %u", brightness, BRIGHTNESS_LEVELS);
      break;

    case CMD_TOUCHTEST:
      touchDebug = !touchDebug;
      setStatus(UI_YELLOW, "Touch test %s", touchDebug ? "ON - tap the corners" : "OFF");
      break;

    case CMD_UNKNOWN:
      setStatus(UI_RED, "Unknown command - type help");
      break;
  }
}

// ---------------------------------------------------------------------------
// battle events -> player state, messages
// ---------------------------------------------------------------------------
static void handleEvent(const BattleEvent &e, uint32_t now)
{
  const char *opp = nameOf(e.oppId);
  switch (e.type)
  {
    case EV_CHALLENGE_SENT:      break;
    case EV_CHALLENGE_RECEIVED:  setStatus(UI_YELLOW, "%s challenges you! y/n", opp); showResult = false; break;
    case EV_CHALLENGE_CANCELLED: setStatus(UI_GREY, "%s withdrew the challenge", opp); break;
    case EV_CHALLENGE_DECLINED:  setStatus(UI_ORANGE, "%s declined", opp); break;
    case EV_CHALLENGE_TIMEOUT:   setStatus(UI_ORANGE, "%s didn't answer", opp); break;

    case EV_BATTLE_START:
      haveLastRound = false;
      showResult = false;
      setStatus(UI_GREEN, "Battle vs %s!", opp);
      break;

    case EV_ROUND_START:
      Serial.printf("--- Round %u: choose 1=STRIKE 2=BLOCK 3=CHARGE ---\n", e.round);
      break;

    case EV_AUTO_MOVE:
      setStatus(UI_ORANGE, "Too slow - random move played");
      break;

    case EV_ROUND_RESULT:
      haveLastRound = true;
      lastMyMove = e.myMove;
      lastOppMove = e.oppMove;
      lastRound = e.rr;
      Serial.printf("Round %u: you %s vs %s -> %s (you -%d, them -%d)  HP %d vs %d\n",
                    e.round, moveName(e.myMove), moveName(e.oppMove), effectText(e.rr.effect),
                    e.rr.dmgToA, e.rr.dmgToB, engine.me().hp, engine.them().hp);
      break;

    case EV_CHEAT_DETECTED:
      resultCheat = true;
      setStatus(UI_ORANGE, "%s's move didn't match its seal!", opp);
      break;

    case EV_BATTLE_OVER:
    {
      resultAward = playerApplyBattle(player, e.outcome, engine.me().hp, engine.them().level, e.oppId, now);
      storageSave(player);
      resultOutcome = e.outcome;
      resultOpp = e.oppId;
      showResult = true;
      resultShownAt = now;
      setStatus(e.outcome > 0 ? UI_GREEN : (e.outcome < 0 ? UI_RED : UI_YELLOW), "%s vs %s  (+%lu XP)",
                e.outcome > 0 ? "WON" : (e.outcome < 0 ? "LOST" : "DRAW"), opp,
                (unsigned long)resultAward.gained);
      if (resultAward.leveledUp) Serial.printf("*** LEVEL UP! Now level %u ***\n", player.level);
      break;
    }

    case EV_ABORTED:
    {
      const char *why = e.reason == AB_SILENT ? "out of range" : (e.reason == AB_LEFT ? "left the battle" : "stalled");
      setStatus(UI_ORANGE, "Battle cancelled: %s %s", opp, why);
      break;
    }
  }
}

// Auto-play: accept challenges and pick random moves after a short "think".
static void autoPlay(uint32_t now)
{
  if (!autoMode) { autoActAt = 0; return; }
  bool wantsAction = engine.phase() == Phase::CHALLENGED || engine.needsMove();
  if (!wantsAction) { autoActAt = 0; return; }
  if (!autoActAt) { autoActAt = now + 800 + esp_random() % 2000; return; }
  if ((int32_t)(now - autoActAt) < 0) return;
  autoActAt = 0;
  if (engine.phase() == Phase::CHALLENGED) engine.respond(true, now);
  else if (engine.needsMove())
  {
    uint8_t m = MOVE_STRIKE + esp_random() % 3;
    engine.chooseMove(m, now);
    Serial.printf("(auto) chose %s\n", moveName(m));
  }
}

// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(300);

  uiBegin();
  brightness = storageLoadBrightness(BRIGHTNESS_DEFAULT);
  if (brightness < 1 || brightness > BRIGHTNESS_LEVELS) brightness = BRIGHTNESS_DEFAULT;
  uiSetBrightness(brightness);
  inputBegin();
  touchBegin();

  if (!storageLoad(player))
  {
    playerInit(player);
    storageSave(player);
  }
  else if (playerNormalize(player))   // saved under older rules (HP / XP caps)
  {
    storageSave(player);
  }

  badgeName(radioBadgeId(), myName);
  myId = radioBegin(myName);
  engine.begin(myId, rngFn);

  Serial.printf("\n=== DC35 badge %s ===\n", myName);
  printStats();
  printHelp();
}

void loop()
{
  uint32_t now = millis();

  peerCount = radioSnapshot(peers, MAX_PEERS, now);

  Command c;
  while (inputPoll(c)) handleCommand(c, now);

  int16_t tx, ty, rx, ry;
  if (touchPollTap(tx, ty, &rx, &ry))
  {
    if (touchDebug)
    {
      Serial.printf("tap raw (%d,%d) -> screen (%d,%d)\n", rx, ry, tx, ty);
      uiDebugDot(tx, ty);
    }
    else if (uiHitTest(tx, ty, c)) handleCommand(c, now);
  }

  if (!engine.busy() && playerRegen(player, now)) storageSave(player);
  engine.setSelf(player.cls, player.level, player.hp, playerCanBattle(player), autoMode ? FLAG_BOT : 0);
  engine.tick(now, peers, peerCount);

  BattleEvent e;
  while (engine.pollEvent(e)) handleEvent(e, now);
  if (!showResult) resultCheat = false;

  autoPlay(now);

  AdvFrame f;
  engine.buildFrame(f);
  radioSetFrame(f, engine.busy() && engine.phase() != Phase::CHALLENGED);

  if (showResult && now - resultShownAt > 10000 && engine.phase() == Phase::IDLE) showResult = false;

  static uint32_t lastUi = 0;
  if (now - lastUi >= 100)
  {
    lastUi = now;
    UiModel m = {};
    m.myName = myName;
    m.player = &player;
    m.autoMode = autoMode;
    m.peers = peers;
    m.peerCount = peerCount;
    m.selectedId = selectedId;
    m.brightness = brightness;
    m.eng = &engine;
    m.now = now;
    m.status = status;
    m.statusColor = statusColor;
    m.haveLastRound = haveLastRound;
    m.lastMyMove = lastMyMove;
    m.lastOppMove = lastOppMove;
    m.lastRound = lastRound;
    m.showResult = showResult;
    m.outcome = resultOutcome;
    m.oppId = resultOpp;
    m.award = resultAward;
    m.cheatWin = resultCheat;
    uiRender(m);
  }

  delay(5);
}
