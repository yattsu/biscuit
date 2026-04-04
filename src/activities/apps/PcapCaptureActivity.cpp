#include "PcapCaptureActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

static PcapCaptureActivity* activeCapture = nullptr;

static void pcapPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!activeCapture || !buf) return;
  const wifi_promiscuous_pkt_t* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  activeCapture->onPacket(pkt->payload, pkt->rx_ctrl.sig_len);
}

void PcapCaptureActivity::onPacket(const uint8_t* buf, uint16_t len) {
  if (!fileOpen) return;

  if (captureMode == EAPOL_ONLY && !isEapolPacket(buf, len)) return;

  if (isEapolPacket(buf, len)) eapolFound = true;

  portENTER_CRITICAL(&fileMux);
  writePacket(buf, len);
  portEXIT_CRITICAL(&fileMux);
}

bool PcapCaptureActivity::isEapolPacket(const uint8_t* data, uint16_t len) const {
  // Check for EAPOL ethertype 0x888E in 802.11 data frames
  // Simplified check: look for 0x88 0x8E in the packet
  if (len < 34) return false;
  for (int i = 24; i < len - 1 && i < 40; i++) {
    if (data[i] == 0x88 && data[i + 1] == 0x8E) return true;
  }
  return false;
}

void PcapCaptureActivity::onEnter() {
  Activity::onEnter();
  RADIO.ensureWifi();
  state = MODE_SELECT;
  modeIndex = 0;
  packetsSaved = 0;
  fileSize = 0;
  eapolFound = false;
  autoHop = true;
  currentChannel = 1;
  activeCapture = this;
  requestUpdate();
}

void PcapCaptureActivity::onExit() {
  Activity::onExit();
  stopCapture();
  activeCapture = nullptr;
}

void PcapCaptureActivity::writePcapHeader() {
  // PCAP global header
  struct {
    uint32_t magic = 0xa1b2c3d4;
    uint16_t version_major = 2;
    uint16_t version_minor = 4;
    int32_t thiszone = 0;
    uint32_t sigfigs = 0;
    uint32_t snaplen = 65535;
    uint32_t network = 105;  // IEEE 802.11
  } __attribute__((packed)) header;

  pcapFile.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  fileSize = sizeof(header);
}

void PcapCaptureActivity::writePacket(const uint8_t* data, uint16_t len) {
  // PCAP packet header
  uint32_t ts = millis() / 1000;
  uint32_t ts_usec = (millis() % 1000) * 1000;

  struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
  } __attribute__((packed)) pktHeader = {ts, ts_usec, len, len};

  pcapFile.write(reinterpret_cast<const uint8_t*>(&pktHeader), sizeof(pktHeader));
  pcapFile.write(data, len);
  fileSize = fileSize + sizeof(pktHeader) + len;
  packetsSaved = packetsSaved + 1;
}

void PcapCaptureActivity::startCapture() {
  Storage.mkdir("/biscuit");
  Storage.mkdir("/biscuit/pcap");

  char filename[64];
  snprintf(filename, sizeof(filename), "/biscuit/pcap/capture_%lu.pcap", millis());

  pcapFile = Storage.open(filename, O_WRITE | O_CREAT | O_TRUNC);
  if (!pcapFile) {
    LOG_ERR("PCAP", "Failed to open %s", filename);
    state = DONE;
    requestUpdate();
    return;
  }

  fileOpen = true;
  writePcapHeader();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(pcapPromiscuousCallback);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  state = CAPTURING;
  lastHopTime = millis();
  lastUpdateTime = millis();
  LOG_DBG("PCAP", "Capture started, mode=%d", captureMode);
}

void PcapCaptureActivity::stopCapture() {
  // Disable promiscuous first to stop callbacks before closing file
  esp_wifi_set_promiscuous(false);

  portENTER_CRITICAL(&fileMux);
  if (fileOpen) {
    pcapFile.flush();
    pcapFile.close();
    fileOpen = false;
  }
  portEXIT_CRITICAL(&fileMux);
  LOG_DBG("PCAP", "Capture stopped, %u packets, %u bytes", packetsSaved, fileSize);
}

void PcapCaptureActivity::loop() {
  if (state == MODE_SELECT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      modeIndex = 1 - modeIndex;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      captureMode = (modeIndex == 0) ? ALL_PACKETS : EAPOL_ONLY;
      startCapture();
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == CAPTURING) {
    unsigned long now = millis();

    // Auto channel hop
    if (autoHop && (now - lastHopTime >= 500)) {
      lastHopTime = now;
      currentChannel = (currentChannel % 13) + 1;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }

    // Update display
    if (now - lastUpdateTime >= 2000) {
      lastUpdateTime = now;
      portENTER_CRITICAL(&fileMux);
      pcapFile.flush();
      portEXIT_CRITICAL(&fileMux);
      requestUpdate();
    }

    // Manual channel
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      autoHop = false;
      if (currentChannel > 1) currentChannel--;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      autoHop = false;
      if (currentChannel < 13) currentChannel++;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      stopCapture();
      state = DONE;
      requestUpdate();
    }
    return;
  }

  // DONE
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void PcapCaptureActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PCAP_CAPTURE));

  if (state == MODE_SELECT) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    const char* modes[] = {tr(STR_ALL_PACKETS), tr(STR_EAPOL_ONLY)};
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, 2, modeIndex,
        [&modes](int i) -> std::string { return modes[i]; }, nullptr);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int leftPad = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + 30;
  const int lineH = 40;

  std::string chStr = std::string(tr(STR_CHANNEL)) + " " + std::to_string(currentChannel) +
                      (autoHop ? " (auto)" : "");
  renderer.drawText(UI_10_FONT_ID, leftPad, y, chStr.c_str());
  y += lineH;

  std::string pktStr = std::string(tr(STR_PACKETS_SENT)) + ": " + std::to_string(packetsSaved);
  renderer.drawText(UI_10_FONT_ID, leftPad, y, pktStr.c_str(), true, EpdFontFamily::BOLD);
  y += lineH;

  std::string sizeStr = std::string(tr(STR_FILE_SIZE)) + ": " + std::to_string(fileSize / 1024) + " KB";
  renderer.drawText(UI_10_FONT_ID, leftPad, y, sizeStr.c_str());
  y += lineH;

  std::string modeStr = captureMode == EAPOL_ONLY ? "EAPOL Only" : "All Packets";
  renderer.drawText(UI_10_FONT_ID, leftPad, y, modeStr.c_str());
  y += lineH;

  if (eapolFound) {
    renderer.drawText(UI_10_FONT_ID, leftPad, y, tr(STR_EAPOL_FOUND), true, EpdFontFamily::BOLD);
  }

  if (state == DONE) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, tr(STR_CAPTURE_SAVED));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "Ch-", "Ch+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
