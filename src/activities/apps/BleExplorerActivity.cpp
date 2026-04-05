#include "BleExplorerActivity.h"
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <algorithm>
#include <string>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---- lifecycle ----

void BleExplorerActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING;
  devices.clear();
  services.clear();
  characteristics.clear();
  selectorIndex = 0;
  selectedDevice = -1;
  selectedService = -1;
  selectedChar = -1;
  connected = false;
  pClient = nullptr;
  RADIO.ensureBle();
  scanInitialized = true;
  startBleScan();
  requestUpdate();
}

void BleExplorerActivity::onExit() {
  Activity::onExit();
  disconnect();
  stopBleScan();
  if (scanInitialized) {
    RADIO.shutdown();
    scanInitialized = false;
  }
}

// ---- BLE scan helpers ----

void BleExplorerActivity::startBleScan() {
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->start(3, false);
  lastScanTime = millis();
}

void BleExplorerActivity::stopBleScan() {
  BLEDevice::getScan()->stop();
}

// ---- connection helpers ----

void BleExplorerActivity::connectToDevice(int index) {
  if (index < 0 || index >= static_cast<int>(devices.size())) return;
  selectedDevice = index;
  state = CONNECTING;
  requestUpdate(true);

  stopBleScan();

  pClient = BLEDevice::createClient();
  connected = pClient->connect(devices[index].address);

  if (connected) {
    enumerateServices();
    state = SERVICES;
    selectorIndex = 0;
  } else {
    delete pClient;
    pClient = nullptr;
    state = DEVICE_LIST;
  }
  requestUpdate();
}

void BleExplorerActivity::disconnect() {
  if (pClient) {
    if (connected) pClient->disconnect();
    delete pClient;
    pClient = nullptr;
  }
  connected = false;
  services.clear();
  characteristics.clear();
}

// ---- service / characteristic enumeration ----

void BleExplorerActivity::enumerateServices() {
  services.clear();
  if (!pClient || !connected) return;

  auto* svcMap = pClient->getServices();
  if (!svcMap) return;

  int count = 0;
  for (auto& pair : *svcMap) {
    if (count >= 20) break;
    ServiceInfo info;
    info.uuid = pair.first;
    const char* resolved = resolveServiceName(pair.first);
    if (resolved) {
      info.name = resolved;
    } else {
      // Use short UUID if long enough, else full UUID
      info.name = (pair.first.size() >= 8) ? pair.first.substr(4, 4) : pair.first;
    }
    auto* charMap = pair.second->getCharacteristics();
    info.charCount = charMap ? static_cast<int>(charMap->size()) : 0;
    services.push_back(std::move(info));
    count++;
  }
}

void BleExplorerActivity::enumerateCharacteristics(int serviceIndex) {
  characteristics.clear();
  if (!pClient || !connected || serviceIndex < 0) return;

  auto* svcMap = pClient->getServices();
  if (!svcMap) return;

  // Iterate to the service at serviceIndex
  int si = 0;
  for (auto& spair : *svcMap) {
    if (si == serviceIndex) {
      auto* charMap = spair.second->getCharacteristics();
      if (!charMap) return;
      int ci = 0;
      for (auto& cpair : *charMap) {
        if (ci >= 30) break;
        CharInfo info;
        info.uuid = cpair.first;
        const char* resolved = resolveCharName(cpair.first);
        info.name = resolved ? resolved : cpair.first;
        info.canRead   = cpair.second->canRead();
        info.canWrite  = cpair.second->canWrite() || cpair.second->canWriteNoResponse();
        info.canNotify = cpair.second->canNotify();
        info.properties = 0;
        if (info.canRead)   info.properties |= 0x02;
        if (info.canWrite)  info.properties |= 0x08;
        if (info.canNotify) info.properties |= 0x10;
        if (info.canRead) {
          String val = cpair.second->readValue();
          if (val.length() > 0) {
            std::string hex = bytesToHex(
                reinterpret_cast<const uint8_t*>(val.c_str()),
                static_cast<int>(val.length()));
            std::string ascii = bytesToAsciiSafe(
                reinterpret_cast<const uint8_t*>(val.c_str()),
                static_cast<int>(val.length()));
            info.value = hex;
            if (!ascii.empty()) {
              info.value += " (";
              info.value += ascii;
              info.value += ")";
            }
          } else {
            info.value = "(empty)";
          }
        } else {
          info.value = "(not readable)";
        }
        characteristics.push_back(std::move(info));
        ci++;
      }
      return;
    }
    si++;
  }
}

void BleExplorerActivity::readCharacteristic(int charIndex) {
  // Re-enumerate the selected service; this re-reads all chars including charIndex.
  enumerateCharacteristics(selectedService);
}

// ---- static helpers ----

const char* BleExplorerActivity::resolveServiceName(const std::string& uuid) {
  if (uuid.size() < 8) return nullptr;
  std::string s = uuid.substr(4, 4);
  // Compare lower-case (BLE UUIDs come lowercase from the stack)
  if (s == "1800") return "Generic Access";
  if (s == "1801") return "Generic Attribute";
  if (s == "180a") return "Device Information";
  if (s == "180f") return "Battery Service";
  if (s == "1812") return "HID Service";
  if (s == "181a") return "Environmental Sensing";
  if (s == "181c") return "User Data";
  if (s == "1810") return "Blood Pressure";
  if (s == "1808") return "Glucose";
  if (s == "1809") return "Health Thermometer";
  if (s == "180d") return "Heart Rate";
  if (s == "fee0") return "Vendor (Xiaomi)";
  if (s == "fe95") return "Vendor (Xiaomi Mi)";
  return nullptr;
}

const char* BleExplorerActivity::resolveCharName(const std::string& uuid) {
  if (uuid.size() < 8) return nullptr;
  std::string s = uuid.substr(4, 4);
  if (s == "2a00") return "Device Name";
  if (s == "2a01") return "Appearance";
  if (s == "2a19") return "Battery Level";
  if (s == "2a24") return "Model Number";
  if (s == "2a25") return "Serial Number";
  if (s == "2a26") return "Firmware Rev";
  if (s == "2a27") return "Hardware Rev";
  if (s == "2a28") return "Software Rev";
  if (s == "2a29") return "Manufacturer";
  if (s == "2a37") return "Heart Rate Meas.";
  if (s == "2a6e") return "Temperature";
  if (s == "2a6f") return "Humidity";
  return nullptr;
}

std::string BleExplorerActivity::propertiesToString(uint8_t props) {
  std::string result;
  if (props & 0x02)        result += "R";
  if (props & (0x08|0x04)) result += "W";
  if (props & 0x10)        result += "N";
  if (props & 0x20)        result += "I";
  return result;
}

std::string BleExplorerActivity::bytesToHex(const uint8_t* data, int len) {
  char buf[4];
  std::string result;
  result.reserve(len <= 16 ? len * 3 : 16 * 3 + 3);
  int limit = len > 16 ? 16 : len;
  for (int i = 0; i < limit; i++) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    result += buf;
    if (i + 1 < limit) result += ' ';
  }
  if (len > 16) result += "...";
  return result;
}

std::string BleExplorerActivity::bytesToAsciiSafe(const uint8_t* data, int len) {
  for (int i = 0; i < len; i++) {
    if (data[i] < 32 || data[i] > 126) return {};
  }
  return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
}

// ---- loop ----

void BleExplorerActivity::loop() {
  if (state == SCANNING) {
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults* results = scan->getResults();
    int count = results ? results->getCount() : 0;

    bool timedOut = (millis() - lastScanTime) > 4000;
    if (count > 0 || timedOut) {
      // Merge results
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        std::string mac = dev.getAddress().toString().c_str();

        auto it = std::find_if(devices.begin(), devices.end(),
                               [&mac](const BleTarget& d) { return d.mac == mac; });
        if (it != devices.end()) {
          it->rssi = dev.getRSSI();
          if (dev.haveName() && it->name == "Unknown") {
            it->name = dev.getName().c_str();
          }
        } else if (static_cast<int>(devices.size()) < 50) {
          BleTarget t;
          t.name    = dev.haveName() ? dev.getName().c_str() : "Unknown";
          t.mac     = mac;
          t.rssi    = dev.getRSSI();
          t.address = dev.getAddress();
          devices.push_back(std::move(t));
        }
      }

      // Sort by RSSI descending
      std::sort(devices.begin(), devices.end(),
                [](const BleTarget& a, const BleTarget& b) { return a.rssi > b.rssi; });

      scan->clearResults();

      if (!devices.empty()) {
        state = DEVICE_LIST;
        selectorIndex = 0;
        requestUpdate();
      } else {
        // No devices found, keep scanning
        startBleScan();
        requestUpdate();
      }
    }

    // Navigation allowed during scan
    const int devCount = static_cast<int>(devices.size());
    buttonNavigator.onNext([this, devCount] {
      if (devCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, devCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, devCount] {
      if (devCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, devCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); }
    return;
  }

  if (state == DEVICE_LIST) {
    const int devCount = static_cast<int>(devices.size());

    buttonNavigator.onNext([this, devCount] {
      if (devCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, devCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, devCount] {
      if (devCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, devCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!devices.empty()) {
        connectToDevice(selectorIndex);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); }
    return;
  }

  if (state == CONNECTING) {
    // connectToDevice is synchronous; we should never linger here
    return;
  }

  if (state == SERVICES) {
    const int svcCount = static_cast<int>(services.size());

    buttonNavigator.onNext([this, svcCount] {
      if (svcCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, svcCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, svcCount] {
      if (svcCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, svcCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!services.empty()) {
        selectedService = selectorIndex;
        enumerateCharacteristics(selectorIndex);
        state = CHARACTERISTICS;
        selectorIndex = 0;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      disconnect();
      state = DEVICE_LIST;
      selectorIndex = selectedDevice >= 0 ? selectedDevice : 0;
      startBleScan();
      requestUpdate();
    }
    return;
  }

  if (state == CHARACTERISTICS) {
    const int charCount = static_cast<int>(characteristics.size());

    buttonNavigator.onNext([this, charCount] {
      if (charCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, charCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, charCount] {
      if (charCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, charCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!characteristics.empty()) {
        selectedChar = selectorIndex;
        if (characteristics[selectorIndex].canRead) {
          readCharacteristic(selectorIndex);
          // After re-enumerate, restore selection
          if (selectedChar < static_cast<int>(characteristics.size())) {
            selectorIndex = selectedChar;
          }
        }
        state = CHAR_VALUE;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = SERVICES;
      selectorIndex = selectedService >= 0 ? selectedService : 0;
      requestUpdate();
    }
    return;
  }

  if (state == CHAR_VALUE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Re-read button
      if (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size())) {
        readCharacteristic(selectedChar);
        if (selectedChar < static_cast<int>(characteristics.size())) {
          selectorIndex = selectedChar;
        }
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = CHARACTERISTICS;
      selectorIndex = selectedChar >= 0 ? selectedChar : 0;
      requestUpdate();
    }
    return;
  }
}

// ---- render ----

void BleExplorerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state == SCANNING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "BLE Explorer");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning for devices...");
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == DEVICE_LIST) {
    char subtitle[24];
    snprintf(subtitle, sizeof(subtitle), "%d devices", static_cast<int>(devices.size()));
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "BLE Explorer", subtitle);

    const int contentTop    = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int devCount      = static_cast<int>(devices.size());

    if (devCount > 0) {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, devCount, selectorIndex,
          [this](int index) -> std::string { return devices[index].name; },
          [this](int index) -> std::string {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s  %d dBm",
                     devices[index].mac.c_str(), static_cast<int>(devices[index].rssi));
            return buf;
          });
    }

    const auto labels = mappedInput.mapLabels("Back", "Connect", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CONNECTING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "BLE Explorer");
    char msg[64];
    const char* devName = (selectedDevice >= 0 && selectedDevice < static_cast<int>(devices.size()))
                              ? devices[selectedDevice].name.c_str()
                              : "device";
    snprintf(msg, sizeof(msg), "Connecting to %s...", devName);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, msg);
    renderer.displayBuffer();
    return;
  }

  if (state == SERVICES) {
    const char* devName = (selectedDevice >= 0 && selectedDevice < static_cast<int>(devices.size()))
                              ? devices[selectedDevice].name.c_str()
                              : "Device";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   devName, "Services");

    const int contentTop    = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int svcCount      = static_cast<int>(services.size());

    if (svcCount > 0) {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, svcCount, selectorIndex,
          [this](int index) -> std::string { return services[index].name; },
          [this](int index) -> std::string {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d characteristics", services[index].charCount);
            return buf;
          });
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No services found");
    }

    const auto labels = mappedInput.mapLabels("Discon.", "Explore", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CHARACTERISTICS) {
    const char* svcName = (selectedService >= 0 && selectedService < static_cast<int>(services.size()))
                              ? services[selectedService].name.c_str()
                              : "Service";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   svcName, "Characteristics");

    const int contentTop    = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int charCount     = static_cast<int>(characteristics.size());

    if (charCount > 0) {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, charCount, selectorIndex,
          [this](int index) -> std::string {
            std::string title = characteristics[index].name;
            title += " [";
            title += propertiesToString(characteristics[index].properties);
            title += "]";
            return title;
          },
          [this](int index) -> std::string {
            const std::string& val = characteristics[index].value;
            // Truncate long values for subtitle
            if (val.size() > 40) {
              return val.substr(0, 37) + "...";
            }
            return val;
          });
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No characteristics");
    }

    const auto labels = mappedInput.mapLabels("Back", "Read", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CHAR_VALUE) {
    const char* charName = (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size()))
                               ? characteristics[selectedChar].name.c_str()
                               : "Characteristic";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   charName);

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
    const int lineH = 45;

    if (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size())) {
      const CharInfo& ch = characteristics[selectedChar];

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "UUID", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, ch.uuid.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Properties", true, EpdFontFamily::BOLD);
      y += 22;
      std::string props = propertiesToString(ch.properties);
      if (ch.canRead)   props += "  Read";
      if (ch.canWrite)  props += "  Write";
      if (ch.canNotify) props += "  Notify";
      renderer.drawText(UI_10_FONT_ID, leftPad, y, props.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Value (hex)", true, EpdFontFamily::BOLD);
      y += 22;
      // Show only hex portion before any '(' for this field
      std::string hexPart = ch.value;
      auto parenPos = ch.value.find(" (");
      if (parenPos != std::string::npos) hexPart = ch.value.substr(0, parenPos);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, hexPart.c_str());
      y += lineH;

      // ASCII field — only if value contains a parenthesized portion
      if (parenPos != std::string::npos && ch.value.size() > parenPos + 2) {
        std::string asciiPart = ch.value.substr(parenPos + 2);
        if (!asciiPart.empty() && asciiPart.back() == ')') asciiPart.pop_back();
        renderer.drawText(SMALL_FONT_ID, leftPad, y, "Value (ASCII)", true, EpdFontFamily::BOLD);
        y += 22;
        renderer.drawText(UI_10_FONT_ID, leftPad, y, asciiPart.c_str());
      }
    }

    const auto labels = mappedInput.mapLabels("Back", "Re-read", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.displayBuffer();
}
