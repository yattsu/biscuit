#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RogueApDetectorActivity final : public Activity {
 public:
  explicit RogueApDetectorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RogueApDetector", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { SCANNING, RESULTS, DETAIL };
  State state = SCANNING;

  struct ApRecord {
    std::string ssid;
    std::string bssid;
    int32_t rssi;
    uint8_t channel;
    uint8_t encType;
  };

  struct SsidGroup {
    std::string ssid;
    int apCount;
    bool suspicious;
    bool mixedEncryption;
    bool mixedChannels;
  };

  std::vector<ApRecord> allAps;
  std::vector<SsidGroup> groups;
  int suspiciousCount = 0;
  int scanCount = 0;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int detailGroupIndex = -1;

  int scanPhase = 0;
  static constexpr int SCAN_PHASES = 3;

  void startScan();
  void processScanResults();
  void analyzeGroups();
  void renderScanning();
  void renderResults();
  void renderDetail();
  static const char* encryptionString(uint8_t type);
};
