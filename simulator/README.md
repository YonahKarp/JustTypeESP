# Leaf browser simulator

This builds the current Leaf C++ application and LVGL 9.2.2 to WebAssembly. JavaScript handles physical keyboard reports, mobile text input, RGB565-to-Canvas conversion and browser persistence. It does not implement an editor. The website is entirely client-side; no ESP32, backend, accounts, API keys, or network services are needed at runtime.

## Build and run

Install Python 3, CMake 3.22+, Ninja, and [Emscripten](https://emscripten.org/docs/getting_started/downloads.html). Tested with Emscripten **4.0.23**. In an activated emsdk terminal, from the repository root:

```sh
python simulator/build.py
python -m http.server 4173 --directory simulator/build/site
```

Visit **http://localhost:4173**. Use HTTP, not `file://`. The page focuses the display when ready. Click it to resume typing. The **Menu** button sends Leaf's real Escape menu action; it avoids OS-reserved Windows/Command key combinations. Use arrows and Enter to navigate, Esc to close. The **Type** button opens a mobile text entry field and the system keyboard. The display scales proportionally but always renders at 800x480.

If tools are not on PATH:

```powershell
python simulator/build.py --emsdk C:/path/to/emsdk --cmake C:/path/to/cmake.exe --ninja C:/path/to/ninja.exe
```

Equivalent manual build:

```sh
emcmake cmake -S simulator -B simulator/build -G Ninja
cmake --build simulator/build --parallel 8
```

The build uses the production `managed_components/lvgl__lvgl` source when present. Otherwise it fetches the pinned LVGL v9.2.2 tag, requiring Git and internet during configuration. `--lvgl PATH` selects another local copy. Use the same LVGL version as the firmware: sidebar snapshots access private LVGL structures. If the firmware upgrades LVGL, update the fallback tag and revalidate the simulator together. Application sources, fonts and `lv_conf.h` are read directly from `main/`; rebuilding picks up firmware changes. Brightness uses a browser adapter that dims the canvas and persists its 10–100% setting with documents; no hardware PWM is simulated. Command toggles Settings; Escape and backtick toggle the menu. Only public backup interfaces are included; device credentials and hardware network code are excluded.

## Live deployment and JustTypeWeb integration

The simulator is live at **https://justtypeleaf.com/sim**, hosted by the Vercel
`just-type-web` project from [JustTypeWeb](https://github.com/YonahKarp/JustTypeWeb).
That repository keeps a published build in `client/public/sim/`; it does not need
a symlink or an ESP/Emscripten SDK during website deployment. After rebuilding
this simulator, run from the web repository:

```sh
npm run sim:sync -- /path/to/JustTypeESP/simulator/build/site
```

The current sync script replaces only `leaf.js` and `leaf.wasm` and regenerates
manifest hashes. It preserves the web repository's `index.html`, `browser.js`,
and `style.css`. Port any browser adapter changes (such as a changed Menu key)
to its `browser.js` explicitly, keeping the page's custom controls and styling.

Review and commit the copied assets together, then push the web repository to
`main` for its Vercel production integration. Its `SIMULATOR.md` documents the
routing, checksum manifest and cache settings. The live `/sim` and `/sim/` URLs,
all five asset checksums/MIME types, missing-asset 404 behavior and full browser
regression suite were verified after deployment.

## Deploy on a static website

Upload all files in `simulator/build/site/` to any static host, for example under `/try-leaf/`. The build helper also creates `simulator/build/leaf-simulator.zip` containing these files. Serve `.wasm` as `application/wasm`, `.js` as JavaScript, and HTML/CSS normally. Keep `leaf.js` and `leaf.wasm` from the same build and deploy them together. All asset paths are relative, including the Wasm file, so subdirectory hosting works. No npm dependencies, shared memory, COOP/COEP headers or server code are required for visitors. Test a deployment with the browser regression test using `LEAF_URL`.

To embed in another page, use an iframe pointed at the simulator URL. Browser storage is per origin; third-party iframe storage restrictions can prevent persistence, so prefer a same-origin iframe or a direct page. Focus stays in the writing surface during editing; click outside it to return browser keyboard controls to the page.

**Restart** flushes the document and editor state, waits for IndexedDB, then reloads. It preserves documents. **Save locally** reports completion only after the current IndexedDB write finishes. The firmware's 500 ms document debounce remains active; the adapter synchronizes the virtual filesystem approximately once per second. Page hide/visibility change attempts a final flush, but browsers do not guarantee asynchronous saves during abrupt termination. Use Save locally before closing when the latest text matters. Clearing site data deletes all demo documents; private browsing, quota limits, or storage policies may make persistence unavailable. Storage errors are displayed; failed saving cancels Restart.

Avoid editing the same demo origin in multiple tabs: whole filesystem snapshots are not a multi-tab synchronization protocol.

## Architecture investigation and reuse

| Area | Firmware architecture | Browser implementation |
| --- | --- | --- |
| Application | `app.cpp` owns init, focus, shortcuts, selection, menu dispatch and the bounded event loop | Same source, same `init_app` / `run_app` / `shutdown_app` |
| Keyboard | BLE reports queue into `keyboard_input.cpp`; decode on UI task; `keyboard_config.cpp` supplies layouts/remapping; native LVGL keypad gets unconsumed edges | Browser `KeyboardEvent.code` becomes the same 8-byte HID reports; the real decoder and configuration are compiled unchanged |
| Editor | LVGL textarea, UTF-8 positions/navigation, selection, search, document size limits and debounced saves | Same editor, search, navigation and textarea utilities |
| Documents | C++ streams/POSIX paths under `/spiffs/user_files`, temporary saves/recovery, folders, rename, trash, state | Same paths and file manager on Emscripten's virtual filesystem, mounted to IndexedDB via IDBFS before application startup |
| UI | Editor, sidebar, file/folder dialogs, settings, themes, icons, Montserrat and international fallback fonts | All compiled directly from firmware sources; actual LVGL software rasterizer, not browser text |
| Animations | PSRAM-backed clipped menu snapshot using LVGL private APIs | Same snapshot implementation, with browser-only ESP allocation/memory helpers |
| Spellcheck | Fixed viewport checker and dictionary | Same sources and compiled dictionary |
| Word count | `std::thread` worker plus an existing synchronous fallback | Compile-time guard skips starting the thread; same debounce and counting fallback |
| Timing / loop | ESP timer/LVGL ticks and FreeRTOS queue waits | Monotonic browser clock; nonblocking bounded report queue; `pump_events()` calls `emscripten_sleep(4)` with Asyncify so the actual application loop yields, including during animations |
| Hardware services | BLE/ESP-IDF startup, LCD driver, flash mount/migration, backup worker/Wi-Fi/credentials, USB export | Excluded; browser platform initializes LVGL and service adapters explicitly report backup as unavailable |

`App` defaults to a desktop-sized window; the browser entry point sets it to **800x480 before initialization**, matching Leaf. Browser integration lives in `platform_browser.cpp`, `hardware_services.cpp`, `compat/`, `browser.js` and the page assets. The isolated `simulator/CMakeLists.txt` does not participate in the ESP-IDF build.

### Production memory and rendering

Leaf **does render full framebuffers**. `platform_esp.cpp` asks the ESP LCD RGB driver for **two PSRAM RGB565 buffers**, 800 x 480 x 2 = **768,000 bytes each**, and registers them with LVGL in DIRECT mode. LVGL renders invalidated regions into the back buffer; after the last flush the platform submits it and waits for the frame-complete sequence before permitting reuse. The LCD driver continuously copies already-rendered pixels into two internal DMA bounce buffers of **800 x 12 x 2 = 19,200 bytes each**. The bounce callback does not dynamically render editor pixels. The driver also configures alignment, ISR placement and cache behavior for flash-write reliability.

The browser uses one 768,000-byte direct RGB565 buffer in WebAssembly memory and converts the completed image to a 1,536,000-byte RGBA Canvas image. It needs no DMA, bounce buffers or scanout ownership handshake. Snapshot and spellcheck allocations use Wasm malloc through narrowly scoped ESP header shims. The shims' PSRAM capacity checks are simulator policy, not an ESP32 memory-pressure model. Initial Wasm memory is 32 MiB and may grow. The browser has no impact on firmware allocation policy.

### Existing production file changed

Only **`main/justtype/src/editor_word_count.cpp`** was changed for this task: `#ifndef __EMSCRIPTEN__` surrounds the two statements that start the worker thread. The browser therefore uses the synchronous fallback already implemented by `request_word_count`; ESP32 builds execute the original worker path. No virtual calls, callbacks, additional allocations, hardware-path changes or simulator dependencies were added to production. All pre-existing uncommitted edits were preserved. Further platform refactoring was unnecessary because the existing interface and small compatibility headers were sufficient.

## Validation

See [VALIDATION.md](VALIDATION.md) for measured production size comparisons and browser results.

The repeatable browser test requires Node and Chrome (only development dependencies):

```sh
cd simulator
npm ci
# In another terminal, serve build/site on port 4173 first.
npm test
```

`LEAF_URL` changes the URL; `LEAF_BROWSER` changes the Playwright browser channel (default `chrome`, e.g. `msedge`). Tests create an isolated browser context and do not erase your normal profile's documents. Screenshots are saved under `simulator/build/`. The exported `leaf_state`, `leaf_cursor` and `leaf_view` functions are read-only test diagnostics and are not an alternative editing API.

## Limitations and tradeoffs

- No Bluetooth pairing, electrical keyboard behavior, physical LCD timings/tearing, PSRAM/cache/DMA contention, flash wear, battery/charging, sleep, power management or trusted device RTC simulation. The battery icon is the firmware UI's indicator, not a measurement of the visitor's device.
- Wi-Fi, Google authorization/backup and USB serial document export are unavailable. Their firmware menus remain visible for fidelity, but backup status says unavailable and actions do not perform hardware/network operations.
- Physical keyboard text uses Leaf's selected HID layout (five layouts and remapping), rather than the host OS layout. The browser cannot capture every OS shortcut. Use the Menu button instead of Windows/Command. Page Up/Down, undo and other keys absent from Leaf's existing decoder are not independently invented here.
- Copy/cut/paste use Leaf's internal clipboard, as on the current firmware. They do not access the system clipboard. Mobile text input passes committed Unicode through real application TextInput handling. Text unsupported by Leaf's font coverage may show a missing glyph, as in firmware. IME and soft-keyboard event sequences differ across browsers and still need real-device testing.
- The Canvas itself is keyboard-operated, without pointer/touch hit-testing of LVGL widgets. On phones, Type enables text entry; full menu navigation needs a physical keyboard. The Canvas is not a screen-reader semantic editor.
- Single-threaded word counting runs after the existing debounce on the browser thread. Browser scheduling, full-image Canvas conversions and Asyncify overhead are simulator costs; they do not model device latency or CPU usage. Shared application behavior is prioritized over matching hardware speed.
- Files survive reload through IndexedDB, but browser storage is not archival storage. Multi-tab edits, storage eviction and abrupt process termination are not equivalent to the firmware's flash persistence guarantees.

## Content downloads

Settings > Content can refresh, install, update and remove dictionaries, fonts,
themes and packs in the browser. Asynchronous Fetch keeps progress and cancellation responsive;
Typing/paste is discarded and Settings stays open during downloads. Escape,
backtick or Cancel stops the transfer without closing Settings.
Web Crypto verifies SHA-256, then the shared C++ package manager validates and
commits the installation. Assets persist with documents in IndexedDB. The browser
approximates Leaf storage capacity, preserves a 1 MiB document reserve and checks
browser quota. HTTPS or localhost is required. Local previews need a same-origin
API proxy at /api/leaf/content/v1/. Physical Wi-Fi settings and Google Drive backup
remain hardware-only.
