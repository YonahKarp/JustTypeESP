#pragma once
#include <cstdint>
struct lv_color_t { uint32_t value; };
inline lv_color_t lv_color_hex(uint32_t value){return {value};}
inline lv_color_t lv_color_make(unsigned r,unsigned g,unsigned b){return {(r<<16)|(g<<8)|b};}
struct lv_font_t {const lv_font_t *fallback=nullptr;};
inline lv_font_t *lv_binfont_create(const char*){return nullptr;}
inline void lv_binfont_destroy(lv_font_t*){}

inline lv_font_t lv_font_montserrat_14;
inline lv_font_t lv_font_montserrat_16;
inline lv_font_t lv_font_montserrat_18;
inline lv_font_t lv_font_montserrat_20;
inline lv_font_t lv_font_montserrat_22;
inline lv_font_t lv_font_montserrat_24;
inline lv_font_t lv_font_montserrat_26;
inline lv_font_t lv_font_montserrat_30;
inline lv_font_t lv_font_montserrat_28;
