#pragma once
#include "justtype/platforms/platform.h"
#include <string>
namespace keyboard_config {
constexpr int layout_count = 5;
constexpr int action_count = 11;
constexpr int backup_action = 8;
void load();
bool save();
int layout();
void set_layout(int);
const char *layout_name(int);
const char *action_name(int);
std::string binding_name(int);
bool bind(int action, uint8_t usage, platform::KeyModifiers modifiers = {true, false, false, false});
void reset();
platform::KeyCode shortcut(uint8_t usage, platform::KeyModifiers modifiers = {true, false, false, false});
std::string text(uint8_t usage, bool shift, bool altgr, bool caps = false);
bool cancel_composition();
} // namespace keyboard_config
