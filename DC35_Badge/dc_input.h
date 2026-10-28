// dc_input.h - user input as abstract commands.
//
// Three input sources produce the same Command values:
//   - on-screen buttons (XPT2046 resistive touch -> dc_ui hit test)
//   - Serial commands (dev / testing)
//   - the BOOT button
// Physical Strike/Block/Charge buttons can be added here later; nothing
// above this layer needs to change.
#pragma once
#include <stdint.h>

enum CmdType : uint8_t
{
  CMD_NONE,
  CMD_HELP,
  CMD_LIST,
  CMD_CHALLENGE,   // arg: list index (1..) or badge ID (CMD_ID_FLAG | id)
  CMD_ACCEPT,
  CMD_DECLINE,
  CMD_MOVE,        // arg: Move
  CMD_STATS,
  CMD_AUTO,        // toggle auto-play (bot) mode
  CMD_HEAL,        // dev: refill HP
  CMD_RESET,       // dev: erase saved player
  CMD_BUTTON,      // BOOT button press / tap to continue
  CMD_SELECT,      // arg: badge ID tapped in the nearby list
  CMD_TOUCHTEST,   // dev: show tap coordinates
  CMD_BRIGHT,      // arg: 1..5 set level, 0 = next level (cycles), -1 = show current
  CMD_UNKNOWN,
};

#define CMD_ID_FLAG 0x01000000

struct Command
{
  uint8_t type;
  int32_t arg;
};

void inputBegin();
bool inputPoll(Command &c);
