# LCD stress test

Standalone ESP-IDF 5.5.6 firmware for the existing 800x480 RGB565 prototype. It uses the application's ten-line bounce buffer only, with requested clocks of 16, 20, 22, 25, 27 and 30 MHz. Each clock runs for about 10 seconds without writes, then about 10 seconds with 16 KiB writes and fsync every five seconds. It repeats indefinitely; a full pass takes a little over two minutes. The display overlay shows the requested pixel clock and DMA mode, VSYNC-measured refresh, estimated load on each CPU core, free PSRAM, and the save state. Free PSRAM is also included in every serial RESULT line. SAVING appears shortly before each write and remains visible briefly after it finishes. Watch the remaining colour bars and checkerboard for flicker, shifts and corruption and associate observations with the overlay and serial results. This is an accelerated flash-wear workload: stop it when the experiment is complete.

VSYNC counts are measured over each phase; effective pixel clock is inferred from refresh and the configured 1056 x 525 timing. This does not prove pixel integrity. ESP-IDF exposes no reliable underrun count in this driver, so underruns are reported as unknown. The overlay CPU load is an estimate derived from reduced background-loop throughput, clamped to 0-100%; it is not a system CPU utilization counter. Core headroom is relative throughput of yielding low-priority synthetic loops against an initial five-second baseline, not a measurement of actual LVGL or keyboard performance. The screen pattern is static; compare modes and flash phases, but this is not a PSRAM bandwidth benchmark under realistic rendering traffic. Automatic long-term glitch detection requires display capture or external instrumentation.

The benchmark mounts the existing `documents` FAT partition with formatting disabled and creates a randomly named `/stress/Sxxxxxxx.TMP` exclusively. An existing file or mount failure disables flash testing rather than modifying existing data. The scratch file remains after reset; remove the printed `Sxxxxxxx.TMP` file from the documents filesystem when finished. The test must not run while another host has the filesystem mounted.

Build and monitor (adjust port):

```powershell
. C:\Users\karpy\esp\esp-idf-v5.5.6\export.ps1
idf.py build
idf.py -p COM3 app-flash monitor
```

Only use app-flash after checking the device partition table matches `partitions.csv`. To restore the saved original factory partition for this device:

```powershell
python -m esptool --chip esp32s3 --port COM3 write_flash 0x200000 ..\..\device-backups\lcd-stress-original-factory.bin
```


