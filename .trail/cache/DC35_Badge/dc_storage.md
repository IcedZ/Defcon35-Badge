# DC35_Badge/dc_storage.h

- storageLoad · function · L5-L5 — bool storageLoad(Player &p);    // false if nothing saved (or old format)
- storageSave · function · L6-L6 — void storageSave(const Player &p);
- storageErase · function · L7-L7 — void storageErase();
- storageLoadBrightness · function · L11-L11 — uint8_t storageLoadBrightness(uint8_t fallback);
- storageSaveBrightness · function · L12-L12 — void    storageSaveBrightness(uint8_t level);
