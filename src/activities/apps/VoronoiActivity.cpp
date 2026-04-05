#include "VoronoiActivity.h"

#include <I18n.h>
#include <esp_random.h>

#include <cmath>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void VoronoiActivity::generate() {
  for (int i = 0; i < numPoints; i++) {
    points[i].x = esp_random() % genWidth;
    points[i].y = esp_random() % genHeight;
  }
  precompute();
}

void VoronoiActivity::precompute() {
  gridW = genWidth / GRID_STEP;
  gridH = genHeight / GRID_STEP;
  if (gridW > MAX_GRID_W) gridW = MAX_GRID_W;
  if (gridH > MAX_GRID_H) gridH = MAX_GRID_H;

  for (int gy = 0; gy < gridH; gy++) {
    int py = gy * GRID_STEP + GRID_STEP / 2;
    for (int gx = 0; gx < gridW; gx++) {
      int px = gx * GRID_STEP + GRID_STEP / 2;
      int minDist = INT32_MAX;
      uint8_t nearest = 0;
      for (int i = 0; i < numPoints; i++) {
        int dx = px - points[i].x;
        int dy = py - points[i].y;
        int dist = dx * dx + dy * dy;
        if (dist < minDist) {
          minDist = dist;
          nearest = static_cast<uint8_t>(i);
        }
      }
      nearestGrid[gy][gx] = nearest;
    }
  }
}

void VoronoiActivity::onEnter() {
  Activity::onEnter();
  genWidth = renderer.getScreenWidth();
  genHeight = renderer.getScreenHeight();
  generate();
  requestUpdate();
}

void VoronoiActivity::loop() {
  // Regenerate
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    generate();
    requestUpdate();
  }

  // Adjust point count
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (numPoints < MAX_POINTS) {
      numPoints += 5;
      generate();
      requestUpdate();
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (numPoints > 5) {
      numPoints -= 5;
      generate();
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void VoronoiActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Fill regions from pre-computed grid
  for (int gy = 0; gy < gridH; gy++) {
    for (int gx = 0; gx < gridW; gx++) {
      if (nearestGrid[gy][gx] % 2 == 0) {
        renderer.fillRect(gx * GRID_STEP, gy * GRID_STEP, GRID_STEP, GRID_STEP, true);
      }
    }
  }

  // Draw cell borders where nearest point changes
  for (int gy = 0; gy < gridH; gy++) {
    for (int gx = 0; gx < gridW; gx++) {
      uint8_t cur = nearestGrid[gy][gx];
      // Check right neighbor
      if (gx + 1 < gridW && cur != nearestGrid[gy][gx + 1]) {
        int bx = (gx + 1) * GRID_STEP;
        int by = gy * GRID_STEP;
        renderer.fillRect(bx - 1, by, 2, GRID_STEP, true);
      }
      // Check bottom neighbor
      if (gy + 1 < gridH && cur != nearestGrid[gy + 1][gx]) {
        int bx = gx * GRID_STEP;
        int by = (gy + 1) * GRID_STEP;
        renderer.fillRect(bx, by - 1, GRID_STEP, 2, true);
      }
    }
  }

  // Draw seed points
  for (int i = 0; i < numPoints; i++) {
    int px = points[i].x;
    int py = points[i].y;
    renderer.fillRect(px - 2, py - 2, 5, 5, !(i % 2 == 0));
    renderer.drawRect(px - 2, py - 2, 5, 5, true);
  }

  // Info overlay
  std::string info = std::string(tr(STR_POINTS)) + ": " + std::to_string(numPoints);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VORONOI),
                 info.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_REGENERATE), "+5", "-5");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
