// dc_radio.h - BLE advertising + scanning (device only).
// Advertises this badge's AdvFrame and keeps a table of nearby badges.
#pragma once
#include <stdint.h>
#include "dc_battle.h"

uint32_t radioBegin(const char *name);                // returns 24-bit badge ID (from BT MAC)
uint32_t radioBadgeId();                              // ID without starting the radio
void     radioSetFrame(const AdvFrame &f, bool fast); // fast = battle advertising interval
int      radioSnapshot(PeerInfo *out, int max, uint32_t now);   // live peers, strongest first
void     badgeName(uint32_t id, char out[16]);        // "DC35-XXXXXX"
