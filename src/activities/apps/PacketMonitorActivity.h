#pragma once
#include <cstdint>
#include <string>

#include <freertos/portmacro.h>

#include "activities/Activity.h"

class PacketMonitorActivity final : public Activity {
 public:
  explicit PacketMonitorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PacketMonitor", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return monitoring; }

  // Packet callback (called from promiscuous callback, must be fast)
  void onPacket(const uint8_t* buf, uint16_t len, int rssi);

 private:
  bool monitoring = false;
  uint8_t currentChannel = 1;
  bool autoHop = true;

  // Stats (updated from callback via spinlock, read from render)
  portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t totalPackets = 0;
  volatile uint32_t intervalPackets = 0;
  uint32_t packetsPerSec = 0;
  uint32_t channelPackets[14]{};  // channels 1-13
  // MAC deduplication: simple hash table (fixed size, no heap alloc in callback)
  static constexpr int MAC_TABLE_SIZE = 256;
  uint64_t macTable[MAC_TABLE_SIZE]{};
  volatile uint32_t uniqueMacCount = 0;

  unsigned long lastUpdateTime = 0;
  unsigned long lastHopTime = 0;
  static constexpr unsigned long UPDATE_INTERVAL_MS = 2500;
  static constexpr unsigned long HOP_INTERVAL_MS = 500;

  void startMonitor();
  void stopMonitor();
  void setChannel(uint8_t ch);
  void saveToCsv();
};
