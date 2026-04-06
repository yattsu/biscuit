#include "AppCategoryActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

void AppCategoryActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;

  // Show disclaimer for offensive category (one-time)
  if (requiresDisclaimer && !RADIO.isDisclaimerAcknowledged()) {
    disclaimerShown = true;
  }

  requestUpdate();
}

void AppCategoryActivity::loop() {
  if (disclaimerShown) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      RADIO.setDisclaimerAcknowledged();
      disclaimerShown = false;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  const int count = static_cast<int>(entries.size());

  buttonNavigator.onNext([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < count) {
      auto app = entries[selectorIndex].factory(renderer, mappedInput);
      if (app) {
        activityManager.pushActivity(std::move(app));
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void AppCategoryActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // === Branded header (matches dashboard style) ===
  constexpr int headerH = 40;
  constexpr int pad = 14;

  if (disclaimerShown) {
    // Disclaimer screen
    renderer.drawText(UI_12_FONT_ID, pad, 10, title, true, EpdFontFamily::BOLD);
    renderer.drawLine(pad, headerH - 2, pageWidth - pad, headerH - 2, true);

    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DISCLAIMER));
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Category title on left (bold)
  renderer.drawText(UI_12_FONT_ID, pad, 10, title, true, EpdFontFamily::BOLD);

  // Item count on right
  const int count = static_cast<int>(entries.size());
  char countStr[16];
  snprintf(countStr, sizeof(countStr), "%d items", count);
  int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
  renderer.drawText(SMALL_FONT_ID, pageWidth - pad - countW, 14, countStr);

  // Separator line
  renderer.drawLine(pad, headerH - 2, pageWidth - pad, headerH - 2, true);

  // === App list ===
  const int listTop = headerH + 4;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, count, selectorIndex,
      [this](int index) -> std::string { return entries[index].nameStrId; },
      [this](int index) -> std::string {
          return entries[index].description ? std::string(entries[index].description) : "";
      },
      nullptr);

  // === Button hints ===
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
