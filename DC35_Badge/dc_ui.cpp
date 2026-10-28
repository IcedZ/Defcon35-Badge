#include "dc_ui.h"
#include "dc_config.h"
#include "dc_radio.h"
#include <Arduino_GFX_Library.h>
#include <stdarg.h>

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
static Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0 /* rotation */, true /* IPS */);

#define BG        0x0000
#define DKGREY    0x2104
#define MIDGREY   0x4208
#define NAVY      0x000F
#define DKRED     0x7800
#define DKGREEN   0x0320
#define BTN_BLUE  0x0230
#define HEART_ON  0xF800
#define HEART_OFF 0x3186

enum Screen { SCR_NONE, SCR_HOME, SCR_CHALLENGE, SCR_BATTLE, SCR_RESULT };
static Screen screen = SCR_NONE;

// ---------------------------------------------------------------------------
// cached text lines: a line is only redrawn when its text or color changes
// ---------------------------------------------------------------------------
#define MAX_SLOTS 24
struct Slot { char text[41]; uint16_t fg; };
static Slot slots[MAX_SLOTS];

static void line(int slot, int16_t x, int16_t y, uint16_t fg, uint16_t bg, int width, const char *fmt, ...)
{
  char buf[41];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (width > 40) width = 40;

  char padded[41];
  snprintf(padded, sizeof(padded), "%-*.*s", width, width, buf);   // fixed width erases old text
  Slot &s = slots[slot];
  if (s.fg == fg && strcmp(s.text, padded) == 0) return;
  strcpy(s.text, padded);
  s.fg = fg;

  gfx->setTextSize(2);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x, y);
  gfx->print(padded);
}

// ---------------------------------------------------------------------------
// on-screen buttons: registered every frame (for hit testing), drawn only
// when their look changes
// ---------------------------------------------------------------------------
#define MAX_BUTTONS 12
struct Button
{
  int16_t x, y, w, h;
  uint8_t cmd;
  int32_t arg;
  bool    enabled;
  // last drawn look
  char     label[16];
  uint16_t color;
  bool     drawnEnabled;
  bool     drawn;
};
static Button buttons[MAX_BUTTONS];
static int    buttonCount = 0;

// nearby-badge grid state (drawn further down, but needed by the hit test)
#define GRID_COLS   4
#define GRID_ROWS   2
#define GRID_TILES  (GRID_COLS * GRID_ROWS)
#define GRID_X      8
#define GRID_Y      110
#define TILE_W      112
#define TILE_H      44
#define TILE_GAP    4
#define TILE_BG     0x18E3
#define UI_CMD_PAGE 0xF0      // UI-internal: "MORE" tile flips the page

enum TileKind : uint8_t { TILE_EMPTY, TILE_PEER, TILE_MORE };
struct TileCache { bool valid; uint8_t kind; uint32_t id; uint8_t level, range, state; bool sel; int more; };
static TileCache tiles[GRID_TILES];
static int gridPage = 0, gridPeerCount = 0;
static void invalidateTiles() { for (auto &tc : tiles) tc.valid = false; }

static void invalidate()
{
  for (auto &s : slots) { s.text[0] = 1; s.text[1] = 0; s.fg = 0; }   // never matches real text
  for (auto &b : buttons) b.drawn = false;
  invalidateTiles();
}

// Visible button. Disabled buttons are drawn grey and ignore taps.
static void button(int idx, int16_t x, int16_t y, int16_t w, int16_t h, const char *label,
                   uint16_t color, bool enabled, uint8_t cmd, int32_t arg)
{
  Button &b = buttons[idx];
  if (idx >= buttonCount) buttonCount = idx + 1;
  b.x = x; b.y = y; b.w = w; b.h = h; b.cmd = cmd; b.arg = arg; b.enabled = enabled;

  if (b.drawn && b.color == color && b.drawnEnabled == enabled && strcmp(b.label, label) == 0) return;
  strlcpy(b.label, label, sizeof(b.label));
  b.color = color;
  b.drawnEnabled = enabled;
  b.drawn = true;

  uint16_t fill = enabled ? color : DKGREY;
  gfx->fillRoundRect(x, y, w, h, 8, fill);
  gfx->drawRoundRect(x, y, w, h, 8, enabled ? UI_WHITE : MIDGREY);
  int size = (int)strlen(label) * 18 <= w - 12 ? 3 : 2;
  int tw = strlen(label) * 6 * size, th = 8 * size;
  gfx->setTextSize(size);
  gfx->setTextColor(enabled ? UI_WHITE : UI_GREY, fill);
  gfx->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  gfx->print(label);
}

// Invisible tap area (list rows, "tap anywhere").
static void hotspot(int idx, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t cmd, int32_t arg, bool enabled = true)
{
  Button &b = buttons[idx];
  if (idx >= buttonCount) buttonCount = idx + 1;
  b.x = x; b.y = y; b.w = w; b.h = h; b.cmd = cmd; b.arg = arg; b.enabled = enabled;
  b.drawn = true;   // nothing to draw
  b.label[0] = 0;
}

bool uiHitTest(int16_t x, int16_t y, Command &c)
{
  for (int i = 0; i < buttonCount; i++)
  {
    Button &b = buttons[i];
    if (!b.enabled || b.cmd == CMD_NONE) continue;
    if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
    if (b.cmd == UI_CMD_PAGE)   // "MORE" tile: next page of the grid, handled here
    {
      gridPage++;
      if (gridPage * (GRID_TILES - 1) >= gridPeerCount) gridPage = 0;
      invalidateTiles();
      return false;
    }
    c.type = b.cmd;
    c.arg = b.arg;
    if (b.label[0])   // flash visible buttons as feedback; redrawn on next frame
    {
      gfx->fillRoundRect(b.x, b.y, b.w, b.h, 8, UI_WHITE);
      b.drawn = false;
    }
    return true;
  }
  return false;
}

void uiDebugDot(int16_t x, int16_t y)
{
  gfx->drawFastHLine(x - 8, y, 17, UI_RED);
  gfx->drawFastVLine(x, y - 8, 17, UI_RED);
  gfx->fillCircle(x, y, 2, UI_YELLOW);
}

// ---------------------------------------------------------------------------
// nearby-badge grid: 4 x 2 tiles, each with a unique identicon
// ---------------------------------------------------------------------------

static uint32_t mix32(uint32_t x)
{
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}

// 5x5 mirrored pixel pattern + color, both derived from the badge ID, so
// every badge gets its own recognizable icon (same on every screen).
static void identicon(int16_t x, int16_t y, int cell, uint32_t id, bool dim)
{
  static const uint16_t pal[] = {0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0x041F,
                                 0x781F, 0xF81F, 0xFB56, 0x5FE0, 0x2D7F, 0xFC08};
  uint32_t h = mix32(id ^ 0x9E3779B9);
  if ((h & 0x7FFF) == 0) h |= 0x2492;                 // never an empty icon
  uint16_t c = dim ? MIDGREY : pal[(h >> 20) % 12];
  gfx->fillRect(x, y, 5 * cell, 5 * cell, BG);
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 3; col++)
      if ((h >> (r * 3 + col)) & 1)
      {
        gfx->fillRect(x + col * cell, y + r * cell, cell, cell, c);
        gfx->fillRect(x + (4 - col) * cell, y + r * cell, cell, cell, c);
      }
}

static void tileXY(int i, int16_t &x, int16_t &y)
{
  x = GRID_X + (i % GRID_COLS) * (TILE_W + TILE_GAP);
  y = GRID_Y + (i / GRID_COLS) * (TILE_H + TILE_GAP);
}

static void drawTile(int i, const PeerInfo *q, bool sel, uint8_t kind, int more)
{
  uint8_t range = 0, state = 0, level = 0;
  uint32_t id = 0;
  if (kind == TILE_PEER)
  {
    id = q->id;
    level = q->f.level;
    range = q->rssiAvg >= RSSI_CLOSE ? 0 : (q->rssiAvg >= RSSI_NEAR ? 1 : 2);
    state = (q->f.flags & FLAG_KO) ? 2 : ((q->f.flags & FLAG_BUSY) ? 1 : 0);
  }
  TileCache &tc = tiles[i];
  if (tc.valid && tc.kind == kind && tc.id == id && tc.level == level && tc.range == range &&
      tc.state == state && tc.sel == sel && tc.more == more) return;
  tc = {true, kind, id, level, range, state, sel, more};

  int16_t x, y;
  tileXY(i, x, y);
  gfx->fillRect(x, y, TILE_W, TILE_H, BG);
  if (kind == TILE_EMPTY) return;

  if (kind == TILE_MORE)
  {
    gfx->fillRoundRect(x, y, TILE_W, TILE_H, 6, TILE_BG);
    gfx->drawRoundRect(x, y, TILE_W, TILE_H, 6, UI_GREY);
    gfx->setTextSize(2);
    gfx->setTextColor(UI_WHITE, TILE_BG);
    gfx->setCursor(x + 32, y + 6);
    gfx->print("MORE");
    gfx->setTextSize(1);
    gfx->setTextColor(UI_GREY, TILE_BG);
    gfx->setCursor(x + 34, y + 28);
    gfx->printf("+%d badges", more);
    return;
  }

  bool dim = state != 0;
  static const uint16_t rangeCol[3] = {UI_GREEN, UI_YELLOW, UI_GREY};
  uint16_t border = sel ? UI_WHITE : (dim ? MIDGREY : rangeCol[range]);
  gfx->fillRoundRect(x, y, TILE_W, TILE_H, 6, TILE_BG);
  gfx->drawRoundRect(x, y, TILE_W, TILE_H, 6, border);
  if (sel)
  {
    gfx->drawRoundRect(x + 1, y + 1, TILE_W - 2, TILE_H - 2, 5, border);
    gfx->drawRoundRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, 4, border);
  }
  identicon(x + 5, y + 9, 5, id, dim);   // 25x25, vertically centred

  char hex[8];
  snprintf(hex, sizeof(hex), "%06lX", (unsigned long)(id & 0xFFFFFF));
  gfx->setTextSize(2);
  gfx->setTextColor(dim ? UI_GREY : UI_WHITE, TILE_BG);
  gfx->setCursor(x + 35, y + 7);
  gfx->print(hex);

  static const char *rangeTxt[3] = {"CLOSE", "NEAR", "FAR"};
  gfx->setTextSize(1);
  gfx->setTextColor(dim ? UI_GREY : rangeCol[range], TILE_BG);
  gfx->setCursor(x + 35, y + 29);
  gfx->printf("LV%-2u %s", level, state == 2 ? "KO" : (state == 1 ? "BUSY" : rangeTxt[range]));
}

// ---------------------------------------------------------------------------
// gauges
// ---------------------------------------------------------------------------
// Heart from two circles + a triangle; half = left half only.
static void heart(int16_t x, int16_t y, int r, uint16_t c, bool half)
{
  gfx->fillCircle(x + r, y + r, r, c);
  if (half)
  {
    gfx->fillTriangle(x, y + r + 1, x + 2 * r, y + r + 1, x + 2 * r, y + 4 * r, c);
  }
  else
  {
    gfx->fillCircle(x + 3 * r, y + r, r, c);
    gfx->fillTriangle(x, y + r + 1, x + 4 * r, y + r + 1, x + 2 * r, y + 4 * r, c);
  }
}

static int cacheHp = -1, cacheMaxHp = -1;
static void hearts(int16_t x, int16_t y, int hp, int maxHp)
{
  if (hp == cacheHp && maxHp == cacheMaxHp) return;
  cacheHp = hp; cacheMaxHp = maxHp;
  // 1 HP per heart. Max HP is small (2..8), so hearts are big.
  const int r = 5, pitch = 4 * r + 5;
  gfx->fillRect(x, y, 12 * pitch, 4 * r + 1, BG);   // stops before the HP text
  for (int i = 0; i < maxHp && i < 12; i++)
  {
    int16_t hx = x + i * pitch;
    heart(hx, y, r, i < hp ? HEART_ON : HEART_OFF, false);
  }
}

static void bar(int16_t x, int16_t y, int16_t w, int16_t h, int val, int maxVal, uint16_t c)
{
  int fill = maxVal > 0 ? (int)((long)w * (val < 0 ? 0 : val) / maxVal) : 0;
  if (fill > w) fill = w;
  gfx->fillRect(x, y, fill, h, c);
  gfx->fillRect(x + fill, y, w - fill, h, DKGREY);
}

static uint16_t hpColor(int hp, int maxHp)
{
  if (hp * 3 > maxHp * 2) return UI_GREEN;
  if (hp * 3 > maxHp) return UI_YELLOW;
  return UI_RED;
}

static void header(uint16_t color)
{
  gfx->fillRect(0, 0, SCREEN_W, 30, color);
}

static void setScreen(Screen s)
{
  if (screen == s) return;
  screen = s;
  gfx->fillScreen(BG);
  invalidate();
  cacheHp = cacheMaxHp = -1;
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
//  HOME   0 header | 36 hearts | 62 XP | 90 nearby grid (4 x 2 tiles, tap to select)
//         206 status | 226 [CHALLENGE] [AUTO] [BRIGHT n]
static void drawHome(const UiModel &m)
{
  static int lastXp = -1, lastXpNext = -1;
  bool fresh = screen != SCR_HOME;
  setScreen(SCR_HOME);
  const Player &p = *m.player;
  uint8_t maxHp = playerMaxHp(p);

  if (fresh)
  {
    header(NAVY);
    gfx->drawFastHLine(10, 86, SCREEN_W - 20, DKGREY);
    lastXp = lastXpNext = -1;
  }

  line(0, 10, 7, UI_WHITE, NAVY, 26, "%s %s LV%u", m.myName, className(p.cls), p.level);
  {
    char wl[32];
    snprintf(wl, sizeof(wl), "W%u L%u D%u", p.wins, p.losses, p.draws);
    line(1, 330, 7, UI_WHITE, NAVY, 12, "%12s", wl);
  }

  hearts(10, 36, p.hp, maxHp);
  line(2, 330, 38, p.ko ? UI_RED : hpColor(p.hp, maxHp), BG, 12, "%s %u/%u", p.ko ? "KO" : "HP", p.hp, maxHp);

  uint32_t next = playerXpToNext(p);
  if (p.level >= MAX_LEVEL) line(3, 10, 62, UI_CYAN, BG, 12, "XP MAX");
  else line(3, 10, 62, UI_CYAN, BG, 12, "XP %lu/%lu", (unsigned long)p.xp, (unsigned long)next);
  if ((int)p.xp != lastXp || (int)next != lastXpNext)
  {
    bar(170, 64, 300, 12, p.level >= MAX_LEVEL ? 1 : p.xp, p.level >= MAX_LEVEL ? 1 : next, UI_CYAN);
    lastXp = p.xp; lastXpNext = next;
  }

  line(4, 10, 90, UI_CYAN, BG, 39, "NEARBY (%d)  tap one, then CHALLENGE", m.peerCount);

  // Stable order (first seen) so tiles don't jump around as signal strength
  // fluctuates; the border color shows range instead.
  const PeerInfo *order[MAX_PEERS];
  int n = 0;
  bool selectedVisible = false;
  for (int i = 0; i < m.peerCount && n < MAX_PEERS; i++)
  {
    order[n++] = &m.peers[i];
    if (m.peers[i].id == m.selectedId) selectedVisible = true;
  }
  for (int i = 1; i < n; i++)
  {
    const PeerInfo *k = order[i];
    int j = i - 1;
    while (j >= 0 && (order[j]->firstSeen > k->firstSeen ||
                      (order[j]->firstSeen == k->firstSeen && order[j]->id > k->id)))
    {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = k;
  }

  // Empty grid shows a message; when the first badge appears, wipe it and
  // force every tile to redraw over that area.
  static bool showingEmpty = false;
  if (fresh) showingEmpty = false;   // screen was just cleared
  if (n == 0 && !showingEmpty)
  {
    gfx->fillRect(GRID_X, GRID_Y, GRID_COLS * (TILE_W + TILE_GAP), GRID_ROWS * (TILE_H + TILE_GAP), BG);
    for (auto &tc : tiles) tc = {true, TILE_EMPTY, 0, 0, 0, 0, false, 0};   // already blank: don't redraw over the message
    gfx->setTextSize(2);
    gfx->setTextColor(UI_GREY, BG);
    gfx->setCursor(GRID_X + 124, GRID_Y + 40);
    gfx->print("No badges in range");
    showingEmpty = true;
  }
  else if (n > 0 && showingEmpty)
  {
    gfx->fillRect(GRID_X, GRID_Y, GRID_COLS * (TILE_W + TILE_GAP), GRID_ROWS * (TILE_H + TILE_GAP), BG);
    invalidateTiles();
    showingEmpty = false;
  }

  // Up to 8 tiles; with more badges, the last tile is "MORE" and pages through.
  gridPeerCount = n;
  bool paged = n > GRID_TILES;
  int perPage = paged ? GRID_TILES - 1 : GRID_TILES;
  if (gridPage * perPage >= n) gridPage = 0;
  int start = gridPage * perPage;
  for (int i = 0; i < GRID_TILES; i++)
  {
    int16_t x, y;
    tileXY(i, x, y);
    int idx = start + i;
    if (paged && i == GRID_TILES - 1)
    {
      drawTile(i, nullptr, false, TILE_MORE, n - perPage);
      hotspot(4 + i, x, y, TILE_W, TILE_H, UI_CMD_PAGE, 0);
    }
    else if (i < perPage && idx < n)
    {
      drawTile(i, order[idx], order[idx]->id == m.selectedId, TILE_PEER, 0);
      hotspot(4 + i, x, y, TILE_W, TILE_H, CMD_SELECT, (int32_t)order[idx]->id);
    }
    else
    {
      drawTile(i, nullptr, false, TILE_EMPTY, 0);
      hotspot(4 + i, x, y, TILE_W, TILE_H, CMD_NONE, 0, false);
    }
  }

  line(10, 10, 206, m.statusColor, BG, 39, "%s", m.status);

  bool canChallenge = selectedVisible && !m.eng->busy() && playerCanBattle(p);
  button(0, 8, 226, 222, 42, "CHALLENGE", DKRED, canChallenge, CMD_CHALLENGE, CMD_ID_FLAG | (int32_t)m.selectedId);
  button(1, 238, 226, 112, 42, m.autoMode ? "AUTO:ON" : "AUTO", m.autoMode ? DKGREEN : BTN_BLUE, true, CMD_AUTO, 0);
  char bl[16];
  snprintf(bl, sizeof(bl), "BRIGHT %u", m.brightness);
  button(2, 358, 226, 114, 42, bl, MIDGREY, true, CMD_BRIGHT, 0);   // tap = next level
}

//  CHALLENGE   who's asking, countdown, [ACCEPT] [DECLINE]
static void drawChallenge(const UiModel &m)
{
  bool fresh = screen != SCR_CHALLENGE;
  setScreen(SCR_CHALLENGE);
  if (fresh) header(DKRED);

  const BattleEngine &e = *m.eng;
  const PeerInfo *q = nullptr;
  for (int i = 0; i < m.peerCount; i++) if (m.peers[i].id == e.opponent()) q = &m.peers[i];
  char nm[16];
  badgeName(e.opponent(), nm);
  int secs = (int)((int32_t)(e.deadline() - m.now) / 1000);
  if (secs < 0) secs = 0;

  line(0, 10, 7, UI_WHITE, DKRED, 39, "CHALLENGE!");
  line(1, 10, 46, UI_YELLOW, BG, 39, "%s wants to battle", nm);
  if (q) line(2, 10, 72, UI_WHITE, BG, 39, "%s  Level %u  HP %u/%u", className(q->f.cls), q->f.level, q->f.hp,
              levelMaxHp(q->f.level));
  line(3, 10, 110, UI_GREY, BG, 39, "Battles are optional.");
  line(4, 10, 140, UI_ORANGE, BG, 39, "Auto-decline in %ds", secs);

  button(0, 10, 176, 225, 74, "ACCEPT", DKGREEN, true, CMD_ACCEPT, 0);
  button(1, 245, 176, 225, 74, "DECLINE", DKRED, true, CMD_DECLINE, 0);
}

//  BATTLE   both fighters, last round, prompt, [STRIKE] [BLOCK] [CHARGE]
static void drawBattle(const UiModel &m)
{
  static int cMe = -1, cOpp = -1, cMeterMe = -1, cMeterOpp = -1;
  bool fresh = screen != SCR_BATTLE;
  setScreen(SCR_BATTLE);
  if (fresh)
  {
    header(DKRED);
    gfx->drawFastHLine(10, 128, SCREEN_W - 20, DKGREY);
    cMe = cOpp = cMeterMe = cMeterOpp = -1;
  }

  const BattleEngine &e = *m.eng;
  const Fighter &me = e.me(), &op = e.them();
  char nm[16];
  badgeName(e.opponent(), nm);

  line(0, 10, 7, UI_WHITE, DKRED, 26, "BATTLE vs %s", nm);
  line(1, 340, 7, UI_WHITE, DKRED, 11, "ROUND %u/%u", e.round(), MAX_ROUNDS);

  line(2, 10, 40, UI_CYAN, BG, 19, "YOU %s L%u", className(me.cls), me.level);
  line(3, 250, 40, UI_ORANGE, BG, 19, "%s L%u", className(op.cls), op.level);

  int maxMe = levelMaxHp(me.level), maxOp = levelMaxHp(op.level);
  if (me.hp != cMe) { bar(10, 62, 220, 14, me.hp, maxMe, hpColor(me.hp, maxMe)); cMe = me.hp; }
  if (op.hp != cOpp) { bar(250, 62, 220, 14, op.hp, maxOp, hpColor(op.hp, maxOp)); cOpp = op.hp; }
  line(4, 10, 82, UI_WHITE, BG, 19, "HP %d/%d", me.hp, maxMe);
  line(5, 250, 82, UI_WHITE, BG, 19, "HP %d/%d", op.hp, maxOp);

  line(6, 10, 104, UI_GREY, BG, 6, "METER");
  line(7, 250, 104, UI_GREY, BG, 6, "METER");
  if (me.meter != cMeterMe || op.meter != cMeterOpp)
  {
    for (int i = 0; i < METER_MAX; i++)
    {
      gfx->fillRect(84 + i * 22, 105, 16, 12, i < me.meter ? UI_YELLOW : DKGREY);
      gfx->fillRect(324 + i * 22, 105, 16, 12, i < op.meter ? UI_YELLOW : DKGREY);
    }
    cMeterMe = me.meter; cMeterOpp = op.meter;
  }

  if (m.haveLastRound)
  {
    const RoundResult &r = m.lastRound;
    uint16_t c = r.winner > 0 ? UI_GREEN : (r.winner < 0 ? UI_RED : UI_YELLOW);
    line(8, 10, 136, UI_WHITE, BG, 39, "Last: you %s  -  them %s", moveName(m.lastMyMove), moveName(m.lastOppMove));
    line(9, 10, 158, c, BG, 39, "%s  (you -%d, them -%d)", effectText(r.effect), r.dmgToA, r.dmgToB);
  }
  else
  {
    line(8, 10, 136, UI_GREY, BG, 39, "Strike > Charge > Block > Strike");
    line(9, 10, 158, UI_GREY, BG, 39, "Full meter: Strike is unblockable");
  }

  int secs = (int)((int32_t)(e.deadline() - m.now) / 1000);
  if (secs < 0) secs = 0;
  bool choosing = e.needsMove();
  if (choosing)                       line(10, 10, 182, UI_YELLOW, BG, 39, "Choose your move!  (random in %ds)", secs);
  else if (e.phase() == Phase::PICK)  line(10, 10, 182, UI_GREEN, BG, 39, "Locked in - waiting for opponent...");
  else if (e.phase() == Phase::DONE)  line(10, 10, 182, UI_WHITE, BG, 39, "Battle over");
  else                                line(10, 10, 182, UI_GREEN, BG, 39, "Revealing moves...");

  // Move buttons: active while choosing; afterwards the chosen one stays lit.
  uint8_t chosen = e.chosenMove();
  const char *labels[3] = {"STRIKE", "BLOCK", "CHARGE"};
  const uint16_t colors[3] = {DKRED, BTN_BLUE, 0x6010 /* purple */};
  for (int i = 0; i < 3; i++)
  {
    uint8_t mv = MOVE_STRIKE + i;
    bool lit = choosing || chosen == mv;
    button(i, 8 + i * 157, 206, 150, 60, labels[i], colors[i], lit, choosing ? CMD_MOVE : CMD_NONE, mv);
  }
}

//  RESULT   tap anywhere to continue
static void drawResult(const UiModel &m)
{
  bool fresh = screen != SCR_RESULT;
  setScreen(SCR_RESULT);
  const Player &p = *m.player;

  if (fresh)
  {
    const char *word = m.outcome > 0 ? "VICTORY" : (m.outcome < 0 ? "DEFEAT" : "DRAW");
    uint16_t c = m.outcome > 0 ? UI_GREEN : (m.outcome < 0 ? UI_RED : UI_YELLOW);
    gfx->setTextSize(5);
    gfx->setTextColor(c, BG);
    int w = strlen(word) * 30;
    gfx->setCursor((SCREEN_W - w) / 2, 30);
    gfx->print(word);
  }

  char nm[16];
  badgeName(m.oppId, nm);
  line(0, 10, 100, UI_WHITE, BG, 39, "vs %s", nm);
  if (m.cheatWin) line(1, 10, 126, UI_ORANGE, BG, 39, "Opponent cheated - they forfeit");
  else line(1, 10, 126, UI_GREY, BG, 39, "");
  if (m.award.repeatOpponent) line(2, 10, 152, UI_GREY, BG, 39, "No XP - fought them recently");
  else if (m.award.percent > 100)
    line(2, 10, 152, UI_GREEN, BG, 39, "+%lu XP  (%u%% - stronger foe!)", (unsigned long)m.award.gained, m.award.percent);
  else if (m.award.percent < 100)
    line(2, 10, 152, UI_ORANGE, BG, 39, "+%lu XP  (%u%% - weaker foe)", (unsigned long)m.award.gained, m.award.percent);
  else
    line(2, 10, 152, UI_CYAN, BG, 39, "+%lu XP", (unsigned long)m.award.gained);
  if (m.award.leveledUp) line(3, 10, 178, UI_YELLOW, BG, 39, "LEVEL UP! Now level %u", p.level);
  else line(3, 10, 178, UI_GREY, BG, 39, "");
  line(4, 10, 204, UI_WHITE, BG, 39, "HP %u/%u%s", p.hp, playerMaxHp(p), p.ko ? "  KNOCKED OUT" : "");
  line(5, 10, 248, UI_GREY, BG, 39, "Tap anywhere to continue");
  hotspot(0, 0, 0, SCREEN_W, SCREEN_H, CMD_BUTTON, 0);
}

// ---------------------------------------------------------------------------
void uiSetBrightness(uint8_t level)
{
  static const uint8_t duty[] = BACKLIGHT_DUTY;
  if (level < 1) level = 1;
  if (level > BRIGHTNESS_LEVELS) level = BRIGHTNESS_LEVELS;
  ledcWrite(PIN_TFT_BL, duty[level - 1]);
}

void uiBegin()
{
  ledcAttach(PIN_TFT_BL, BACKLIGHT_PWM_HZ, 8);   // PWM backlight (8-bit duty)
  uiSetBrightness(BRIGHTNESS_DEFAULT);
  gfx->begin();
  gfx->fillScreen(BG);
}

void uiRender(const UiModel &m)
{
  // Buttons are re-registered every frame; a screen change clears them.
  Screen before = screen;
  Phase ph = m.eng->phase();
  Screen want = ph == Phase::CHALLENGED                       ? SCR_CHALLENGE
              : (m.eng->inBattle() && ph != Phase::DONE)      ? SCR_BATTLE
              : m.showResult                                  ? SCR_RESULT
                                                              : SCR_HOME;
  if (want != before)
  {
    for (auto &b : buttons) { b.cmd = CMD_NONE; b.enabled = false; }
    buttonCount = 0;
  }

  switch (want)
  {
    case SCR_CHALLENGE: drawChallenge(m); break;
    case SCR_BATTLE:    drawBattle(m); break;
    case SCR_RESULT:    drawResult(m); break;
    default:            drawHome(m); break;
  }
}
