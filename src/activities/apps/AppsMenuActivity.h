#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class AppsMenuActivity final : public Activity {
 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  static constexpr int ITEM_COUNT = 4;  // Network Tools, Wireless Testing, Games, Utilities
};
