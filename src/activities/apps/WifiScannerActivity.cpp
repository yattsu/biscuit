#include "WifiScannerActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void WifiScannerActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING;
  networks.clear();
  selectorIndex = 0;
  sortBySignal = true;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  startScan();
  requestUpdate();
}

void WifiScannerActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

void WifiScannerActivity::startScan() {
  state = SCANNING;
  networks.clear();
  selectorIndex = 0;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
}

void WifiScannerActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  networks.clear();
  if (result > 0) {
    networks.reserve(result);
    for (int i = 0; i < result; i++) {
      Network net;
      net.ssid = WiFi.SSID(i).c_str();
      if (net.ssid.empty()) net.ssid = "(hidden)";
      net.rssi = WiFi.RSSI(i);
      net.channel = WiFi.channel(i);
      net.encType = WiFi.encryptionType(i);

      uint8_t* bssid = WiFi.BSSID(i);
      char buf[20];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
               bssid[5]);
      net.bssid = buf;

      networks.push_back(std::move(net));
    }
    sortNetworks();
  }
  WiFi.scanDelete();
  state = LIST;
  requestUpdate();
}

void WifiScannerActivity::sortNetworks() {
  if (sortBySignal) {
    std::sort(networks.begin(), networks.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  } else {
    std::sort(networks.begin(), networks.end(),
              [](const Network& a, const Network& b) { return a.ssid < b.ssid; });
  }
}

const char* WifiScannerActivity::encryptionString(uint8_t type) const {
  switch (type) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
  }
}

const char* WifiScannerActivity::signalBars(int32_t rssi) const {
  if (rssi >= -50) return "||||";
  if (rssi >= -60) return "|||.";
  if (rssi >= -70) return "||..";
  if (rssi >= -80) return "|...";
  return "....";
}

void WifiScannerActivity::saveToCsv() {
  Storage.mkdir("/biscuit");
  Storage.mkdir("/biscuit/logs");
  char filename[64];
  snprintf(filename, sizeof(filename), "/biscuit/logs/wifi_scan_%lu.csv", millis());

  String csv = "SSID,BSSID,RSSI,Channel,Encryption\n";
  for (const auto& net : networks) {
    csv += String(net.ssid.c_str()) + "," + net.bssid.c_str() + "," + String(net.rssi) + "," +
           String(net.channel) + "," + encryptionString(net.encType) + "\n";
  }
  Storage.writeFile(filename, csv);
  LOG_DBG("WSCAN", "Saved %zu networks to %s", networks.size(), filename);
}

void WifiScannerActivity::loop() {
  if (state == SCANNING) {
    processScanResults();
    return;
  }

  if (state == DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = LIST;
      requestUpdate();
    }
    return;
  }

  // LIST state
  const int count = static_cast<int>(networks.size());

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
    if (mappedInput.getHeldTime() >= 500) {
      // Long press: save CSV
      saveToCsv();
      requestUpdate();
    } else if (!networks.empty()) {
      detailIndex = selectorIndex;
      state = DETAIL;
      requestUpdate();
    }
  }

  // Left/Right toggle sort
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    sortBySignal = !sortBySignal;
    sortNetworks();
    requestUpdate();
  }

  // PageForward: rescan
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    startScan();
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void WifiScannerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state == SCANNING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_SCANNER));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SCANNING_WIFI));
    renderer.displayBuffer();
    return;
  }

  if (state == DETAIL && detailIndex < static_cast<int>(networks.size())) {
    const auto& net = networks[detailIndex];
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, net.ssid.c_str());

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
    const int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "SSID", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, net.ssid.c_str());
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "BSSID", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, net.bssid.c_str());
    y += lineH;

    std::string rssiStr = std::to_string(net.rssi) + " dBm  " + signalBars(net.rssi);
    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_RSSI), true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, rssiStr.c_str());
    y += lineH;

    std::string chStr = std::string(tr(STR_CHANNEL)) + " " + std::to_string(net.channel);
    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_CHANNEL), true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, std::to_string(net.channel).c_str());
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_ENCRYPTION), true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, encryptionString(net.encType));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // LIST state
  std::string subtitle = std::to_string(networks.size()) + " found | " + (sortBySignal ? "By Signal" : "By Name");
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_SCANNER),
                 subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (networks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(networks.size()), selectorIndex,
        [this](int index) -> std::string {
          const auto& net = networks[index];
          return net.ssid;
        },
        [this](int index) -> std::string {
          const auto& net = networks[index];
          return std::string(signalBars(net.rssi)) + " " + std::to_string(net.rssi) + "dBm  Ch" +
                 std::to_string(net.channel) + "  " + encryptionString(net.encType);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_SELECT), tr(STR_SORT_SIGNAL), tr(STR_SCAN_AGAIN));
  GUI.drawButtonHints(renderer, labels.btn1, "Hold: CSV", labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
