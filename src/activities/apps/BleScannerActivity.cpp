#include "BleScannerActivity.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Static pointer for callback context
static BleScannerActivity* activeScanner = nullptr;

class BleScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!activeScanner) return;
    // Handled in main loop via BLEScan results
  }
};

void BleScannerActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING_VIEW;
  devices.clear();
  selectorIndex = 0;
  scanning = false;
  scanInitialized = false;

  // Ensure WiFi is off before BLE
  WiFi.mode(WIFI_OFF);
  delay(100);

  BLEDevice::init("biscuit");
  scanInitialized = true;
  activeScanner = this;
  startBleScan();
  requestUpdate();
}

void BleScannerActivity::onExit() {
  Activity::onExit();
  stopBleScan();
  activeScanner = nullptr;
  if (scanInitialized) {
    BLEDevice::deinit(false);
    scanInitialized = false;
  }
}

void BleScannerActivity::startBleScan() {
  if (!scanInitialized) return;
  scanning = true;
  lastScanTime = millis();

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->start(3, false);  // 3 second scan, non-blocking=false but short
}

void BleScannerActivity::stopBleScan() {
  if (!scanInitialized) return;
  BLEScan* scan = BLEDevice::getScan();
  scan->stop();
  scanning = false;
}

void BleScannerActivity::saveToCsv() {
  Storage.mkdir("/biscuit");
  Storage.mkdir("/biscuit/logs");
  char filename[64];
  snprintf(filename, sizeof(filename), "/biscuit/logs/ble_scan_%lu.csv", millis());

  String csv = "Name,MAC,RSSI\n";
  for (const auto& dev : devices) {
    csv += String(dev.name.c_str()) + "," + dev.mac.c_str() + "," + String(dev.rssi) + "\n";
  }
  Storage.writeFile(filename, csv);
  LOG_DBG("BSCAN", "Saved %zu devices to %s", devices.size(), filename);
}

void BleScannerActivity::loop() {
  if (state == DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = SCANNING_VIEW;
      requestUpdate();
    }
    return;
  }

  // Check if scan completed and collect results
  if (scanning && scanInitialized) {
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults* results = scan->getResults();
    int count = results ? results->getCount() : 0;

    if (count > 0 || (millis() - lastScanTime > 4000)) {
      // Merge new results
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        std::string mac = dev.getAddress().toString().c_str();

        // Check if already in list
        auto it = std::find_if(devices.begin(), devices.end(),
                               [&mac](const BleDevice& d) { return d.mac == mac; });
        if (it != devices.end()) {
          it->rssi = dev.getRSSI();
          if (dev.haveName() && it->name == "Unknown") {
            it->name = dev.getName().c_str();
          }
        } else {
          BleDevice newDev;
          newDev.name = dev.haveName() ? dev.getName().c_str() : "Unknown";
          newDev.mac = mac;
          newDev.rssi = dev.getRSSI();
          devices.push_back(std::move(newDev));
        }
      }

      // Sort by RSSI
      std::sort(devices.begin(), devices.end(),
                [](const BleDevice& a, const BleDevice& b) { return a.rssi > b.rssi; });

      scan->clearResults();
      requestUpdate();

      // Restart scan
      lastScanTime = millis();
      scan->start(3, false);
    }
  }

  // Navigation
  const int count = static_cast<int>(devices.size());

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
      saveToCsv();
    } else if (!devices.empty()) {
      detailIndex = selectorIndex;
      state = DETAIL;
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void BleScannerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state == DETAIL && detailIndex < static_cast<int>(devices.size())) {
    const auto& dev = devices[detailIndex];
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_SCANNER));

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
    const int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Name", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, dev.name.c_str());
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "MAC", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, dev.mac.c_str());
    y += lineH;

    std::string rssiStr = std::to_string(dev.rssi) + " dBm";
    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_RSSI), true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, rssiStr.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // SCANNING_VIEW
  std::string subtitle = std::to_string(devices.size()) + " devices";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_SCANNER),
                 subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (devices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SCANNING_BLE));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(devices.size()), selectorIndex,
        [this](int index) -> std::string { return devices[index].name; },
        [this](int index) -> std::string {
          return devices[index].mac + "  " + std::to_string(devices[index].rssi) + "dBm";
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, "Hold: CSV", labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
