# DC35_Badge/dc_rules.h

- Move · type · L7-L13 — enum Move : uint8_t
- PlayerClass · type · L17-L21 — enum PlayerClass : uint8_t
- RoundEffect · type · L23-L32 — enum RoundEffect : uint8_t
- Fighter · class · L34-L41 — struct Fighter
- RoundResult · class · L43-L49 — struct RoundResult
- moveName · function · L51-L51 — const char *moveName(uint8_t m);
- className · function · L52-L52 — const char *className(uint8_t c);
- effectText · function · L53-L53 — const char *effectText(uint8_t fx);
- levelTier · function · L58-L58 — uint8_t levelTier(uint8_t level);
- levelHitDamage · function · L59-L59 — uint8_t levelHitDamage(uint8_t level);
- levelMaxHp · function · L60-L60 — uint8_t levelMaxHp(uint8_t level);
- resolveRound · function · L62-L62 — RoundResult resolveRound(Fighter &a, uint8_t moveA, Fighter &b, uint8_t moveB);
- battleOver · function · L63-L63 — bool battleOver(const Fighter &a, const Fighter &b, uint8_t round);
- battleWinner · function · L64-L64 — int8_t battleWinner(const Fighter &a, const Fighter &b);   // +1 a, -1 b, 0 draw
