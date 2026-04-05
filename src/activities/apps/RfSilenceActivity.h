#pragma once
#include "activities/Activity.h"

class RfSilenceActivity final : public Activity {
 public:
  explicit RfSilenceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RfSilence", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool wifiOff = false;
  bool bleOff = false;
  bool verified = false;

  void killRadios();
  void verifyRadios();
};
