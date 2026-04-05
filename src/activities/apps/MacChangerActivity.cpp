#include "MacChangerActivity.h"

#include <WiFi.h>
#include <BLEDevice.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>

// NimBLE host API for setting a random static BLE address.
// ble_hs_id_set_rnd() returns 0 on success.
extern "C" int ble_hs_id_set_rnd(const uint8_t *rnd_addr);

#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MacChangerActivity::onEnter() {
  Activity::onEnter();
  selectedMode = WIFI_MAC;
  wifiRandomized = false;
  bleRandomized = false;
  statusMessage.clear();
  readCurrentMacs();
  requestUpdate();
}

void MacChangerActivity::onExit() {
  // Restore WiFi MAC automatically on exit if it was randomized
  if (wifiRandomized) {
    RADIO.ensureWifi();
    esp_wifi_set_mac(WIFI_IF_STA, originalWifiMac);
    RADIO.shutdown();
    wifiRandomized = false;
  }
  Activity::onExit();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void MacChangerActivity::readCurrentMacs() {
  // Read from eFuse — no radio init required
  esp_read_mac(originalWifiMac, ESP_MAC_WIFI_STA);
  esp_read_mac(originalBleMac, ESP_MAC_BT);
  memcpy(currentWifiMac, originalWifiMac, 6);
  memcpy(currentBleMac, originalBleMac, 6);
}

void MacChangerActivity::randomizeWifiMac() {
  uint8_t newMac[6];
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  newMac[0] = static_cast<uint8_t>(r1 >> 24);
  newMac[1] = static_cast<uint8_t>(r1 >> 16);
  newMac[2] = static_cast<uint8_t>(r1 >> 8);
  newMac[3] = static_cast<uint8_t>(r1);
  newMac[4] = static_cast<uint8_t>(r2 >> 24);
  newMac[5] = static_cast<uint8_t>(r2 >> 16);
  // Locally administered, unicast
  newMac[0] = (newMac[0] | 0x02) & 0xFE;

  RADIO.ensureWifi();
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, newMac);
  RADIO.shutdown();

  if (err == ESP_OK) {
    memcpy(currentWifiMac, newMac, 6);
    wifiRandomized = true;
    statusMessage = "WiFi MAC randomized!";
  } else {
    statusMessage = "WiFi MAC change failed";
  }
}

void MacChangerActivity::randomizeBleMac() {
  uint8_t newMac[6];
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  newMac[0] = static_cast<uint8_t>(r1 >> 24);
  newMac[1] = static_cast<uint8_t>(r1 >> 16);
  newMac[2] = static_cast<uint8_t>(r1 >> 8);
  newMac[3] = static_cast<uint8_t>(r1);
  newMac[4] = static_cast<uint8_t>(r2 >> 24);
  newMac[5] = static_cast<uint8_t>(r2 >> 16);
  // Random static address: top two bits set
  newMac[0] |= 0xC0;

  RADIO.ensureBle();
  int err = ble_hs_id_set_rnd(newMac);
  RADIO.shutdown();

  if (err == 0) {
    memcpy(currentBleMac, newMac, 6);
    bleRandomized = true;
    statusMessage = "BLE MAC randomized!";
  } else {
    statusMessage = "BLE MAC change failed";
  }
}

void MacChangerActivity::restoreOriginalMac() {
  if (selectedMode == WIFI_MAC) {
    if (wifiRandomized) {
      RADIO.ensureWifi();
      esp_wifi_set_mac(WIFI_IF_STA, originalWifiMac);
      RADIO.shutdown();
      memcpy(currentWifiMac, originalWifiMac, 6);
      wifiRandomized = false;
    }
    statusMessage = "WiFi MAC restored";
  } else {
    // BLE random address resets on reboot; just update display state
    memcpy(currentBleMac, originalBleMac, 6);
    bleRandomized = false;
    statusMessage = "BLE resets on reboot";
  }
}

// static
std::string MacChangerActivity::macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void MacChangerActivity::loop() {
  // Up / Down: toggle selected mode
  buttonNavigator.onNext([this] {
    selectedMode = (selectedMode == WIFI_MAC) ? BLE_MAC : WIFI_MAC;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedMode = (selectedMode == WIFI_MAC) ? BLE_MAC : WIFI_MAC;
    requestUpdate();
  });

  // Confirm: randomize selected MAC
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedMode == WIFI_MAC) {
      randomizeWifiMac();
    } else {
      randomizeBleMac();
    }
    requestUpdate();
    return;
  }

  // PageForward (side button): restore original MAC for selected mode
  if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    restoreOriginalMac();
    requestUpdate();
    return;
  }

  // Back: restore both MACs, then exit
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Restore WiFi if randomized (BLE resets on reboot automatically)
    if (wifiRandomized) {
      RADIO.ensureWifi();
      esp_wifi_set_mac(WIFI_IF_STA, originalWifiMac);
      RADIO.shutdown();
      wifiRandomized = false;
    }
    bleRandomized = false;
    finish();
  }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void MacChangerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "MAC Changer");

  const int leftPad = metrics.contentSidePadding;
  const int sectionW = pageWidth - leftPad * 2;

  // Compute section tops — two equal sections in the content area
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int hintsAreaTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentH = hintsAreaTop - contentTop;
  // Reserve bottom ~40px of content area for status message
  const int statusLineH = 40;
  const int sectionsH = contentH - statusLineH;
  const int sectionH = sectionsH / 2;

  // Row offsets within a section
  const int labelOffsetY = 8;
  const int macOffsetY = labelOffsetY + 28;
  const int statusOffsetY = macOffsetY + 38;

  struct SectionInfo {
    Mode mode;
    const char* label;
    const uint8_t* mac;
    bool randomized;
  };

  const SectionInfo sections[2] = {
      {WIFI_MAC, "WiFi MAC", currentWifiMac, wifiRandomized},
      {BLE_MAC,  "BLE MAC",  currentBleMac,  bleRandomized},
  };

  for (int i = 0; i < 2; i++) {
    const auto& sec = sections[i];
    const int sectionTop = contentTop + i * sectionH;
    const bool selected = (sec.mode == selectedMode);

    // Highlight selected section
    if (selected) {
      renderer.fillRect(0, sectionTop, pageWidth, sectionH, true);
    }

    const bool inv = !selected;  // white text on black background when selected
    const int textLeft = leftPad;

    // Section label (e.g. "WiFi MAC")
    renderer.drawText(SMALL_FONT_ID, textLeft, sectionTop + labelOffsetY,
                      sec.label, inv, EpdFontFamily::BOLD);

    // Current MAC address
    std::string macStr = macToString(sec.mac);
    renderer.drawText(UI_12_FONT_ID, textLeft, sectionTop + macOffsetY,
                      macStr.c_str(), inv, EpdFontFamily::BOLD);

    // Status annotation: "(original)" or "(randomized)"
    const char* annotation = sec.randomized ? "(randomized)" : "(original)";
    renderer.drawText(UI_10_FONT_ID, textLeft, sectionTop + statusOffsetY,
                      annotation, inv);

    // Divider line between sections (only after first)
    if (i == 0) {
      const int divY = sectionTop + sectionH - 1;
      renderer.fillRect(leftPad, divY, sectionW, 1, true);
    }
  }

  // Status message at bottom of content area
  if (!statusMessage.empty()) {
    const int msgY = contentTop + sectionsH + 6;
    renderer.drawCenteredText(UI_10_FONT_ID, msgY, statusMessage.c_str());
  }

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Randomize", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "", "Restore");

  renderer.displayBuffer();
}
