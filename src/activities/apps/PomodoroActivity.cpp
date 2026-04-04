#include "PomodoroActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void PomodoroActivity::onEnter() {
  Activity::onEnter();
  state = IDLE;
  paused = false;
  completedPomodoros = 0;
  cycleCount = 0;
  remainingMs = workDurationMs;
  lastTickMs = millis();
  lastDisplayUpdate = 0;
  requestUpdate();
}

void PomodoroActivity::onExit() { Activity::onExit(); }

void PomodoroActivity::startWork() {
  state = WORK;
  paused = false;
  remainingMs = workDurationMs;
  lastTickMs = millis();
  requestUpdate();
}

void PomodoroActivity::startShortBreak() {
  state = SHORT_BREAK;
  paused = false;
  remainingMs = shortBreakMs;
  lastTickMs = millis();
  requestUpdate();
}

void PomodoroActivity::startLongBreak() {
  state = LONG_BREAK;
  paused = false;
  remainingMs = longBreakMs;
  lastTickMs = millis();
  requestUpdate();
}

void PomodoroActivity::resetToIdle() {
  state = IDLE;
  paused = false;
  remainingMs = workDurationMs;
  requestUpdate();
}

void PomodoroActivity::formatTime(unsigned long ms, char* buf, size_t bufLen) {
  unsigned long totalSec = ms / 1000;
  int h = totalSec / 3600;
  int m = (totalSec % 3600) / 60;
  int s = totalSec % 60;
  snprintf(buf, bufLen, "%02d:%02d:%02d", h, m, s);
}

const char* PomodoroActivity::stateLabel() const {
  switch (state) {
    case IDLE:
      return "Ready";
    case WORK:
      return paused ? "Work (Paused)" : "Work";
    case SHORT_BREAK:
      return paused ? "Short Break (Paused)" : "Short Break";
    case LONG_BREAK:
      return paused ? "Long Break (Paused)" : "Long Break";
  }
  return "";
}

void PomodoroActivity::loop() {
  // Timer tick
  if (!paused && state != IDLE) {
    unsigned long now = millis();
    unsigned long elapsed = now - lastTickMs;
    lastTickMs = now;

    if (elapsed >= remainingMs) {
      remainingMs = 0;
    } else {
      remainingMs -= elapsed;
    }

    // Timer expired
    if (remainingMs == 0) {
      if (state == WORK) {
        completedPomodoros++;
        cycleCount++;
        if (cycleCount >= 4) {
          cycleCount = 0;
          startLongBreak();
        } else {
          startShortBreak();
        }
        return;
      } else {
        // Break ended, start next work
        startWork();
        return;
      }
    }

    // Throttle display updates to once per second
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
      lastDisplayUpdate = now;
      requestUpdate();
    }
  }

  // Confirm = start / pause / resume
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (state == IDLE) {
      startWork();
    } else {
      paused = !paused;
      if (!paused) {
        lastTickMs = millis();
      }
      requestUpdate();
    }
    return;
  }

  // Back = reset if running, exit if idle
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (state == IDLE) {
      finish();
    } else {
      resetToIdle();
    }
    return;
  }

  // Up/Down = adjust remaining time +/- 1 minute
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    remainingMs += 60000;
    requestUpdate();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (remainingMs > 60000) {
      remainingMs -= 60000;
    } else {
      remainingMs = 0;
    }
    requestUpdate();
  }
}

void PomodoroActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_POMODORO));

  // State label
  int y = pageHeight / 2 - 80;
  renderer.drawCenteredText(UI_12_FONT_ID, y, stateLabel(), true, EpdFontFamily::BOLD);

  // Large countdown
  char timeBuf[16];
  formatTime(remainingMs, timeBuf, sizeof(timeBuf));
  y += 50;
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeBuf, true, EpdFontFamily::BOLD);

  // Pomodoro count
  y += 50;
  char countBuf[48];
  snprintf(countBuf, sizeof(countBuf), "Completed: %d  Cycle: %d/4", completedPomodoros, cycleCount);
  renderer.drawCenteredText(UI_10_FONT_ID, y, countBuf);

  // Button hints
  const char* confirmLabel = (state == IDLE) ? "Start" : (paused ? "Resume" : "Pause");
  const char* backLabel = (state == IDLE) ? tr(STR_BACK) : "Reset";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, "+1min", "-1min");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
