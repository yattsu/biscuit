#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <BLEDevice.h>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BleExplorerActivity final : public Activity {
 public:
  explicit BleExplorerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleExplorer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { SCANNING, DEVICE_LIST, CONNECTING, SERVICES, CHARACTERISTICS, CHAR_VALUE };
  State state = SCANNING;

  struct BleTarget {
    std::string name;
    std::string mac;
    int32_t rssi;
    BLEAddress address;
  };
  std::vector<BleTarget> devices;

  BLEClient* pClient = nullptr;
  bool connected = false;

  struct ServiceInfo {
    std::string uuid;
    std::string name;
    int charCount;
  };
  std::vector<ServiceInfo> services;

  struct CharInfo {
    std::string uuid;
    std::string name;
    std::string value;
    uint8_t properties;
    bool canRead;
    bool canWrite;
    bool canNotify;
  };
  std::vector<CharInfo> characteristics;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int selectedDevice = -1;
  int selectedService = -1;
  int selectedChar = -1;

  unsigned long lastScanTime = 0;
  bool scanInitialized = false;

  void startBleScan();
  void stopBleScan();
  void connectToDevice(int index);
  void disconnect();
  void enumerateServices();
  void enumerateCharacteristics(int serviceIndex);
  void readCharacteristic(int charIndex);

  static const char* resolveServiceName(const std::string& uuid);
  static const char* resolveCharName(const std::string& uuid);
  static std::string propertiesToString(uint8_t props);
  static std::string bytesToHex(const uint8_t* data, int len);
  static std::string bytesToAsciiSafe(const uint8_t* data, int len);
};
