# DC35_Badge/dc_player.h

- Player · class · L8-L19 — struct Player
- XpAward · class · L21-L27 — struct XpAward
- playerInit · function · L29-L29 — void     playerInit(Player &p);
- playerMaxHp · function · L30-L30 — uint8_t  playerMaxHp(const Player &p);
- playerXpToNext · function · L31-L31 — uint32_t playerXpToNext(const Player &p);
- playerCanBattle · function · L32-L32 — bool     playerCanBattle(const Player &p);
- playerNormalize · function · L35-L35 — bool     playerNormalize(Player &p);
- playerRegen · function · L38-L38 — bool     playerRegen(Player &p, uint32_t now);
- playerApplyBattle · function · L41-L42 — XpAward  playerApplyBattle(Player &p, int8_t outcome, int16_t endHp,
