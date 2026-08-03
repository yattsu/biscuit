#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <cstring>
#include <esp_ota_ops.h>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  state = State::PICKING;
  scanForBinFiles();
  selectorIndex = 0;
  requestUpdate();
}

void SdFirmwareUpdateActivity::scanForBinFiles() {
  binFiles.clear();

  auto root = Storage.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (FsHelpers::checkFileExtension(std::string_view{name}, ".bin")) {
        binFiles.push_back(std::string("/") + name);
      }
    }
    file.close();
  }
  root.close();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FW", "no next-update partition available");
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  // Run the same end-to-end integrity check (header / segment table / XOR checksum / SHA256
  // trailer) that the shared firmware-flasher applies right before raw-writing otadata. This
  // catches truncated or corrupted .bin files at confirmation time, before the user ever sees
  // the "Updating…" progress bar.
  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    state = State::PICKING;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    // immediate=true: wake the render task directly. We're in a tight sync
    // loop so the main loop won't drain the requestedUpdate flag for us.
    self->requestUpdate(true);
  };

  // Re-validate at flash time (TOCTOU): SD is removable, so don't trust the
  // pre-confirmation pass.
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    errorMessage = tr(STR_FIRMWARE_WRITE_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  if (state == State::FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (recoveryMode) {
        state = State::PICKING;
        scanForBinFiles();
        selectorIndex = 0;
        requestUpdate();
        return;
      }
      finish();
    }
    return;
  }

  if (state != State::PICKING) return;

  const int count = static_cast<int>(binFiles.size());

  if (count > 0) {
    buttonNavigator.onNext([this, count] {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count);
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && count > 0) {
    firmwarePath = binFiles[selectorIndex];
    if (!validateFirmware()) {
      state = State::FAILED;
      requestUpdate();
      return;
    }

    std::string heading = tr(STR_FIRMWARE_UPDATE_PROMPT);
    std::string body = firmwarePath;
    const auto pos = body.find_last_of('/');
    if (pos != std::string::npos) body = body.substr(pos + 1);

    state = State::CONFIRMING;
    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                           [this](const ActivityResult& result) { onConfirmationResult(result); });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (recoveryMode) return;  // nothing to go back to in recovery mode
    finish();
  }
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const char* headerText = recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);

  if (state == State::PICKING || state == State::CONFIRMING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerText);

    if (binFiles.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_BIN_FILES));
    } else {
      const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
      GUI.drawList(
          renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(binFiles.size()),
          static_cast<int>(selectorIndex), [this](int index) -> std::string { return binFiles[index]; });
    }

    const auto labels =
        mappedInput.mapLabels(recoveryMode ? "" : tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerText);

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) {
      return;
    }
    lastRenderedPercent = pct;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);

    int y = top + lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(pct), 100);
    y += metrics.progressBarHeight + metrics.verticalSpacing;
    y += lineHeight + metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
  } else if (state == State::SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, tr(STR_RESTARTING_HINT));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (!errorMessage.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
