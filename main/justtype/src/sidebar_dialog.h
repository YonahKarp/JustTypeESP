#pragma once
#include "lvgl.h"
struct Theme;
lv_obj_t* create_confirmation_dialog(lv_obj_t*, const Theme&, int, int, const char*, const char*, lv_color_t, lv_obj_t**, lv_obj_t**, lv_obj_t**);
void update_confirmation_selection(lv_obj_t*, lv_obj_t*, int);

struct SidebarState;
struct EditorState;

void show_delete_dialog(SidebarState& sidebar, EditorState& editor, int index);
void hide_delete_dialog(SidebarState& sidebar);
void update_delete_dialog_selection(SidebarState& sidebar, EditorState& editor);
void confirm_delete_action(SidebarState& sidebar, EditorState& editor);

void show_restore_dialog(SidebarState& sidebar, EditorState& editor, int deleted_index);
void hide_restore_dialog(SidebarState& sidebar);
void update_restore_dialog_selection(SidebarState& sidebar, EditorState& editor);
void confirm_restore_action(SidebarState& sidebar, EditorState& editor);
