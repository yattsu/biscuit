#include "AppsMenuActivity.h"

#include <I18n.h>

#include "AirTagTestActivity.h"
#include "AppCategoryActivity.h"
#include "BeaconTestActivity.h"
#include "BleBeaconActivity.h"
#include "BleKeyboardActivity.h"
#include "BleProximityActivity.h"
#include "BleScannerActivity.h"
#include "CredentialViewerActivity.h"
#include "CasinoActivity.h"
#include "ChessActivity.h"
#include "DiceRollerActivity.h"
#include "DnsLookupActivity.h"
#include "EtchASketchActivity.h"
#include "HttpClientActivity.h"
#include "CaptivePortalActivity.h"
#include "GameOfLifeActivity.h"
#include "HostScannerActivity.h"
#include "MappedInputManager.h"
#include "MinesweeperActivity.h"
#include "MorseCodeActivity.h"
#include "ClockActivity.h"
#include "PacketMonitorActivity.h"
#include "PasswordManagerActivity.h"
#include "PingActivity.h"
#include "ProbeSnifferActivity.h"
#include "QrGeneratorActivity.h"
#include "SnakeActivity.h"
#include "SudokuActivity.h"
#include "TetrisActivity.h"
#include "SdFileBrowserActivity.h"
#include "UnitConverterActivity.h"
#include "VoronoiActivity.h"
#include "WifiConnectActivity.h"
#include "WifiTestActivity.h"
#include "WardrivingActivity.h"
#include "WifiScannerActivity.h"
#include "NetworkMonitorActivity.h"
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
#include "SweepActivity.h"
#include "NetworkChangeActivity.h"
#include "CrowdDensityActivity.h"
#include "DeviceFingerprinterActivity.h"
#include "PerimeterWatchActivity.h"
#include "VendorOuiActivity.h"
#include "ApHistoryLoggerActivity.h"
#include "BreadcrumbTrailActivity.h"
#include "VehicleFinderActivity.h"
#include "TransitAlertActivity.h"
#include "AutomationActivity.h"
#include "TotpActivity.h"
#include "EventLoggerActivity.h"
#include "FlashcardActivity.h"
#include "CipherActivity.h"
#include "OtpGeneratorActivity.h"
#include "HabitTrackerActivity.h"
#include "ReadingStatsActivity.h"
#include "TrackerDetectorActivity.h"
#include "BleContactExchangeActivity.h"
#include "RfSilenceActivity.h"
#include "EmergencyActivity.h"
#include "MeshChatActivity.h"
#include "MedicalCardActivity.h"
#include "BulletinBoardActivity.h"
#include "DeadDropActivity.h"
#include "QuickWipeActivity.h"
#include "ScreenDecoyActivity.h"
#include "SecurityPinActivity.h"
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

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  refreshSystemInfo();
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

  // Periodic info refresh
  if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
    refreshSystemInfo();
    requestUpdate();
  }

  // === CONFIRM: open category ===
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    std::unique_ptr<Activity> app;
    switch (selectorIndex) {
      case 0: {
        // NETWORK
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_WIFI_CONNECT), "Join a WiFi network", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
            {tr(STR_WIFI_SCANNER), "Scan APs — list/signal/channel", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
            {tr(STR_HOST_SCANNER), "Find devices on local network", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HostScannerActivity>(r, m); }},
            {tr(STR_PING_TOOL), "Ping a host or IP address", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PingActivity>(r, m); }},
            {tr(STR_DNS_LOOKUP), "Resolve domain names", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DnsLookupActivity>(r, m); }},
            {"HTTP Client", "Send GET/POST requests", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HttpClientActivity>(r, m); }},
            {"mDNS Browser", "Discover local services", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MdnsBrowserActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Network", std::move(e));
        break;
      }
      case 1: {
        // RECON
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_BLE_SCANNER), "Scan BLE devices + services", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleScannerActivity>(r, m); }},
            {tr(STR_PACKET_MONITOR), "Monitor WiFi frames + PCAP", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PacketMonitorActivity>(r, m); }},
            {"Probe Sniffer", "Capture WiFi probe requests", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ProbeSnifferActivity>(r, m); }},
            {"Wardriving", "Log APs with signal strength", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WardrivingActivity>(r, m); }},
            {"Crowd Density", "Estimate people nearby via probes", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CrowdDensityActivity>(r, m); }},
            {"Device Fingerprint", "Identify device OS from probes", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceFingerprinterActivity>(r, m); }},
            {"Vendor Lookup", "Identify device maker by MAC", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VendorOuiActivity>(r, m); }},
            {"AP History", "Log APs over time to SD", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ApHistoryLoggerActivity>(r, m); }},
            {"Network Change", "Diff snapshots of nearby devices", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkChangeActivity>(r, m); }},
            {"Perimeter Watch", "Alert on new devices in area", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PerimeterWatchActivity>(r, m); }},
            {"BLE Proximity", "Track BLE signal strength", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleProximityActivity>(r, m); }},
            {"Credential Viewer", "View captured portal credentials", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CredentialViewerActivity>(r, m); }},
            {tr(STR_BEACON_TEST), "Broadcast wireless beacons", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BeaconTestActivity>(r, m); }},
            {tr(STR_WIFI_TEST), "Wireless connectivity testing", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiTestActivity>(r, m); }},
            {tr(STR_CAPTIVE_PORTAL), "Network portal for testing", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CaptivePortalActivity>(r, m); }},
            {tr(STR_BLE_BEACON), "BLE advertisement broadcast", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleBeaconActivity>(r, m); }},
            {tr(STR_AIRTAG_TEST), "Device location testing", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<AirTagTestActivity>(r, m); }},
            {tr(STR_BLE_KEYBOARD), "HID keyboard emulation", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleKeyboardActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Recon", std::move(e), true);
        break;
      }
      case 2: {
        // SECURITY
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Tracker Detector", "Detect AirTags following you", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TrackerDetectorActivity>(r, m); }},
            {"Security Sweep", "Scan for cameras/trackers/rogues", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SweepActivity>(r, m); }},
            {"Network Monitor", "Detect rogue APs + suspicious frames", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkMonitorActivity>(r, m); }},
            {"Emergency", "SOS beacon + dead man's switch", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EmergencyActivity>(r, m); }},
            {"Quick Wipe", "Erase all biscuit data from SD", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QuickWipeActivity>(r, m); }},
            {"PIN Security", "Lock device with PIN + duress mode", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SecurityPinActivity>(r, m); }},
            {"RF Silence", "Kill all radios + verify they're off", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RfSilenceActivity>(r, m); }},
            {"Screen Decoy", "Fake screen to hide activity", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ScreenDecoyActivity>(r, m); }},
            {"MAC Changer", "Randomize WiFi/BLE MAC address", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MacChangerActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Security", std::move(e));
        break;
      }
      case 3: {
        // COMMS
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Mesh Chat", "Text chat via ESP-NOW, no WiFi needed", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MeshChatActivity>(r, m); }},
            {"SSID Channel", "Hide messages in WiFi network names", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SsidChannelActivity>(r, m); }},
            {"Contact Exchange", "Swap contact cards via BLE", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleContactExchangeActivity>(r, m); }},
            {"Dead Drop", "Temporary AP for anonymous file swap", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeadDropActivity>(r, m); }},
            {"Bulletin Board", "Local anonymous message board", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BulletinBoardActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Comms", std::move(e));
        break;
      }
      case 4: {
        // TOOLS
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Authenticator", "TOTP 2FA codes (offline)", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TotpActivity>(r, m); }},
            {"Medical Card", "Emergency medical info on screen", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MedicalCardActivity>(r, m); }},
            {"Clock", "NTP clock / stopwatch / pomodoro", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
            {"Calculator", "Basic calculator", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
            {tr(STR_PASSWORD_MANAGER), "Encrypted credentials on SD", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PasswordManagerActivity>(r, m); }},
            {tr(STR_QR_GENERATOR), "Generate QR codes from text", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
            {tr(STR_MORSE_CODE), "Encode/decode morse", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MorseCodeActivity>(r, m); }},
            {tr(STR_UNIT_CONVERTER), "Convert between units", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
            {"Cipher Tools", "ROT13, Caesar, Vigenere, XOR", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CipherActivity>(r, m); }},
            {"OTP Generator", "One-time pad random numbers", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OtpGeneratorActivity>(r, m); }},
            {"File Browser", "Browse files on SD card", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdFileBrowserActivity>(r, m); }},
            {"Event Logger", "Timestamped notes with location", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EventLoggerActivity>(r, m); }},
            {"Flashcards", "Study decks from SD (CSV)", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FlashcardActivity>(r, m); }},
            {"Habit Tracker", "Daily habits with streaks", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HabitTrackerActivity>(r, m); }},
            {"Breadcrumb Trail", "Retrace your path via WiFi", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BreadcrumbTrailActivity>(r, m); }},
            {"Vehicle Finder", "Find parked car via WiFi", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VehicleFinderActivity>(r, m); }},
            {"Transit Alert", "Alert when nearing your stop", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TransitAlertActivity>(r, m); }},
            {tr(STR_ETCH_A_SKETCH), "Draw on the e-ink screen", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EtchASketchActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Tools", std::move(e));
        break;
      }
      case 5: {
        // GAMES
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Casino", "Slots, blackjack, roulette + lootbox", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CasinoActivity>(r, m); }},
            {tr(STR_MINESWEEPER), "Classic minesweeper", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
            {tr(STR_SUDOKU), "Number puzzle", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
            {tr(STR_CHESS), "Play against the device", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ChessActivity>(r, m); }},
            {tr(STR_SNAKE), "Classic snake game", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
            {tr(STR_TETRIS), "Block stacking", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TetrisActivity>(r, m); }},
            {"Maze", "Navigate random mazes", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MazeActivity>(r, m); }},
            {tr(STR_DICE_ROLLER), "Roll dice with animation", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
            {tr(STR_GAME_OF_LIFE), "Conway's cellular automaton", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
            {tr(STR_VORONOI), "Generate Voronoi patterns", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VoronoiActivity>(r, m); }},
            {"Matrix Rain", "The Matrix digital rain effect", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MatrixRainActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e));
        break;
      }
      case 6: {
        // SYSTEM
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Settings", "Display, reader, controls, system", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SettingsActivity>(r, m); }},
            {"File Transfer", "Upload/download via WiFi", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkModeSelectionActivity>(r, m); }},
            {"Task Manager", "View heap, uptime, activity stack", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TaskManagerActivity>(r, m); }},
            {"Battery", "Battery level + history graph", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BatteryMonitorActivity>(r, m); }},
            {"Device Info", "Chip, flash, RAM, firmware info", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceInfoActivity>(r, m); }},
            {"Background", "Radio state, SD, active timers", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BackgroundManagerActivity>(r, m); }},
            {"Automation", "Triggers: WiFi geofence + timers", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<AutomationActivity>(r, m); }},
            {"Reading Stats", "Pages read, streaks, progress", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ReadingStatsActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "System", std::move(e));
        break;
      }
      case 7: {
        // READER
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Open Book", "Browse and open an ebook", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
            {"Recent Books", "Continue where you left off", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
            {"Browse Files", "File manager for SD card", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
            {"OPDS Browser", "Download books from OPDS servers", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(e));
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

  // === TILE GRID ===
  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  constexpr int sidePad = 14;
  constexpr int tileGap = 6;
  constexpr int gridTop = statusBarH + 2;
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
}

void AppsMenuActivity::drawStatusBar() const {
  const auto pageWidth = renderer.getScreenWidth();
  constexpr int pad = 14;

  // Left: branding
  renderer.drawText(UI_12_FONT_ID, pad, 10, "biscuit.", true, EpdFontFamily::BOLD);

  // Right side: build right-to-left to avoid overlap

  // Uptime (rightmost)
  char uptimeStr[16];
  unsigned long hrs = uptimeSeconds / 3600;
  unsigned long mins = (uptimeSeconds % 3600) / 60;
  if (hrs > 0) {
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh%02lum", hrs, mins);
  } else {
    snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
  }
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
    case 0: name = "Network";   subtitle = "Connect & diagnose"; appCount = 7;  break;
    case 1: name = "Recon";     subtitle = "Scan & monitor";     appCount = 18; break;
    case 2: name = "Security";  subtitle = "Defend & protect";   appCount = 9;  break;
    case 3: name = "Comms";     subtitle = "Chat & exchange";    appCount = 5;  break;
    case 4: name = "Tools";     subtitle = "Utilities";          appCount = 18; break;
    case 5: name = "Games";     subtitle = "Entertainment";      appCount = 11; break;
    case 6: name = "System";    subtitle = "Device & settings";  appCount = 8;  break;
    case 7: name = "Reader";    subtitle = "Books & OPDS";       appCount = 4;  break;
  }

  renderer.drawText(UI_12_FONT_ID, x + pad, nameY, name, !selected, EpdFontFamily::BOLD);
  nameY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(SMALL_FONT_ID, x + pad, nameY, subtitle, !selected);

  // --- Zone 2: Bottom-right — app count ---
  char countStr[16];
  snprintf(countStr, sizeof(countStr), "%d apps", appCount);
  int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
  int countY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x + w - pad - countW, countY, countStr, !selected);

  // --- Zone 3: Bottom-left — live status (selected tile only) ---
  if (selected) {
    char statusStr[48] = "";
    switch (index) {
      case 0:
        snprintf(statusStr, sizeof(statusStr), wifiConnected ? "WiFi: connected" : "WiFi: off");
        break;
      case 2:
        snprintf(statusStr, sizeof(statusStr), "RF: %s", wifiConnected ? "active" : "silent");
        break;
      case 6:
        snprintf(statusStr, sizeof(statusStr), "Heap: %luK", (unsigned long)(freeHeap / 1024));
        break;
      default:
        break;
    }
    if (statusStr[0] != '\0') {
      int statusY = countY - renderer.getLineHeight(SMALL_FONT_ID) - 4;
      renderer.drawText(SMALL_FONT_ID, x + pad, statusY, statusStr, !selected);
    }
  }
}

