# DC35_Badge/dc_radio.h

- radioBegin · function · L7-L7 — uint32_t radioBegin(const char *name);                // returns 24-bit badge ID (from BT MAC)
- radioBadgeId · function · L8-L8 — uint32_t radioBadgeId();                              // ID without starting the radio
- radioSetFrame · function · L9-L9 — void     radioSetFrame(const AdvFrame &f, bool fast); // fast = battle advertising interval
- radioSnapshot · function · L10-L10 — int      radioSnapshot(PeerInfo *out, int max, uint32_t now);   // live peers, strongest first
- badgeName · function · L11-L11 — void     badgeName(uint32_t id, char out[16]);        // "DC35-XXXXXX"
