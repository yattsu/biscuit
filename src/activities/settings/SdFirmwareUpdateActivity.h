#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> scan the SD card root for .bin files, show a picker list.
 *  2) On selection: validate the .bin (header magic, size fits OTA partition).
 *  3) Show a confirmation prompt ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition, drawing a progress
 *     bar; on success, restart.
 *
 * Used both from Settings -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (UP + POWER held at boot).
 *
 * Intentionally does not reuse FileBrowserActivity: this only ever needs a
 * flat listing of .bin files at the SD root, so it keeps its own minimal
 * picker rather than adding a firmware-picker mode to the shared file
 * browser.
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State {
    PICKING,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : Activity("SdFirmwareUpdate", renderer, mappedInput), recoveryMode(recoveryMode) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }

 private:
  State state = State::PICKING;
  bool recoveryMode = false;

  ButtonNavigator buttonNavigator;
  std::vector<std::string> binFiles;
  size_t selectorIndex = 0;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  void scanForBinFiles();
  bool validateFirmware();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
