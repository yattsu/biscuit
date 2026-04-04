#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class WifiScannerActivity final : public Activity {
 public:
  explicit WifiScannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WifiScanner", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { SCANNING, LIST, DETAIL };

  struct Network {
    std::string ssid;
    std::string bssid;
    int32_t rssi;
    uint8_t channel;
    uint8_t encType;  // wifi_auth_mode_t
  };

  State state = SCANNING;
  std::vector<Network> networks;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int detailIndex = 0;
  bool sortBySignal = true;

  void startScan();
  void processScanResults();
  void sortNetworks();
  void saveToCsv();
  const char* encryptionString(uint8_t type) const;
  const char* signalBars(int32_t rssi) const;
};
