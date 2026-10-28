#include "dc_protocol.h"
#include <string.h>

static void put24(uint8_t *p, uint32_t v) { p[0] = v >> 16; p[1] = v >> 8; p[2] = v; }
static uint32_t get24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

void encodeFrame(const AdvFrame &f, uint8_t out[FRAME_LEN])
{
  out[0] = FRAME_MAGIC;
  out[1] = PROTO_VERSION;
  put24(out + 2, f.id);
  out[5] = (f.cls & 0x0F) | (uint8_t)(f.flags << 4);
  out[6] = f.level;
  out[7] = f.hp;
  out[8] = f.state;
  put24(out + 9, f.opp);
  out[12] = f.session & 0xFF;
  out[13] = f.session >> 8;
  out[14] = f.round;
  memcpy(out + 15, f.data, FRAME_DATA_LEN);
  out[23] = 0;
}

bool decodeFrame(const uint8_t *in, size_t len, AdvFrame &f)
{
  if (len < FRAME_LEN || in[0] != FRAME_MAGIC || in[1] != PROTO_VERSION) return false;
  f.version = in[1];
  f.id      = get24(in + 2);
  f.cls     = in[5] & 0x0F;
  f.flags   = in[5] >> 4;
  f.level   = in[6];
  f.hp      = in[7];
  f.state   = in[8];
  f.opp     = get24(in + 9);
  f.session = (uint16_t)in[12] | ((uint16_t)in[13] << 8);
  f.round   = in[14];
  memcpy(f.data, in + 15, FRAME_DATA_LEN);
  return f.id != 0;
}
