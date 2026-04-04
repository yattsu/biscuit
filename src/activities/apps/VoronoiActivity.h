#pragma once
#include <cstdint>

#include "activities/Activity.h"

class VoronoiActivity final : public Activity {
 public:
  explicit VoronoiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Voronoi", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int MAX_POINTS = 40;
  int numPoints = 20;

  struct Point {
    int x, y;
  };
  Point points[MAX_POINTS]{};
  int genWidth = 480;
  int genHeight = 800;

  void generate();
};
