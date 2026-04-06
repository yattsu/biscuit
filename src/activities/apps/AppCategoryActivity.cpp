#include "AppCategoryActivity.h"

#include <I18n.h>
#include <HalStorage.h>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

void AppCategoryActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  backPressedHere = false;

  // Skip initial section header if present
  const int count = static_cast<int>(entries.size());
  while (selectorIndex < count && entries[selectorIndex].isSectionHeader) {
    selectorIndex++;
  }

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
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    return;
  }

  const int count = static_cast<int>(entries.size());

  buttonNavigator.onNext([this, count] {
    int next = ButtonNavigator::nextIndex(selectorIndex, count);
    int safety = count;
    while (safety-- > 0 && next < count && entries[next].isSectionHeader) {
      next = ButtonNavigator::nextIndex(next, count);
    }
    selectorIndex = next;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, count] {
    int prev = ButtonNavigator::previousIndex(selectorIndex, count);
    int safety = count;
    while (safety-- > 0 && prev >= 0 && entries[prev].isSectionHeader) {
      prev = ButtonNavigator::previousIndex(prev, count);
    }
    selectorIndex = prev;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < count && !entries[selectorIndex].isSectionHeader) {
      // Save last-used activity name
      if (categoryIndex >= 0) {
        char path[40];
        snprintf(path, sizeof(path), "/biscuit/lastused_%d.txt", categoryIndex);
        FsFile file;
        if (Storage.openFileForWrite("APPS", path, file)) {
          file.write((const uint8_t*)entries[selectorIndex].nameStrId, strlen(entries[selectorIndex].nameStrId));
          file.close();
        }
      }
      auto app = entries[selectorIndex].factory(renderer, mappedInput);
      if (app) {
        activityManager.pushActivity(std::move(app));
      }
    }
  }

  // Track that Back was physically pressed while this activity is active on screen
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    backPressedHere = true;
  }

  // Back button: short press = back one level, long press (1500ms+) = dashboard
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!backPressedHere) {
      // Stale release from child activity — ignore it
      return;
    }
    backPressedHere = false;
    if (mappedInput.getHeldTime() >= 1500) {
      onGoHome();  // Long-press: go straight to dashboard
    } else {
      finish();    // Short press: go back one level
    }
    return;
  }
}

void AppCategoryActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const int headerY = metrics.topPadding;

  if (disclaimerShown) {
    // Disclaimer screen
    GUI.drawHeader(renderer, Rect{0, headerY, pageWidth, metrics.headerHeight}, title);

    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DISCLAIMER));
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Item count subtitle (exclude section headers)
  const int count = static_cast<int>(entries.size());
  int appCount = 0;
  for (const auto& e : entries) {
    if (!e.isSectionHeader) appCount++;
  }
  char countStr[16];
  snprintf(countStr, sizeof(countStr), "%d items", appCount);

  GUI.drawHeader(renderer, Rect{0, headerY, pageWidth, metrics.headerHeight}, title, countStr);

  // === App list ===
  const int listTop = headerY + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, count, selectorIndex,
      [this](int index) -> std::string {
          if (entries[index].isSectionHeader) {
            return std::string("\xE2\x94\x80\xE2\x94\x80 ") + entries[index].nameStrId + " \xE2\x94\x80\xE2\x94\x80";
          }
          return entries[index].nameStrId;
      },
      [this](int index) -> std::string {
          if (entries[index].isSectionHeader) return "";
          return entries[index].description ? std::string(entries[index].description) : "";
      },
      [this](int index) -> UIIcon { return entries[index].icon; },
      [this](int index) -> std::string {
          if (entries[index].hasActiveState && entries[index].hasActiveState()) {
            return "\xE2\x97\x8F";  // ● (UTF-8 black circle)
          }
          return "";
      });

  // === Button hints ===
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
