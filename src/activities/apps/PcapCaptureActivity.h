#pragma once
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "activities/Activity.h"

class PcapCaptureActivity final : public Activity {
 public:
  explicit PcapCaptureActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PcapCapture", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return state == CAPTURING; }

  void onPacket(const uint8_t* buf, uint16_t len);

 private:
  enum State { MODE_SELECT, CAPTURING, DONE };
  enum CaptureMode { ALL_PACKETS, EAPOL_ONLY };

  State state = MODE_SELECT;
  CaptureMode captureMode = ALL_PACKETS;
  int modeIndex = 0;

  SemaphoreHandle_t fileMux = nullptr;
  FsFile pcapFile;
  bool fileOpen = false;
  volatile uint32_t packetsSaved = 0;
  volatile uint32_t fileSize = 0;
  uint8_t currentChannel = 1;
  bool autoHop = true;
  bool eapolFound = false;
  unsigned long lastHopTime = 0;
  unsigned long lastUpdateTime = 0;

  void startCapture();
  void stopCapture();
  void writePcapHeader();
  void writePacket(const uint8_t* data, uint16_t len);
  bool isEapolPacket(const uint8_t* data, uint16_t len) const;
};
