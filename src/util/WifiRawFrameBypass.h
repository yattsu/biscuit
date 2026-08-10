#pragma once

// Enables/disables the Wi-Fi raw-frame validation override defined in
// WifiRawFrameBypass.cpp, which replaces libnet80211.a's
// ieee80211_raw_frame_sanity_check (weakened by scripts/patch_wifi_raw_tx_check.py
// so our strong definition wins at link time). Shared by every activity that
// transmits hand-built 802.11 management frames via esp_wifi_80211_tx. Always
// pair an enable with a matching disable once the activity is done.
void setWifiFrameCheckBypass(bool enabled);
