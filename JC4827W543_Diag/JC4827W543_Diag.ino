/*******************************************************************************
 * JC4827W543_Diag.ino  -  No-display diagnostic for the Guition JC4827W543
 *
 * Does NOT use Arduino_GFX. It only:
 *   - blinks the backlight (GPIO1) once per second  -> proves code is running
 *   - prints chip / PSRAM / flash / reset-reason to Serial every second
 *
 * Expected on a healthy board: flash 4 MB, PSRAM found: YES (~8192 KB)
 *
 * Backlight blinking + no serial  -> serial path problem (toggle USB CDC On Boot)
 * Serial OK, PSRAM found: NO       -> set Tools > PSRAM > OPI PSRAM
 * Nothing blinks, nothing prints   -> board not flashed / stuck in download mode (press RESET)
 ******************************************************************************/
#include "esp_system.h"

#define TFT_BL 1

const char *resetReasonStr(esp_reset_reason_t r)
{
  switch (r)
  {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_SW:       return "SOFTWARE";
    case ESP_RST_PANIC:    return "PANIC (crash)";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_EXT:      return "EXT (reset pin)";
    default:               return "OTHER";
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(1000);
}

void loop()
{
  static uint32_t n = 0;
  static bool bl = true;

  Serial.printf("[%lu] alive | %s rev%d | reset: %s | flash %u MB | PSRAM found: %s, %u KB (free %u KB)\n",
                (unsigned long)n++,
                ESP.getChipModel(), ESP.getChipRevision(),
                resetReasonStr(esp_reset_reason()),
                ESP.getFlashChipSize() / (1024 * 1024),
                psramFound() ? "YES" : "NO",
                ESP.getPsramSize() / 1024, ESP.getFreePsram() / 1024);

  bl = !bl;
  digitalWrite(TFT_BL, bl);   // backlight blinks every second
  delay(1000);
}
