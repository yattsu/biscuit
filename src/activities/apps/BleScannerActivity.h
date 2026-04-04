#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BleScannerActivity final : public Activity {
 public:
  explicit BleScannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleScanner", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return scanning; }

 private:
  enum State { SCANNING_VIEW, DETAIL };

  struct BleDevice {
    std::string name;
    std::string mac;
    int rssi;
  };

  State state = SCANNING_VIEW;
  std::vector<BleDevice> devices;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int detailIndex = 0;
  bool scanning = false;
  bool scanInitialized = false;
  unsigned long lastScanTime = 0;
  static constexpr unsigned long SCAN_INTERVAL_MS = 5000;

  void startBleScan();
  void stopBleScan();
  void saveToCsv();
};
