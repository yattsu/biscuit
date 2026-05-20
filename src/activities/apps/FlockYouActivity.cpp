#include "FlockYouActivity.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---------------------------------------------------------------------------
// Known Flock Safety / Raven camera OUIs (31 entries, colonelpanichacks/flock-you)
// DRAM_ATTR keeps them in DRAM so the ISR reads them without flash cache misses.
// ---------------------------------------------------------------------------
static const uint8_t kFlockOuis[][3] DRAM_ATTR = {
  {0x70, 0xC9, 0x4E}, {0x3C, 0x91, 0x80}, {0xD8, 0xF3, 0xBC},
  {0x80, 0x30, 0x49}, {0xB8, 0x35, 0x32}, {0x14, 0x5A, 0xFC},
  {0x74, 0x4C, 0xA1}, {0x08, 0x3A, 0x88}, {0x9C, 0x2F, 0x9D},
  {0xC0, 0x35, 0x32}, {0x94, 0x08, 0x53}, {0xE4, 0xAA, 0xEA},
  {0xF4, 0x6A, 0xDD}, {0xF8, 0xA2, 0xD6}, {0x24, 0xB2, 0xB9},
  {0x00, 0xF4, 0x8D}, {0xD0, 0x39, 0x57}, {0xE8, 0xD0, 0xFC},
  {0xE0, 0x4F, 0x43}, {0xB8, 0x1E, 0xA4}, {0x70, 0x08, 0x94},
  {0x58, 0x8E, 0x81}, {0xEC, 0x1B, 0xBD}, {0x3C, 0x71, 0xBF},
  {0x58, 0x00, 0xE3}, {0x90, 0x35, 0xEA}, {0x5C, 0x93, 0xA2},
  {0x64, 0x6E, 0x69}, {0x48, 0x27, 0xEA}, {0xA4, 0xCF, 0x12},
  {0x82, 0x6B, 0xF2},
};
static constexpr int kFlockOuiCount = sizeof(kFlockOuis) / sizeof(kFlockOuis[0]);

static FlockYouActivity* activeScanner = nullptr;

static void IRAM_ATTR flockCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!activeScanner || !buf) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  activeScanner->onPacket(
      pkt->payload,
      static_cast<uint16_t>(pkt->rx_ctrl.sig_len),
      static_cast<int8_t>(pkt->rx_ctrl.rssi),
      static_cast<uint8_t>(pkt->rx_ctrl.channel));
}

// ---------------------------------------------------------------------------
// ISR helpers
// ---------------------------------------------------------------------------
bool IRAM_ATTR FlockYouActivity::matchOui(const uint8_t* mac) {
  for (int i = 0; i < kFlockOuiCount; i++) {
    if (mac[0] == kFlockOuis[i][0] &&
        mac[1] == kFlockOuis[i][1] &&
        mac[2] == kFlockOuis[i][2]) return true;
  }
  return false;
}

bool IRAM_ATTR FlockYouActivity::isWildcardProbe(const uint8_t* body, int bodyLen) {
  for (int i = 0; i + 1 < bodyLen; ) {
    uint8_t tag = body[i], len = body[i + 1];
    if (i + 2 + len > bodyLen) break;
    if (tag == 0) return (len == 0);
    i += 2 + len;
  }
  if (bodyLen > 4) {
    int t = bodyLen - 4;
    for (int i = 0; i + 1 < t; ) {
      uint8_t tag = body[i], len = body[i + 1];
      if (i + 2 + len > t) break;
      if (tag == 0) return (len == 0);
      i += 2 + len;
    }
  }
  return false;
}

bool IRAM_ATTR FlockYouActivity::parseSsidIe(const uint8_t* body, int bodyLen,
                                              char* ssidOut, bool& isHidden) {
  for (int i = 0; i + 1 < bodyLen; ) {
    uint8_t tag = body[i], len = body[i + 1];
    if (i + 2 + len > bodyLen) break;
    if (tag == 0) {
      isHidden = (len == 0);
      int n = (len > 32) ? 32 : len;
      memcpy(ssidOut, body + i + 2, n);
      ssidOut[n] = '\0';
      return true;
    }
    i += 2 + len;
  }
  return false;
}

// ---------------------------------------------------------------------------
// onPacket — ISR context
// ---------------------------------------------------------------------------
void IRAM_ATTR FlockYouActivity::onPacket(const uint8_t* data, uint16_t len,
                                          int8_t rssi, uint8_t channel) {
  static constexpr int8_t RSSI_MIN = -95;
  if (len < 24 || rssi < RSSI_MIN) return;

  portENTER_CRITICAL_ISR(&statsMux);
  totalFrames++;
  portEXIT_CRITICAL_ISR(&statsMux);

  uint8_t fc0     = data[0];
  uint8_t ftype   = (fc0 >> 2) & 0x03;
  uint8_t subtype = (fc0 >> 4) & 0x0F;

  const uint8_t* addr1 = data + 4;
  const uint8_t* addr2 = data + 10;

  // --- OUI match on transmitter (addr2) ---
  if (matchOui(addr2)) {
    Alert a{};
    a.kind    = KIND_FLOCK;
    a.rssi    = rssi;
    a.channel = channel;
    a.method  = METHOD_ADDR2;
    memcpy(a.mac, addr2, 6);
    if (ftype == 0 && subtype == 4) {
      const uint8_t* body = data + 24;
      int bodyLen = static_cast<int>(len) - 24;
      if (bodyLen > 0 && isWildcardProbe(body, bodyLen))
        a.method = METHOD_WILDCARD_PROBE;
    }
    portENTER_CRITICAL_ISR(&alertMux);
    int next = (alertHead + 1) % ALERT_BUF_SIZE;
    if (next != alertTail) { alertBuf[alertHead] = a; alertHead = next; }
    portEXIT_CRITICAL_ISR(&alertMux);
  }

  // --- OUI match on receiver (addr1) — catches burst-sleep devices ---
  if (!isMulticast(addr1) && matchOui(addr1)) {
    Alert a{};
    a.kind    = KIND_FLOCK;
    a.rssi    = rssi;
    a.channel = channel;
    a.method  = METHOD_ADDR1;
    memcpy(a.mac, addr1, 6);
    portENTER_CRITICAL_ISR(&alertMux);
    int next = (alertHead + 1) % ALERT_BUF_SIZE;
    if (next != alertTail) { alertBuf[alertHead] = a; alertHead = next; }
    portEXIT_CRITICAL_ISR(&alertMux);
  }

  // --- Hidden SSID cross-reference — management frames only ---
  if (ftype != 0) return;

  bool isBeacon    = (subtype == 8);
  bool isProbeResp = (subtype == 5);

  // Beacon and probe response both carry BSSID in addr2 and have 12 bytes of
  // fixed parameters (timestamp + beacon interval + capability) after the header.
  if ((isBeacon || isProbeResp) && len >= 24 + 12) {
    const uint8_t* bssid   = addr2;
    const uint8_t* body    = data + 24 + 12;
    int            bodyLen = static_cast<int>(len) - 24 - 12;
    char ssid[33] = {};
    bool isHidden = false;

    if (parseSsidIe(body, bodyLen, ssid, isHidden) && matchOui(bssid)) {
      Alert a{};
      a.rssi    = rssi;
      a.channel = channel;
      memcpy(a.mac, bssid, 6);

      if (isHidden) {
        a.kind    = KIND_HIDDEN_FLOCK;
        a.ssid[0] = '\0';
      } else {
        // Probe response reveals the SSID of a (potentially hidden) Flock AP
        a.kind = KIND_REVEALED_FLOCK;
        memcpy(a.ssid, ssid, 33);
      }

      portENTER_CRITICAL_ISR(&alertMux);
      int next = (alertHead + 1) % ALERT_BUF_SIZE;
      if (next != alertTail) { alertBuf[alertHead] = a; alertHead = next; }
      portEXIT_CRITICAL_ISR(&alertMux);
    }
  }
}

// ---------------------------------------------------------------------------
// Unified detection table — main loop only
// ---------------------------------------------------------------------------
void FlockYouActivity::upsertDetection(const Alert& a) {
  unsigned long now = millis();

  for (int i = 0; i < detectionCount; i++) {
    if (memcmp(detections[i].mac, a.mac, 6) != 0) continue;

    detections[i].rssi       = a.rssi;
    detections[i].channel    = a.channel;
    detections[i].hitCount++;
    detections[i].lastSeenMs = now;

    // Promote to a higher-confidence method (lower enum value)
    if (a.kind == KIND_FLOCK && a.method < detections[i].method)
      detections[i].method = a.method;

    // Reveal SSID if we now have it
    if (a.kind == KIND_REVEALED_FLOCK && !detections[i].ssidRevealed && a.ssid[0] != '\0') {
      memcpy(detections[i].ssid, a.ssid, 33);
      detections[i].ssidRevealed = true;
    }
    return;
  }

  if (detectionCount >= MAX_DETECTIONS) return;
  Detection& d    = detections[detectionCount++];
  memcpy(d.mac, a.mac, 6);
  d.ssid[0]       = '\0';
  d.ssidRevealed  = false;
  d.rssi          = a.rssi;
  d.channel       = a.channel;
  d.method        = (a.kind == KIND_FLOCK) ? a.method : METHOD_HIDDEN;
  d.hitCount      = 1;
  d.firstSeenMs   = now;
  d.lastSeenMs    = now;

  if (a.kind == KIND_REVEALED_FLOCK && a.ssid[0] != '\0') {
    memcpy(d.ssid, a.ssid, 33);
    d.ssidRevealed = true;
  }
}

void FlockYouActivity::processAlerts() {
  while (true) {
    portENTER_CRITICAL(&alertMux);
    bool empty = (alertTail == alertHead);
    Alert a{};
    if (!empty) { a = alertBuf[alertTail]; alertTail = (alertTail + 1) % ALERT_BUF_SIZE; }
    portEXIT_CRITICAL(&alertMux);
    if (empty) break;
    upsertDetection(a);
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void FlockYouActivity::onEnter() {
  Activity::onEnter();
  state          = IDLE;
  detectionCount = 0;
  totalFrames    = 0;
  alertHead      = 0;
  alertTail      = 0;
  currentChannel = 1;
  autoHop        = true;
  selectorIndex  = 0;
  lastUpdateMs   = millis();
  lastHopMs      = millis();
  memset(detections, 0, sizeof(detections));
  requestUpdate();
}

void FlockYouActivity::onExit() {
  Activity::onExit();
  if (state == SCANNING) stopScan();
}

FlockYouActivity::~FlockYouActivity() {
  if (activeScanner == this) {
    esp_wifi_set_promiscuous(false);
    activeScanner = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------------------
void FlockYouActivity::startScan() {
  RADIO.ensureWifi();
  WiFi.disconnect();

  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;

  activeScanner = this;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(flockCallback);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  lastHopMs    = millis();
  lastUpdateMs = millis();
  state        = SCANNING;
}

void FlockYouActivity::stopScan() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  activeScanner = nullptr;
  RADIO.shutdown();
  state = IDLE;
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void FlockYouActivity::loop() {
  if (state == IDLE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startScan();
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  unsigned long now = millis();

  if (autoHop && (now - lastHopMs >= HOP_INTERVAL_MS)) {
    currentChannel = static_cast<uint8_t>((currentChannel % 13) + 1);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    lastHopMs = now;
  }

  processAlerts();

  if (now - lastUpdateMs >= UPDATE_INTERVAL_MS) {
    lastUpdateMs = now;
    if (selectorIndex >= detectionCount && detectionCount > 0)
      selectorIndex = detectionCount - 1;
    requestUpdate();
  }

  buttonNavigator.onNext([this] {
    if (detectionCount > 0) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, detectionCount);
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this] {
    if (detectionCount > 0) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, detectionCount);
      requestUpdate();
    }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    autoHop = !autoHop;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    stopScan();
    finish();
  }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void FlockYouActivity::formatMac(char* out, int bufLen, const uint8_t* mac) {
  snprintf(out, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char* FlockYouActivity::methodLabel(uint8_t method) {
  switch (method) {
    case METHOD_WILDCARD_PROBE: return "PROBE";
    case METHOD_ADDR2:          return "TX";
    case METHOD_ADDR1:          return "RX";
    case METHOD_HIDDEN:         return "HIDE";
    default:                    return "?";
  }
}

void FlockYouActivity::render(RenderLock&&) {
  const auto& metrics  = UITheme::getInstance().getMetrics();
  const int pageWidth  = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // ---- IDLE ----------------------------------------------------------------
  if (state == IDLE) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Flock-You");
    const int midY = (metrics.topPadding + metrics.headerHeight +
                      pageHeight - metrics.buttonHintsHeight) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 40,
                              "Passive Flock Safety camera detector.");
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 16,
                              "Matches 31 known OUIs. Cross-references");
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 8,
                              "hidden beacons against Flock OUI list.");
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 36,
                              "Press OK to start.");
    const auto labels = mappedInput.mapLabels("Back", "Start", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // ---- SCANNING ------------------------------------------------------------
  portENTER_CRITICAL(&statsMux);
  uint32_t snapFrames = totalFrames;
  portEXIT_CRITICAL(&statsMux);

  char chBuf[16];
  snprintf(chBuf, sizeof(chBuf), "Ch:%u%s", currentChannel, autoHop ? " auto" : "");
  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Flock-You", chBuf);

  const int leftPad = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 4;
  const int fontH = renderer.getTextHeight(UI_10_FONT_ID);

  char statBuf[64];
  snprintf(statBuf, sizeof(statBuf), "Frames: %lu   Detected: %d",
           static_cast<unsigned long>(snapFrames), detectionCount);
  renderer.drawText(UI_10_FONT_ID, leftPad, y, statBuf, true, EpdFontFamily::BOLD);
  y += fontH + 6;
  renderer.drawLine(leftPad, y, pageWidth - leftPad, y, true);
  y += 6;

  if (detectionCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 28, "No Flock devices detected.");
    renderer.drawCenteredText(SMALL_FONT_ID,  y + 54, "Scanning all channels...");
  } else {
    const int listAreaH = pageHeight - metrics.buttonHintsHeight - y - 4;
    GUI.drawList(
        renderer,
        Rect{0, y, pageWidth, listAreaH},
        detectionCount,
        selectorIndex,
        // Primary line: method badge + MAC
        [&](int i) -> std::string {
          const Detection& d = detections[i];
          char mac[18];
          formatMac(mac, sizeof(mac), d.mac);
          char label[48];
          if (d.ssidRevealed) {
            snprintf(label, sizeof(label), "[%s] %s \"%s\"",
                     methodLabel(d.method), mac, d.ssid);
          } else {
            snprintf(label, sizeof(label), "[%s] %s",
                     methodLabel(d.method), mac);
          }
          return label;
        },
        // Secondary line: signal + channel + hits + age
        [&](int i) -> std::string {
          const Detection& d = detections[i];
          unsigned long age  = (millis() - d.lastSeenMs) / 1000;
          char sub[56];
          if (d.method == METHOD_HIDDEN && !d.ssidRevealed) {
            snprintf(sub, sizeof(sub), "RSSI:%d  Ch:%u  x%lu  hidden SSID  %lus ago",
                     static_cast<int>(d.rssi),
                     static_cast<unsigned>(d.channel),
                     static_cast<unsigned long>(d.hitCount),
                     age);
          } else {
            snprintf(sub, sizeof(sub), "RSSI:%d  Ch:%u  x%lu  %lus ago",
                     static_cast<int>(d.rssi),
                     static_cast<unsigned>(d.channel),
                     static_cast<unsigned long>(d.hitCount),
                     age);
          }
          return sub;
        });
  }

  const auto labels = mappedInput.mapLabels("Stop", autoHop ? "Manual ch" : "Auto ch", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Scroll", "Scroll");

  renderer.displayBuffer();
}
