#pragma once
#include "lvgl.h"
#include "src/indev/lv_indev_private.h"

inline void configure_keyboard_repeat(lv_indev_t* keyboard) {
    lv_indev_set_long_press_time(keyboard, 280);
    // LVGL 9.2 exposes no public setter for the repeat interval. Recheck this
    // private field when upgrading LVGL (the sidebar already uses its internals).
    keyboard->long_press_repeat_time = 55;
}
