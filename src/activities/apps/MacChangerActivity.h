#pragma once
#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MacChangerActivity final : public Activity {
 public:
  explicit MacChangerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MacChanger", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Mode { WIFI_MAC, BLE_MAC };
  Mode selectedMode = WIFI_MAC;
  ButtonNavigator buttonNavigator;

  uint8_t originalWifiMac[6] = {};
  uint8_t originalBleMac[6] = {};
  uint8_t currentWifiMac[6] = {};
  uint8_t currentBleMac[6] = {};
  bool wifiRandomized = false;
  bool bleRandomized = false;
  std::string statusMessage;

  void readCurrentMacs();
  void randomizeWifiMac();
  void randomizeBleMac();
  void restoreOriginalMac();
  static std::string macToString(const uint8_t* mac);
};
