#pragma once
#include <cstdint>

#include "activities/Activity.h"

class GameOfLifeActivity final : public Activity {
 public:
  explicit GameOfLifeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GameOfLife", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return running; }

 private:
  static constexpr int COLS = 48;
  static constexpr int ROWS = 80;

  uint8_t grid[COLS][ROWS]{};
  uint8_t nextGrid[COLS][ROWS]{};

  int cursorX = COLS / 2;
  int cursorY = ROWS / 2;
  bool running = false;
  int generation = 0;
  int population = 0;
  unsigned long lastStepTime = 0;
  static constexpr unsigned long STEP_INTERVAL_MS = 200;

  void randomize();
  void step();
  int countNeighbors(int x, int y) const;
  int countPopulation() const;
};
