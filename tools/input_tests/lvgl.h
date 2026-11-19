#pragma once
#include <cstdint>
struct lv_display_t;
struct lv_indev_t;
enum lv_indev_state_t { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };
struct lv_indev_data_t { uint32_t key; lv_indev_state_t state; };
enum { LV_KEY_ENTER=10, LV_KEY_ESC=27, LV_KEY_BACKSPACE=8, LV_KEY_NEXT=9,
       LV_KEY_HOME=2, LV_KEY_DEL=127, LV_KEY_END=3, LV_KEY_RIGHT=19,
       LV_KEY_LEFT=20, LV_KEY_DOWN=18, LV_KEY_UP=17 };
