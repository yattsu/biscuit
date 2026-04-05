#include "RfSilenceActivity.h"

#include <WiFi.h>
#include <esp_bt.h>
#include <esp_wifi.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

void RfSilenceActivity::killRadios() {
  RADIO.shutdown();
  // Belt-and-suspenders: force WiFi stack off directly as well.
  WiFi.mode(WIFI_OFF);
}

void RfSilenceActivity::verifyRadios() {
  // --- WiFi ---
  wifi_mode_t mode = WIFI_MODE_NULL;
  esp_err_t wifiErr = esp_wifi_get_mode(&mode);
  // If the WiFi driver was never initialised, esp_wifi_get_mode returns an
  // error (ESP_ERR_WIFI_NOT_INIT).  That is still "off" for our purposes.
  wifiOff = (wifiErr != ESP_OK) || (mode == WIFI_MODE_NULL);

  // --- BLE ---
  esp_bt_controller_status_t btStatus = esp_bt_controller_get_status();
  // IDLE means the controller exists but is not running.
  // ESP_BT_CONTROLLER_STATUS_IDLE == 0; if the controller was never
  // initialised it may also report IDLE.  Any non-active state counts as off.
  bleOff = (btStatus == ESP_BT_CONTROLLER_STATUS_IDLE);

  verified = wifiOff && bleOff;
}

// ---- Activity lifecycle ----

void RfSilenceActivity::onEnter() {
  Activity::onEnter();
  killRadios();
  verifyRadios();
  requestUpdate();
}

void RfSilenceActivity::onExit() {
  Activity::onExit();
  // Radios stay OFF — user must re-enable explicitly.
}

void RfSilenceActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

// ---- Rendering ----

void RfSilenceActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();   // 800
  const auto pageHeight = renderer.getScreenHeight();  // 480

  // ---- Central RF SILENT box ----
  // Reserve ~60% of height for the emphasis block, centered vertically.
  constexpr int BOX_TOP_OFFSET   = 40;   // pixels below top of screen
  constexpr int BOX_SIDE_MARGIN  = 40;
  constexpr int BOX_H            = 280;

  const int boxX = BOX_SIDE_MARGIN;
  const int boxY = BOX_TOP_OFFSET;
  const int boxW = pageWidth - BOX_SIDE_MARGIN * 2;

  // Double-border for visual weight.
  renderer.drawRect(boxX,     boxY,     boxW,     BOX_H,     true);
  renderer.drawRect(boxX + 3, boxY + 3, boxW - 6, BOX_H - 6, true);

  // "RF SILENT" — large, bold, centred inside the box.
  const int titleY = boxY + 20;
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, "RF SILENT", true, EpdFontFamily::BOLD);

  // Thin separator below title.
  const int sepY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 8;
  renderer.drawLine(boxX + 12, sepY, boxX + boxW - 12, sepY, true);

  // ---- Radio status lines ----
  int statusY = sepY + 14;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;

  // WiFi status
  {
    // Small filled square acts as a monochrome checkmark when radio is off.
    const int sqSize = 8;
    const int sqX    = boxX + 20;
    const int sqY    = statusY + (renderer.getLineHeight(UI_10_FONT_ID) - sqSize) / 2;
    if (wifiOff) {
      renderer.fillRect(sqX, sqY, sqSize, sqSize, true);
    } else {
      renderer.drawRect(sqX, sqY, sqSize, sqSize, true);
    }
    renderer.drawText(UI_10_FONT_ID, sqX + sqSize + 8, statusY,
                      wifiOff ? "WiFi: OFF" : "WiFi: ACTIVE — not silenced!", true);
  }
  statusY += lineH;

  // BLE status
  {
    const int sqSize = 8;
    const int sqX    = boxX + 20;
    const int sqY    = statusY + (renderer.getLineHeight(UI_10_FONT_ID) - sqSize) / 2;
    if (bleOff) {
      renderer.fillRect(sqX, sqY, sqSize, sqSize, true);
    } else {
      renderer.drawRect(sqX, sqY, sqSize, sqSize, true);
    }
    renderer.drawText(UI_10_FONT_ID, sqX + sqSize + 8, statusY,
                      bleOff ? "BLE:  OFF" : "BLE:  ACTIVE — not silenced!", true);
  }
  statusY += lineH + 10;

  // ---- Verification summary ----
  if (verified) {
    renderer.drawCenteredText(UI_10_FONT_ID, statusY,
                              "VERIFIED -- All radios disabled", true, EpdFontFamily::BOLD);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, statusY,
                              "WARNING -- Could not verify radio state", true, EpdFontFamily::BOLD);
  }

  // ---- Footer note (below box) ----
  const int footerY = boxY + BOX_H + 16;
  renderer.drawCenteredText(SMALL_FONT_ID, footerY,
                            "Radios will remain off after exit");

  // ---- Button hints ----
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
