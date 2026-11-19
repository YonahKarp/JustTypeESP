#pragma once
#include "lvgl.h"
LV_FONT_DECLARE(intl_14);
LV_FONT_DECLARE(intl_16);
LV_FONT_DECLARE(intl_18);
LV_FONT_DECLARE(intl_20);
LV_FONT_DECLARE(intl_22);
LV_FONT_DECLARE(intl_24);
LV_FONT_DECLARE(intl_26);
LV_FONT_DECLARE(intl_28);
LV_FONT_DECLARE(intl_30);
LV_FONT_DECLARE(intl_32);
LV_FONT_DECLARE(intl_36);
LV_FONT_DECLARE(intl_40);
inline const lv_font_t *get_ui_font(int size) {
    static const lv_font_t font_14 = [] {
        auto f = lv_font_montserrat_14;
        f.fallback = &intl_14;
        return f;
    }();
    static const lv_font_t font_16 = [] {
        auto f = lv_font_montserrat_16;
        f.fallback = &intl_16;
        return f;
    }();
    static const lv_font_t font_18 = [] {
        auto f = lv_font_montserrat_18;
        f.fallback = &intl_18;
        return f;
    }();
    static const lv_font_t font_20 = [] {
        auto f = lv_font_montserrat_20;
        f.fallback = &intl_20;
        return f;
    }();
    static const lv_font_t font_22 = [] {
        auto f = lv_font_montserrat_22;
        f.fallback = &intl_22;
        return f;
    }();
    static const lv_font_t font_24 = [] {
        auto f = lv_font_montserrat_24;
        f.fallback = &intl_24;
        return f;
    }();
    static const lv_font_t font_26 = [] {
        auto f = lv_font_montserrat_26;
        f.fallback = &intl_26;
        return f;
    }();
    static const lv_font_t font_28 = [] {
        auto f = lv_font_montserrat_28;
        f.fallback = &intl_28;
        return f;
    }();
    static const lv_font_t font_30 = [] {
        auto f = lv_font_montserrat_30;
        f.fallback = &intl_30;
        return f;
    }();
    static const lv_font_t font_32 = [] {
        auto f = lv_font_montserrat_32;
        f.fallback = &intl_32;
        return f;
    }();
    static const lv_font_t font_36 = [] {
        auto f = lv_font_montserrat_36;
        f.fallback = &intl_36;
        return f;
    }();
    static const lv_font_t font_40 = [] {
        auto f = lv_font_montserrat_40;
        f.fallback = &intl_40;
        return f;
    }();
    switch (size) {
    case 14:
        return &font_14;
    case 16:
        return &font_16;
    case 18:
        return &font_18;
    case 20:
        return &font_20;
    case 22:
        return &font_22;
    case 24:
        return &font_24;
    case 26:
        return &font_26;
    case 28:
        return &font_28;
    case 30:
        return &font_30;
    case 32:
        return &font_32;
    case 36:
        return &font_36;
    case 40:
        return &font_40;
    default:
        return &font_24;
    }
}
