#pragma once

#include <string>

#include "activities/Activity.h"

class NtpClockActivity final : public Activity {
 public:
  explicit NtpClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NtpClock", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool synced = false;
  bool syncFailed = false;
  unsigned long lastUpdateMs = 0;
  static constexpr unsigned long UPDATE_INTERVAL_MS = 60000;

  static const char* dayOfWeekName(int wday);
  static const char* monthName(int mon);
};
