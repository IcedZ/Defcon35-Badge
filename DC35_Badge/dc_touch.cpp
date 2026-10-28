#include "dc_touch.h"
#include "dc_config.h"
#include <Arduino.h>
#include <SPI.h>

// Touch + microSD share this bus. It must NOT be the default SPI object:
// sharing that with the display's QSPI host breaks touch/SD after a flush.
static SPIClass bus2(HSPI);

static bool     down = false;
static uint8_t  pressCount = 0, releaseCount = 0;
static uint32_t lastPoll = 0;

// One 12-bit conversion: send the control byte, clock out the result.
static int channel(uint8_t ctrl)
{
  bus2.transfer(ctrl);
  uint8_t hi = bus2.transfer(0), lo = bus2.transfer(0);
  return ((hi << 8) | lo) >> 3;
}

// Returns pressure; fills raw x/y when pressed firmly enough.
static int readRaw(int &rx, int &ry)
{
  bus2.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TOUCH_CS, LOW);
  int z1 = channel(0xB1);
  int z2 = channel(0xC1);
  int z = z1 + (4095 - z2);
  if (z >= TOUCH_PRESSURE_MIN)
  {
    channel(0xD1);                                // first sample is noisy
    rx = (channel(0xD1) + channel(0xD1)) / 2;     // X
    ry = (channel(0x91) + channel(0x91)) / 2;     // Y
  }
  channel(0x90);                                  // power down, pen IRQ enabled
  digitalWrite(PIN_TOUCH_CS, HIGH);
  bus2.endTransaction();
  return z;
}

void touchBegin()
{
  // Deselect both devices on the shared bus before anything talks on it.
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  bus2.begin(PIN_SPI2_SCK, PIN_SPI2_MISO, PIN_SPI2_MOSI, -1);
}

static int16_t mapAxis(int raw, int lo, int hi, int size, bool mirror)
{
  long v = (long)(raw - lo) * (size - 1) / (hi - lo);
  if (mirror) v = size - 1 - v;
  return (int16_t)constrain(v, 0, size - 1);
}

bool touchPollTap(int16_t &x, int16_t &y, int16_t *rawX, int16_t *rawY)
{
  uint32_t now = millis();
  if (now - lastPoll < 15) return false;
  lastPoll = now;

  int rx = 0, ry = 0;
  bool pressed = readRaw(rx, ry) >= TOUCH_PRESSURE_MIN;

  if (!pressed)
  {
    pressCount = 0;
    if (down && ++releaseCount >= 3) down = false;   // ~45 ms of no pressure = released
    return false;
  }
  releaseCount = 0;
  if (down) return false;                // still the same press
  if (++pressCount < 2) return false;    // need 2 firm samples in a row (debounce)

  down = true;
  if (rawX) *rawX = rx;
  if (rawY) *rawY = ry;
#if TOUCH_SWAP_XY
  int t = rx; rx = ry; ry = t;
#endif
  x = mapAxis(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, SCREEN_W, TOUCH_MIRROR_X);
  y = mapAxis(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, SCREEN_H, TOUCH_MIRROR_Y);
  return true;
}
