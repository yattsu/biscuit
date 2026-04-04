#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class PomodoroActivity final : public Activity {
 public:
  explicit PomodoroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pomodoro", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State { IDLE, WORK, SHORT_BREAK, LONG_BREAK };

  State state = IDLE;
  bool paused = false;

  // Timer
  unsigned long remainingMs = 0;
  unsigned long lastTickMs = 0;

  // Durations in ms
  unsigned long workDurationMs = 25 * 60 * 1000UL;
  unsigned long shortBreakMs = 5 * 60 * 1000UL;
  unsigned long longBreakMs = 15 * 60 * 1000UL;

  // Tracking
  int completedPomodoros = 0;
  int cycleCount = 0;  // counts work sessions before long break (resets at 4)

  // Display update throttle
  unsigned long lastDisplayUpdate = 0;
  static constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 1000;

  void startWork();
  void startShortBreak();
  void startLongBreak();
  void resetToIdle();

  static void formatTime(unsigned long ms, char* buf, size_t bufLen);
  const char* stateLabel() const;
};
