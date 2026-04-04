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

  // Draw Voronoi diagram using brute-force nearest-neighbor
  // For e-ink: alternate black/white cells based on nearest point index parity
  // Use a step size to speed up rendering on constrained hardware
  constexpr int STEP = 4;

  for (int y = 0; y < pageHeight; y += STEP) {
    for (int x = 0; x < pageWidth; x += STEP) {
      int minDist = INT32_MAX;
      int nearest = 0;
      for (int i = 0; i < numPoints; i++) {
        int dx = x - points[i].x;
        int dy = y - points[i].y;
        int dist = dx * dx + dy * dy;
        if (dist < minDist) {
          minDist = dist;
          nearest = i;
        }
      }

      // Use pattern based on point index for visual variety
      // Every other region is filled, creating a mosaic
      if (nearest % 2 == 0) {
        renderer.fillRect(x, y, STEP, STEP, true);
      }
    }
  }

  // Draw cell borders by checking where nearest point changes
  for (int y = 0; y < pageHeight; y += 2) {
    for (int x = 0; x < pageWidth; x += 2) {
      int minDist = INT32_MAX;
      int nearest = 0;
      for (int i = 0; i < numPoints; i++) {
        int dx = x - points[i].x;
        int dy = y - points[i].y;
        int dist = dx * dx + dy * dy;
        if (dist < minDist) {
          minDist = dist;
          nearest = i;
        }
      }
      // Check neighbor
      int minDist2 = INT32_MAX;
      int nearest2 = 0;
      for (int i = 0; i < numPoints; i++) {
        int dx = (x + 2) - points[i].x;
        int dy = y - points[i].y;
        int dist = dx * dx + dy * dy;
        if (dist < minDist2) {
          minDist2 = dist;
          nearest2 = i;
        }
      }
      if (nearest != nearest2) {
        renderer.drawPixel(x, y, true);
        renderer.drawPixel(x + 1, y, true);
      }
      // Check below
      int minDist3 = INT32_MAX;
      int nearest3 = 0;
      for (int i = 0; i < numPoints; i++) {
        int dx = x - points[i].x;
        int dy = (y + 2) - points[i].y;
        int dist = dx * dx + dy * dy;
        if (dist < minDist3) {
          minDist3 = dist;
          nearest3 = i;
        }
      }
      if (nearest != nearest3) {
        renderer.drawPixel(x, y, true);
        renderer.drawPixel(x, y + 1, true);
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

  // Info overlay at bottom
  std::string info = std::string(tr(STR_POINTS)) + ": " + std::to_string(numPoints);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VORONOI),
                 info.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_REGENERATE), "+5", "-5");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
