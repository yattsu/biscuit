#include "NtpClockActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <time.h>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void NtpClockActivity::onEnter() {
  Activity::onEnter();
  synced = false;
  syncFailed = false;
  lastUpdateMs = 0;

  // Ensure WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               syncFailed = true;
                               requestUpdate();
                             } else {
                               // WiFi connected, do NTP sync
                               configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                               struct tm timeinfo;
                               if (getLocalTime(&timeinfo, 10000)) {
                                 synced = true;
                               } else {
                                 syncFailed = true;
                               }
                               lastUpdateMs = millis();
                               requestUpdate();
                             }
                           });
    return;
  }

  // Already connected, sync NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10000)) {
    synced = true;
  } else {
    syncFailed = true;
  }
  lastUpdateMs = millis();
  requestUpdate();
}

void NtpClockActivity::onExit() { Activity::onExit(); }

const char* NtpClockActivity::dayOfWeekName(int wday) {
  static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (wday >= 0 && wday <= 6) return days[wday];
  return "???";
}

const char* NtpClockActivity::monthName(int mon) {
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (mon >= 0 && mon <= 11) return months[mon];
  return "???";
}

void NtpClockActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (synced) {
    unsigned long now = millis();
    if (now - lastUpdateMs >= UPDATE_INTERVAL_MS) {
      lastUpdateMs = now;
      requestUpdate();
    }
  }
}

void NtpClockActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NTP_CLOCK));

  if (syncFailed) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_NTP_SYNC_FAILED), true, EpdFontFamily::BOLD);
  } else if (!synced) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SYNCING_NTP));
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int y = pageHeight / 2 - 60;

      // Day of week
      renderer.drawCenteredText(UI_10_FONT_ID, y, dayOfWeekName(timeinfo.tm_wday));
      y += 40;

      // Large time HH:MM
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      renderer.drawCenteredText(UI_12_FONT_ID, y, timeBuf, true, EpdFontFamily::BOLD);
      y += 50;

      // Date
      char dateBuf[32];
      snprintf(dateBuf, sizeof(dateBuf), "%s %d, %d", monthName(timeinfo.tm_mon), timeinfo.tm_mday,
               timeinfo.tm_year + 1900);
      renderer.drawCenteredText(UI_10_FONT_ID, y, dateBuf);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
