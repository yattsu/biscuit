#include "LootBoxActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <cmath>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ================================================================
// ITEM DATABASE (stored in flash via static const)
// ================================================================

const LootBoxActivity::Item LootBoxActivity::ITEMS[50] = {
  // COMMON (0-19)
  {"Resistor",       COMMON,    0},
  {"Capacitor",      COMMON,    1},
  {"LED",            COMMON,    2},
  {"Battery",        COMMON,    3},
  {"Antenna",        COMMON,    4},
  {"USB Plug",       COMMON,    5},
  {"SD Card",        COMMON,    6},
  {"Floppy Disk",    COMMON,    7},
  {"Mouse",          COMMON,    8},
  {"Keyboard Key",   COMMON,    9},
  {"Pixel Heart",    COMMON,   10},
  {"Coffee Cup",     COMMON,   11},
  {"Light Bulb",     COMMON,   12},
  {"Wrench",         COMMON,   13},
  {"Magnifier",      COMMON,   14},
  {"Clock",          COMMON,   15},
  {"Envelope",       COMMON,   16},
  {"Star",           COMMON,   17},
  {"Moon",           COMMON,   18},
  {"Cloud",          COMMON,   19},
  // RARE (20-34)
  {"Microchip",      RARE,     20},
  {"Circuit Board",  RARE,     21},
  {"Router",         RARE,     22},
  {"Satellite",      RARE,     23},
  {"Robot Head",     RARE,     24},
  {"Game Boy",       RARE,     25},
  {"Oscilloscope",   RARE,     26},
  {"Soldering Iron", RARE,     27},
  {"Drone",          RARE,     28},
  {"VR Headset",     RARE,     29},
  {"Server Rack",    RARE,     30},
  {"Ethernet Jack",  RARE,     31},
  {"Raspberry Pi",   RARE,     32},
  {"Arduino",        RARE,     33},
  {"Logic Analyzer", RARE,     34},
  // EPIC (35-44)
  {"Golden Chip",    EPIC,     35},
  {"Cyber Eye",      EPIC,     36},
  {"Plasma Ball",    EPIC,     37},
  {"Hologram",       EPIC,     38},
  {"Quantum Bit",    EPIC,     39},
  {"Neural Net",     EPIC,     40},
  {"Infinity Loop",  EPIC,     41},
  {"Crypto Key",     EPIC,     42},
  {"Zero Day",       EPIC,     43},
  {"Black Box",      EPIC,     44},
  // LEGENDARY (45-49)
  {"The Kernel",     LEGENDARY, 45},
  {"Root Shell",     LEGENDARY, 46},
  {"Packet Ghost",   LEGENDARY, 47},
  {"E-Ink Dragon",   LEGENDARY, 48},
  {"biscuit. Logo",  LEGENDARY, 49},
};

// ================================================================
// DITHERING HELPERS
// ================================================================

void LootBoxActivity::fillDithered25(const GfxRenderer& r, int x, int y, int w, int h) {
  for (int dy = 0; dy < h; dy += 2)
    for (int dx = ((dy / 2) % 2); dx < w; dx += 2)
      r.drawPixel(x + dx, y + dy, true);
}

void LootBoxActivity::fillDithered50(const GfxRenderer& r, int x, int y, int w, int h) {
  for (int dy = 0; dy < h; dy++)
    for (int dx = (dy % 2); dx < w; dx += 2)
      r.drawPixel(x + dx, y + dy, true);
}

void LootBoxActivity::fillDithered75(const GfxRenderer& r, int x, int y, int w, int h) {
  for (int dy = 0; dy < h; dy++)
    for (int dx = ((dy + 1) % 2); dx < w; dx += 2)
      r.drawPixel(x + dx, y + dy, true);
  for (int dy = 1; dy < h; dy += 2)
    for (int dx = ((dy / 2) % 2); dx < w; dx += 2)
      r.drawPixel(x + dx, y + dy, true);
}

// ================================================================
// LIFECYCLE
// ================================================================

void LootBoxActivity::onEnter() {
  Activity::onEnter();
  screen = MAIN_MENU;
  menuIndex = 0;
  animState = ANIM_IDLE;
  loadCredits();
  loadCollection();
  requestUpdate();
}

void LootBoxActivity::onExit() {
  Activity::onExit();
  saveCredits();
  saveCollection();
}

// ================================================================
// CREDITS (shared with Casino, same file format)
// ================================================================

void LootBoxActivity::loadCredits() {
  Storage.mkdir("/biscuit");
  auto file = Storage.open(CASINO_SAVE_PATH);
  if (file && !file.isDirectory()) {
    uint8_t buf[4];
    if (file.read(buf, 4) == 4) {
      credits = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    }
    file.close();
  }
  if (credits <= 0) credits = 1000;
}

void LootBoxActivity::saveCredits() {
  auto file = Storage.open(CASINO_SAVE_PATH, O_WRITE | O_CREAT | O_TRUNC);
  if (file) {
    uint8_t buf[4] = {
      (uint8_t)(credits & 0xFF),
      (uint8_t)((credits >> 8) & 0xFF),
      (uint8_t)((credits >> 16) & 0xFF),
      (uint8_t)((credits >> 24) & 0xFF),
    };
    file.write(buf, 4);
    file.close();
  }
}

// ================================================================
// COLLECTION PERSISTENCE
// ================================================================

void LootBoxActivity::loadCollection() {
  auto file = Storage.open(COLLECTION_SAVE_PATH);
  if (file && !file.isDirectory()) {
    file.read(collected, 7);
    file.close();
  }
  totalCollected = countCollected();
}

void LootBoxActivity::saveCollection() {
  auto file = Storage.open(COLLECTION_SAVE_PATH, O_WRITE | O_CREAT | O_TRUNC);
  if (file) {
    file.write(collected, 7);
    file.close();
  }
}

bool LootBoxActivity::hasItem(int id) const {
  if (id < 0 || id >= ITEM_COUNT) return false;
  return (collected[id / 8] >> (id % 8)) & 1;
}

void LootBoxActivity::setItem(int id) {
  if (id < 0 || id >= ITEM_COUNT) return;
  collected[id / 8] |= (1 << (id % 8));
}

int LootBoxActivity::countCollected() const {
  int count = 0;
  for (int i = 0; i < ITEM_COUNT; i++)
    if (hasItem(i)) count++;
  return count;
}

// ================================================================
// GACHA LOGIC
// ================================================================

const char* LootBoxActivity::rarityName(Rarity r) {
  switch (r) {
    case COMMON:    return "Common";
    case RARE:      return "Rare";
    case EPIC:      return "Epic";
    case LEGENDARY: return "Legendary";
  }
  return "?";
}

LootBoxActivity::Rarity LootBoxActivity::rollRarity(bool guaranteeRare) {
  int roll = (int)(esp_random() % 100);
  if (roll < 3) return LEGENDARY;
  if (roll < 15) return EPIC;
  if (roll < 40) return RARE;
  if (guaranteeRare) return RARE;
  return COMMON;
}

int LootBoxActivity::rollItem(bool guaranteeRare) {
  Rarity r = rollRarity(guaranteeRare);
  int start, count;
  switch (r) {
    case COMMON:    start = 0;  count = COMMON_COUNT;    break;
    case RARE:      start = 20; count = RARE_COUNT;      break;
    case EPIC:      start = 35; count = EPIC_COUNT;      break;
    case LEGENDARY: start = 45; count = LEGENDARY_COUNT; break;
    default:        start = 0;  count = COMMON_COUNT;    break;
  }
  return start + (int)(esp_random() % count);
}

void LootBoxActivity::performSinglePull() {
  if (credits < SINGLE_COST) return;
  credits -= SINGLE_COST;
  pullCount = 1;
  pullResults[0] = rollItem(false);
  pullIsNew[0] = !hasItem(pullResults[0]);
  if (pullIsNew[0]) {
    setItem(pullResults[0]);
    totalCollected = countCollected();
  } else {
    credits += 25;
  }
  saveCredits();
  saveCollection();
  animState = ANIM_SHAKING;
  animFrame = 0;
  animStartMs = millis();
  screen = PULLING;
  requestUpdate();
}

void LootBoxActivity::performMultiPull() {
  if (credits < MULTI_COST) return;
  credits -= MULTI_COST;
  pullCount = MULTI_COUNT;
  bool hasRareOrBetter = false;
  for (int i = 0; i < MULTI_COUNT; i++) {
    bool guarantee = (i == MULTI_COUNT - 1 && !hasRareOrBetter);
    pullResults[i] = rollItem(guarantee);
    pullIsNew[i] = !hasItem(pullResults[i]);
    if (pullIsNew[i]) {
      setItem(pullResults[i]);
    } else {
      credits += 25;
    }
    if (ITEMS[pullResults[i]].rarity >= RARE) hasRareOrBetter = true;
  }
  totalCollected = countCollected();
  saveCredits();
  saveCollection();
  animState = ANIM_SHAKING;
  animFrame = 0;
  animStartMs = millis();
  revealIndex = 0;
  screen = PULLING;
  requestUpdate();
}

// ================================================================
// MAIN LOOP
// ================================================================

void LootBoxActivity::loop() {
  switch (screen) {
    case MAIN_MENU: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { finish(); return; }
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        menuIndex = ButtonNavigator::previousIndex(menuIndex, 3);
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        menuIndex = ButtonNavigator::nextIndex(menuIndex, 3);
        requestUpdate();
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        switch (menuIndex) {
          case 0: performSinglePull(); break;
          case 1: performMultiPull(); break;
          case 2:
            screen = COLLECTION;
            collectionPage = 0;
            collectionCursor = 0;
            requestUpdate();
            break;
        }
      }
      break;
    }
    case PULLING: {
      unsigned long elapsed = millis() - animStartMs;
      if (animState == ANIM_SHAKING) {
        int frame = (int)(elapsed / SHAKE_FRAME_MS);
        if (frame >= SHAKE_FRAMES) {
          animState = ANIM_OPENING;
          animFrame = 0;
          animStartMs = millis();
          requestUpdate();
        } else if (frame != animFrame) {
          animFrame = frame;
          requestUpdate();
        }
      } else if (animState == ANIM_OPENING) {
        int frame = (int)(elapsed / OPEN_FRAME_MS);
        if (frame >= OPEN_FRAMES) {
          animState = ANIM_REVEALED;
          if (pullCount == 1) {
            screen = REVEAL_SINGLE;
          } else {
            screen = REVEAL_MULTI;
            revealIndex = 0;
          }
          requestUpdate();
        } else if (frame != animFrame) {
          animFrame = frame;
          requestUpdate();
        }
      }
      break;
    }
    case REVEAL_SINGLE: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        screen = MAIN_MENU;
        requestUpdate();
      }
      break;
    }
    case REVEAL_MULTI: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        screen = MAIN_MENU;
        requestUpdate();
        break;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        revealIndex++;
        if (revealIndex >= pullCount) {
          screen = MAIN_MENU;
        }
        requestUpdate();
      }
      break;
    }
    case COLLECTION: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        screen = MAIN_MENU;
        requestUpdate();
        break;
      }
      int totalPages = (ITEM_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
      if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        collectionCursor++;
        if (collectionCursor >= ITEMS_PER_PAGE) {
          collectionCursor = 0;
          collectionPage = (collectionPage + 1) % totalPages;
        }
        int itemId = collectionPage * ITEMS_PER_PAGE + collectionCursor;
        if (itemId >= ITEM_COUNT) { collectionPage = 0; collectionCursor = 0; }
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        collectionCursor--;
        if (collectionCursor < 0) {
          collectionCursor = ITEMS_PER_PAGE - 1;
          collectionPage = (collectionPage - 1 + totalPages) % totalPages;
        }
        int itemId = collectionPage * ITEMS_PER_PAGE + collectionCursor;
        if (itemId >= ITEM_COUNT) {
          collectionCursor = (ITEM_COUNT - 1) % ITEMS_PER_PAGE;
          collectionPage = (ITEM_COUNT - 1) / ITEMS_PER_PAGE;
        }
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        collectionCursor -= GRID_COLS;
        if (collectionCursor < 0) {
          collectionPage = (collectionPage - 1 + totalPages) % totalPages;
          collectionCursor += ITEMS_PER_PAGE;
          int itemId = collectionPage * ITEMS_PER_PAGE + collectionCursor;
          if (itemId >= ITEM_COUNT) {
            collectionCursor = (ITEM_COUNT - 1) % ITEMS_PER_PAGE;
            collectionPage = (ITEM_COUNT - 1) / ITEMS_PER_PAGE;
          }
        }
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        collectionCursor += GRID_COLS;
        if (collectionCursor >= ITEMS_PER_PAGE) {
          collectionCursor -= ITEMS_PER_PAGE;
          collectionPage = (collectionPage + 1) % totalPages;
        }
        int itemId = collectionPage * ITEMS_PER_PAGE + collectionCursor;
        if (itemId >= ITEM_COUNT) { collectionPage = 0; collectionCursor = 0; }
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
        collectionPage = (collectionPage + 1) % totalPages;
        collectionCursor = 0;
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
        collectionPage = (collectionPage - 1 + totalPages) % totalPages;
        collectionCursor = 0;
        requestUpdate();
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        int itemId = collectionPage * ITEMS_PER_PAGE + collectionCursor;
        if (itemId < ITEM_COUNT && hasItem(itemId)) {
          detailItemId = itemId;
          screen = ITEM_DETAIL;
          requestUpdate();
        }
      }
      break;
    }
    case ITEM_DETAIL: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        screen = COLLECTION;
        requestUpdate();
      }
      break;
    }
  }
}

// ================================================================
// RENDER DISPATCHER
// ================================================================

void LootBoxActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (screen) {
    case MAIN_MENU:     renderMainMenu();     break;
    case PULLING:       renderPulling();      break;
    case REVEAL_SINGLE: renderRevealSingle(); break;
    case REVEAL_MULTI:  renderRevealMulti();  break;
    case COLLECTION:    renderCollection();   break;
    case ITEM_DETAIL:   renderItemDetail();   break;
  }

  if (screen == REVEAL_SINGLE || screen == REVEAL_MULTI ||
      screen == ITEM_DETAIL   || screen == COLLECTION) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}

// ================================================================
// RENDER: MAIN MENU
// ================================================================

void LootBoxActivity::renderMainMenu() {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Loot Box", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);

  char buf[32];
  snprintf(buf, sizeof(buf), "Credits: %d", (int)credits);
  renderer.drawText(UI_10_FONT_ID, 15, 50, buf, true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 72, pageWidth, 72);

  drawLootBox(pageWidth / 2, 250, 140, 0);

  int menuY = 400;
  const int menuSpacing = 45;

  char singleBuf[40], multiBuf[40], collBuf[40];
  snprintf(singleBuf, sizeof(singleBuf), "Single Pull (%d)", SINGLE_COST);
  snprintf(multiBuf,  sizeof(multiBuf),  "5x Pull (%d)",    MULTI_COST);
  snprintf(collBuf,   sizeof(collBuf),   "Collection (%d/%d)", totalCollected, ITEM_COUNT);

  const char* items[] = {singleBuf, multiBuf, collBuf};
  for (int i = 0; i < 3; i++) {
    int y = menuY + i * menuSpacing;
    if (i == menuIndex) {
      renderer.fillRect(30, y - 2, pageWidth - 60, 30, true);
      renderer.drawCenteredText(UI_10_FONT_ID, y, items[i], false, EpdFontFamily::BOLD);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, y, items[i]);
    }
  }

  if (menuIndex == 0 && credits < SINGLE_COST) {
    renderer.drawCenteredText(SMALL_FONT_ID, menuY + 3 * menuSpacing + 10, "Not enough credits!");
  } else if (menuIndex == 1 && credits < MULTI_COST) {
    renderer.drawCenteredText(SMALL_FONT_ID, menuY + 3 * menuSpacing + 10, "Not enough credits!");
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ================================================================
// RENDER: PULLING ANIMATION
// ================================================================

void LootBoxActivity::renderPulling() {
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  int cx = pageWidth / 2;
  int cy = pageHeight / 2 - 50;

  if (animState == ANIM_SHAKING) {
    static constexpr int shakeOffsets[] = {6, -6, 8, -8};
    int offset = shakeOffsets[animFrame % 4];
    drawLootBox(cx, cy, 160, offset);
    renderer.drawCenteredText(UI_10_FONT_ID, cy + 120, "Opening...");
  } else if (animState == ANIM_OPENING) {
    int lidOffset = (animFrame + 1) * 15;
    int x = cx - 80;
    int y = cy - 80;

    // Shadow
    fillDithered50(renderer, x + 4, y + 4 + 54, 160, 106);
    // Box body
    renderer.fillRect(x, y + 54, 160, 106, false);
    renderer.drawRect(x, y + 54, 160, 106);
    renderer.drawRect(x + 2, y + 56, 156, 102);
    // "?" on front
    renderer.drawCenteredText(UI_12_FONT_ID, cy + 20, "?", true, EpdFontFamily::BOLD);

    // Lid floating up
    int lidY = y - lidOffset;
    int lidX = x - 4 + (animFrame * 5);
    renderer.fillRect(lidX, lidY, 168, 54, false);
    renderer.drawRect(lidX, lidY, 168, 54);

    if (animFrame >= 2) {
      // Starburst effect
      for (int i = 0; i < 8; i++) {
        float angle = i * 0.7854f;
        int ex = cx + (int)(60 * cosf(angle));
        int ey = cy + (int)(60 * sinf(angle));
        renderer.drawLine(cx, cy, ex, ey);
      }
    }
    renderer.drawCenteredText(UI_10_FONT_ID, cy + 120, "Opening...");
  }
}

// ================================================================
// RENDER: REVEAL SINGLE
// ================================================================

void LootBoxActivity::renderRevealSingle() {
  const auto pageWidth  = renderer.getScreenWidth();
  int itemId = pullResults[0];
  const Item& item = ITEMS[itemId];
  bool isNew = pullIsNew[0];

  if (isNew) {
    renderer.drawCenteredText(UI_12_FONT_ID, 20, "NEW ITEM!", true, EpdFontFamily::BOLD);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, 20, "DUPLICATE", true, EpdFontFamily::BOLD);
  }
  renderer.drawLine(15, 50, pageWidth - 15, 50);

  int frameX = pageWidth / 2 - 80;
  int frameY = 80;
  int frameW = 160;
  int frameH = 160;
  drawRarityBorder(frameX, frameY, frameW, frameH, item.rarity);

  int iconCx = pageWidth / 2;
  int iconCy = frameY + frameH / 2;
  if (isNew) {
    for (int i = 0; i < 8; i++) {
      float angle = i * 0.7854f;
      int ex = iconCx + (int)(90 * cosf(angle));
      int ey = iconCy + (int)(90 * sinf(angle));
      renderer.drawLine(iconCx, iconCy, ex, ey);
    }
  }

  drawItemIcon(frameX + 20, frameY + 20, frameW - 40, item.iconId, false);

  int starY = frameY + frameH + 20;
  drawStars(pageWidth / 2, starY, item.rarity);

  char rarBuf[32];
  snprintf(rarBuf, sizeof(rarBuf), "~ %s ~", rarityName(item.rarity));
  renderer.drawCenteredText(UI_10_FONT_ID, starY + 30, rarBuf, true, EpdFontFamily::BOLD);

  renderer.drawCenteredText(UI_12_FONT_ID, starY + 65, item.name, true, EpdFontFamily::BOLD);

  if (!isNew) {
    renderer.drawCenteredText(SMALL_FONT_ID, starY + 100, "+25 credits refunded");
  }

  char progBuf[32];
  snprintf(progBuf, sizeof(progBuf), "%d/%d Collected", totalCollected, ITEM_COUNT);
  renderer.drawCenteredText(SMALL_FONT_ID, starY + 130, progBuf);

  const auto labels = mappedInput.mapLabels("Back", "OK", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ================================================================
// RENDER: REVEAL MULTI
// ================================================================

void LootBoxActivity::renderRevealMulti() {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, "5x PULL RESULTS", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 45, pageWidth - 15, 45);

  int slotSize = 70;
  int spacing  = 10;
  int totalW   = MULTI_COUNT * slotSize + (MULTI_COUNT - 1) * spacing;
  int startX   = (pageWidth - totalW) / 2;
  int slotY    = 80;

  for (int i = 0; i < MULTI_COUNT; i++) {
    int sx = startX + i * (slotSize + spacing);

    if (i <= revealIndex) {
      int itemId = pullResults[i];
      const Item& item = ITEMS[itemId];
      drawRarityBorder(sx, slotY, slotSize, slotSize, item.rarity);
      drawItemIcon(sx + 8, slotY + 8, slotSize - 16, item.iconId, false);

      // Name below slot
      const char* name = item.name;
      int tw = renderer.getTextWidth(SMALL_FONT_ID, name);
      if (tw <= slotSize + 10) {
        renderer.drawText(SMALL_FONT_ID, sx + (slotSize - tw) / 2, slotY + slotSize + 4, name);
      } else {
        char shortName[8];
        strncpy(shortName, name, 6);
        shortName[6] = '.';
        shortName[7] = '\0';
        int tw2 = renderer.getTextWidth(SMALL_FONT_ID, shortName);
        renderer.drawText(SMALL_FONT_ID, sx + (slotSize - tw2) / 2, slotY + slotSize + 4, shortName);
      }

      // NEW badge
      if (pullIsNew[i]) {
        renderer.fillRect(sx, slotY - 12, 28, 12, true);
        renderer.drawText(SMALL_FONT_ID, sx + 2, slotY - 12, "NEW", false);
      }

      // Star rating
      int starCount = 1;
      if (item.rarity == RARE)      starCount = 2;
      else if (item.rarity == EPIC) starCount = 3;
      else if (item.rarity == LEGENDARY) starCount = 5;
      char starBuf[6] = {};
      int sc = starCount < 5 ? starCount : 5;
      for (int si = 0; si < sc; si++) starBuf[si] = '*';
      starBuf[sc] = '\0';
      int stw = renderer.getTextWidth(SMALL_FONT_ID, starBuf);
      renderer.drawText(SMALL_FONT_ID, sx + (slotSize - stw) / 2, slotY + slotSize + 18, starBuf);
    } else {
      // Unrevealed: dithered box with "?"
      fillDithered50(renderer, sx + 2, slotY + 2, slotSize - 4, slotSize - 4);
      renderer.drawRect(sx, slotY, slotSize, slotSize);
      char qm[] = "?";
      int tw = renderer.getTextWidth(UI_12_FONT_ID, qm, EpdFontFamily::BOLD);
      // Clear dither in text area and redraw text at correct position
      renderer.fillRect(sx + 4, slotY + slotSize / 2 - 12, slotSize - 8, 24, false);
      fillDithered50(renderer, sx + 4, slotY + slotSize / 2 - 12, slotSize - 8, 24);
      renderer.drawText(UI_12_FONT_ID, sx + (slotSize - tw) / 2,
                        slotY + slotSize / 2 - 10, qm, true, EpdFontFamily::BOLD);
    }
  }

  char progBuf[32];
  snprintf(progBuf, sizeof(progBuf), "Revealing: %d of %d", revealIndex + 1, MULTI_COUNT);
  renderer.drawCenteredText(UI_10_FONT_ID, slotY + slotSize + 50, progBuf);

  if (revealIndex >= MULTI_COUNT - 1) {
    int newCount = 0, dupeCount = 0;
    for (int i = 0; i < MULTI_COUNT; i++) {
      if (pullIsNew[i]) newCount++; else dupeCount++;
    }
    char sumBuf[64];
    if (dupeCount > 0) {
      snprintf(sumBuf, sizeof(sumBuf), "%d new! %d dupes (+%d credits)", newCount, dupeCount, dupeCount * 25);
    } else {
      snprintf(sumBuf, sizeof(sumBuf), "%d new items!", newCount);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, slotY + slotSize + 80, sumBuf, true, EpdFontFamily::BOLD);
  }

  const char* confirmLabel = (revealIndex >= MULTI_COUNT - 1) ? "Done" : "Next";
  const auto labels = mappedInput.mapLabels("Back", confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ================================================================
// RENDER: COLLECTION GRID
// ================================================================

void LootBoxActivity::renderCollection() {
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  int totalPages = (ITEM_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;

  char hdrBuf[40];
  snprintf(hdrBuf, sizeof(hdrBuf), "Collection (%d/%d)", totalCollected, ITEM_COUNT);
  renderer.drawCenteredText(UI_12_FONT_ID, 12, hdrBuf, true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);

  int cellSize    = 70;
  int cellSpacing = 12;
  int gridW       = GRID_COLS * cellSize + (GRID_COLS - 1) * cellSpacing;
  int gridStartX  = (pageWidth - gridW) / 2;
  int gridStartY  = 60;

  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      int idx    = row * GRID_COLS + col;
      int itemId = collectionPage * ITEMS_PER_PAGE + idx;
      if (itemId >= ITEM_COUNT) continue;

      int cx = gridStartX + col * (cellSize + cellSpacing);
      int cy = gridStartY + row * (cellSize + cellSpacing + 20);

      bool owned    = hasItem(itemId);
      bool selected = (idx == collectionCursor);

      if (selected) {
        renderer.drawRect(cx - 3, cy - 3, cellSize + 6, cellSize + 6);
        renderer.drawRect(cx - 2, cy - 2, cellSize + 4, cellSize + 4);
      }

      renderer.drawRect(cx, cy, cellSize, cellSize);
      drawItemIcon(cx + 5, cy + 5, cellSize - 10, ITEMS[itemId].iconId, !owned);

      if (owned) {
        const char* name = ITEMS[itemId].name;
        int tw = renderer.getTextWidth(SMALL_FONT_ID, name);
        if (tw <= cellSize + 10) {
          renderer.drawText(SMALL_FONT_ID, cx + (cellSize - tw) / 2, cy + cellSize + 2, name);
        } else {
          char shortName[7];
          strncpy(shortName, name, 5);
          shortName[5] = '.';
          shortName[6] = '\0';
          int tw2 = renderer.getTextWidth(SMALL_FONT_ID, shortName);
          renderer.drawText(SMALL_FONT_ID, cx + (cellSize - tw2) / 2, cy + cellSize + 2, shortName);
        }
      } else {
        renderer.drawText(SMALL_FONT_ID, cx + cellSize / 2 - 8, cy + cellSize + 2, "???");
      }
    }
  }

  // Progress bar
  int barY  = pageHeight - 120;
  int barW  = pageWidth - 80;
  int barX  = 40;
  int barH  = 16;
  renderer.drawRect(barX, barY, barW, barH);
  int fillW = (barW - 4) * totalCollected / ITEM_COUNT;
  if (fillW > 0) {
    fillDithered50(renderer, barX + 2, barY + 2, fillW, barH - 4);
  }

  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", collectionPage + 1, totalPages);
  renderer.drawCenteredText(SMALL_FONT_ID, barY + 22, pageBuf);

  const auto labels = mappedInput.mapLabels("Back", "Detail", "Left", "Right");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Pg-", "Pg+");
}

// ================================================================
// RENDER: ITEM DETAIL
// ================================================================

void LootBoxActivity::renderItemDetail() {
  if (detailItemId < 0 || detailItemId >= ITEM_COUNT) return;
  const auto pageWidth = renderer.getScreenWidth();
  const Item& item = ITEMS[detailItemId];

  int frameX = pageWidth / 2 - 110;
  int frameY = 50;
  int frameW = 220;
  int frameH = 220;
  drawRarityBorder(frameX, frameY, frameW, frameH, item.rarity);

  drawItemIcon(frameX + 30, frameY + 30, frameW - 60, item.iconId, false);

  int infoY = frameY + frameH + 20;
  drawStars(pageWidth / 2, infoY, item.rarity);

  char rarBuf[32];
  snprintf(rarBuf, sizeof(rarBuf), "~ %s ~", rarityName(item.rarity));
  renderer.drawCenteredText(UI_10_FONT_ID, infoY + 30, rarBuf, true, EpdFontFamily::BOLD);

  renderer.drawCenteredText(UI_12_FONT_ID, infoY + 65, item.name, true, EpdFontFamily::BOLD);

  char numBuf[16];
  snprintf(numBuf, sizeof(numBuf), "#%d of %d", detailItemId + 1, ITEM_COUNT);
  renderer.drawCenteredText(SMALL_FONT_ID, infoY + 100, numBuf);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ================================================================
// DRAW: LOOT BOX
// ================================================================

void LootBoxActivity::drawLootBox(int cx, int cy, int size, int shakeOffset) const {
  int x = cx - size / 2 + shakeOffset;
  int y = cy - size / 2;

  fillDithered50(renderer, x + 4, y + 4, size, size);

  // Box body
  renderer.fillRect(x, y + size / 3, size, size * 2 / 3, false);
  renderer.drawRect(x, y + size / 3, size, size * 2 / 3);
  renderer.drawRect(x + 2, y + size / 3 + 2, size - 4, size * 2 / 3 - 4);

  // Lid
  renderer.fillRect(x - 4, y, size + 8, size / 3, false);
  renderer.drawRect(x - 4, y, size + 8, size / 3);
  renderer.drawRect(x - 2, y + 2, size + 4, size / 3 - 4);

  // Ribbon cross
  renderer.fillRect(cx + shakeOffset - 2, y, 5, size, true);
  renderer.fillRect(x, cy - 2, size, 5, true);

  renderer.drawCenteredText(UI_12_FONT_ID, cy + size / 6, "?", true, EpdFontFamily::BOLD);
}

// ================================================================
// DRAW: RARITY BORDER
// ================================================================

void LootBoxActivity::drawRarityBorder(int x, int y, int w, int h, Rarity rarity) const {
  switch (rarity) {
    case COMMON:
      renderer.drawRect(x, y, w, h);
      break;
    case RARE:
      renderer.drawRect(x, y, w, h);
      renderer.drawRect(x + 3, y + 3, w - 6, h - 6);
      break;
    case EPIC:
      renderer.drawRect(x, y, w, h);
      renderer.drawRect(x + 1, y + 1, w - 2, h - 2);
      fillDithered25(renderer, x + 3, y + 3, w - 6, 3);
      fillDithered25(renderer, x + 3, y + h - 6, w - 6, 3);
      fillDithered25(renderer, x + 3, y + 3, 3, h - 6);
      fillDithered25(renderer, x + w - 6, y + 3, 3, h - 6);
      renderer.drawRect(x + 6, y + 6, w - 12, h - 12);
      break;
    case LEGENDARY: {
      renderer.fillRect(x, y, w, h, true);
      renderer.fillRect(x + 4, y + 4, w - 8, h - 8, false);
      fillDithered50(renderer, x + 5, y + 5, w - 10, 2);
      fillDithered50(renderer, x + 5, y + h - 7, w - 10, 2);
      fillDithered50(renderer, x + 5, y + 5, 2, h - 10);
      fillDithered50(renderer, x + w - 7, y + 5, 2, h - 10);
      int corners[4][2] = {{x+2, y+2}, {x+w-4, y+2}, {x+2, y+h-4}, {x+w-4, y+h-4}};
      for (int ci = 0; ci < 4; ci++) {
        int cx2 = corners[ci][0];
        int cy2 = corners[ci][1];
        renderer.drawPixel(cx2 + 1, cy2,     false);
        renderer.drawPixel(cx2,     cy2 + 1, false);
        renderer.drawPixel(cx2 + 2, cy2 + 1, false);
        renderer.drawPixel(cx2 + 1, cy2 + 2, false);
      }
      break;
    }
  }
}

// ================================================================
// DRAW: STARS
// ================================================================

void LootBoxActivity::drawStars(int cx, int y, Rarity rarity) const {
  int count = 1;
  if (rarity == RARE)      count = 2;
  else if (rarity == EPIC) count = 3;
  else if (rarity == LEGENDARY) count = 5;

  int starSpacing = 20;
  int startX = cx - (count - 1) * starSpacing / 2;

  for (int i = 0; i < count; i++) {
    int sx = startX + i * starSpacing;
    int r  = 6;
    for (int a = 0; a < 5; a++) {
      float angle = -1.5708f + a * 1.2566f;
      int tx = sx + (int)(r * cosf(angle));
      int ty = y  + (int)(r * sinf(angle));
      renderer.drawLine(sx, y, tx, ty);
    }
  }
}

// ================================================================
// DRAW: ITEM ICON
// ================================================================

void LootBoxActivity::drawItemIcon(int x, int y, int size, int iconId, bool locked) const {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int s  = size / 3;

  if (locked) {
    fillDithered75(renderer, x + 2, y + 2, size - 4, size - 4);
    int lw = s, lh = s;
    renderer.fillRect(cx - lw / 2, cy, lw, lh, false);
    renderer.drawRect(cx - lw / 2, cy, lw, lh);
    renderer.drawRect(cx - lw / 4, cy - lh / 2, lw / 2, lh / 2);
    renderer.fillRect(cx - 1, cy + lh / 3, 3, 3, true);
    return;
  }

  switch (iconId) {
    case 0: { // Resistor
      renderer.drawLine(x + 4, cy, cx - s, cy);
      for (int i = 0; i < 4; i++) {
        int bx = cx - s + i * s / 2;
        renderer.drawLine(bx,          cy,          bx + s / 4, cy - s / 2);
        renderer.drawLine(bx + s / 4,  cy - s / 2,  bx + s / 2, cy + s / 2);
      }
      renderer.drawLine(cx + s, cy, x + size - 4, cy);
      break;
    }
    case 1: { // Capacitor
      renderer.fillRect(cx - s / 3 - 2, cy - s, 3, s * 2, true);
      renderer.fillRect(cx + s / 3,     cy - s, 3, s * 2, true);
      renderer.drawLine(x + 4, cy, cx - s / 3 - 2, cy);
      renderer.drawLine(cx + s / 3 + 3, cy, x + size - 4, cy);
      break;
    }
    case 2: { // LED
      renderer.drawLine(cx - s / 2, cy - s, cx - s / 2, cy + s);
      renderer.drawLine(cx - s / 2, cy - s, cx + s / 2, cy);
      renderer.drawLine(cx - s / 2, cy + s, cx + s / 2, cy);
      renderer.fillRect(cx + s / 2, cy - s, 2, s * 2, true);
      for (int i = -1; i <= 1; i++)
        renderer.drawLine(cx + s / 2 + 3, cy + i * s / 2, cx + s, cy + i * s / 2 - s / 3);
      break;
    }
    case 3: { // Battery
      renderer.drawRect(cx - s, cy - s / 2, s * 3 / 2, s);
      renderer.fillRect(cx + s / 2 + 1, cy - s / 4, s / 3, s / 2, true);
      renderer.drawLine(cx - s / 2, cy - s / 4, cx - s / 2, cy + s / 4);
      renderer.drawLine(cx - s + 3,          cy - s / 6, cx - s + 3 + s / 4, cy - s / 6);
      renderer.drawLine(cx - s + 3 + s / 8,  cy - s / 3, cx - s + 3 + s / 8, cy);
      break;
    }
    case 4: { // Antenna
      renderer.drawLine(cx, cy + s, cx, cy - s / 2);
      renderer.drawLine(cx, cy - s / 2, cx - s / 2, cy - s);
      renderer.drawLine(cx, cy - s / 2, cx + s / 2, cy - s);
      renderer.drawLine(cx + s / 2 + 2, cy - s / 3, cx + s / 2 + 4, cy - s / 2);
      renderer.drawLine(cx + s / 2 + 5, cy - s / 4, cx + s / 2 + 7, cy - s / 2);
      break;
    }
    case 5: { // USB Plug
      renderer.drawRect(cx - s / 2, cy - s, s, s * 2);
      renderer.fillRect(cx - s / 4, cy - s - s / 3, s / 2, s / 3, true);
      renderer.drawLine(cx - s / 6, cy - s / 2, cx - s / 6, cy + s / 2);
      renderer.drawLine(cx + s / 6, cy - s / 2, cx + s / 6, cy + s / 2);
      break;
    }
    case 6: { // SD Card — rectangle with angled corner and lines
      renderer.drawRect(cx - s / 2, cy - s, s, s * 2);
      renderer.drawLine(cx - s / 2, cy - s, cx - s / 4, cy - s - s / 3);
      renderer.drawLine(cx - s / 4, cy - s - s / 3, cx + s / 2, cy - s - s / 3);
      renderer.fillRect(cx - s / 4, cy - s / 2, s / 2, 2, true);
      renderer.fillRect(cx - s / 4, cy,          s / 2, 2, true);
      break;
    }
    case 7: { // Floppy Disk — square with notch and label
      renderer.drawRect(cx - s, cy - s, s * 2, s * 2);
      renderer.fillRect(cx - s / 2, cy - s, s, s / 2, true);
      renderer.fillRect(cx - s / 3, cy + s / 4, s * 2 / 3, s * 3 / 4, false);
      renderer.drawRect(cx - s / 3, cy + s / 4, s * 2 / 3, s * 3 / 4);
      break;
    }
    case 8: { // Mouse — rounded rect with button divider
      renderer.drawRect(cx - s / 2, cy - s, s, s * 2);
      renderer.drawLine(cx, cy - s, cx, cy - s / 3);
      renderer.drawLine(cx - s / 2, cy - s / 3, cx + s / 2, cy - s / 3);
      renderer.drawLine(cx - s / 2, cy + s, cx + s / 2, cy + s);
      break;
    }
    case 9: { // Keyboard Key — square with inset and letter
      renderer.drawRect(cx - s / 2, cy - s / 2, s, s);
      renderer.drawRect(cx - s / 2 + 2, cy - s / 2 + 2, s - 4, s - 4);
      char key[] = "A";
      int tw = renderer.getTextWidth(SMALL_FONT_ID, key);
      renderer.drawText(SMALL_FONT_ID, cx - tw / 2, cy - 5, key, true, EpdFontFamily::BOLD);
      break;
    }
    case 10: { // Pixel Heart
      for (int dy = -s; dy <= s; dy++) {
        for (int dx = -s; dx <= s; dx++) {
          int lx = dx + s / 3, ly = dy + s / 3;
          int rx = dx - s / 3, ry = dy + s / 3;
          bool lb  = (lx * lx + ly * ly * 2) <= s * s / 2;
          bool rb  = (rx * rx + ry * ry * 2) <= s * s / 2;
          bool tri = (dy >= 0) && (abs(dx) <= s - dy);
          if (lb || rb || tri) renderer.drawPixel(cx + dx, cy + dy, true);
        }
      }
      break;
    }
    case 11: { // Coffee Cup
      renderer.drawRect(cx - s / 2, cy - s / 2, s, s + s / 3);
      renderer.drawRect(cx + s / 2, cy - s / 4, s / 3, s / 2);
      for (int i = 0; i < 3; i++) {
        int sx2 = cx - s / 4 + i * s / 4;
        renderer.drawPixel(sx2,     cy - s / 2 - 2, true);
        renderer.drawPixel(sx2 + 1, cy - s / 2 - 4, true);
        renderer.drawPixel(sx2,     cy - s / 2 - 6, true);
      }
      break;
    }
    case 12: { // Light Bulb — circle top, rect base
      for (int a = 0; a < 360; a += 10) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s / 2 * cosf(rad)),
                           cy - s / 4 + (int)(s / 2 * sinf(rad)), true);
      }
      renderer.fillRect(cx - s / 4, cy + s / 4, s / 2, s / 3, true);
      break;
    }
    case 13: { // Wrench — diagonal line with open end
      renderer.drawLine(cx - s, cy + s, cx + s / 2, cy - s / 2);
      renderer.drawLine(cx + s / 2, cy - s / 2, cx + s, cy - s / 3);
      renderer.drawLine(cx + s / 2, cy - s / 2, cx + s / 3, cy - s);
      break;
    }
    case 14: { // Magnifier — circle with handle
      for (int a = 0; a < 360; a += 10) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx - s / 4 + (int)(s / 2 * cosf(rad)),
                           cy - s / 4 + (int)(s / 2 * sinf(rad)), true);
      }
      renderer.drawLine(cx + s / 6,     cy + s / 6,     cx + s,     cy + s);
      renderer.drawLine(cx + s / 6 + 1, cy + s / 6,     cx + s + 1, cy + s);
      break;
    }
    case 15: { // Clock — circle with hands
      for (int a = 0; a < 360; a += 10) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * cosf(rad)), cy + (int)(s * sinf(rad)), true);
      }
      renderer.drawLine(cx, cy, cx,          cy - s * 2 / 3);
      renderer.drawLine(cx, cy, cx + s / 2,  cy);
      break;
    }
    case 16: { // Envelope — rectangle with V
      renderer.drawRect(cx - s, cy - s / 2, s * 2, s);
      renderer.drawLine(cx - s, cy - s / 2, cx, cy + s / 4);
      renderer.drawLine(cx + s, cy - s / 2, cx, cy + s / 4);
      break;
    }
    case 17: { // Star (5-pointed spokes)
      for (int i = 0; i < 5; i++) {
        float angle = -1.5708f + i * 1.2566f;
        int tx = cx + (int)(s * cosf(angle));
        int ty = cy + (int)(s * sinf(angle));
        renderer.drawLine(cx, cy, tx, ty);
      }
      break;
    }
    case 18: { // Moon — crescent via circle minus offset circle
      for (int a = 0; a < 360; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * cosf(rad)), cy + (int)(s * sinf(rad)), true);
      }
      for (int dy2 = -s; dy2 <= s; dy2++) {
        for (int dx2 = -s; dx2 <= s; dx2++) {
          if (dx2 * dx2 + dy2 * dy2 <= (s - 2) * (s - 2)) {
            int ox = dx2 + s / 3;
            if (ox * ox + dy2 * dy2 <= s * s)
              renderer.drawPixel(cx + dx2 + s / 3, cy + dy2, false);
          }
        }
      }
      break;
    }
    case 19: { // Cloud — overlapping circles
      for (int i = -1; i <= 1; i++) {
        for (int a = 0; a < 360; a += 10) {
          float rad = a * 3.14159f / 180.0f;
          int r2 = s / 2;
          renderer.drawPixel(cx + i * s / 3 + (int)(r2 * cosf(rad)),
                             cy + (int)(r2 * sinf(rad)), true);
        }
      }
      for (int a = 0; a < 360; a += 10) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s / 3 * cosf(rad)),
                           cy - s / 3 + (int)(s / 3 * sinf(rad)), true);
      }
      break;
    }
    case 20: { // Microchip — square with pins
      renderer.drawRect(cx - s / 2, cy - s / 2, s, s);
      for (int i = 0; i < 4; i++) {
        int py = cy - s / 2 + 2 + i * (s - 4) / 3;
        renderer.fillRect(cx - s / 2 - s / 4, py, s / 4, 2, true);
        renderer.fillRect(cx + s / 2,          py, s / 4, 2, true);
      }
      renderer.fillRect(cx - s / 2 + 2, cy - s / 2 + 2, 3, 3, true);
      break;
    }
    case 21: { // Circuit Board — grid of traces
      renderer.drawRect(cx - s, cy - s, s * 2, s * 2);
      renderer.drawLine(cx - s / 2, cy - s, cx - s / 2, cy);
      renderer.drawLine(cx - s / 2, cy, cx + s / 2, cy);
      renderer.drawLine(cx + s / 2, cy, cx + s / 2, cy + s);
      renderer.fillRect(cx - s / 2 - 2, cy - 2, 4, 4, true);
      renderer.fillRect(cx + s / 2 - 2, cy + s - 2, 4, 4, true);
      break;
    }
    case 22: { // Router — box with antennas and LEDs
      renderer.drawRect(cx - s, cy, s * 2, s / 2);
      renderer.drawLine(cx - s / 2, cy, cx - s / 3, cy - s);
      renderer.drawLine(cx + s / 2, cy, cx + s / 3, cy - s);
      renderer.fillRect(cx - s / 3 - 2, cy - s - 2, 4, 4, true);
      renderer.fillRect(cx + s / 3 - 2, cy - s - 2, 4, 4, true);
      for (int i = 0; i < 3; i++)
        renderer.fillRect(cx - s / 2 + i * s / 2, cy + s / 6, 3, 3, true);
      break;
    }
    case 23: { // Satellite — body + solar panels
      renderer.drawRect(cx - s / 4, cy - s / 4, s / 2, s / 2);
      renderer.fillRect(cx - s,       cy - s / 6, s * 2 / 3, s / 3, true);
      renderer.fillRect(cx + s / 4,   cy - s / 6, s * 2 / 3, s / 3, true);
      renderer.drawLine(cx, cy - s / 4, cx, cy - s);
      renderer.fillRect(cx - 2, cy - s - 2, 4, 4, true);
      break;
    }
    case 24: { // Robot Head — rect with eyes and antenna
      renderer.drawRect(cx - s, cy - s / 2, s * 2, s + s / 2);
      renderer.fillRect(cx - s / 2, cy - s / 4, s / 3, s / 3, true);
      renderer.fillRect(cx + s / 4,  cy - s / 4, s / 3, s / 3, true);
      renderer.drawLine(cx - s / 2, cy + s / 3, cx + s / 2, cy + s / 3);
      renderer.drawLine(cx, cy - s / 2, cx, cy - s);
      renderer.fillRect(cx - 2, cy - s - 3, 5, 3, true);
      break;
    }
    case 25: { // Game Boy — rect with screen and buttons
      renderer.drawRect(cx - s / 2, cy - s, s, s * 2);
      renderer.fillRect(cx - s / 3, cy - s + s / 4, s * 2 / 3, s / 2, false);
      renderer.drawRect(cx - s / 3, cy - s + s / 4, s * 2 / 3, s / 2);
      renderer.fillRect(cx + s / 6, cy + s / 3, 4, 4, true);
      renderer.fillRect(cx - s / 4, cy + s / 3, 4, 4, true);
      renderer.fillRect(cx - s / 3, cy + s / 2, s / 4, 2, true);
      renderer.fillRect(cx - s / 4, cy + s / 3, 2, s / 4, true);
      break;
    }
    case 26: { // Oscilloscope — screen with sine wave
      renderer.drawRect(cx - s, cy - s / 2, s * 2, s);
      for (int px = -s + 2; px < s - 2; px++) {
        float wave = sinf(px * 3.14159f / (float)(s / 2));
        int py = cy + (int)(s / 4 * wave);
        renderer.drawPixel(cx + px, py, true);
      }
      break;
    }
    case 27: { // Soldering Iron — diagonal with glowing tip
      renderer.drawLine(cx - s, cy + s, cx + s / 3, cy - s / 3);
      renderer.drawLine(cx - s + 1, cy + s, cx + s / 3 + 1, cy - s / 3);
      renderer.drawLine(cx + s / 3, cy - s / 3, cx + s, cy - s);
      renderer.fillRect(cx + s - 2, cy - s - 2, 4, 4, true);
      break;
    }
    case 28: { // Drone — X shape with circles at tips
      renderer.drawLine(cx - s, cy - s, cx + s, cy + s);
      renderer.drawLine(cx + s, cy - s, cx - s, cy + s);
      renderer.fillRect(cx - 2, cy - 2, 4, 4, true);
      for (int corner = 0; corner < 4; corner++) {
        int dx2 = (corner & 1) ? s : -s;
        int dy2 = (corner & 2) ? s : -s;
        renderer.drawRect(cx + dx2 - 3, cy + dy2 - 3, 6, 6);
      }
      break;
    }
    case 29: { // VR Headset — wide rect with lens slots and strap
      renderer.drawRect(cx - s, cy - s / 3, s * 2, s * 2 / 3);
      renderer.fillRect(cx - s / 2, cy - s / 6, s / 3, s / 3, true);
      renderer.fillRect(cx + s / 4,  cy - s / 6, s / 3, s / 3, true);
      renderer.drawLine(cx - s, cy, cx - s - s / 3, cy - s / 2);
      renderer.drawLine(cx + s, cy, cx + s + s / 3, cy - s / 2);
      break;
    }
    case 30: { // Server Rack — stacked rectangles with LEDs
      for (int i = 0; i < 3; i++) {
        int ry = cy - s + i * (s * 2 / 3);
        renderer.drawRect(cx - s / 2, ry, s, s * 2 / 3 - 2);
        renderer.fillRect(cx + s / 3, ry + 3, 3, 3, true);
      }
      break;
    }
    case 31: { // Ethernet Jack — trapezoid with pins
      renderer.drawRect(cx - s / 2, cy - s / 3, s, s * 2 / 3);
      renderer.drawRect(cx - s / 3, cy - s / 3 - s / 4, s * 2 / 3, s / 4);
      for (int i = 0; i < 4; i++)
        renderer.fillRect(cx - s / 4 + i * s / 6, cy - s / 3 - s / 6, 2, s / 6, true);
      break;
    }
    case 32: { // Raspberry Pi — small board with pins
      renderer.drawRect(cx - s, cy - s / 2, s * 2, s);
      renderer.fillRect(cx - s + 2, cy - s / 2 + 2, s / 3, s / 4, true);
      for (int i = 0; i < 5; i++)
        renderer.fillRect(cx + s / 2 + i * 3 - 7, cy - s / 2 - 3, 2, 3, true);
      renderer.fillRect(cx, cy - s / 4, s / 2, s / 3, false);
      renderer.drawRect(cx, cy - s / 4, s / 2, s / 3);
      break;
    }
    case 33: { // Arduino — board with header pins
      renderer.drawRect(cx - s, cy - s / 2, s * 2, s);
      renderer.fillRect(cx - s / 2, cy - s / 2 - 2, s, 2, true);
      renderer.fillRect(cx - s / 4, cy + s / 2, s / 2, 2, true);
      renderer.fillRect(cx + s / 3, cy - s / 4, s / 3, s / 4, true);
      break;
    }
    case 34: { // Logic Analyzer — box with probes
      renderer.drawRect(cx - s / 2, cy - s / 3, s, s * 2 / 3);
      for (int i = 0; i < 4; i++) {
        int px = cx - s / 3 + i * s / 4;
        renderer.drawLine(px, cy - s / 3, px, cy - s);
        renderer.fillRect(px - 1, cy - s - 2, 3, 3, true);
      }
      break;
    }
    case 35: { // Golden Chip — dithered chip with extra pins
      fillDithered25(renderer, cx - s, cy - s, s * 2, s * 2);
      renderer.drawRect(cx - s, cy - s, s * 2, s * 2);
      renderer.drawRect(cx - s + 2, cy - s + 2, s * 2 - 4, s * 2 - 4);
      for (int i = 0; i < 3; i++) {
        int py = cy - s + 4 + i * (s * 2 - 8) / 2;
        renderer.fillRect(cx - s - s / 3, py, s / 3, 3, true);
        renderer.fillRect(cx + s,          py, s / 3, 3, true);
      }
      break;
    }
    case 36: { // Cyber Eye — circle with filled iris and scan lines
      for (int a = 0; a < 360; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * cosf(rad)), cy + (int)(s * sinf(rad)), true);
      }
      for (int dy2 = -s / 3; dy2 <= s / 3; dy2++) {
        int dx2 = 0;
        while ((dx2 + 1) * (dx2 + 1) + dy2 * dy2 <= (s / 3) * (s / 3)) dx2++;
        if (dx2 > 0) renderer.fillRect(cx - dx2, cy + dy2, dx2 * 2 + 1, 1, true);
      }
      renderer.drawLine(cx - s - 3, cy, cx - s, cy);
      renderer.drawLine(cx + s, cy, cx + s + 3, cy);
      renderer.drawLine(cx, cy - s, cx, cy - s - 3);
      break;
    }
    case 37: { // Plasma Ball — circle with dithered interior + lightning
      fillDithered25(renderer, cx - s, cy - s, s * 2, s * 2);
      for (int a = 0; a < 360; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * cosf(rad)), cy + (int)(s * sinf(rad)), true);
      }
      renderer.drawLine(cx, cy, cx + s / 2, cy - s / 3);
      renderer.drawLine(cx + s / 2, cy - s / 3, cx + s / 4, cy - s / 6);
      renderer.drawLine(cx + s / 4, cy - s / 6, cx + s * 2 / 3, cy - s * 2 / 3);
      renderer.drawLine(cx, cy, cx - s / 3, cy + s / 2);
      renderer.drawLine(cx - s / 3, cy + s / 2, cx - s / 6, cy + s / 3);
      break;
    }
    case 38: { // Hologram — diamond with dithered fill
      renderer.drawLine(cx,      cy - s, cx + s,  cy);
      renderer.drawLine(cx + s,  cy,     cx,      cy + s);
      renderer.drawLine(cx,      cy + s, cx - s,  cy);
      renderer.drawLine(cx - s,  cy,     cx,      cy - s);
      fillDithered25(renderer, cx - s / 2, cy - s / 2, s, s);
      break;
    }
    case 39: { // Quantum Bit — two overlapping circles with dither
      for (int a = 0; a < 360; a += 8) {
        float rad = a * 3.14159f / 180.0f;
        int r2 = s * 2 / 3;
        renderer.drawPixel(cx - s / 4 + (int)(r2 * cosf(rad)), cy + (int)(r2 * sinf(rad)), true);
        renderer.drawPixel(cx + s / 4 + (int)(r2 * cosf(rad)), cy + (int)(r2 * sinf(rad)), true);
      }
      fillDithered50(renderer, cx - s / 6, cy - s / 3, s / 3, s * 2 / 3);
      break;
    }
    case 40: { // Neural Net — nodes connected by lines
      int nodes[5][2] = {{cx, cy - s}, {cx - s, cy}, {cx + s, cy}, {cx - s / 2, cy + s}, {cx + s / 2, cy + s}};
      for (int i = 0; i < 5; i++) {
        renderer.fillRect(nodes[i][0] - 2, nodes[i][1] - 2, 5, 5, true);
        for (int j = i + 1; j < 5; j++)
          renderer.drawLine(nodes[i][0], nodes[i][1], nodes[j][0], nodes[j][1]);
      }
      break;
    }
    case 41: { // Infinity Loop — figure-8 Lissajous
      for (int a = 0; a < 360; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        float ix = s * 2 / 3 * cosf(rad);
        float iy = s / 2 * sinf(rad) * cosf(rad);
        renderer.drawPixel(cx + (int)ix, cy + (int)iy, true);
      }
      break;
    }
    case 42: { // Crypto Key — key shape
      renderer.drawRect(cx - s, cy - s / 4, s, s / 2);
      renderer.drawLine(cx, cy, cx + s, cy);
      renderer.drawLine(cx + s / 2,     cy, cx + s / 2,     cy + s / 3);
      renderer.drawLine(cx + s * 3 / 4, cy, cx + s * 3 / 4, cy + s / 4);
      break;
    }
    case 43: { // Zero Day — ominous dithered square with eye-like cutouts
      fillDithered50(renderer, cx - s / 2, cy - s / 2, s, s);
      renderer.drawRect(cx - s / 2, cy - s / 2, s, s);
      renderer.fillRect(cx - s / 3, cy - s / 4, s / 4, s / 4, false);
      renderer.fillRect(cx + s / 8, cy - s / 4, s / 4, s / 4, false);
      renderer.fillRect(cx - s / 6, cy + s / 6, s / 3, 2, false);
      break;
    }
    case 44: { // Black Box — filled with dithered interior and border
      renderer.fillRect(cx - s, cy - s, s * 2, s * 2, true);
      fillDithered50(renderer, cx - s + 3, cy - s + 3, s * 2 - 6, s * 2 - 6);
      renderer.drawRect(cx - s / 3, cy - s / 3, s * 2 / 3, s * 2 / 3);
      break;
    }
    case 45: { // The Kernel — terminal window with title bar
      renderer.fillRect(cx - s - 2, cy - s - 2, s * 2 + 4, s * 2 + 4, true);
      renderer.fillRect(cx - s, cy - s + s / 3, s * 2, s * 2 - s / 3, false);
      renderer.fillRect(cx - s, cy - s, s * 2, s / 3, true);
      renderer.fillRect(cx + s - s / 4, cy - s + 2, s / 5, s / 5, false);
      for (int i = 0; i < 5; i++)
        renderer.fillRect(cx - s + 4 + i * 4, cy - s / 4, 2, 3, true);
      renderer.fillRect(cx - s + 24, cy - s / 4, 3, 5, true);
      break;
    }
    case 46: { // Root Shell — terminal with # prompt
      renderer.drawRect(cx - s, cy - s, s * 2, s * 2);
      renderer.fillRect(cx - s, cy - s, s * 2, s / 3, true);
      renderer.fillRect(cx + s - s / 4, cy - s + 2, s / 5, s / 5, false);
      renderer.drawLine(cx - s / 2,       cy - s / 6, cx - s / 2 + s / 3, cy - s / 6);
      renderer.drawLine(cx - s / 2,       cy + s / 6, cx - s / 2 + s / 3, cy + s / 6);
      renderer.drawLine(cx - s / 3, cy - s / 3, cx - s / 3, cy + s / 3);
      renderer.drawLine(cx - s / 6, cy - s / 3, cx - s / 6, cy + s / 3);
      break;
    }
    case 47: { // Packet Ghost — ghost silhouette
      for (int a = 0; a <= 180; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * 2 / 3 * cosf(rad)),
                           cy - s / 4 + (int)(s * 2 / 3 * -sinf(rad)), true);
      }
      renderer.drawLine(cx - s * 2 / 3, cy - s / 4, cx - s * 2 / 3, cy + s);
      renderer.drawLine(cx + s * 2 / 3, cy - s / 4, cx + s * 2 / 3, cy + s);
      renderer.drawLine(cx - s * 2 / 3, cy + s, cx - s / 3, cy + s / 2);
      renderer.drawLine(cx - s / 3,     cy + s / 2, cx,          cy + s);
      renderer.drawLine(cx,             cy + s,     cx + s / 3,  cy + s / 2);
      renderer.drawLine(cx + s / 3,     cy + s / 2, cx + s * 2 / 3, cy + s);
      renderer.fillRect(cx - s / 3, cy - s / 3, 3, 3, true);
      renderer.fillRect(cx + s / 4, cy - s / 3, 3, 3, true);
      break;
    }
    case 48: { // E-Ink Dragon — body ellipse with wings
      for (int dy2 = -s / 2; dy2 <= s / 2; dy2++) {
        float ratio = 1.0f - (float)(dy2 * dy2) / ((float)(s / 2) * (s / 2));
        if (ratio < 0.0f) ratio = 0.0f;
        int hw = (int)(s * sqrtf(ratio));
        if (hw > 0) renderer.fillRect(cx - hw / 2, cy + dy2, hw, 1, true);
      }
      renderer.drawLine(cx - s / 2, cy - s / 4, cx - s, cy - s);
      renderer.drawLine(cx - s,     cy - s,      cx - s / 3, cy);
      renderer.drawLine(cx + s / 2, cy - s / 4, cx + s, cy - s);
      renderer.drawLine(cx + s,     cy - s,      cx + s / 3, cy);
      renderer.drawLine(cx + s / 2, cy - s / 4, cx + s, cy);
      renderer.drawLine(cx + s / 2, cy + s / 4, cx + s, cy);
      renderer.drawLine(cx - s / 2, cy,           cx - s, cy + s / 2);
      renderer.drawLine(cx - s,     cy + s / 2,   cx - s + s / 3, cy + s);
      break;
    }
    case 49: { // biscuit. Logo — double circle with vertical line
      for (int a = 0; a < 360; a += 5) {
        float rad = a * 3.14159f / 180.0f;
        renderer.drawPixel(cx + (int)(s * cosf(rad)),       cy + (int)(s * sinf(rad)), true);
        renderer.drawPixel(cx + (int)((s - 2) * cosf(rad)), cy + (int)((s - 2) * sinf(rad)), true);
      }
      renderer.fillRect(cx - s / 3, cy - s / 2, 2, s, true);
      renderer.drawRect(cx - s / 3, cy, s / 2, s / 3);
      renderer.fillRect(cx + s / 4, cy + s / 4, 3, 3, true);
      break;
    }
    default: {
      // Fallback: bordered square with icon number
      renderer.drawRect(x + 2, y + 2, size - 4, size - 4);
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", iconId % 100);
      int tw = renderer.getTextWidth(SMALL_FONT_ID, buf);
      renderer.drawText(SMALL_FONT_ID, cx - tw / 2, cy - 5, buf);
      break;
    }
  }
}
