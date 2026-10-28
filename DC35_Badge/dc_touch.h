// dc_touch.h - XPT2046 resistive touch driver (device only, JC4827W543R).
// No external library: the XPT2046 is read directly over SPI (HSPI), the
// same bus the microSD slot uses.
#pragma once
#include <stdint.h>

void touchBegin();
// True once per new press. x/y are screen coordinates; rawX/rawY are the
// controller's 12-bit readings (for calibration with "touchtest").
bool touchPollTap(int16_t &x, int16_t &y, int16_t *rawX = nullptr, int16_t *rawY = nullptr);
