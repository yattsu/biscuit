#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Generic category submenu for apps.
 * Takes a list of app names and factory functions to create them.
 */
class AppCategoryActivity final : public Activity {
 public:
  struct AppEntry {
    const char* nameStrId;  // will be resolved via tr() at render time
    const char* description;  // one-line description shown as subtitle
    std::function<std::unique_ptr<Activity>(GfxRenderer&, MappedInputManager&)> factory;
  };

  explicit AppCategoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title,
                                std::vector<AppEntry> entries, bool requiresDisclaimer = false)
      : Activity("AppCategory", renderer, mappedInput),
        title(title),
        entries(std::move(entries)),
        requiresDisclaimer(requiresDisclaimer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const char* title;
  std::vector<AppEntry> entries;
  bool requiresDisclaimer;
  bool disclaimerShown = false;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
};
