# Validation — 19 November 2026

Validated on Windows with ESP-IDF **v5.5.6**, Xtensa GCC **14.2.0 / esp-14.2.0_20260121**, Emscripten **4.0.23**, CMake **3.30.2**, Ninja **1.12.1**, and headless installed Google Chrome via Playwright **1.65.1**.

## Production baseline and comparison

The checkout already contained substantial uncommitted firmware work. The baseline is that working tree, not repository HEAD. A full ESP32-S3 baseline build succeeded before simulator validation. For a controlled comparison, the original word-count file and the version with the Emscripten guard were separately rebuilt with the same SDK/configuration. The final source and ESP32 build contain the guard.

Both `idf.py build size` runs passed, including the partition size check. Browser sources and compatibility headers are absent from the production build.

| Measurement | Baseline | With simulator change | Delta |
| --- | ---: | ---: | ---: |
| Application `.bin` | 2,997,888 B | 2,997,888 B | 0 |
| IDF flash code | 1,655,298 B | 1,655,298 B | 0 |
| IDF flash data | 1,207,804 B | 1,207,804 B | 0 |
| IDF DIRAM used | 163,767 B | 163,767 B | 0 |
| IDF DIRAM `.data` | 24,956 B | 24,956 B | 0 |
| IDF DIRAM `.bss` | 45,520 B | 45,520 B | 0 |
| IDF IRAM used | 16,384 B | 16,384 B | 0 |
| GNU size `text` | 1,778,769 B | 1,778,769 B | 0 |
| GNU size `data` | 1,232,536 B | 1,232,536 B | 0 |
| GNU size `bss` | 4,792,157 B | 4,792,157 B | 0 |

GNU size uses different section groupings from the IDF report; its BSS total is not an internal-RAM heap measurement. No dynamic heap usage was measured.

All nonempty executable ELF sections (`SHF_EXECINSTR`) were also extracted with pyelftools and compared using SHA-256. **Every executable section is byte-identical**:

| Section | SHA-256, identical in both builds |
| --- | --- |
| `.iram0.vectors` | `3f8b211389fc65aba0dc4311490c5a478653f9bb719b1056540e6e0110b0b185` |
| `.iram0.text` | `35e202665fc48ef8a4ea9ccd263527b0d1b31b22ba60ae6c24d065ba967f545c` |
| `.flash.text` | `1b4cf609224b25d1c8e51ae8dfa8a784c8da64cf26a7ed8e4763684954741f70` |

The complete `.bin` hashes differ between comparison builds. Executable sections and sizes were compared separately; a whole-image byte-for-byte claim is not made. Build metadata/debug-derived image identity can differ on rebuild.

The production display backend, PSRAM/DMA allocations, bounce buffers, frame ownership waits, startup, BLE, backup, USB export, file persistence and power paths were not edited. The sole shared-code change excludes worker startup only when `__EMSCRIPTEN__` is defined, leaving the production worker and fallback code intact.

**Unverified without hardware:** LCD refresh timing, animation latency, input latency under flash/network load, tearing, DMA/cache contention, runtime heap/high-water marks, battery/power/sleep behavior, physical storage reliability and long-duration stability. Equal sizes and executable bytes support production neutrality, but do not constitute physical-device performance measurements.

## Browser validation

The Wasm build and the `build.py` packaging helper both succeed. The build uses the local firmware LVGL 9.2.2 source. The fallback Git download path is provided but was not independently exercised.

The automated browser regression test uses real browser keyboard events through the production HID decoder and real LVGL UI, not JavaScript editor mutations. Read-only exported diagnostics inspect document text/path, cursor and current view; virtual filesystem reads verify persisted results.

Validated:

- Initial startup without hardware; correct 800x480 Canvas rendering.
- HID typing, Shift uppercase, backspace, forward delete, Home/End and Ctrl+word navigation.
- Ctrl+A replacement, Shift+arrow selection/deletion and the firmware clipboard's copy/paste.
- Find entry, navigation and close without changing document text.
- Animated menu changes the rendered pixels; keyboard navigation and real new-file dialog create a second document.
- Both documents persist after reload. Appearance settings changed in the firmware settings UI also persist.
- Save status waits for IndexedDB completion; Restart saves and reloads the latest document.
- Mobile-size layout at 390px fits without horizontal overflow. Native InputEvent text and backspace preserve UTF-8, including accented text and a supplementary-plane codepoint.
- Composition commit handling inserts text once when an engine follows compositionend with a final input event.
- Rename, trash/delete, new-folder creation, and folder persistence through real firmware dialogs.
- Browser-storage-denied fallback: typing remains possible, and Restart is cancelled when persistence cannot succeed.
- No uncaught page exceptions in the main regression flow.

Screenshots of desktop, mobile and settings views were rendered and visually inspected. Real iOS/Android soft keyboards, Safari, Firefox, assistive technology and physical keyboard layouts beyond the default have not been device-tested. Synthetic mobile input events validate the bridge, not every mobile browser's IME event ordering.

## Existing host regression suites

Passed:

```powershell
tools\input_tests\run.cmd
tools\keyboard_tests\run.cmd
```

Reported buffered HID order/pressure/releases/disconnect success, keyboard/UTF-8 success, spellcheck viewport success (31,176-byte fixed cache), and backup policy success. These are host checks; physical-device flash/network behavior was not tested.

## Reproduce

Follow [README.md](README.md) to build and serve the simulator, then run `npm ci && npm test` from `simulator/`. Build firmware with your normal activated ESP-IDF environment using `idf.py build size`. For binary comparisons, retain the same SDK, `sdkconfig`, dependency versions and dirty working-tree baseline.

Local build logs and machine-readable comparison JSON are retained under ignored `simulator/*.log` and `simulator/build/`; the measured summary above is tracked independently of generated files. The static deployment ZIP is `simulator/build/leaf-simulator.zip`.

## Published website verification

Deployed to `https://justtypeleaf.com/sim` through JustTypeWeb commit
`5a2cfeb20777f62616f10aacd6c9890b8012bc18`, Vercel production deployment
`dpl_Ebjy9YDbRQrdLu3Gc2wEaDeahsuC` (READY). The full browser regression suite passed
against the live URL, including denied-storage fallback. Both `/sim` and `/sim/`
return the simulator; each of the five served assets matches the published
SHA-256 manifest, and Wasm has `application/wasm` MIME type. Assets revalidate
with `public, max-age=0, must-revalidate`. A nonexistent `/sim/*.wasm` returns 404;
the homepage and `/updates` still return 200.
