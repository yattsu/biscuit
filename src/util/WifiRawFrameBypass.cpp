#include "WifiRawFrameBypass.h"

#include <cstdint>

// esp_wifi_80211_tx() and ieee80211_raw_frame_sanity_check() are compiled into
// the same object file inside libnet80211.a, so a linker --wrap can never
// intercept that call (--wrap only redirects references resolved *across*
// object/archive boundaries -- confirmed by disassembling the linked firmware
// and by a minimal reproduction outside this project). Instead,
// scripts/patch_wifi_raw_tx_check.py weakens the library's symbol via
// `objcopy --weaken-symbol`, so this strong definition overrides it globally
// at link time for every caller, regardless of which object file it's in.
//
// esp_wifi_80211_tx() is only ever called from our own raw-TX activities
// (nothing in normal WiFi connect/AP/scan code paths goes through it -- those
// use the driver's internal, node-bound deauth/disassoc construction instead),
// so failing closed when no activity has enabled the bypass costs nothing.
static volatile bool s_bypassFrameCheck = false;

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return s_bypassFrameCheck ? 0 : 258;
}

void setWifiFrameCheckBypass(bool enabled) { s_bypassFrameCheck = enabled; }
