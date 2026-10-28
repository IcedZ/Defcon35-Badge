/*******************************************************************************
 * JC4827W543_SpeakerTest.ino - checks the onboard NS4168 I2S amp + speaker
 *
 * Hardware: plug a small 4-8 ohm speaker (1-3 W) into the SPEAK connector.
 * The amp is driven over I2S: BCLK GPIO42, LRCLK GPIO2, DATA GPIO41
 * (no enable pin - it plays whenever it gets I2S clocks).
 *
 * Tests (BOOT button = next test, or type 1-5 in Serial Monitor at 115200):
 *   1  Scale       C5 -> C6, one note at a time
 *   2  Sweep       200 Hz -> 4 kHz glide (listen for buzz or dropouts)
 *   3  Volume      the same tone at 10%, 25%, 50%, 75% loudness
 *   4  Level-up    a short game jingle
 *   5  Silence     stop
 *
 * Volume starts low on purpose - small speakers distort or get damaged when
 * driven hard. Raise MASTER_VOLUME if it's too quiet.
 *
 * Uses the ESP_I2S library that comes with the esp32 board package 3.x.
 ******************************************************************************/
#include <Arduino_GFX_Library.h>
#include <ESP_I2S.h>

// --- audio ---
#define I2S_BCLK      42
#define I2S_LRCLK     2
#define I2S_DOUT      41
#define SAMPLE_RATE   22050
#define MASTER_VOLUME 0.30f      // 0.0 .. 1.0

// --- display / button ---
#define TFT_BL   1
#define BOOT_BTN 0
Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);

I2SClass i2s;
static float phase = 0;
static int   test = 1;
static bool  stopRequested = false;

const char *testNames[] = {"", "Scale C5-C6", "Sweep 200Hz-4kHz", "Volume steps", "Level-up jingle", "Silence"};

// ---------------------------------------------------------------------------
// input: checked between audio chunks so a test can be interrupted
// ---------------------------------------------------------------------------
static int pollInput()
{
  static bool wasDown = false;
  static uint32_t changedAt = 0;
  bool down = digitalRead(BOOT_BTN) == LOW;
  if (down != wasDown && millis() - changedAt > 40)
  {
    wasDown = down;
    changedAt = millis();
    if (down) return test % 5 + 1;           // next test
  }
  while (Serial.available())
  {
    char c = Serial.read();
    if (c >= '1' && c <= '5') return c - '0';
  }
  return 0;
}

// Play a sine tone (freq 0 = rest). Same sample to left and right, since the
// mono amp only uses one channel. Short fade in/out avoids clicks.
static bool playTone(float freq, uint32_t ms, float vol = 1.0f)
{
  const int CHUNK = 256;
  int16_t buf[CHUNK * 2];
  uint32_t total = (uint64_t)SAMPLE_RATE * ms / 1000;
  uint32_t fade = SAMPLE_RATE / 200;         // 5 ms
  float amp = 32767.0f * MASTER_VOLUME * vol;

  for (uint32_t n = 0; n < total; n += CHUNK)
  {
    int count = min<uint32_t>(CHUNK, total - n);
    for (int i = 0; i < count; i++)
    {
      uint32_t k = n + i;
      float env = 1.0f;
      if (k < fade) env = (float)k / fade;
      else if (total - k < fade) env = (float)(total - k) / fade;
      int16_t s = 0;
      if (freq > 0)
      {
        s = (int16_t)(sinf(phase) * amp * env);
        phase += 2.0f * PI * freq / SAMPLE_RATE;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      buf[2 * i] = buf[2 * i + 1] = s;
    }
    i2s.write((uint8_t *)buf, count * 2 * sizeof(int16_t));

    int req = pollInput();
    if (req) { test = req; stopRequested = true; return false; }
  }
  return true;
}

// Linear frequency glide.
static bool sweep(float f0, float f1, uint32_t ms)
{
  const uint32_t step = 20;
  for (uint32_t t = 0; t < ms; t += step)
    if (!playTone(f0 + (f1 - f0) * t / ms, step)) return false;
  return true;
}

// ---------------------------------------------------------------------------
static void show(const char *line2)
{
  gfx->fillRect(0, 40, 480, 232, 0x0000);
  gfx->setTextSize(3);
  gfx->setTextColor(0x07E0, 0x0000);
  gfx->setCursor(10, 60);
  gfx->printf("Test %d: %s", test, testNames[test]);
  gfx->setTextSize(2);
  gfx->setTextColor(0xFFFF, 0x0000);
  gfx->setCursor(10, 110);
  gfx->print(line2);
  gfx->setTextColor(0x8410, 0x0000);
  gfx->setCursor(10, 200);
  gfx->print("BOOT = next test   Serial: 1-5");
  gfx->setCursor(10, 226);
  gfx->printf("Volume %d%%  (MASTER_VOLUME)", (int)(MASTER_VOLUME * 100));
  Serial.printf("Test %d: %s - %s\n", test, testNames[test], line2);
}

static void runTest()
{
  stopRequested = false;
  switch (test)
  {
    case 1:
    {
      show("Eight rising notes, repeating");
      const float notes[] = {523.25, 587.33, 659.25, 698.46, 783.99, 880.00, 987.77, 1046.50};
      for (float f : notes)
        if (!playTone(f, 280) || !playTone(0, 60)) return;
      playTone(0, 600);
      break;
    }
    case 2:
      show("Smooth rise, then fall");
      if (!sweep(200, 4000, 2500)) return;
      if (!sweep(4000, 200, 2500)) return;
      playTone(0, 500);
      break;
    case 3:
      show("Same 880 Hz tone, getting louder");
      {
        const float vols[] = {0.10f, 0.25f, 0.50f, 0.75f};
        for (float v : vols)
        {
          Serial.printf("  volume %d%% of master\n", (int)(v * 100));
          if (!playTone(880, 600, v) || !playTone(0, 300)) return;
        }
      }
      playTone(0, 600);
      break;
    case 4:
    {
      show("Short jingle, repeating");
      const float f[] = {523.25, 659.25, 783.99, 1046.50, 0, 783.99, 1046.50};
      const uint16_t d[] = {110, 110, 110, 260, 60, 110, 420};
      for (int i = 0; i < 7; i++)
        if (!playTone(f[i], d[i])) return;
      playTone(0, 900);
      break;
    }
    default:
      show("Quiet. Pick a test to resume.");
      while (!stopRequested) playTone(0, 100);
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(0x0000);
  gfx->fillRect(0, 0, 480, 34, 0x000F);
  gfx->setTextSize(2);
  gfx->setTextColor(0xFFFF, 0x000F);
  gfx->setCursor(10, 9);
  gfx->print("Speaker test (NS4168 amp, I2S)");

  i2s.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
  {
    Serial.println("ERROR: I2S failed to start");
    gfx->setTextColor(0xF800, 0x0000);
    gfx->setCursor(10, 60);
    gfx->print("I2S failed to start");
    while (true) delay(1000);
  }
  Serial.println("\nSpeaker test ready. BOOT or 1-5 to change test.");
}

void loop()
{
  runTest();   // each test repeats until another one is chosen
}
