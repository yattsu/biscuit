#include "RogueApDetectorActivity.h"
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <algorithm>
#include <string>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING;
  allAps.clear();
  groups.clear();
  scanPhase = 0;
  suspiciousCount = 0;
  scanCount = 0;
  selectorIndex = 0;
  detailGroupIndex = -1;
  RADIO.ensureWifi();
  WiFi.disconnect();
  startScan();
  requestUpdate();
}

void RogueApDetectorActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  RADIO.shutdown();
}

// ---------------------------------------------------------------------------
// Scan management
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::startScan() {
  state = SCANNING;
  if (scanPhase == 0) {
    allAps.clear();
    groups.clear();
  }
  scanCount++;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
  requestUpdate();
}

void RogueApDetectorActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  if (result > 0) {
    for (int i = 0; i < result; i++) {
      ApRecord rec;
      rec.ssid = WiFi.SSID(i).c_str();
      if (rec.ssid.empty()) rec.ssid = "(hidden)";
      rec.rssi = WiFi.RSSI(i);
      rec.channel = static_cast<uint8_t>(WiFi.channel(i));
      rec.encType = static_cast<uint8_t>(WiFi.encryptionType(i));

      uint8_t* bssid = WiFi.BSSID(i);
      char buf[20];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      rec.bssid = buf;

      // Deduplicate by BSSID — update RSSI if we already have this AP
      bool found = false;
      for (auto& existing : allAps) {
        if (existing.bssid == rec.bssid) {
          existing.rssi = rec.rssi;
          found = true;
          break;
        }
      }
      if (!found && static_cast<int>(allAps.size()) < 200) {
        allAps.push_back(std::move(rec));
      }
    }
  }

  WiFi.scanDelete();
  scanPhase++;

  if (scanPhase < SCAN_PHASES) {
    startScan();
  } else {
    analyzeGroups();
    state = RESULTS;
    selectorIndex = 0;
    requestUpdate();
  }
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::analyzeGroups() {
  groups.clear();
  suspiciousCount = 0;

  // Group APs by SSID
  for (const auto& ap : allAps) {
    bool found = false;
    for (auto& g : groups) {
      if (g.ssid == ap.ssid) {
        g.apCount++;
        found = true;
        break;
      }
    }
    if (!found) {
      SsidGroup g;
      g.ssid = ap.ssid;
      g.apCount = 1;
      g.suspicious = false;
      g.mixedEncryption = false;
      g.mixedChannels = false;
      groups.push_back(std::move(g));
    }
  }

  // Analyse each group for anomalies
  for (auto& g : groups) {
    if (g.apCount <= 1) continue;

    // Multiple APs with the same SSID is suspicious
    g.suspicious = true;

    // Detect mixed encryption and mixed channels within the group
    uint8_t firstEnc = 0xFF;
    uint8_t firstCh = 0xFF;
    bool encSet = false;
    bool chSet = false;

    for (const auto& ap : allAps) {
      if (ap.ssid != g.ssid) continue;

      if (!encSet) {
        firstEnc = ap.encType;
        encSet = true;
      } else if (ap.encType != firstEnc) {
        g.mixedEncryption = true;
      }

      if (!chSet) {
        firstCh = ap.channel;
        chSet = true;
      } else if (ap.channel != firstCh) {
        g.mixedChannels = true;
      }
    }
  }

  // Count suspicious groups
  for (const auto& g : groups) {
    if (g.suspicious) suspiciousCount++;
  }

  // Sort: suspicious first, then by apCount descending
  std::sort(groups.begin(), groups.end(), [](const SsidGroup& a, const SsidGroup& b) {
    if (a.suspicious != b.suspicious) return a.suspicious > b.suspicious;
    return a.apCount > b.apCount;
  });
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::loop() {
  if (state == SCANNING) {
    processScanResults();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = RESULTS;
      requestUpdate();
    }
    return;
  }

  // RESULTS state
  const int count = static_cast<int>(groups.size());

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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!groups.empty()) {
      detailGroupIndex = selectorIndex;
      state = DETAIL;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

// ---------------------------------------------------------------------------
// Render dispatch
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case SCANNING: renderScanning(); break;
    case RESULTS:  renderResults();  break;
    case DETAIL:   renderDetail();   break;
  }
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// renderScanning
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::renderScanning() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Rogue AP Detector");

  char buf[48];
  snprintf(buf, sizeof(buf), "Scanning... (Phase %d/%d)", scanPhase + 1, SCAN_PHASES);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, buf);

  char countBuf[32];
  snprintf(countBuf, sizeof(countBuf), "%d APs found so far", static_cast<int>(allAps.size()));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 20, countBuf);
}

// ---------------------------------------------------------------------------
// renderResults
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::renderResults() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "%d APs, %d suspicious",
           static_cast<int>(allAps.size()), suspiciousCount);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Rogue AP Detector", subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (groups.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No networks found");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(groups.size()), selectorIndex,
        [this](int index) -> std::string {
          const auto& g = groups[index];
          std::string title;
          if (g.suspicious) title = "! ";
          title += g.ssid;
          return title;
        },
        [this](int index) -> std::string {
          const auto& g = groups[index];
          char buf[64];
          snprintf(buf, sizeof(buf), "%d APs", g.apCount);
          std::string sub = buf;
          if (g.mixedEncryption) sub += " MIXED ENC!";
          if (g.mixedChannels)   sub += " Multi-Ch";
          return sub;
        });
  }

  const auto labels = mappedInput.mapLabels("Exit", "Detail", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// renderDetail
// ---------------------------------------------------------------------------

void RogueApDetectorActivity::renderDetail() {
  if (detailGroupIndex < 0 || detailGroupIndex >= static_cast<int>(groups.size())) {
    state = RESULTS;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int leftPad = metrics.contentSidePadding;

  const SsidGroup& g = groups[detailGroupIndex];

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 g.ssid.c_str());

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;

  // Risk assessment
  const char* riskLabel;
  if (g.mixedEncryption) {
    riskLabel = "RISK: HIGH";
  } else if (g.suspicious) {
    riskLabel = "RISK: MEDIUM";
  } else {
    riskLabel = "RISK: LOW";
  }

  renderer.drawText(UI_12_FONT_ID, leftPad, y, riskLabel, true, EpdFontFamily::BOLD);
  y += 40;

  // Reason line
  if (g.mixedEncryption) {
    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Multiple BSSIDs + mixed encryption detected");
  } else if (g.suspicious) {
    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Multiple BSSIDs sharing this SSID");
  } else {
    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Single AP — no anomaly detected");
  }
  y += 28;

  // Separator
  renderer.fillRect(leftPad, y, pageWidth - leftPad * 2, 1, true);
  y += 10;

  // List all APs in this group
  const int lineH = 38;
  const int bottomLimit = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lineH;

  for (const auto& ap : allAps) {
    if (ap.ssid != g.ssid) continue;
    if (y >= bottomLimit) break;

    char buf[64];
    snprintf(buf, sizeof(buf), "%s  Ch%d  %ddBm  %s",
             ap.bssid.c_str(),
             static_cast<int>(ap.channel),
             static_cast<int>(ap.rssi),
             encryptionString(ap.encType));
    renderer.drawText(UI_10_FONT_ID, leftPad, y, buf);
    y += lineH;
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

const char* RogueApDetectorActivity::encryptionString(uint8_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN:         return "Open";
    case WIFI_AUTH_WEP:          return "WEP";
    case WIFI_AUTH_WPA_PSK:      return "WPA";
    case WIFI_AUTH_WPA2_PSK:     return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:     return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/3";
    default:                     return "?";
  }
}
