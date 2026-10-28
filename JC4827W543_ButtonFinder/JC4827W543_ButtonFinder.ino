/*******************************************************************************
 * JC4827W543_ButtonFinder.ino - find which GPIO the SW1 button is wired to
 *
 * Watches every GPIO that isn't already used by the board (display, touch,
 * SD, audio, USB, serial, PSRAM) and reports any pin that changes state.
 * Pins are only ever read (INPUT_PULLUP) - nothing is driven.
 *
 * How to use:
 *   1. Upload, open Serial Monitor at 115200 (the screen shows the same info).
 *   2. Press BOOT first: "GPIO0" should be reported. That proves the test works.
 *   3. Press SW1 a few times. The pin that toggles each time is SW1.
 *
 * Board: Guition JC4827W543R. Tools settings as for the other sketches.
 ******************************************************************************/
#include <Arduino_GFX_Library.h>

#define TFT_BL 1
Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);

// GPIO0 (BOOT) is included as a known-good reference button.
// Excluded: 1 (backlight), 2/41/42 (audio amp), 3/10/11/12/13/38 (touch + SD),
//           19/20 (USB), 21/39/40/45/47/48 (display), 26-37 (flash/PSRAM),
//           43/44 (serial).
const uint8_t candidates[] = {0, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17, 18, 46};
const int N = sizeof(candidates) / sizeof(candidates[0]);

uint8_t  baseline[N];     // state at boot (idle level)
uint8_t  last[N];
uint16_t toggles[N];
int      lastChanged = -1;
uint32_t lastChangeAt = 0;

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREY   0x8410
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_NAVY   0x000F

void drawTable()
{
  gfx->setTextSize(2);
  for (int i = 0; i < N; i++)
  {
    int col = i % 3, row = i / 3;
    int16_t x = 10 + col * 156, y = 48 + row * 26;
    bool active = last[i] != baseline[i];
    uint16_t c = active ? C_GREEN : (toggles[i] ? C_YELLOW : C_GREY);
    gfx->setTextColor(c, C_BLACK);
    gfx->setCursor(x, y);
    gfx->printf("IO%-2u %s %3u ", candidates[i], last[i] ? "H" : "L", toggles[i]);
  }
}

void drawResult()
{
  gfx->fillRect(0, 196, 480, 76, C_BLACK);
  gfx->setTextColor(C_GREEN, C_BLACK);
  gfx->setTextSize(4);
  gfx->setCursor(10, 200);
  if (lastChanged < 0)
  {
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE, C_BLACK);
    gfx->print("Press BOOT, then SW1...");
    return;
  }
  gfx->printf("GPIO%u", candidates[lastChanged]);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE, C_BLACK);
  gfx->setCursor(10, 244);
  gfx->printf("idle %s, pressed %s, %u toggles",
              baseline[lastChanged] ? "HIGH" : "LOW",
              baseline[lastChanged] ? "LOW" : "HIGH", toggles[lastChanged]);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(C_BLACK);
  gfx->fillRect(0, 0, 480, 34, C_NAVY);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE, C_NAVY);
  gfx->setCursor(10, 9);
  gfx->print("SW1 button finder  (H/L, toggles)");

  for (int i = 0; i < N; i++) pinMode(candidates[i], INPUT_PULLUP);
  delay(50);

  Serial.println("\nSW1 button finder - idle levels:");
  for (int i = 0; i < N; i++)
  {
    baseline[i] = last[i] = digitalRead(candidates[i]);
    toggles[i] = 0;
    Serial.printf("  GPIO%-2u idle %s\n", candidates[i], baseline[i] ? "HIGH" : "LOW");
  }
  Serial.println("Press BOOT (should report GPIO0), then press SW1.\n");
  drawTable();
  drawResult();
}

void loop()
{
  bool changed = false;
  for (int i = 0; i < N; i++)
  {
    uint8_t v = digitalRead(candidates[i]);
    if (v == last[i]) continue;
    delay(15);                                   // debounce: confirm the change
    if (digitalRead(candidates[i]) != v) continue;
    last[i] = v;
    changed = true;
    if (v != baseline[i])                        // count presses, not releases
    {
      toggles[i]++;
      lastChanged = i;
      lastChangeAt = millis();
      Serial.printf(">>> GPIO%u went %s (press #%u)\n", candidates[i], v ? "HIGH" : "LOW", toggles[i]);
    }
    else
    {
      Serial.printf("    GPIO%u released\n", candidates[i]);
    }
  }
  if (changed)
  {
    drawTable();
    drawResult();
  }
  delay(2);
}
