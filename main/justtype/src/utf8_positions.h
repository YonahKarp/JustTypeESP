#pragma once
#include <cstdint>
inline uint32_t utf8_length(const char *s) {
    uint32_t n = 0;
    for (; *s; ++s)
        if ((static_cast<unsigned char>(*s) & 0xc0) != 0x80)
            ++n;
    return n;
}
inline uint32_t utf8_byte(const char *s, uint32_t chars) {
    uint32_t i = 0;
    while (s[i] && chars) {
        ++i;
        while (s[i] && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80)
            ++i;
        --chars;
    }
    return i;
}
