#include "international_font.h"
#include "editor_font_sizes.h"
#include "content.h"
#include "app.h"
#include "backup/backup.h"
#include "utf8_positions.h"
#include "settings.h"
#include "spellcheck.h"
#include "serial_export.h"
#include "keyboard_input.h"
#include "backlight.h"
#include "keyboard_config.h"
#include "editor.h"
#include "sidebar.h"
#include "sidebar_file_dialog.h"
#include "search.h"
#include "theme.h"
#include "file_manager.h"
#include "quick_open.h"
#include "textarea_utils.h"
#include "text_navigation.h"
#include "platform.h"
#include <fstream>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <string>

#include "lvgl.h"

static EditorState g_editor;
static SidebarState g_sidebar;
static SearchState g_search;
static lv_group_t* g_input_group = nullptr;
static int32_t g_selection_start = -1;
static bool g_suppress_close_shortcut_text = false;
static lv_obj_t* g_deferred_focus = nullptr;

static bool is_close_shortcut_pressed() {
    return platform::was_key_pressed(platform::KeyCode::Escape) ||
           platform::was_key_pressed(platform::KeyCode::Grave);
}

static bool is_close_shortcut_event(const platform::PlatformEvent& event) {
    return event.type == platform::PlatformEvent::Type::KeyDown &&
           (event.key == platform::KeyCode::Escape ||
            event.key == platform::KeyCode::Grave);
}

static void focus_editor_textarea() {
    if (g_sidebar.animating) { g_deferred_focus = g_editor.textarea; return; }
    if (g_input_group) {
        lv_group_focus_obj(g_editor.textarea);
    }
}

static void focus_sidebar_search() {
    if (g_sidebar.animating) { g_deferred_focus = g_sidebar.search_input; return; }
    if (g_input_group) {
        lv_group_focus_obj(g_sidebar.search_input);
    }
}

static void focus_editor_search() {
    if (g_sidebar.animating) { g_deferred_focus = g_editor.search_input; return; }
    if (g_input_group) {
        lv_group_focus_obj(g_editor.search_input);
    }
}

static void highlight_current_match() {
    if (!g_search.active || g_search.current_match_index < 0 ||
        g_search.current_match_index >= static_cast<int>(g_search.matches.size())) {
        return;
    }

    const SearchMatch& match = g_search.matches[g_search.current_match_index];

    lv_obj_t* label = lv_textarea_get_label(g_editor.textarea);
    if (label) {
        lv_label_set_text_selection_start(label, match.start);
        lv_label_set_text_selection_end(label, match.end);
        lv_obj_invalidate(g_editor.textarea);
    }

    lv_textarea_set_cursor_pos(g_editor.textarea, match.start);
}

static void update_match_label() {
    if (g_search.matches.empty()) {
        lv_label_set_text(g_editor.match_label, "0/0");
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d/%d",
                 g_search.current_match_index + 1,
                 static_cast<int>(g_search.matches.size()));
        lv_label_set_text(g_editor.match_label, buf);
    }
}

static void search_input_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        const char* query = lv_textarea_get_text(g_editor.search_input);
        int query_len = strlen(query);

        if (query_len > 0 && query_len < static_cast<int>(sizeof(g_search.query))) {
            strncpy(g_search.query, query, sizeof(g_search.query) - 1);
            g_search.query[sizeof(g_search.query) - 1] = '\0';
            g_search.query_len = query_len;

            const char* text = lv_textarea_get_text(g_editor.textarea);
            int text_len = strlen(text);

            perform_search(g_search, text, text_len);
            update_match_label();
            highlight_current_match();
        } else {
            g_search.query[0] = '\0';
            g_search.query_len = 0;
            g_search.matches.clear();
            g_search.current_match_index = -1;
            update_match_label();

            lv_obj_t* label = lv_textarea_get_label(g_editor.textarea);
            if (label) {
                lv_label_set_text_selection_start(label, LV_LABEL_TEXT_SELECTION_OFF);
                lv_label_set_text_selection_end(label, LV_LABEL_TEXT_SELECTION_OFF);
                lv_obj_invalidate(g_editor.textarea);
            }
        }
    }
}

static void clear_selection_local(lv_obj_t* textarea) {
    if (!textarea) return;
    lv_obj_t* label = lv_textarea_get_label(textarea);
    if (label) {
        lv_label_set_text_selection_start(label, LV_LABEL_TEXT_SELECTION_OFF);
        lv_label_set_text_selection_end(label, LV_LABEL_TEXT_SELECTION_OFF);
        lv_obj_invalidate(textarea);
    }
    g_selection_start = -1;
}

static std::string get_selected_text(lv_obj_t* textarea) {
    if (!textarea || !lv_obj_check_type(textarea, &lv_textarea_class)) {
        return "";
    }

    lv_obj_t* label = lv_textarea_get_label(textarea);
    if (!label) return "";

    uint32_t sel_start = lv_label_get_text_selection_start(label);
    uint32_t sel_end = lv_label_get_text_selection_end(label);

    if (sel_start == LV_LABEL_TEXT_SELECTION_OFF || sel_end == LV_LABEL_TEXT_SELECTION_OFF) {
        return "";
    }

    if (sel_start > sel_end) {
        uint32_t tmp = sel_start;
        sel_start = sel_end;
        sel_end = tmp;
    }

    const char* text = lv_textarea_get_text(textarea);
    if (!text) return "";

    uint32_t text_len = utf8_length(text);
    if (sel_start >= text_len) return "";
    if (sel_end > text_len) sel_end = text_len;

    uint32_t start_byte = utf8_byte(text, sel_start);
    uint32_t end_byte = utf8_byte(text, sel_end);
    return std::string(text + start_byte, end_byte - start_byte);
}

static void delete_selected_text(lv_obj_t* textarea, uint32_t* out_cursor_pos = nullptr) {
    if (textarea_delete_selected_text(textarea, out_cursor_pos)) {
        g_selection_start = -1;
    }
}

static bool has_selection(lv_obj_t* textarea) {
    return textarea_has_selection(textarea);
}

static void handle_clipboard() {
    platform::KeyModifiers modifiers = platform::get_key_modifiers();
    bool ctrl = modifiers.ctrl || keyboard_input::current_shortcut();
    bool cmd = modifiers.meta;

    if (!ctrl && !cmd) return;
    if (g_sidebar.visible) return;

    static uint32_t last_clipboard_time = 0;
    uint32_t now = lv_tick_get();
    bool debounce = (now - last_clipboard_time) > 200;
    if (!debounce) return;

    lv_obj_t* focused = lv_group_get_focused(g_input_group);
    if (!focused || !lv_obj_check_type(focused, &lv_textarea_class)) return;

    // Copy (Ctrl+C or Cmd+C)
    if (platform::was_key_pressed(platform::KeyCode::C)) {
        last_clipboard_time = now;
        std::string selected = get_selected_text(focused);
        if (!selected.empty()) {
            platform::clipboard_set(selected.c_str());
        }
    }

    // Cut (Ctrl+X or Cmd+X)
    if (platform::was_key_pressed(platform::KeyCode::X)) {
        last_clipboard_time = now;
        std::string selected = get_selected_text(focused);
        if (!selected.empty()) {
            platform::clipboard_set(selected.c_str());
            delete_selected_text(focused);

            if (focused == g_editor.textarea) {
                g_editor.content_pending_save = true;
                g_editor.content_change_time = lv_tick_get();
                update_word_count(g_editor);
            }
        }
    }

    // Paste (Ctrl+V or Cmd+V)
    if (platform::was_key_pressed(platform::KeyCode::V)) {
        last_clipboard_time = now;
        char* clipboard = platform::clipboard_get();
        if (clipboard && strlen(clipboard) > 0) {
            size_t max_bytes = (focused == g_editor.textarea) ? static_cast<size_t>(TEXT_BUFFER_SIZE - 1) : 0;
            if (textarea_add_text_with_limit(focused, clipboard, max_bytes) && focused == g_editor.textarea) {
                g_editor.content_pending_save = true;
                g_editor.content_change_time = lv_tick_get();
                update_word_count(g_editor);
            }
        }
        platform::clipboard_free(clipboard);
    }
}

static void handle_text_selection() {
    platform::KeyModifiers modifiers = platform::get_key_modifiers();
    bool shift = modifiers.shift;
    bool ctrl = modifiers.ctrl;
    bool alt = modifiers.alt;

    static int32_t last_cursor_pos = -1;
    static bool last_shift = false;
    static bool last_any_arrow = false;
    static uint32_t last_nav_time = 0;

    static KeyRepeatState ctrl_up_state;
    static KeyRepeatState ctrl_down_state;
    static KeyRepeatState ctrl_shift_up_state;
    static KeyRepeatState ctrl_shift_down_state;
    static KeyRepeatState ctrl_shift_left_state;
    static KeyRepeatState ctrl_shift_right_state;
    static KeyRepeatState ctrl_backspace_state;
    static KeyRepeatState ctrl_left_state;
    static KeyRepeatState ctrl_right_state;
    static KeyRepeatState alt_left_state;
    static KeyRepeatState alt_right_state;

    lv_obj_t* focused = lv_group_get_focused(g_input_group);
    if (!focused || !lv_obj_check_type(focused, &lv_textarea_class)) {
        last_cursor_pos = -1;
        last_shift = false;
        last_any_arrow = false;
        return;
    }

    lv_obj_t* label = lv_textarea_get_label(focused);
    if (!label) return;

    int32_t cur_pos = lv_textarea_get_cursor_pos(focused);
    const char* text = lv_textarea_get_text(focused);
    // Ordinary typing and idle passes do not need a document-wide UTF-8 scan.

    uint32_t now = lv_tick_get();
    bool nav_debounce = (now - last_nav_time) > 100;

    // Handle Ctrl+Backspace (delete previous word, or delete selection if present)
    bool ctrl_backspace = ctrl && platform::is_key_pressed(platform::KeyCode::Backspace) && focused == g_editor.textarea;
    if (ctrl_backspace_state.should_act(ctrl_backspace, now)) {
        if (has_selection(focused)) {
            delete_selected_text(focused);
        } else {
            int32_t word_start = find_prev_word_start(text, cur_pos);
            if (word_start < cur_pos) {
                // Delete characters using native LVGL deletion (faster than set_text)
                int32_t chars_to_delete = cur_pos - word_start;
                for (int32_t i = 0; i < chars_to_delete; i++) {
                    lv_textarea_delete_char(focused);
                }
                cur_pos = word_start;
            }
        }
        g_editor.content_pending_save = true;
        g_editor.content_change_time = lv_tick_get();
        update_word_count(g_editor);
    }
    if (ctrl_backspace) return;

    // Handle Ctrl+Left/Right (word navigation) with key repeat
    bool ctrl_left = ctrl && !shift && !alt && platform::is_key_pressed(platform::KeyCode::Left);
    bool ctrl_right = ctrl && !shift && !alt && platform::is_key_pressed(platform::KeyCode::Right);

    if (ctrl_left_state.should_act(ctrl_left, now)) {
        int32_t new_pos = find_prev_word_start(text, cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        clear_selection_local(focused);
    }

    if (ctrl_right_state.should_act(ctrl_right, now)) {
        int32_t new_pos = find_next_word_end(text, utf8_length(text), cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        clear_selection_local(focused);
    }

    if (ctrl_left || ctrl_right) return;

    // Handle Alt+Left/Right (line start/end) with key repeat
    bool alt_left = alt && !shift && !ctrl && platform::is_key_pressed(platform::KeyCode::Left);
    bool alt_right = alt && !shift && !ctrl && platform::is_key_pressed(platform::KeyCode::Right);

    if (alt_left_state.should_act(alt_left, now)) {
        int32_t new_pos = find_line_start(text, cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        g_selection_start = -1;
    }

    if (alt_right_state.should_act(alt_right, now)) {
        int32_t new_pos = find_line_end(text, utf8_length(text), cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        g_selection_start = -1;
    }

    if (alt_left || alt_right) return;

    // Handle Alt+Up/Down (jump to top/bottom of document)
    if (alt && !shift && !ctrl && nav_debounce) {
        if (platform::is_key_pressed(platform::KeyCode::Up)) {
            last_nav_time = now;
            lv_textarea_set_cursor_pos(focused, 0);
            g_selection_start = -1;
            return;
        }
        if (platform::is_key_pressed(platform::KeyCode::Down)) {
            last_nav_time = now;
            lv_textarea_set_cursor_pos(focused, utf8_length(text));
            g_selection_start = -1;
            return;
        }
    }

    // Handle Ctrl+Up/Down (jump to previous/next paragraph) with key repeat
    bool ctrl_up = ctrl && !shift && !alt && platform::is_key_pressed(platform::KeyCode::Up);
    bool ctrl_down = ctrl && !shift && !alt && platform::is_key_pressed(platform::KeyCode::Down);

    if (ctrl_up_state.should_act(ctrl_up, now)) {
        int32_t new_pos = find_paragraph_start(text, cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        g_selection_start = -1;
    }

    if (ctrl_down_state.should_act(ctrl_down, now)) {
        int32_t new_pos = find_paragraph_end(text, utf8_length(text), cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        g_selection_start = -1;
    }

    // Handle Ctrl+Shift+Up/Down (extend selection to previous/next paragraph) with key repeat
    bool ctrl_shift_up = ctrl && shift && !alt && platform::is_key_pressed(platform::KeyCode::Up);
    bool ctrl_shift_down = ctrl && shift && !alt && platform::is_key_pressed(platform::KeyCode::Down);
    bool ctrl_shift_left = ctrl && shift && !alt && platform::is_key_pressed(platform::KeyCode::Left);
    bool ctrl_shift_right = ctrl && shift && !alt && platform::is_key_pressed(platform::KeyCode::Right);

    if (ctrl_shift_up_state.should_act(ctrl_shift_up, now)) {
        if (g_selection_start < 0) g_selection_start = cur_pos;
        int32_t new_pos = find_paragraph_start(text, cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        uint32_t sel_start = (g_selection_start < new_pos) ? g_selection_start : new_pos;
        uint32_t sel_end = (g_selection_start < new_pos) ? new_pos : g_selection_start;
        lv_label_set_text_selection_start(label, sel_start);
        lv_label_set_text_selection_end(label, sel_end);
        lv_obj_invalidate(focused);
        last_cursor_pos = new_pos;
    }

    if (ctrl_shift_down_state.should_act(ctrl_shift_down, now)) {
        if (g_selection_start < 0) g_selection_start = cur_pos;
        int32_t new_pos = find_paragraph_end(text, utf8_length(text), cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        uint32_t sel_start = (g_selection_start < new_pos) ? g_selection_start : new_pos;
        uint32_t sel_end = (g_selection_start < new_pos) ? new_pos : g_selection_start;
        lv_label_set_text_selection_start(label, sel_start);
        lv_label_set_text_selection_end(label, sel_end);
        lv_obj_invalidate(focused);
        last_cursor_pos = new_pos;
    }

    // Handle Ctrl+Shift+Left/Right (extend selection by word) with key repeat
    if (ctrl_shift_left_state.should_act(ctrl_shift_left, now)) {
        if (g_selection_start < 0) g_selection_start = cur_pos;
        int32_t new_pos = find_prev_word_start(text, cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        uint32_t sel_start = (g_selection_start < new_pos) ? g_selection_start : new_pos;
        uint32_t sel_end = (g_selection_start < new_pos) ? new_pos : g_selection_start;
        lv_label_set_text_selection_start(label, sel_start);
        lv_label_set_text_selection_end(label, sel_end);
        lv_obj_invalidate(focused);
        last_cursor_pos = new_pos;
    }

    if (ctrl_shift_right_state.should_act(ctrl_shift_right, now)) {
        if (g_selection_start < 0) g_selection_start = cur_pos;
        int32_t new_pos = find_next_word_end(text, utf8_length(text), cur_pos);
        lv_textarea_set_cursor_pos(focused, new_pos);
        uint32_t sel_start = (g_selection_start < new_pos) ? g_selection_start : new_pos;
        uint32_t sel_end = (g_selection_start < new_pos) ? new_pos : g_selection_start;
        lv_label_set_text_selection_start(label, sel_start);
        lv_label_set_text_selection_end(label, sel_end);
        lv_obj_invalidate(focused);
        last_cursor_pos = new_pos;
    }

    if (ctrl_up || ctrl_down || ctrl_shift_up || ctrl_shift_down || ctrl_shift_left || ctrl_shift_right) {
        return;
    }

    bool any_arrow = platform::is_key_pressed(platform::KeyCode::Left) || platform::is_key_pressed(platform::KeyCode::Right) ||
                     platform::is_key_pressed(platform::KeyCode::Up) || platform::is_key_pressed(platform::KeyCode::Down) ||
                     platform::is_key_pressed(platform::KeyCode::Home) || platform::is_key_pressed(platform::KeyCode::End);

    // Only clear selection when moving cursor WITHOUT shift
    if (!shift && any_arrow) {
        if (g_selection_start >= 0) {
            lv_label_set_text_selection_start(label, LV_LABEL_TEXT_SELECTION_OFF);
            lv_label_set_text_selection_end(label, LV_LABEL_TEXT_SELECTION_OFF);
            lv_obj_invalidate(focused);
        }
        g_selection_start = -1;
        last_cursor_pos = cur_pos;
        last_shift = false;
        last_any_arrow = any_arrow;
        return;
    }

    // If shift is not pressed and no arrow key, just update tracking
    if (!shift) {
        last_cursor_pos = cur_pos;
        last_shift = false;
        last_any_arrow = any_arrow;
        return;
    }

    if (!any_arrow) {
        last_cursor_pos = cur_pos;
        last_shift = shift;
        last_any_arrow = false;
        return;
    }

    // Detect new arrow key press while shift is held
    bool new_selection_move = (shift && any_arrow && (!last_any_arrow || !last_shift));

    // Initialize selection start from the position BEFORE the key was pressed
    if (g_selection_start < 0 && new_selection_move && last_cursor_pos >= 0) {
        g_selection_start = last_cursor_pos;
    } else if (g_selection_start < 0) {
        g_selection_start = cur_pos;
    }

    // Update selection based on current cursor position
    uint32_t sel_start = (g_selection_start < cur_pos) ? g_selection_start : cur_pos;
    uint32_t sel_end = (g_selection_start < cur_pos) ? cur_pos : g_selection_start;

    lv_label_set_text_selection_start(label, sel_start);
    lv_label_set_text_selection_end(label, sel_end);
    lv_obj_invalidate(focused);

    last_cursor_pos = cur_pos;
    last_shift = shift;
    last_any_arrow = any_arrow;
}

static void keyboard_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_KEY) return;

    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_ESC) {
        if (g_search.active) {
            close_search(g_search);
            lv_obj_add_flag(g_editor.search_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_editor.word_count_label, LV_OBJ_FLAG_HIDDEN);
            clear_selection_local(g_editor.textarea);
            focus_editor_textarea();
        } else if (g_sidebar.visible) {
            hide_sidebar(g_sidebar, g_editor);
            focus_editor_textarea();
        }
    }
}

static void handle_sidebar_keyboard() {
    if (!g_sidebar.visible) return;

    bool ctrl = platform::get_key_modifiers().ctrl || keyboard_input::current_shortcut();
    uint32_t now = lv_tick_get();
    // HID emits one edge per press, so a time gate loses fast distinct taps.
    bool nav_debounce = true;

    // Close keys bypass navigation debounce and dismiss only the current surface.
    if (is_close_shortcut_pressed()) {
        if (g_sidebar.file_dialog_active) hide_file_dialog(g_sidebar);
        else if (g_sidebar.confirm_restore) hide_restore_dialog(g_sidebar);
        else if (g_sidebar.confirm_delete) hide_delete_dialog(g_sidebar);
        else if (g_sidebar.renaming) {
            if (g_sidebar.rename_textarea) lv_group_remove_obj(g_sidebar.rename_textarea);
            cancel_rename(g_sidebar, g_editor);
        } else if (g_sidebar.searching) toggle_sidebar_search(g_sidebar, g_editor);
        else { hide_sidebar(g_sidebar, g_editor); focus_editor_textarea(); }
        return;
    }

    if (g_sidebar.file_dialog_active) {
        handle_file_dialog_keyboard(g_sidebar, g_editor);
        return;
    }

    // Skip processing for a short time after file dialog closes
    // to prevent Enter key from triggering sidebar actions
    if (g_sidebar.file_dialog_close_time > 0 &&
        (now - g_sidebar.file_dialog_close_time) < 200 &&
        platform::was_key_pressed(platform::KeyCode::Enter)) {
        return;
    }

    // Handle Ctrl+F to toggle search
    if (ctrl && nav_debounce && platform::was_key_pressed(platform::KeyCode::F)) {
        toggle_sidebar_search(g_sidebar, g_editor);
        return;
    }

    // Handle restore dialog
    if (g_sidebar.confirm_restore) {
        if (nav_debounce) {
            if (platform::was_key_pressed(platform::KeyCode::Left)) {
                g_sidebar.dialog_selection = 0;
                update_restore_dialog_selection(g_sidebar, g_editor);
            }

            if (platform::was_key_pressed(platform::KeyCode::Right)) {
                g_sidebar.dialog_selection = 1;
                update_restore_dialog_selection(g_sidebar, g_editor);
            }

            if (platform::was_key_pressed(platform::KeyCode::Enter)) {
                confirm_restore_action(g_sidebar, g_editor);
            }

            if (is_close_shortcut_pressed()) {
                hide_restore_dialog(g_sidebar);
            }
        }
        return;
    }

    // Handle delete dialog
    if (g_sidebar.confirm_delete) {
        if (nav_debounce) {
            if (platform::was_key_pressed(platform::KeyCode::Left)) {
                g_sidebar.dialog_selection = 0;
                update_delete_dialog_selection(g_sidebar, g_editor);
            }

            if (platform::was_key_pressed(platform::KeyCode::Right)) {
                g_sidebar.dialog_selection = 1;
                update_delete_dialog_selection(g_sidebar, g_editor);
            }

            if (platform::was_key_pressed(platform::KeyCode::Enter)) {
                confirm_delete_action(g_sidebar, g_editor);
            }

            if (is_close_shortcut_pressed()) {
                hide_delete_dialog(g_sidebar);
            }
        }
        return;
    }

    // Only navigation actions need a fresh display list. Idle animation passes,
    // text input and key releases must not rebuild/copy every file and snippet.
    if (!nav_debounce ||
        !(platform::was_key_pressed(platform::KeyCode::Up) ||
          platform::was_key_pressed(platform::KeyCode::Down) ||
          platform::was_key_pressed(platform::KeyCode::Left) ||
          platform::was_key_pressed(platform::KeyCode::Right) ||
          platform::was_key_pressed(platform::KeyCode::Tab) ||
          platform::was_key_pressed(platform::KeyCode::Enter))) return;

    // Build display items first to get accurate count
    g_sidebar.folder_data.build_display_items(g_sidebar.searching);
    int display_count = (int)g_sidebar.folder_data.display_items.size();
    int deleted_count = g_sidebar.searching ? (int)g_sidebar.filtered_deleted_list.size() : 0;
    int total_count = display_count + deleted_count;

    if (nav_debounce) {
        if (platform::was_key_pressed(platform::KeyCode::Up)) {
            if (g_sidebar.renaming) {
                if (g_sidebar.rename_textarea) {
                    lv_group_remove_obj(g_sidebar.rename_textarea);
                }
                cancel_rename(g_sidebar, g_editor);
            }
            if (g_sidebar.new_folder_selected || g_sidebar.new_file_selected || g_sidebar.settings_selected) {
                // Already at top, do nothing
            } else if (g_sidebar.selected_index == 0) {
                if (!g_sidebar.searching) {
                g_sidebar.new_file_selected = true;
                }
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (g_sidebar.selected_index > 0) {
                g_sidebar.selected_index--;
                refresh_file_list_ui(g_sidebar, g_editor);
            }
        }

        if (platform::was_key_pressed(platform::KeyCode::Down)) {
            if (g_sidebar.renaming) {
                if (g_sidebar.rename_textarea) {
                    lv_group_remove_obj(g_sidebar.rename_textarea);
                }
                cancel_rename(g_sidebar, g_editor);
            }
            if (g_sidebar.settings_selected) {
                g_sidebar.settings_selected = false;
                g_sidebar.selected_index = 0;
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (g_sidebar.new_folder_selected) {
                g_sidebar.new_folder_selected = false;
                g_sidebar.selected_index = 0;
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (g_sidebar.new_file_selected) {
                g_sidebar.new_file_selected = false;
                g_sidebar.selected_index = 0;
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (g_sidebar.selected_index < total_count - 1) {
                g_sidebar.selected_index++;
                refresh_file_list_ui(g_sidebar, g_editor);
            }
        }

        // Left arrow: navigate from new_file to new_folder
        if (platform::was_key_pressed(platform::KeyCode::Left) && !g_sidebar.searching && !g_sidebar.renaming) {
            if (g_sidebar.settings_selected) {
                g_sidebar.settings_selected = false;
                g_sidebar.new_file_selected = true;
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (g_sidebar.new_file_selected) {
                g_sidebar.new_file_selected = false;
                g_sidebar.new_folder_selected = true;
                refresh_file_list_ui(g_sidebar, g_editor);
            }
        }

        // Right arrow: navigate from new_folder to new_file, or start rename on file/folder
        if (platform::was_key_pressed(platform::KeyCode::Right) && !g_sidebar.searching) {
            if (g_sidebar.new_folder_selected) {
                g_sidebar.new_folder_selected = false;
                g_sidebar.new_file_selected = true;
                refresh_file_list_ui(g_sidebar, g_editor);
            } else if (!g_sidebar.settings_selected && !g_sidebar.new_file_selected && !g_sidebar.renaming &&
                       g_sidebar.selected_index >= 0 && g_sidebar.selected_index < display_count) {
                const SidebarItem& item = g_sidebar.folder_data.display_items[g_sidebar.selected_index];
                if (item.is_folder()) {
                    show_file_dialog(g_sidebar, g_editor, FILE_DIALOG_MODE_RENAME_FOLDER);
                    if (g_sidebar.file_dialog_name_input) {
                        lv_group_add_obj(g_input_group, g_sidebar.file_dialog_name_input);
                        lv_group_focus_obj(g_sidebar.file_dialog_name_input);
                    }
                } else {
                    start_rename(g_sidebar, g_editor);
                    if (g_sidebar.rename_textarea) {
                        lv_group_add_obj(g_input_group, g_sidebar.rename_textarea);
                        lv_group_focus_obj(g_sidebar.rename_textarea);
                    }
                }
            }
        }

        // Tab key: rename selected file/folder
        if (platform::was_key_pressed(platform::KeyCode::Tab) && !g_sidebar.searching && !g_sidebar.renaming &&
            !g_sidebar.new_file_selected && !g_sidebar.new_folder_selected) {
            if (g_sidebar.selected_index >= 0 && g_sidebar.selected_index < display_count) {
                const SidebarItem& item = g_sidebar.folder_data.display_items[g_sidebar.selected_index];
                if (item.is_folder()) {
                    show_file_dialog(g_sidebar, g_editor, FILE_DIALOG_MODE_RENAME_FOLDER);
                    if (g_sidebar.file_dialog_name_input) {
                        lv_group_add_obj(g_input_group, g_sidebar.file_dialog_name_input);
                        lv_group_focus_obj(g_sidebar.file_dialog_name_input);
                    }
                } else {
                    show_file_dialog(g_sidebar, g_editor, FILE_DIALOG_MODE_MOVE_RENAME_FILE);
                    // Add the name input to the input group and focus it
                    if (g_sidebar.file_dialog_name_input) {
                        lv_group_add_obj(g_input_group, g_sidebar.file_dialog_name_input);
                        lv_group_focus_obj(g_sidebar.file_dialog_name_input);
                    }
                }
            }
            return;
        }

        if (platform::was_key_pressed(platform::KeyCode::Enter)) {
            if (g_sidebar.settings_selected) {
                settings_open(g_sidebar, g_editor);
            } else if (g_sidebar.renaming) {
                if (g_sidebar.rename_textarea) {
                    lv_group_remove_obj(g_sidebar.rename_textarea);
                }
                commit_rename(g_sidebar, g_editor);
            } else if (g_sidebar.new_folder_selected) {
                show_file_dialog(g_sidebar, g_editor, FILE_DIALOG_MODE_NEW_FOLDER);
                if (g_sidebar.file_dialog_name_input) {
                    lv_group_add_obj(g_input_group, g_sidebar.file_dialog_name_input);
                    lv_group_focus_obj(g_sidebar.file_dialog_name_input);
                }
            } else if (g_sidebar.new_file_selected) {
                show_file_dialog(g_sidebar, g_editor, FILE_DIALOG_MODE_NEW_FILE);
                // Add the name input to the input group and focus it
                if (g_sidebar.file_dialog_name_input) {
                    lv_group_add_obj(g_input_group, g_sidebar.file_dialog_name_input);
                    lv_group_focus_obj(g_sidebar.file_dialog_name_input);
                }
            } else if (g_sidebar.selected_index >= display_count && g_sidebar.searching) {
                int deleted_index = g_sidebar.selected_index - display_count;
                show_restore_dialog(g_sidebar, g_editor, deleted_index);
            } else if (g_sidebar.selected_index >= 0 && g_sidebar.selected_index < display_count) {
                const SidebarItem& item = g_sidebar.folder_data.display_items[g_sidebar.selected_index];
                if (item.is_file()) {
                    SidebarFileEntry entry;
                    entry.filename = item.name;
                    entry.folder = item.folder;
                    switch_to_file_entry(g_sidebar, g_editor, entry);
                    hide_sidebar(g_sidebar, g_editor);
                    focus_editor_textarea();
                } else {
                    // Toggle folder expansion on Enter (collapsed_folders tracks closed folders)
                    if (g_sidebar.folder_data.collapsed_folders.find(item.name) !=
                        g_sidebar.folder_data.collapsed_folders.end()) {
                        g_sidebar.folder_data.collapsed_folders.erase(item.name);
                    } else {
                        g_sidebar.folder_data.collapsed_folders.insert(item.name);
                    }
                    g_editor.state_pending_save = true;
                    g_editor.state_change_time = lv_tick_get();
                    refresh_file_list_ui(g_sidebar, g_editor);
                }
            }
        }

        // Delete key shows delete dialog (for files and folders)
        if (platform::was_key_pressed(platform::KeyCode::Delete) && !g_sidebar.searching && !g_sidebar.renaming) {
            if (!g_sidebar.new_file_selected && !g_sidebar.new_folder_selected &&
                g_sidebar.selected_index >= 0 && g_sidebar.selected_index < display_count) {
                show_delete_dialog(g_sidebar, g_editor, g_sidebar.selected_index);
            }
        }

        if (is_close_shortcut_pressed()) {
            if (g_sidebar.renaming) {
                if (g_sidebar.rename_textarea) {
                    lv_group_remove_obj(g_sidebar.rename_textarea);
                }
                cancel_rename(g_sidebar, g_editor);
            } else if (g_sidebar.searching) {
                toggle_sidebar_search(g_sidebar, g_editor);
            } else {
                hide_sidebar(g_sidebar, g_editor);
                focus_editor_textarea();
            }
        }
    }
}

static void handle_search_navigation() {
    if (!g_search.active) return;

    static uint32_t last_nav_time = 0;
    uint32_t now = lv_tick_get();
    bool debounce = (now - last_nav_time) > 150;

    // Handle ESC/backtick before navigation debounce.
    // Handle ESC to close search
    if (is_close_shortcut_pressed()) {
        last_nav_time = now;
        close_search(g_search);
        lv_obj_add_flag(g_editor.search_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_editor.word_count_label, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(g_editor.search_input, "");
        clear_selection_local(g_editor.textarea);
        focus_editor_textarea();
        return;
    }

    if (!debounce) return;
    lv_obj_t* focused = lv_group_get_focused(g_input_group);
    if (focused != g_editor.search_input) return;

    bool shift = platform::get_key_modifiers().shift;

    if (platform::was_key_pressed(platform::KeyCode::Enter)) {
        last_nav_time = now;
        if (shift) {
            navigate_to_prev_match(g_search);
        } else {
            navigate_to_next_match(g_search);
        }
        update_match_label();
        highlight_current_match();
    }
}

static bool handle_shortcuts() {
    platform::KeyModifiers modifiers = platform::get_key_modifiers();
    bool ctrl = modifiers.ctrl || keyboard_input::current_shortcut();

    if (ctrl && !g_sidebar.file_dialog_active && !g_sidebar.confirm_delete &&
        !g_sidebar.confirm_restore && !g_sidebar.renaming &&
        (platform::was_key_pressed(platform::KeyCode::N) ||
         platform::was_key_pressed(platform::KeyCode::O))) {
        const bool create = platform::was_key_pressed(platform::KeyCode::N);
        if (g_search.active) {
            close_search(g_search);
            lv_obj_add_flag(g_editor.search_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(g_editor.word_count_label, LV_OBJ_FLAG_HIDDEN);
            clear_selection_local(g_editor.textarea);
        }
        if (g_sidebar.visible) {
            hide_sidebar(g_sidebar, g_editor);
            finish_sidebar_slide(g_sidebar);
        }
        g_deferred_focus = nullptr;
        keyboard_config::cancel_composition();
        if (create) {
            std::string error;
            if (!create_blank_document(g_editor, error)) quick_open_show(g_editor, g_input_group, error);
            else focus_editor_textarea();
        } else quick_open_show(g_editor, g_input_group);
        return true;
    }

    if (platform::was_key_pressed(platform::KeyCode::Meta)) {
        settings_open(g_sidebar, g_editor);
        return true;
    }

    if (is_close_shortcut_pressed()) {
        // Dismiss just the current surface. With no surface open, these keys
        // open the sidebar instead of being delivered to the editor.
        if (g_sidebar.visible) handle_sidebar_keyboard();
        else if (g_search.active) handle_search_navigation();
        else { show_sidebar(g_sidebar, g_editor); focus_sidebar_search(); }
        return true;
    }

    if (g_sidebar.visible) {
        handle_sidebar_keyboard();
        if (settings_active()) return true;
    }

    if (ctrl) {
        if (platform::was_key_pressed(platform::KeyCode::B)) backup::request_now();
        if (platform::was_key_pressed(platform::KeyCode::A) && !g_sidebar.visible) {
            lv_obj_t* focused = lv_group_get_focused(g_input_group);
            if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                const char* text = lv_textarea_get_text(focused);
                uint32_t text_len = utf8_length(text);
                lv_textarea_set_cursor_pos(focused, 0);
                lv_obj_t* label = lv_textarea_get_label(focused);
                if (label) {
                    lv_label_set_text_selection_start(label, 0);
                    lv_label_set_text_selection_end(label, text_len);
                    lv_obj_invalidate(focused);
                }
            }
        }

        if (platform::was_key_pressed(platform::KeyCode::F) && !g_sidebar.visible) {
            if (!g_search.active) {
                g_search.active = true;
                lv_obj_clear_flag(g_editor.search_container, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(g_editor.word_count_label, LV_OBJ_FLAG_HIDDEN);
                focus_editor_search();
            } else {
                close_search(g_search);
                lv_obj_add_flag(g_editor.search_container, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(g_editor.word_count_label, LV_OBJ_FLAG_HIDDEN);
                clear_selection_local(g_editor.textarea);
                focus_editor_textarea();
            }
        }

        if (platform::was_key_pressed(platform::KeyCode::T)) {
            const int count=int(content::theme_choices().size());
            const int next=(content::active_theme_index(g_editor.dark_theme)+1)%count;
            if(!content::select_theme(next,g_editor.dark_theme))return true;
            content::tick();
            update_editor_theme(g_editor, g_editor.dark_theme);
            update_sidebar_theme(g_sidebar, g_editor.dark_theme);
            g_editor.state_pending_save = true;
            g_editor.state_change_time = lv_tick_get();
        }

        if (platform::was_key_pressed(platform::KeyCode::Equals) ||
            platform::was_key_pressed(platform::KeyCode::Minus)) {
            const int delta=platform::was_key_pressed(platform::KeyCode::Equals)?1:-1;
            const int choice=std::clamp(editor_font_choice(g_editor.font_size_index)+delta,0,3);
            g_editor.font_size_index=editor_font_indices[choice];
            update_editor_font(g_editor);
            g_editor.state_pending_save=true;
            g_editor.state_change_time=lv_tick_get();
        }
    }

    return false;
}

static void load_editor_state(EditorState& state, const char* state_file_path,
                              const char* default_file_path, std::string& out_file_path) {
    std::string current_file_path = default_file_path;
    int saved_cursor_pos = -1;
    int saved_font_size_index = 3;
    int saved_dark_theme = 1;

    std::ifstream state_in(state_file_path);
    if (state_in) {
        std::getline(state_in, current_file_path);
        state_in >> saved_cursor_pos;
        if (state_in >> saved_font_size_index) {
            if (saved_font_size_index < 0 || saved_font_size_index > 10) {
                saved_font_size_index = 3;
            }
        }
        state_in >> saved_dark_theme;
        state_in.close();
        if (current_file_path.empty()) {
            current_file_path = default_file_path;
        }
    }

    state.state_file_path = state_file_path;
    state.saved_cursor_pos = saved_cursor_pos;
    state.font_size_index = editor_font_index(saved_font_size_index);
    state.dark_theme = (saved_dark_theme == 0) ? 0 : 1;
    out_file_path = current_file_path;
}

bool init_app(App& app) {
    lv_init();

    if (!platform::init(app.window_width, app.window_height)) {
        fprintf(stderr, "Failed to initialize platform backend\n");
        lv_deinit();
        return false;
    }

    app.display = platform::get_display();
    app.keyboard_indev = platform::get_keyboard_indev();
    if (!app.display) {
        fprintf(stderr, "Failed to create platform display\n");
        platform::shutdown();
        lv_deinit();
        return false;
    }

    app.screen = lv_scr_act();

    app.editor = &g_editor;
    app.sidebar = &g_sidebar;
    app.search = &g_search;

    const char* user_files_dir = "/spiffs/user_files";
    const char* recently_deleted_dir = "/spiffs/user_files/recently_deleted";
    const char* state_file_path = "/spiffs/user_files/.editor_state";
    const char* default_file_path = "/spiffs/user_files/file 1.txt";

    ensure_directory(user_files_dir);
    ensure_directory(recently_deleted_dir);
    recover_document_saves(user_files_dir);
    ensure_shortcuts_file(user_files_dir);

    g_editor.user_files_dir = user_files_dir;
    g_editor.recently_deleted_dir = recently_deleted_dir;

    std::string file_to_load;
    load_editor_state(g_editor, state_file_path, default_file_path, file_to_load);
    load_collapsed_folders(state_file_path, g_sidebar.folder_data.collapsed_folders);

    create_editor_ui(g_editor, app.screen);
    start_editor_background_tasks(g_editor);
    set_search_input_callback(g_editor, search_input_event_cb, nullptr);
    create_sidebar_ui(g_sidebar, g_editor, app.screen);

    struct stat st;
    if (stat(file_to_load.c_str(), &st) != 0) {
        std::ofstream new_file(file_to_load);
        new_file.close();
    }

    load_file_into_editor(g_editor, file_to_load.c_str());

    content::init();
    content::tick();
    spellcheck::init();
    keyboard_config::load();
    serial_export::init();
    backup::init();

    // Age-based deletion requires a trusted RTC; the prototype clock resets each boot.

    return true;
}

void shutdown_app(App& app) {
    finish_sidebar_slide(g_sidebar);
    quick_open_close();
    backup::shutdown();
    (void)app;

    if (g_editor.content_pending_save) {
        save_editor_content(g_editor);
    }
    if (g_editor.state_pending_save) {
        save_editor_state(g_editor, g_sidebar.folder_data.collapsed_folders);
    }
    stop_editor_background_tasks(g_editor);

    if (g_input_group) {
        lv_group_delete(g_input_group);
        g_input_group = nullptr;
    }


    lv_deinit();
    platform::shutdown();
}

void run_app(App& app) {
    const uint32_t DEBOUNCE_DELAY = 500;
    const uint32_t RENAME_DEBOUNCE_DELAY = 1000;
    const uint32_t IDLE_WAIT_MS = 50;

    g_input_group = lv_group_create();
    app.input_group = g_input_group;

    lv_group_add_obj(g_input_group, g_editor.textarea);
    lv_group_add_obj(g_input_group, g_editor.search_input);
    lv_group_add_obj(g_input_group, g_sidebar.search_input);

    if (app.keyboard_indev) {
        lv_indev_set_group(app.keyboard_indev, g_input_group);
    }

    lv_group_focus_obj(g_editor.textarea);

    bool running = true;
    bool content_was_busy = false;
    uint32_t next_timer_wait = 0;

    while (running) {
        if (g_sidebar.animating && keyboard_input::backlog_high())
            finish_sidebar_slide(g_sidebar);
        if (!g_sidebar.animating && g_deferred_focus) {
            lv_group_focus_obj(g_deferred_focus);
            g_deferred_focus = nullptr;
        }
        // Check if sidebar is animating
        bool sidebar_animating = false;
        if (g_sidebar.animating) {
            uint32_t elapsed = lv_tick_get() - g_sidebar.anim_start_time;
            if (elapsed < SidebarState::ANIM_DURATION_MS + 250) {
                sidebar_animating = true;
            } else {
                finish_sidebar_slide(g_sidebar);
            }
        }

        // A watchdog completion must restore focus before replaying reports,
        // just like a normal completion on the preceding loop iteration.
        if (!g_sidebar.animating && g_deferred_focus) {
            lv_group_focus_obj(g_deferred_focus);
            g_deferred_focus = nullptr;
        }

        // Use short wait during animation, longer when idle
        uint32_t wait_time = sidebar_animating ? 1 : IDLE_WAIT_MS;
        // Wake for LVGL's next input/refresh timer instead of adding an idle
        // 50 ms delay when a redraw is already due.
        if (next_timer_wait < wait_time) wait_time = next_timer_wait;
        // Service bounded backup IO slices promptly while retaining input priority.
        if (backup::pending_storage()) wait_time = std::min<uint32_t>(wait_time, 1);
        if (spellcheck::suggestions_pending()) wait_time = std::min<uint32_t>(wait_time, 1);

        if(content_was_busy && !content::busy()){
            keyboard_input::discard_pending();
            platform::PlatformEvent discarded{};
            while(platform::poll_event(discarded,0)){}
            content_was_busy=false;
            settings_refresh();
        }
        if(content::busy()){
            content_was_busy=true;
            if(!settings_active())settings_open(g_sidebar,g_editor);
        }
        // Drain queued input before rendering; each key can generate several events.
        for (unsigned batch = 0; batch < 64 && !g_sidebar.animating; ++batch) {
            // Wait only for the first event in a batch.
            platform::PlatformEvent wait_event {};
            bool had_event = platform::poll_event(wait_event, batch == 0 ? wait_time : 0);
            if (had_event && !settings_capturing_binding() && backlight::handle_event(wait_event)) {
                keyboard_config::cancel_composition();
                backup::note_input();
                continue;
            }
            // Clear even if the release is consumed by a modal or the sidebar.
            if (had_event && wait_event.type == platform::PlatformEvent::Type::KeyUp &&
                wait_event.key == platform::KeyCode::Grave)
                g_suppress_close_shortcut_text = false;

            // Process the event we received
            const bool settings_was_active = settings_active();
            const bool tooltip_was_active = spellcheck::tooltip_active();
            const bool picker_was_active = quick_open_active();
            bool tooltip_consumed = false;
            if (had_event) {
                if (wait_event.type == platform::PlatformEvent::Type::KeyDown &&
                    wait_event.key == platform::KeyCode::Escape && wait_event.modifiers.meta &&
                    wait_event.modifiers.shift && wait_event.modifiers.alt) {
                    running = false;
                    break;
                }
                backup::note_input();
                bool consumed = spellcheck::handle_event(wait_event, g_editor,
                    !settings_was_active && !picker_was_active && !g_sidebar.visible && !g_search.active &&
                    lv_group_get_focused(g_input_group) == g_editor.textarea);
                tooltip_consumed = consumed;
                if (!consumed && picker_was_active) {
                    if (wait_event.type == platform::PlatformEvent::Type::KeyDown &&
                        wait_event.key == platform::KeyCode::Grave) g_suppress_close_shortcut_text = true;
                    quick_open_handle_event(wait_event);
                    consumed = true;
                }
                if (settings_was_active) {
                    settings_handle_event(wait_event, g_sidebar, g_editor);
                    if (!settings_active() && wait_event.type == platform::PlatformEvent::Type::KeyDown &&
                        wait_event.key == platform::KeyCode::Grave) {
                        g_suppress_close_shortcut_text = true;
                    }
                    consumed = true;
                }

                if (wait_event.type == platform::PlatformEvent::Type::Quit) {
                    running = false;
                    consumed = true;
                }

                if (!consumed && wait_event.type == platform::PlatformEvent::Type::KeyDown) {
                    if (is_close_shortcut_event(wait_event)) {
                        if (wait_event.key == platform::KeyCode::Grave)
                            g_suppress_close_shortcut_text = true;
                        consumed = true;
                    }
                    // Consume Ctrl+Arrow keys so LVGL doesn't also handle them
                    if (wait_event.modifiers.ctrl &&
                        (wait_event.key == platform::KeyCode::Up ||
                         wait_event.key == platform::KeyCode::Down ||
                         wait_event.key == platform::KeyCode::Left ||
                         wait_event.key == platform::KeyCode::Right)) {
                        consumed = true;
                    }
                    // Consume Alt+Arrow keys so LVGL doesn't also handle them
                    if (wait_event.modifiers.alt &&
                        (wait_event.key == platform::KeyCode::Left ||
                         wait_event.key == platform::KeyCode::Right)) {
                        consumed = true;
                    }
                    // Consume Ctrl+Backspace so LVGL doesn't also handle it
                    if (wait_event.modifiers.ctrl &&
                        wait_event.key == platform::KeyCode::Backspace) {
                        consumed = true;
                    }
                    // Handle backspace/delete with selection in editor (delete selected text)
                    if (!g_sidebar.visible &&
                        (wait_event.key == platform::KeyCode::Backspace || wait_event.key == platform::KeyCode::Delete) &&
                        !wait_event.modifiers.ctrl) {
                        lv_obj_t* focused = lv_group_get_focused(g_input_group);
                        if (focused == g_editor.textarea && has_selection(focused)) {
                            delete_selected_text(focused);
                            g_editor.content_pending_save = true;
                            g_editor.content_change_time = lv_tick_get();
                            update_word_count(g_editor);
                            consumed = true;
                        }
                    }
                    // When sidebar is visible, handle keyboard input ourselves
                    if (g_sidebar.visible) {
                        // If file dialog is active, handle input appropriately
                        if (g_sidebar.file_dialog_active) {
                            // When text field is selected (selection == 0), let LVGL handle text input
                            // but consume Enter and Delete keys to prevent sidebar actions
                            // Note: Backspace is NOT consumed so LVGL textarea can delete characters
                            if (g_sidebar.file_dialog_selection == 0) {
                                if (wait_event.key == platform::KeyCode::Enter ||
                                    wait_event.key == platform::KeyCode::Delete) {
                                    consumed = true;
                                }
                                // Don't consume other keys - let LVGL textarea handle input
                            } else {
                                // For dropdown and buttons, consume input
                                consumed = true;
                            }
                        } else {
                            // Handle backspace for search (renaming uses LVGL textarea)
                            if (wait_event.key == platform::KeyCode::Backspace) {
                                if (g_sidebar.searching) {
                                    handle_sidebar_backspace(g_sidebar, g_editor);
                                }
                            }
                            // Handle DELETE key for file deletion (also handle BACKSPACE on Mac when not searching/renaming)
                            if (wait_event.key == platform::KeyCode::Delete ||
                                (wait_event.key == platform::KeyCode::Backspace && !g_sidebar.searching && !g_sidebar.renaming)) {
                                if (!g_sidebar.searching && !g_sidebar.renaming &&
                                    !g_sidebar.confirm_delete && !g_sidebar.confirm_restore) {
                                    g_sidebar.folder_data.build_display_items(g_sidebar.searching);
                                    int display_count = (int)g_sidebar.folder_data.display_items.size();
                                    if (!g_sidebar.new_file_selected && !g_sidebar.new_folder_selected &&
                                        g_sidebar.selected_index >= 0 &&
                                        g_sidebar.selected_index < display_count) {
                                        show_delete_dialog(g_sidebar, g_editor, g_sidebar.selected_index);
                                    }
                                }
                            }
                            // Consume keyboard events when sidebar is visible, except when renaming
                            // (rename textarea needs to receive input from LVGL)
                            if (!g_sidebar.renaming) {
                                consumed = true;
                            }
                        }
                    }
                }

                // Also consume key up events when sidebar is visible
                if (!consumed &&
                    wait_event.type == platform::PlatformEvent::Type::KeyUp &&
                    g_sidebar.visible) {
                    consumed = true;
                }

                // Handle text input - intercept when sidebar is visible
                if (!consumed && wait_event.type == platform::PlatformEvent::Type::TextInput) {
                    if (g_suppress_close_shortcut_text && wait_event.hid_usage == 0x35) {
                        g_suppress_close_shortcut_text = false;
                        consumed = true;
                    } else if (g_sidebar.visible && (g_sidebar.searching || g_sidebar.renaming)) {
                        handle_sidebar_text_input(g_sidebar, g_editor, wait_event.text);
                        consumed = true;
                    } else {
                        lv_obj_t* focused = lv_group_get_focused(g_input_group);
                        if (focused && lv_obj_check_type(focused, &lv_textarea_class) && wait_event.text[0] != '\0') {
                            size_t max_bytes = (focused == g_editor.textarea) ? static_cast<size_t>(TEXT_BUFFER_SIZE - 1) : 0;
                            if (textarea_add_text_with_limit(focused, wait_event.text, max_bytes) &&
                                focused == g_editor.textarea) {
                                g_editor.content_pending_save = true;
                                g_editor.content_change_time = lv_tick_get();
                                update_word_count(g_editor);
                            }
                        }
                        consumed = true;
                    }
                } else if (!consumed &&
                           wait_event.type == platform::PlatformEvent::Type::KeyUp &&
                           wait_event.key == platform::KeyCode::Grave) {
                    g_suppress_close_shortcut_text = false;
                }

                // A remapped navigation or Enter key belongs to the action,
                // not to the LVGL widget underneath it.
                if (wait_event.shortcut) consumed = true;
                // Push event back if not consumed, so LVGL can process it
                if (!consumed) {
                    platform::requeue_last_event();
                }
            }

            if (!quick_open_active() && !picker_was_active && !settings_active() && !settings_was_active && !tooltip_was_active && !tooltip_consumed && !spellcheck::tooltip_active()) {
                if (!handle_shortcuts()) {
                    handle_search_navigation();
                    handle_text_selection();
                    handle_clipboard();
                }
            }
            if (!had_event || !running) break;
        }

        content_was_busy = content_was_busy || content::busy();
        static bool cursor_suppressed = false;
        const bool suppress_cursor = g_sidebar.visible || g_sidebar.animating || settings_active();
        if (g_editor.textarea && suppress_cursor != cursor_suppressed) {
            cursor_suppressed = suppress_cursor;
            lv_obj_set_style_border_width(g_editor.textarea, suppress_cursor ? 0 : 2, LV_PART_CURSOR);
            lv_obj_set_style_border_width(g_editor.textarea, suppress_cursor ? 0 : 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
        }
        // Reports remain queued, undecoded and in order. Disable native LVGL
        // key repeat as well so a previously held key cannot edit frozen UI.
        if (g_sidebar.animating && keyboard_input::backlog_high()) finish_sidebar_slide(g_sidebar);
        const bool slide_was_animating = g_sidebar.animating;
        if (app.keyboard_indev) lv_indev_enable(app.keyboard_indev, !g_sidebar.animating && !content::busy());
        if (!g_sidebar.animating) update_editor_backup_progress(g_editor);
        next_timer_wait = lv_timer_handler();
        update_sidebar_slide(g_sidebar);
        if (app.keyboard_indev) lv_indev_enable(app.keyboard_indev, !g_sidebar.animating && !content::busy());
        // Complete the sample after the final animated position was presented.
        if (!g_sidebar.animating) platform::end_ui_animation();
        platform::pump_events();
        if (g_sidebar.animating || slide_was_animating) {
            // Network work continues on its worker; service bounded storage
            // requests without changing the live widgets behind the snapshots.
            // On completion, replay queued keys before accepting USB UI commands.
            backup::tick(g_editor);
            continue;
        }

        if (serial_export::requested()) {
            lv_obj_t* overlay = lv_obj_create(app.screen);
            lv_obj_set_size(overlay, app.window_width, app.window_height);
            lv_obj_center(overlay);
            lv_obj_t* message = lv_label_create(overlay);
            lv_label_set_text(message, "Copying documents to computer...\nPlease wait before typing or unplugging USB.");
            lv_obj_center(message);
            lv_timer_handler();
            serial_export::send_documents(g_editor.user_files_dir.c_str(), flush_editor_content(g_editor));
            keyboard_input::discard_pending();
            lv_obj_delete(overlay);
        }
        static unsigned content_revision = ~0u;
        content::tick();
        const auto revision = content::revision();
        if (content_revision != revision) {
            content_revision = revision;
            update_editor_font(g_editor);
            update_editor_theme(g_editor, g_editor.dark_theme);
            update_sidebar_theme(g_sidebar, g_editor.dark_theme);
        }
        spellcheck::update(g_editor, !quick_open_active() && !settings_active() && !g_sidebar.visible && !g_search.active);
        process_pending_word_count(g_editor, 120);

        process_pending_saves(g_editor, DEBOUNCE_DELAY, g_sidebar.folder_data.collapsed_folders);
        process_rename_save(g_sidebar, g_editor, RENAME_DEBOUNCE_DELAY);
        backup::tick(g_editor);
    }
}
