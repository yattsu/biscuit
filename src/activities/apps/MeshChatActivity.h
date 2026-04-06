#pragma once
#include <cstdint>
#include <esp_now.h>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MeshChatActivity final : public Activity {
 public:
  explicit MeshChatActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MeshChat", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State { CHAT_VIEW, PEERS };

  struct Message {
    uint8_t senderMac[6];
    char senderName[16];
    char text[200];
    unsigned long timestamp;
    bool isLocal;
  };

  struct Peer {
    uint8_t mac[6];
    char name[16];
    unsigned long lastSeen;
  };

  State state = CHAT_VIEW;
  ButtonNavigator buttonNavigator;

  // Messages (ring buffer using fixed array)
  static constexpr int MAX_MESSAGES = 30;
  Message messages[MAX_MESSAGES];
  int messageCount = 0;
  int messageHead = 0;
  int scrollOffset = 0;

  // Peers
  std::vector<Peer> peers;
  int peerSelectorIndex = 0;
  SemaphoreHandle_t peersMux = nullptr;

  // Identity
  char localName[16] = "biscuit";
  uint8_t localMac[6] = {};

  // ESP-NOW state
  bool espnowInitialized = false;
  static constexpr uint8_t ESPNOW_CHANNEL = 1;
  unsigned long lastPresenceTime = 0;
  static constexpr unsigned long PRESENCE_INTERVAL_MS = 10000;

  // Protocol frame format:
  // [0]     = frame type: 0x01 = chat, 0x02 = presence
  // [1-6]   = sender MAC
  // [7-22]  = sender name (16 bytes, null-terminated)
  // [23-222]= text payload (200 bytes max, null-terminated)
  static constexpr uint8_t FRAME_CHAT = 0x01;
  static constexpr uint8_t FRAME_PRESENCE = 0x02;

  void initEspNow();
  void deinitEspNow();
  void sendMessage(const char* text);
  void sendPresenceBeacon();
  void addMessage(const uint8_t* senderMac, const char* senderName, const char* text, bool isLocal);

  // ESP-NOW callbacks
  static void onDataRecv(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len);

  void renderChatView() const;
  void renderPeers() const;

  static MeshChatActivity* activeInstance;
};
