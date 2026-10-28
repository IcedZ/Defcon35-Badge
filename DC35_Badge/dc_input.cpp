#include "dc_input.h"
#include "dc_config.h"
#include "dc_rules.h"
#include <Arduino.h>

static char line[48];
static uint8_t len = 0;
static bool btnWasDown = false;
static bool btnLongFired = false;
static uint32_t btnChangedAt = 0;
#define LONG_PRESS_MS 1500

void inputBegin()
{
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
}

static bool is(const char *s, const char *a, const char *b = nullptr, const char *c = nullptr)
{
  return strcmp(s, a) == 0 || (b && strcmp(s, b) == 0) || (c && strcmp(s, c) == 0);
}

static Command parse(char *s)
{
  Command c = {CMD_UNKNOWN, 0};
  for (char *p = s; *p; p++) *p = tolower(*p);

  char *arg = strchr(s, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }

  if (is(s, "h", "help", "?"))                 c.type = CMD_HELP;
  else if (is(s, "l", "list", "ls"))           c.type = CMD_LIST;
  else if (is(s, "y", "yes", "accept"))        c.type = CMD_ACCEPT;
  else if (is(s, "n", "no", "decline"))        c.type = CMD_DECLINE;
  else if (is(s, "1", "strike"))               { c.type = CMD_MOVE; c.arg = MOVE_STRIKE; }
  else if (is(s, "2", "block", "guard"))       { c.type = CMD_MOVE; c.arg = MOVE_BLOCK; }
  else if (is(s, "3", "charge"))               { c.type = CMD_MOVE; c.arg = MOVE_CHARGE; }
  else if (is(s, "stats", "me"))               c.type = CMD_STATS;
  else if (is(s, "auto", "bot"))               c.type = CMD_AUTO;
  else if (is(s, "heal"))                      c.type = CMD_HEAL;
  else if (is(s, "factory-reset"))             c.type = CMD_RESET;
  else if (is(s, "touchtest"))                 c.type = CMD_TOUCHTEST;
  else if (is(s, "bright", "brightness", "dim"))
  {
    c.type = CMD_BRIGHT;
    if (!arg || !*arg) c.arg = -1;            // show current
    else if (*arg == '+') c.arg = 0;          // next level
    else c.arg = atoi(arg);                   // 1..5
  }
  else if (is(s, "c", "challenge", "fight") && arg && *arg)
  {
    c.type = CMD_CHALLENGE;
    if (strncmp(arg, "dc35-", 5) == 0) arg += 5;
    if (strlen(arg) >= 4) c.arg = CMD_ID_FLAG | (int32_t)(strtoul(arg, nullptr, 16) & 0xFFFFFF);
    else c.arg = atoi(arg);
  }
  return c;
}

bool inputPoll(Command &c)
{
  // BOOT button (active low): short press = CMD_BUTTON (on release),
  // hold 1.5 s = toggle auto-play (lets a second board run without Serial).
  bool down = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (down != btnWasDown && now - btnChangedAt > 40)
  {
    btnWasDown = down;
    btnChangedAt = now;
    if (down) btnLongFired = false;
    else if (!btnLongFired) { c = {CMD_BUTTON, 0}; return true; }
  }
  if (down && btnWasDown && !btnLongFired && now - btnChangedAt >= LONG_PRESS_MS)
  {
    btnLongFired = true;
    c = {CMD_AUTO, 0};
    return true;
  }

  while (Serial.available())
  {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n')
    {
      line[len] = 0;
      len = 0;
      char *s = line;
      while (*s == ' ') s++;
      if (!*s) continue;
      c = parse(s);
      return true;
    }
    if (len < sizeof(line) - 1) line[len++] = ch;
  }
  return false;
}
