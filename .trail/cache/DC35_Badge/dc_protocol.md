# DC35_Badge/dc_protocol.h

- WireState · type · L35-L44 — enum WireState : uint8_t
- FrameFlags · type · L46-L52 — enum FrameFlags : uint8_t
- AdvFrame · class · L54-L67 — struct AdvFrame
- encodeFrame · function · L69-L69 — void encodeFrame(const AdvFrame &f, uint8_t out[FRAME_LEN]);
- decodeFrame · function · L70-L70 — bool decodeFrame(const uint8_t *in, size_t len, AdvFrame &f);   // false if not a DC35 frame
