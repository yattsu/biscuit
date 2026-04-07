#include "AppsMenuActivity.h"

#include <I18n.h>

#include "AirTagTestActivity.h"
#include "AppCategoryActivity.h"
#include "BeaconTestActivity.h"
#include "BleKeyboardActivity.h"
#include "BleProximityActivity.h"
#include "BleScannerActivity.h"
#include "CredentialViewerActivity.h"
#include "CasinoActivity.h"
#include "ChessActivity.h"
#include "DiceRollerActivity.h"
#include "DnsLookupActivity.h"
#include "EtchASketchActivity.h"
#include "FireActivity.h"
#include "HttpClientActivity.h"
#include "CaptivePortalActivity.h"
#include "GameOfLifeActivity.h"
#include "HostScannerActivity.h"
#include "MappedInputManager.h"
#include "MinesweeperActivity.h"
#include "MorseCodeActivity.h"
#include "ClockActivity.h"
#include "CountdownActivity.h"
#include "PacketMonitorActivity.h"
#include "PasswordManagerActivity.h"
#include "PingActivity.h"
#include "ProbeSnifferActivity.h"
#include "BarcodeActivity.h"
#include "KeyCopierActivity.h"
#include "QrGeneratorActivity.h"
#include "WifiCredsActivity.h"
#include "SnakeActivity.h"
#include "SudokuActivity.h"
#include "TetrisActivity.h"
#include "SdFileBrowserActivity.h"
#include "UnitConverterActivity.h"
#include "VoronoiActivity.h"
#include "WifiConnectActivity.h"
#include "WifiTestActivity.h"
#include "WardrivingActivity.h"
#include "WifiHeatMapActivity.h"
#include "SignalTriangulationActivity.h"
#include "WifiScannerActivity.h"
#include "NetworkMonitorActivity.h"
#include "DeauthDetectorActivity.h"
#include "SsidChannelActivity.h"
#include "MacChangerActivity.h"
#include "MatrixRainActivity.h"
#include "MazeActivity.h"
#include "CalculatorActivity.h"
#include "MdnsBrowserActivity.h"
#include "TaskManagerActivity.h"
#include "BatteryMonitorActivity.h"
#include "DeviceInfoActivity.h"
#include "BackgroundManagerActivity.h"
#include "ScanActivity.h"
#include "HuntActivity.h"
#include "LootActivity.h"
#include "GhostActivity.h"
#include "SweepActivity.h"
#include "NetworkChangeActivity.h"
#include "CrowdDensityActivity.h"
#include "DeviceFingerprinterActivity.h"
#include "PerimeterWatchActivity.h"
#include "VendorOuiActivity.h"
#include "ApClonerActivity.h"
#include "ApHistoryLoggerActivity.h"
#include "BreadcrumbTrailActivity.h"
#include "VehicleFinderActivity.h"
#include "TransitAlertActivity.h"
#include "AutomationActivity.h"
#include "TotpActivity.h"
#include "QrTotpActivity.h"
#include "EventLoggerActivity.h"
#include "FlashcardActivity.h"
#include "CipherActivity.h"
#include "OtpGeneratorActivity.h"
#include "SteganographyActivity.h"
#include "HabitTrackerActivity.h"
#include "ReadingStatsActivity.h"
#include "TrackerDetectorActivity.h"
#include "PhoneTetherActivity.h"
#include "BleContactExchangeActivity.h"
#include "BleSpamActivity.h"
#include "RfSilenceActivity.h"
#include "EmergencyActivity.h"
#include "MeshChatActivity.h"
#include "MedicalCardActivity.h"
#include "BulletinBoardActivity.h"
#include "DeadDropActivity.h"
#include "QuickWipeActivity.h"
#include "UsbHidActivity.h"
#include "UsbMassStorageActivity.h"
#include "ScreenDecoyActivity.h"
#include "SecurityPinActivity.h"
#include "SdEncryptionActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <HalPowerManager.h>
#include <HalStorage.h>

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  // Check badges once on enter (SD I/O only here, not in periodic refresh)
  badgeSecurity = Storage.exists("/biscuit/security.dat") ? 0 : -1;
  refreshSystemInfo();
  loadLastUsed();
  requestUpdate();
}

void AppsMenuActivity::loop() {
  // === 2D GRID NAVIGATION ===
  // Left/Right (front buttons) move between columns
  // Up/Down (side volume buttons) move between rows

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    int col = getCol();
    int row = getRow();
    col++;
    if (col >= COLS) {
      col = 0;
      row = (row + 1) % ROWS;
    }
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) selectorIndex = 0;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    int col = getCol();
    int row = getRow();
    col--;
    if (col < 0) {
      col = COLS - 1;
      row = (row - 1 + ROWS) % ROWS;
    }
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) selectorIndex = ITEM_COUNT - 1;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    int col = getCol();
    int row = (getRow() + 1) % ROWS;
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) selectorIndex = col;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    int col = getCol();
    int row = (getRow() - 1 + ROWS) % ROWS;
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) {
      row = (row - 1 + ROWS) % ROWS;
      selectorIndex = row * COLS + col;
    }
    requestUpdate();
  }

  // Periodic info refresh — only redraw if visible values changed
  if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
    uint32_t oldHeap = freeHeap;
    bool oldWifi = wifiConnected;
    refreshSystemInfo();
    // Only trigger e-ink refresh if KB-level heap changed or wifi status changed
    bool heapChanged = (freeHeap / 1024) != (oldHeap / 1024);
    if (heapChanged || (wifiConnected != oldWifi)) {
      requestUpdate();
    }
  }

  // === CONFIRM: open category ===
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    std::unique_ptr<Activity> app;
    switch (selectorIndex) {
        case 0:
          app = std::make_unique<ScanActivity>(renderer, mappedInput);
          break;
        case 1:
          app = std::make_unique<HuntActivity>(renderer, mappedInput);
          break;
        case 2:
          {
            std::vector<AppCategoryActivity::AppEntry> e = {
                AppCategoryActivity::SectionHeader("WIRELESS TESTING"),
                {tr(STR_BEACON_TEST), "Broadcast wireless beacons", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BeaconTestActivity>(r, m); }},
                {tr(STR_WIFI_TEST), "Wireless connectivity testing", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiTestActivity>(r, m); }},
                {tr(STR_CAPTIVE_PORTAL), "Network portal for testing", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CaptivePortalActivity>(r, m); }},
                {"BLE Spam", "Proximity/Fast Pair/Swift Pair spam", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleSpamActivity>(r, m); }},
                {tr(STR_AIRTAG_TEST), "Device location testing", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<AirTagTestActivity>(r, m); }},
                {tr(STR_BLE_KEYBOARD), "HID keyboard emulation", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleKeyboardActivity>(r, m); }},
                {"USB Keyboard", "Wired DuckyScript via USB-C", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UsbHidActivity>(r, m); }},
                {"AP Cloner", "Clone a WiFi AP (open, same channel)", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ApClonerActivity>(r, m); }},
                AppCategoryActivity::SectionHeader("BLE TOOLS"),
                {tr(STR_BLE_SCANNER), "Scan BLE devices + services", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleScannerActivity>(r, m); }},
                {"BLE Proximity", "Track BLE signal strength", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleProximityActivity>(r, m); }},
                AppCategoryActivity::SectionHeader("WIFI TOOLS"),
                {tr(STR_WIFI_CONNECT), "Join a WiFi network", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
                {tr(STR_WIFI_SCANNER), "Scan APs — list/signal/channel", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
                {tr(STR_HOST_SCANNER), "Find devices on local network", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HostScannerActivity>(r, m); }},
                {tr(STR_PING_TOOL), "Ping a host or IP address", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PingActivity>(r, m); }},
                {tr(STR_DNS_LOOKUP), "Resolve domain names", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DnsLookupActivity>(r, m); }},
                {"HTTP Client", "Send GET/POST requests", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HttpClientActivity>(r, m); }},
                {"mDNS Browser", "Discover local services", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MdnsBrowserActivity>(r, m); }},
                AppCategoryActivity::SectionHeader("RECON"),
                {"Wardriving", "Log APs with signal strength", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WardrivingActivity>(r, m); }},
                {"Heat Map", "Log RSSI per AP while walking", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiHeatMapActivity>(r, m); }},
                {"Triangulation", "Estimate AP direction from 3 points", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SignalTriangulationActivity>(r, m); }},
                {"Probe Sniffer", "Capture WiFi probe requests", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ProbeSnifferActivity>(r, m); }},
                {tr(STR_PACKET_MONITOR), "Monitor WiFi frames + PCAP", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PacketMonitorActivity>(r, m); }},
                {"Crowd Density", "Estimate people nearby via probes", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CrowdDensityActivity>(r, m); }},
                {"Device Fingerprint", "Identify device OS from probes", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceFingerprinterActivity>(r, m); }},
                {"Vendor Lookup", "Identify device maker by MAC", UIIcon::Library, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VendorOuiActivity>(r, m); }},
                {"AP History", "Log APs over time to SD", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ApHistoryLoggerActivity>(r, m); }},
                {"Network Change", "Diff snapshots of nearby devices", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkChangeActivity>(r, m); }},
                {"Perimeter Watch", "Alert on new devices in area", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PerimeterWatchActivity>(r, m); }},
                {"Credential Viewer", "View captured portal credentials", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CredentialViewerActivity>(r, m); }},
                AppCategoryActivity::SectionHeader("COMMS"),
                {"Mesh Chat", "Text chat via ESP-NOW, no WiFi needed", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MeshChatActivity>(r, m); }},
                {"SSID Channel", "Hide messages in WiFi network names", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SsidChannelActivity>(r, m); }},
                {"Contact Exchange", "Swap contact cards via BLE", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleContactExchangeActivity>(r, m); }},
                {"Dead Drop", "Temporary AP for anonymous file swap", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeadDropActivity>(r, m); }},
                {"Bulletin Board", "Local anonymous message board", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BulletinBoardActivity>(r, m); }},
                AppCategoryActivity::SectionHeader("SECURITY"),
                {"Tracker Detector", "Detect AirTags following you", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TrackerDetectorActivity>(r, m); }},
                {"Phone Tether", "Alert when paired device leaves range", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PhoneTetherActivity>(r, m); }},
                {"Security Sweep", "Scan for cameras/trackers/rogues", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SweepActivity>(r, m); }},
                {"Network Monitor", "Detect rogue APs + suspicious frames", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkMonitorActivity>(r, m); }},
                {"Deauth Detector", "Detect deauth/disassoc frame spikes", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeauthDetectorActivity>(r, m); }},
                {"Emergency", "SOS beacon + dead man's switch", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EmergencyActivity>(r, m); }},
                {"Quick Wipe", "Erase all biscuit data from SD", UIIcon::Folder, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QuickWipeActivity>(r, m); }},
                {"PIN Security", "Lock device with PIN + duress mode", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SecurityPinActivity>(r, m); }},
                {"SD Encryption", "Encrypt biscuit data with PIN", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdEncryptionActivity>(r, m); }},
                {"RF Silence", "Kill all radios + verify they're off", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RfSilenceActivity>(r, m); }},
                {"Screen Decoy", "Fake screen to hide activity", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ScreenDecoyActivity>(r, m); }},
                {"MAC Changer", "Randomize WiFi/BLE MAC address", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MacChangerActivity>(r, m); }},
            };
            app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Fire", std::move(e), true, 2);
          }
          break;
        case 3:
          app = std::make_unique<LootActivity>(renderer, mappedInput);
          break;
        case 4:
          app = std::make_unique<GhostActivity>(renderer, mappedInput);
          break;
        case 5: {
          // GAMES
          std::vector<AppCategoryActivity::AppEntry> e = {
              {"Casino", "Slots, blackjack, roulette + lootbox", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CasinoActivity>(r, m); }, false, []() -> bool { return Storage.exists("/biscuit/casino.dat"); }},
              {tr(STR_MINESWEEPER), "Classic minesweeper", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
              {tr(STR_SUDOKU), "Number puzzle", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
              {tr(STR_CHESS), "Play against the device", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ChessActivity>(r, m); }},
              {tr(STR_SNAKE), "Classic snake game", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
              {tr(STR_TETRIS), "Block stacking", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TetrisActivity>(r, m); }},
              {"Maze", "Navigate random mazes", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MazeActivity>(r, m); }},
              {tr(STR_DICE_ROLLER), "Roll dice with animation", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
              {tr(STR_GAME_OF_LIFE), "Conway's cellular automaton", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
              {tr(STR_VORONOI), "Generate Voronoi patterns", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VoronoiActivity>(r, m); }},
              {"Matrix Rain", "The Matrix digital rain effect", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MatrixRainActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e), false, 5);
          break;
        }
        case 6: {
          // TOOLS (non-wireless utilities only)
          std::vector<AppCategoryActivity::AppEntry> e = {
              AppCategoryActivity::SectionHeader("SECURITY & AUTH"),
              {"Authenticator", "TOTP 2FA codes (offline)", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TotpActivity>(r, m); }, false, []() -> bool { return Storage.exists("/biscuit/totp.dat"); }},
              {"TOTP QR", "Show 2FA code as scannable QR", UIIcon::Image, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrTotpActivity>(r, m); }, false, []() -> bool { return Storage.exists("/biscuit/totp.dat"); }},
              {"Medical Card", "Emergency medical info on screen", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MedicalCardActivity>(r, m); }},
              {tr(STR_PASSWORD_MANAGER), "Encrypted credentials on SD", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PasswordManagerActivity>(r, m); }},
              AppCategoryActivity::SectionHeader("UTILITIES"),
              {"Clock", "NTP clock / stopwatch / pomodoro", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
              {"Countdown", "Big countdown timer", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CountdownActivity>(r, m); }},
              {"Calculator", "Basic calculator", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
              {tr(STR_QR_GENERATOR), "Generate QR codes from text", UIIcon::Image, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
              {"Barcode Generator", "Code 128 / Code 39 / EAN-13 barcodes", UIIcon::Image, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BarcodeActivity>(r, m); }},
              {"Key Copier", "Draw key profiles from bitting codes", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<KeyCopierActivity>(r, m); }},
              {"WiFi QR Share", "Share WiFi credentials as a QR code", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiCredsActivity>(r, m); }},
              {tr(STR_MORSE_CODE), "Encode/decode morse", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MorseCodeActivity>(r, m); }},
              {tr(STR_UNIT_CONVERTER), "Convert between units", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
              {"Cipher Tools", "ROT13, Caesar, Vigenere, XOR", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CipherActivity>(r, m); }},
              {"OTP Generator", "One-time pad random numbers", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OtpGeneratorActivity>(r, m); }},
              {"Stego Notes", "Hide text in BMP images", UIIcon::Image, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SteganographyActivity>(r, m); }},
              {"File Browser", "Browse files on SD card", UIIcon::Folder, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdFileBrowserActivity>(r, m); }},
              AppCategoryActivity::SectionHeader("TRACKING & NOTES"),
              {"Event Logger", "Timestamped notes with location", UIIcon::Text, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EventLoggerActivity>(r, m); }},
              {"Flashcards", "Study decks from SD (CSV)", UIIcon::Book, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FlashcardActivity>(r, m); }},
              {"Habit Tracker", "Daily habits with streaks", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HabitTrackerActivity>(r, m); }, false, []() -> bool { return Storage.exists("/biscuit/habits.dat"); }},
              AppCategoryActivity::SectionHeader("NAVIGATION"),
              {"Breadcrumb Trail", "Retrace your path via WiFi", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BreadcrumbTrailActivity>(r, m); }},
              {"Vehicle Finder", "Find parked car via WiFi", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VehicleFinderActivity>(r, m); }},
              {"Transit Alert", "Alert when nearing your stop", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TransitAlertActivity>(r, m); }},
              AppCategoryActivity::SectionHeader("CREATIVE"),
              {tr(STR_ETCH_A_SKETCH), "Draw on the e-ink screen", UIIcon::Image, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EtchASketchActivity>(r, m); }},
              AppCategoryActivity::SectionHeader("SYSTEM"),
              {"Settings", "Display, reader, controls, system", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SettingsActivity>(r, m); }},
              {"File Transfer", "Upload/download via WiFi", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkModeSelectionActivity>(r, m); }},
              {"Task Manager", "View heap, uptime, activity stack", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TaskManagerActivity>(r, m); }},
              {"Battery", "Battery level + history graph", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BatteryMonitorActivity>(r, m); }},
              {"Device Info", "Chip, flash, RAM, firmware info", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceInfoActivity>(r, m); }},
              {"Background", "Radio state, SD, active timers", UIIcon::Settings, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BackgroundManagerActivity>(r, m); }},
              {"Automation", "Triggers: WiFi geofence + timers", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<AutomationActivity>(r, m); }},
              {"Reading Stats", "Pages read, streaks, progress", UIIcon::Book, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ReadingStatsActivity>(r, m); }},
              {"USB Storage", "Share SD card as USB drive", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UsbMassStorageActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Tools", std::move(e), true, 6);
          break;
        }
        case 7: {
          // READER
          std::vector<AppCategoryActivity::AppEntry> e = {
              {"Open Book", "Browse and open an ebook", UIIcon::Book, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
              {"Recent Books", "Continue where you left off", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
              {"Browse Files", "File manager for SD card", UIIcon::Folder, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
              {"OPDS Browser", "Download books from OPDS servers", UIIcon::Library, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(e), false, 7);
          break;
        }
      }
    if (app) activityManager.pushActivity(std::move(app));
  }

  // Back button ignored on main screen — use Power button to sleep
}

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // === STATUS BAR (top 44px) ===
  drawStatusBar();

  // === STATUS INFO ROW (below separator, above tiles) ===
  constexpr int statusRowY = 42;
  char statusBuf[64];
  if (wifiConnected) {
    snprintf(statusBuf, sizeof(statusBuf), "WiFi: on | %luK | %s",
             (unsigned long)(freeHeap / 1024), uptimeStr);
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "WiFi: off | %luK | %s",
             (unsigned long)(freeHeap / 1024), uptimeStr);
  }
  renderer.drawText(SMALL_FONT_ID, 14, statusRowY, statusBuf);

  // === TILE GRID ===
  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  constexpr int sidePad = 14;
  constexpr int tileGap = 6;
  constexpr int gridTop = statusBarH + 32;  // Below status info row (clearance for status text)
  const int gridBottom = pageHeight - buttonHintsH - 2;
  const int gridHeight = gridBottom - gridTop;

  const int tileW = (pageWidth - sidePad * 2 - tileGap) / COLS;
  const int tileH = (gridHeight - tileGap * (ROWS - 1)) / ROWS;

  for (int i = 0; i < ITEM_COUNT; i++) {
    int row = i / COLS;
    int col = i % COLS;
    int x = sidePad + col * (tileW + tileGap);
    int y = gridTop + row * (tileH + tileGap);
    drawTile(i, x, y, tileW, tileH, i == selectorIndex);
  }

  // === BUTTON HINTS ===
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void AppsMenuActivity::refreshSystemInfo() {
  freeHeap = esp_get_free_heap_size();
  uptimeSeconds = (unsigned long)(esp_timer_get_time() / 1000000LL);
  batteryPercent = (uint8_t)powerManager.getBatteryPercentage();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  lastInfoRefresh = millis();

  unsigned long hrs = uptimeSeconds / 3600;
  unsigned long mins = (uptimeSeconds % 3600) / 60;
  if (hrs > 0) {
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh%02lum", hrs, mins);
  } else {
    snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
  }

}

void AppsMenuActivity::loadLastUsed() {
  for (int i = 0; i < ITEM_COUNT; i++) {
    lastUsedName[i][0] = '\0';
    char path[40];
    snprintf(path, sizeof(path), "/biscuit/lastused_%d.txt", i);
    FsFile file;
    if (Storage.openFileForRead("APPS", path, file)) {
      int len = file.read((uint8_t*)lastUsedName[i], 31);
      if (len > 0) {
        lastUsedName[i][len] = '\0';
        // Strip trailing newline
        if (len > 0 && lastUsedName[i][len - 1] == '\n') {
          lastUsedName[i][len - 1] = '\0';
        }
      }
      file.close();
    }
  }
}

void AppsMenuActivity::drawStatusBar() const {
  const auto pageWidth = renderer.getScreenWidth();
  constexpr int pad = 14;

  // Left: branding
  renderer.drawText(UI_12_FONT_ID, pad, 10, "biscuit.", true, EpdFontFamily::BOLD);

  // Right side: build right-to-left to avoid overlap

  // Uptime (rightmost)
  int uptimeW = renderer.getTextWidth(SMALL_FONT_ID, uptimeStr);
  int rightX = pageWidth - pad;
  renderer.drawText(SMALL_FONT_ID, rightX - uptimeW, 14, uptimeStr);
  rightX -= uptimeW + 10;

  // Heap
  char heapStr[16];
  snprintf(heapStr, sizeof(heapStr), "%luK", (unsigned long)(freeHeap / 1024));
  int heapW = renderer.getTextWidth(SMALL_FONT_ID, heapStr);
  renderer.drawText(SMALL_FONT_ID, rightX - heapW, 14, heapStr);
  rightX -= heapW + 10;

  // WiFi dot
  if (wifiConnected) {
    renderer.fillRect(rightX - 6, 16, 6, 6, true);
  } else {
    renderer.drawRect(rightX - 6, 16, 6, 6, true);
  }
  rightX -= 14;

  // Battery — drawBatteryRight draws percentage text at rect.y, icon at rect.y+6
  GUI.drawBatteryRight(renderer, Rect{rightX - 16, 14, 15, 12});

  // Separator line
  renderer.drawLine(pad, 38, pageWidth - pad, 38, true);
}

void AppsMenuActivity::drawTile(int index, int x, int y, int w, int h, bool selected) const {
  if (selected) {
    renderer.fillRect(x, y, w, h, true);
  } else {
    renderer.drawRect(x, y, w, h, true);
  }

  constexpr int pad = 10;

  // --- Zone 1: Top — category name + subtitle ---
  int nameY = y + pad;
  const char* name = "";
  const char* subtitle = "";
  int appCount = 0;

  switch (index) {
    case 0: name = "SCAN";    subtitle = "Passive intel";      appCount = 0;  break;
    case 1: name = "HUNT";    subtitle = "Target profiling";   appCount = 0;  break;
    case 2: name = "FIRE";    subtitle = "Tools & testing";    appCount = 45; break;
    case 3: name = "LOOT";    subtitle = "Captured data";      appCount = 0;  break;
    case 4: name = "GHOST";   subtitle = "Stealth & defense";  appCount = 0;  break;
    case 5: name = "Games";   subtitle = "Entertainment";      appCount = 11; break;
    case 6: name = "Tools";   subtitle = "Utils & system";     appCount = 33; break;
    case 7: name = "Reader";  subtitle = "Books & OPDS";       appCount = 4;  break;
  }

  renderer.drawText(UI_12_FONT_ID, x + pad, nameY, name, !selected, EpdFontFamily::BOLD);
  nameY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(SMALL_FONT_ID, x + pad, nameY, subtitle, !selected);

  // --- Zone 2: Bottom-right — app count (skip for modules with 0) ---
  int countY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
  if (appCount > 0) {
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d apps", appCount);
    int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
    renderer.drawText(SMALL_FONT_ID, x + w - pad - countW, countY, countStr, !selected);
  }

  // --- Badge indicator (top-right corner of tile) ---
  int badge = 0;
  bool showBang = false;
  switch (index) {
    case 0: badge = badgeRecon; break;  // scan finds new devices
    case 4: showBang = (badgeSecurity < 0); break;  // ghost/security PIN
    case 6: badge = badgeSystem; break;
  }

  if (badge > 0 || showBang) {
    int badgeX = x + w - 24;
    int badgeY = y + 6;
    // Draw badge background (inverted relative to tile)
    renderer.fillRect(badgeX, badgeY, 16, 16, !selected);
    // Draw badge text
    char badgeStr[4];
    if (showBang) {
      snprintf(badgeStr, sizeof(badgeStr), "!");
    } else {
      snprintf(badgeStr, sizeof(badgeStr), "%d", badge);
    }
    int bw = renderer.getTextWidth(SMALL_FONT_ID, badgeStr);
    renderer.drawText(SMALL_FONT_ID, badgeX + 8 - bw / 2, badgeY + 1, badgeStr, selected);
  }

  // --- Zone 3: Bottom-left — live status (selected tile only) ---
  if (selected) {
    char statusStr[48] = "";
    switch (index) {
      case 0:
        snprintf(statusStr, sizeof(statusStr), "Targets: %d", 0);  // placeholder — TARGETS.totalCached() when wired
        break;
      case 2:
        snprintf(statusStr, sizeof(statusStr), wifiConnected ? "WiFi: connected" : "WiFi: off");
        break;
      case 4:
        snprintf(statusStr, sizeof(statusStr), "RF: %s", wifiConnected ? "active" : "silent");
        break;
      case 6:
        snprintf(statusStr, sizeof(statusStr), "Heap: %luK", (unsigned long)(freeHeap / 1024));
        break;
      case 5:  // Games
      case 7:  // Reader
        if (lastUsedName[index][0] != '\0') {
          snprintf(statusStr, sizeof(statusStr), "Last: %s", lastUsedName[index]);
        }
        break;
    }
    if (statusStr[0] != '\0') {
      int statusY = countY - renderer.getLineHeight(SMALL_FONT_ID) - 4;
      renderer.drawText(SMALL_FONT_ID, x + pad, statusY, statusStr, !selected);
    }
  }
}

