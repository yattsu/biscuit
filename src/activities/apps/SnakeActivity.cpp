#include "SnakeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <esp_random.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SnakeActivity::onEnter() {
  Activity::onEnter();
  initGame();
}

void SnakeActivity::onExit() { Activity::onExit(); }

void SnakeActivity::initGame() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();

  // Reserve top area for score and bottom for button hints
  int topReserve = metrics.topPadding + 25;
  int bottomReserve = metrics.buttonHintsHeight;

  gridW = screenW / CELL_SIZE;
  gridH = (screenH - topReserve - bottomReserve) / CELL_SIZE;
  offsetX = (screenW - gridW * CELL_SIZE) / 2;
  offsetY = topReserve;

  snake.clear();
  int startX = gridW / 2;
  int startY = gridH / 2;
  snake.push_back({startX, startY});
  snake.push_back({startX - 1, startY});
  snake.push_back({startX - 2, startY});

  dirX = 1;
  dirY = 0;
  nextDirX = 1;
  nextDirY = 0;
  score = 0;
  state = PLAYING;
  lastStepMs = millis();

  spawnFood();
  requestUpdate();
}

void SnakeActivity::spawnFood() {
  int attempts = 0;
  do {
    food.x = esp_random() % gridW;
    food.y = esp_random() % gridH;
    attempts++;
  } while (isSnakeAt(food.x, food.y) && attempts < 1000);
}

bool SnakeActivity::isSnakeAt(int x, int y) const {
  for (auto& seg : snake) {
    if (seg.x == x && seg.y == y) return true;
  }
  return false;
}

void SnakeActivity::step() {
  // Apply buffered direction
  dirX = nextDirX;
  dirY = nextDirY;

  Point head = snake.front();
  Point newHead = {head.x + dirX, head.y + dirY};

  // Wall collision
  if (newHead.x < 0 || newHead.x >= gridW || newHead.y < 0 || newHead.y >= gridH) {
    state = GAME_OVER;
    requestUpdate();
    return;
  }

  // Self collision
  if (isSnakeAt(newHead.x, newHead.y)) {
    state = GAME_OVER;
    requestUpdate();
    return;
  }

  snake.insert(snake.begin(), newHead);

  // Check food
  if (newHead.x == food.x && newHead.y == food.y) {
    score += 10;
    spawnFood();
  } else {
    snake.pop_back();
  }

  requestUpdate();
}

void SnakeActivity::loop() {
  if (state == GAME_OVER) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      initGame();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // Direction input - prevent reversal
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && dirY == 0) {
    nextDirX = 0;
    nextDirY = -1;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) && dirY == 0) {
    nextDirX = 0;
    nextDirY = 1;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) && dirX == 0) {
    nextDirX = -1;
    nextDirY = 0;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right) && dirX == 0) {
    nextDirX = 1;
    nextDirY = 0;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Step timer
  unsigned long now = millis();
  if (now - lastStepMs >= STEP_INTERVAL_MS) {
    lastStepMs = now;
    step();
  }
}

void SnakeActivity::render(RenderLock&&) {
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

void SnakeActivity::renderPlaying() const {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Score
  char scoreBuf[32];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", score);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding, scoreBuf);

  // Grid border
  renderer.drawRect(offsetX - 1, offsetY - 1, gridW * CELL_SIZE + 2, gridH * CELL_SIZE + 2);

  // Snake
  for (auto& seg : snake) {
    int px = offsetX + seg.x * CELL_SIZE;
    int py = offsetY + seg.y * CELL_SIZE;
    renderer.fillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2);
  }

  // Food
  {
    int px = offsetX + food.x * CELL_SIZE;
    int py = offsetY + food.y * CELL_SIZE;
    renderer.drawRect(px + 2, py + 2, CELL_SIZE - 4, CELL_SIZE - 4);
    renderer.drawPixel(px + CELL_SIZE / 2, py + CELL_SIZE / 2);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SnakeActivity::renderGameOver() const {
  const auto pageHeight = renderer.getScreenHeight();
  int y = pageHeight / 2 - 40;

  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
  y += 40;

  char scoreBuf[32];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", score);
  renderer.drawCenteredText(UI_10_FONT_ID, y, scoreBuf);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
