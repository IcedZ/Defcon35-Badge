#include "dc_storage.h"
#include <Preferences.h>

static Preferences prefs;

bool storageLoad(Player &p)
{
  prefs.begin("dc35", true);
  size_t n = prefs.getBytesLength("player");
  bool ok = n == sizeof(Player) && prefs.getBytes("player", &p, sizeof(Player)) == sizeof(Player) &&
            p.saveVersion == PLAYER_SAVE_VERSION;
  prefs.end();
  return ok;
}

void storageSave(const Player &p)
{
  prefs.begin("dc35", false);
  prefs.putBytes("player", &p, sizeof(Player));
  prefs.end();
}

void storageErase()
{
  prefs.begin("dc35", false);
  prefs.remove("player");
  prefs.end();
}

uint8_t storageLoadBrightness(uint8_t fallback)
{
  prefs.begin("dc35", true);
  uint8_t v = prefs.getUChar("bright", fallback);
  prefs.end();
  return v;
}

void storageSaveBrightness(uint8_t level)
{
  prefs.begin("dc35", false);
  prefs.putUChar("bright", level);
  prefs.end();
}
