#include "DeauthDetectorActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---------------------------------------------------------------------------
// Static C callback — must be a plain function, not a method
// ---------------------------------------------------------------------------

static DeauthDetectorActivity* activeDetector = nullptr;

static void deauthPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!activeDetector || !buf) return;
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = pkt->payload;
  const uint16_t sig_len = pkt->rx_ctrl.sig_len;

  if (sig_len < 26) return;

  // Frame control byte 0: subtype in upper nibble
  // 0xC0 = deauthentication, 0xA0 = disassociation
  const uint8_t frameCtrl = payload[0];
  if (frameCtrl != 0xC0 && frameCtrl != 0xA0) return;

  activeDetector->onFrame(payload, sig_len, pkt->rx_ctrl.rssi,
                          static_cast<uint8_t>(pkt->rx_ctrl.channel));
}

// ---------------------------------------------------------------------------
// onFrame — called from the WiFi RX task, must be ISR-safe
// ---------------------------------------------------------------------------

void DeauthDetectorActivity::onFrame(const uint8_t* payload, uint16_t len, int rssi,
                                     uint8_t channel) {
  portENTER_CRITICAL(&dataMux);

  totalFrames = totalFrames + 1;
  framesThisInterval = framesThisInterval + 1;

  // 802.11 management frame layout (fixed header):
  //   [0]   Frame Control byte 0  (type/subtype)
  //   [1]   Frame Control byte 1  (flags)
  //   [2-3] Duration
  //   [4-9] Destination MAC
  //   [10-15] Source MAC (attacker)
  //   [16-21] BSSID (target)
  //   [22-23] Sequence control
  //   [24]  Reason code (deauth/disassoc body)
  const uint8_t* srcMac  = payload + 10;
  const uint8_t* bssid   = payload + 16;
  const uint8_t  reason  = (len > 24) ? payload[24] : 0;
  const uint8_t  ftype   = payload[0];

  // Search for an existing event matching attacker MAC + BSSID
  for (int i = 0; i < eventCount; i++) {
    if (memcmp(events[i].attackerMac, srcMac, 6) == 0 &&
        memcmp(events[i].targetBssid, bssid,  6) == 0) {
      events[i].count++;
      events[i].lastSeen = millis();
      events[i].rssi     = static_cast<int8_t>(rssi);
      portEXIT_CRITICAL(&dataMux);
      return;
    }
  }

  // New event
  if (eventCount < MAX_EVENTS) {
    DetectionEvent& ev = events[eventCount];
    memcpy(ev.attackerMac, srcMac, 6);
    memcpy(ev.targetBssid, bssid,  6);
    ev.channel    = channel;
    ev.rssi       = static_cast<int8_t>(rssi);
    ev.count      = 1;
    ev.firstSeen  = millis();
    ev.lastSeen   = millis();
    ev.frameType  = ftype;
    ev.reasonCode = reason;
    eventCount++;
  }

  portEXIT_CRITICAL(&dataMux);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DeauthDetectorActivity::onEnter() {
  Activity::onEnter();

  state         = MONITORING;
  selectorIndex = 0;
  detailIndex   = 0;
  alertActive   = false;
  autoHop       = true;
  currentChannel = 1;
  historyIndex  = 0;
  framesPerSec  = 0;
  totalFrames   = 0;
  framesThisInterval = 0;
  eventCount    = 0;

  memset(events,      0, sizeof(events));
  memset(rateHistory, 0, sizeof(rateHistory));

  lastHopTime    = millis();
  lastUpdateTime = millis();

  startMonitoring();
  requestUpdate();
}

void DeauthDetectorActivity::onExit() {
  Activity::onExit();
  stopMonitoring();
}

void DeauthDetectorActivity::startMonitoring() {
  RADIO.ensureWifi();
  WiFi.disconnect();

  activeDetector = this;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(deauthPromiscuousCallback);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  LOG_DBG("DEAUTH", "Monitoring started on ch%d", currentChannel);
}

void DeauthDetectorActivity::stopMonitoring() {
  esp_wifi_set_promiscuous(false);
  activeDetector = nullptr;
  RADIO.shutdown();
  LOG_DBG("DEAUTH", "Monitoring stopped, %d events", eventCount);
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------

void DeauthDetectorActivity::loop() {
  // DETAIL state — only Back is handled
  if (state == DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = MONITORING;
      requestUpdate();
    }
    return;
  }

  // ---- MONITORING state ----

  const unsigned long now = millis();

  // Channel hopping
  if (autoHop && (now - lastHopTime >= HOP_INTERVAL_MS)) {
    lastHopTime = now;
    currentChannel = static_cast<uint8_t>((currentChannel % 13) + 1);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  }

  // Periodic stats update
  if (now - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    const unsigned long elapsed = now - lastUpdateTime;

    portENTER_CRITICAL(&dataMux);
    const uint32_t frames = framesThisInterval;
    framesThisInterval = 0;
    portEXIT_CRITICAL(&dataMux);

    framesPerSec = (elapsed > 0) ? (frames * 1000UL) / elapsed : 0;

    rateHistory[historyIndex] = static_cast<uint16_t>(
        framesPerSec > 0xFFFF ? 0xFFFF : framesPerSec);
    historyIndex = (historyIndex + 1) % GRAPH_POINTS;

    alertActive    = (framesPerSec > 10);
    lastUpdateTime = now;
    requestUpdate();
  }

  // Navigation in event list
  portENTER_CRITICAL(&dataMux);
  const int count = eventCount;
  portEXIT_CRITICAL(&dataMux);

  buttonNavigator.onNext([this, count] {
    if (count > 0) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this, count] {
    if (count > 0) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
      requestUpdate();
    }
  });

  // Confirm: open detail view (hold = save CSV)
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= 500) {
      saveToCsv();
    } else if (count > 0) {
      detailIndex = selectorIndex;
      state = DETAIL;
    }
    requestUpdate();
    return;
  }

  // Manual channel control (disables auto-hop)
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    autoHop = false;
    if (currentChannel > 1) {
      currentChannel--;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    autoHop = false;
    if (currentChannel < 13) {
      currentChannel++;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void DeauthDetectorActivity::render(RenderLock&&) {
  const auto& metrics  = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // ---- DETAIL view ----
  if (state == DETAIL) {
    portENTER_CRITICAL(&dataMux);
    const bool validIndex = (detailIndex >= 0 && detailIndex < eventCount);
    DetectionEvent ev = validIndex ? events[detailIndex] : DetectionEvent{};
    portEXIT_CRITICAL(&dataMux);

    if (!validIndex) {
      // Stale index — fall back silently
      const_cast<DeauthDetectorActivity*>(this)->state = MONITORING;
    } else {
      GUI.drawHeader(renderer,
                     Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                     "Detection Detail");

      const int leftPad = metrics.contentSidePadding;
      int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
      constexpr int lineH = 45;

      // Source MAC
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Source MAC", true, EpdFontFamily::BOLD);
      y += 22;
      std::string srcStr = macToString(ev.attackerMac);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, srcStr.c_str());
      y += lineH;

      // Target BSSID
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Target BSSID", true, EpdFontFamily::BOLD);
      y += 22;
      std::string bssidStr = macToString(ev.targetBssid);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, bssidStr.c_str());
      y += lineH;

      // Channel
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Channel", true, EpdFontFamily::BOLD);
      y += 22;
      char chBuf[8];
      snprintf(chBuf, sizeof(chBuf), "%d", ev.channel);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, chBuf);
      y += lineH;

      // Type
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Type", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y,
                        (ev.frameType == 0xA0) ? "Disassociation" : "Deauthentication");
      y += lineH;

      // Reason
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Reason", true, EpdFontFamily::BOLD);
      y += 22;
      char reasonBuf[48];
      snprintf(reasonBuf, sizeof(reasonBuf), "%d (%s)", ev.reasonCode,
               reasonCodeStr(ev.reasonCode));
      renderer.drawText(UI_10_FONT_ID, leftPad, y, reasonBuf);
      y += lineH;

      // Count
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Count", true, EpdFontFamily::BOLD);
      y += 22;
      char cntBuf[16];
      snprintf(cntBuf, sizeof(cntBuf), "%lu", static_cast<unsigned long>(ev.count));
      renderer.drawText(UI_10_FONT_ID, leftPad, y, cntBuf);
      y += lineH;

      // RSSI
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "RSSI", true, EpdFontFamily::BOLD);
      y += 22;
      char rssiBuf[16];
      snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", static_cast<int>(ev.rssi));
      renderer.drawText(UI_10_FONT_ID, leftPad, y, rssiBuf);
      y += lineH;

      // First / last seen (seconds ago)
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "First seen", true, EpdFontFamily::BOLD);
      y += 22;
      char timeBuf[24];
      unsigned long ageSec = (millis() - ev.firstSeen) / 1000UL;
      snprintf(timeBuf, sizeof(timeBuf), "%lus ago", ageSec);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, timeBuf);
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Last seen", true, EpdFontFamily::BOLD);
      y += 22;
      ageSec = (millis() - ev.lastSeen) / 1000UL;
      snprintf(timeBuf, sizeof(timeBuf), "%lus ago", ageSec);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, timeBuf);

      const auto labels = mappedInput.mapLabels("Back", "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }

    renderer.displayBuffer();
    return;
  }

  // ---- MONITORING view ----

  // Header subtitle: channel + mode
  char subtitle[24];
  snprintf(subtitle, sizeof(subtitle), "Ch %d (%s)", currentChannel,
           autoHop ? "auto" : "manual");
  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Deauth Detector", subtitle);

  const int leftPad = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 6;

  // Alert banner — filled rect + inverted text
  if (alertActive) {
    renderer.fillRect(leftPad, y, pageWidth - 2 * leftPad, 28, true);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 4, "ALERT: ACTIVE ATTACK DETECTED", false,
                              EpdFontFamily::BOLD);
    y += 34;
  }

  // Frame rate text
  char rateBuf[32];
  snprintf(rateBuf, sizeof(rateBuf), "%lu frames/sec",
           static_cast<unsigned long>(framesPerSec));
  renderer.drawText(UI_10_FONT_ID, leftPad, y, rateBuf, true, EpdFontFamily::BOLD);
  y += renderer.getTextHeight(UI_10_FONT_ID) + 6;

  // ---- Rate history bar graph ----
  const int graphX      = leftPad + 20;
  const int graphWidth  = pageWidth - graphX - 20;
  const int graphHeight = 60;
  const int graphY      = y;

  // Find max rate for scaling
  uint16_t maxRate = 1;
  for (int i = 0; i < GRAPH_POINTS; i++) {
    if (rateHistory[i] > maxRate) maxRate = rateHistory[i];
  }

  const int barW = graphWidth / GRAPH_POINTS;
  for (int i = 0; i < GRAPH_POINTS; i++) {
    // Draw bars in chronological order (oldest first)
    const int idx = (historyIndex + i) % GRAPH_POINTS;
    const int barH = (static_cast<int>(rateHistory[idx]) * graphHeight) / maxRate;
    if (barH > 0) {
      renderer.fillRect(graphX + i * barW,
                        graphY + graphHeight - barH,
                        (barW > 2 ? barW - 1 : 1),
                        barH, true);
    }
  }
  // Graph border
  renderer.drawRect(graphX - 1, graphY - 1, graphWidth + 2, graphHeight + 2, true);
  y += graphHeight + 10;

  // Total frames and source count
  portENTER_CRITICAL(&dataMux);
  const uint32_t capturedTotal  = totalFrames;
  const int      capturedCount  = eventCount;
  portEXIT_CRITICAL(&dataMux);

  char statBuf[64];
  snprintf(statBuf, sizeof(statBuf), "Total: %lu  Sources: %d",
           static_cast<unsigned long>(capturedTotal), capturedCount);
  renderer.drawText(UI_10_FONT_ID, leftPad, y, statBuf);
  y += renderer.getTextHeight(UI_10_FONT_ID) + 8;

  // ---- Event list ----
  const int listTop    = y;
  const int listBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int listHeight = listBottom - listTop;

  if (capturedCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listHeight / 2, "No deauth frames detected");
  } else {
    // Clamp selector defensively
    if (selectorIndex >= capturedCount) {
      const_cast<DeauthDetectorActivity*>(this)->selectorIndex = capturedCount - 1;
    }

    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, listHeight},
        capturedCount, selectorIndex,
        [this](int i) -> std::string {
          portENTER_CRITICAL(&dataMux);
          const uint8_t atkMac[6] = {
              events[i].attackerMac[0], events[i].attackerMac[1],
              events[i].attackerMac[2], events[i].attackerMac[3],
              events[i].attackerMac[4], events[i].attackerMac[5]};
          const uint8_t tgtBssid[6] = {
              events[i].targetBssid[0], events[i].targetBssid[1],
              events[i].targetBssid[2], events[i].targetBssid[3],
              events[i].targetBssid[4], events[i].targetBssid[5]};
          portEXIT_CRITICAL(&dataMux);
          char buf[48];
          snprintf(buf, sizeof(buf), "%02X:%02X:%02X > %02X:%02X:%02X",
                   atkMac[0], atkMac[1], atkMac[2],
                   tgtBssid[0], tgtBssid[1], tgtBssid[2]);
          return std::string(buf);
        },
        [this](int i) -> std::string {
          portENTER_CRITICAL(&dataMux);
          const uint32_t cnt  = events[i].count;
          const uint8_t  ch   = events[i].channel;
          const int8_t   rssi = events[i].rssi;
          portEXIT_CRITICAL(&dataMux);
          char buf[40];
          snprintf(buf, sizeof(buf), "%lux Ch%d %ddBm",
                   static_cast<unsigned long>(cnt),
                   static_cast<int>(ch),
                   static_cast<int>(rssi));
          return std::string(buf);
        });
  }

  const auto labels = mappedInput.mapLabels("Exit", "Detail", "Ch-", "Ch+");
  GUI.drawButtonHints(renderer, labels.btn1, "Hold:CSV", labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// saveToCsv
// ---------------------------------------------------------------------------

void DeauthDetectorActivity::saveToCsv() {
  Storage.mkdir("/biscuit");
  Storage.mkdir("/biscuit/logs");

  char filename[64];
  snprintf(filename, sizeof(filename), "/biscuit/logs/deauth_%lu.csv", millis());

  String csv = "SourceMAC,TargetBSSID,Channel,Type,Reason,Count,RSSI\n";

  portENTER_CRITICAL(&dataMux);
  const int count = eventCount;
  for (int i = 0; i < count; i++) {
    char line[128];
    snprintf(line, sizeof(line),
             "%02X:%02X:%02X:%02X:%02X:%02X,"
             "%02X:%02X:%02X:%02X:%02X:%02X,"
             "%d,%s,%d,%lu,%d\n",
             events[i].attackerMac[0], events[i].attackerMac[1],
             events[i].attackerMac[2], events[i].attackerMac[3],
             events[i].attackerMac[4], events[i].attackerMac[5],
             events[i].targetBssid[0], events[i].targetBssid[1],
             events[i].targetBssid[2], events[i].targetBssid[3],
             events[i].targetBssid[4], events[i].targetBssid[5],
             static_cast<int>(events[i].channel),
             (events[i].frameType == 0xA0) ? "Disassoc" : "Deauth",
             static_cast<int>(events[i].reasonCode),
             static_cast<unsigned long>(events[i].count),
             static_cast<int>(events[i].rssi));
    csv += line;
  }
  portEXIT_CRITICAL(&dataMux);

  Storage.writeFile(filename, csv);
  LOG_DBG("DEAUTH", "Saved %d events to %s", count, filename);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string DeauthDetectorActivity::macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

const char* DeauthDetectorActivity::reasonCodeStr(uint8_t code) {
  switch (code) {
    case 1: return "Unspecified";
    case 2: return "Auth expired";
    case 3: return "STA leaving";
    case 4: return "Inactivity";
    case 5: return "AP full";
    case 6: return "Class 2 err";
    case 7: return "Class 3 err";
    case 8: return "STA leaving BSS";
    default: return "Other";
  }
}
