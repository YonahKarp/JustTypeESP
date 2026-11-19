#pragma once

#include "lvgl.h"

struct Theme {
    lv_color_t background;
    lv_color_t text;
    lv_color_t text_dim;
    lv_color_t edit_bg;
    lv_color_t edit_text;
    lv_color_t cursor;
    lv_color_t selection;
    
    lv_color_t sidebar_bg;
    lv_color_t sidebar_btn_normal;
    lv_color_t sidebar_btn_hover;
    lv_color_t sidebar_btn_active;
    lv_color_t sidebar_btn_text;
    lv_color_t sidebar_btn_text_hover;
    lv_color_t sidebar_selected;
    lv_color_t sidebar_new_file;
    lv_color_t sidebar_delete;
    lv_color_t sidebar_cancel;

    lv_color_t battery_border;
    lv_color_t battery_good;
    lv_color_t battery_low;
    
    lv_color_t scrollbar_bg;
    lv_color_t scrollbar_cursor;
    
    lv_color_t search_input_bg;
    lv_color_t search_input_text;
    lv_color_t search_highlight;
};

inline Theme get_dark_theme() {
    Theme t;
    t.background = lv_color_make(40, 40, 40);
    t.text = lv_color_make(200, 200, 200);
    t.text_dim = lv_color_make(120, 120, 120);
    t.edit_bg = lv_color_make(30, 30, 30);
    t.edit_text = lv_color_make(200, 200, 200);
    t.cursor = lv_color_make(220, 220, 220);
    t.selection = lv_color_make(70, 100, 150);
    
    t.sidebar_bg = lv_color_make(40, 40, 40);
    t.sidebar_btn_normal = lv_color_make(50, 50, 50);
    t.sidebar_btn_hover = lv_color_make(70, 70, 70);
    t.sidebar_btn_active = lv_color_make(90, 90, 90);
    t.sidebar_btn_text = lv_color_make(200, 200, 200);
    t.sidebar_btn_text_hover = lv_color_make(255, 255, 255);
    t.sidebar_selected = lv_color_make(80, 100, 120);
    t.sidebar_new_file = lv_color_make(60, 70, 60);
    t.sidebar_delete = lv_color_make(120, 60, 60);
    t.sidebar_cancel = lv_color_make(70, 70, 70);

    t.battery_border = lv_color_make(120, 120, 120);
    t.battery_good = lv_color_make(100, 180, 100);
    t.battery_low = lv_color_make(200, 80, 80);
    
    t.scrollbar_bg = lv_color_make(40, 40, 40);
    t.scrollbar_cursor = lv_color_make(80, 80, 80);
    
    t.search_input_bg = lv_color_make(50, 50, 50);
    t.search_input_text = lv_color_make(220, 220, 220);
    t.search_highlight = lv_color_make(255, 255, 0);
    
    return t;
}

inline Theme get_light_theme() {
    Theme t;
    t.background = lv_color_make(245, 245, 245);
    t.text = lv_color_make(40, 40, 40);
    t.text_dim = lv_color_make(120, 120, 120);
    t.edit_bg = lv_color_make(255, 255, 255);
    t.edit_text = lv_color_make(30, 30, 30);
    t.cursor = lv_color_make(200, 200, 200);
    t.selection = lv_color_make(180, 210, 255);
    
    t.sidebar_bg = lv_color_make(235, 235, 235);
    t.sidebar_btn_normal = lv_color_make(225, 225, 225);
    t.sidebar_btn_hover = lv_color_make(210, 210, 210);
    t.sidebar_btn_active = lv_color_make(195, 195, 195);
    t.sidebar_btn_text = lv_color_make(50, 50, 50);
    t.sidebar_btn_text_hover = lv_color_make(20, 20, 20);
    t.sidebar_selected = lv_color_make(180, 200, 220);
    t.sidebar_new_file = lv_color_make(200, 220, 200);
    t.sidebar_delete = lv_color_make(220, 160, 160);
    t.sidebar_cancel = lv_color_make(200, 200, 200);

    t.battery_border = lv_color_make(100, 100, 100);
    t.battery_good = lv_color_make(80, 160, 80);
    t.battery_low = lv_color_make(200, 80, 80);
    
    t.scrollbar_bg = lv_color_make(230, 230, 230);
    t.scrollbar_cursor = lv_color_make(180, 180, 180);
    
    t.search_input_bg = lv_color_make(255, 255, 255);
    t.search_input_text = lv_color_make(30, 30, 30);
    t.search_highlight = lv_color_make(255, 255, 150);
    
    return t;
}

inline const Theme& get_dark_theme_cached() {
    static const Theme theme = get_dark_theme();
    return theme;
}

inline const Theme& get_light_theme_cached() {
    static const Theme theme = get_light_theme();
    return theme;
}

// IDs 0 and 1 preserve the original light/dark preference on disk.
inline const Theme& get_theme(int id) {
    static const Theme ocean = [] { Theme t = get_dark_theme();
        t.background = lv_color_hex(0x132839); t.edit_bg = lv_color_hex(0x10202e);
        t.sidebar_bg = t.background; t.edit_text = lv_color_hex(0xd4eef0);
        t.sidebar_btn_text = t.edit_text; t.text_dim = lv_color_hex(0x89b3bd);
        t.sidebar_btn_normal = lv_color_hex(0x1c3748); t.sidebar_btn_hover = lv_color_hex(0x305c70);
        t.sidebar_selected = lv_color_hex(0x31667a); t.sidebar_new_file = lv_color_hex(0x255366);
        t.selection = lv_color_hex(0x32647a); t.search_input_bg = t.sidebar_btn_normal;
        t.search_input_text = t.edit_text; return t; }();
    static const Theme forest = [] { Theme t = get_dark_theme();
        t.background = lv_color_hex(0x25382f); t.edit_bg = lv_color_hex(0x1a2c22);
        t.sidebar_bg = t.background; t.edit_text = lv_color_hex(0xe0ead4);
        t.sidebar_btn_text = t.edit_text; t.text_dim = lv_color_hex(0xabc39f);
        t.sidebar_btn_normal = lv_color_hex(0x304838); t.sidebar_btn_hover = lv_color_hex(0x46664b);
        t.sidebar_selected = lv_color_hex(0x4a7254); t.sidebar_new_file = lv_color_hex(0x456447);
        t.selection = lv_color_hex(0x4c7054); t.search_input_bg = t.sidebar_btn_normal;
        t.search_input_text = t.edit_text; return t; }();
    static const Theme sunset = [] { Theme t = get_light_theme();
        t.background = lv_color_hex(0xf4dec9); t.edit_bg = lv_color_hex(0xfff2df);
        t.sidebar_bg = lv_color_hex(0xefd1bc); t.edit_text = lv_color_hex(0x4a3038);
        t.sidebar_btn_text = t.edit_text; t.text_dim = lv_color_hex(0x8f6664);
        t.sidebar_btn_normal = lv_color_hex(0xf6dfcd); t.sidebar_btn_hover = lv_color_hex(0xe4b69e);
        t.sidebar_selected = lv_color_hex(0xeab49e); t.sidebar_new_file = lv_color_hex(0xe4b09c);
        t.selection = lv_color_hex(0xe9bba6); t.search_input_bg = t.edit_bg;
        t.search_input_text = t.edit_text; return t; }();
    switch (id) { case 0: return get_light_theme_cached(); case 2: return ocean;
        case 3: return forest; case 4: return sunset; default: return get_dark_theme_cached(); }
}
