#pragma once
#include "lvgl.h"
#include "content.h"
#include <array>
struct UiFontEntry { int size; const lv_font_t *base; lv_font_t font; bool initialized; };
inline std::array<UiFontEntry,6> &ui_font_entries() {
  static std::array<UiFontEntry,6> entries={{{14,&lv_font_montserrat_14,{},false},{16,&lv_font_montserrat_16,{},false},{18,&lv_font_montserrat_18,{},false},{20,&lv_font_montserrat_20,{},false},{24,&lv_font_montserrat_24,{},false},{28,&lv_font_montserrat_28,{},false}}};
  return entries;
}
inline void reset_ui_font_fallbacks(){for(auto &e:ui_font_entries())if(e.initialized)e.font.fallback=nullptr;}
inline const lv_font_t *get_ui_font(int size){
  auto &entries=ui_font_entries();
  for(auto &e:entries)if(e.size==size){
    if(!e.initialized){e.font=*e.base;e.initialized=true;}
    if(!e.font.fallback)e.font.fallback=content::international_font(size);
    return &e.font;
  }
  return get_ui_font(24);
}
