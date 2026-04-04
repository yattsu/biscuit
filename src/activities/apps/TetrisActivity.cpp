#include "TetrisActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <esp_random.h>

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Tetromino shapes encoded as 4x4 bitmaps in a 16-bit value
// Bit layout: bits 15-12 = row 0, bits 11-8 = row 1, bits 7-4 = row 2, bits 3-0 = row 3
// Within each row, bit 3 = col 0 (leftmost), bit 0 = col 3
const TetrisActivity::Piece TetrisActivity::PIECES[7] = {
    // I
    {{0x0F00, 0x2222, 0x00F0, 0x4444}},
    // O
    {{0x6600, 0x6600, 0x6600, 0x6600}},
    // T
    {{0x4E00, 0x4640, 0x0E40, 0x4C40}},
    // S
    {{0x6C00, 0x4620, 0x06C0, 0x8C40}},
    // Z
    {{0xC600, 0x2640, 0x0C60, 0x4C80}},
    // J
    {{0x8E00, 0x6440, 0x0E20, 0x44C0}},
    // L
    {{0x2E00, 0x4460, 0x0E80, 0xC440}},
};

bool TetrisActivity::getPieceBit(uint16_t shape, int row, int col) {
  // row 0 is bits 15-12, row 1 is bits 11-8, etc.
  // col 0 is the highest bit within that nibble
  int bit = 15 - (row * 4 + col);
  return (shape >> bit) & 1;
}

int TetrisActivity::randomPiece() { return esp_random() % 7; }

void TetrisActivity::onEnter() {
  Activity::onEnter();
  initGame();
}

void TetrisActivity::onExit() { Activity::onExit(); }

void TetrisActivity::initGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  linesCleared = 0;
  level = 1;
  state = PLAYING;

  const auto& metrics = UITheme::getInstance().getMetrics();
  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();
  int boardPixelW = BOARD_W * CELL_SIZE;
  int boardPixelH = BOARD_H * CELL_SIZE;
  boardOffsetX = (screenW - boardPixelW) / 2;
  boardOffsetY = metrics.topPadding + 25;

  nextPiece = randomPiece();
  spawnPiece();
  lastDropMs = millis();
  requestUpdate();
}

void TetrisActivity::spawnPiece() {
  currentPiece = nextPiece;
  nextPiece = randomPiece();
  currentRotation = 0;
  pieceX = BOARD_W / 2 - 2;
  pieceY = 0;

  if (!canPlace(currentPiece, currentRotation, pieceX, pieceY)) {
    state = GAME_OVER;
  }
}

bool TetrisActivity::canPlace(int piece, int rotation, int x, int y) const {
  uint16_t shape = PIECES[piece].shape[rotation];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getPieceBit(shape, r, c)) continue;
      int bx = x + c;
      int by = y + r;
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) return false;
      if (by >= 0 && board[by][bx]) return false;
    }
  }
  return true;
}

void TetrisActivity::lockPiece() {
  uint16_t shape = PIECES[currentPiece].shape[currentRotation];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getPieceBit(shape, r, c)) continue;
      int bx = pieceX + c;
      int by = pieceY + r;
      if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
        board[by][bx] = 1;
      }
    }
  }
}

int TetrisActivity::clearLines() {
  int cleared = 0;
  for (int y = BOARD_H - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < BOARD_W; x++) {
      if (!board[y][x]) {
        full = false;
        break;
      }
    }
    if (full) {
      cleared++;
      // Shift everything above down
      for (int yy = y; yy > 0; yy--) {
        memcpy(board[yy], board[yy - 1], BOARD_W);
      }
      memset(board[0], 0, BOARD_W);
      y++;  // re-check this row
    }
  }
  return cleared;
}

unsigned long TetrisActivity::getDropInterval() const {
  // Speed increases with level
  unsigned long base = 800;
  unsigned long reduction = static_cast<unsigned long>(level - 1) * 70;
  if (reduction >= base - 100) return 100;
  return base - reduction;
}

void TetrisActivity::step() {
  if (canPlace(currentPiece, currentRotation, pieceX, pieceY + 1)) {
    pieceY++;
  } else {
    lockPiece();
    int lines = clearLines();
    if (lines > 0) {
      // Scoring: 1=100, 2=300, 3=500, 4=800
      static const int lineScores[] = {0, 100, 300, 500, 800};
      score += lineScores[lines] * level;
      linesCleared += lines;
      level = 1 + linesCleared / 10;
    }
    spawnPiece();
  }
  requestUpdate();
}

void TetrisActivity::loop() {
  if (state == GAME_OVER) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      initGame();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // Input
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (canPlace(currentPiece, currentRotation, pieceX - 1, pieceY)) {
      pieceX--;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (canPlace(currentPiece, currentRotation, pieceX + 1, pieceY)) {
      pieceX++;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    // Soft drop
    if (canPlace(currentPiece, currentRotation, pieceX, pieceY + 1)) {
      pieceY++;
      score += 1;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    // Rotate
    int newRot = (currentRotation + 1) % 4;
    if (canPlace(currentPiece, newRot, pieceX, pieceY)) {
      currentRotation = newRot;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Hard drop
    while (canPlace(currentPiece, currentRotation, pieceX, pieceY + 1)) {
      pieceY++;
      score += 2;
    }
    lockPiece();
    int lines = clearLines();
    if (lines > 0) {
      static const int lineScores[] = {0, 100, 300, 500, 800};
      score += lineScores[lines] * level;
      linesCleared += lines;
      level = 1 + linesCleared / 10;
    }
    spawnPiece();
    lastDropMs = millis();
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Auto-drop timer
  unsigned long now = millis();
  if (now - lastDropMs >= getDropInterval()) {
    lastDropMs = now;
    step();
  }
}

void TetrisActivity::drawCell(int screenX, int screenY, bool filled) const {
  if (filled) {
    renderer.fillRect(screenX + 1, screenY + 1, CELL_SIZE - 2, CELL_SIZE - 2);
  } else {
    renderer.drawRect(screenX, screenY, CELL_SIZE, CELL_SIZE);
  }
}

void TetrisActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (state) {
    case PLAYING:
      renderPlaying();
      break;
    case GAME_OVER:
      renderGameOver();
      break;
  }

  renderer.displayBuffer();
}

void TetrisActivity::renderPlaying() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int screenW = renderer.getScreenWidth();

  // Score/level at top
  char scoreBuf[48];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score:%d  Lv:%d", score, level);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding, scoreBuf);

  // Board border
  int bw = BOARD_W * CELL_SIZE;
  int bh = BOARD_H * CELL_SIZE;
  renderer.drawRect(boardOffsetX - 1, boardOffsetY - 1, bw + 2, bh + 2);

  // Draw board
  for (int y = 0; y < BOARD_H; y++) {
    for (int x = 0; x < BOARD_W; x++) {
      if (board[y][x]) {
        int sx = boardOffsetX + x * CELL_SIZE;
        int sy = boardOffsetY + y * CELL_SIZE;
        renderer.fillRect(sx + 1, sy + 1, CELL_SIZE - 2, CELL_SIZE - 2);
      }
    }
  }

  // Draw current piece
  uint16_t shape = PIECES[currentPiece].shape[currentRotation];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getPieceBit(shape, r, c)) continue;
      int bx = pieceX + c;
      int by = pieceY + r;
      if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
        int sx = boardOffsetX + bx * CELL_SIZE;
        int sy = boardOffsetY + by * CELL_SIZE;
        renderer.fillRect(sx + 1, sy + 1, CELL_SIZE - 2, CELL_SIZE - 2);
      }
    }
  }

  // Ghost piece (drop preview) - draw outline only
  int ghostY = pieceY;
  while (canPlace(currentPiece, currentRotation, pieceX, ghostY + 1)) {
    ghostY++;
  }
  if (ghostY != pieceY) {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (!getPieceBit(shape, r, c)) continue;
        int bx = pieceX + c;
        int by = ghostY + r;
        if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
          int sx = boardOffsetX + bx * CELL_SIZE;
          int sy = boardOffsetY + by * CELL_SIZE;
          renderer.drawRect(sx + 2, sy + 2, CELL_SIZE - 4, CELL_SIZE - 4);
        }
      }
    }
  }

  // Next piece preview - draw to the right of the board
  int previewX = boardOffsetX + bw + 15;
  int previewY = boardOffsetY + 20;
  renderer.drawText(SMALL_FONT_ID, previewX, boardOffsetY, "Next:");
  uint16_t nextShape = PIECES[nextPiece].shape[0];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getPieceBit(nextShape, r, c)) continue;
      int sx = previewX + c * (CELL_SIZE / 2);
      int sy = previewY + r * (CELL_SIZE / 2);
      renderer.fillRect(sx, sy, CELL_SIZE / 2 - 1, CELL_SIZE / 2 - 1);
    }
  }

  // Lines info
  char linesBuf[24];
  snprintf(linesBuf, sizeof(linesBuf), "Lines:%d", linesCleared);
  renderer.drawText(SMALL_FONT_ID, previewX, previewY + 60, linesBuf);
}

void TetrisActivity::renderGameOver() const {
  const auto pageHeight = renderer.getScreenHeight();
  int y = pageHeight / 2 - 40;

  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
  y += 40;

  char scoreBuf[48];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d  Lines: %d", score, linesCleared);
  renderer.drawCenteredText(UI_10_FONT_ID, y, scoreBuf);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
