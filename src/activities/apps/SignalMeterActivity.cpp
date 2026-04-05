#include "SignalMeterActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "util/RadioManager.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SignalMeterActivity::onEnter() {
  Activity::onEnter();
  RADIO.ensureWifi();
  startScan();
  requestUpdate();
}

void SignalMeterActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  RADIO.shutdown();
}

void SignalMeterActivity::startScan() {
  state = SCANNING;
  aps.clear();
  selectorIndex = 0;
  targetIndex = -1;
  WiFi.scanDelete();
  WiFi.disconnect();
  WiFi.scanNetworks(true);  // async
}

void SignalMeterActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  aps.clear();
  if (result > 0) {
    aps.reserve(static_cast<size_t>(result));
    for (int i = 0; i < result; i++) {
      AP ap;
      String ssid = WiFi.SSID(i);
      ap.ssid = ssid.length() > 0 ? ssid.c_str() : "(hidden)";
      uint8_t* bssid = WiFi.BSSID(i);
      memcpy(ap.bssid, bssid, 6);
      ap.rssi = WiFi.RSSI(i);
      ap.channel = static_cast<uint8_t>(WiFi.channel(i));
      aps.push_back(std::move(ap));
    }
    // Sort descending by RSSI (strongest first)
    std::sort(aps.begin(), aps.end(), [](const AP& a, const AP& b) { return a.rssi > b.rssi; });
  }
  WiFi.scanDelete();
  state = SELECT_AP;
  requestUpdate();
}

void SignalMeterActivity::doMeasurement() {
  if (targetIndex < 0 || targetIndex >= static_cast<int>(aps.size())) return;

  const uint8_t ch = aps[targetIndex].channel;
  // Synchronous targeted single-channel scan: show hidden, passive=false, 300ms timeout
  int n = WiFi.scanNetworks(false, true, false, 300, ch);

  int32_t found = -200;  // sentinel: not seen
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      uint8_t* bssid = WiFi.BSSID(i);
      if (bssid && memcmp(bssid, aps[targetIndex].bssid, 6) == 0) {
        found = WiFi.RSSI(i);
        break;
      }
    }
  }
  WiFi.scanDelete();

  if (found == -200) {
    // AP not seen this scan — keep last value, don't update stats
    requestUpdate();
    return;
  }

  currentRssi = found;
  sampleCount++;
  rssiSum += found;
  avgRssi = static_cast<int32_t>(rssiSum / sampleCount);

  if (sampleCount == 1) {
    minRssi = found;
    maxRssi = found;
  } else {
    if (found < minRssi) minRssi = found;
    if (found > maxRssi) maxRssi = found;
  }

  rssiHistory[historyIndex] = found;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;

  requestUpdate();
}

void SignalMeterActivity::loop() {
  if (state == SCANNING) {
    processScanResults();
    return;
  }

  if (state == SELECT_AP) {
    const int count = static_cast<int>(aps.size());

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
      if (!aps.empty()) {
        targetIndex = selectorIndex;
        // Reset measurement state
        currentRssi = -100;
        minRssi = 0;
        maxRssi = -100;
        avgRssi = -100;
        sampleCount = 0;
        rssiSum = 0;
        historyIndex = 0;
        historyCount = 0;
        lastMeasureTime = 0;
        state = MEASURING;
        requestUpdate();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // MEASURING state
  unsigned long now = millis();
  if (now - lastMeasureTime >= MEASURE_INTERVAL_MS) {
    lastMeasureTime = now;
    doMeasurement();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Rescan for new AP selection
    startScan();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

// Map RSSI (-100 to -30 dBm) to a 0..1 fraction, clamped
static float rssiToFraction(int32_t rssi) {
  static constexpr int32_t RSSI_MIN = -100;
  static constexpr int32_t RSSI_MAX = -30;
  if (rssi <= RSSI_MIN) return 0.0f;
  if (rssi >= RSSI_MAX) return 1.0f;
  return static_cast<float>(rssi - RSSI_MIN) / static_cast<float>(RSSI_MAX - RSSI_MIN);
}

static const char* qualityLabel(int32_t rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "Fair";
  return "Weak";
}

void SignalMeterActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // ---- SCANNING ----
  if (state == SCANNING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Signal Meter");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SCANNING_WIFI));
    renderer.displayBuffer();
    return;
  }

  // ---- SELECT_AP ----
  if (state == SELECT_AP) {
    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "%d found", static_cast<int>(aps.size()));
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Signal Meter",
                   subtitle);

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    if (aps.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
    } else {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(aps.size()), selectorIndex,
          [this](int index) -> std::string { return aps[index].ssid; },
          [this](int index) -> std::string {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d dBm  Ch%d", static_cast<int>(aps[index].rssi),
                     static_cast<int>(aps[index].channel));
            return buf;
          });
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // ---- MEASURING ----
  if (targetIndex < 0 || targetIndex >= static_cast<int>(aps.size())) {
    renderer.displayBuffer();
    return;
  }

  const AP& target = aps[targetIndex];

  // Header: AP name + channel
  char headerSub[32];
  snprintf(headerSub, sizeof(headerSub), "Ch %d", static_cast<int>(target.channel));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, target.ssid.c_str(),
                 headerSub);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int leftPad = metrics.contentSidePadding;
  const int rightPad = metrics.contentSidePadding;

  // Large centered RSSI value
  char rssiText[16];
  snprintf(rssiText, sizeof(rssiText), "%d dBm", static_cast<int>(currentRssi));
  renderer.drawCenteredText(UI_12_FONT_ID, y, rssiText, true, EpdFontFamily::BOLD);
  y += renderer.getTextHeight(UI_12_FONT_ID) + 6;

  // Signal quality label
  renderer.drawCenteredText(UI_10_FONT_ID, y, qualityLabel(currentRssi));
  y += renderer.getTextHeight(UI_10_FONT_ID) + 12;

  // Horizontal signal bar
  const int barMaxWidth = pageWidth - leftPad - rightPad;
  const int barHeight = 28;
  const int filledWidth = static_cast<int>(rssiToFraction(currentRssi) * static_cast<float>(barMaxWidth));
  renderer.drawRect(leftPad, y, barMaxWidth, barHeight, true);
  if (filledWidth > 2) {
    renderer.fillRect(leftPad + 1, y + 1, filledWidth - 2, barHeight - 2, true);
  }
  y += barHeight + 12;

  // Stats line: Min / Avg / Max
  char statsText[48];
  if (sampleCount > 0) {
    snprintf(statsText, sizeof(statsText), "Min: %d  Avg: %d  Max: %d", static_cast<int>(minRssi),
             static_cast<int>(avgRssi), static_cast<int>(maxRssi));
  } else {
    snprintf(statsText, sizeof(statsText), "Waiting for samples...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, statsText);
  y += renderer.getTextHeight(UI_10_FONT_ID) + 16;

  // History graph
  if (historyCount >= 2) {
    const int graphLeft = leftPad;
    const int graphWidth = pageWidth - leftPad - rightPad;
    const int graphBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
    const int graphTop = y;
    const int graphHeight = graphBottom - graphTop;

    if (graphHeight > 10) {
      // Outline box
      renderer.drawRect(graphLeft, graphTop, graphWidth, graphHeight, true);

      // Connect sampled history points as a polyline
      // History is stored from oldest to newest via the ring buffer.
      // Reconstruct in chronological order.
      int prevX = -1;
      int prevY = -1;

      for (int i = 0; i < historyCount; i++) {
        // Oldest entry starts at historyIndex when buffer is full,
        // otherwise starts at 0.
        int slot;
        if (historyCount < HISTORY_SIZE) {
          slot = i;
        } else {
          slot = (historyIndex + i) % HISTORY_SIZE;
        }

        const float frac = rssiToFraction(rssiHistory[slot]);
        // Y: high RSSI (strong) maps to top of graph
        const int px = graphLeft + 1 + (i * (graphWidth - 2)) / (historyCount - 1);
        const int py = graphTop + 1 + static_cast<int>((1.0f - frac) * static_cast<float>(graphHeight - 2));

        if (prevX >= 0) {
          renderer.drawLine(prevX, prevY, px, py, true);
        }
        prevX = px;
        prevY = py;
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SCAN_AGAIN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
