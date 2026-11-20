# Building for the ESP32-S3



ESP-IDF v5.5.6 and the ESP32-S3 toolchain are required. The project downloads

LVGL v9.2.2 through the ESP-IDF Component Manager during configuration. Its

configuration lives in `main/justtype/lv_conf.h`.



From PowerShell in this directory:



```powershell

. C:\Users\karpy\esp\esp-idf-v5.5.6\export.ps1

idf.py set-target esp32s3

idf.py build

idf.py -p COM3 flash monitor

```



`COM3` was the connected board's CH340 serial port on October 23, 2026; check

Device Manager if the port changes. Exit the monitor with Ctrl+]. The board

identified as an ESP32-S3 with 16 MB flash and 8 MB octal PSRAM.



The current port is configured for an 800 x 480 RGB565 display and the

partition table in `partitions.csv`. The BLE test build places the factory app

at 0x200000. The original SPIFFS partition remains at 0x180000 and is not

overwritten by `idf.py flash`. Documents now live in a separate 4 MB FAT

partition at 0x500000 so folders work. On first boot, firmware copies legacy

SPIFFS documents into FAT without deleting originals. It formats the FAT

partition only when every byte is erased; a mount failure on nonblank storage

stops startup instead of formatting user data.



## Temporary BLE keyboard test



This prototype firmware scans for BLE HID keyboards at startup and after

disconnects. Put the keyboard in Bluetooth pairing mode, then watch the serial

monitor for `ble_keyboard` discovery, connection, and pairing messages. If it

prints a six-digit pairing code, type that code on the keyboard and press Enter.

On October 23, 2026, the test keyboard advertised as `Bluetooth Keyboard`,

paired successfully, and the user verified typing, Backspace, and arrow keys

in the editor.

The ESP32-S3 cannot connect to Bluetooth Classic-only keyboards; a successful

connection to a Raspberry Pi does not establish which Bluetooth mode it uses.



The current HID decoder supports standard eight-byte keyboard reports and a

US-English key layout. Other report formats are logged but not decoded yet.

The production keyboard is planned as a 4 x 12 matrix on an MCP23017 I2C

expander. This BLE transport is for prototype testing and must be disabled in

the final hardware build, per `../PCB/FIRMWARE_REQUIREMENTS.md`.



Document saves are debounced by 500 ms. Allow a moment after typing or switching

documents before removing power. The prototype initializes its clock from

`kBaseStartupTime` in `main/platform_esp.cpp`, set to November 7, 2026 at

8:56:09 PM Eastern. It advances while powered and returns to that fixed time

on every boot. New saves receive dates in Eastern time, including daylight

saving adjustments. Replace this initialization with the battery-backed RTC

when available. Existing invalid timestamps remain unknown until a file is saved.

On startup, interrupted saves are recovered; older failed saves are restored

from `.tmp` files when the corresponding document is empty.



The RGB LCD uses PSRAM XIP together with IRAM-safe LCD/GDMA interrupts so

document writes do not suspend bounce-buffer refills. Keep

`CONFIG_LCD_RGB_ISR_IRAM_SAFE` enabled and the frame-completion callback and

its semaphore in internal RAM. Bounce mode requires 64-byte data-cache lines;

the panel uses twenty-line bounce buffers, with LCD initialization and LVGL

rendering pinned to core 1. The driver automatically recovers detected DMA

underruns; forced restart on every VSYNC is disabled because a late restart

can itself shift a frame.



## Input latency diagnostics



Input is drained in batches of up to 64 events before an LCD refresh, preserving

shortcut handling for each event. The BLE host requests a 7.5–15 ms connection

interval with zero peripheral latency; negotiated parameters are logged by

`ble_keyboard`. A keyboard may choose different parameters. `keyboard_input`

logs queue ages above 100 ms (at most once per second) and queue overflow.

The report queue holds 128 snapshots; overflow drops the oldest snapshot to

preserve the latest held/released state. Overflow can still lose characters.

Verify rapid typing, modifier shortcuts, Backspace, and disconnect while holding

a key on hardware, including during autosave and QR export.



Failed saves now block document switching. Files above 524287 bytes are rejected

before reading, preserving the active document. Age-based trash cleanup is

disabled until a trusted clock is implemented.



## Download documents over USB



Connect the existing CH340 USB port and double-click `Download Documents.cmd`.

Choose the serial port (normally detected automatically), click **Download

documents…**, and choose a destination folder. The utility creates a fresh

`JustType-<date>-<time>-<id>` backup folder and opens it when finished.

Text files and their folder structure are copied; hidden state, temporary files,

and recently deleted files are excluded. No files are removed from the device.

The editor saves pending changes, pauses editing during transfer, and resumes

when export completes. Do not type or unplug during export. Transfers use

115200 baud (roughly 5 KB/s of document content). Close other serial monitors.

The utility verifies each file's size, sequence and CRC32 before publishing it;

incomplete files are removed. Files verified before a later error remain saved.



CLI equivalent (ESP-IDF Python environment includes pyserial):



```powershell

python tools/download_documents.py --port COM6 --output C:/Backups

```



The CH340 connection remains a serial device; USB mass-storage support requires

a native USB connection. The downloader works with the current hardware.


## Keyboard preferences

Ctrl+N creates and opens a blank document in the main documents folder. Names
start at `New File.txt`, then `New File 1.txt`, `New File 2.txt`, and so on,
without replacing an existing file. Pending edits are saved before switching.

Ctrl+O opens the file picker. Files appear newest modified first; typing filters
their file and folder names, including nested folders. Up/Down selects a result,
Enter opens it, and Escape or backtick closes the panel. Hidden files and recently
deleted files are excluded. Both shortcuts can be remapped in Keyboard settings;
older custom shortcuts are preserved when loading existing preferences.

Run `node simulator/quick_open_test.cjs` against the simulator on port 4175
(`LEAF_URL` overrides it) to check creation, filtering, save protection and
shortcut-preference migration.

Settings has Appearance and Keyboard tabs. Tab switches tabs; Up/Down selects a
row. Left/Right changes the layout or shortcut action. Layouts: US QWERTY,
Dvorak, Dvorak right hand, Spanish (Spain), and Russian. Shortcuts use physical
US key positions independently of the text layout. Enter on the binding row
captures an unused Ctrl + letter, minus, or equals. Escape or backtick closes Settings, including during capture.
Search, theme toggle, QR export, select-all, copy/cut/paste, and font-size
shortcuts can be remapped. Navigation and the following interface keys stay fixed:
Escape and backtick/tilde open or close the sidebar, dismissing an active dialog
or search first. Command/Meta toggles Settings directly, including closing its
nested forms. Escape/backtick/tilde
continue to close Settings and its subdialogs, without also opening the sidebar.

Settings -> Appearance -> Brightness changes the backlight in 10% steps with
Left/Right (Enter increases). Option/Alt + minus lowers brightness; Option/Alt +
equals raises it, including while Settings or Wi-Fi configuration is open.
Brightness is limited to 10-100%, defaults to 100%, and is saved in ordinary NVS
after 750 ms without another adjustment. Writes wait until a sidebar slide ends
and retry after failures. This preference does not change LCD refresh timing.
GPIO 2 uses hardware LEDC PWM at 300 Hz with 10-bit resolution and the crystal
clock; 100% is full duty. The board blanked below 80% at 20 kHz and at 20%
with 1.2 kHz, so lower-frequency pulses are used for its backlight enable. The PWM update resets the old brightness preference
once to 100%; subsequent adjustments persist normally. PWM setup failure falls back to full brightness.

`tools\backlight_tests\run.cmd` checks bounds, shortcut text consumption,
debounced persistence/reboot loading, failed-write retries and hardware fallback.
Reset keyboard defaults restores QWERTY and default bindings. Preferences save
to `.keyboard`; save failures appear in the modal. Caps Lock and Spanish acute,
grave, circumflex and diaeresis composition are supported. Right Alt supplies
Spanish AltGr symbols. Latin-1 and Cyrillic glyph fallbacks cover all editor
sizes. Non-ASCII search is exact-case. Application labels remain English.
CJK is deferred.

Noto Sans font sources: https://github.com/notofonts/noto-fonts
Font license: main/justtype/src/fonts/OFL.txt
Layout reference: https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/tree/master/symbols

## Minimal English spellcheck

English spellcheck runs automatically. Put the cursor on or immediately after
an unrecognized word and press Ctrl+Space to see up to two suggested corrections,
Add to dictionary, and Cancel. Use Up/Down and Enter to choose; Escape closes.
Suggestions are calculated incrementally for the word at the cursor and reused
as the cursor moves within it. The personal dictionary holds up to 128 words;
save failures stay visible for retry.
Settings contains Appearance, Keyboard and Backup tabs. After a 600 ms pause in editing or scrolling, visible words are checked
incrementally and unrecognized words receive dotted underlines. Moving the
cursor within the same viewport reuses cached results. Adding a word accepts it and refreshes the visible underlines. Search and selection keep their
own highlighting. There are no correction suggestions.

Each scan update processes at most 96 glyphs and 8 bounded line-layout calls,
with a cooperative 2 ms deadline checked between operations. The scanner uses
LVGL's line-breaking routine on at most 2048 bytes per call. Two fixed viewport snapshots (about 61 KiB total) are allocated in PSRAM,
plus 64 small cached dictionary results. A completed snapshot stays visible
until its replacement is ready, so typing does not clear every underline.
Only edited words are invalidated; word offsets after edits are adjusted.
Layout refreshes while typing reuse known results, and new dictionary checks
still wait for the idle pause. Rendering only draws
cached segments; it never checks dictionary entries or lays out text. A bounded
checkpoint ring accelerates repeat scrolling. Edits invalidate the line index;
reaching a distant viewport after an edit can take several updates, while input
continues. Cache limits are 384 visible word entries and 512 underline fragments.
The same 101,038-byte lexicon is reused; no extra dictionary data is added.

English checks fold case and normalize curly apostrophes. Non-English tokens,
numbers, and words above 32 letters are skipped. Proper names can be added to the
personal dictionary. Non-English spellchecking is not included.

The built-in 49,660-word American-English lexicon occupies 101,038 bytes in its
serialized form (under 100 KiB). The source word lists and offline build tooling
are in tools/spellcheck; licenses accompany the generated lexicon. Personal words
and the preference are saved atomically in .spelling, outside ordinary documents.

## Google Drive backup

Open the sidebar gear and select Backup to configure Wi-Fi, link Google from
your phone, and enable automatic backup. Ctrl+B runs Back up now; the shortcut
can be remapped on the Keyboard tab. See [BACKUP_SETUP.md](BACKUP_SETUP.md) for
Google client setup, secure credential persistence, scheduling and validation.
On the current prototype, Wi-Fi credentials and Google refresh tokens persist
in ordinary NVS and survive restarts. The same storage APIs support provisioned
encrypted NVS for production; see the setup guide before that transition.
Normal backups use persistent per-file dirty tracking and upload only changed
content, streamed from a temporary on-flash snapshot in bounded buffers.
Verify backup copies explicitly checks all Drive copies. New edits made while
an older version uploads remain dirty for the next backup.
While backup or verification is running, the top bar replaces the word count
with a progress bar. The current word count returns when the operation finishes,
fails, or pauses for retry.


## Sidebar animation timing

The ESP32 serial log emits one `ui_animation` summary per sidebar slide, after
the final framebuffer handoff. It reports presentations, elapsed time (including
menu preparation when opening), first presentation, average refresh work and LCD
wait, and the largest gap between presentations. Refresh work includes layout,
drawing and buffer synchronization; it is wall time excluding the LCD wait,
not an exclusive CPU-time measurement. No document content is logged.

Idle and key-release passes do not rebuild the sidebar display list. Slides use
an RGB565 snapshot of only the 280 x 480 menu: 268,800 bytes (262.5 KiB) of
temporary PSRAM, freed at completion. The clipping parent is transparent so the
editor and its document-scroll animation remain live behind the menu. No editor
or background snapshot is captured or displayed. Allocation requires
another 512 KiB of PSRAM headroom; otherwise the live-widget animation is used.
The clipped capture follows LVGL 9.2's internal snapshot draw path and must be
reviewed when upgrading LVGL.

Keyboard reports stay queued and undecoded during a slide, preserving event and
modifier order. Native LVGL key repeat is disabled during that pause. At 16
pending reports the slide ends early, well before the 128-report capacity. A
completion watchdog also restores live widgets if an animation is interrupted.
Widget updates and USB UI commands wait until queued input can resume, while
backup networking and bounded storage servicing continue. The slide duration
remains 150 ms; CPU frequency and LCD timing are unchanged.

Run `tools\input_tests\run.cmd` for buffered HID ordering/release tests, in
addition to the existing keyboard and backup tests. Hardware checks should
include opening/closing repeatedly, typing during a closing slide, modifier
release, navigation, and stable free-PSRAM values after each backing is released.

The layout/copy figure separates work before drawing from the total refresh work.

The animation clock starts after the initial menu scene is presented.
Focus/cursor style changes wait until the slide is complete. Backing is retained
until the exact cached endpoint has been presented before handing back to live
widgets, avoiding a delayed final movement during the heavier live redraw.
