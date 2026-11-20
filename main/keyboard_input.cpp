#include "keyboard_input.h"
#include "keyboard_config.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace keyboard_input {
namespace {

struct Report {
    uint8_t keys[8];
    bool disconnect;
    int64_t received_us;
};

QueueHandle_t reports = nullptr;
platform::PlatformEvent events[32] = {};
size_t head = 0;
size_t count = 0;
uint8_t previous[8] = {};
bool held[static_cast<size_t>(platform::KeyCode::Meta) + 1] = {};
platform::KeyModifiers active_modifiers = {};
bool caps_lock = false;
platform::KeyCode active_keys[256] = {};
uint32_t lv_key = 0;
lv_indev_state_t lv_state = LV_INDEV_STATE_RELEASED;

platform::KeyCode key_from_usage(uint8_t usage) {
    using K = platform::KeyCode;
    if (usage >= 0x04 && usage <= 0x1d) {
        switch (usage) {
        case 0x04: return K::A;
        case 0x05: return K::B;
        case 0x06: return K::C;
        case 0x09: return K::F;
        case 0x13: return K::P;
        case 0x17: return K::T;
        case 0x19: return K::V;
        case 0x1b: return K::X;
        default: return K::Unknown;
        }
    }
    switch (usage) {
    case 0x28: return K::Enter;
    case 0x29: return K::Escape;
    case 0x2a: return K::Backspace;
    case 0x2b: return K::Tab;
    case 0x2d: return K::Minus;
    case 0x2e: return K::Equals;
    case 0x35: return K::Grave;
    case 0x4a: return K::Home;
    case 0x4c: return K::Delete;
    case 0x4d: return K::End;
    case 0x4f: return K::Right;
    case 0x50: return K::Left;
    case 0x51: return K::Down;
    case 0x52: return K::Up;
    default: return K::Unknown;
    }
}

uint32_t lv_key_from_usage(uint8_t usage) {
    switch (usage) {
    case 0x28: return LV_KEY_ENTER;
    case 0x29: return LV_KEY_ESC;
    case 0x2a: return LV_KEY_BACKSPACE;
    case 0x2b: return LV_KEY_NEXT;
    case 0x4a: return LV_KEY_HOME;
    case 0x4c: return LV_KEY_DEL;
    case 0x4d: return LV_KEY_END;
    case 0x4f: return LV_KEY_RIGHT;
    case 0x50: return LV_KEY_LEFT;
    case 0x51: return LV_KEY_DOWN;
    case 0x52: return LV_KEY_UP;
    default: return 0;
    }
}

void push(platform::PlatformEvent event) {
    if (count == std::size(events)) return;
    events[(head + count++) % std::size(events)] = event;
}

bool contains(const uint8_t* report, uint8_t usage) {
    for (int i = 2; i < 8; ++i) if (report[i] == usage) return true;
    return false;
}

// Treat the six boot-report slots as a set. Some keyboards repeat a usage
// (the prototype sends Right Arrow twice), which must not duplicate edges.
bool first_slot(const uint8_t* keys, int slot) {
    for (int i = 2; i < slot; ++i) if (keys[i] == keys[slot]) return false;
    return true;
}

void update_modifiers(uint8_t bits) {
    const bool previous_meta = active_modifiers.meta;
    active_modifiers.ctrl = (bits & 0x11) != 0;
    active_modifiers.shift = (bits & 0x22) != 0;
    active_modifiers.alt = (bits & 0x44) != 0;
    active_modifiers.meta = (bits & 0x88) != 0;
    held[static_cast<size_t>(platform::KeyCode::Control)] = active_modifiers.ctrl;
    held[static_cast<size_t>(platform::KeyCode::Shift)] = active_modifiers.shift;
    held[static_cast<size_t>(platform::KeyCode::Alt)] = active_modifiers.alt;
    held[static_cast<size_t>(platform::KeyCode::Meta)] = active_modifiers.meta;
    // Command is a standalone UI shortcut. Emit real edges so releasing it
    // inside a modal cannot leave an application-level edge detector stale.
    if (previous_meta != active_modifiers.meta) {
        platform::PlatformEvent event{};
        event.type = active_modifiers.meta ? platform::PlatformEvent::Type::KeyDown
                                          : platform::PlatformEvent::Type::KeyUp;
        event.key = platform::KeyCode::Meta;
        event.modifiers = active_modifiers;
        push(event);
    }
}

void emit_key(uint8_t usage, bool down) {
    if (down && usage == 0x39) caps_lock = !caps_lock;
    if (down && usage == 0x2a && keyboard_config::cancel_composition()) return;
    if (active_modifiers.ctrl || active_modifiers.meta) keyboard_config::cancel_composition();
    platform::PlatformEvent event {};
    event.type = down ? platform::PlatformEvent::Type::KeyDown : platform::PlatformEvent::Type::KeyUp;
    event.key = active_modifiers.ctrl && ((usage >= 4 && usage <= 29) || usage == 0x2d || usage == 0x2e) ? keyboard_config::shortcut(usage) : key_from_usage(usage);
    if (down) active_keys[usage] = event.key;
    else { event.key = active_keys[usage]; active_keys[usage] = platform::KeyCode::Unknown; }
    event.modifiers = active_modifiers;
    if (event.key != platform::KeyCode::Unknown) {
        held[static_cast<size_t>(event.key)] = down;
    }
    event.hid_usage = usage;
    push(event);

    const bool altgr = (previous[0] & 0x40) != 0;
    if (down && !active_modifiers.ctrl && (!active_modifiers.alt || altgr) && !active_modifiers.meta) {
        const std::string value = keyboard_config::text(usage, active_modifiers.shift, altgr, caps_lock);
        if (!value.empty()) {
            platform::PlatformEvent text {};
            text.type = platform::PlatformEvent::Type::TextInput;
            text.hid_usage = usage;
            text.modifiers = active_modifiers;
            std::strncpy(text.text, value.c_str(), sizeof(text.text)-1);
            push(text);
        }
    }
}

void decode(const Report& report) {
    if (report.disconnect) {
        const uint8_t empty[8] = {};
        Report release {};
        std::memcpy(release.keys, empty, sizeof(empty));
        decode(release);
        return;
    }
    // HID rollover/error usages mean the report cannot describe key state.
    for (int i = 2; i < 8; ++i) if (report.keys[i] >= 1 && report.keys[i] <= 3) return;
    previous[0] = report.keys[0];
    update_modifiers(report.keys[0]);
    for (int i = 2; i < 8; ++i) {
        if (previous[i] && first_slot(previous, i) && !contains(report.keys, previous[i])) emit_key(previous[i], false);
    }
    for (int i = 2; i < 8; ++i) {
        const uint8_t usage = report.keys[i];
        if (usage && first_slot(report.keys, i) && !contains(previous, usage)) emit_key(usage, true);
    }
    std::memcpy(previous, report.keys, sizeof(previous));
}

}  // namespace

void discard_pending() {
    if (reports) xQueueReset(reports);
    head = count = 0;
    std::memset(previous, 0, sizeof(previous));
    std::memset(held, 0, sizeof(held));
    std::fill(std::begin(active_keys), std::end(active_keys), platform::KeyCode::Unknown);
    active_modifiers = {};
    keyboard_config::cancel_composition();
    lv_state = LV_INDEV_STATE_RELEASED;
}

bool init() {
    reports = xQueueCreate(128, sizeof(Report));
    return reports != nullptr;
}

void submit_report(const uint8_t* data, size_t length) {
    if (!reports || !data) return;
    if (length == 9) {
        // Some HID hosts retain the report-ID prefix; others remove it.
        ++data;
        --length;
    }
    if (length != 8) return;
    Report report {};
    std::memcpy(report.keys, data, 8);
    report.received_us = esp_timer_get_time();
    if (xQueueSend(reports, &report, 0) != pdTRUE) {
        // Reports are complete snapshots. Preserve the newest state on overflow,
        // including releases/disconnects, rather than leaving keys stuck.
        Report discarded {};
        xQueueReceive(reports, &discarded, 0);
        xQueueSend(reports, &report, 0);
        ESP_LOGW("keyboard_input", "Report queue overflow; oldest snapshot dropped");
    }
}

void disconnected() {
    if (!reports) return;
    Report report {};
    report.disconnect = true;
    report.received_us = esp_timer_get_time();
    if (xQueueSend(reports, &report, 0) != pdTRUE) {
        // Reports are complete snapshots. Preserve the newest state on overflow,
        // including releases/disconnects, rather than leaving keys stuck.
        Report discarded {};
        xQueueReceive(reports, &discarded, 0);
        xQueueSend(reports, &report, 0);
        ESP_LOGW("keyboard_input", "Report queue overflow; oldest snapshot dropped");
    }
}

bool backlog_high() {
    // End a visual pause well before the 128-report queue can fill. Leave
    // ample headroom for reports arriving while the UI finishes a frame.
    return reports && uxQueueMessagesWaiting(reports) >= 16;
}

bool poll(platform::PlatformEvent& event, uint32_t timeout_ms) {
    if (count == 0 && reports) {
        Report report {};
        if (xQueueReceive(reports, &report, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            const int64_t age_ms = (esp_timer_get_time() - report.received_us) / 1000;
            static int64_t last_warning_us = 0;
            if (age_ms > 100 && esp_timer_get_time() - last_warning_us > 1000000) {
                last_warning_us = esp_timer_get_time();
                ESP_LOGW("keyboard_input", "Queue age %lld ms, backlog %u reports",
                         static_cast<long long>(age_ms), unsigned(uxQueueMessagesWaiting(reports)));
            }
            decode(report);
        }
    }
    if (count == 0) return false;
    event = events[head];
    head = (head + 1) % std::size(events);
    --count;
    if (event.type == platform::PlatformEvent::Type::KeyUp &&
        lv_key_from_usage(event.hid_usage) == lv_key) {
        lv_state = LV_INDEV_STATE_RELEASED;
    }
    return true;
}

platform::KeyModifiers modifiers() { return active_modifiers; }

bool pressed(platform::KeyCode key) {
    const auto index = static_cast<size_t>(key);
    return index < std::size(held) && held[index];
}

bool forward_to_lvgl(const platform::PlatformEvent& event) {
    if (event.type != platform::PlatformEvent::Type::KeyDown &&
        event.type != platform::PlatformEvent::Type::KeyUp) return false;
    const uint32_t key = lv_key_from_usage(event.hid_usage);
    if (!key) return false;
    lv_key = key;
    lv_state = event.type == platform::PlatformEvent::Type::KeyDown
        ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    return true;
}

void read_lvgl(lv_indev_data_t* data) {
    data->key = lv_key;
    data->state = lv_state;
}

}  // namespace keyboard_input
