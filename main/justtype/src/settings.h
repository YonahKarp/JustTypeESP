#pragma once
#include "platform.h"
struct SidebarState;
struct EditorState;
bool settings_active();
void settings_open(SidebarState&, EditorState&);
void settings_handle_event(const platform::PlatformEvent&, SidebarState&, EditorState&);
