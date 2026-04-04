#include "TextViewerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TextViewerActivity::onEnter() {
  Activity::onEnter();
  state = FILE_SELECT;
  fileIndex = 0;
  {
    RenderLock lock(*this);
    loadFileList();
  }
  requestUpdate();
}

void TextViewerActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileContent.clear();
}

void TextViewerActivity::loadFileList() {
  files.clear();

  // Search common directories for .txt files
  const char* searchDirs[] = {"/", "/biscuit/"};

  for (auto dir : searchDirs) {
    auto root = Storage.open(dir);
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      continue;
    }

    root.rewindDirectory();
    char name[256];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      file.getName(name, sizeof(name));
      if (!file.isDirectory()) {
        std::string fname(name);
        // Check for .txt extension
        if (fname.size() > 4) {
          std::string ext = fname.substr(fname.size() - 4);
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          if (ext == ".txt") {
            std::string fullPath = std::string(dir);
            if (fullPath.back() != '/') fullPath += '/';
            fullPath += fname;
            files.push_back(fullPath);
          }
        }
      }
      file.close();
    }
    root.close();
  }

  std::sort(files.begin(), files.end());
}

void TextViewerActivity::loadFile(const std::string& path) {
  fileContent.clear();
  filePath = path;

  RenderLock lock(*this);
  auto file = Storage.open(path.c_str());
  if (!file || file.isDirectory()) {
    if (file) file.close();
    fileContent = "(Unable to open file)";
    return;
  }

  // Read up to 32KB
  static constexpr size_t MAX_SIZE = 32768;
  size_t size = file.size();
  if (size > MAX_SIZE) size = MAX_SIZE;

  fileContent.resize(size);
  file.read(reinterpret_cast<uint8_t*>(&fileContent[0]), size);
  file.close();

  calculatePagination();
}

void TextViewerActivity::calculatePagination() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int usableWidth = pageWidth - metrics.contentSidePadding * 2;
  const int usableHeight =
      pageHeight - metrics.topPadding - metrics.headerHeight - metrics.verticalSpacing * 2 - metrics.buttonHintsHeight;
  const int linesPerPage = usableHeight / lineH;

  // Estimate chars per line based on average char width
  int avgCharWidth = renderer.getTextWidth(UI_10_FONT_ID, "M");
  if (avgCharWidth <= 0) avgCharWidth = 8;
  int charsPerLine = usableWidth / avgCharWidth;
  charsPerPage = charsPerLine * linesPerPage;

  if (charsPerPage <= 0) charsPerPage = 200;
  totalPages = (static_cast<int>(fileContent.size()) + charsPerPage - 1) / charsPerPage;
  if (totalPages < 1) totalPages = 1;
  currentPage = 0;
}

void TextViewerActivity::loop() {
  if (state == FILE_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    buttonNavigator.onNext([this] {
      fileIndex = ButtonNavigator::nextIndex(fileIndex, files.size());
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      fileIndex = ButtonNavigator::previousIndex(fileIndex, files.size());
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!files.empty()) {
        loadFile(files[fileIndex]);
        state = VIEWING;
        requestUpdate();
      }
    }
    return;
  }

  if (state == VIEWING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = FILE_SELECT;
      fileContent.clear();
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (currentPage < totalPages - 1) {
        currentPage++;
        requestUpdate();
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (currentPage > 0) {
        currentPage--;
        requestUpdate();
      }
    }
  }
}

void TextViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TEXT_VIEWER));

  switch (state) {
    case FILE_SELECT:
      renderFileSelect();
      break;
    case VIEWING:
      renderViewing();
      break;
  }

  renderer.displayBuffer();
}

void TextViewerActivity::renderFileSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (files.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_TXT_FILES));
  } else {
    int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(files.size()), fileIndex,
        [this](int i) {
          // Show just filename
          auto& path = files[i];
          auto pos = path.rfind('/');
          return pos != std::string::npos ? path.substr(pos + 1) : path;
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), files.empty() ? "" : "Open", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TextViewerActivity::renderViewing() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Page content
  int startIdx = currentPage * charsPerPage;
  int endIdx = startIdx + charsPerPage;
  if (endIdx > static_cast<int>(fileContent.size())) endIdx = fileContent.size();

  std::string pageText = fileContent.substr(startIdx, endIdx - startIdx);
  int usableWidth = pageWidth - metrics.contentSidePadding * 2;
  auto lines = renderer.wrappedText(UI_10_FONT_ID, pageText.c_str(), usableWidth, 30);

  for (auto& line : lines) {
    if (y > pageHeight - metrics.buttonHintsHeight - 10) break;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line.c_str());
    y += lineH;
  }

  // Page indicator
  char pageBuf[32];
  snprintf(pageBuf, sizeof(pageBuf), "%d/%d", currentPage + 1, totalPages);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "PgUp", "PgDn");
  GUI.drawButtonHints(renderer, labels.btn1, pageBuf, labels.btn3, labels.btn4);
}
