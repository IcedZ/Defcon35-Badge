// dc_ui.h - screens on the 480x272 display (device only).
// Redraws only what changed (cached text lines, gauges and buttons) to avoid
// flicker on the non-buffered QSPI panel. Also owns the on-screen button
// layout, so it answers "what did that tap hit?" (uiHitTest).
#pragma once
#include <stdint.h>
#include "dc_battle.h"
#include "dc_player.h"
#include "dc_input.h"

struct UiModel
{
  const char       *myName;
  const Player     *player;
  bool              autoMode;
  const PeerInfo   *peers;
  int               peerCount;
  uint32_t          selectedId;    // badge highlighted in the nearby list (0 = none)
  uint8_t           brightness;    // 1..BRIGHTNESS_LEVELS
  const BattleEngine *eng;
  uint32_t          now;

  const char *status;          // status line
  uint16_t    statusColor;

  // last round (battle screen)
  bool        haveLastRound;
  uint8_t     lastMyMove, lastOppMove;
  RoundResult lastRound;

  // result screen
  bool     showResult;
  int8_t   outcome;
  uint32_t oppId;
  XpAward  award;
  bool     cheatWin;
};

void uiBegin();
void uiSetBrightness(uint8_t level);                // 1..BRIGHTNESS_LEVELS
void uiRender(const UiModel &m);
bool uiHitTest(int16_t x, int16_t y, Command &c);   // tap -> command (false = nothing there)
void uiDebugDot(int16_t x, int16_t y);              // touchtest marker

// colors shared with the main sketch
#define UI_WHITE   0xFFFF
#define UI_GREY    0x8410
#define UI_GREEN   0x07E0
#define UI_YELLOW  0xFFE0
#define UI_RED     0xF800
#define UI_CYAN    0x07FF
#define UI_ORANGE  0xFD20
