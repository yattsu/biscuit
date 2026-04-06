# Biscuit

**Biscuit is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**, the open-source firmware for the Xteink X4 e-paper device. All core reading functionality — EPUB parsing, rendering, file management, WiFi upload, OTA updates, KOReader Sync — comes from CrossPoint. Biscuit builds on top of it with additional apps, themes, and tools.

If you're looking for a clean, well-maintained e-reader firmware, use CrossPoint directly. Biscuit is an experimental fork that adds things CrossPoint doesn't ship.

![](./docs/images/cover.jpg)

## What Biscuit adds on top of CrossPoint

* **Apps module** — wireless testing tools, network utilities, games, and productivity apps
* **Military theme** — optional rugged UI theme with inverted headers, sharp corners, dashed separators
* **SD card tools** — captive portal templates, DuckyScript runner, BLE beacon tools
* **Internationalization** — additional language contributions

Everything else (the reader, the UI framework, the build system, the hardware support) is CrossPoint.

## CrossPoint features (inherited)

All of these come from upstream CrossPoint:

- EPUB parsing and rendering (EPUB 2 and EPUB 3)
- Image support within EPUB
- Saved reading position, file explorer, nested folders
- Custom sleep screen (cover, dark, light, custom)
- WiFi book upload, WiFi OTA updates
- KOReader Sync integration
- Configurable font, layout, and display options
- Screen rotation, multi-language support

See the [CrossPoint README](https://github.com/crosspoint-reader/crosspoint-reader) for the full feature list and [the user guide](./USER_GUIDE.md) for usage instructions.

## Biscuit features

### Apps

**Network tools:** WiFi connect, WiFi scanner, host scanner, ping, DNS lookup

**Wireless testing:** BLE scanner, packet monitor, PCAP capture, beacon spammer, WiFi deauther, captive portal (evil portal), BLE beacon spam, AirTag spoofer, BadBLE (DuckyScript over BLE HID)

**Games:** Minesweeper, Sudoku, Chess, Game of Life, Voronoi, Snake, Tetris, Dice Roller

**Utilities:** Password manager, Pomodoro timer, NTP clock, Stopwatch, QR generator, Morse code, Unit converter, Text viewer, Etch-A-Sketch

### Themes

- **Classic** and **Lyra** — from CrossPoint
- **Military** — inverted header bars, sharp corners, dashed separators, bracket-style button hints, uppercase labels

### SD card structure

Biscuit creates `/biscuit/` on the SD card for app data:

```
/biscuit/
  portals/     # HTML templates for captive portal
  ducky/       # DuckyScript .txt files for BadBLE
  pcap/        # Packet captures
  scans/       # Network scan results
  logs/        # BLE scan logs, WiFi logs
  drawings/    # Etch-A-Sketch saves
  creds.csv    # Captured portal credentials
```

## Installing

### Web (latest firmware)

1. Connect your Xteink X4 to your computer via USB-C and wake/unlock the device
2. Go to https://xteink.dve.al/ and click "Flash Biscuit firmware"

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Web (specific firmware version)

1. Connect your Xteink X4 to your computer via USB-C
2. Download the `firmware.bin` file from the release of your choice via the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases)
3. Go to https://xteink.dve.al/ and flash the firmware file using the "OTA fast flash controls" section

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Manual

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

```
git clone --recursive https://github.com/user/biscuit

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
pio run --target upload
```
### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```
after that run the script:
```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```
Minor adjustments may be required for Windows.

## Internals

Biscuit Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only
has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based
on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the 
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:


```
.crosspoint/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache. 

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book
file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Contributing

Contributions are very welcome!

If you are new to the codebase, start with the [contributing docs](./docs/contributing/README.md).

If you're looking for a way to help out, take a look at the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas).
If there's something there you'd like to work on, leave a comment so that we can avoid duplicated effort.

Everyone here is a volunteer, so please be respectful and patient. For more details on our goverance and community 
principles, please see [GOVERNANCE.md](GOVERNANCE.md).

### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/dithering-improvement`)
3. Make changes
4. Submit a PR

---

## Credits

Biscuit is a fork of [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader). All credit for the core e-reader firmware goes to the CrossPoint contributors.

CrossPoint itself was inspired by [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader).

Not affiliated with Xteink or any manufacturer of the X4 hardware.

