/*******************************************************************************
 * JC4827W543_HelloWorld.ino
 *
 * Hello World + display self-test for the Guition JC4827W543 (N/R/C)
 * 4.3" 480x272 IPS TFT, NV3041A controller over QSPI
 * MCU: ESP32-S3 (N4R8: 4 MB flash, 8 MB OPI PSRAM)
 *
 * What it does:
 *   - Turns on the backlight (GPIO1)
 *   - Draws "Hello World!" plus chip / memory info
 *   - Draws R, G, B and grey gradient strips (smooth gradients = colors OK)
 *   - Updates an uptime counter on screen and prints to Serial every second
 *
 * Requires: esp32 board package 3.x, "GFX Library for Arduino" 1.4.x+
 *
 * Arduino IDE settings (Tools menu):
 *   Board ............ ESP32S3 Dev Module
 *   USB CDC On Boot .. same setting that worked for CYD_Diag.ino
 *   Flash Size ....... 4MB (32Mb)
 *   Partition Scheme . Default 4MB with spiffs
 *   PSRAM ............ OPI PSRAM
 ******************************************************************************/
#include <Arduino_GFX_Library.h>

// ---------------------------------------------------------------------------
// JC4827W543 pins
// ---------------------------------------------------------------------------
#define TFT_BL      1      // Backlight enable
#define LCD_CS      45
#define LCD_SCK     47
#define LCD_D0      21
#define LCD_D1      48
#define LCD_D2      40
#define LCD_D3      39

#define SCREEN_W    480
#define SCREEN_H    272

// RGB565 colors (own names, so they never clash with library macros)
#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
#define C_RED       0xF800
#define C_GREEN     0x07E0
#define C_BLUE      0x001F
#define C_YELLOW    0xFFE0
#define C_CYAN      0x07FF
#define C_NAVY      0x000F

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);

// NV3041A: no reset pin wired; rotation 0 (only 0 or 2 = 180 deg work); IPS = true
Arduino_GFX *gfx = new Arduino_NV3041A(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Four horizontal gradient strips. Each should fade smoothly from black to
// full color. Wrong colors = invert/byte-order setting is off.
void drawGradients(int16_t y0, int16_t h)
{
  for (int16_t x = 0; x < SCREEN_W; x++)
  {
    uint16_t r5 = (uint32_t)x * 31 / (SCREEN_W - 1);
    uint16_t g6 = (uint32_t)x * 63 / (SCREEN_W - 1);
    uint16_t b5 = r5;

    gfx->drawFastVLine(x, y0,         h, r5 << 11);                       // red
    gfx->drawFastVLine(x, y0 + h,     h, g6 << 5);                        // green
    gfx->drawFastVLine(x, y0 + 2 * h, h, b5);                             // blue
    gfx->drawFastVLine(x, y0 + 3 * h, h, (r5 << 11) | (g6 << 5) | b5);    // grey
  }
}

void drawStaticScreen()
{
  gfx->fillScreen(C_BLACK);

  // Header bar
  gfx->fillRect(0, 0, SCREEN_W, 40, C_NAVY);
  gfx->setTextColor(C_WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(12, 9);
  gfx->print("Hello World!");

  // Corner markers - confirm all four edges are visible (no offset/cropping)
  gfx->fillRect(0, 0, 6, 6, C_RED);
  gfx->fillRect(SCREEN_W - 6, 0, 6, 6, C_GREEN);
  gfx->fillRect(0, SCREEN_H - 6, 6, 6, C_BLUE);
  gfx->fillRect(SCREEN_W - 6, SCREEN_H - 6, 6, 6, C_WHITE);

  // Board / chip info
  gfx->setTextSize(2);
  gfx->setTextColor(C_YELLOW);
  gfx->setCursor(12, 52);
  gfx->print("JC4827W543  480x272 NV3041A");

  gfx->setTextColor(C_WHITE);
  gfx->setCursor(12, 76);
  gfx->printf("%s rev%d, %d cores @ %d MHz",
              ESP.getChipModel(), ESP.getChipRevision(),
              ESP.getChipCores(), ESP.getCpuFreqMHz());

  gfx->setCursor(12, 100);
  gfx->printf("Flash: %u MB  PSRAM: %u KB",
              ESP.getFlashChipSize() / (1024 * 1024),
              ESP.getPsramSize() / 1024);

  gfx->setCursor(12, 124);
  gfx->printf("Free heap: %u KB", ESP.getFreeHeap() / 1024);

  // Gradient test strips at the bottom (4 x 16 px)
  drawGradients(SCREEN_H - 6 - 4 * 16 - 4, 16);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("JC4827W543 Hello World starting...");

  // Backlight on first - a lit (even black) screen proves setup() is running
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  Serial.println("Calling gfx->begin()...");
  // If the image is garbled/noisy, try a slower bus: gfx->begin(32000000)
  if (!gfx->begin())
  {
    Serial.println("ERROR: gfx->begin() failed.");
  }

  drawStaticScreen();
  Serial.println("Display initialised.");
}

void loop()
{
  static uint32_t lastUpdate = 0;
  static uint32_t seconds = 0;

  if (millis() - lastUpdate >= 1000)
  {
    lastUpdate += 1000;
    seconds++;

    char buf[40];
    snprintf(buf, sizeof(buf), "Uptime: %02lu:%02lu:%02lu   ",
             (unsigned long)(seconds / 3600),
             (unsigned long)((seconds / 60) % 60),
             (unsigned long)(seconds % 60));

    // Overwrite the previous value using a background color (no flicker)
    gfx->setTextSize(2);
    gfx->setTextColor(C_CYAN, C_BLACK);
    gfx->setCursor(12, 156);
    gfx->print(buf);

    Serial.printf("hello from JC4827W543 - %s\n", buf);
  }
}
