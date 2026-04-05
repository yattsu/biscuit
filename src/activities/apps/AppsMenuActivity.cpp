#include "AppsMenuActivity.h"

#include <I18n.h>

#include "AirTagTestActivity.h"
#include "ApClonerActivity.h"
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
#include "MacChangerActivity.h"
#include "MatrixRainActivity.h"
#include "MazeActivity.h"
#include "CalculatorActivity.h"
#include "MdnsBrowserActivity.h"
#include "TaskManagerActivity.h"
#include "BatteryMonitorActivity.h"
#include "DeviceInfoActivity.h"
#include "BackgroundManagerActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate();
}

void AppsMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    std::unique_ptr<Activity> app;
    switch (selectorIndex) {
      case 0: {
        // Network
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_WIFI_CONNECT), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
            {tr(STR_WIFI_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
            {tr(STR_HOST_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HostScannerActivity>(r, m); }},
            {tr(STR_PING_TOOL), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PingActivity>(r, m); }},
            {tr(STR_DNS_LOOKUP), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DnsLookupActivity>(r, m); }},
            {"HTTP Client", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HttpClientActivity>(r, m); }},
            {"mDNS Browser", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MdnsBrowserActivity>(r, m); }},
            {tr(STR_BLE_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleScannerActivity>(r, m); }},
            {"Wardriving", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WardrivingActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_NETWORK_TOOLS), std::move(e));
        break;
      }
      case 1: {
        // Wireless Ops
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_PACKET_MONITOR), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PacketMonitorActivity>(r, m); }},
            {"Probe Sniffer", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ProbeSnifferActivity>(r, m); }},
            {"BLE Proximity", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleProximityActivity>(r, m); }},
            {"Credential Viewer", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CredentialViewerActivity>(r, m); }},
            {"MAC Changer", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MacChangerActivity>(r, m); }},
            {"AP Cloner", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ApClonerActivity>(r, m); }},
            {"Network Monitor", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NetworkMonitorActivity>(r, m); }},
            {tr(STR_BEACON_TEST), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BeaconTestActivity>(r, m); }},
            {tr(STR_WIFI_TEST), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiTestActivity>(r, m); }},
            {tr(STR_CAPTIVE_PORTAL), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CaptivePortalActivity>(r, m); }},
            {tr(STR_BLE_BEACON), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleBeaconActivity>(r, m); }},
            {tr(STR_AIRTAG_TEST), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<AirTagTestActivity>(r, m); }},
            {tr(STR_BLE_KEYBOARD), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleKeyboardActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_WIRELESS_TESTING), std::move(e), true);
        break;
      }
      case 2: {
        // Tools
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Clock", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
            {"Calculator", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
            {tr(STR_QR_GENERATOR), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
            {tr(STR_MORSE_CODE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MorseCodeActivity>(r, m); }},
            {tr(STR_UNIT_CONVERTER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
            {tr(STR_PASSWORD_MANAGER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PasswordManagerActivity>(r, m); }},
            {"SD File Browser", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdFileBrowserActivity>(r, m); }},
            {tr(STR_ETCH_A_SKETCH), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EtchASketchActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_UTILITIES), std::move(e));
        break;
      }
      case 3: {
        // Games
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Casino", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CasinoActivity>(r, m); }},
            {tr(STR_MINESWEEPER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
            {tr(STR_SUDOKU), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
            {tr(STR_CHESS), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ChessActivity>(r, m); }},
            {tr(STR_SNAKE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
            {tr(STR_TETRIS), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TetrisActivity>(r, m); }},
            {"Maze", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MazeActivity>(r, m); }},
            {tr(STR_DICE_ROLLER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
            {tr(STR_GAME_OF_LIFE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
            {tr(STR_VORONOI), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VoronoiActivity>(r, m); }},
            {"Matrix Rain", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MatrixRainActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e));
        break;
      }
      case 4: {
        // System
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Task Manager", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TaskManagerActivity>(r, m); }},
            {"Battery Monitor", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BatteryMonitorActivity>(r, m); }},
            {"Device Info", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceInfoActivity>(r, m); }},
            {"Background", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BackgroundManagerActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "System", std::move(e));
        break;
      }
      case 5: {
        // Reader
        std::vector<AppCategoryActivity::AppEntry> e = {
            {"Open Book", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
            {"Recent Books", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
            {"Browse Files", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
            {"OPDS Browser", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(e));
        break;
      }
    }
    if (app) activityManager.pushActivity(std::move(app));
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void AppsMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectorIndex,
      [](int index) -> std::string {
        switch (index) {
          case 0: return tr(STR_NETWORK_TOOLS);
          case 1: return tr(STR_WIRELESS_TESTING);
          case 2: return tr(STR_UTILITIES);
          case 3: return tr(STR_GAMES);
          case 4: return "System";
          case 5: return "Reader";
          default: return "";
        }
      },
      nullptr, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
