#pragma once
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FlockYouActivity final : public Activity {
 public:
  explicit FlockYouActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlockYou", renderer, mappedInput) {}

  ~FlockYouActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return state == SCANNING; }

  // Called from promiscuous callback — ISR context, no heap, must be fast
  void onPacket(const uint8_t* data, uint16_t len, int8_t rssi, uint8_t channel);

 private:
  enum State { IDLE, SCANNING };
  State state = IDLE;

  enum Method : uint8_t {
    METHOD_WILDCARD_PROBE = 0,  // probe request with empty SSID IE — highest precision
    METHOD_ADDR2          = 1,  // OUI match on transmitter address
    METHOD_ADDR1          = 2,  // OUI match on receiver address
    METHOD_HIDDEN         = 3,  // hidden beacon whose BSSID matches a Flock OUI
  };

  enum AlertKind : uint8_t {
    KIND_FLOCK          = 0,  // OUI match on TX/RX/probe
    KIND_HIDDEN_FLOCK   = 1,  // hidden beacon with Flock OUI BSSID
    KIND_REVEALED_FLOCK = 2,  // probe response reveals SSID for a hidden Flock BSSID
  };

  struct Alert {
    AlertKind kind;
    uint8_t   mac[6];    // matched MAC or BSSID
    char      ssid[33];  // empty unless KIND_REVEALED_FLOCK
    int8_t    rssi;
    uint8_t   channel;
    uint8_t   method;    // KIND_FLOCK only
  };
  static constexpr int ALERT_BUF_SIZE = 48;
  Alert        alertBuf[ALERT_BUF_SIZE]{};
  volatile int alertHead = 0;
  volatile int alertTail = 0;
  portMUX_TYPE alertMux  = portMUX_INITIALIZER_UNLOCKED;

  // Unified detection table — Flock OUI matches + hidden Flock APs
  struct Detection {
    uint8_t  mac[6];
    char     ssid[33];      // empty unless SSID has been revealed
    bool     ssidRevealed;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  method;
    uint32_t hitCount;
    uint32_t firstSeenMs;
    uint32_t lastSeenMs;
  };
  static constexpr int MAX_DETECTIONS = 40;
  Detection detections[MAX_DETECTIONS]{};
  int       detectionCount = 0;

  portMUX_TYPE      statsMux    = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t totalFrames = 0;

  // Channel hopping — full sweep 1..13 at 200ms dwell
  static constexpr unsigned long HOP_INTERVAL_MS = 200;
  uint8_t       currentChannel = 1;
  bool          autoHop        = true;
  unsigned long lastHopMs      = 0;

  int           selectorIndex = 0;
  unsigned long lastUpdateMs  = 0;
  static constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
  ButtonNavigator buttonNavigator;

  void startScan();
  void stopScan();
  void processAlerts();
  void upsertDetection(const Alert& a);

  static bool        matchOui(const uint8_t* mac);
  static bool        isWildcardProbe(const uint8_t* body, int bodyLen);
  static bool        isMulticast(const uint8_t* mac) { return (mac[0] & 0x01) != 0; }
  static bool        parseSsidIe(const uint8_t* body, int bodyLen,
                                  char* ssidOut, bool& isHidden);
  static void        formatMac(char* out, int bufLen, const uint8_t* mac);
  static const char* methodLabel(uint8_t method);
};
