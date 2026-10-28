// dc_protocol.h - the over-the-air frame every badge advertises.
//
// Badges never connect to each other. Each badge continuously advertises one
// 24-byte frame describing who it is and, during a battle, where it is in the
// battle. Every badge listens to everyone else's frames. Because each frame
// is a full snapshot of state (not a one-off message), a lost packet is
// harmless - the next one carries the same information.
//
// Legacy BLE advertising payload (31 bytes max):
//   Flags AD (3) + Manufacturer AD (2 + company 2 + frame 24) = 31
//
// Frame layout (24 bytes):
//   [0]      magic 0xDC
//   [1]      protocol version
//   [2..4]   badge ID (24 bit, from the Bluetooth MAC)
//   [5]      class (low nibble) | flags (high nibble)
//   [6]      level
//   [7]      HP
//   [8]      battle wire state (WireState)
//   [9..11]  opponent badge ID (0 when idle)
//   [12..13] battle session ID (little endian)
//   [14]     round number
//   [15..22] 8 data bytes (meaning depends on state - see dc_battle.cpp)
//   [23]     reserved (0)
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MFG_COMPANY_ID    0xFFFF   // Bluetooth SIG ID reserved for testing
#define FRAME_MAGIC       0xDC
#define PROTO_VERSION     2
#define FRAME_LEN         24
#define FRAME_DATA_LEN    8

enum WireState : uint8_t
{
  W_IDLE      = 0,
  W_CHALLENGE = 1,   // data: challenger nonce (4 bytes)
  W_PICK      = 2,   // round r>=1, choosing a move. data: nonce (r==1) or my reveal of round r-1
  W_COMMIT    = 3,   // data: 8-byte commitment to my move
  W_REVEAL    = 4,   // data: my move (1) + salt (7)
  W_DONE      = 5,   // battle finished. data: my reveal of the final round
  W_DECLINE   = 6,   // challenge declined
};

enum FrameFlags : uint8_t
{
  FLAG_BUSY = 0x1,   // in a battle or answering a challenge
  FLAG_KO   = 0x2,   // knocked out - can't battle
  FLAG_BOT  = 0x4,   // auto-play mode (testing)
  FLAG_ACK  = 0x8,   // in COMMIT: "I have your commitment for this round"
};

struct AdvFrame
{
  uint8_t  version;
  uint32_t id;        // 24-bit
  uint8_t  cls;
  uint8_t  flags;
  uint8_t  level;
  uint8_t  hp;
  uint8_t  state;     // WireState
  uint32_t opp;       // 24-bit
  uint16_t session;
  uint8_t  round;
  uint8_t  data[FRAME_DATA_LEN];
};

void encodeFrame(const AdvFrame &f, uint8_t out[FRAME_LEN]);
bool decodeFrame(const uint8_t *in, size_t len, AdvFrame &f);   // false if not a DC35 frame
