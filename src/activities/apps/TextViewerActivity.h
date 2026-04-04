#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TextViewerActivity final : public Activity {
 public:
  explicit TextViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TextViewer", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { FILE_SELECT, VIEWING };

  State state = FILE_SELECT;
  ButtonNavigator buttonNavigator;

  // File selection
  std::vector<std::string> files;
  int fileIndex = 0;

  // Viewing
  std::string filePath;
  std::string fileContent;
  int currentPage = 0;
  int totalPages = 0;
  int charsPerPage = 0;

  void loadFileList();
  void loadFile(const std::string& path);
  void calculatePagination();

  void renderFileSelect() const;
  void renderViewing() const;
};
