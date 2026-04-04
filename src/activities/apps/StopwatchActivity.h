#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class StopwatchActivity final : public Activity {
 public:
  explicit StopwatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Stopwatch", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State {
    MODE_SELECT,
    STOPWATCH_IDLE,
    STOPWATCH_RUNNING,
    STOPWATCH_STOPPED,
    COUNTDOWN_SET,
    COUNTDOWN_RUNNING,
    COUNTDOWN_DONE
  };

  State state = MODE_SELECT;
  int modeIndex = 0;  // 0 = Stopwatch, 1 = Countdown
  ButtonNavigator buttonNavigator;

  // Stopwatch
  unsigned long swStartMs = 0;
  unsigned long swElapsedMs = 0;
  unsigned long swPausedElapsed = 0;
  std::vector<unsigned long> laps;
  static constexpr int MAX_LAPS = 10;

  // Countdown
  int countdownMinutes = 5;
  unsigned long cdRemainingMs = 0;
  unsigned long cdLastTickMs = 0;
  bool cdFlashing = false;
  unsigned long cdFlashTime = 0;

  // Display throttle
  unsigned long lastDisplayMs = 0;
  static constexpr unsigned long DISPLAY_INTERVAL = 100;

  static void formatMs(unsigned long ms, char* buf, size_t len);

  void renderModeSelect() const;
  void renderStopwatch() const;
  void renderCountdownSet() const;
  void renderCountdownRunning() const;
  void renderCountdownDone() const;
};
