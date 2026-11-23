#include "quick_open.h"
#include "quick_file_index.h"
#include "backup/backup.h"
#include "editor.h"
#include "international_font.h"
#include "textarea_utils.h"
#include "utf8_positions.h"
#include "theme.h"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr size_t visible_rows = 6;
struct Picker {
    EditorState* editor = nullptr;
    lv_group_t* group = nullptr;
    lv_obj_t* overlay = nullptr;
    lv_obj_t* input = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* rows[visible_rows] {};
    std::vector<quick_files::Entry> files;
    std::vector<size_t> matches;
    size_t selected = 0;
    bool scan_ok = true;
} picker;

void render_rows() {
    const Theme& theme = get_theme(picker.editor->dark_theme);
    const size_t first = picker.selected >= visible_rows ? picker.selected - visible_rows + 1 : 0;
    for (size_t row = 0; row < visible_rows; ++row) {
        auto* label = picker.rows[row];
        const size_t index = first + row;
        if (index >= picker.matches.size()) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        const auto& entry = picker.files[picker.matches[index]];
        std::string name = entry.path.substr(0, entry.path.size() - 4);
        lv_label_set_text(label, name.c_str());
        lv_obj_set_style_bg_color(label, theme.sidebar_selected, 0);
        lv_obj_set_style_bg_opa(label, index == picker.selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (picker.matches.empty()) lv_label_set_text(picker.status, "No matching files");
    else lv_label_set_text_fmt(picker.status, "%u of %u  |  Most recently modified first",
                              unsigned(picker.selected + 1), unsigned(picker.matches.size()));
    if (!picker.scan_ok) lv_label_set_text(picker.status, "Some folders could not be read");
}
void filter_changed(lv_event_t*) {
    picker.matches = quick_files::filter(picker.files, lv_textarea_get_text(picker.input));
    picker.selected = 0;
    render_rows();
}
lv_obj_t* label(lv_obj_t* parent, const char* text, int y, int size, lv_color_t color) {
    auto* result = lv_label_create(parent);
    lv_label_set_text(result, text);
    lv_obj_set_style_text_font(result, get_ui_font(size), 0);
    lv_obj_set_style_text_color(result, color, 0);
    lv_obj_set_pos(result, 0, y);
    return result;
}
} // namespace

bool create_blank_document(EditorState& editor, std::string& error) {
    if (!flush_editor_content(editor)) { error = "Could not save current file. Please try again."; return false; }
    backup::before_storage_mutation();
    for (unsigned n = 0; n < 10000; ++n) {
        const std::string path = editor.user_files_dir + "/" + quick_files::new_name(n);
        struct stat st {};
        if (stat(path.c_str(), &st) == 0 || stat((path + ".tmp").c_str(), &st) == 0 ||
            stat((path + ".bak").c_str(), &st) == 0) continue;
        if (!backup::mark_document_dirty(path)) { error = "Could not prepare new file for saving."; return false; }
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            error = "Could not create new file. Check available storage.";
            return false;
        }
        if (close(fd) != 0 || !load_file_into_editor(editor, path.c_str())) {
            error = "New file could not be opened.";
            return false;
        }
        lv_textarea_set_cursor_pos(editor.textarea, 0);
        editor.state_pending_save = true;
        editor.state_change_time = lv_tick_get();
        return true;
    }
    error = "No available New File name.";
    return false;
}

bool quick_open_active() { return picker.overlay != nullptr; }
void quick_open_show(EditorState& editor, lv_group_t* group, const std::string& error) {
    if (quick_open_active()) return;
    picker.editor = &editor;
    picker.group = group;
    // Flush first so the active document has its current modification time.
    const bool saved = flush_editor_content(editor);
    picker.scan_ok = quick_files::scan(editor.user_files_dir, picker.files);
    quick_files::sort_recent(picker.files);
    const Theme& theme = get_theme(editor.dark_theme);
    picker.overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(picker.overlay);
    lv_obj_set_size(picker.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(picker.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(picker.overlay, LV_OPA_60, 0);
    lv_obj_remove_flag(picker.overlay, LV_OBJ_FLAG_SCROLLABLE);
    auto* panel = lv_obj_create(picker.overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 620, 414);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, theme.sidebar_bg, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    label(panel, "Open file", 0, 22, theme.text);
    picker.input = lv_textarea_create(panel);
    lv_obj_set_pos(picker.input, 0, 36);
    lv_obj_set_size(picker.input, 580, 40);
    lv_textarea_set_one_line(picker.input, true);
    lv_textarea_set_text_selection(picker.input, true);
    lv_textarea_set_max_length(picker.input, 255);
    lv_textarea_set_placeholder_text(picker.input, "Search file or folder name...");
    lv_obj_set_style_text_font(picker.input, get_ui_font(18), 0);
    lv_obj_set_style_text_color(picker.input, theme.search_input_text, 0);
    lv_obj_set_style_bg_color(picker.input, theme.search_input_bg, 0);
    lv_obj_set_style_border_color(picker.input, theme.text_dim, 0);
    lv_obj_set_style_border_width(picker.input, 1, 0);
    lv_obj_set_style_border_color(picker.input, theme.cursor, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(picker.input, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(picker.input, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(picker.input, theme.cursor, LV_PART_CURSOR);
    picker.status = label(panel, "", 84, 14, theme.text_dim);
    lv_obj_set_width(picker.status, 580);
    lv_label_set_long_mode(picker.status, LV_LABEL_LONG_DOT);
    for (size_t i = 0; i < visible_rows; ++i) {
        picker.rows[i] = label(panel, "", 112 + int(i) * 35, 18, theme.text);
        lv_obj_set_size(picker.rows[i], 580, 32);
        lv_label_set_long_mode(picker.rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_pad_hor(picker.rows[i], 8, 0);
        lv_obj_set_style_pad_ver(picker.rows[i], 5, 0);
        lv_obj_set_style_radius(picker.rows[i], 4, 0);
    }
    label(panel, "Up/Down: select    Enter: open    Esc / `: close", 344, 14, theme.text_dim);
    lv_obj_add_event_cb(picker.input, filter_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    filter_changed(nullptr);
    if (!error.empty()) lv_label_set_text(picker.status, error.c_str());
    else if (!saved) lv_label_set_text(picker.status, "Could not save current file. Opening is blocked until saved.");
    lv_group_add_obj(group, picker.input);
    lv_group_focus_obj(picker.input);
}
void quick_open_close() {
    if (!quick_open_active()) return;
    auto* editor = picker.editor;
    lv_obj_delete(picker.overlay);
    picker = Picker{};
    lv_group_focus_obj(editor->textarea);
}
void quick_open_handle_event(const platform::PlatformEvent& event) {
    using T = platform::PlatformEvent::Type;
    using K = platform::KeyCode;
    if (event.type == T::TextInput) {
        if (!event.modifiers.ctrl && !event.modifiers.meta)
            textarea_add_text_with_limit(picker.input, event.text, 1024);
        return;
    }
    if (event.type != T::KeyDown) return;
    if (event.key == K::Escape || event.key == K::Grave) { quick_open_close(); return; }
    if (event.shortcut && event.key == K::A) {
        auto* text = lv_textarea_get_label(picker.input);
        lv_label_set_text_selection_start(text, 0);
        lv_label_set_text_selection_end(text, utf8_length(lv_textarea_get_text(picker.input)));
        lv_obj_invalidate(picker.input);
        return;
    }
    if (event.modifiers.ctrl || event.modifiers.meta || event.modifiers.alt) return;
    switch (event.key) {
    case K::Down:
        if (picker.selected + 1 < picker.matches.size()) ++picker.selected;
        render_rows();
        break;
    case K::Up:
        if (picker.selected) --picker.selected;
        render_rows();
        break;
    case K::Enter:
        if (!picker.matches.empty()) {
            const auto path = picker.editor->user_files_dir + "/" + picker.files[picker.matches[picker.selected]].path;
            if (!flush_editor_content(*picker.editor)) {
                lv_label_set_text(picker.status, "Could not save current file. Please try again.");
            } else if (!load_file_into_editor(*picker.editor, path.c_str())) {
                lv_label_set_text(picker.status, "Could not open file. It may be missing or too large.");
            } else {
                picker.editor->state_pending_save = true;
                picker.editor->state_change_time = lv_tick_get();
                quick_open_close();
            }
        }
        break;
    case K::Backspace:
        if (!textarea_delete_selected_text(picker.input)) lv_textarea_delete_char(picker.input);
        break;
    case K::Delete:
        if (!textarea_delete_selected_text(picker.input)) lv_textarea_delete_char_forward(picker.input);
        break;
    case K::Left: lv_textarea_cursor_left(picker.input); break;
    case K::Right: lv_textarea_cursor_right(picker.input); break;
    case K::Home: lv_textarea_set_cursor_pos(picker.input, 0); break;
    case K::End: lv_textarea_set_cursor_pos(picker.input, LV_TEXTAREA_CURSOR_LAST); break;
    default: break;
    }
}
