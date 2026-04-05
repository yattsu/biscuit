#pragma once
#include <string>
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
  int selectorIndex = 0;
  static constexpr int ITEM_COUNT = 6;
  static constexpr int COLS = 2;
  static constexpr int ROWS = 3;

  // Grid navigation
  int getRow() const { return selectorIndex / COLS; }
  int getCol() const { return selectorIndex % COLS; }

  // Cached system info (refreshed on enter + periodically)
  uint32_t freeHeap = 0;
  uint8_t batteryPercent = 0;
  unsigned long uptimeSeconds = 0;
  bool wifiConnected = false;
  unsigned long lastInfoRefresh = 0;
  static constexpr unsigned long INFO_REFRESH_MS = 5000;

  void refreshSystemInfo();

  // Tile rendering
  void drawTile(int index, int x, int y, int w, int h, bool selected) const;
  void drawStatusBar() const;

};
