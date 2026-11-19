#include "platform.h"
#include "app.h"
#include "editor.h"
#include "sidebar.h"
#include "settings.h"
#include "file_manager.h"
#include "keyboard_input.h"
#include <emscripten.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

namespace {
App app;
lv_display_t* display;
lv_indev_t* keyboard;
uint16_t pixels[800 * 480];
platform::PlatformEvent last;
std::deque<platform::PlatformEvent> text_events;
std::string clipboard;
EM_JS(void, present, (const uint16_t* data), { Module.present(data); });
EM_JS(void, storage_sync, (), { Module.syncStorage(); });
void flush(lv_display_t* d, const lv_area_t*, uint8_t*) {
    if (lv_display_flush_is_last(d)) present(pixels);
    lv_display_flush_ready(d);
}
}
namespace platform {
bool init(int32_t, int32_t) {
    lv_tick_set_cb([]() -> uint32_t { return static_cast<uint32_t>(emscripten_get_now()); });
    if (!keyboard_input::init()) return false;
    display = lv_display_create(800, 480);
    if (!display) return false;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    keyboard = lv_indev_create();
    lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keyboard, [](lv_indev_t*, lv_indev_data_t* d) { keyboard_input::read_lvgl(d); });
    return keyboard != nullptr;
}
void shutdown() {}
lv_display_t* get_display() { return display; }
lv_indev_t* get_keyboard_indev() { return keyboard; }
bool poll_event(PlatformEvent& event, uint32_t) {
    last = {};
    if (keyboard_input::poll(event, 0)) { last = event; return true; }
    if (!text_events.empty()) { event = text_events.front(); text_events.pop_front(); last = event; return true; }
    event = {};
    return false;
}
void requeue_last_event() {
    if (keyboard_input::forward_to_lvgl(last)) lv_indev_read(keyboard);
}
void pump_events() {
    // Yield even during sidebar animations, which skip poll_event entirely.
    // Asyncify preserves the real run_app stack across this browser turn.
    emscripten_sleep(4);
    static uint32_t synced = 0;
    if (get_ticks_ms() - synced > 1000) { storage_sync(); synced = get_ticks_ms(); }
}
void begin_ui_animation(const char*) {}
void finish_ui_animation() {}
void end_ui_animation() {}
bool copy_display_strip(uint8_t* out, uint32_t stride, uint32_t width, uint32_t height) {
    if (!out || width > 800 || height > 480 || stride < width * 2) return false;
    for (uint32_t row = 0; row < height; ++row) std::memcpy(out + row * stride, pixels + row * 800, width * 2);
    return true;
}
KeyModifiers get_key_modifiers() { return keyboard_input::modifiers(); }
bool is_key_pressed(KeyCode key) { return keyboard_input::pressed(key); }
bool was_key_pressed(KeyCode key) { return last.type == PlatformEvent::Type::KeyDown && last.key == key && !last.repeat; }
bool clipboard_set(const char* value) { clipboard = value ? value : ""; return true; }
char* clipboard_get() { return strdup(clipboard.c_str()); }
void clipboard_free(char* value) { std::free(value); }
uint32_t get_ticks_ms() { return lv_tick_get(); }
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void leaf_report(int mods, int a, int b, int c, int d, int e, int f) {
    const uint8_t report[] = {uint8_t(mods), 0, uint8_t(a), uint8_t(b), uint8_t(c), uint8_t(d), uint8_t(e), uint8_t(f)};
    keyboard_input::submit_report(report, sizeof(report));
}
EMSCRIPTEN_KEEPALIVE void leaf_text(const char* value) {
    // Split only on UTF-8 codepoint boundaries to fit PlatformEvent::text.
    const char* start = value;
    while (*start) {
        size_t size = std::min<size_t>(31, std::strlen(start));
        if (start[size]) while (size && (static_cast<unsigned char>(start[size]) & 0xc0) == 0x80) --size;
        platform::PlatformEvent event{};
        event.type = platform::PlatformEvent::Type::TextInput;
        std::memcpy(event.text, start, size);
        text_events.push_back(event);
        start += size;
    }
}
EMSCRIPTEN_KEEPALIVE int leaf_save() {
    if (!app.editor) return 0;
    const bool ok = flush_editor_content(*app.editor);
    const bool state_ok = save_editor_state(*app.editor, app.sidebar->folder_data.collapsed_folders);
    return ok && state_ok;
}
EMSCRIPTEN_KEEPALIVE int leaf_cursor() {
    return app.editor ? lv_textarea_get_cursor_pos(app.editor->textarea) : -1;
}
EMSCRIPTEN_KEEPALIVE int leaf_view() {
    return app.sidebar ? (app.sidebar->visible ? 1 : 0) |
        (app.sidebar->file_dialog_active ? 2 : 0) | (settings_active() ? 4 : 0) : 0;
}
// Read-only diagnostics used by the browser integration regression test.
EMSCRIPTEN_KEEPALIVE const char* leaf_state() {
    static std::string state;
    if (!app.editor) return "";
    state = app.editor->current_file_path + "\n" + lv_textarea_get_text(app.editor->textarea);
    return state.c_str();
}
}
int main() {
    app.window_width = 800;
    app.window_height = 480;
    if (!init_app(app)) return 1;
    run_app(app);
    shutdown_app(app);
    return 0;
}
