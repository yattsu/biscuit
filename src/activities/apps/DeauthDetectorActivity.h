#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <freertos/portmacro.h>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class DeauthDetectorActivity final : public Activity {
 public:
  explicit DeauthDetectorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeauthDetector", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

  void onFrame(const uint8_t* payload, uint16_t len, int rssi, uint8_t channel);

 private:
  enum State { MONITORING, DETAIL, LOG_VIEW };
  State state = MONITORING;

  struct DetectionEvent {
    uint8_t targetBssid[6];
    uint8_t attackerMac[6];
    uint8_t channel;
    int8_t rssi;
    uint32_t count;
    unsigned long firstSeen;
    unsigned long lastSeen;
    uint8_t frameType;     // 0xC0=deauth, 0xA0=disassoc
    uint8_t reasonCode;
  };

  static constexpr int MAX_EVENTS = 30;
  DetectionEvent events[MAX_EVENTS] = {};
  int eventCount = 0;

  volatile uint32_t totalFrames = 0;
  volatile uint32_t framesThisInterval = 0;
  uint32_t framesPerSec = 0;
  bool alertActive = false;

  uint8_t currentChannel = 1;
  bool autoHop = true;
  unsigned long lastHopTime = 0;
  static constexpr unsigned long HOP_INTERVAL_MS = 300;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int detailIndex = 0;
  unsigned long lastUpdateTime = 0;
  static constexpr unsigned long UPDATE_INTERVAL_MS = 1500;

  portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

  static constexpr int GRAPH_POINTS = 40;
  uint16_t rateHistory[GRAPH_POINTS] = {};
  int historyIndex = 0;

  void startMonitoring();
  void stopMonitoring();
  void saveToCsv();
  static std::string macToString(const uint8_t* mac);
  static const char* reasonCodeStr(uint8_t code);
};
