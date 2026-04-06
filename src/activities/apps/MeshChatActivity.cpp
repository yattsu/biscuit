#include "MeshChatActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

MeshChatActivity* MeshChatActivity::activeInstance = nullptr;

void MeshChatActivity::onEnter() {
  Activity::onEnter();
  state = CHAT_VIEW;
  messageCount = 0;
  messageHead = 0;
  scrollOffset = 0;
  peers.clear();
  peerSelectorIndex = 0;
  espnowInitialized = false;
  lastPresenceTime = 0;
  activeInstance = this;
  if (!peersMux) peersMux = xSemaphoreCreateMutex();

  initEspNow();
  requestUpdate();
}

void MeshChatActivity::onExit() {
  Activity::onExit();
  deinitEspNow();
  activeInstance = nullptr;
  if (peersMux) { vSemaphoreDelete(peersMux); peersMux = nullptr; }
}

void MeshChatActivity::initEspNow() {
  RADIO.ensureWifi();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Set channel
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    LOG_ERR("MESH", "ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer
  esp_now_peer_info_t broadcastPeer = {};
  memset(broadcastPeer.peer_addr, 0xFF, 6);
  broadcastPeer.channel = ESPNOW_CHANNEL;
  broadcastPeer.encrypt = false;
  if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
    LOG_ERR("MESH", "Failed to add broadcast peer");
  }

  WiFi.macAddress(localMac);
  espnowInitialized = true;
  LOG_DBG("MESH", "ESP-NOW initialized on channel %d", ESPNOW_CHANNEL);
}

void MeshChatActivity::deinitEspNow() {
  if (espnowInitialized) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    RADIO.shutdown();
    espnowInitialized = false;
  }
}

void MeshChatActivity::onDataRecv(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len) {
  if (!activeInstance || !recvInfo || len < 23) return;
  const uint8_t* mac = recvInfo->src_addr;

  uint8_t frameType = data[0];

  if (frameType == FRAME_CHAT) {
    char senderName[16] = {};
    char text[200] = {};
    memcpy(senderName, data + 7, 15);
    senderName[15] = '\0';
    int textLen = len - 23;
    if (textLen > 199) textLen = 199;
    memcpy(text, data + 23, textLen);
    text[199] = '\0';

    activeInstance->addMessage(mac, senderName, text, false);
    activeInstance->requestUpdate();
  } else if (frameType == FRAME_PRESENCE) {
    char peerName[16] = {};
    memcpy(peerName, data + 7, 15);
    peerName[15] = '\0';

    if (activeInstance->peersMux &&
        xSemaphoreTake(activeInstance->peersMux, pdMS_TO_TICKS(10)) == pdTRUE) {
      bool found = false;
      for (auto& p : activeInstance->peers) {
        if (memcmp(p.mac, mac, 6) == 0) {
          memcpy(p.name, peerName, 16);
          p.lastSeen = millis();
          found = true;
          break;
        }
      }
      if (!found && activeInstance->peers.size() < 20) {
        Peer newPeer = {};
        memcpy(newPeer.mac, mac, 6);
        memcpy(newPeer.name, peerName, 16);
        newPeer.lastSeen = millis();
        activeInstance->peers.push_back(newPeer);
      }
      xSemaphoreGive(activeInstance->peersMux);
    }
  }
}

void MeshChatActivity::sendMessage(const char* text) {
  if (!espnowInitialized || !text || text[0] == '\0') return;

  uint8_t frame[223] = {};
  frame[0] = FRAME_CHAT;
  memcpy(frame + 1, localMac, 6);
  strncpy(reinterpret_cast<char*>(frame + 7), localName, 15);
  strncpy(reinterpret_cast<char*>(frame + 23), text, 199);

  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast, frame, sizeof(frame));

  addMessage(localMac, localName, text, true);
}

void MeshChatActivity::sendPresenceBeacon() {
  if (!espnowInitialized) return;

  uint8_t frame[23] = {};
  frame[0] = FRAME_PRESENCE;
  memcpy(frame + 1, localMac, 6);
  strncpy(reinterpret_cast<char*>(frame + 7), localName, 15);

  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast, frame, sizeof(frame));
  lastPresenceTime = millis();
}

void MeshChatActivity::addMessage(const uint8_t* senderMac, const char* senderName, const char* text, bool isLocal) {
  int idx = messageHead;
  memcpy(messages[idx].senderMac, senderMac, 6);
  strncpy(messages[idx].senderName, senderName, 15);
  messages[idx].senderName[15] = '\0';
  strncpy(messages[idx].text, text, 199);
  messages[idx].text[199] = '\0';
  messages[idx].timestamp = millis();
  messages[idx].isLocal = isLocal;

  messageHead = (messageHead + 1) % MAX_MESSAGES;
  if (messageCount < MAX_MESSAGES) messageCount++;

  // Auto-scroll to newest
  scrollOffset = 0;
}

void MeshChatActivity::loop() {
  // Send presence beacon periodically
  if (espnowInitialized && millis() - lastPresenceTime >= PRESENCE_INTERVAL_MS) {
    sendPresenceBeacon();
  }

  switch (state) {
    case CHAT_VIEW: {
      // Up/Down scroll through messages
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (scrollOffset < messageCount - 1) {
          scrollOffset++;
          requestUpdate();
        }
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (scrollOffset > 0) {
          scrollOffset--;
          requestUpdate();
        }
      }

      // Confirm -> compose message (keyboard)
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Message", "", 199),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& text = std::get<KeyboardResult>(result.data).text;
                if (!text.empty()) {
                  sendMessage(text.c_str());
                }
              }
              requestUpdate();
            });
      }

      // Right -> show peers
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        state = PEERS;
        peerSelectorIndex = 0;
        requestUpdate();
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();
      }
      break;
    }

    case PEERS: {
      int peerCount = 0;
      if (peersMux && xSemaphoreTake(peersMux, pdMS_TO_TICKS(10)) == pdTRUE) {
        peerCount = static_cast<int>(peers.size());
        xSemaphoreGive(peersMux);
      }
      if (peerCount > 0) {
        buttonNavigator.onNext([this, peerCount] {
          peerSelectorIndex = ButtonNavigator::nextIndex(peerSelectorIndex, peerCount);
          requestUpdate();
        });
        buttonNavigator.onPrevious([this, peerCount] {
          peerSelectorIndex = ButtonNavigator::previousIndex(peerSelectorIndex, peerCount);
          requestUpdate();
        });
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        state = CHAT_VIEW;
        requestUpdate();
      }
      break;
    }
  }
}

void MeshChatActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case CHAT_VIEW: renderChatView(); break;
    case PEERS: renderPeers(); break;
  }
  renderer.displayBuffer();
}

void MeshChatActivity::renderChatView() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  int peersCount = 0;
  if (peersMux && xSemaphoreTake(peersMux, pdMS_TO_TICKS(10)) == pdTRUE) {
    peersCount = static_cast<int>(peers.size());
    xSemaphoreGive(peersMux);
  }
  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "%d msgs | %d peers", messageCount, peersCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Mesh Chat", subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;

  if (messageCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2,
                              "No messages yet. Press Confirm to send.");
  } else {
    // Render messages from newest (bottom) to oldest (top)
    int y = contentBottom - lineH;
    int msgIdx = (messageHead - 1 - scrollOffset + MAX_MESSAGES) % MAX_MESSAGES;
    int drawn = 0;
    int maxDraw = messageCount - scrollOffset;

    while (drawn < maxDraw && y >= contentTop) {
      const Message& msg = messages[msgIdx];

      char line[240];
      if (msg.isLocal) {
        snprintf(line, sizeof(line), "> %s", msg.text);
      } else {
        snprintf(line, sizeof(line), "%s: %s", msg.senderName, msg.text);
      }

      renderer.drawText(SMALL_FONT_ID, 16, y, line);
      y -= lineH;
      msgIdx = (msgIdx - 1 + MAX_MESSAGES) % MAX_MESSAGES;
      drawn++;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "Send", "Scroll", "Peers >");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MeshChatActivity::renderPeers() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  std::vector<Peer> peersCopy;
  if (peersMux && xSemaphoreTake(peersMux, pdMS_TO_TICKS(10)) == pdTRUE) {
    peersCopy = peers;
    xSemaphoreGive(peersMux);
  }

  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "%d nearby", static_cast<int>(peersCopy.size()));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Peers", subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (peersCopy.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2,
                              "No peers discovered yet...");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(peersCopy.size()), peerSelectorIndex,
        [&peersCopy](int index) -> std::string {
          return std::string(peersCopy[index].name);
        },
        [&peersCopy](int index) -> std::string {
          char buf[48];
          unsigned long ago = (millis() - peersCopy[index].lastSeen) / 1000;
          snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X  %lus ago",
                   peersCopy[index].mac[0], peersCopy[index].mac[1], peersCopy[index].mac[2],
                   peersCopy[index].mac[3], peersCopy[index].mac[4], peersCopy[index].mac[5], ago);
          return buf;
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
