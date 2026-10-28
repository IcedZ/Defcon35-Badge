// dc_storage.h - saves the player to flash (NVS) so progress survives power-off.
#pragma once
#include "dc_player.h"

bool storageLoad(Player &p);    // false if nothing saved (or old format)
void storageSave(const Player &p);
void storageErase();

// Display brightness level (1..BRIGHTNESS_LEVELS), kept separate from the
// player so a factory reset of the player doesn't change it.
uint8_t storageLoadBrightness(uint8_t fallback);
void    storageSaveBrightness(uint8_t level);
