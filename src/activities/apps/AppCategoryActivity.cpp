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

  if (disclaimerShown) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DISCLAIMER));
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int count = static_cast<int>(entries.size());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, count, selectorIndex,
      [this](int index) -> std::string { return entries[index].nameStrId; }, nullptr, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
