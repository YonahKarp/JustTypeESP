#pragma once
#include "platform.h"
#include <cstddef>
#include <string>
struct EditorState;
namespace spellcheck {
void init();
bool tooltip_active();
bool handle_event(const platform::PlatformEvent &, EditorState &,
                  bool can_open);
void update(EditorState &, bool visible);
bool enabled();
bool toggle();
std::string current_word();
bool add_current();
size_t personal_count();
} // namespace spellcheck
