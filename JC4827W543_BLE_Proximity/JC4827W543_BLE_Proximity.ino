/*******************************************************************************
 * JC4827W543_BLE_Proximity.ino  -  DC35 badge BLE proximity test
 *
 *  FLASH SETTINGS:
 *    DEVICE: ESP32S3 Dev Module
 *    TOOLS -> USB CDC On Boot: "Enabled"
 *    TOOLS -> PSRAM -> OPI PSRAM
 * Every badge running this sketch:
 *   - gets a unique name from its Bluetooth MAC:  DC35-XXXXXX  (last 3 bytes, hex)
 *   - advertises that name plus a small "I am a DC35 badge" marker
 *   - continuously scans for other badges (phones, headphones etc. are ignored)
 *   - shows nearby badges on screen, strongest signal first, with RSSI and a
 *     CLOSE / NEAR / FAR range estimate; badges drop off after STALE_MS unseen
 *   - logs found / lost badges and a periodic summary to Serial (115200)
 *
 * Board: Guition JC4827W543 (ESP32-S3 N4R8, NV3041A 480x272 QSPI display)
 * Requires: esp32 board package 3.x (built-in BLE library),
 *           "GFX Library for Arduino" 1.4.x+
 *
 * Arduino IDE settings (Tools menu):
 *   Board ............ ESP32S3 Dev Module
 *   Flash Size ....... 4MB (32Mb)
 *   Partition Scheme . Default 4MB with spiffs   (sketch is ~680 KB of 1.25 MB)
 *   PSRAM ............ OPI PSRAM
 *   USB CDC On Boot .. same setting that worked for the Diag sketch
 *
 * Advertising packet (31-byte limit, legacy advertising):
 *   Flags (3) + Name "DC35-XXXXXX" (13) + Manufacturer data (7) = 23 bytes
 *   Manufacturer data = company ID 0xFFFF (Bluetooth SIG "testing" ID),
 *                       'D' 'C' magic, protocol version.
 *   Room is left in the packet for game data later (level, class, clan...).
 ******************************************************************************/
#include <Arduino_GFX_Library.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include "esp_mac.h"

// ---------------------------------------------------------------------------
// Display (JC4827W543)
// ---------------------------------------------------------------------------
#define TFT_BL      1
#define SCREEN_W    480
#define SCREEN_H    272

Arduino_DataBus *bus = new Arduino_ESP32QSPI(45 /* CS */, 47 /* SCK */, 21, 48, 40, 39);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
#define C_GREEN     0x07E0
#define C_YELLOW    0xFFE0
#define C_CYAN      0x07FF
#define C_GREY      0x8410
#define C_DKGREY    0x2104
#define C_NAVY      0x000F

// ---------------------------------------------------------------------------
// Badge protocol / tuning
// ---------------------------------------------------------------------------
#define BADGE_PREFIX    "DC35-"
#define MFG_COMPANY_LO  0xFF      // 0xFFFF = SIG-reserved test company ID
#define MFG_COMPANY_HI  0xFF
#define MFG_MAGIC_0     'D'
#define MFG_MAGIC_1     'C'
#define PROTO_VERSION   0x01

#define SCAN_SECONDS    2         // length of each scan window cycle
#define STALE_MS        10000     // forget a badge not heard for this long
#define RSSI_CLOSE      -60       // >= this (smoothed) = CLOSE  (future battle range)
#define RSSI_NEAR       -85       // >= this = NEAR, below = FAR
#define MAX_PEERS       32
#define MAX_ROWS        7         // rows that fit on screen
#define REDRAW_MS       500

// ---------------------------------------------------------------------------
// Peer table (written by the BLE scan callback, read by the UI loop)
// ---------------------------------------------------------------------------
struct Peer
{
  bool     used;
  char     name[16];
  int      rssi;        // last raw RSSI
  float    rssiAvg;     // smoothed RSSI
  uint32_t firstSeen;
  uint32_t lastSeen;
  uint32_t hits;        // advertisements received
};

static Peer peers[MAX_PEERS];
static SemaphoreHandle_t peersMutex;
static char myName[16];

// Found-badge events are queued for loop() so the BLE callback stays fast
static QueueHandle_t foundQueue;

// ---------------------------------------------------------------------------
// BLE scan callback
// ---------------------------------------------------------------------------
class BadgeScanCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev) override
  {
    // Only accept advertisements carrying our manufacturer marker
    if (!dev.haveManufacturerData() || !dev.haveName()) return;
    auto md = dev.getManufacturerData();
    if (md.length() < 5) return;
    const uint8_t *p = (const uint8_t *)md.c_str();
    if (p[0] != MFG_COMPANY_LO || p[1] != MFG_COMPANY_HI ||
        p[2] != MFG_MAGIC_0    || p[3] != MFG_MAGIC_1) return;

    auto nm = dev.getName();
    if (strncmp(nm.c_str(), BADGE_PREFIX, strlen(BADGE_PREFIX)) != 0) return;

    int rssi = dev.getRSSI();
    uint32_t now = millis();
    int newIdx = -1;

    xSemaphoreTake(peersMutex, portMAX_DELAY);
    int idx = -1, freeIdx = -1;
    for (int i = 0; i < MAX_PEERS; i++)
    {
      if (peers[i].used && strncmp(peers[i].name, nm.c_str(), sizeof(peers[i].name)) == 0) { idx = i; break; }
      if (!peers[i].used && freeIdx < 0) freeIdx = i;
    }
    if (idx < 0 && freeIdx >= 0)
    {
      idx = freeIdx;
      Peer &n = peers[idx];
      n.used = true;
      strlcpy(n.name, nm.c_str(), sizeof(n.name));
      n.rssiAvg = rssi;
      n.firstSeen = now;
      n.hits = 0;
      newIdx = idx;
    }
    if (idx >= 0)
    {
      Peer &q = peers[idx];
      q.rssi = rssi;
      q.rssiAvg = q.rssiAvg * 0.75f + rssi * 0.25f;   // exponential smoothing
      q.lastSeen = now;
      q.hits++;
    }
    xSemaphoreGive(peersMutex);

    if (newIdx >= 0)
    {
      Peer evt;
      strlcpy(evt.name, nm.c_str(), sizeof(evt.name));
      evt.rssi = rssi;
      xQueueSend(foundQueue, &evt, 0);
    }
  }
};

// Scanning runs in its own task so the display loop never blocks on it.
// Each cycle is a short scan; results are cleared so memory never grows.
void scanTask(void *)
{
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new BadgeScanCallbacks(), true /* want duplicates -> live RSSI */);
  scan->setActiveScan(false);   // name is in the advertisement itself; passive is quieter
  scan->setInterval(100);
  scan->setWindow(90);

  for (;;)
  {
    scan->start(SCAN_SECONDS, false);
    scan->clearResults();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------------------------------------------------------------------
// Setup helpers
// ---------------------------------------------------------------------------
void buildBadgeName()
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(myName, sizeof(myName), BADGE_PREFIX "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void startAdvertising()
{
  BLEAdvertising *adv = BLEDevice::getAdvertising();

  BLEAdvertisementData ad;
  ad.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  ad.setName(myName);
  const uint8_t mfg[] = {MFG_COMPANY_LO, MFG_COMPANY_HI, MFG_MAGIC_0, MFG_MAGIC_1, PROTO_VERSION};
  ad.setManufacturerData(String((const char *)mfg, sizeof(mfg)));

  adv->setAdvertisementData(ad);
  adv->setMinInterval(0xA0);   // 0xA0 * 0.625 ms = 100 ms
  adv->setMaxInterval(0xF0);   // 150 ms
  adv->start();
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
const char *rangeLabel(float rssi)
{
  if (rssi >= RSSI_CLOSE) return "CLOSE";
  if (rssi >= RSSI_NEAR)  return "NEAR";
  return "FAR";
}

uint16_t rangeColor(float rssi)
{
  if (rssi >= RSSI_CLOSE) return C_GREEN;
  if (rssi >= RSSI_NEAR)  return C_YELLOW;
  return C_GREY;
}

#define LIST_Y      112
#define ROW_H       22
#define BAR_X       384
#define BAR_W       84
#define BAR_H       12

void drawStaticScreen()
{
  gfx->fillScreen(C_BLACK);

  gfx->fillRect(0, 0, SCREEN_W, 36, C_NAVY);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE, C_NAVY);
  gfx->setCursor(12, 10);
  gfx->print("DC35 BLE Proximity Test");

  gfx->setTextColor(C_YELLOW, C_BLACK);
  gfx->setCursor(12, 46);
  gfx->printf("This badge: %s", myName);

  gfx->setTextColor(C_CYAN, C_BLACK);
  gfx->setCursor(12, LIST_Y - 20);
  gfx->print("NAME         RSSI RANGE  SEEN");
  gfx->drawFastHLine(12, LIST_Y - 3, SCREEN_W - 24, C_DKGREY);
}

// Draw rows in place using text background color (no full clears -> no flicker)
void drawPeerList(Peer *list, int count, int total)
{
  char line[40];

  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE, C_BLACK);
  gfx->setCursor(12, 70);
  snprintf(line, sizeof(line), "Nearby badges: %-3d", total);
  gfx->print(line);

  uint32_t now = millis();
  for (int r = 0; r < MAX_ROWS; r++)
  {
    int16_t y = LIST_Y + r * ROW_H;
    gfx->setCursor(12, y);

    if (r < count)
    {
      Peer &p = list[r];
      uint32_t ago = (now - p.lastSeen) / 1000;
      snprintf(line, sizeof(line), "%-12s %4d %-5s %3lus",
               p.name, (int)lroundf(p.rssiAvg), rangeLabel(p.rssiAvg), (unsigned long)ago);
      gfx->setTextColor(rangeColor(p.rssiAvg), C_BLACK);
      gfx->print(line);

      // Signal bar: -100 dBm = empty, -40 dBm = full
      int fill = constrain((int)((p.rssiAvg + 100.0f) * BAR_W / 60.0f), 0, BAR_W);
      gfx->fillRect(BAR_X, y + 1, fill, BAR_H, rangeColor(p.rssiAvg));
      gfx->fillRect(BAR_X + fill, y + 1, BAR_W - fill, BAR_H, C_DKGREY);
    }
    else
    {
      snprintf(line, sizeof(line), "%-30s", "");
      gfx->print(line);
      gfx->fillRect(BAR_X, y + 1, BAR_W, BAR_H, C_BLACK);
    }
  }

  if (total > MAX_ROWS)
  {
    gfx->setTextColor(C_GREY, C_BLACK);
    gfx->setCursor(300, 70);
    gfx->printf("(+%d more)", total - MAX_ROWS);
  }
  else
  {
    gfx->fillRect(300, 70, 168, 16, C_BLACK);
  }
}

// Copy live peers, drop stale ones, sort strongest first. Returns count.
int snapshotPeers(Peer *out)
{
  uint32_t now = millis();
  int n = 0;

  xSemaphoreTake(peersMutex, portMAX_DELAY);
  for (int i = 0; i < MAX_PEERS; i++)
  {
    if (!peers[i].used) continue;
    if (now - peers[i].lastSeen > STALE_MS)
    {
      Serial.printf("[-] LOST   %s (last %.0f dBm, seen %lus)\n",
                    peers[i].name, peers[i].rssiAvg,
                    (unsigned long)((peers[i].lastSeen - peers[i].firstSeen) / 1000));
      peers[i].used = false;
      continue;
    }
    out[n++] = peers[i];
  }
  xSemaphoreGive(peersMutex);

  // insertion sort by smoothed RSSI, strongest first
  for (int i = 1; i < n; i++)
  {
    Peer k = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].rssiAvg < k.rssiAvg) { out[j + 1] = out[j]; j--; }
    out[j + 1] = k;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(500);

  buildBadgeName();
  Serial.printf("\nDC35 BLE proximity test - this badge is %s\n", myName);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  if (!gfx->begin()) Serial.println("ERROR: gfx->begin() failed.");
  drawStaticScreen();

  peersMutex = xSemaphoreCreateMutex();
  foundQueue = xQueueCreate(16, sizeof(Peer));

  BLEDevice::init(myName);
  startAdvertising();
  xTaskCreatePinnedToCore(scanTask, "bleScan", 4096, nullptr, 1, nullptr, 0);

  Serial.println("Advertising + scanning. Waiting for other badges...");
}

void loop()
{
  static Peer view[MAX_PEERS];
  static uint32_t lastDraw = 0, lastSummary = 0;

  // Report newly found badges (queued from the BLE callback)
  Peer evt;
  while (xQueueReceive(foundQueue, &evt, 0) == pdTRUE)
  {
    Serial.printf("[+] FOUND  %s (%d dBm)\n", evt.name, evt.rssi);
  }

  uint32_t now = millis();
  if (now - lastDraw >= REDRAW_MS)
  {
    lastDraw = now;
    int n = snapshotPeers(view);
    drawPeerList(view, min(n, MAX_ROWS), n);

    if (now - lastSummary >= 5000)
    {
      lastSummary = now;
      Serial.printf("--- %s sees %d badge(s) ---\n", myName, n);
      for (int i = 0; i < n; i++)
      {
        Serial.printf("    %-12s %4d dBm (avg %5.1f) %-5s  %lu adv\n",
                      view[i].name, view[i].rssi, view[i].rssiAvg,
                      rangeLabel(view[i].rssiAvg), (unsigned long)view[i].hits);
      }
    }
  }

  delay(10);
}
