#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SignalMeterActivity final : public Activity {
 public:
  explicit SignalMeterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SignalMeter", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { SCANNING, SELECT_AP, MEASURING };

  struct AP {
    std::string ssid;
    uint8_t bssid[6];
    int32_t rssi;
    uint8_t channel;
  };

  State state = SCANNING;
  std::vector<AP> aps;
  int selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  int targetIndex = -1;
  int32_t currentRssi = -100;
  int32_t minRssi = 0;
  int32_t maxRssi = -100;
  int32_t avgRssi = -100;
  int sampleCount = 0;
  int64_t rssiSum = 0;

  static constexpr int HISTORY_SIZE = 40;
  int32_t rssiHistory[HISTORY_SIZE] = {};
  int historyIndex = 0;
  int historyCount = 0;

  unsigned long lastMeasureTime = 0;
  static constexpr unsigned long MEASURE_INTERVAL_MS = 500;

  void startScan();
  void processScanResults();
  void doMeasurement();
};
