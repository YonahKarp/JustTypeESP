// Browser implementation of the hardware backlight service. Shared UI stays unchanged.
#include "backlight.h"
#include <emscripten.h>
#include <algorithm>
#include <cstdio>

namespace backlight {
namespace {
int current = 100;
bool pending = false;
double save_at = 0;
constexpr const char* path = "/spiffs/.brightness";
void present() {
    EM_ASM({ document.getElementById('screen').style.filter = 'brightness(' + ($0 / 100) + ')'; }, current);
}
void persist() {
    FILE* file = std::fopen("/spiffs/.brightness.tmp", "w");
    bool ok = false;
    if (file) {
        const bool written = std::fprintf(file, "%d\n", current) > 0;
        const bool closed = std::fclose(file) == 0;
        ok = written && closed && std::rename("/spiffs/.brightness.tmp", path) == 0;
    }
    pending = !ok;
    if (!ok) save_at = emscripten_get_now() + 5000;
}
}
bool init(int) {
    current = 100;
    pending = false;
    if (FILE* file = std::fopen(path, "r")) {
        int value;
        if (std::fscanf(file, "%d", &value) == 1 && value >= 10 && value <= 100) current = value;
        std::fclose(file);
    }
    present();
    return true;
}
int percent() { return current; }
bool adjust(int delta) {
    const int value = std::clamp(current + delta, 10, 100);
    if (value == current) return true;
    current = value;
    pending = true;
    save_at = emscripten_get_now() + 750;
    present();
    return true;
}
bool handle_event(const platform::PlatformEvent& event) {
    if (!event.modifiers.alt || event.modifiers.ctrl || event.modifiers.meta) return false;
    if (event.type == platform::PlatformEvent::Type::KeyDown) {
        if (event.hid_usage == 0x2d || (!event.hid_usage && event.key == platform::KeyCode::Minus)) { adjust(-10); return true; }
        if (event.hid_usage == 0x2e || (!event.hid_usage && event.key == platform::KeyCode::Equals)) { adjust(10); return true; }
    }
    return event.type == platform::PlatformEvent::Type::TextInput &&
        (event.hid_usage == 0x2d || event.hid_usage == 0x2e);
}
void service(bool allow_save) {
    if (allow_save && pending && emscripten_get_now() >= save_at) persist();
}
void shutdown() { if (pending) persist(); }
}
