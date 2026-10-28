#include "dc_radio.h"
#include "dc_config.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include "esp_mac.h"

static PeerInfo table[MAX_PEERS];
static bool used[MAX_PEERS];
static SemaphoreHandle_t mtx;

static BLEAdvertising *adv = nullptr;
static uint8_t curFrame[FRAME_LEN];
static bool haveFrame = false, curFast = false;

void badgeName(uint32_t id, char out[16])
{
  snprintf(out, 16, "DC35-%06lX", (unsigned long)(id & 0xFFFFFF));
}

uint32_t radioBadgeId()
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  return ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
}

// ---------------------------------------------------------------------------
// scanning
// ---------------------------------------------------------------------------
class ScanCb : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev) override
  {
    if (!dev.haveManufacturerData()) return;
    auto md = dev.getManufacturerData();
    if (md.length() < 2 + FRAME_LEN) return;
    const uint8_t *p = (const uint8_t *)md.c_str();
    if (p[0] != (MFG_COMPANY_ID & 0xFF) || p[1] != (MFG_COMPANY_ID >> 8)) return;

    AdvFrame f;
    if (!decodeFrame(p + 2, md.length() - 2, f)) return;

    int rssi = dev.getRSSI();
    uint32_t now = millis();

    xSemaphoreTake(mtx, portMAX_DELAY);
    int idx = -1, freeIdx = -1;
    for (int i = 0; i < MAX_PEERS; i++)
    {
      if (used[i] && table[i].id == f.id) { idx = i; break; }
      if (!used[i] && freeIdx < 0) freeIdx = i;
    }
    if (idx < 0 && freeIdx >= 0)
    {
      idx = freeIdx;
      used[idx] = true;
      table[idx].id = f.id;
      table[idx].rssiAvg = rssi;
      table[idx].firstSeen = now;
    }
    if (idx >= 0)
    {
      PeerInfo &q = table[idx];
      q.f = f;
      q.rssi = rssi;
      q.rssiAvg = q.rssiAvg * 0.75f + rssi * 0.25f;
      q.lastSeen = now;
    }
    xSemaphoreGive(mtx);
  }
};

static void scanTask(void *)
{
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCb(), true /* every advert -> live state */);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(90);
  for (;;)
  {
    scan->start(2, false);
    scan->clearResults();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------------------------------------------------------------------
uint32_t radioBegin(const char *name)
{
  mtx = xSemaphoreCreateMutex();
  BLEDevice::init(name);
  adv = BLEDevice::getAdvertising();

  // Scan response carries the readable name for phone apps (nRF Connect etc.).
  // Badges scan passively, so they never request it.
  BLEAdvertisementData sr;
  sr.setName(name);
  adv->setScanResponseData(sr);

  xTaskCreatePinnedToCore(scanTask, "bleScan", 4096, nullptr, 1, nullptr, 0);
  return radioBadgeId();
}

void radioSetFrame(const AdvFrame &f, bool fast)
{
  uint8_t buf[FRAME_LEN];
  encodeFrame(f, buf);
  bool frameChanged = !haveFrame || memcmp(buf, curFrame, FRAME_LEN) != 0;
  bool speedChanged = !haveFrame || fast != curFast;
  if (!frameChanged && !speedChanged) return;

  if (frameChanged)
  {
    uint8_t mfg[2 + FRAME_LEN];
    mfg[0] = MFG_COMPANY_ID & 0xFF;
    mfg[1] = MFG_COMPANY_ID >> 8;
    memcpy(mfg + 2, buf, FRAME_LEN);

    BLEAdvertisementData ad;
    ad.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    ad.setManufacturerData(String((const char *)mfg, sizeof(mfg)));
    adv->setAdvertisementData(ad);   // updates a running advertisement in place
    memcpy(curFrame, buf, FRAME_LEN);
  }

  if (speedChanged)
  {
    adv->stop();
    adv->setMinInterval(fast ? ADV_BATTLE_MIN : ADV_IDLE_MIN);
    adv->setMaxInterval(fast ? ADV_BATTLE_MAX : ADV_IDLE_MAX);
    adv->start();
    curFast = fast;
  }
  haveFrame = true;
}

int radioSnapshot(PeerInfo *out, int max, uint32_t now)
{
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < MAX_PEERS; i++)
  {
    if (!used[i]) continue;
    if (now - table[i].lastSeen > PEER_STALE_MS) { used[i] = false; continue; }
    if (n < max) out[n++] = table[i];
  }
  xSemaphoreGive(mtx);

  for (int i = 1; i < n; i++)   // strongest signal first
  {
    PeerInfo k = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].rssiAvg < k.rssiAvg) { out[j + 1] = out[j]; j--; }
    out[j + 1] = k;
  }
  return n;
}
