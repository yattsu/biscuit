#include "MinesweeperActivity.h"

#include <I18n.h>
#include <esp_random.h>

#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MinesweeperActivity::onEnter() {
  Activity::onEnter();
  state = DIFFICULTY_SELECT;
  difficultyIndex = 1;
  requestUpdate();
}

void MinesweeperActivity::initGame() {
  memset(grid, 0, sizeof(grid));
  cursorX = cols / 2;
  cursorY = rows / 2;
  flagCount = 0;
  firstReveal = true;
  state = PLAYING;
}

void MinesweeperActivity::placeMines(int safeX, int safeY) {
  int placed = 0;
  while (placed < mineCount) {
    int x = esp_random() % cols;
    int y = esp_random() % rows;
    // Don't place on safe cell or adjacent to it
    if (abs(x - safeX) <= 1 && abs(y - safeY) <= 1) continue;
    if (isMine(x, y)) continue;
    grid[x][y] = MINE;
    placed++;
  }
  calculateNumbers();
}

void MinesweeperActivity::calculateNumbers() {
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      if (isMine(x, y)) continue;
      grid[x][y] = (grid[x][y] & 0xF0) | static_cast<uint8_t>(countAdjacentMines(x, y));
    }
  }
}

int MinesweeperActivity::countAdjacentMines(int x, int y) const {
  int count = 0;
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < cols && ny >= 0 && ny < rows && isMine(nx, ny)) {
        count++;
      }
    }
  }
  return count;
}

void MinesweeperActivity::reveal(int x, int y) {
  if (x < 0 || x >= cols || y < 0 || y >= rows) return;
  if (isRevealed(x, y) || isFlagged(x, y)) return;

  // Iterative flood fill using fixed-size stack
  static constexpr int MAX_CELLS = 160;  // 10*16 max grid
  struct Cell { int8_t x, y; };
  Cell stack[MAX_CELLS];
  int top = 0;
  stack[top++] = {static_cast<int8_t>(x), static_cast<int8_t>(y)};

  while (top > 0) {
    Cell c = stack[--top];
    if (c.x < 0 || c.x >= cols || c.y < 0 || c.y >= rows) continue;
    if (isRevealed(c.x, c.y) || isFlagged(c.x, c.y)) continue;

    grid[c.x][c.y] |= REVEALED;

    if (getCellValue(c.x, c.y) == 0) {
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          if (dx == 0 && dy == 0) continue;
          if (top < MAX_CELLS) {
            stack[top++] = {static_cast<int8_t>(c.x + dx), static_cast<int8_t>(c.y + dy)};
          }
        }
      }
    }
  }
}

void MinesweeperActivity::revealAll() {
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      grid[x][y] |= REVEALED;
    }
  }
}

bool MinesweeperActivity::checkWin() const {
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      if (!isMine(x, y) && !isRevealed(x, y)) return false;
    }
  }
  return true;
}

void MinesweeperActivity::loop() {
  if (state == DIFFICULTY_SELECT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      difficultyIndex = (difficultyIndex + 1) % 3;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      difficultyIndex = (difficultyIndex + 2) % 3;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (difficultyIndex == 0) {
        cols = 8;
        rows = 12;
        mineCount = 10;
      } else if (difficultyIndex == 1) {
        cols = 10;
        rows = 16;
        mineCount = 25;
      } else {
        cols = 10;
        rows = 16;
        mineCount = 40;
      }
      initGame();
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == WON || state == GAME_OVER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = DIFFICULTY_SELECT;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // PLAYING state
  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (cursorY > 0) cursorY--;
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (cursorY < rows - 1) cursorY++;
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (cursorX > 0) cursorX--;
    moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (cursorX < cols - 1) cursorX++;
    moved = true;
  }

  // Confirm: short press = reveal, long press (500ms+) = toggle flag
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool longPress = mappedInput.getHeldTime() >= 500;
    if (!isRevealed(cursorX, cursorY)) {
      if (longPress) {
        // Toggle flag
        if (isFlagged(cursorX, cursorY)) {
          grid[cursorX][cursorY] &= ~FLAGGED;
          flagCount--;
        } else {
          grid[cursorX][cursorY] |= FLAGGED;
          flagCount++;
        }
      } else if (!isFlagged(cursorX, cursorY)) {
        // Reveal
        if (firstReveal) {
          firstReveal = false;
          placeMines(cursorX, cursorY);
        }
        if (isMine(cursorX, cursorY)) {
          revealAll();
          state = GAME_OVER;
        } else {
          reveal(cursorX, cursorY);
          if (checkWin()) {
            revealAll();
            state = WON;
          }
        }
      }
      moved = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (moved) requestUpdate();
}

void MinesweeperActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state == DIFFICULTY_SELECT) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DIFFICULTY));

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    const char* labels[] = {nullptr, nullptr, nullptr};
    labels[0] = tr(STR_EASY);
    labels[1] = tr(STR_MEDIUM);
    labels[2] = tr(STR_HARD);

    const char* subs[] = {"8x12, 10 mines", "10x16, 25 mines", "10x16, 40 mines"};

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, 3, difficultyIndex,
        [&labels](int index) -> std::string { return labels[index]; },
        [&subs](int index) -> std::string { return subs[index]; });

    const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Draw header with mine/flag info
  std::string headerInfo =
      std::string(tr(STR_MINES)) + ": " + std::to_string(mineCount) + "  " + tr(STR_FLAGS) + ": " + std::to_string(flagCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MINESWEEPER),
                 headerInfo.c_str());

  // Calculate cell size and grid position
  const int headerBottom = metrics.topPadding + metrics.headerHeight + 4;
  const int hintsTop = pageHeight - metrics.buttonHintsHeight;
  const int availHeight = hintsTop - headerBottom - 4;
  const int availWidth = pageWidth - 2 * metrics.contentSidePadding;

  int cellSize = std::min(availWidth / cols, availHeight / rows);
  if (cellSize > 44) cellSize = 44;
  if (cellSize < 20) cellSize = 20;

  const int gridWidth = cellSize * cols;
  const int gridHeight = cellSize * rows;
  const int gridX = (pageWidth - gridWidth) / 2;
  const int gridY = headerBottom + (availHeight - gridHeight) / 2;

  // Font metrics for centering text in cells
  const int fontH = renderer.getTextHeight(SMALL_FONT_ID);

  // Draw grid
  for (int x = 0; x < cols; x++) {
    for (int y = 0; y < rows; y++) {
      int px = gridX + x * cellSize;
      int py = gridY + y * cellSize;
      bool isCursor = (x == cursorX && y == cursorY && state == PLAYING);

      if (isRevealed(x, y)) {
        // Revealed cell
        if (isCursor) {
          // Cursor on revealed: inverted (black fill, white content)
          renderer.fillRect(px, py, cellSize, cellSize, true);
          if (isMine(x, y)) {
            int cx = px + cellSize / 2;
            int cy = py + cellSize / 2;
            int r = cellSize / 4;
            renderer.drawLine(cx - r, cy - r, cx + r, cy + r);
            renderer.drawLine(cx - r, cy + r, cx + r, cy - r);
          } else {
            int val = getCellValue(x, y);
            if (val > 0) {
              char num[2] = {static_cast<char>('0' + val), 0};
              int tw = renderer.getTextWidth(SMALL_FONT_ID, num, EpdFontFamily::BOLD);
              renderer.drawText(SMALL_FONT_ID, px + (cellSize - tw) / 2, py + (cellSize - fontH) / 2, num, false,
                                EpdFontFamily::BOLD);
            }
          }
        } else {
          // Normal revealed
          renderer.fillRect(px, py, cellSize, cellSize, false);
          if (isMine(x, y)) {
            renderer.drawRect(px, py, cellSize, cellSize, true);
            int cx = px + cellSize / 2;
            int cy = py + cellSize / 2;
            int r = cellSize / 4;
            renderer.drawLine(cx - r, cy - r, cx + r, cy + r);
            renderer.drawLine(cx - r, cy + r, cx + r, cy - r);
          } else {
            int val = getCellValue(x, y);
            if (val > 0) {
              // Has number: draw border + number
              renderer.drawRect(px, py, cellSize, cellSize, true);
              char num[2] = {static_cast<char>('0' + val), 0};
              int tw = renderer.getTextWidth(SMALL_FONT_ID, num, EpdFontFamily::BOLD);
              renderer.drawText(SMALL_FONT_ID, px + (cellSize - tw) / 2, py + (cellSize - fontH) / 2, num, true,
                                EpdFontFamily::BOLD);
            }
            // Empty (val==0): no border, just blank white
          }
        }
      } else {
        // Unrevealed cell
        bool flagged = isFlagged(x, y);

        if (isCursor) {
          // Cursor on unrevealed: inverted (black fill)
          renderer.fillRect(px, py, cellSize, cellSize, true);
          if (flagged) {
            int tw = renderer.getTextWidth(SMALL_FONT_ID, "F", EpdFontFamily::BOLD);
            renderer.drawText(SMALL_FONT_ID, px + (cellSize - tw) / 2, py + (cellSize - fontH) / 2, "F", false,
                              EpdFontFamily::BOLD);
          }
        } else {
          // Normal unrevealed: white with border
          renderer.fillRect(px, py, cellSize, cellSize, false);
          renderer.drawRect(px, py, cellSize, cellSize, true);
          if (flagged) {
            int tw = renderer.getTextWidth(SMALL_FONT_ID, "F", EpdFontFamily::BOLD);
            renderer.drawText(SMALL_FONT_ID, px + (cellSize - tw) / 2, py + (cellSize - fontH) / 2, "F", true,
                              EpdFontFamily::BOLD);
          }
        }
      }
    }
  }

  // Win/lose overlay
  if (state == WON) {
    GUI.drawPopup(renderer, tr(STR_YOU_WIN));
  } else if (state == GAME_OVER) {
    GUI.drawPopup(renderer, tr(STR_GAME_OVER));
  }

  if (state == PLAYING) {
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_REVEAL), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, "Hold: Flag", labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_NEW_GAME), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
