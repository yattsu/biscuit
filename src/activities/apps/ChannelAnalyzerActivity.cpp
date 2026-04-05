#include "ChannelAnalyzerActivity.h"

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

// ---- onEnter / onExit -------------------------------------------------------

void ChannelAnalyzerActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING;
  memset(channels, 0, sizeof(channels));
  aps.clear();
  scanCount = 0;
  viewMode = VIEW_SPECTRUM;
  RADIO.ensureWifi();
  WiFi.disconnect();
  startScan();
  requestUpdate();
}

void ChannelAnalyzerActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  RADIO.shutdown();
}

// ---- Scan helpers -----------------------------------------------------------

void ChannelAnalyzerActivity::startScan() {
  state = SCANNING;
  aps.clear();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
  requestUpdate();
}

void ChannelAnalyzerActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  if (result > 0) {
    for (int i = 0; i < result; i++) {
      if (static_cast<int>(aps.size()) >= 200) break;
      ApInfo ap;
      ap.ssid = WiFi.SSID(i).c_str();
      if (ap.ssid.empty()) ap.ssid = "(hidden)";
      ap.rssi = WiFi.RSSI(i);
      ap.channel = static_cast<uint8_t>(WiFi.channel(i));
      aps.push_back(std::move(ap));
    }
  }

  WiFi.scanDelete();
  scanCount++;
  analyzeChannels();
  state = SHOWING;
  requestUpdate();
}

void ChannelAnalyzerActivity::analyzeChannels() {
  // Reset channel data
  for (int ch = 1; ch <= 13; ch++) {
    channels[ch].apCount = 0;
    channels[ch].strongestRssi = -100;
    channels[ch].avgRssi = -100;
    channels[ch].rssiSum = 0;
    channels[ch].interferenceScore = 0;
  }

  // Count APs per channel, track strongest RSSI, accumulate rssiSum
  for (const auto& ap : aps) {
    int ch = static_cast<int>(ap.channel);
    if (ch < 1 || ch > 13) continue;
    channels[ch].apCount++;
    if (ap.rssi > channels[ch].strongestRssi) channels[ch].strongestRssi = ap.rssi;
    channels[ch].rssiSum += ap.rssi;
  }

  // Compute avgRssi
  for (int ch = 1; ch <= 13; ch++) {
    if (channels[ch].apCount > 0) {
      channels[ch].avgRssi = static_cast<int32_t>(channels[ch].rssiSum / channels[ch].apCount);
    }
  }

  // Compute interference scores with ±2 channel overlap
  for (const auto& ap : aps) {
    int apCh = static_cast<int>(ap.channel);
    if (apCh < 1 || apCh > 13) continue;

    // Signal weight: stronger signal = more interference. Clamp to min 1.
    int signalWeight = 100 + ap.rssi;
    if (signalWeight < 1) signalWeight = 1;

    for (int target = 1; target <= 13; target++) {
      int dist = apCh - target;
      if (dist < 0) dist = -dist;
      if (dist > 2) continue;

      int overlapPct;
      if (dist == 0) overlapPct = 100;
      else if (dist == 1) overlapPct = 70;
      else overlapPct = 30;  // dist == 2

      channels[target].interferenceScore += signalWeight * overlapPct / 100;
    }
  }

  recommendedChannel = findBestChannel();
}

int ChannelAnalyzerActivity::findBestChannel() const {
  // First prefer the classic non-overlapping channels: 1, 6, 11
  static const int preferred[] = {1, 6, 11};
  int bestScore = INT32_MAX;
  int bestCh = 1;

  for (int p : preferred) {
    if (channels[p].interferenceScore < bestScore) {
      bestScore = channels[p].interferenceScore;
      bestCh = p;
    }
  }

  // Then check all channels in case a non-standard channel beats our best preferred
  for (int ch = 1; ch <= 13; ch++) {
    if (channels[ch].interferenceScore < bestScore) {
      bestScore = channels[ch].interferenceScore;
      bestCh = ch;
    }
  }

  return bestCh;
}

// ---- loop -------------------------------------------------------------------

void ChannelAnalyzerActivity::loop() {
  if (state == SCANNING) {
    processScanResults();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) finish();
    return;
  }

  // DISPLAY state
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    startScan();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    viewMode = (viewMode == VIEW_SPECTRUM) ? VIEW_TABLE : VIEW_SPECTRUM;
    requestUpdate();
  }
}

// ---- render dispatch --------------------------------------------------------

void ChannelAnalyzerActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  if (state == SCANNING) {
    renderScanning();
  } else {
    if (viewMode == VIEW_SPECTRUM) renderSpectrum();
    else renderTable();
  }
  renderer.displayBuffer();
}

// ---- renderScanning ---------------------------------------------------------

void ChannelAnalyzerActivity::renderScanning() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Channel Analyzer");

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning WiFi channels...");
}

// ---- renderSpectrum ---------------------------------------------------------

void ChannelAnalyzerActivity::renderSpectrum() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Header subtitle: "X APs — Best: Ch Y"
  char subtitle[40];
  snprintf(subtitle, sizeof(subtitle), "%d APs  Best: Ch %d",
           static_cast<int>(aps.size()), recommendedChannel);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Channel Analyzer", subtitle);

  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  // Graph area
  const int graphX = 30;
  const int graphY = headerBottom + 20;
  const int graphW = pageWidth - 50;
  const int graphH = 250;

  // Draw X axis and Y axis
  renderer.drawLine(graphX, graphY + graphH, graphX + graphW, graphY + graphH, true);  // X axis
  renderer.drawLine(graphX, graphY, graphX, graphY + graphH, true);                     // Y axis

  // Y axis labels: -30 at top, -65 at middle, -100 at bottom
  renderer.drawText(SMALL_FONT_ID, 0, graphY, "-30");
  renderer.drawText(SMALL_FONT_ID, 0, graphY + graphH / 2 - renderer.getTextHeight(SMALL_FONT_ID) / 2, "-65");
  renderer.drawText(SMALL_FONT_ID, 0, graphY + graphH - renderer.getTextHeight(SMALL_FONT_ID), "-100");

  // Channel spacing across the graph width (13 channels)
  const int chSpacing = graphW / 13;

  // Helper: channel index to X center on graph
  // ch 1 => graphX + chSpacing/2, ch 13 => graphX + 12*chSpacing + chSpacing/2
  auto chToX = [&](int ch) -> int {
    return graphX + (ch - 1) * chSpacing + chSpacing / 2;
  };

  // Dotted vertical grid lines per channel
  for (int ch = 1; ch <= 13; ch++) {
    int cx = chToX(ch);
    for (int py = graphY; py < graphY + graphH; py += 4) {
      renderer.drawPixel(cx, py, true);
    }
  }

  // Recommended channel column highlight (a thin rectangle border)
  {
    int rcx = chToX(recommendedChannel);
    int colLeft = rcx - chSpacing / 2;
    renderer.drawRect(colLeft, graphY, chSpacing, graphH, true);
  }

  // X axis channel labels below axis
  const int labelY = graphY + graphH + 3;
  for (int ch = 1; ch <= 13; ch++) {
    char label[4];
    snprintf(label, sizeof(label), "%d", ch);
    int tw = renderer.getTextWidth(SMALL_FONT_ID, label);
    int cx = chToX(ch);
    renderer.drawText(SMALL_FONT_ID, cx - tw / 2, labelY, label);
  }

  // Bell curves for each AP (outline only — 1-bit display)
  // RSSI range: -100 (bottom) to -30 (top) = 70 dB span
  // peakH = (rssiClamped + 100) * graphH / 70
  for (const auto& ap : aps) {
    int apCh = static_cast<int>(ap.channel);
    if (apCh < 1 || apCh > 13) continue;

    int32_t rssiClamped = ap.rssi;
    if (rssiClamped > -30) rssiClamped = -30;
    if (rssiClamped < -100) rssiClamped = -100;

    int peakH = (rssiClamped + 100) * graphH / 70;
    int peakY = graphY + graphH - peakH;
    int centerX = chToX(apCh);

    // Draw outline by sampling dx at 1px intervals from -2*chSpacing to +2*chSpacing
    // and connecting adjacent computed points with drawLine
    int halfSpread = 2 * chSpacing;
    int prevPx = -1, prevPy = -1;

    for (int dx = -halfSpread; dx <= halfSpread; dx++) {
      int px = centerX + dx;
      if (px < graphX || px > graphX + graphW) { prevPx = -1; continue; }

      // Gaussian-like falloff based on normalized distance
      // dist in units of chSpacing
      float normDist = static_cast<float>(dx < 0 ? -dx : dx) / static_cast<float>(chSpacing);
      float factor;
      if (normDist <= 0.5f) factor = 1.0f;
      else if (normDist <= 1.5f) factor = 0.7f;
      else {
        // linear falloff from 0.7 at 1.5 to 0 at 2.0
        factor = 0.7f * (2.0f - normDist) / 0.5f;
        if (factor < 0.0f) factor = 0.0f;
      }

      int h = static_cast<int>(peakH * factor);
      int py = graphY + graphH - h;

      if (prevPx != -1) {
        renderer.drawLine(prevPx, prevPy, px, py, true);
      }
      prevPx = px;
      prevPy = py;
    }
  }

  // Interference bar chart below the spectrum graph
  const int barY = graphY + graphH + 25;
  const int barMaxH = 40;
  const int barW = chSpacing;

  // Find max interference score for scaling
  int maxInterference = 1;
  for (int ch = 1; ch <= 13; ch++) {
    if (channels[ch].interferenceScore > maxInterference)
      maxInterference = channels[ch].interferenceScore;
  }

  for (int ch = 1; ch <= 13; ch++) {
    int cx = chToX(ch);
    int bx = cx - barW / 2 + 1;
    int bw = barW - 2;
    int bh = (channels[ch].interferenceScore * barMaxH) / maxInterference;
    if (bh < 1 && channels[ch].interferenceScore > 0) bh = 1;

    if (bh > 0) {
      renderer.fillRect(bx, barY + barMaxH - bh, bw, bh, true);
    }

    // Highlight recommended channel bar with drawRect border
    if (ch == recommendedChannel) {
      renderer.drawRect(bx - 1, barY, bw + 2, barMaxH, true);
    }
  }

  // Recommendation text centered
  const int recY = barY + barMaxH + 10;
  char recBuf[40];
  snprintf(recBuf, sizeof(recBuf), "Recommended: Channel %d", recommendedChannel);
  renderer.drawCenteredText(UI_10_FONT_ID, recY, recBuf, true, EpdFontFamily::BOLD);

  // Button hints: Back | Rescan | Table | (empty)
  const auto labels = mappedInput.mapLabels("Back", "Rescan", "Table", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- renderTable ------------------------------------------------------------

void ChannelAnalyzerActivity::renderTable() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Channel Analyzer", "Table View");

  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int pad = metrics.contentSidePadding;

  // Column X positions: Ch | APs | Best | Avg | Interference
  const int colCh   = pad;
  const int colAps  = colCh + 42;
  const int colBest = colAps + 44;
  const int colAvg  = colBest + 58;
  const int colBar  = colAvg + 54;
  const int barMaxW = pageWidth - colBar - pad;

  const int rowH = renderer.getTextHeight(SMALL_FONT_ID) + 6;
  int y = headerBottom + 8;

  // Column headers
  renderer.drawText(SMALL_FONT_ID, colCh,   y, "Ch",    true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, colAps,  y, "APs",   true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, colBest, y, "Best",  true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, colAvg,  y, "Avg",   true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, colBar,  y, "Interf",true, EpdFontFamily::BOLD);
  y += rowH + 2;

  // Separator line
  renderer.drawLine(pad, y, pageWidth - pad, y, true);
  y += 4;

  // Find max interference for bar scaling
  int maxInterference = 1;
  for (int ch = 1; ch <= 13; ch++) {
    if (channels[ch].interferenceScore > maxInterference)
      maxInterference = channels[ch].interferenceScore;
  }

  // Check if all rows fit; stop if we exceed page
  const int hintsTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  for (int ch = 1; ch <= 13; ch++) {
    if (y + rowH > hintsTop) break;

    const ChannelInfo& info = channels[ch];
    const bool isRec = (ch == recommendedChannel);

    // Mark recommended channel with ">" prefix
    char chBuf[6];
    snprintf(chBuf, sizeof(chBuf), isRec ? ">%d" : "%d", ch);
    renderer.drawText(SMALL_FONT_ID, colCh, y, chBuf, true,
                      isRec ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    char apsBuf[8];
    snprintf(apsBuf, sizeof(apsBuf), "%d", info.apCount);
    renderer.drawText(SMALL_FONT_ID, colAps, y, apsBuf);

    char bestBuf[8];
    if (info.apCount > 0) snprintf(bestBuf, sizeof(bestBuf), "%d", info.strongestRssi);
    else bestBuf[0] = '-', bestBuf[1] = '\0';
    renderer.drawText(SMALL_FONT_ID, colBest, y, bestBuf);

    char avgBuf[8];
    if (info.apCount > 0) snprintf(avgBuf, sizeof(avgBuf), "%d", info.avgRssi);
    else avgBuf[0] = '-', avgBuf[1] = '\0';
    renderer.drawText(SMALL_FONT_ID, colAvg, y, avgBuf);

    // Interference bar (proportional fillRect)
    int barW = (info.interferenceScore * barMaxW) / maxInterference;
    if (barW < 1 && info.interferenceScore > 0) barW = 1;
    const int barH = rowH - 4;
    if (barW > 0) {
      renderer.fillRect(colBar, y + 2, barW, barH, true);
    }
    // Border for recommended channel bar
    if (isRec) {
      renderer.drawRect(colBar - 1, y + 1, barMaxW + 2, barH + 2, true);
    }

    y += rowH;
  }

  // Button hints: Back | Rescan | Spectrum | (empty)
  const auto labels = mappedInput.mapLabels("Back", "Rescan", "Spectrum", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- static helper ----------------------------------------------------------

const char* ChannelAnalyzerActivity::encryptionString(uint8_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN:        return "Open";
    case WIFI_AUTH_WEP:         return "WEP";
    case WIFI_AUTH_WPA_PSK:     return "WPA";
    case WIFI_AUTH_WPA2_PSK:    return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:    return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default:                    return "?";
  }
}
