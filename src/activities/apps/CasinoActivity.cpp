#include "CasinoActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Static constexpr initializers
constexpr int CasinoActivity::BET_OPTIONS[];
constexpr const char* CasinoActivity::SUIT_CHARS[];
constexpr const char* CasinoActivity::RANK_CHARS[];

// Symbol IDs:
//  0=Seven, 1=BAR, 2=Cherry, 3=Bell, 4=Star, 5=Diamond,
//  6=Lemon, 7=Orange, 8=Grape, 9=Melon, 10=Plum, 11=SevenOutline(wild)
const CasinoActivity::SlotMachineType CasinoActivity::MACHINES[NUM_MACHINES] = {
    {"Classic",        "Traditional 3-reel slots",    6, {{0,1,2,3,4,5,0,0},   {50,20,10,8,5,3,0,0}},    2, 0xFF, false, false, 10},
    {"Fruit Frenzy",   "Fruity fun, free spins!",     6, {{2,6,7,8,9,10,0,0},  {15,12,10,8,6,4,0,0}},    2, 0xFF, true,  false, 10},
    {"Lucky 7s",       "Wild sevens match anything",  5, {{0,11,1,3,4,0,0,0},  {50,25,20,8,5,0,0,0}},    2, 1,    false, false, 25},
    {"Diamond Deluxe", "Hold reels, big diamonds",    6, {{5,4,3,1,2,0,0,0},   {40,15,10,8,5,3,0,0}},    3, 0xFF, false, true,  50},
    {"High Roller",    "Big bets, bigger payouts",    4, {{0,1,5,4,0,0,0,0},   {80,30,20,10,0,0,0,0}},   3, 0xFF, false, false, 100},
};

bool CasinoActivity::isRed(int n) {
  static constexpr int reds[] = {1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
  for (int r : reds) { if (r == n) return true; }
  return false;
}

// ================================================================
// LIFECYCLE
// ================================================================

void CasinoActivity::onEnter() {
  Activity::onEnter();
  screen = LOBBY;
  lobbyIndex = 0;
  betIndex = 1;
  showingResult = false;
  loadCredits();
  bjShuffle();
  requestUpdate();
}

void CasinoActivity::onExit() {
  Activity::onExit();
  saveCredits();
}

// ================================================================
// CREDITS — persistent on SD
// ================================================================

void CasinoActivity::loadCredits() {
  Storage.mkdir("/biscuit");
  auto file = Storage.open(SAVE_PATH);
  if (file && !file.isDirectory()) {
    uint8_t buf[4];
    if (file.read(buf, 4) == 4) {
      credits = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
      LOG_DBG("Casino", "Loaded credits: %d", credits);
    }
    file.close();
  }
  if (credits <= 0) credits = 1000;  // free refill when broke
}

void CasinoActivity::saveCredits() {
  auto file = Storage.open(SAVE_PATH, O_WRITE | O_CREAT | O_TRUNC);
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
// MAIN LOOP — dispatches to active game
// ================================================================

void CasinoActivity::loop() {
  // Dismiss result overlay
  if (showingResult) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingResult = false;
      // Refill if broke
      if (credits <= 0) { credits = 1000; saveCredits(); }
      requestUpdate();
      return;
    }
    return;
  }

  switch (screen) {
    case LOBBY: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { finish(); return; }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        switch (lobbyIndex) {
          case 0:
            screen = SLOTS;
            slotsScreen = SLOTS_MENU;
            slotsMachineMenuIndex = 0;
            break;
          case 1: screen = BLACKJACK; bjState = BJ_BET; break;
          case 2: screen = COINFLIP; coinState = COIN_BET; break;
          case 3: screen = HIGHLOW; hlState = HL_BET; hlStreak = 0; break;
          case 4: screen = ROULETTE; rlState = RL_BET; break;
          case 5:
            resetConfirmCount++;
            if (resetConfirmCount >= 3) {
              resetCredits();
              resetConfirmCount = 0;
            }
            requestUpdate();
            return;
        }
        requestUpdate(); return;
      }
      buttonNavigator.onNext([this] { lobbyIndex = ButtonNavigator::nextIndex(lobbyIndex, LOBBY_COUNT); resetConfirmCount = 0; requestUpdate(); });
      buttonNavigator.onPrevious([this] { lobbyIndex = ButtonNavigator::previousIndex(lobbyIndex, LOBBY_COUNT); resetConfirmCount = 0; requestUpdate(); });
      break;
    }
    case SLOTS: slotsLoop(); break;
    case BLACKJACK: bjLoop(); break;
    case COINFLIP: coinLoop(); break;
    case HIGHLOW: hlLoop(); break;
    case ROULETTE: rlLoop(); break;
  }
}

// ================================================================
// RENDER — dispatches to active game
// ================================================================

void CasinoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (screen) {
    case LOBBY: renderLobby(); break;
    case SLOTS: slotsRender(); break;
    case BLACKJACK: bjRender(); break;
    case COINFLIP: coinRender(); break;
    case HIGHLOW: hlRender(); break;
    case ROULETTE: rlRender(); break;
  }

  // Result overlay
  if (showingResult) {
    // Dim background with border box
    int boxY = 300, boxH = 180;
    renderer.fillRect(20, boxY, 440, boxH, false);
    renderer.drawRect(20, boxY, 440, boxH);
    renderer.drawRect(22, boxY + 2, 436, boxH - 4);

    renderer.drawCenteredText(UI_12_FONT_ID, boxY + 25, resultMessage.c_str(), true, EpdFontFamily::BOLD);

    char amtBuf[32];
    if (resultAmount > 0) {
      snprintf(amtBuf, sizeof(amtBuf), "+%d credits", resultAmount);
    } else if (resultAmount < 0) {
      snprintf(amtBuf, sizeof(amtBuf), "%d credits", resultAmount);
    } else {
      snprintf(amtBuf, sizeof(amtBuf), "Push");
    }
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 70, amtBuf);

    char balBuf[32];
    snprintf(balBuf, sizeof(balBuf), "Balance: %d", credits);
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 110, balBuf);

    renderer.drawCenteredText(SMALL_FONT_ID, boxY + 145, "[OK] Continue");
  }

  renderer.displayBuffer();
}

// ================================================================
// RENDERING HELPERS
// ================================================================

void CasinoActivity::renderCreditsBar() {
  const auto pageWidth = renderer.getScreenWidth();
  char buf[32];
  snprintf(buf, sizeof(buf), "Credits: %d", credits);
  renderer.drawText(UI_10_FONT_ID, 15, 50, buf, true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 72, pageWidth, 72);
}

void CasinoActivity::renderBetSelector(int y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "< Bet: %d >", currentBet());
  renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
}

void CasinoActivity::renderLobby() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "biscuit. Casino", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);

  renderCreditsBar();

  int contentTop = 90;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  static const char* games[] = {"Slot Machine", "Blackjack", "Coin Flip", "Higher / Lower", "Roulette", "Reset Credits"};
  static const char* descs[] = {"5 machines with powerups", "Beat the dealer to 21", "Pick heads or tails, 2x payout", "Guess the next card, streak bonus", "European roulette, 0-36", "Start fresh with 1000 credits"};

  static const char* resetDescs[] = {"Start fresh with 1000 credits", "Are you sure? Press again.", "Really sure? Press once more."};
  const char* resetDesc = resetDescs[resetConfirmCount < 3 ? resetConfirmCount : 2];

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, LOBBY_COUNT, lobbyIndex,
      [](int i) { return std::string(games[i]); },
      [resetDesc](int i) { return std::string(i == 5 ? resetDesc : descs[i]); });

  const auto labels = mappedInput.mapLabels("Exit", "Play", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// Draw a card suit shape using only primitives
// suit: 0=Spades, 1=Hearts, 2=Diamonds, 3=Clubs
static void drawSuitShape(GfxRenderer& r, int cx, int cy, int sz, int suit) {
  switch (suit) {
    case 0: {  // Spades: upward triangle + two bumps + stem
      // Upward pointing body
      for (int dy = -sz; dy <= sz / 2; dy++) {
        int halfW = (dy + sz) * sz / (sz + sz / 2);
        if (halfW < 0) halfW = 0;
        if (halfW > 0) r.fillRect(cx - halfW, cy + dy, halfW * 2 + 1, 1, true);
      }
      // Two small bumps at bottom sides
      int bumpR = sz / 3;
      if (bumpR < 1) bumpR = 1;
      for (int dy = -bumpR; dy <= bumpR; dy++) {
        int dx = bumpR - (dy < 0 ? -dy : dy);
        if (dx > 0) {
          r.fillRect(cx - sz / 2 - dx / 2, cy + sz / 2 + dy, dx, 1, true);
          r.fillRect(cx + sz / 2 - dx / 2, cy + sz / 2 + dy, dx, 1, true);
        }
      }
      // Stem
      r.fillRect(cx - 1, cy + sz / 2, 3, sz, true);
      break;
    }
    case 1: {  // Hearts: two bumps on top + downward point
      // Two top bumps
      int bumpR = sz * 2 / 3;
      if (bumpR < 2) bumpR = 2;
      for (int dy = -bumpR; dy <= 0; dy++) {
        int dx = bumpR * bumpR - dy * dy;
        // approximate sqrt with iterative narrowing
        int w = 0;
        while ((w + 1) * (w + 1) <= dx) w++;
        if (w > 0) {
          r.fillRect(cx - sz / 2 - w, cy - sz / 3 + dy, w * 2 + 1, 1, true);
          r.fillRect(cx + sz / 2 - w, cy - sz / 3 + dy, w * 2 + 1, 1, true);
        }
      }
      // Downward triangle body
      for (int dy = 0; dy <= sz; dy++) {
        int halfW = sz * (sz - dy) / sz;
        if (halfW > 0) r.fillRect(cx - halfW, cy - sz / 3 + dy, halfW * 2 + 1, 1, true);
      }
      break;
    }
    case 2: {  // Diamond: expanding then contracting scanlines
      for (int dy = -sz; dy <= sz; dy++) {
        int absY = dy < 0 ? -dy : dy;
        int halfW = sz - absY;
        if (halfW > 0) r.fillRect(cx - halfW, cy + dy, halfW * 2 + 1, 1, true);
      }
      break;
    }
    case 3: {  // Clubs: three circles in clover + stem
      int cr = sz * 2 / 5;
      if (cr < 2) cr = 2;
      // Top circle
      for (int dy = -cr; dy <= cr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
        if (dx > 0) r.fillRect(cx - dx, cy - sz / 2 - cr / 3 + dy, dx * 2 + 1, 1, true);
        else r.drawPixel(cx, cy - sz / 2 - cr / 3 + dy, true);
      }
      // Bottom-left circle
      for (int dy = -cr; dy <= cr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
        if (dx > 0) r.fillRect(cx - sz / 2 - dx, cy + cr / 3 + dy, dx * 2 + 1, 1, true);
        else r.drawPixel(cx - sz / 2, cy + cr / 3 + dy, true);
      }
      // Bottom-right circle
      for (int dy = -cr; dy <= cr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
        if (dx > 0) r.fillRect(cx + sz / 2 - dx, cy + cr / 3 + dy, dx * 2 + 1, 1, true);
        else r.drawPixel(cx + sz / 2, cy + cr / 3 + dy, true);
      }
      // Stem
      r.fillRect(cx - 1, cy + cr / 2, 3, sz, true);
      break;
    }
  }
}

void CasinoActivity::drawCard(int x, int y, const Card& c, bool faceDown) {
  int cw = 70, ch = 100;
  renderer.fillRect(x + 2, y + 2, cw, ch, true);   // shadow
  renderer.fillRect(x, y, cw, ch, false);            // white face
  renderer.drawRect(x, y, cw, ch);
  renderer.drawRect(x + 1, y + 1, cw - 2, ch - 2);

  if (faceDown) {
    // Crosshatch pattern
    for (int i = 6; i < cw - 6; i += 4) {
      renderer.drawLine(x + i, y + 6, x + i, y + ch - 6);
    }
    for (int j = 6; j < ch - 6; j += 4) {
      renderer.drawLine(x + 6, y + j, x + cw - 6, y + j);
    }
    return;
  }

  // Rank top-left
  renderer.drawText(UI_10_FONT_ID, x + 4, y + 3, RANK_CHARS[c.rank]);
  // Small suit shape under rank text
  drawSuitShape(renderer, x + 12, y + 28, 5, c.suit);
  // Large suit shape centered in card
  drawSuitShape(renderer, x + cw / 2, y + ch / 2 + 4, 13, c.suit);
  // Rank bottom-right
  int rw = renderer.getTextWidth(UI_10_FONT_ID, RANK_CHARS[c.rank]);
  renderer.drawText(UI_10_FONT_ID, x + cw - rw - 4, y + ch - 22, RANK_CHARS[c.rank]);
}

// ================================================================
// SLOT MACHINE ICON DRAWING
// ================================================================

static void drawIcon7(GfxRenderer& r, int cx, int cy) {
  // Thick "7" shape
  r.fillRect(cx - 18, cy - 25, 36, 7, true);
  r.fillRect(cx - 18, cy - 18, 7, 5, true);
  for (int i = 0; i < 40; i++) {
    int px = cx + 14 - i * 28 / 40;
    int py = cy - 18 + i;
    r.fillRect(px - 3, py, 7, 1, true);
  }
}

static void drawIconBar(GfxRenderer& r, int cx, int cy) {
  r.fillRect(cx - 30, cy - 14, 60, 28, true);
  r.fillRect(cx - 27, cy - 11, 54, 22, false);
  r.drawRect(cx - 27, cy - 11, 54, 22);
  int tw = r.getTextWidth(UI_10_FONT_ID, "BAR", EpdFontFamily::BOLD);
  r.drawText(UI_10_FONT_ID, cx - tw / 2, cy - 8, "BAR", true, EpdFontFamily::BOLD);
}

static void drawIconCherry(GfxRenderer& r, int cx, int cy) {
  int cr = 10;
  int offX = 10;
  int offY = 6;
  // Left cherry
  for (int dy = -cr; dy <= cr; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
    r.fillRect(cx - offX - dx, cy + offY + dy, dx * 2 + 1, 1, true);
  }
  // Right cherry
  for (int dy = -cr; dy <= cr; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
    r.fillRect(cx + offX - dx, cy + offY + dy, dx * 2 + 1, 1, true);
  }
  // Stems
  for (int i = 0; i < 20; i++) {
    int sx = cx - offX + i * offX / 20;
    int sy = cy + offY - cr - i;
    r.fillRect(sx, sy, 3, 1, true);
  }
  for (int i = 0; i < 20; i++) {
    int sx = cx + offX - i * offX / 20;
    int sy = cy + offY - cr - i;
    r.fillRect(sx, sy, 3, 1, true);
  }
  // Small leaf at top
  for (int dy = -3; dy <= 3; dy++) {
    int w = 3 - (dy < 0 ? -dy : dy);
    if (w > 0) r.fillRect(cx + 2, cy - 16 + dy, w * 2, 1, true);
  }
}

static void drawIconBell(GfxRenderer& r, int cx, int cy) {
  r.fillRect(cx - 2, cy - 26, 5, 5, true);
  for (int dy = -20; dy <= 8; dy++) {
    int halfW = 6 + (dy + 20) * 16 / 28;
    r.fillRect(cx - halfW, cy + dy, halfW * 2 + 1, 1, true);
  }
  r.fillRect(cx - 25, cy + 9, 51, 5, true);
  r.fillRect(cx - 3, cy + 14, 7, 7, true);
}

static void drawIconStar(GfxRenderer& r, int cx, int cy) {
  static constexpr int N = 10;
  static constexpr int vx[N] = {0, 5, 21, 8, 14, 0, -14, -8, -21, -5};
  static constexpr int vy[N] = {-22, -7, -7, 3, 18, 9, 18, 3, -7, -7};

  int minY = -24, maxY = 20;
  for (int sy = minY; sy <= maxY; sy++) {
    int xmin = 999, xmax = -999;
    for (int i = 0; i < N; i++) {
      int j = (i + 1) % N;
      int y0 = vy[i], y1 = vy[j];
      int x0 = vx[i], x1 = vx[j];
      if ((y0 <= sy && y1 > sy) || (y1 <= sy && y0 > sy)) {
        int ix = x0 + (sy - y0) * (x1 - x0) / (y1 - y0);
        if (ix < xmin) xmin = ix;
        if (ix > xmax) xmax = ix;
      }
    }
    if (xmin <= xmax) {
      r.fillRect(cx + xmin, cy + sy, xmax - xmin + 1, 1, true);
    }
  }
}

static void drawIconDiamond(GfxRenderer& r, int cx, int cy) {
  int crownTop = cy - 22;
  int girdle = cy - 4;
  int pavilionBottom = cy + 22;
  int topHalfW = 12;
  int girdleHalfW = 28;

  for (int y = crownTop; y <= girdle; y++) {
    int t = y - crownTop;
    int h = girdle - crownTop;
    int halfW = topHalfW + (girdleHalfW - topHalfW) * t / h;
    r.fillRect(cx - halfW, y, halfW * 2 + 1, 1, true);
  }
  for (int y = girdle + 1; y <= pavilionBottom; y++) {
    int t = y - girdle;
    int h = pavilionBottom - girdle;
    int halfW = girdleHalfW * (h - t) / h;
    if (halfW > 0) r.fillRect(cx - halfW, y, halfW * 2 + 1, 1, true);
  }
  r.drawLine(cx, crownTop, cx - girdleHalfW, girdle);
  r.drawLine(cx, crownTop, cx + girdleHalfW, girdle);
  r.drawLine(cx - girdleHalfW, girdle, cx + girdleHalfW, girdle);
  r.drawLine(cx - girdleHalfW, girdle, cx, pavilionBottom);
  r.drawLine(cx + girdleHalfW, girdle, cx, pavilionBottom);
  r.drawLine(cx - topHalfW, crownTop, cx - girdleHalfW / 2, girdle);
  r.drawLine(cx + topHalfW, crownTop, cx + girdleHalfW / 2, girdle);
}

// New icons for Fruit Frenzy and wild

static void drawIconLemon(GfxRenderer& r, int cx, int cy) {
  // Oval lemon shape: wide ellipse with small nub at right end
  int hw = 26;  // half-width
  int hh = 18;  // half-height
  for (int dy = -hh; dy <= hh; dy++) {
    // Ellipse: x^2/hw^2 + y^2/hh^2 <= 1  =>  x <= hw * sqrt(1 - (dy/hh)^2)
    int dx = 0;
    while (dx < hw) {
      int lhs = dx * dx * hh * hh + dy * dy * hw * hw;
      if (lhs > hw * hw * hh * hh) break;
      dx++;
    }
    if (dx > 0) r.fillRect(cx - dx, cy + dy, dx * 2, 1, true);
  }
  // Small nub at right end (top)
  r.fillRect(cx + hw - 4, cy - hh - 4, 6, 5, true);
  r.fillRect(cx + hw - 2, cy - hh - 7, 3, 4, true);
}

static void drawIconOrange(GfxRenderer& r, int cx, int cy) {
  // Large filled circle
  int cr = 22;
  int drawCy = cy + 2;
  for (int dy = -cr; dy <= cr; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
    if (dx > 0) r.fillRect(cx - dx, drawCy + dy, dx * 2 + 1, 1, true);
  }
  // Small stem on top
  r.fillRect(cx - 1, drawCy - cr - 5, 3, 6, true);
  // Leaf to the right of stem
  for (int dy = -3; dy <= 3; dy++) {
    int w = 4 - (dy < 0 ? -dy : dy);
    if (w > 0) r.fillRect(cx + 2, drawCy - cr - 3 + dy, w, 1, true);
  }
  // White highlight dot (inset)
  r.fillRect(cx - cr / 2 - 2, drawCy - cr / 2, 6, 4, false);
}

static void drawIconGrape(GfxRenderer& r, int cx, int cy) {
  // Cluster of 6 small circles in 1-2-3 pyramid arrangement
  int gr = 8;  // grape circle radius
  int sp = gr * 2 + 2;  // spacing

  // Row 1 (top): 1 grape
  {
    int gcy = cy - sp;
    for (int dy = -gr; dy <= gr; dy++) {
      int dx = 0;
      while ((dx + 1) * (dx + 1) + dy * dy <= gr * gr) dx++;
      if (dx > 0) r.fillRect(cx - dx, gcy + dy, dx * 2 + 1, 1, true);
    }
  }
  // Row 2 (middle): 2 grapes
  {
    int gcy = cy;
    int offX = sp / 2;
    for (int gi = 0; gi < 2; gi++) {
      int gcx = cx + (gi == 0 ? -offX : offX);
      for (int dy = -gr; dy <= gr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= gr * gr) dx++;
        if (dx > 0) r.fillRect(gcx - dx, gcy + dy, dx * 2 + 1, 1, true);
      }
    }
  }
  // Row 3 (bottom): 3 grapes
  {
    int gcy = cy + sp;
    for (int gi = 0; gi < 3; gi++) {
      int gcx = cx + (gi - 1) * sp;
      for (int dy = -gr; dy <= gr; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= gr * gr) dx++;
        if (dx > 0) r.fillRect(gcx - dx, gcy + dy, dx * 2 + 1, 1, true);
      }
    }
  }
  // Small stem at top
  r.fillRect(cx - 1, cy - sp - gr - 5, 3, 6, true);
}

static void drawIconMelon(GfxRenderer& r, int cx, int cy) {
  // Half circle (flat top, dome at bottom) with stripe lines
  int cr = 24;
  int topY = cy - 6;
  // Flat top bar
  r.fillRect(cx - cr, topY, cr * 2 + 1, 3, true);
  // Dome: lower half circle
  for (int dy = 0; dy <= cr; dy++) {
    // For a circle centered at topY with radius cr, x = sqrt(cr^2 - dy^2)
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
    if (dx > 0) r.fillRect(cx - dx, topY + dy, dx * 2 + 1, 1, true);
  }
  // Three vertical stripes (white lines on black dome)
  for (int si = 0; si < 3; si++) {
    int sx = cx - cr / 2 + si * (cr / 2);
    // Find vertical span of dome at this x
    int startY = topY;
    int endY = topY;
    for (int dy = 0; dy <= cr; dy++) {
      int dx = 0;
      while ((dx + 1) * (dx + 1) + dy * dy <= cr * cr) dx++;
      if (sx >= cx - dx && sx <= cx + dx) endY = topY + dy;
    }
    if (endY > startY + 4) {
      r.drawLine(sx, startY + 2, sx, endY - 2);
      // Override to white
      r.fillRect(sx, startY + 2, 1, endY - startY - 4, false);
    }
  }
}

static void drawIconPlum(GfxRenderer& r, int cx, int cy) {
  // Slightly tall oval with a small stem
  int hw = 20;
  int hh = 24;
  int drawCy = cy + 2;
  for (int dy = -hh; dy <= hh; dy++) {
    int dx = 0;
    while (dx < hw) {
      int lhs = dx * dx * hh * hh + dy * dy * hw * hw;
      if (lhs > hw * hw * hh * hh) break;
      dx++;
    }
    if (dx > 0) r.fillRect(cx - dx, drawCy + dy, dx * 2, 1, true);
  }
  // Stem
  r.fillRect(cx - 1, drawCy - hh - 5, 3, 6, true);
  // Small highlight
  r.fillRect(cx - hw / 2, drawCy - hh / 2, 5, 4, false);
}

static void drawIcon7Outline(GfxRenderer& r, int cx, int cy) {
  // Outlined "7" — same geometry as drawIcon7 but using draw instead of fill
  // Top horizontal bar (outlined as rect)
  r.drawRect(cx - 18, cy - 25, 36, 7);
  // Left serif (outlined)
  r.drawRect(cx - 18, cy - 18, 7, 5);
  // Diagonal stroke outline (draw only edge pixels of the thick diagonal)
  for (int i = 0; i < 40; i++) {
    int px = cx + 14 - i * 28 / 40;
    int py = cy - 18 + i;
    // Draw just the outline of the 7-pixel-wide stroke
    r.drawPixel(px - 3, py, true);
    r.drawPixel(px + 3, py, true);
  }
  // Top and bottom edge of diagonal
  r.drawPixel(cx + 14 - 3, cy - 18, true);
  r.drawPixel(cx + 14 + 3, cy - 18, true);
  r.drawPixel(cx - 14 - 3, cy + 22, true);
  r.drawPixel(cx - 14 + 3, cy + 22, true);
}

// Draw ellipse outline (3px thick ring) for coin animation
static void drawCoinEllipse(GfxRenderer& r, int cx, int cy, int radiusX, int radiusY) {
  if (radiusX < 2) {
    // Edge-on: just draw a vertical line
    r.fillRect(cx - 1, cy - radiusY, 3, radiusY * 2, true);
    return;
  }
  for (int dy = -radiusY; dy <= radiusY; dy++) {
    float t = 1.0f - (float)(dy * dy) / (float)(radiusY * radiusY);
    if (t < 0.0f) t = 0.0f;
    int outerW = (int)(radiusX * sqrtf(t));
    int innerW = (int)((radiusX - 3) * sqrtf(t));
    if (innerW < 0) innerW = 0;
    if (outerW > innerW) {
      // Left arc
      r.fillRect(cx - outerW, cy + dy, outerW - innerW, 1, true);
      // Right arc
      r.fillRect(cx + innerW + 1, cy + dy, outerW - innerW, 1, true);
    }
  }
}

void CasinoActivity::drawSlotIcon(int cx, int cy, uint8_t symbolId) {
  switch (symbolId) {
    case 0:  drawIcon7(renderer, cx, cy);        break;
    case 1:  drawIconBar(renderer, cx, cy);      break;
    case 2:  drawIconCherry(renderer, cx, cy);   break;
    case 3:  drawIconBell(renderer, cx, cy);     break;
    case 4:  drawIconStar(renderer, cx, cy);     break;
    case 5:  drawIconDiamond(renderer, cx, cy);  break;
    case 6:  drawIconLemon(renderer, cx, cy);    break;
    case 7:  drawIconOrange(renderer, cx, cy);   break;
    case 8:  drawIconGrape(renderer, cx, cy);    break;
    case 9:  drawIconMelon(renderer, cx, cy);    break;
    case 10: drawIconPlum(renderer, cx, cy);     break;
    case 11: drawIcon7Outline(renderer, cx, cy); break;
    default: break;
  }
}

void CasinoActivity::drawReelBlur(int x, int y, int w, int h, int frameOffset) {
  int bandH = 14;
  int shift = (frameOffset * 8) % (bandH * 2);
  for (int by = 0; by < h; by += bandH) {
    int drawY = y + ((by + shift) % h);
    int drawH = std::min(bandH, y + h - drawY);
    if (drawH > 0) {
      Color c = ((by / bandH) % 2 == 0) ? DarkGray : LightGray;
      renderer.fillRectDither(x + 4, drawY, w - 8, drawH, c);
    }
  }
}

void CasinoActivity::drawSlotReel(int x, int y, int symbolId, bool held, bool blur, int blurFrame) {
  int rw = 130, rh = 130;
  renderer.fillRect(x + 3, y + 3, rw, rh, true);   // shadow
  renderer.fillRect(x, y, rw, rh, false);            // white
  renderer.drawRect(x, y, rw, rh);
  renderer.drawRect(x + 2, y + 2, rw - 4, rh - 4);

  int cx = x + rw / 2;
  int cy = y + rh / 2;

  if (blur) {
    drawReelBlur(x + 3, y + 3, rw - 6, rh - 6, blurFrame);
  } else {
    drawSlotIcon(cx, cy, (uint8_t)symbolId);
  }

  // "HOLD" indicator at bottom of reel box
  if (held) {
    renderer.fillRect(x + 2, y + rh - 20, rw - 4, 18, true);
    int tw = renderer.getTextWidth(SMALL_FONT_ID, "HOLD");
    renderer.drawText(SMALL_FONT_ID, cx - tw / 2, y + rh - 17, "HOLD", false);
  }
}

// ================================================================
// SLOT MACHINE
// ================================================================

void CasinoActivity::slotsLoop() {
  switch (slotsScreen) {
    case SLOTS_MENU:    slotsMenuLoop();    break;
    case SLOTS_PLAY:    slotsPlayLoop();    break;
    case SLOTS_PAYOUTS: slotsPayoutsLoop(); break;
    case SLOTS_POWERUPS: slotsPowerupsLoop(); break;
  }
}

void CasinoActivity::slotsMenuLoop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    screen = LOBBY;
    requestUpdate();
    return;
  }
  buttonNavigator.onNext([this] {
    slotsMachineMenuIndex = ButtonNavigator::nextIndex(slotsMachineMenuIndex, NUM_MACHINES);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    slotsMachineMenuIndex = ButtonNavigator::previousIndex(slotsMachineMenuIndex, NUM_MACHINES);
    requestUpdate();
  });
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    slotsMachineIndex = slotsMachineMenuIndex;
    slotsScreen = SLOTS_PLAY;
    slotsPlayState = SP_BET;
    // Set bet to at least the machine's minimum
    const auto& machine = MACHINES[slotsMachineIndex];
    while (betIndex < NUM_BETS - 1 && BET_OPTIONS[betIndex] < machine.minBet) betIndex++;
    // Reset powerups and holds
    slotsPowerups = SlotsPowerups{};
    for (int i = 0; i < 3; i++) reelHold[i] = false;
    holdCursor = 0;
    requestUpdate();
  }
}

void CasinoActivity::slotsPlayLoop() {
  const auto& machine = MACHINES[slotsMachineIndex];

  if (slotsPlayState == SP_BET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      slotsScreen = SLOTS_MENU;
      slotsShowLastResult = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
      slotsScreen = SLOTS_PAYOUTS;
      slotsShowLastResult = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      slotsScreen = SLOTS_POWERUPS;
      slotsShowLastResult = false;
      powerupMenuIndex = 0;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (betIndex > 0 && BET_OPTIONS[betIndex - 1] >= machine.minBet) betIndex--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (betIndex < NUM_BETS - 1) betIndex++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      slotsShowLastResult = false;
      bool isFreeSpinActive = slotsPowerups.freeSpins > 0;
      if (!isFreeSpinActive && credits < currentBet()) {
        if (credits <= 0) { credits = 1000; saveCredits(); }
        requestUpdate();
        return;
      }
      if (!isFreeSpinActive) {
        credits -= currentBet();
      } else {
        slotsPowerups.freeSpins--;  // consume the free spin
      }
      if (machine.hasHoldReel) {
        slotsPlayState = SP_HOLD_SELECT;
        holdCursor = 0;
      } else {
        slotsSpin();
      }
      requestUpdate();
    }
    return;
  }

  if (slotsPlayState == SP_HOLD_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (holdCursor > 0) holdCursor--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (holdCursor < 2) holdCursor++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      reelHold[holdCursor] = !reelHold[holdCursor];
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      slotsSpin();
    }
    return;
  }

  if (slotsPlayState == SP_SPIN) {
    unsigned long elapsed = millis() - animStartMs;
    int frame = (int)(elapsed / SPIN_FRAME_MS);
    if (frame >= SPIN_FRAMES) {
      // All reels stopped — use pre-computed final values
      for (int i = 0; i < 3; i++) reels[i] = finalReels[i];
      slotsEvaluate();
    } else if (frame != animFrame) {
      animFrame = frame;
      const auto& mach = MACHINES[slotsMachineIndex];
      for (int i = 0; i < 3; i++) {
        if (reelHold[i] || frame >= reelStopFrame[i]) {
          reels[i] = finalReels[i];  // locked to final value
        } else {
          reels[i] = esp_random() % mach.numSymbols;  // still spinning
        }
      }
      requestUpdate();
    }
    return;
  }
}

void CasinoActivity::slotsPayoutsLoop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    slotsScreen = SLOTS_PLAY;
    requestUpdate();
  }
}

void CasinoActivity::slotsPowerupsLoop() {
  static constexpr int NUM_POWERUP_ITEMS = 3;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    slotsScreen = SLOTS_PLAY;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (powerupMenuIndex > 0) powerupMenuIndex--;
    requestUpdate();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (powerupMenuIndex < NUM_POWERUP_ITEMS - 1) powerupMenuIndex++;
    requestUpdate();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const auto& machine = MACHINES[slotsMachineIndex];
    switch (powerupMenuIndex) {
      case 0:  // 2x Multiplier — 50 credits
        if (credits >= 50) {
          credits -= 50;
          slotsPowerups.multiplier = 2;
          saveCredits();
          slotsScreen = SLOTS_PLAY;
          requestUpdate();
        }
        break;
      case 1:  // Free Spin — 30 credits
        if (credits >= 30) {
          credits -= 30;
          slotsPowerups.freeSpins++;
          saveCredits();
          slotsScreen = SLOTS_PLAY;
          requestUpdate();
        }
        break;
      case 2:  // Wild Card — 40 credits (only for machines without natural wild)
        if (machine.wildSymbolIdx == 0xFF && credits >= 40) {
          credits -= 40;
          slotsPowerups.wildActive = true;
          saveCredits();
          slotsScreen = SLOTS_PLAY;
          requestUpdate();
        }
        break;
    }
  }
}

void CasinoActivity::slotsSpin() {
  // Pre-compute final reel values
  const auto& machine = MACHINES[slotsMachineIndex];
  for (int i = 0; i < 3; i++) {
    if (!reelHold[i]) finalReels[i] = esp_random() % machine.numSymbols;
    else finalReels[i] = reels[i];
  }
  slotsPlayState = SP_SPIN;
  animFrame = 0;
  animStartMs = millis();
  requestUpdate();
}

void CasinoActivity::slotsEvaluate() {
  const auto& machine = MACHINES[slotsMachineIndex];
  // Use pre-computed final reel values
  for (int i = 0; i < 3; i++) {
    reels[i] = finalReels[i];
  }
  // Reset holds after use
  for (int i = 0; i < 3; i++) reelHold[i] = false;

  int bet = currentBet();
  int win = 0;

  // Check if a reel is wild
  auto isWild = [&](int reelIdx) -> bool {
    if (machine.wildSymbolIdx != 0xFF && reels[reelIdx] == machine.wildSymbolIdx) return true;
    return false;
  };

  // Find best 3-of-a-kind match including wilds
  int bestMatchCount = 0;
  int bestSymbolIdx = -1;
  for (int s = 0; s < machine.numSymbols; s++) {
    uint8_t sid = machine.symbols.ids[s];
    if (machine.wildSymbolIdx != 0xFF && sid == machine.wildSymbolIdx) continue;
    int count = 0;
    for (int r = 0; r < 3; r++) {
      if (machine.symbols.ids[reels[r]] == sid || isWild(r)) count++;
    }
    if (count > bestMatchCount) {
      bestMatchCount = count;
      bestSymbolIdx = s;
    }
  }

  // If wildActive powerup, treat one non-matching reel as bonus count
  if (slotsPowerups.wildActive && bestMatchCount == 2) {
    bestMatchCount = 3;  // force jackpot with wild card
  }

  if (bestMatchCount >= 3 && bestSymbolIdx >= 0) {
    win = bet * machine.symbols.payouts[bestSymbolIdx];
    resultMessage = "JACKPOT!";
    // Free spin award (Fruit Frenzy: 3x Cherry = symbol index 0 in that machine)
    if (machine.hasFreeSpin && bestSymbolIdx == 0) {
      slotsPowerups.freeSpins += 3;
      resultMessage = "JACKPOT + 3 FREE SPINS!";
    }
  } else if (bestMatchCount == 2 && machine.twoMatchMult > 0) {
    win = bet * machine.twoMatchMult;
    resultMessage = "Two Match!";
  } else {
    resultMessage = "No Luck";
  }

  // Apply multiplier powerup
  if (slotsPowerups.multiplier > 1 && win > 0) {
    win *= slotsPowerups.multiplier;
    slotsPowerups.multiplier = 1;
  }

  // Consume wild powerup after use
  if (slotsPowerups.wildActive) slotsPowerups.wildActive = false;

  credits += win;
  resultAmount = win > 0 ? win : -bet;
  slotsShowLastResult = true;
  slotsPlayState = SP_BET;
  saveCredits();
  requestUpdate();
}

// ================================================================
// SLOT RENDER DISPATCH
// ================================================================

void CasinoActivity::slotsRender() {
  switch (slotsScreen) {
    case SLOTS_MENU:    slotsRenderMenu();    break;
    case SLOTS_PLAY:    slotsRenderPlay();    break;
    case SLOTS_PAYOUTS: slotsRenderPayouts(); break;
    case SLOTS_POWERUPS: slotsRenderPowerups(); break;
  }
}

void CasinoActivity::slotsRenderMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Slot Machine", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);
  renderCreditsBar();

  int contentTop = 90;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight},
      NUM_MACHINES, slotsMachineMenuIndex,
      [](int i) { return std::string(CasinoActivity::MACHINES[i].name); },
      [](int i) { return std::string(CasinoActivity::MACHINES[i].description); });

  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CasinoActivity::slotsRenderPlay() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& machine = MACHINES[slotsMachineIndex];

  // Machine name header
  renderer.drawCenteredText(UI_12_FONT_ID, 12, machine.name, true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 32, pageWidth, 32);

  // Credits + Bet on same line
  char credBuf[32], betBuf[32];
  snprintf(credBuf, sizeof(credBuf), "Credits: %d", credits);
  snprintf(betBuf, sizeof(betBuf), "Bet: %d", currentBet());
  renderer.drawText(UI_10_FONT_ID, 10, 38, credBuf);
  int bw = renderer.getTextWidth(UI_10_FONT_ID, betBuf);
  renderer.drawText(UI_10_FONT_ID, pageWidth - bw - 10, 38, betBuf);
  renderer.drawLine(0, 60, pageWidth, 60);

  // 3 reels centered — 130x130 each, gap=15
  int reelW = 130, reelH = 130, gap = 15;
  int totalW = reelW * 3 + gap * 2;
  int startX = (pageWidth - totalW) / 2;
  int reelY = 75;

  bool spinning = (slotsPlayState == SP_SPIN);
  bool holdSelect = (slotsPlayState == SP_HOLD_SELECT);

  for (int i = 0; i < 3; i++) {
    // Staggered stop: reel 0 stops at frame 9, reel 1 at 10, reel 2 at 11
    bool reelDone = !spinning || (animFrame >= reelStopFrame[i]);
    bool blur = spinning && !reelDone;
    bool held = reelHold[i];
    // Show hold cursor highlight
    if (holdSelect && holdCursor == i) {
      // Draw a thicker border by drawing one extra rect
      renderer.drawRect(startX + i * (reelW + gap) - 2, reelY - 2, reelW + 4, reelH + 4);
    }
    drawSlotReel(startX + i * (reelW + gap), reelY, reels[i], held, blur, animFrame);
  }

  int infoY = reelY + reelH + 18;

  // Hold select instruction
  if (holdSelect) {
    renderer.drawCenteredText(UI_10_FONT_ID, infoY, "Select reels to HOLD");
    infoY += 22;
  }

  // Active powerup status
  bool hasPowerup = slotsPowerups.multiplier > 1 || slotsPowerups.freeSpins > 0 || slotsPowerups.wildActive;
  if (hasPowerup) {
    char pwBuf[48];
    if (slotsPowerups.freeSpins > 0) {
      snprintf(pwBuf, sizeof(pwBuf), "Free Spins: %d", slotsPowerups.freeSpins);
    } else if (slotsPowerups.multiplier > 1) {
      snprintf(pwBuf, sizeof(pwBuf), "%dx ACTIVE", slotsPowerups.multiplier);
    } else {
      snprintf(pwBuf, sizeof(pwBuf), "WILD ACTIVE");
    }
    renderer.drawCenteredText(SMALL_FONT_ID, infoY, pwBuf);
    infoY += 20;
  }

  // Spinning label
  if (spinning) {
    renderer.drawCenteredText(UI_12_FONT_ID, infoY, "SPINNING...", true, EpdFontFamily::BOLD);
  }

  // Inline result from last spin
  if (slotsShowLastResult && !spinning) {
    renderer.drawCenteredText(UI_12_FONT_ID, infoY, resultMessage.c_str(), true, EpdFontFamily::BOLD);
    infoY += 28;
    char amtBuf[32];
    if (resultAmount > 0) {
      snprintf(amtBuf, sizeof(amtBuf), "+%d credits", resultAmount);
    } else if (resultAmount < 0) {
      snprintf(amtBuf, sizeof(amtBuf), "%d credits", resultAmount);
    } else {
      snprintf(amtBuf, sizeof(amtBuf), "Push");
    }
    renderer.drawCenteredText(UI_10_FONT_ID, infoY, amtBuf);
  }

  // Button hints
  if (holdSelect) {
    const auto labels = mappedInput.mapLabels("Spin", "Toggle", "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels("Back", "Spin", "Bet-", "Bet+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void CasinoActivity::slotsRenderPayouts() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& machine = MACHINES[slotsMachineIndex];

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Payout Table", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);

  char macBuf[48];
  snprintf(macBuf, sizeof(macBuf), "Machine: %s", machine.name);
  renderer.drawCenteredText(UI_10_FONT_ID, 55, macBuf);
  renderer.drawLine(15, 75, pageWidth - 15, 75);

  // Symbol names
  static constexpr const char* SYMBOL_DISPLAY_NAMES[] = {
      "Seven", "BAR", "Cherry", "Bell", "Star", "Diamond",
      "Lemon", "Orange", "Grape", "Melon", "Plum", "7-Wild"
  };

  int y = 88;
  for (int s = 0; s < machine.numSymbols; s++) {
    uint8_t sid = machine.symbols.ids[s];
    int8_t pay = machine.symbols.payouts[s];
    char lineBuf[48];
    const char* sname = (sid < 12) ? SYMBOL_DISPLAY_NAMES[sid] : "?";
    if (machine.wildSymbolIdx != 0xFF && sid == machine.wildSymbolIdx) {
      snprintf(lineBuf, sizeof(lineBuf), "%-8s  3x = %dx  (WILD)", sname, (int)pay);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "%-8s  3x = %dx", sname, (int)pay);
    }
    renderer.drawText(SMALL_FONT_ID, 20, y, lineBuf);
    y += 20;
  }

  if (machine.twoMatchMult > 0) {
    char twoBuf[32];
    snprintf(twoBuf, sizeof(twoBuf), "2-of-a-kind = %dx", (int)machine.twoMatchMult);
    renderer.drawText(SMALL_FONT_ID, 20, y + 4, twoBuf);
    y += 24;
  }

  if (machine.hasFreeSpin) {
    renderer.drawText(SMALL_FONT_ID, 20, y + 4, "3x top symbol = +3 Free Spins");
    y += 20;
  }
  if (machine.hasHoldReel) {
    renderer.drawText(SMALL_FONT_ID, 20, y + 4, "Hold reels between spins");
  }

  char minBuf[32];
  snprintf(minBuf, sizeof(minBuf), "Min bet: %d", (int)machine.minBet);
  renderer.drawCenteredText(SMALL_FONT_ID, 710, minBuf);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CasinoActivity::slotsRenderPowerups() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& machine = MACHINES[slotsMachineIndex];

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Powerups", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);

  char credBuf[32];
  snprintf(credBuf, sizeof(credBuf), "Credits: %d", credits);
  renderer.drawCenteredText(UI_10_FONT_ID, 55, credBuf);
  renderer.drawLine(15, 75, pageWidth - 15, 75);

  static constexpr int NUM_POWERUP_ITEMS = 3;
  static const char* pwNames[] = {
      "2x Multiplier",
      "Free Spin",
      "Wild Card"
  };
  static const char* pwDescs[] = {
      "Next win doubled  —  50 cr",
      "One free spin     —  30 cr",
      "Force 2-match win —  40 cr"
  };
  static const int pwCosts[] = {50, 30, 40};

  int y = 100;
  for (int i = 0; i < NUM_POWERUP_ITEMS; i++) {
    bool selected = (i == powerupMenuIndex);
    bool available = true;
    if (i == 2 && machine.wildSymbolIdx != 0xFF) available = false;  // machine has own wild

    if (selected) {
      renderer.fillRect(10, y - 2, pageWidth - 20, 40, true);
    }

    char lineBuf[48];
    snprintf(lineBuf, sizeof(lineBuf), "%s", pwNames[i]);
    renderer.drawText(UI_10_FONT_ID, 20, y + 2, lineBuf, selected);

    if (!available) {
      renderer.drawText(SMALL_FONT_ID, 20, y + 20, "N/A (machine has wild)", selected);
    } else if (credits < pwCosts[i]) {
      renderer.drawText(SMALL_FONT_ID, 20, y + 20, pwDescs[i], selected);
      // Mark as too expensive
      int tw = renderer.getTextWidth(SMALL_FONT_ID, "Not enough credits");
      renderer.drawText(SMALL_FONT_ID, pageWidth - tw - 20, y + 20, "Not enough credits", selected);
    } else {
      renderer.drawText(SMALL_FONT_ID, 20, y + 20, pwDescs[i], selected);
    }

    y += 50;
  }

  const auto labels = mappedInput.mapLabels("Back", "Buy", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ================================================================
// BLACKJACK
// ================================================================

void CasinoActivity::bjShuffle() {
  int idx = 0;
  for (uint8_t s = 0; s < 4; s++) {
    for (uint8_t r = 1; r <= 13; r++) {
      deck[idx++] = {r, s};
    }
  }
  // Fisher-Yates shuffle
  for (int i = 51; i > 0; i--) {
    int j = esp_random() % (i + 1);
    Card tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp;
  }
  deckPos = 0;
}

CasinoActivity::Card CasinoActivity::bjDraw() {
  if (deckPos >= 52) bjShuffle();
  return deck[deckPos++];
}

int CasinoActivity::bjHandValue(const std::vector<Card>& hand) const {
  int total = 0, aces = 0;
  for (auto& c : hand) {
    if (c.rank == 1) { total += 11; aces++; }
    else if (c.rank >= 10) total += 10;
    else total += c.rank;
  }
  while (total > 21 && aces > 0) { total -= 10; aces--; }
  return total;
}

bool CasinoActivity::bjIsBlackjack(const std::vector<Card>& hand) const {
  return hand.size() == 2 && bjHandValue(hand) == 21;
}

std::string CasinoActivity::bjCardStr(const Card& c) const {
  return std::string(RANK_CHARS[c.rank]) + SUIT_CHARS[c.suit];
}

void CasinoActivity::bjDeal() {
  playerHand.clear();
  dealerHand.clear();
  dealerRevealed = false;

  playerHand.push_back(bjDraw());
  dealerHand.push_back(bjDraw());
  playerHand.push_back(bjDraw());
  dealerHand.push_back(bjDraw());

  bjState = BJ_PLAYING;

  // Check for natural blackjack
  if (bjIsBlackjack(playerHand)) {
    dealerRevealed = true;
    bjEvaluate();
  }
}

void CasinoActivity::bjDealerPlay() {
  dealerRevealed = true;
  bjState = BJ_DEALER;

  while (bjHandValue(dealerHand) < 17) {
    dealerHand.push_back(bjDraw());
  }

  bjEvaluate();
}

void CasinoActivity::bjEvaluate() {
  int pVal = bjHandValue(playerHand);
  int dVal = bjHandValue(dealerHand);
  int bet = currentBet();

  if (pVal > 21) {
    resultMessage = "BUST!";
    resultAmount = -bet;
  } else if (bjIsBlackjack(playerHand) && !bjIsBlackjack(dealerHand)) {
    int win = bet * 5 / 2;  // 2.5x
    credits += win;
    resultMessage = "BLACKJACK!";
    resultAmount = win;
  } else if (dVal > 21) {
    credits += bet * 2;
    resultMessage = "Dealer Busts!";
    resultAmount = bet;
  } else if (pVal > dVal) {
    credits += bet * 2;
    resultMessage = "You Win!";
    resultAmount = bet;
  } else if (pVal == dVal) {
    credits += bet;  // push — return bet
    resultMessage = "Push";
    resultAmount = 0;
  } else {
    resultMessage = "Dealer Wins";
    resultAmount = -bet;
  }

  showingResult = true;
  bjState = BJ_BET;
  saveCredits();
  requestUpdate();
}

void CasinoActivity::bjLoop() {
  if (bjState == BJ_BET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      screen = LOBBY; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (betIndex > 0) betIndex--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (betIndex < NUM_BETS - 1) betIndex++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (credits < BET_OPTIONS[0]) {
        resetCredits();
        requestUpdate();
      } else if (credits >= currentBet()) {
        credits -= currentBet();
        bjDeal();
        requestUpdate();
      }
    }
    return;
  }

  if (bjState == BJ_PLAYING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Hit
      playerHand.push_back(bjDraw());
      if (bjHandValue(playerHand) > 21) {
        dealerRevealed = true;
        bjEvaluate();
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Stand
      bjDealerPlay();
      requestUpdate();
    }
    return;
  }
}

void CasinoActivity::bjRender() {
  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Blackjack", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);
  renderCreditsBar();

  if (bjState == BJ_BET) {
    renderer.drawCenteredText(UI_10_FONT_ID, 160, "Place your bet");
    renderBetSelector(220);
    if (credits < BET_OPTIONS[0]) {
      renderer.drawCenteredText(UI_10_FONT_ID, 310, "Broke! Press OK to reset");
    }
    const auto labels = mappedInput.mapLabels("Back", "Deal", "Bet-", "Bet+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // Dealer hand — cards are 70px wide, stride 78px
  renderer.drawText(UI_10_FONT_ID, 15, 85, "Dealer:", true, EpdFontFamily::BOLD);
  for (int i = 0; i < (int)dealerHand.size(); i++) {
    bool hidden = (!dealerRevealed && i == 1);
    drawCard(15 + i * 78, 108, dealerHand[i], hidden);
  }
  if (dealerRevealed || dealerHand.size() > 0) {
    int dVal = dealerRevealed ? bjHandValue(dealerHand) : bjHandValue({dealerHand[0]});
    char buf[16];
    snprintf(buf, sizeof(buf), "= %d%s", dVal, dealerRevealed ? "" : " + ?");
    renderer.drawText(UI_10_FONT_ID, 15 + (int)dealerHand.size() * 78 + 10, 148, buf);
  }

  // Player hand
  renderer.drawText(UI_10_FONT_ID, 15, 240, "You:", true, EpdFontFamily::BOLD);
  for (int i = 0; i < (int)playerHand.size(); i++) {
    drawCard(15 + i * 78, 263, playerHand[i]);
  }
  {
    int pVal = bjHandValue(playerHand);
    char buf[16];
    snprintf(buf, sizeof(buf), "= %d", pVal);
    renderer.drawText(UI_10_FONT_ID, 15 + (int)playerHand.size() * 78 + 10, 303, buf, true, EpdFontFamily::BOLD);
  }

  if (bjState == BJ_PLAYING) {
    const auto labels = mappedInput.mapLabels("Stand", "Hit", "", "Stand");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

// ================================================================
// COIN FLIP
// ================================================================

void CasinoActivity::coinLoop() {
  if (coinState == COIN_BET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      screen = LOBBY; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (betIndex > 0) betIndex--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (betIndex < NUM_BETS - 1) betIndex++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (credits < BET_OPTIONS[0]) {
        resetCredits();
        requestUpdate();
      } else if (credits >= currentBet()) {
        coinState = COIN_PICK;
        requestUpdate();
      }
    }
    return;
  }

  if (coinState == COIN_PICK) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      coinPick = 0; requestUpdate();  // Heads
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      coinPick = 1; requestUpdate();  // Tails
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      coinFlip();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      coinState = COIN_BET; requestUpdate();
    }
    return;
  }

  if (coinState == COIN_FLIPPING) {
    unsigned long elapsed = millis() - coinAnimStartMs;
    int frame = (int)(elapsed / COIN_FRAME_MS);
    if (frame >= COIN_ANIM_FRAMES) {
      // Animation done — resolve bet
      int bet = currentBet();
      credits -= bet;
      if (coinResult == coinPick) {
        credits += bet * 2;
        resultMessage = coinResult == 0 ? "HEADS - You Win!" : "TAILS - You Win!";
        resultAmount = bet;
      } else {
        resultMessage = coinResult == 0 ? "HEADS - You Lose" : "TAILS - You Lose";
        resultAmount = -bet;
      }
      showingResult = true;
      coinState = COIN_BET;
      saveCredits();
      requestUpdate();
    } else if (frame != coinAnimFrame) {
      coinAnimFrame = frame;
      requestUpdate();
    }
    return;
  }
}

void CasinoActivity::coinFlip() {
  coinResult = esp_random() % 2;  // pre-compute result
  coinAnimFrame = 0;
  coinAnimStartMs = millis();
  coinState = COIN_FLIPPING;
  requestUpdate();
}

void CasinoActivity::coinRender() {
  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Coin Flip", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);
  renderCreditsBar();

  if (coinState == COIN_BET) {
    renderer.drawCenteredText(UI_10_FONT_ID, 160, "Double or nothing!");
    renderBetSelector(220);
    if (credits < BET_OPTIONS[0]) {
      renderer.drawCenteredText(UI_10_FONT_ID, 310, "Broke! Press OK to reset");
    }
    const auto labels = mappedInput.mapLabels("Back", "Play", "Bet-", "Bet+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (coinState == COIN_PICK) {
    renderer.drawCenteredText(UI_10_FONT_ID, 160, "Pick your side:");

    // Draw coin — larger square, moved up
    int cx = pageWidth / 2, cy = 260, cr = 90;
    renderer.drawRect(cx - cr, cy - cr, cr * 2, cr * 2);
    renderer.drawRect(cx - cr + 3, cy - cr + 3, cr * 2 - 6, cr * 2 - 6);
    renderer.drawCenteredText(UI_12_FONT_ID, cy - 14, coinPick == 0 ? "HEADS" : "TAILS", true, EpdFontFamily::BOLD);

    // Selection indicators
    renderer.drawCenteredText(UI_10_FONT_ID, 400, coinPick == 0 ? "> HEADS <" : "  HEADS  ");
    renderer.drawCenteredText(UI_10_FONT_ID, 440, coinPick == 1 ? "> TAILS <" : "  TAILS  ");

    char betBuf[32];
    snprintf(betBuf, sizeof(betBuf), "Bet: %d", currentBet());
    renderer.drawCenteredText(UI_10_FONT_ID, 490, betBuf);

    const auto labels = mappedInput.mapLabels("Cancel", "Flip!", "Heads", "Tails");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (coinState == COIN_FLIPPING) {
    renderer.drawCenteredText(UI_10_FONT_ID, 100, "Flipping...");

    int cx = pageWidth / 2;
    int cy = 280;
    int fullR = 80;

    // Animate coin rotation: vary horizontal radius
    int radiusX;
    switch (coinAnimFrame) {
      case 0: radiusX = fullR; break;          // full circle
      case 1: radiusX = fullR * 3 / 5; break;  // squished
      case 2: radiusX = 2; break;              // edge-on
      case 3: radiusX = fullR * 3 / 5; break;  // expanding
      default: radiusX = fullR; break;          // full
    }

    drawCoinEllipse(renderer, cx, cy, radiusX, fullR);

    // Show "?" during animation
    if (radiusX > 20) {
      renderer.drawCenteredText(UI_12_FONT_ID, cy - 14, "?", true, EpdFontFamily::BOLD);
    }

    char betBuf[32];
    snprintf(betBuf, sizeof(betBuf), "Bet: %d", currentBet());
    renderer.drawCenteredText(UI_10_FONT_ID, 420, betBuf);
  }
}

// ================================================================
// HIGHER / LOWER
// ================================================================

void CasinoActivity::hlDraw() {
  hlCurrentCard = bjDraw();  // reuse deck
}

void CasinoActivity::hlLoop() {
  if (hlState == HL_BET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      screen = LOBBY; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (betIndex > 0) betIndex--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (betIndex < NUM_BETS - 1) betIndex++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (credits < BET_OPTIONS[0]) {
        resetCredits();
        requestUpdate();
      } else if (credits >= currentBet()) {
        credits -= currentBet();
        hlPot = currentBet();
        hlStreak = 0;
        if (deckPos > 40) bjShuffle();  // reshuffle if low
        hlDraw();
        hlState = HL_PLAYING;
        requestUpdate();
      }
    }
    return;
  }

  if (hlState == HL_PLAYING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      hlGuess(true);  // Higher
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      hlGuess(false);  // Lower
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      hlCashOut();  // Take winnings
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      hlCashOut();  // Take winnings on back too
    }
    return;
  }
}

void CasinoActivity::hlGuess(bool higher) {
  hlNextCard = bjDraw();
  int cur = hlCurrentCard.rank;
  int nxt = hlNextCard.rank;

  bool correct = higher ? (nxt >= cur) : (nxt <= cur);

  if (correct) {
    hlStreak++;
    hlPot = hlPot * 3 / 2;  // 1.5x per correct guess
    hlCurrentCard = hlNextCard;
    requestUpdate();
  } else {
    // Lost everything
    resultMessage = "Wrong!";
    resultAmount = -(int)currentBet();
    showingResult = true;
    hlState = HL_BET;
    hlStreak = 0;
    saveCredits();
    requestUpdate();
  }
}

void CasinoActivity::hlCashOut() {
  credits += hlPot;
  int profit = hlPot - currentBet();

  char buf[32];
  snprintf(buf, sizeof(buf), "Cashed out! Streak: %d", hlStreak);
  resultMessage = buf;
  resultAmount = profit;
  showingResult = true;
  hlState = HL_BET;
  hlStreak = 0;
  saveCredits();
  requestUpdate();
}

void CasinoActivity::hlRender() {
  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Higher / Lower", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);
  renderCreditsBar();

  if (hlState == HL_BET) {
    renderer.drawCenteredText(UI_10_FONT_ID, 160, "Guess if the next card");
    renderer.drawCenteredText(UI_10_FONT_ID, 185, "is higher or lower!");
    renderBetSelector(260);

    if (credits < BET_OPTIONS[0]) {
      renderer.drawCenteredText(UI_10_FONT_ID, 310, "Broke! Press OK to reset");
    } else if (hlStreak > 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "Last streak: %d", hlStreak);
      renderer.drawCenteredText(SMALL_FONT_ID, 315, buf);
    }

    const auto labels = mappedInput.mapLabels("Back", "Start", "Bet-", "Bet+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // Show current card centered — 70px wide, so center at pageWidth/2 - 35
    drawCard(pageWidth / 2 - 35, 120, hlCurrentCard);

    // Streak and pot — below card (card bottom is at 120 + 100 = 220)
    char buf[48];
    snprintf(buf, sizeof(buf), "Streak: %d", hlStreak);
    renderer.drawCenteredText(UI_10_FONT_ID, 250, buf);

    snprintf(buf, sizeof(buf), "Pot: %d", hlPot);
    renderer.drawCenteredText(UI_12_FONT_ID, 290, buf, true, EpdFontFamily::BOLD);

    renderer.drawCenteredText(UI_10_FONT_ID, 370, "Higher or Lower?");

    const auto labels = mappedInput.mapLabels("", "Cash Out", "Higher", "Lower");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, "Higher", "Lower");
  }
}

// ================================================================
// ROULETTE
// ================================================================

void CasinoActivity::rlLoop() {
  if (rlState == RL_BET) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      screen = LOBBY; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (betIndex > 0) betIndex--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (betIndex < NUM_BETS - 1) betIndex++;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (credits < BET_OPTIONS[0]) {
        resetCredits();
        requestUpdate();
      } else if (credits >= currentBet()) {
        rlState = RL_PICK;
        requestUpdate();
      }
    }
    return;
  }

  if (rlState == RL_PICK) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      rlState = RL_BET; requestUpdate(); return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      int t = (int)rlBetType - 1;
      if (t < 0) t = RL_NUM_BET_TYPES - 1;
      rlBetType = (RLBetType)t;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      int t = (int)rlBetType + 1;
      if (t >= RL_NUM_BET_TYPES) t = 0;
      rlBetType = (RLBetType)t;
      requestUpdate();
    }
    if (rlBetType == RL_NUMBER) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        if (rlNumber > 0) rlNumber--; else rlNumber = 36;
        requestUpdate();
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (rlNumber < 36) rlNumber++; else rlNumber = 0;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      rlSpin();
    }
    return;
  }
}

void CasinoActivity::rlSpin() {
  int bet = currentBet();
  credits -= bet;
  rlResult = (int)(esp_random() % 37);  // 0-36

  bool win = false;
  int payout = 0;

  switch (rlBetType) {
    case RL_RED:
      win = (rlResult != 0 && isRed(rlResult));
      payout = bet * 2;
      break;
    case RL_BLACK:
      win = (rlResult != 0 && !isRed(rlResult));
      payout = bet * 2;
      break;
    case RL_ODD:
      win = (rlResult != 0 && rlResult % 2 == 1);
      payout = bet * 2;
      break;
    case RL_EVEN:
      win = (rlResult != 0 && rlResult % 2 == 0);
      payout = bet * 2;
      break;
    case RL_LOW:
      win = (rlResult >= 1 && rlResult <= 18);
      payout = bet * 2;
      break;
    case RL_HIGH:
      win = (rlResult >= 19 && rlResult <= 36);
      payout = bet * 2;
      break;
    case RL_DOZ1:
      win = (rlResult >= 1 && rlResult <= 12);
      payout = bet * 3;
      break;
    case RL_DOZ2:
      win = (rlResult >= 13 && rlResult <= 24);
      payout = bet * 3;
      break;
    case RL_DOZ3:
      win = (rlResult >= 25 && rlResult <= 36);
      payout = bet * 3;
      break;
    case RL_NUMBER:
      win = (rlResult == rlNumber);
      payout = bet * 36;
      break;
  }

  char resBuf[48];
  const char* color = rlResult == 0 ? " Zero" : (isRed(rlResult) ? " Red" : " Black");
  if (win) {
    credits += payout;
    resultAmount = payout - bet;
    snprintf(resBuf, sizeof(resBuf), "WIN! %d%s", rlResult, color);
  } else {
    resultAmount = -bet;
    snprintf(resBuf, sizeof(resBuf), "Lose: %d%s", rlResult, color);
  }
  resultMessage = resBuf;

  showingResult = true;
  rlState = RL_BET;
  saveCredits();
  requestUpdate();
}

// Helper: get bet type display name
const char* CasinoActivity::rlBetName(RLBetType t, int num, char* buf, int bufsz) {
  switch (t) {
    case RL_RED:    return "Red";
    case RL_BLACK:  return "Black";
    case RL_ODD:    return "Odd";
    case RL_EVEN:   return "Even";
    case RL_LOW:    return "Low (1-18)";
    case RL_HIGH:   return "High (19-36)";
    case RL_DOZ1:   return "Dozen 1-12";
    case RL_DOZ2:   return "Dozen 13-24";
    case RL_DOZ3:   return "Dozen 25-36";
    case RL_NUMBER:
      snprintf(buf, bufsz, "Number: %d", num);
      return buf;
  }
  return "";
}

// Returns true if cell n is covered by current bet type selection
bool CasinoActivity::rlCellHighlighted(int n) const {
  if (n == 0) return false;
  switch (rlBetType) {
    case RL_RED:    return isRed(n);
    case RL_BLACK:  return !isRed(n);
    case RL_ODD:    return (n % 2 == 1);
    case RL_EVEN:   return (n % 2 == 0);
    case RL_LOW:    return (n >= 1 && n <= 18);
    case RL_HIGH:   return (n >= 19 && n <= 36);
    case RL_DOZ1:   return (n >= 1 && n <= 12);
    case RL_DOZ2:   return (n >= 13 && n <= 24);
    case RL_DOZ3:   return (n >= 25 && n <= 36);
    case RL_NUMBER: return (n == rlNumber);
  }
  return false;
}

void CasinoActivity::rlRender() {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.drawCenteredText(UI_12_FONT_ID, 12, "Roulette", true, EpdFontFamily::BOLD);
  renderer.drawLine(15, 42, pageWidth - 15, 42);
  renderCreditsBar();

  if (rlState == RL_BET) {
    renderer.drawCenteredText(UI_10_FONT_ID, 160, "Place your bet");
    renderBetSelector(220);
    if (credits < BET_OPTIONS[0]) {
      renderer.drawCenteredText(UI_10_FONT_ID, 310, "Broke! Press OK to reset");
    }
    const auto labels = mappedInput.mapLabels("Back", "Play", "Bet-", "Bet+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // RL_PICK: show bet type selector + number grid

  // Bet type selector row
  char nameBuf[24];
  const char* btName = rlBetName(rlBetType, rlNumber, nameBuf, (int)sizeof(nameBuf));
  char selectorBuf[40];
  snprintf(selectorBuf, sizeof(selectorBuf), "< %s >", btName);
  renderer.drawCenteredText(UI_10_FONT_ID, 85, selectorBuf);

  // Bet amount
  char betBuf[24];
  snprintf(betBuf, sizeof(betBuf), "Bet: %d", currentBet());
  renderer.drawCenteredText(SMALL_FONT_ID, 110, betBuf);

  // Number grid: 3 columns, 13 rows (row 0 = zero spanning full width, rows 1-12 = numbers 1-36)
  // Cell dimensions: 42 wide, 30 tall
  static constexpr int CW = 42;
  static constexpr int CH = 30;
  static constexpr int COLS = 3;
  static constexpr int ROWS = 12;  // rows of numbers 1-36
  int gridX = (pageWidth - CW * COLS) / 2;
  int gridY = 130;

  // Row 0: "0" spanning full grid width
  {
    int zeroX = gridX;
    int zeroY = gridY;
    renderer.drawRect(zeroX, zeroY, CW * COLS, CH);
    renderer.drawRect(zeroX + 2, zeroY + 2, CW * COLS - 4, CH - 4);
    int tw = renderer.getTextWidth(SMALL_FONT_ID, "0");
    renderer.drawText(SMALL_FONT_ID, zeroX + (CW * COLS - tw) / 2, zeroY + 6, "0");
  }

  // Rows 1-12: numbers 1-36, 3 per row
  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      int n = row * COLS + col + 1;  // 1-36
      int cx = gridX + col * CW;
      int cy = gridY + CH + row * CH;

      renderer.drawRect(cx, cy, CW, CH);

      bool red = isRed(n);
      bool highlighted = rlCellHighlighted(n);

      // Red numbers: small filled dot in top-right corner
      if (red) {
        renderer.fillRect(cx + CW - 7, cy + 2, 5, 5, true);
      }

      // Highlighted cells: filled bottom strip marker
      if (highlighted) {
        renderer.fillRect(cx + 2, cy + CH - 5, CW - 4, 3, true);
      }

      // Selected number (RL_NUMBER): double border
      if (rlBetType == RL_NUMBER && n == rlNumber) {
        renderer.drawRect(cx + 1, cy + 1, CW - 2, CH - 2);
      }

      // Number text
      char nbuf[4];
      snprintf(nbuf, sizeof(nbuf), "%d", n);
      int tw = renderer.getTextWidth(SMALL_FONT_ID, nbuf);
      renderer.drawText(SMALL_FONT_ID, cx + (CW - tw) / 2, cy + 6, nbuf);
    }
  }

  // Button hints depend on whether we're in number mode
  if (rlBetType == RL_NUMBER) {
    const auto labels = mappedInput.mapLabels("Back", "Spin!", "Type", "Type");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, "#-", "#+");
  } else {
    const auto labels = mappedInput.mapLabels("Back", "Spin!", "Type", "Type");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}
