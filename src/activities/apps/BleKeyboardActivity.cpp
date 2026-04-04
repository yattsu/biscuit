#include "BleKeyboardActivity.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HIDKeyboardTypes.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// HID Report Descriptor for a standard keyboard
static const uint8_t HID_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) - Modifier byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant) - Reserved byte
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) - Key arrays (6 keys)
    0xC0         // End Collection
};

class BleKeyboardActivity::ServerCallbacks : public BLEServerCallbacks {
  BleKeyboardActivity& activity;

 public:
  explicit ServerCallbacks(BleKeyboardActivity& act) : activity(act) {}

  void onConnect(BLEServer* pServer) override {
    activity.deviceConnected = true;
    activity.state = PAIRED;
    activity.requestUpdate();
    LOG_DBG("BadBLE", "Device connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    activity.deviceConnected = false;
    if (activity.state == EXECUTING) {
      activity.state = DONE;
    }
    activity.requestUpdate();
    LOG_DBG("BadBLE", "Device disconnected");
  }
};

void BleKeyboardActivity::onEnter() {
  Activity::onEnter();
  RADIO.ensureBle();

  state = SELECT_SCRIPT;
  selectedIndex = 0;
  currentLine = 0;
  deviceConnected = false;
  delayUntil = 0;
  repeatCount = 0;
  pServer = nullptr;
  pHid = nullptr;
  pInputChar = nullptr;

  {
    RenderLock lock(*this);
    loadScriptList();
  }

  requestUpdate();
}

void BleKeyboardActivity::onExit() {
  Activity::onExit();
  stopAdvertising();
  RADIO.shutdown();
}

void BleKeyboardActivity::loadScriptList() {
  scriptFiles.clear();

  auto dir = Storage.open(DUCKY_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    LOG_DBG("BadBLE", "No ducky script directory found at %s", DUCKY_DIR);
    return;
  }

  dir.rewindDirectory();
  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    std::string_view filename(name);
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".txt") {
      scriptFiles.emplace_back(name);
    }
    file.close();
  }
  dir.close();

  std::sort(scriptFiles.begin(), scriptFiles.end());
}

void BleKeyboardActivity::loadScriptContent(const std::string& path) {
  scriptLines.clear();
  std::string fullPath = std::string(DUCKY_DIR) + path;

  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("BadBLE", "Failed to open script: %s", fullPath.c_str());
    return;
  }

  char lineBuf[512];
  while (file.available()) {
    int len = 0;
    char c;
    while (file.available() && len < 511) {
      c = file.read();
      if (c == '\n') break;
      if (c != '\r') {
        lineBuf[len++] = c;
      }
    }
    lineBuf[len] = '\0';
    if (len > 0) {
      scriptLines.emplace_back(lineBuf);
    }
  }
  file.close();
}

void BleKeyboardActivity::startAdvertising() {
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks(*this));

  pHid = new BLEHIDDevice(pServer);

  pHid->manufacturer()->setValue("biscuit");
  pHid->pnp(0x02, 0x05AC, 0x820A, 0x0210);
  pHid->hidInfo(0x00, 0x01);

  pHid->reportMap(const_cast<uint8_t*>(HID_REPORT_DESCRIPTOR), sizeof(HID_REPORT_DESCRIPTOR));

  pInputChar = pHid->inputReport(1);

  pHid->startServices();

  BLEAdvertising* pAdv = pServer->getAdvertising();
  pAdv->setAppearance(0x03C1);  // Keyboard
  pAdv->addServiceUUID(pHid->hidService()->getUUID());
  pAdv->start();

  LOG_DBG("BadBLE", "BLE HID keyboard advertising as '%s'", BLE_DEVICE_NAME);
}

void BleKeyboardActivity::stopAdvertising() {
  if (pServer) {
    pServer->getAdvertising()->stop();
  }
  deviceConnected = false;
}

void BleKeyboardActivity::sendKey(uint8_t keyCode, uint8_t modifiers) {
  if (!pInputChar || !deviceConnected) return;

  // Report format: [modifier, reserved, key1, key2, key3, key4, key5, key6]
  uint8_t report[8] = {0};
  report[0] = modifiers;
  report[2] = keyCode;
  pInputChar->setValue(report, sizeof(report));
  pInputChar->notify();

  delay(15);
  releaseKeys();
}

void BleKeyboardActivity::sendKeyCombo(uint8_t modifiers, uint8_t keyCode) {
  sendKey(keyCode, modifiers);
}

void BleKeyboardActivity::releaseKeys() {
  if (!pInputChar || !deviceConnected) return;

  uint8_t report[8] = {0};
  pInputChar->setValue(report, sizeof(report));
  pInputChar->notify();
  delay(15);
}

void BleKeyboardActivity::sendString(const std::string& text) {
  for (char c : text) {
    uint8_t keyCode = charToKeyCode(c);
    uint8_t modifier = charToModifier(c);
    if (keyCode != 0) {
      sendKey(keyCode, modifier);
    }
  }
}

uint8_t BleKeyboardActivity::charToKeyCode(char c) {
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A');
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;

  switch (c) {
    case '\n':
    case '\r':
      return 0x28;  // Enter
    case '\t':
      return 0x2B;  // Tab
    case ' ':
      return 0x2C;  // Space
    case '-':
      return 0x2D;
    case '=':
      return 0x2E;
    case '[':
      return 0x2F;
    case ']':
      return 0x30;
    case '\\':
      return 0x31;
    case ';':
      return 0x33;
    case '\'':
      return 0x34;
    case '`':
      return 0x35;
    case ',':
      return 0x36;
    case '.':
      return 0x37;
    case '/':
      return 0x38;
    case '!':
      return 0x1E;
    case '@':
      return 0x1F;
    case '#':
      return 0x20;
    case '$':
      return 0x21;
    case '%':
      return 0x22;
    case '^':
      return 0x23;
    case '&':
      return 0x24;
    case '*':
      return 0x25;
    case '(':
      return 0x26;
    case ')':
      return 0x27;
    case '_':
      return 0x2D;
    case '+':
      return 0x2E;
    case '{':
      return 0x2F;
    case '}':
      return 0x30;
    case '|':
      return 0x31;
    case ':':
      return 0x33;
    case '"':
      return 0x34;
    case '~':
      return 0x35;
    case '<':
      return 0x36;
    case '>':
      return 0x37;
    case '?':
      return 0x38;
    default:
      return 0;
  }
}

uint8_t BleKeyboardActivity::charToModifier(char c) {
  if (c >= 'A' && c <= 'Z') return 0x02;  // Left Shift

  switch (c) {
    case '!':
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')':
    case '_':
    case '+':
    case '{':
    case '}':
    case '|':
    case ':':
    case '"':
    case '~':
    case '<':
    case '>':
    case '?':
      return 0x02;  // Left Shift
    default:
      return 0;
  }
}

uint8_t BleKeyboardActivity::specialKeyCode(const std::string& keyName) {
  if (keyName == "ENTER") return 0x28;
  if (keyName == "TAB") return 0x2B;
  if (keyName == "ESCAPE" || keyName == "ESC") return 0x29;
  if (keyName == "BACKSPACE") return 0x2A;
  if (keyName == "DELETE" || keyName == "DEL") return 0x4C;
  if (keyName == "SPACE") return 0x2C;
  if (keyName == "UP") return 0x52;
  if (keyName == "DOWN") return 0x51;
  if (keyName == "LEFT") return 0x50;
  if (keyName == "RIGHT") return 0x4F;
  if (keyName == "F1") return 0x3A;
  if (keyName == "F2") return 0x3B;
  if (keyName == "F3") return 0x3C;
  if (keyName == "F4") return 0x3D;
  if (keyName == "F5") return 0x3E;
  if (keyName == "F6") return 0x3F;
  if (keyName == "F7") return 0x40;
  if (keyName == "F8") return 0x41;
  if (keyName == "F9") return 0x42;
  if (keyName == "F10") return 0x43;
  if (keyName == "F11") return 0x44;
  if (keyName == "F12") return 0x45;
  if (keyName == "CAPSLOCK") return 0x39;
  if (keyName == "PRINTSCREEN") return 0x46;
  if (keyName == "SCROLLLOCK") return 0x47;
  if (keyName == "PAUSE") return 0x48;
  if (keyName == "INSERT") return 0x49;
  if (keyName == "HOME") return 0x4A;
  if (keyName == "PAGEUP") return 0x4B;
  if (keyName == "PAGEDOWN") return 0x4E;
  if (keyName == "END") return 0x4D;
  return 0;
}

uint8_t BleKeyboardActivity::modifierBit(const std::string& modName) {
  if (modName == "GUI" || modName == "WINDOWS" || modName == "COMMAND" || modName == "SUPER") return 0x08;
  if (modName == "ALT") return 0x04;
  if (modName == "CTRL" || modName == "CONTROL") return 0x01;
  if (modName == "SHIFT") return 0x02;
  return 0;
}

void BleKeyboardActivity::executeLine(const std::string& line) {
  if (line.empty()) return;

  // REM = comment, skip
  if (line.substr(0, 3) == "REM") return;

  // STRING text
  if (line.size() > 7 && line.substr(0, 7) == "STRING ") {
    sendString(line.substr(7));
    lastCommand = line;
    return;
  }

  // DELAY ms
  if (line.size() > 6 && line.substr(0, 6) == "DELAY ") {
    unsigned long ms = strtoul(line.substr(6).c_str(), nullptr, 10);
    delayUntil = millis() + ms;
    lastCommand = line;
    return;
  }

  // REPEAT n
  if (line.size() > 7 && line.substr(0, 7) == "REPEAT ") {
    int count = atoi(line.substr(7).c_str());
    if (count > 0 && !lastCommand.empty()) {
      repeatCount = count;
    }
    return;
  }

  // Check if this is a modifier combo (e.g., "GUI r", "CTRL ALT DELETE")
  uint8_t modifiers = 0;
  std::string remaining = line;

  // Parse modifier keys
  while (!remaining.empty()) {
    size_t spacePos = remaining.find(' ');
    std::string token = (spacePos != std::string::npos) ? remaining.substr(0, spacePos) : remaining;

    uint8_t mod = modifierBit(token);
    if (mod != 0) {
      modifiers |= mod;
      remaining = (spacePos != std::string::npos) ? remaining.substr(spacePos + 1) : "";
    } else {
      break;
    }
  }

  // Remaining is either a special key or a single character
  if (!remaining.empty()) {
    uint8_t keyCode = specialKeyCode(remaining);
    if (keyCode != 0) {
      sendKey(keyCode, modifiers);
    } else if (remaining.size() == 1) {
      // Single character key
      uint8_t kc = charToKeyCode(remaining[0]);
      uint8_t km = charToModifier(remaining[0]);
      sendKey(kc, modifiers | km);
    }
  } else if (modifiers != 0) {
    // Modifier-only press (rare but possible)
    sendKey(0, modifiers);
  } else {
    // Try as a plain special key
    uint8_t keyCode = specialKeyCode(line);
    if (keyCode != 0) {
      sendKey(keyCode, 0);
    }
  }

  lastCommand = line;
}

void BleKeyboardActivity::executeCurrentLine() {
  if (currentLine >= static_cast<int>(scriptLines.size())) {
    state = DONE;
    releaseKeys();
    requestUpdate();
    return;
  }

  // Handle REPEAT
  if (repeatCount > 0) {
    executeLine(lastCommand);
    repeatCount--;
    requestUpdate();
    return;
  }

  executeLine(scriptLines[currentLine]);
  currentLine++;
  requestUpdate();
}

void BleKeyboardActivity::loop() {
  if (state == SELECT_SCRIPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !scriptFiles.empty()) {
      selectedScript = scriptFiles[selectedIndex];
      {
        RenderLock lock(*this);
        loadScriptContent(selectedScript);
      }
      state = PREVIEW;
      requestUpdate();
      return;
    }

    int listSize = static_cast<int>(scriptFiles.size());
    buttonNavigator.onNext([this, listSize] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, listSize);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, listSize] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, listSize);
      requestUpdate();
    });
    return;
  }

  if (state == PREVIEW) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = SELECT_SCRIPT;
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = ADVERTISING;
      startAdvertising();
      requestUpdate();
      return;
    }
    return;
  }

  if (state == ADVERTISING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      stopAdvertising();
      state = SELECT_SCRIPT;
      requestUpdate();
      return;
    }
    // deviceConnected is set by ServerCallbacks, which transitions to PAIRED
    return;
  }

  if (state == PAIRED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      stopAdvertising();
      state = SELECT_SCRIPT;
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = EXECUTING;
      currentLine = 0;
      repeatCount = 0;
      delayUntil = 0;
      lastCommand.clear();
      requestUpdate();
      return;
    }
    return;
  }

  if (state == EXECUTING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      stopAdvertising();
      state = DONE;
      requestUpdate();
      return;
    }

    if (!deviceConnected) {
      state = DONE;
      requestUpdate();
      return;
    }

    // Handle delay
    if (delayUntil > 0) {
      if (millis() < delayUntil) {
        return;
      }
      delayUntil = 0;
    }

    executeCurrentLine();
    return;
  }

  if (state == DONE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      stopAdvertising();
      state = SELECT_SCRIPT;
      requestUpdate();
      return;
    }
  }
}

void BleKeyboardActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "BadBLE");

  if (state == SELECT_SCRIPT) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    if (scriptFiles.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "No scripts found");
      renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 15, "Place .txt files in /biscuit/ducky/");
    } else {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(scriptFiles.size()), selectedIndex,
          [this](int index) {
            std::string name = scriptFiles[index];
            // Remove .txt extension for display
            if (name.size() > 4) name = name.substr(0, name.size() - 4);
            return name;
          });
    }

    const auto labels = mappedInput.mapLabels("Back", scriptFiles.empty() ? "" : "Select", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == PREVIEW) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 5;

    // Show script name
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop, selectedScript.c_str());

    // Show first 5 lines
    int lineY = contentTop + 30;
    int linesToShow = std::min(5, static_cast<int>(scriptLines.size()));
    for (int i = 0; i < linesToShow; i++) {
      std::string displayLine = scriptLines[i];
      if (displayLine.length() > 35) {
        displayLine = displayLine.substr(0, 32) + "...";
      }
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, lineY, displayLine.c_str());
      lineY += renderer.getLineHeight(SMALL_FONT_ID) + 2;
    }

    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%zu lines total", scriptLines.size());
    renderer.drawCenteredText(SMALL_FONT_ID, lineY + 10, countBuf);

    const auto labels = mappedInput.mapLabels("Back", "Run", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == ADVERTISING) {
    const int centerY = pageHeight / 2 - 20;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 15, "Advertising...", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 15, "Waiting for connection");
    renderer.drawCenteredText(SMALL_FONT_ID, centerY + 40, BLE_DEVICE_NAME);

    const auto labels = mappedInput.mapLabels("Cancel", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == PAIRED) {
    const int centerY = pageHeight / 2 - 20;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 15, "Connected!", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 15, "Press OK to execute script");

    const auto labels = mappedInput.mapLabels("Cancel", "Execute", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == EXECUTING) {
    const int centerY = pageHeight / 2 - 30;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 15, "Executing...", true, EpdFontFamily::BOLD);

    // Current line being executed
    if (currentLine > 0 && currentLine <= static_cast<int>(scriptLines.size())) {
      std::string curLine = scriptLines[currentLine - 1];
      if (curLine.length() > 30) {
        curLine = curLine.substr(0, 27) + "...";
      }
      renderer.drawCenteredText(SMALL_FONT_ID, centerY + 15, curLine.c_str());
    }

    // Progress
    char progBuf[32];
    snprintf(progBuf, sizeof(progBuf), "Line %d of %zu", currentLine, scriptLines.size());
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 40, progBuf);

    const auto labels = mappedInput.mapLabels("Abort", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == DONE) {
    const int centerY = pageHeight / 2 - 10;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Done", true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("Back", "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
