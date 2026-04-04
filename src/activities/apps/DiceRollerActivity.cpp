#include "DiceRollerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <esp_random.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

constexpr int DiceRollerActivity::DIE_TYPES[];

uint32_t DiceRollerActivity::randomRange(uint32_t max) {
  return (esp_random() % max) + 1;
}

void DiceRollerActivity::onEnter() {
  Activity::onEnter();
  state = SELECT;
  dieTypeIndex = 1;
  dieCount = 1;
  diceResults.clear();
  total = 0;
  requestUpdate();
}

void DiceRollerActivity::onExit() { Activity::onExit(); }

void DiceRollerActivity::doRoll() {
  diceResults.clear();
  total = 0;
  int sides = DIE_TYPES[dieTypeIndex];
  for (int i = 0; i < dieCount; i++) {
    int val = static_cast<int>(randomRange(sides));
    diceResults.push_back(val);
    total += val;
  }
}

void DiceRollerActivity::loop() {
  if (state == SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    // Up/Down = change die type
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      dieTypeIndex = ButtonNavigator::previousIndex(dieTypeIndex, NUM_DIE_TYPES);
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      dieTypeIndex = ButtonNavigator::nextIndex(dieTypeIndex, NUM_DIE_TYPES);
      requestUpdate();
    }

    // Left/Right = change count
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (dieCount > 1) dieCount--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (dieCount < 6) dieCount++;
      requestUpdate();
    }

    // Confirm = roll
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = ROLLING;
      animFrame = 0;
      animStartMs = millis();
      requestUpdate();
    }
    return;
  }

  if (state == ROLLING) {
    unsigned long elapsed = millis() - animStartMs;
    int frame = static_cast<int>(elapsed / ANIM_FRAME_MS);
    if (frame >= ANIM_FRAMES) {
      doRoll();
      state = RESULT;
      requestUpdate();
    } else if (frame != animFrame) {
      animFrame = frame;
      // Show random preview values
      diceResults.clear();
      total = 0;
      int sides = DIE_TYPES[dieTypeIndex];
      for (int i = 0; i < dieCount; i++) {
        int val = static_cast<int>(randomRange(sides));
        diceResults.push_back(val);
        total += val;
      }
      requestUpdate();
    }
    return;
  }

  if (state == RESULT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = SELECT;
      requestUpdate();
      return;
    }
    // Confirm = roll again with same settings
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = ROLLING;
      animFrame = 0;
      animStartMs = millis();
      requestUpdate();
    }
  }
}

void DiceRollerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DICE_ROLLER));

  switch (state) {
    case SELECT:
      renderSelect();
      break;
    case ROLLING:
      renderRolling();
      break;
    case RESULT:
      renderResult();
      break;
  }

  renderer.displayBuffer();
}

void DiceRollerActivity::renderSelect() const {
  const auto pageHeight = renderer.getScreenHeight();
  int y = pageHeight / 2 - 50;

  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SELECT_DICE));
  y += 35;

  char dieBuf[32];
  snprintf(dieBuf, sizeof(dieBuf), "%dd%d", dieCount, DIE_TYPES[dieTypeIndex]);
  renderer.drawCenteredText(UI_12_FONT_ID, y, dieBuf, true, EpdFontFamily::BOLD);
  y += 40;

  renderer.drawCenteredText(SMALL_FONT_ID, y, "Up/Down: die type  Left/Right: count");

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ROLL), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Type", "Type");
}

void DiceRollerActivity::renderRolling() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_ROLLING), true, EpdFontFamily::BOLD);
}

void DiceRollerActivity::renderResult() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;

  // Die description
  char dieBuf[32];
  snprintf(dieBuf, sizeof(dieBuf), "%dd%d", dieCount, DIE_TYPES[dieTypeIndex]);
  renderer.drawCenteredText(UI_10_FONT_ID, y, dieBuf);
  y += lineH + 15;

  // Individual results
  if (dieCount > 1) {
    std::string resultStr;
    for (int i = 0; i < static_cast<int>(diceResults.size()); i++) {
      if (i > 0) resultStr += " + ";
      resultStr += std::to_string(diceResults[i]);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, y, resultStr.c_str());
    y += lineH + 15;
  }

  // Total - large
  char totalBuf[16];
  snprintf(totalBuf, sizeof(totalBuf), "%d", total);
  renderer.drawCenteredText(UI_12_FONT_ID, y, totalBuf, true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_REROLL), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
