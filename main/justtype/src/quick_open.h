#pragma once
#include "platform.h"
#include <string>

struct EditorState;
bool create_blank_document(EditorState& editor, std::string& error);
bool quick_open_active();
void quick_open_show(EditorState& editor, lv_group_t* group, const std::string& error = {});
void quick_open_close();
void quick_open_handle_event(const platform::PlatformEvent& event);
