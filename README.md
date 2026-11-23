# JustType Leaf — Firmware

Firmware for **JustType Leaf**, a distraction-free writing device: a plain-text
editor with folders, search, spellcheck, themes, and Google Drive backup,
running on an ESP32-S3 with an 800×480 RGB display and an LVGL interface.

Live product site: [justtypeleaf.com](https://justtypeleaf.com) — try the
firmware in your browser at [justtypeleaf.com/sim](https://justtypeleaf.com/sim)
(see [Simulator](#simulator-browser-build), below).

> Status: prototype firmware. The hardware is still being developed (see
> [Hardware](#hardware)), and some behaviours are explicitly prototype-only —
> notably BLE keyboard input and unencrypted credential storage. See
> [BACKUP_SETUP.md](BACKUP_SETUP.md) and [HOWTO.md](HOWTO.md) for the details.

## Hardware

- **SoC:** ESP32-S3 (the prototype board identified as 16 MB flash, 8 MB octal
  PSRAM — see [HOWTO.md](HOWTO.md))
- **Display:** 800×480 RGB565 LCD, 16-bit parallel RGB interface, double
  framebuffer in PSRAM, 12-line bounce buffers. LVGL rendering and LCD
  initialisation run pinned to core 1. The timing preset is the
  ESP32-8048S070 board: `idf.py menuconfig` → **Leaf display** →
  **RGB LCD timing preset** selects Baseline (16 MHz) or Comparison
  (14 MHz, LovyanGFX Sunton preset — the default). See
  [DISPLAY_TIMING_TEST.md](DISPLAY_TIMING_TEST.md).
- **Backlight:** LEDC PWM, adjustable in 10% steps (see
  [Shortcuts](#keyboard-shortcuts)); defaults to 100%.
- **Input (prototype):** a BLE HID keyboard — the firmware scans for BLE HID
  keyboards at startup and after disconnects; if a six-digit pairing code is
  printed on the serial monitor, type it on the keyboard and press Enter.
  Only standard 8-byte HID reports with a US-English layout are decoded.
  Bluetooth Classic-only keyboards are not supported. The production keyboard
  is planned as a 4×12 matrix on an MCP23017 I2C expander, and the BLE
  transport is intended to be disabled in the final hardware build.
- **Storage:** documents live in FAT partition (`documents`, at
  0x500000); a legacy SPIFFS partition (0x180000) is kept for migration —
  on first boot the firmware copies legacy SPIFFS documents into FAT without
  deleting the originals, and only formats the FAT partition if every byte is
  erased. A mount failure on non-blank storage stops startup rather than
  formatting user data. See `partitions.csv`.

## Features

- **Editor:** LVGL text editor for plain-text (`.txt`) documents, up to
  512 KiB of text per document (files over 524,287 bytes are rejected on
  open). Autosave is debounced by 500 ms — allow a moment after typing or
  switching documents before removing power. Interrupted saves are recovered
  from `.tmp` files.
- **Documents & folders:** a 280 px sidebar (150 ms slide) lists documents
  and folders, newest modified first. Create, rename, move, and delete files
  and folders; deleted items go to `recently_deleted` and can be restored.
  A word count and battery indicator sit in the status bar.
- **Quick Open (Ctrl+O):** type to filter file and folder names (including
  nested folders); Up/Down selects, Enter opens, Esc or backtick closes.
  **Ctrl+N** creates and opens a blank document (`New File.txt`,
  `New File 1.txt`, …).
- **Search (Ctrl+F):** in-document search with match navigation; search and
  selection keep their own highlighting.
- **Spellcheck:** automatic English spellcheck with a built-in 49,660-word
  American-English lexicon (~101 KiB). After a 600 ms pause, unrecognised
  words in view get dotted underlines. **Ctrl+Space** on a word shows up to
  two suggested corrections, **Add to dictionary**, and Cancel. The personal
  dictionary holds up to 128 words (saved in `.spelling`). Non-English
  spellchecking is not included; CJK input is deferred.
- **Appearance:** multiple themes and font sizes (18–40 pt, default 24 pt), 
  in Settings → Appearance. Font sizes use Montserrat plus Noto Sans fallbacks
  for Latin-1/Cyrillic.
- **Keyboard layouts & remapping:** US QWERTY, Dvorak, Dvorak right hand,
  Spanish (Spain), and Russian. Shortcuts use physical US key positions
  independently of the text layout. Twelve Ctrl shortcuts can be rebound in
  Settings → Keyboard (capture an unused Ctrl + letter, `-`, or `=`); older
  custom shortcuts are preserved when loading existing preferences.
  Preferences are stored in `.keyboard`. Caps Lock and Spanish accent
  composition are supported; Right Alt supplies Spanish AltGr symbols.
- **Google Drive backup:** one-way backup of saved `.txt` documents to a
  dedicated `Leaf Backup - XXXXXX` folder in Google Drive, preserving
  subfolders. Drive content never replaces a local document. Automatic
  backup checks every 30 minutes while awake (after 30 s without keyboard
  activity) and uploads only changed files, tracked with persistent
  per-file dirty records. **Ctrl+B** runs Back up now; **Verify backup
  copies** does a full check against Drive. Setup — Google OAuth client,
  Wi-Fi, and phone linking — is in [BACKUP_SETUP.md](BACKUP_SETUP.md).
- **USB document download:** connect the CH340 USB serial port and run
  [`Download Documents.cmd`](Download%20Documents.cmd) (or
  `python tools/download_documents.py --port COM6 --output C:/Backups`).
  Documents are copied with folder structure into a fresh
  `JustType-<date>-<time>-<id>` folder, each file verified by size, sequence
  and CRC32, at 115200 baud (~5 KB/s). Nothing is removed from the device.

## Keyboard shortcuts

Defaults — the twelve `Ctrl+…` actions can be rebound in Settings → Keyboard.

| Shortcut | Action |
| --- | --- |
| Ctrl+N | New document |
| Ctrl+O | Quick Open (file picker) |
| Ctrl+F | Find in document |
| Ctrl+A | Select all |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste |
| Ctrl+P | QR export |
| Ctrl+B | Back up now |
| Ctrl+T | Toggle theme |
| Ctrl+= / Ctrl+− | Larger / smaller font |
| Ctrl+Space | Spelling suggestions for the word at the cursor |
| Ctrl+Backspace | Delete previous word |
| Ctrl+← / → | Move by word |
| Ctrl+↑ / ↓ | Previous / next paragraph |
| Ctrl+Shift+↑ / ↓ / ← / → | Extend selection by paragraph / word |
| Alt+← / → | Start / end of line |
| Alt+↑ / ↓ | Top / bottom of document |
| Shift+← / → / ↑ / ↓ | Extend selection |
| Home / End | Start / end of line |
| Alt+− / Alt+= | Lower / raise brightness (10% steps, 10–100%) |
| Cmd/Meta | Open Settings |
| Esc or \` | Close sidebar / dialog / search — or open the sidebar if nothing is open |
| Tab (in Settings) | Switch settings tabs |

## Repo layout

```
├── CMakeLists.txt            ESP-IDF project (project justTypeESP, MINIMAL_BUILD)
├── partitions.csv            nvs / phy_init / spiffs (legacy) / factory / documents (FAT)
├── dependencies.lock         LVGL 9.2.2 via the ESP-IDF Component Manager
├── sdkconfig.ci              (empty placeholder)
├── HOWTO.md                  Build/flash, BLE test, display, input latency, backup notes
├── BACKUP_SETUP.md           Google Drive backup: OAuth client, Wi-Fi, linking, scheduling
├── DISPLAY_TIMING_TEST.md    Baseline vs comparison LCD timing experiment
├── Download Documents.cmd    USB serial document download (Windows launcher)
├── pytest_hello_world.py     Leftover ESP-IDF hello_world test — does not match this firmware
├── main/
│   ├── CMakeLists.txt        Component sources, LVGL config include, backup_secrets handling
│   ├── Kconfig.projbuild     "Leaf display" LCD timing preset choice
│   ├── idf_component.yml     lvgl/lvgl == 9.2.2
│   ├── esp_port_main.cpp     app_main: keyboard queue, BLE start, app task on core 1
│   ├── platform_esp.cpp      RGB LCD init/timing, SPIFFS→FAT migration, storage mount
│   ├── backlight.cpp/.h      LEDC PWM backlight
│   ├── ble_keyboard.cpp/.h   Temporary BLE HID keyboard transport (prototype)
│   ├── keyboard_input.cpp/.h Keyboard event queue (128 snapshots), LVGL forwarding
│   ├── keyboard_config.cpp/.h Layouts, 12 remappable shortcuts, .keyboard prefs
│   ├── serial_export.cpp     @JT-framed USB serial document export
│   ├── hello_world_main.c    Leftover template file — not built (not in CMakeLists)
│   ├── justtype/             The editor application (LVGL UI, C++17)
│   │   ├── lv_conf.h         LVGL config: 800×480, 16-bit colour, QR code enabled
│   │   ├── platforms/platform.h  Key/event abstraction shared by device + simulator
│   │   └── src/              app, editor, sidebar (+ dialogs/snapshot), search,
│   │                         settings, spellcheck (+ spelling/ lexicon), quick_open,
│   │                         qr_export, file_manager, text_navigation, themes, fonts
│   └── backup/               Google Drive backup: backup.cpp, storage, http_transport,
│       │                     dirty_tracker, file_snapshot, policy; backup_secrets.example.h
│       └── backup_secrets.h (local, git-ignored — copy from the .example file)
├── simulator/                Same firmware sources compiled to WebAssembly (Emscripten)
│   ├── build.py              emcmake build → build/site + leaf-simulator.zip
│   ├── CMakeLists.txt        WASM build; compiles main/justtype sources in place
│   ├── index.html, browser.js, style.css   Browser shell (800×480 canvas, IndexedDB)
│   ├── platform_browser.cpp, backlight_browser.cpp, hardware_services.cpp
│   ├── compat/               Browser stubs for ESP-IDF/FreeRTOS headers
│   ├── test.cjs (+ *_test.cjs)  Playwright browser regression tests
│   └── VALIDATION.md         Build/size comparison and browser validation notes
├── lcd_stress/               Standalone ESP-IDF app: LCD pixel-clock stress test
│                             (16–30 MHz) with flash-write phases; see its README
└── tools/                    Host test runners (input_tests, keyboard_tests,
                              backup_tests, backlight_tests), spellcheck tooling,
                              download_documents.py
```

## Building and flashing (ESP32-S3)

Requires **ESP-IDF v5.5.6** and the ESP32-S3 toolchain. LVGL v9.2.2 is
downloaded by the ESP-IDF Component Manager during configuration; its
configuration lives in `main/justtype/lv_conf.h`.

From PowerShell in this directory (adjust the ESP-IDF path and COM port):

```powershell
. C:\Users\karpy\esp\esp-idf-v5.5.6\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

`COM3` was the connected board's CH340 serial port on 2026-10-23; check Device
Manager if the port changes. Exit the monitor with Ctrl+]. The factory app is
flashed at 0x200000; `idf.py flash` does not overwrite the SPIFFS partition at
0x180000 or the documents FAT partition at 0x500000.

LCD timing preset: `idf.py menuconfig` → **Leaf display** → **RGB LCD timing
preset** (Baseline 16 MHz / Comparison 14 MHz, default Comparison).

## Simulator (browser build)

The simulator compiles the **same firmware sources in place** (no copied UI
code) to WebAssembly with Emscripten, so the browser demo at
[justtypeleaf.com/sim](https://justtypeleaf.com/sim) is the real editor.
Documents persist in the browser via IndexedDB. Wi-Fi, Google backup,
Bluetooth, USB, and battery hardware are unavailable in the simulator.

```powershell
python simulator/build.py        # requires an activated emsdk (emcmake on PATH)
python -m http.server 4173 --directory simulator/build/site
```

Then open http://localhost:4173. The build also produces
`simulator/build/leaf-simulator.zip` (static site). Run the browser
regression tests with:

```powershell
cd simulator
npm ci
npm test
```

Host test suites live under `tools/` (`input_tests`, `keyboard_tests`,
`backup_tests`, `backlight_tests` — run via their `run.cmd` scripts).
Validation measurements and what was (and was not) verified on hardware are
recorded in [simulator/VALIDATION.md](simulator/VALIDATION.md).

## Configuration

- **LCD timing:** `idf.py menuconfig` → Leaf display (see
  [DISPLAY_TIMING_TEST.md](DISPLAY_TIMING_TEST.md)).
- **Keyboard layouts & shortcuts:** on-device, Settings → Keyboard; stored in
  `.keyboard` in the documents filesystem.
- **Themes / font size:** on-device, Settings → Appearance.
- **Google Drive backup:** copy `main/backup/backup_secrets.example.h` to
  `main/backup/backup_secrets.h` (git-ignored) and fill in
  `LEAF_GOOGLE_CLIENT_ID` / `LEAF_GOOGLE_CLIENT_SECRET` from a Google OAuth
  client of type **TVs and Limited Input devices**, then `idf.py reconfigure`
  and rebuild. Building without the local header is supported — Backup
  settings will explain that OAuth setup is missing. Full setup, linking,
  scheduling, and the prototype credential-storage caveats are in
  [BACKUP_SETUP.md](BACKUP_SETUP.md).
- **Clock:** the prototype initialises its clock from a fixed startup time in
  `main/platform_esp.cpp` (`kBaseStartupTime`) and returns to it on every
  boot; it is meant to be replaced with a battery-backed RTC. New saves get
  dates in Eastern time.

## Troubleshooting

- **Upload failure:** check the serial connection and port
  (`idf.py -p PORT monitor`, reboot the board and watch the log); lower the
  download baud rate in `menuconfig` if needed.
- **Keyboard won't connect:** put the keyboard in Bluetooth pairing mode and
  watch the monitor for `ble_keyboard` discovery/connection messages; if a
  six-digit code is printed, type it on the keyboard and press Enter.
  Bluetooth Classic-only keyboards cannot connect.
- **Documents partition won't mount:** a mount failure on non-blank storage
  stops startup instead of formatting — do not force a format; check the
  serial log.
- **Backup fails:** see the scheduling, retry, and validation sections of
  [BACKUP_SETUP.md](BACKUP_SETUP.md); note that credentials are stored
  unencrypted on the prototype and that Google testing-audience accounts may
  need reauthorisation.
- **Display timing issues:** try the other preset under
  `idf.py menuconfig` → Leaf display, and compare cold power-on behaviour
  (a powered reset alone does not reproduce cold power-on conditions).
  See [DISPLAY_TIMING_TEST.md](DISPLAY_TIMING_TEST.md).

## Links

- Product site: <https://justtypeleaf.com>
- Browser simulator: <https://justtypeleaf.com/sim>
- Hardware (PCB): [YonahKarp/JustTypePCB](https://github.com/YonahKarp/JustTypePCB)
