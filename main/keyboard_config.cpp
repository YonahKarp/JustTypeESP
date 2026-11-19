#include "keyboard_config.h"
#include "justtype/src/file_manager.h"
#include <algorithm>
#include <cstring>
#include <fstream>
namespace keyboard_config {
namespace {
int selected = 0;
uint32_t accent = 0;
const uint8_t defaults[] = {0x09, 0x17, 0x13, 0x04, 0x06,
                            0x1b, 0x19, 0x2e, 0x2d, 0x05};
uint8_t bindings[action_count] = {0x09, 0x17, 0x13, 0x04, 0x06,
                                  0x1b, 0x19, 0x2e, 0x2d, 0x05};
const platform::KeyCode actions[] = {
    platform::KeyCode::F, platform::KeyCode::T,      platform::KeyCode::P,
    platform::KeyCode::A, platform::KeyCode::C,      platform::KeyCode::X,
    platform::KeyCode::V, platform::KeyCode::Equals, platform::KeyCode::Minus,
    platform::KeyCode::B};
const char *names[] = {
    "Search", "Toggle theme", "QR export",   "Select all",   "Copy",
    "Cut",    "Paste",        "Larger font", "Smaller font", "Back up now"};
std::string utf8(uint32_t c) {
    std::string s;
    if (c < 128)
        s += char(c);
    else if (c < 2048) {
        s += char(0xc0 | (c >> 6));
        s += char(0x80 | (c & 63));
    } else {
        s += char(0xe0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    }
    return s;
}
// Physical HID usages in keyboard row order. Layout data follows
// xkeyboard-config.
const uint8_t keys[] = {0x35, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
                        0x26, 0x27, 0x2d, 0x2e, 0x14, 0x1a, 0x08, 0x15, 0x17,
                        0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x04, 0x16,
                        0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34,
                        0x31, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36,
                        0x37, 0x38, 0x64, 0x2c};
constexpr const char32_t *plain[] = {
    U"`1234567890-=qwertyuiop[]asdfghjkl;'\\zxcvbnm,./< ",
    U"`1234567890[]',.pyfgcrl/=aoeuidhtns-\\;qjkxbmwvz< ",
    U"`1234jlmfp/[]56q.orsuyb;=78zaehtdck-\\90x,inwvg'< ",
    U"º1234567890'¡qwertyuiop`+asdfghjklñ´çzxcvbnm,.-< ",
    U"ё1234567890-=йцукенгшщзхъфывапролджэ\\ячсмитьбю.\\ "};
constexpr const char32_t *shifted[] = {
    U"~!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"|ZXCVBNM<>?> ",
    U"~!@#$%^&*(){}\"<>PYFGCRL?+AOEUIDHTNS_|:QJKXBMWVZ> ",
    U"~!@#$JLMFP?{}%^Q>ORSUYB:+&*ZAEHTDCK_|()X<INWVG\"> ",
    U"ª!\"·$%&/()=?¿QWERTYUIOP^*ASDFGHJKLÑ¨ÇZXCVBNM;:_> ",
    U"Ё!\"№;%:?*()_+ЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭ/ЯЧСМИТЬБЮ,/ "};
static_assert(std::char_traits<char32_t>::length(plain[0]) == sizeof(keys),
              "plain row size");
static_assert(std::char_traits<char32_t>::length(shifted[0]) == sizeof(keys),
              "shift row size");
static_assert(std::char_traits<char32_t>::length(plain[1]) == sizeof(keys),
              "plain row size");
static_assert(std::char_traits<char32_t>::length(shifted[1]) == sizeof(keys),
              "shift row size");
static_assert(std::char_traits<char32_t>::length(plain[2]) == sizeof(keys),
              "plain row size");
static_assert(std::char_traits<char32_t>::length(shifted[2]) == sizeof(keys),
              "shift row size");
static_assert(std::char_traits<char32_t>::length(plain[3]) == sizeof(keys),
              "plain row size");
static_assert(std::char_traits<char32_t>::length(shifted[3]) == sizeof(keys),
              "shift row size");
static_assert(std::char_traits<char32_t>::length(plain[4]) == sizeof(keys),
              "plain row size");
static_assert(std::char_traits<char32_t>::length(shifted[4]) == sizeof(keys),
              "shift row size");
} // namespace
int layout() { return selected; }
void set_layout(int n) {
    selected = (n + layout_count) % layout_count;
    accent = 0;
}
const char *layout_name(int n) {
    static const char *v[] = {"US QWERTY", "Dvorak", "Dvorak right hand",
                              "Spanish (Spain)", "Russian"};
    return v[n];
}
const char *action_name(int n) { return names[n]; }
std::string binding_name(int n) {
    uint8_t u = bindings[n];
    return "Ctrl + " + (u >= 4 && u <= 29 ? std::string(1, char('A' + u - 4))
                        : u == 0x2d       ? std::string("-")
                                          : std::string("="));
}
bool bind(int a, uint8_t u) {
    if (a < 0 || a >= action_count ||
        !((u >= 4 && u <= 29) || u == 0x2d || u == 0x2e))
        return false;
    for (int i = 0; i < action_count; ++i)
        if (i != a && bindings[i] == u)
            return false;
    bindings[a] = u;
    return true;
}
void reset() {
    std::memcpy(bindings, defaults, sizeof(bindings));
    selected = 0;
    accent = 0;
}
platform::KeyCode shortcut(uint8_t u) {
    for (int i = 0; i < action_count; ++i)
        if (bindings[i] == u)
            return actions[i];
    return platform::KeyCode::Unknown;
}
bool save() {
    std::ofstream f("/spiffs/user_files/.keyboard.tmp");
    f << 2 << ' ' << selected;
    for (auto u : bindings)
        f << ' ' << int(u);
    f << '\n';
    f.close();
    return f && replace_saved_file("/spiffs/user_files/.keyboard.tmp",
                                   "/spiffs/user_files/.keyboard");
}
void load() {
    std::ifstream f("/spiffs/user_files/.keyboard");
    int version, n;
    int values[action_count];
    if (!(f >> version >> n) || (version != 1 && version != 2) || n < 0 ||
        n >= layout_count)
        return;
    const int stored_count = version == 1 ? 9 : action_count;
    for (int i = 0; i < stored_count; ++i)
        if (!(f >> values[i]))
            return;
    if (version == 1) {
        values[9] = 0x05;
        while (std::find(values, values + 9, values[9]) != values + 9)
            ++values[9];
    }
    reset();
    for (int i = 0; i < action_count; ++i) {
        if (!((values[i] >= 4 && values[i] <= 29) || values[i] == 0x2d ||
              values[i] == 0x2e)) {
            reset();
            return;
        }
        for (int j = 0; j < i; ++j)
            if (values[j] == values[i]) {
                reset();
                return;
            }
    }
    for (int i = 0; i < action_count; ++i)
        bindings[i] = values[i];
    selected = n;
}
bool cancel_composition() {
    bool pending = accent != 0;
    accent = 0;
    return pending;
}
std::string text(uint8_t u, bool shift, bool altgr, bool caps) {
    int index = -1;
    for (unsigned i = 0; i < sizeof(keys); ++i)
        if (keys[i] == u) {
            index = i;
            break;
        }
    if (index < 0)
        return {};
    uint32_t base = plain[selected][index];
    if (caps &&
        ((base >= 'a' && base <= 'z') || (base >= 0x430 && base <= 0x44f) ||
         base == 0x451 || base == 0xf1 || base == 0xe7))
        shift = !shift;
    uint32_t c = (shift ? shifted : plain)[selected][index];
    if (selected == 3 && altgr) {
        switch (u) {
        case 0x1e:
            c = '|';
            break;
        case 0x21:
            c = '~';
            break;
        case 0x23:
            c = 0xac;
            break;
        case 0x1f:
            c = '@';
            break;
        case 0x20:
            c = '#';
            break;
        case 0x08:
            c = 0x20ac;
            break;
        case 0x2f:
            c = '[';
            break;
        case 0x30:
            c = ']';
            break;
        case 0x34:
            c = '{';
            break;
        case 0x31:
            c = '}';
            break;
        case 0x35:
            c = '\\';
            break;
        default:
            break;
        }
    }
    if (selected == 3 && !altgr && (u == 0x34 || u == 0x2f)) {
        accent = c;
        return {};
    }
    if (accent) {
        uint32_t a = accent;
        accent = 0;
        if (c == ' ')
            return utf8(a);
        const char32_t *vowels = U"aeiouAEIOU";
        const char32_t *result = a == 0xb4   ? U"áéíóúÁÉÍÓÚ"
                                 : a == 0xa8 ? U"äëïöüÄËÏÖÜ"
                                 : a == '`'  ? U"àèìòùÀÈÌÒÙ"
                                             : U"âêîôûÂÊÎÔÛ";
        for (int i = 0; i < 10; ++i) {
            if (c == vowels[i])
                return utf8(result[i]);
        }
        return utf8(a) + utf8(c);
    }
    return utf8(c);
}
} // namespace keyboard_config
