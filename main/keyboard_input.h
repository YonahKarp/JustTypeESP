#pragma once

#include <cstddef>
#include <cstdint>

#include "justtype/platforms/platform.h"

namespace keyboard_input {

// BLE callbacks run outside the LVGL/application task. They only submit HID
// boot-keyboard reports; all key state and LVGL interaction stays on the app task.
bool init();
void discard_pending();
void submit_report(const uint8_t* data, size_t length);
void disconnected();
bool poll(platform::PlatformEvent& event, uint32_t timeout_ms);
bool backlog_high();
platform::KeyModifiers modifiers();
bool pressed(platform::KeyCode key);
bool forward_to_lvgl(const platform::PlatformEvent& event);
void read_lvgl(lv_indev_data_t* data);

}  // namespace keyboard_input
