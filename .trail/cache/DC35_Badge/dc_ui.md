# DC35_Badge/dc_ui.h

- UiModel · class · L11-L37 — struct UiModel
- uiBegin · function · L39-L39 — void uiBegin();
- uiSetBrightness · function · L40-L40 — void uiSetBrightness(uint8_t level);                // 1..BRIGHTNESS_LEVELS
- uiRender · function · L41-L41 — void uiRender(const UiModel &m);
- uiHitTest · function · L42-L42 — bool uiHitTest(int16_t x, int16_t y, Command &c);   // tap -> command (false = nothing there)
- uiDebugDot · function · L43-L43 — void uiDebugDot(int16_t x, int16_t y);              // touchtest marker
