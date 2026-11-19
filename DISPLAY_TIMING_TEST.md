# ESP32-8048S070 timing comparison

The comparison changes only RGB signal timing. The application, BLE parameters,
double framebuffers, twelve-line bounce buffers and backlight behavior are unchanged.

| Parameter | Baseline | Comparison |
| --- | ---: | ---: |
| Requested pixel clock | 16 MHz | 14 MHz |
| Horizontal pulse / back porch / front porch | 30 / 16 / 210 | 4 / 16 / 80 |
| Vertical pulse / back porch / front porch | 13 / 10 / 22 | 4 / 4 / 22 |
| Clock idle level | Low | High |
| Data clocked out on | Falling edge | Falling edge |
| Calculated refresh | 28.86 Hz | 30.50 Hz |

Comparison values come from [LovyanGFX's board preset](https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/LGFX_Sunton_ESP32-8048S070.h)
and its Bus_RGB defaults. This is an experiment, not a confirmed fix or a panel
datasheet guarantee. The actual clock can differ from the requested clock.

Select either preset under `idf.py menuconfig` -> `Leaf display` -> `RGB LCD timing
preset`, then build and flash. Comparison is the default for this experiment.
The startup log names the preset and requested clock.

Compare cold power-on pulsing, time to settle, banding, colors, and picture
alignment. Also check typing and backup remain usable. A powered reset alone
does not reproduce the panel's cold power-on conditions.

The pre-test application binary is saved locally at
`build/display_timing_baseline/justTypeESP.bin` (ignored build output).
Switching the menuconfig preset back to Baseline also restores the original
timing values without reverting other source changes. Neither approach requires
erasing NVS or document storage.
