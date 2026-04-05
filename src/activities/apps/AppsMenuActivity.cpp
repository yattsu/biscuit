#include "AppsMenuActivity.h"

#include <I18n.h>

#include "AirTagTestActivity.h"
#include "AppCategoryActivity.h"
#include "BeaconTestActivity.h"
#include "BleBeaconActivity.h"
#include "BleKeyboardActivity.h"
#include "BleScannerActivity.h"
#include "CasinoActivity.h"
#include "ChessActivity.h"
#include "DiceRollerActivity.h"
#include "DnsLookupActivity.h"
#include "EtchASketchActivity.h"
#include "CaptivePortalActivity.h"
#include "GameOfLifeActivity.h"
#include "HostScannerActivity.h"
#include "MappedInputManager.h"
#include "MinesweeperActivity.h"
#include "MorseCodeActivity.h"
#include "NtpClockActivity.h"
#include "PacketMonitorActivity.h"
#include "PasswordManagerActivity.h"
#include "PcapCaptureActivity.h"
#include "PingActivity.h"
#include "PomodoroActivity.h"
#include "QrGeneratorActivity.h"
#include "SnakeActivity.h"
#include "StopwatchActivity.h"
#include "SudokuActivity.h"
#include "TetrisActivity.h"
#include "TextViewerActivity.h"
#include "UnitConverterActivity.h"
#include "VoronoiActivity.h"
#include "WifiConnectActivity.h"
#include "WifiTestActivity.h"
#include "WifiScannerActivity.h"
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
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_WIFI_CONNECT), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
            {tr(STR_WIFI_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
            {tr(STR_HOST_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<HostScannerActivity>(r, m); }},
            {tr(STR_PING_TOOL), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PingActivity>(r, m); }},
            {tr(STR_DNS_LOOKUP), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DnsLookupActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_NETWORK_TOOLS), std::move(e));
        break;
      }
      case 1: {
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_BLE_SCANNER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleScannerActivity>(r, m); }},
            {tr(STR_PACKET_MONITOR), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PacketMonitorActivity>(r, m); }},
            {tr(STR_PCAP_CAPTURE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PcapCaptureActivity>(r, m); }},
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
        std::vector<AppCategoryActivity::AppEntry> e = {
	    {"Casino", [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CasinoActivity>(r, m); }},
            {tr(STR_MINESWEEPER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
            {tr(STR_SUDOKU), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
            {tr(STR_CHESS), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ChessActivity>(r, m); }},
            {tr(STR_GAME_OF_LIFE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
            {tr(STR_VORONOI), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VoronoiActivity>(r, m); }},
            {tr(STR_SNAKE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
            {tr(STR_TETRIS), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TetrisActivity>(r, m); }},
            {tr(STR_DICE_ROLLER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e));
        break;
      }
      case 3: {
        std::vector<AppCategoryActivity::AppEntry> e = {
            {tr(STR_PASSWORD_MANAGER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PasswordManagerActivity>(r, m); }},
            {tr(STR_POMODORO), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<PomodoroActivity>(r, m); }},
            {tr(STR_NTP_CLOCK), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<NtpClockActivity>(r, m); }},
            {tr(STR_STOPWATCH), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<StopwatchActivity>(r, m); }},
            {tr(STR_QR_GENERATOR), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
            {tr(STR_MORSE_CODE), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MorseCodeActivity>(r, m); }},
            {tr(STR_UNIT_CONVERTER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
            {tr(STR_TEXT_VIEWER), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TextViewerActivity>(r, m); }},
            {tr(STR_ETCH_A_SKETCH), [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<EtchASketchActivity>(r, m); }},
        };
        app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_UTILITIES), std::move(e));
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
          case 2: return tr(STR_GAMES);
          case 3: return tr(STR_UTILITIES);
          default: return "";
        }
      },
      nullptr, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
