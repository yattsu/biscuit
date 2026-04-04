#include "GameOfLifeActivity.h"

#include <I18n.h>
#include <esp_random.h>

#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void GameOfLifeActivity::onEnter() {
  Activity::onEnter();
  running = false;
  generation = 0;
  randomize();
  requestUpdate();
}

void GameOfLifeActivity::randomize() {
  for (int x = 0; x < COLS; x++) {
    for (int y = 0; y < ROWS; y++) {
      grid[x][y] = (esp_random() % 4 == 0) ? 1 : 0;
    }
  }
  generation = 0;
  population = countPopulation();
}

int GameOfLifeActivity::countNeighbors(int x, int y) const {
  int count = 0;
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx == 0 && dy == 0) continue;
      int nx = (x + dx + COLS) % COLS;
      int ny = (y + dy + ROWS) % ROWS;
      count += grid[nx][ny];
    }
  }
  return count;
}

void GameOfLifeActivity::step() {
  for (int x = 0; x < COLS; x++) {
    for (int y = 0; y < ROWS; y++) {
      int n = countNeighbors(x, y);
      if (grid[x][y]) {
        nextGrid[x][y] = (n == 2 || n == 3) ? 1 : 0;
      } else {
        nextGrid[x][y] = (n == 3) ? 1 : 0;
      }
    }
  }
  memcpy(grid, nextGrid, sizeof(grid));
  generation++;
  population = countPopulation();
}

int GameOfLifeActivity::countPopulation() const {
  int count = 0;
  for (int x = 0; x < COLS; x++) {
    for (int y = 0; y < ROWS; y++) {
      count += grid[x][y];
    }
  }
  return count;
}

void GameOfLifeActivity::loop() {
  if (running) {
    unsigned long now = millis();
    if (now - lastStepTime >= STEP_INTERVAL_MS) {
      lastStepTime = now;
      step();
      requestUpdate();
    }
  }

  // Toggle run/pause
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (running) {
      running = false;
    } else {
      running = true;
      lastStepTime = millis();
    }
    requestUpdate();
  }

  // Single step (when paused)
  if (!running && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    step();
    requestUpdate();
  }

  // Randomize
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    running = false;
    randomize();
    requestUpdate();
  }

  // Toggle cell at cursor (when paused)
  if (!running) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (cursorY > 0) cursorY--;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (cursorY < ROWS - 1) cursorY++;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void GameOfLifeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Header info
  std::string info = std::string(tr(STR_GENERATION)) + ": " + std::to_string(generation) + "  " + tr(STR_POPULATION) +
                     ": " + std::to_string(population);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAME_OF_LIFE),
                 info.c_str());

  // Calculate cell size
  const int headerBottom = metrics.topPadding + metrics.headerHeight + 2;
  const int hintsTop = pageHeight - metrics.buttonHintsHeight;
  const int availHeight = hintsTop - headerBottom - 2;
  const int availWidth = pageWidth;

  int cellW = availWidth / COLS;
  int cellH = availHeight / ROWS;
  int cellSize = std::min(cellW, cellH);
  if (cellSize < 2) cellSize = 2;

  const int gridWidth = cellSize * COLS;
  const int gridHeight = cellSize * ROWS;
  const int gridX = (pageWidth - gridWidth) / 2;
  const int gridY = headerBottom + (availHeight - gridHeight) / 2;

  // Draw cells - only draw alive cells (dead = white background from clearScreen)
  for (int x = 0; x < COLS; x++) {
    for (int y = 0; y < ROWS; y++) {
      if (grid[x][y]) {
        int px = gridX + x * cellSize;
        int py = gridY + y * cellSize;
        renderer.fillRect(px, py, cellSize, cellSize, true);
      }
    }
  }

  // Button hints
  if (running) {
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_PAUSED), tr(STR_RANDOMIZE), "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_RUNNING), tr(STR_RANDOMIZE), tr(STR_STEP));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
