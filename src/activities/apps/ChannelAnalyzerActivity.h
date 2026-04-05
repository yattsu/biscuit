#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ChannelAnalyzerActivity final : public Activity {
 public:
  explicit ChannelAnalyzerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ChannelAnalyzer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { SCANNING, SHOWING };
  State state = SCANNING;

  struct ChannelInfo {
    int apCount;
    int32_t strongestRssi;
    int32_t avgRssi;
    int64_t rssiSum;
    int interferenceScore;
  };
  ChannelInfo channels[14] = {};  // index 1-13

  struct ApInfo {
    std::string ssid;
    int32_t rssi;
    uint8_t channel;
  };
  std::vector<ApInfo> aps;

  int scanCount = 0;
  int recommendedChannel = 1;

  enum ViewMode { VIEW_SPECTRUM, VIEW_TABLE };
  ViewMode viewMode = VIEW_SPECTRUM;

  ButtonNavigator buttonNavigator;

  void startScan();
  void processScanResults();
  void analyzeChannels();
  int findBestChannel() const;

  void renderScanning();
  void renderSpectrum();
  void renderTable();
  static const char* encryptionString(uint8_t type);
};
