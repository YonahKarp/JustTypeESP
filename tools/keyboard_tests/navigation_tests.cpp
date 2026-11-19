#include "justtype/src/text_navigation.h"
#include "justtype/src/utf8_positions.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace {
// Compare against the previous character-index behavior, including its precise
// newline rules, at every valid cursor boundary in mixed UTF-8 documents.
bool space(char c) { return c == ' ' || c == '\n' || c == '\t'; }
void check(const std::string& raw, const std::string& units, int32_t pos) {
    const auto len = static_cast<int32_t>(units.size());
    int32_t p = pos;
    if (p > 0) {
        --p;
        while (p > 0 && units[p] != '\n') --p;
        if (p > 0) {
            --p;
            while (p > 0 && units[p] != '\n') --p;
            if (p > 0) ++p;
        }
    }
    assert(find_paragraph_start(raw.c_str(), pos) == p);
    p = pos;
    while (p < len && units[p] != '\n') ++p;
    if (p < len) ++p;
    while (p < len && units[p] != '\n') ++p;
    assert(find_paragraph_end(raw.c_str(), len, pos) == p);
    p = pos;
    if (p > 0) {
        --p;
        while (p > 0 && space(units[p])) --p;
        while (p > 0 && !space(units[p - 1])) --p;
    }
    assert(find_prev_word_start(raw.c_str(), pos) == p);
    p = pos;
    while (p < len && space(units[p])) ++p;
    while (p < len && !space(units[p])) ++p;
    assert(find_next_word_end(raw.c_str(), len, pos) == p);
    p = pos;
    if (p > 0) {
        --p;
        while (p > 0 && units[p] != '\n') --p;
        if (units[p] == '\n') ++p;
    }
    assert(find_line_start(raw.c_str(), pos) == p);
    p = pos;
    while (p < len && units[p] != '\n') ++p;
    assert(find_line_end(raw.c_str(), len, pos) == p);
}
std::string units(const std::string& raw) {
    std::string result;
    for (unsigned char c : raw)
        if ((c & 0xc0) != 0x80) result += c < 128 ? char(c) : 'x';
    return result;
}
}
void test_navigation() {
    const std::string cases[] = {"", "a", " ", "\n", "\n\n", "a\n", "\na",
        "a  \t b\n\nlast", u8"Привет мир\nmañana café\n🙂👍 end\r\n"};
    for (const auto& raw : cases) {
        const auto decoded = units(raw);
        for (int32_t pos = 0; pos <= static_cast<int32_t>(decoded.size()); ++pos)
            check(raw, decoded, pos);
    }
    const char* tokens[] = {"a", "z", " ", "\t", "\r", "\n", u8"é", u8"я", u8"🙂"};
    uint32_t seed = 71;
    for (unsigned trial = 0; trial < 100; ++trial) {
        std::string raw;
        for (unsigned i = 0; i < 160; ++i) {
            seed = seed * 1664525u + 1013904223u;
            raw += tokens[seed % 9];
        }
        const auto decoded = units(raw);
        for (int32_t pos = 0; pos <= static_cast<int32_t>(decoded.size()); ++pos)
            check(raw, decoded, pos);
    }
    std::string large;
    while (large.size() < 500000) large += u8"A paragraph café Привет 🙂\n\n";
    const auto decoded = units(large);
    for (int32_t pos : {0, 1, 100, static_cast<int32_t>(decoded.size()) / 2,
                       static_cast<int32_t>(decoded.size()) - 1,
                       static_cast<int32_t>(decoded.size())})
        check(large, decoded, pos);
}
