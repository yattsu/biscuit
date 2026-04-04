#include "StopwatchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void StopwatchActivity::onEnter() {
  Activity::onEnter();
  state = MODE_SELECT;
  modeIndex = 0;
  laps.clear();
  swElapsedMs = 0;
  swPausedElapsed = 0;
  countdownMinutes = 5;
  cdRemainingMs = 0;
  cdFlashing = false;
  lastDisplayMs = 0;
  requestUpdate();
}

void StopwatchActivity::onExit() { Activity::onExit(); }

void StopwatchActivity::formatMs(unsigned long ms, char* buf, size_t len) {
  unsigned long totalSec = ms / 1000;
  int m = totalSec / 60;
  int s = totalSec % 60;
  int cs = (ms % 1000) / 10;
  snprintf(buf, len, "%02d:%02d.%02d", m, s, cs);
}

void StopwatchActivity::loop() {
  unsigned long now = millis();

  if (state == MODE_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    buttonNavigator.onNext([this] {
      modeIndex = (modeIndex + 1) % 2;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      modeIndex = (modeIndex + 1) % 2;
      requestUpdate();
    });
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (modeIndex == 0) {
        state = STOPWATCH_IDLE;
        swElapsedMs = 0;
        swPausedElapsed = 0;
        laps.clear();
      } else {
        state = COUNTDOWN_SET;
      }
      requestUpdate();
    }
    return;
  }

  // Stopwatch states
  if (state == STOPWATCH_IDLE || state == STOPWATCH_RUNNING || state == STOPWATCH_STOPPED) {
    if (state == STOPWATCH_RUNNING) {
      swElapsedMs = swPausedElapsed + (now - swStartMs);
      if (now - lastDisplayMs >= DISPLAY_INTERVAL) {
        lastDisplayMs = now;
        requestUpdate();
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (state == STOPWATCH_IDLE) {
        swStartMs = now;
        swPausedElapsed = 0;
        state = STOPWATCH_RUNNING;
      } else if (state == STOPWATCH_RUNNING) {
        // Lap
        if (static_cast<int>(laps.size()) < MAX_LAPS) {
          laps.push_back(swElapsedMs);
        }
      } else {
        // Stopped -> restart
        swStartMs = now;
        swPausedElapsed = 0;
        swElapsedMs = 0;
        laps.clear();
        state = STOPWATCH_RUNNING;
      }
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (state == STOPWATCH_RUNNING) {
        swPausedElapsed = swElapsedMs;
        state = STOPWATCH_STOPPED;
        requestUpdate();
      } else {
        state = MODE_SELECT;
        requestUpdate();
      }
      return;
    }
    return;
  }

  // Countdown set
  if (state == COUNTDOWN_SET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = MODE_SELECT;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (countdownMinutes < 99) countdownMinutes++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (countdownMinutes > 1) countdownMinutes--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      cdRemainingMs = static_cast<unsigned long>(countdownMinutes) * 60000UL;
      cdLastTickMs = now;
      state = COUNTDOWN_RUNNING;
      requestUpdate();
    }
    return;
  }

  // Countdown running
  if (state == COUNTDOWN_RUNNING) {
    unsigned long elapsed = now - cdLastTickMs;
    cdLastTickMs = now;
    if (elapsed >= cdRemainingMs) {
      cdRemainingMs = 0;
      state = COUNTDOWN_DONE;
      cdFlashing = true;
      cdFlashTime = now;
      requestUpdate();
    } else {
      cdRemainingMs -= elapsed;
      if (now - lastDisplayMs >= DISPLAY_INTERVAL) {
        lastDisplayMs = now;
        requestUpdate();
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = COUNTDOWN_SET;
      requestUpdate();
    }
    return;
  }

  // Countdown done
  if (state == COUNTDOWN_DONE) {
    // Flash effect
    if (cdFlashing && now - cdFlashTime >= 500) {
      cdFlashing = !cdFlashing;
      cdFlashTime = now;
      requestUpdate();
    } else if (!cdFlashing && now - cdFlashTime >= 500) {
      cdFlashing = !cdFlashing;
      cdFlashTime = now;
      requestUpdate();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = COUNTDOWN_SET;
      requestUpdate();
    }
  }
}

void StopwatchActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STOPWATCH));

  switch (state) {
    case MODE_SELECT:
      renderModeSelect();
      break;
    case STOPWATCH_IDLE:
    case STOPWATCH_RUNNING:
    case STOPWATCH_STOPPED:
      renderStopwatch();
      break;
    case COUNTDOWN_SET:
      renderCountdownSet();
      break;
    case COUNTDOWN_RUNNING:
      renderCountdownRunning();
      break;
    case COUNTDOWN_DONE:
      renderCountdownDone();
      break;
  }

  renderer.displayBuffer();
}

void StopwatchActivity::renderModeSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const char* modes[] = {tr(STR_STOPWATCH), tr(STR_COUNTDOWN_TIMER)};
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, 2, modeIndex,
      [&modes](int i) { return std::string(modes[i]); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StopwatchActivity::renderStopwatch() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;

  // Elapsed time
  char timeBuf[16];
  formatMs(swElapsedMs, timeBuf, sizeof(timeBuf));
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeBuf, true, EpdFontFamily::BOLD);
  y += 45;

  // Laps
  for (int i = static_cast<int>(laps.size()) - 1; i >= 0 && y < pageHeight - metrics.buttonHintsHeight - 20; i--) {
    char lapBuf[32];
    char lapTime[16];
    formatMs(laps[i], lapTime, sizeof(lapTime));
    snprintf(lapBuf, sizeof(lapBuf), "Lap %d: %s", i + 1, lapTime);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, lapBuf);
    y += lineH;
  }

  const char* confirmLabel =
      (state == STOPWATCH_IDLE) ? "Start" : (state == STOPWATCH_RUNNING ? "Lap" : "Restart");
  const char* backLabel = (state == STOPWATCH_RUNNING) ? "Stop" : tr(STR_BACK);
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StopwatchActivity::renderCountdownSet() const {
  const auto pageHeight = renderer.getScreenHeight();
  int y = pageHeight / 2 - 40;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "Set countdown minutes:");
  y += 40;

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", countdownMinutes);
  renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Start", "+", "-");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StopwatchActivity::renderCountdownRunning() const {
  const auto pageHeight = renderer.getScreenHeight();
  int y = pageHeight / 2 - 20;

  char timeBuf[16];
  formatMs(cdRemainingMs, timeBuf, sizeof(timeBuf));
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeBuf, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StopwatchActivity::renderCountdownDone() const {
  const auto pageHeight = renderer.getScreenHeight();

  if (cdFlashing) {
    renderer.invertScreen();
  }

  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, "TIME'S UP!", true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
