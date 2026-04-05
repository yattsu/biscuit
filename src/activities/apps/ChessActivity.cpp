#include "ChessActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ChessActivity::initBoard() {
  memset(board, EMPTY, sizeof(board));
  // Black pieces (top)
  board[0][0] = B_ROOK;   board[0][1] = B_KNIGHT; board[0][2] = B_BISHOP; board[0][3] = B_QUEEN;
  board[0][4] = B_KING;   board[0][5] = B_BISHOP; board[0][6] = B_KNIGHT; board[0][7] = B_ROOK;
  for (int c = 0; c < 8; c++) board[1][c] = B_PAWN;
  // White pieces (bottom)
  for (int c = 0; c < 8; c++) board[6][c] = W_PAWN;
  board[7][0] = W_ROOK;   board[7][1] = W_KNIGHT; board[7][2] = W_BISHOP; board[7][3] = W_QUEEN;
  board[7][4] = W_KING;   board[7][5] = W_BISHOP; board[7][6] = W_KNIGHT; board[7][7] = W_ROOK;
}

const char* ChessActivity::pieceChar(uint8_t piece) const {
  switch (piece) {
    case W_PAWN: case B_PAWN: return "P";
    case W_ROOK: case B_ROOK: return "R";
    case W_KNIGHT: case B_KNIGHT: return "N";
    case W_BISHOP: case B_BISHOP: return "B";
    case W_QUEEN: case B_QUEEN: return "Q";
    case W_KING: case B_KING: return "K";
    default: return "";
  }
}

void ChessActivity::addSlidingMoves(int fx, int fy, int dx, int dy,
                                     std::vector<std::pair<int, int>>& moves) const {
  int x = fx + dx, y = fy + dy;
  while (x >= 0 && x < 8 && y >= 0 && y < 8) {
    if (board[x][y] == EMPTY) {
      moves.push_back({x, y});
    } else {
      if (isEnemyPiece(board[x][y])) moves.push_back({x, y});
      break;
    }
    x += dx;
    y += dy;
  }
}

void ChessActivity::addMovesForPiece(int fx, int fy, std::vector<std::pair<int, int>>& moves) const {
  uint8_t p = board[fx][fy];
  int dir = isWhite(p) ? -1 : 1;

  switch (p) {
    case W_PAWN: case B_PAWN: {
      int startRow = isWhite(p) ? 6 : 1;
      // Forward
      if (fx + dir >= 0 && fx + dir < 8 && board[fx + dir][fy] == EMPTY) {
        moves.push_back({fx + dir, fy});
        // Double move from start
        if (fx == startRow && board[fx + 2 * dir][fy] == EMPTY) {
          moves.push_back({fx + 2 * dir, fy});
        }
      }
      // Captures
      for (int dc : {-1, 1}) {
        int nr = fx + dir, nc = fy + dc;
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && isEnemyPiece(board[nr][nc])) {
          moves.push_back({nr, nc});
        }
      }
      break;
    }
    case W_ROOK: case B_ROOK:
      for (auto [dx, dy] : {std::pair{1,0},{-1,0},{0,1},{0,-1}}) addSlidingMoves(fx, fy, dx, dy, moves);
      break;
    case W_BISHOP: case B_BISHOP:
      for (auto [dx, dy] : {std::pair{1,1},{1,-1},{-1,1},{-1,-1}}) addSlidingMoves(fx, fy, dx, dy, moves);
      break;
    case W_QUEEN: case B_QUEEN:
      for (auto [dx, dy] : {std::pair{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}})
        addSlidingMoves(fx, fy, dx, dy, moves);
      break;
    case W_KNIGHT: case B_KNIGHT:
      for (auto [dx, dy] : {std::pair{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}}) {
        int nr = fx + dx, nc = fy + dy;
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && !isOwnPiece(board[nr][nc])) {
          moves.push_back({nr, nc});
        }
      }
      break;
    case W_KING: case B_KING:
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          if (dx == 0 && dy == 0) continue;
          int nr = fx + dx, nc = fy + dy;
          if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && !isOwnPiece(board[nr][nc])) {
            moves.push_back({nr, nc});
          }
        }
      }
      break;
    default: break;
  }
}

bool ChessActivity::isSquareAttacked(int tx, int ty, bool byWhite) {
  bool savedTurn = whiteTurn;
  whiteTurn = byWhite;
  std::vector<std::pair<int, int>> moves;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      uint8_t p = board[r][c];
      if (p == EMPTY) continue;
      if (byWhite ? !isWhite(p) : !isBlack(p)) continue;
      moves.clear();
      addMovesForPiece(r, c, moves);
      for (auto& [mr, mc] : moves) {
        if (mr == tx && mc == ty) { whiteTurn = savedTurn; return true; }
      }
    }
  }
  whiteTurn = savedTurn;
  return false;
}

bool ChessActivity::findKing(bool white, int& kx, int& ky) const {
  uint8_t target = white ? W_KING : B_KING;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (board[r][c] == target) { kx = r; ky = c; return true; }
    }
  }
  return false;
}

bool ChessActivity::wouldBeInCheck(int fx, int fy, int tx, int ty) {
  // Simulate move
  uint8_t savedTarget = board[tx][ty];
  uint8_t savedSource = board[fx][fy];
  board[tx][ty] = savedSource;
  board[fx][fy] = EMPTY;

  int kx, ky;
  findKing(whiteTurn, kx, ky);
  bool check = isSquareAttacked(kx, ky, !whiteTurn);

  // Undo
  board[fx][fy] = savedSource;
  board[tx][ty] = savedTarget;
  return check;
}

void ChessActivity::computeValidMoves(int fx, int fy) {
  validMoves.clear();
  std::vector<std::pair<int, int>> pseudo;
  addMovesForPiece(fx, fy, pseudo);
  for (auto& [tr, tc] : pseudo) {
    if (!wouldBeInCheck(fx, fy, tr, tc)) {
      validMoves.push_back({tr, tc});
    }
  }
}

bool ChessActivity::hasAnyLegalMove() {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (!isOwnPiece(board[r][c])) continue;
      std::vector<std::pair<int, int>> pseudo;
      addMovesForPiece(r, c, pseudo);
      for (auto& [tr, tc] : pseudo) {
        if (!wouldBeInCheck(r, c, tr, tc)) return true;
      }
    }
  }
  return false;
}

void ChessActivity::doMove(int fx, int fy, int tx, int ty) {
  uint8_t p = board[fx][fy];
  board[tx][ty] = p;
  board[fx][fy] = EMPTY;

  // Pawn promotion to queen
  if ((p == W_PAWN && tx == 0) || (p == B_PAWN && tx == 7)) {
    board[tx][ty] = isWhite(p) ? W_QUEEN : B_QUEEN;
  }
}

void ChessActivity::checkGameState() {
  int kx, ky;
  findKing(whiteTurn, kx, ky);
  inCheck = isSquareAttacked(kx, ky, !whiteTurn);

  if (!hasAnyLegalMove()) {
    gameOver = true;
    state = GAME_OVER;
    gameOverMsg = inCheck ? tr(STR_CHECKMATE) : tr(STR_STALEMATE);
  }
}

void ChessActivity::onEnter() {
  Activity::onEnter();
  initBoard();
  state = SELECT_PIECE;
  whiteTurn = true;
  inCheck = false;
  gameOver = false;
  cursorX = 4;
  cursorY = 7;
  validMoves.clear();
  requestUpdate();
}

void ChessActivity::loop() {
  if (state == GAME_OVER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      initBoard();
      state = SELECT_PIECE;
      whiteTurn = true;
      inCheck = false;
      gameOver = false;
      validMoves.clear();
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) { if (cursorY > 0) cursorY--; moved = true; }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) { if (cursorY < 7) cursorY++; moved = true; }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) { if (cursorX > 0) cursorX--; moved = true; }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) { if (cursorX < 7) cursorX++; moved = true; }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == SELECT_PIECE) {
      if (isOwnPiece(board[cursorY][cursorX])) {
        selectedX = cursorX;
        selectedY = cursorY;
        computeValidMoves(cursorY, cursorX);
        if (!validMoves.empty()) {
          state = SELECT_TARGET;
        }
        moved = true;
      }
    } else if (state == SELECT_TARGET) {
      // Check if target is valid
      auto it = std::find(validMoves.begin(), validMoves.end(), std::pair<int, int>{cursorY, cursorX});
      if (it != validMoves.end()) {
        doMove(selectedY, selectedX, cursorY, cursorX);
        whiteTurn = !whiteTurn;
        state = SELECT_PIECE;
        validMoves.clear();
        checkGameState();
      } else if (isOwnPiece(board[cursorY][cursorX])) {
        // Re-select different piece
        selectedX = cursorX;
        selectedY = cursorY;
        computeValidMoves(cursorY, cursorX);
        if (validMoves.empty()) {
          state = SELECT_PIECE;
        }
      } else {
        state = SELECT_PIECE;
        validMoves.clear();
      }
      moved = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == SELECT_TARGET) {
      state = SELECT_PIECE;
      validMoves.clear();
      moved = true;
    } else {
      finish();
      return;
    }
  }

  if (moved) requestUpdate();
}

void ChessActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Header
  std::string turnInfo = whiteTurn ? tr(STR_WHITE) : tr(STR_BLACK_PIECE);
  if (inCheck) turnInfo += std::string(" - ") + tr(STR_CHECK);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CHESS),
                 turnInfo.c_str());

  const int headerBottom = metrics.topPadding + metrics.headerHeight + 4;
  const int hintsTop = pageHeight - metrics.buttonHintsHeight;
  const int availHeight = hintsTop - headerBottom - 4;
  const int availWidth = pageWidth - 2 * metrics.contentSidePadding;

  int cellSize = std::min(availWidth / 8, availHeight / 8);
  if (cellSize > 55) cellSize = 55;

  const int gridSize = cellSize * 8;
  const int gridX = (pageWidth - gridSize) / 2;
  const int gridY = headerBottom + (availHeight - gridSize) / 2;
  const int fontH = renderer.getTextHeight(UI_10_FONT_ID);

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      int px = gridX + c * cellSize;
      int py = gridY + r * cellSize;
      bool darkSquare = ((r + c) % 2 == 1);
      bool isCursor = (c == cursorX && r == cursorY);
      bool isSelected = (state == SELECT_TARGET && c == selectedX && r == selectedY);
      bool isValidTarget = false;

      if (state == SELECT_TARGET) {
        for (auto& [mr, mc] : validMoves) {
          if (mr == r && mc == c) { isValidTarget = true; break; }
        }
      }

      // Background
      if (isCursor) {
        renderer.fillRect(px, py, cellSize, cellSize, true);
      } else if (isSelected) {
        renderer.fillRect(px, py, cellSize, cellSize, true);
      } else if (darkSquare) {
        // Crosshatch pattern for dark squares
        renderer.fillRect(px, py, cellSize, cellSize, false);
        for (int i = 0; i < cellSize; i += 3) {
          renderer.drawLine(px + i, py, px + i, py + cellSize);
        }
      } else {
        renderer.fillRect(px, py, cellSize, cellSize, false);
      }

      // Valid move indicator: small dot in center
      if (isValidTarget && !isCursor) {
        int cx = px + cellSize / 2;
        int cy = py + cellSize / 2;
        renderer.fillRect(cx - 3, cy - 3, 6, 6, true);
      }

      // Piece
      uint8_t piece = board[r][c];
      if (piece != EMPTY) {
        const char* ch = pieceChar(piece);
        int tw = renderer.getTextWidth(UI_10_FONT_ID, ch, EpdFontFamily::BOLD);
        bool drawWhite = isCursor || isSelected;
        EpdFontFamily::Style style = isBlack(piece) ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
        renderer.drawText(UI_10_FONT_ID, px + (cellSize - tw) / 2, py + (cellSize - fontH) / 2, ch, !drawWhite,
                          style);
      }

      // Cell border
      renderer.drawRect(px, py, cellSize, cellSize, true);
    }
  }

  if (state == GAME_OVER) {
    GUI.drawPopup(renderer, gameOverMsg.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_NEW_GAME), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
