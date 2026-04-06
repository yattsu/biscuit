#include "KeyCopierActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <cmath>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

// Static constexpr definitions (C++14 ODR requirement)
constexpr KeyCopierActivity::KeyType KeyCopierActivity::KEY_TYPES[];

// ---- helpers ----------------------------------------------------------------

static void buildFilePath(char* buf, int bufLen, const char* typeName) {
  // Replace spaces with underscores for the filename
  snprintf(buf, bufLen, "/biscuit/key_%s.txt", typeName);
  for (int i = 0; buf[i]; i++) {
    if (buf[i] == ' ') buf[i] = '_';
  }
}

// ---- lifecycle --------------------------------------------------------------

void KeyCopierActivity::onEnter() {
  Activity::onEnter();
  state = EDIT;
  typeIndex = 0;
  typeSelectIndex = 0;
  cutIndex = 0;
  // Default all cuts to mid-range
  for (int i = 0; i < 8; i++) cuts[i] = 0;
  loadKey();
  requestUpdate();
}

void KeyCopierActivity::onExit() { Activity::onExit(); }

// ---- loop -------------------------------------------------------------------

void KeyCopierActivity::loop() {
  if (state == EDIT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (cutIndex > 0) { cutIndex--; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (cutIndex < kt().numCuts - 1) { cutIndex++; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      int& c = cuts[cutIndex];
      if (c < kt().maxDepth) { c++; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      int& c = cuts[cutIndex];
      if (c > kt().minDepth) { c--; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      saveKey();
      state = SAVED;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      typeSelectIndex = typeIndex;
      state = TYPE_SELECT;
      requestUpdate();
    }
    return;
  }

  if (state == TYPE_SELECT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = EDIT;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      typeSelectIndex = ButtonNavigator::previousIndex(typeSelectIndex, KEY_TYPE_COUNT);
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      typeSelectIndex = ButtonNavigator::nextIndex(typeSelectIndex, KEY_TYPE_COUNT);
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Apply new type — reset cuts to valid defaults for the new type
      typeIndex = typeSelectIndex;
      cutIndex = 0;
      for (int i = 0; i < 8; i++) {
        cuts[i] = KEY_TYPES[typeIndex].minDepth;
      }
      loadKey();  // Try to load saved bitting for this type
      state = EDIT;
      requestUpdate();
    }
    return;
  }

  if (state == SAVED) {
    // Any button press returns to EDIT
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      state = EDIT;
      requestUpdate();
    }
  }
}

// ---- drawing helpers --------------------------------------------------------

void KeyCopierActivity::drawKey() const {
  const auto pageWidth = renderer.getScreenWidth();

  // Key layout constants
  constexpr int bowCX = 52;
  constexpr int bladeTop = 210;
  constexpr int bladeH = 60;
  constexpr int bladeLeft = bowCX + 38;
  constexpr int tipWidth = 18;
  // blade ends before a narrow tip
  const int bladeRight = pageWidth - 20 - tipWidth;
  const int bowCY = bladeTop + bladeH / 2;

  // How much pixel depth per unit of cut depth
  // maxCutPx is about 2/3 of blade height
  const int maxCutPx = (bladeH * 2) / 3;
  const int numCuts = kt().numCuts;
  const int maxDepth = kt().maxDepth;
  const int minDepth = kt().minDepth;
  const int depthRange = (maxDepth > minDepth) ? (maxDepth - minDepth) : 1;

  // Width available for the cut section of the blade
  const int cutAreaLeft = bladeLeft + 6;
  const int cutAreaRight = bladeRight - 6;
  const int cutAreaW = cutAreaRight - cutAreaLeft;

  // Each cut occupies equal width; shoulder = 1/3, valley = 1/3, shoulder = 1/3
  const int cutW = (numCuts > 0) ? (cutAreaW / numCuts) : cutAreaW;

  // --- Draw bow (circle) using drawPixel with cosf/sinf ---
  constexpr int bowR = 35;
  constexpr int holeR = 6;
  // Outer ring
  for (int deg = 0; deg < 360; deg++) {
    float rad = deg * 3.14159f / 180.0f;
    int px = bowCX + (int)(cosf(rad) * bowR);
    int py = bowCY + (int)(sinf(rad) * bowR);
    renderer.drawPixel(px, py, true);
  }
  // Inner hole (keyring hole)
  for (int deg = 0; deg < 360; deg++) {
    float rad = deg * 3.14159f / 180.0f;
    int px = bowCX + (int)(cosf(rad) * holeR);
    int py = bowCY + (int)(sinf(rad) * holeR);
    renderer.drawPixel(px, py, true);
  }

  // --- Draw blade flat bottom edge ---
  renderer.drawLine(bladeLeft, bladeTop + bladeH, bladeRight, bladeTop + bladeH, true);

  // --- Draw blade left side (shoulder from bow to blade) ---
  renderer.drawLine(bladeLeft, bladeTop, bladeLeft, bladeTop + bladeH, true);

  // --- Draw key tip (angled) ---
  // Top of tip: from bladeRight at bladeTop to bladeRight+tipWidth at bladeTop+bladeH/2
  renderer.drawLine(bladeRight, bladeTop, bladeRight + tipWidth, bladeTop + bladeH / 2, true);
  // Bottom of tip: from bladeRight at bladeTop+bladeH to same point
  renderer.drawLine(bladeRight, bladeTop + bladeH, bladeRight + tipWidth, bladeTop + bladeH / 2, true);

  // --- Draw warding groove (horizontal dashed line ~3/4 down the blade) ---
  const int wardY = bladeTop + (bladeH * 3) / 4;
  for (int x = bladeLeft + 4; x < bladeRight; x += 6) {
    renderer.drawPixel(x, wardY, true);
    renderer.drawPixel(x + 1, wardY, true);
    renderer.drawPixel(x + 2, wardY, true);
  }

  // --- Draw cut profile on top edge ---
  // Start at bladeTop (flat region before first cut)
  // For each cut: flat shoulder → slope down → valley → slope up → flat shoulder
  // We draw by building the top profile and connecting points

  // First: draw the flat region from bladeLeft to start of cut area (top)
  renderer.drawLine(bladeLeft, bladeTop, cutAreaLeft, bladeTop, true);
  // Last: draw flat region from end of cut area to bladeRight (top)
  renderer.drawLine(cutAreaRight, bladeTop, bladeRight, bladeTop, true);

  for (int i = 0; i < numCuts; i++) {
    int depth = cuts[i] - minDepth;  // normalize to 0-based
    int depthPx = (depth * maxCutPx) / depthRange;

    int cx0 = cutAreaLeft + i * cutW;
    int cx1 = cx0 + cutW;

    // Shoulder width = slope width = valley floor width = cutW/3
    int sw = cutW / 3;
    int slopeW = sw;
    int valleyW = cutW - 2 * sw;  // remaining

    // Key points on top edge for this cut:
    // (cx0, bladeTop) -- flat shoulder left
    // (cx0+sw, bladeTop) -- top of left slope
    // (cx0+sw+slopeW, bladeTop+depthPx) -- bottom of left slope / valley start
    // (cx0+sw+slopeW+valleyW, bladeTop+depthPx) -- valley end
    // (cx0+sw+slopeW+valleyW+sw, bladeTop) -- top of right slope
    // (cx1, bladeTop) -- flat shoulder right

    int slopeStart = cx0 + sw;
    int valleyLeft  = slopeStart + slopeW;
    int valleyRight = valleyLeft + valleyW;
    int slopeEnd    = valleyRight + sw;

    // Clamp slopeEnd to cx1 to avoid overdraw
    if (slopeEnd > cx1) slopeEnd = cx1;

    renderer.drawLine(cx0, bladeTop, slopeStart, bladeTop, true);
    renderer.drawLine(slopeStart, bladeTop, valleyLeft, bladeTop + depthPx, true);
    renderer.drawLine(valleyLeft, bladeTop + depthPx, valleyRight, bladeTop + depthPx, true);
    renderer.drawLine(valleyRight, bladeTop + depthPx, slopeEnd, bladeTop, true);
    renderer.drawLine(slopeEnd, bladeTop, cx1, bladeTop, true);

    // --- Depth label above each cut ---
    char label[4];
    snprintf(label, sizeof(label), "%d", cuts[i]);
    int labelX = cx0 + (cutW - renderer.getTextWidth(SMALL_FONT_ID, label)) / 2;
    int labelY = bladeTop - renderer.getLineHeight(SMALL_FONT_ID) - 4;

    if (i == cutIndex) {
      // Inverted box for selected cut
      int boxW = cutW - 2;
      int boxH = renderer.getLineHeight(SMALL_FONT_ID) + 2;
      renderer.fillRect(cx0 + 1, labelY - 1, boxW, boxH, true);
      renderer.drawText(SMALL_FONT_ID, labelX, labelY, label, false);
    } else {
      renderer.drawText(SMALL_FONT_ID, labelX, labelY, label, true);
    }
  }

  // --- Guide text below blade ---
  const int guideY = bladeTop + bladeH + 12;
  renderer.drawText(SMALL_FONT_ID, bladeLeft, guideY, "Place key on screen to compare", true);
}

void KeyCopierActivity::drawCutSelector() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();

  const int numCuts = kt().numCuts;
  constexpr int boxH = 32;
  constexpr int sidePad = 14;
  const int totalW = pageWidth - sidePad * 2;
  const int boxW = totalW / numCuts;
  const int rowY = pageHeight - metrics.buttonHintsHeight - boxH - 10;

  for (int i = 0; i < numCuts; i++) {
    int bx = sidePad + i * boxW;
    bool sel = (i == cutIndex);

    if (sel) {
      renderer.fillRect(bx, rowY, boxW - 2, boxH, true);
    } else {
      renderer.drawRect(bx, rowY, boxW - 2, boxH, true);
    }

    // Label: cut number above, depth below
    char numBuf[4];
    snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
    int nx = bx + (boxW - 2 - renderer.getTextWidth(SMALL_FONT_ID, numBuf)) / 2;
    renderer.drawText(SMALL_FONT_ID, nx, rowY + 2, numBuf, !sel);

    char depBuf[4];
    snprintf(depBuf, sizeof(depBuf), "%d", cuts[i]);
    int dx = bx + (boxW - 2 - renderer.getTextWidth(SMALL_FONT_ID, depBuf)) / 2;
    renderer.drawText(SMALL_FONT_ID, dx, rowY + 14, depBuf, !sel);
  }
}

void KeyCopierActivity::drawTypeMenu() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Key Type");

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - 4;

  GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH},
               KEY_TYPE_COUNT, typeSelectIndex,
               [](int i) -> std::string { return KeyCopierActivity::KEY_TYPES[i].name; },
               [](int i) -> std::string {
                 char buf[32];
                 snprintf(buf, sizeof(buf), "%d cuts, depth %d-%d",
                          KeyCopierActivity::KEY_TYPES[i].numCuts,
                          KeyCopierActivity::KEY_TYPES[i].minDepth,
                          KeyCopierActivity::KEY_TYPES[i].maxDepth);
                 return buf;
               });

  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- save / load ------------------------------------------------------------

void KeyCopierActivity::saveKey() const {
  Storage.mkdir("/biscuit");

  char path[64];
  buildFilePath(path, sizeof(path), kt().name);

  // Build content
  char content[128];
  int pos = 0;
  pos += snprintf(content + pos, sizeof(content) - pos, "Type: %s\n", kt().name);
  pos += snprintf(content + pos, sizeof(content) - pos, "Cuts: %d\n", kt().numCuts);
  pos += snprintf(content + pos, sizeof(content) - pos, "Bitting: ");
  for (int i = 0; i < kt().numCuts; i++) {
    if (i > 0 && pos < (int)sizeof(content) - 2) content[pos++] = '-';
    pos += snprintf(content + pos, sizeof(content) - pos, "%d", cuts[i]);
  }
  if (pos < (int)sizeof(content) - 1) content[pos++] = '\n';
  content[pos] = '\0';

  Storage.writeFile(path, String(content));
}

void KeyCopierActivity::loadKey() {
  char path[64];
  buildFilePath(path, sizeof(path), kt().name);

  if (!Storage.exists(path)) return;

  String data = Storage.readFile(path);
  if (data.isEmpty()) return;

  // Find "Bitting:" line
  const char* bittingLabel = "Bitting: ";
  int idx = data.indexOf(bittingLabel);
  if (idx < 0) return;

  const char* p = data.c_str() + idx + strlen(bittingLabel);
  int ci = 0;
  const int numCuts = kt().numCuts;
  while (*p && *p != '\n' && ci < numCuts) {
    int val = 0;
    bool gotDigit = false;
    while (*p >= '0' && *p <= '9') {
      val = val * 10 + (*p - '0');
      p++;
      gotDigit = true;
    }
    if (gotDigit) {
      // Clamp to valid range
      if (val < kt().minDepth) val = kt().minDepth;
      if (val > kt().maxDepth) val = kt().maxDepth;
      cuts[ci++] = val;
    }
    if (*p == '-') p++;
  }
}

// ---- render -----------------------------------------------------------------

void KeyCopierActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (state == TYPE_SELECT) {
    drawTypeMenu();
    renderer.displayBuffer();
    return;
  }

  // EDIT or SAVED state
  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Key Copier", kt().name);

  drawKey();
  drawCutSelector();

  if (state == SAVED) {
    GUI.drawPopup(renderer, "Bitting saved to SD!");
  } else {
    const auto labels = mappedInput.mapLabels("Back", "Save", "L/R Cut", "U/D Depth");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
