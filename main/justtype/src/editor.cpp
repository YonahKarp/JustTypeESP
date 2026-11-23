#include "backup/backup.h"
#include "international_font.h"
#include "editor.h"
#include "file_manager.h"
#include "editor_word_count.h"
#include "theme.h"
#include "search.h"
#include <utility>
#include <cstring>
#include <fstream>
#include <cstdio>

static const lv_font_t* get_font_for_size_index(int index) {
    static const lv_font_t* fonts[] = {
        get_ui_font(18),
        get_ui_font(20),
        get_ui_font(22),
        get_ui_font(24),
        get_ui_font(26),
        get_ui_font(28),
        get_ui_font(30),
        get_ui_font(32),
        get_ui_font(36),
        get_ui_font(40),
    };
    constexpr int num_fonts = sizeof(fonts) / sizeof(fonts[0]);
    if (index < 0) index = 0;
    if (index >= num_fonts) index = num_fonts - 1;
    return fonts[index];
}

static void textarea_event_cb(lv_event_t* e) {
    EditorState* state = static_cast<EditorState*>(lv_event_get_user_data(e));
    if (!state) return;
    if (state->suppress_change_tracking) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        state->content_pending_save = true;
        state->content_change_time = lv_tick_get();
        update_word_count(*state);
    }
}

void create_editor_ui(EditorState& state, lv_obj_t* parent) {
    const Theme& theme = get_theme(state.dark_theme);
    
    lv_obj_set_style_bg_color(parent, theme.background, 0);
    
    lv_obj_t* main_container = lv_obj_create(parent);
    lv_obj_remove_style_all(main_container);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_container, theme.background, 0);
    lv_obj_set_style_bg_opa(main_container, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(main_container, 10, 0);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    state.status_bar = lv_obj_create(main_container);
    lv_obj_remove_style_all(state.status_bar);
    lv_obj_set_size(state.status_bar, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(state.status_bar, theme.background, 0);
    lv_obj_set_style_bg_opa(state.status_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(state.status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state.status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(state.status_bar, 5, 0);
    
    state.filename_label = lv_label_create(state.status_bar);
    lv_label_set_text(state.filename_label, "Untitled");
    lv_obj_set_style_text_color(state.filename_label, theme.text_dim, 0);
    lv_obj_set_style_text_font(state.filename_label, get_ui_font(18), 0);
    
    state.search_container = lv_obj_create(state.status_bar);
    lv_obj_remove_style_all(state.search_container);
    lv_obj_set_size(state.search_container, 250, 26);
    lv_obj_set_style_bg_color(state.search_container, theme.search_input_bg, 0);
    lv_obj_set_style_bg_opa(state.search_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state.search_container, 3, 0);
    lv_obj_set_flex_flow(state.search_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state.search_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(state.search_container, 8, 0);
    lv_obj_add_flag(state.search_container, LV_OBJ_FLAG_HIDDEN);
    
    create_magnifying_glass_icon(state.search_icon, state.search_container, 16, theme.text_dim);
    
    state.search_input = lv_textarea_create(state.search_container);
    lv_obj_set_size(state.search_input, 160, 22);
    lv_textarea_set_one_line(state.search_input, true);
    lv_textarea_set_placeholder_text(state.search_input, "Find...");
    lv_textarea_set_text_selection(state.search_input, true);
    lv_obj_set_style_bg_opa(state.search_input, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.search_input, 0, 0);
    lv_obj_set_style_border_width(state.search_input, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(state.search_input, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(state.search_input, 0, 0);
    lv_obj_set_style_outline_width(state.search_input, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(state.search_input, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(state.search_input, theme.search_input_text, 0);
    lv_obj_set_style_text_font(state.search_input, get_ui_font(16), 0);
    lv_obj_set_style_border_color(state.search_input, theme.search_input_text, LV_PART_CURSOR);
    lv_obj_set_style_border_color(state.search_input, theme.search_input_text, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(state.search_input, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(state.search_input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(state.search_input, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(state.search_input, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(state.search_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(state.search_input, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    
    state.match_label = lv_label_create(state.search_container);
    lv_label_set_text(state.match_label, "");
    lv_obj_set_style_text_color(state.match_label, theme.text_dim, 0);
    lv_obj_set_style_text_font(state.match_label, get_ui_font(14), 0);
    
    state.word_count_label = lv_label_create(state.status_bar);
    lv_label_set_text(state.word_count_label, "0 words");
    lv_obj_set_style_text_color(state.word_count_label, theme.text_dim, 0);
    lv_obj_set_style_text_font(state.word_count_label, get_ui_font(18), 0);

    state.backup_progress_container = lv_obj_create(state.status_bar);
    lv_obj_remove_style_all(state.backup_progress_container);
    lv_obj_set_size(state.backup_progress_container, 120, 26);
    lv_obj_remove_flag(state.backup_progress_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(state.backup_progress_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(state.backup_progress_container, LV_OBJ_FLAG_HIDDEN);
    state.backup_progress_label = lv_label_create(state.backup_progress_container);
    lv_obj_set_style_text_font(state.backup_progress_label, get_ui_font(14), 0);
    lv_obj_set_style_text_color(state.backup_progress_label, theme.text_dim, 0);
    lv_label_set_text(state.backup_progress_label, "Checking...");
    lv_obj_align(state.backup_progress_label, LV_ALIGN_TOP_MID, 0, 0);
    state.backup_progress_bar = lv_bar_create(state.backup_progress_container);
    lv_obj_set_size(state.backup_progress_bar, 120, 4);
    lv_obj_align(state.backup_progress_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(state.backup_progress_bar, 0, 100);
    lv_obj_set_style_bg_color(state.backup_progress_bar, theme.sidebar_btn_hover, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state.backup_progress_bar, theme.battery_good, LV_PART_INDICATOR);
    
    lv_color_t fill_color = state.battery.percent > 20 ? theme.battery_good : theme.battery_low;
    create_battery_icon(state.battery, state.status_bar, state.battery.percent, theme.battery_border, fill_color);
    
    state.textarea = lv_textarea_create(main_container);
    lv_obj_set_size(state.textarea, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(state.textarea, 1);
    lv_textarea_set_placeholder_text(state.textarea, "Start typing...");
    lv_textarea_set_accepted_chars(state.textarea, NULL);
    lv_textarea_set_text_selection(state.textarea, true);
    
    lv_obj_set_style_bg_color(state.textarea, theme.edit_bg, 0);
    lv_obj_set_style_text_color(state.textarea, theme.edit_text, 0);
    lv_obj_set_style_text_font(state.textarea, get_font_for_size_index(state.font_size_index), 0);
    lv_obj_set_style_border_width(state.textarea, 0, 0);
    lv_obj_set_style_border_width(state.textarea, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(state.textarea, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(state.textarea, 0, 0);
    lv_obj_set_style_outline_width(state.textarea, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(state.textarea, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(state.textarea, 0, 0);
    lv_obj_set_style_pad_all(state.textarea, 10, 0);
    
    lv_obj_set_style_anim_duration(state.textarea, 500, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(state.textarea, theme.edit_text, LV_PART_CURSOR);
    lv_obj_set_style_border_color(state.textarea, theme.edit_text, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(state.textarea, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(state.textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(state.textarea, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(state.textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(state.textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(state.textarea, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    
    lv_obj_set_style_bg_color(state.textarea, theme.scrollbar_bg, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(state.textarea, theme.scrollbar_cursor, LV_PART_SCROLLBAR | LV_STATE_SCROLLED);
    
    lv_obj_t* label = lv_textarea_get_label(state.textarea);
    if (label) {
        lv_obj_set_style_bg_color(label, theme.selection, LV_PART_SELECTED);
        lv_obj_set_style_text_color(label, theme.edit_text, LV_PART_SELECTED);
    }
    
    lv_obj_add_event_cb(state.textarea, textarea_event_cb, LV_EVENT_VALUE_CHANGED, &state);
    
    lv_obj_remove_flag(state.textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(state.textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

void update_editor_backup_progress(EditorState& state) {
    if (!state.backup_progress_container) return;
    const auto progress = backup::progress();
    if (!progress.active) {
        if (state.backup_progress_visible) {
            lv_obj_add_flag(state.backup_progress_container, LV_OBJ_FLAG_HIDDEN);
            if (lv_obj_has_flag(state.search_container, LV_OBJ_FLAG_HIDDEN))
                lv_obj_remove_flag(state.word_count_label, LV_OBJ_FLAG_HIDDEN);
            state.backup_progress_visible = false;
        }
        return;
    }
    if (!state.backup_progress_visible) {
        lv_obj_remove_flag(state.backup_progress_container, LV_OBJ_FLAG_HIDDEN);
        state.backup_progress_visible = true;
        state.backup_progress_percent = -2;
        state.backup_progress_phase = -1;
    }
    // Search shortcuts can also change this flag; keep the count out of the
    // same slot while the progress widget is visible. Its cached count updates.
    lv_obj_add_flag(state.word_count_label, LV_OBJ_FLAG_HIDDEN);
    const int phase = static_cast<int>(progress.phase);
    if (progress.percent != state.backup_progress_percent || phase != state.backup_progress_phase) {
        using P = backup::ProgressPhase;
        if (progress.phase == P::Checking) lv_label_set_text(state.backup_progress_label, "Checking...");
        else if (progress.phase == P::Connecting) lv_label_set_text(state.backup_progress_label, "Connecting...");
        else if (progress.phase == P::Finishing) lv_label_set_text(state.backup_progress_label, "Finishing...");
        else lv_label_set_text_fmt(state.backup_progress_label, "%s %d%%",
                                  progress.verification ? "Verify" : "Backup", progress.percent);
        lv_bar_set_mode(state.backup_progress_bar, progress.percent < 0 ? LV_BAR_MODE_RANGE : LV_BAR_MODE_NORMAL);
        if (progress.percent >= 0) lv_bar_set_value(state.backup_progress_bar, progress.percent, LV_ANIM_OFF);
        state.backup_progress_percent = progress.percent;
        state.backup_progress_phase = phase;
    }
    // Unknown scan length uses a moving segment, not a fabricated percentage.
    const uint32_t now = lv_tick_get();
    if (progress.percent < 0 && now - state.backup_progress_tick >= 100) {
        state.backup_progress_tick = now;
        unsigned position = (now / 20) % 160;
        if (position > 80) position = 160 - position;
        lv_bar_set_start_value(state.backup_progress_bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(state.backup_progress_bar, position + 20, LV_ANIM_OFF);
        lv_bar_set_start_value(state.backup_progress_bar, position, LV_ANIM_OFF);
    }
}

bool load_file_into_editor(EditorState& state, const char* file_path) {
    // Validate and read before changing the active document or its dirty state.
    std::ifstream infile(file_path, std::ios::binary | std::ios::ate);
    if (!infile) return false;
    const auto size = infile.tellg();
    if (size < 0 || size > TEXT_BUFFER_SIZE - 1) {
        fprintf(stderr, "Document exceeds editor size limit: %s\n", file_path);
        return false;
    }
    std::string content(static_cast<size_t>(size), '\0');
    infile.seekg(0);
    if (size > 0 && !infile.read(content.data(), static_cast<std::streamsize>(size))) return false;
    state.current_file_path = file_path;
    state.temp_file_path = std::string(file_path) + ".tmp";
    state.suppress_change_tracking = true;
    lv_textarea_set_text(state.textarea, content.c_str());
    show_pending_word_count(state);
    request_word_count(state, std::move(content));

    update_filename_display(state);
    
    if (state.saved_cursor_pos >= 0) {
        lv_textarea_set_cursor_pos(state.textarea, state.saved_cursor_pos);
        state.saved_cursor_pos = -1;
    }

    state.suppress_change_tracking = false;
    state.content_pending_save = false;
    state.content_change_time = 0;
    return true;
}

bool save_editor_content(EditorState& state) {
    backup::before_storage_mutation();
    // Don't save if no file is open (e.g., after deleting the current file)
    if (state.current_file_path.empty() || state.temp_file_path.empty()) {
        return false;
    }
    
    const char* text = lv_textarea_get_text(state.textarea);
    
    std::ofstream outfile(state.temp_file_path, std::ios::binary | std::ios::trunc);
    if (!outfile) return false;
    outfile.write(text, strlen(text));
    outfile.close();
    if (!outfile) return false;
    return replace_saved_file(state.temp_file_path, state.current_file_path);
}

bool flush_editor_content(EditorState& state) {
    if (!state.content_pending_save) return true;
    if (!save_editor_content(state)) {
        fprintf(stderr, "Document save failed: %s\n", state.current_file_path.c_str());
        return false;
    }
    state.content_pending_save = false;
    return true;
}

bool save_editor_state(EditorState& state, const std::set<std::string>& collapsed_folders) {
    // Hidden UI metadata does not mutate the document being staged for backup.
    std::string temp_state_path = state.state_file_path + ".tmp";
    std::ofstream state_out(temp_state_path);
    if (state_out) {
        state_out << state.current_file_path << "\n";
        state_out << lv_textarea_get_cursor_pos(state.textarea) << "\n";
        state_out << state.font_size_index << "\n";
        state_out << state.dark_theme << "\n";
        state_out << collapsed_folders.size() << "\n";
        for (const auto& folder : collapsed_folders) {
            state_out << folder << "\n";
        }
        state_out.close();
        if (state_out) return replace_saved_file(temp_state_path, state.state_file_path);
    }
    return false;
}

void load_collapsed_folders(const std::string& state_file_path, std::set<std::string>& collapsed_folders) {
    collapsed_folders.clear();
    std::ifstream state_in(state_file_path);
    if (!state_in) return;
    
    std::string line;
    std::getline(state_in, line);  // current_file_path
    std::getline(state_in, line);  // cursor_pos
    std::getline(state_in, line);  // font_size_index
    std::getline(state_in, line);  // dark_theme
    
    int folder_count = 0;
    if (state_in >> folder_count) {
        state_in.ignore();  // skip newline
        for (int i = 0; i < folder_count; i++) {
            if (std::getline(state_in, line) && !line.empty()) {
                collapsed_folders.insert(line);
            }
        }
    }
}

void update_editor_theme(EditorState& state, int dark) {
    state.dark_theme = dark;
    const Theme& theme = get_theme(dark);
    
    lv_obj_t* main_container = state.textarea ? lv_obj_get_parent(state.textarea) : nullptr;
    if (main_container) {
        lv_obj_set_style_bg_color(main_container, theme.background, 0);
        lv_obj_t* screen = lv_obj_get_parent(main_container);
        if (screen) lv_obj_set_style_bg_color(screen, theme.background, 0);
    }
    
    if (state.status_bar) {
        lv_obj_set_style_bg_color(state.status_bar, theme.background, 0);
    }
    
    if (state.filename_label) {
        lv_obj_set_style_text_color(state.filename_label, theme.text_dim, 0);
    }
    
    if (state.word_count_label) {
        lv_obj_set_style_text_color(state.word_count_label, theme.text_dim, 0);
    }
    if (state.backup_progress_bar) {
        lv_obj_set_style_text_color(state.backup_progress_label, theme.text_dim, 0);
        lv_obj_set_style_bg_color(state.backup_progress_bar, theme.sidebar_btn_hover, LV_PART_MAIN);
        lv_obj_set_style_bg_color(state.backup_progress_bar, theme.battery_good, LV_PART_INDICATOR);
    }
    
    if (state.textarea) {
        lv_obj_set_style_bg_color(state.textarea, theme.edit_bg, 0);
        lv_obj_set_style_text_color(state.textarea, theme.edit_text, 0);
        lv_obj_set_style_border_color(state.textarea, theme.edit_text, LV_PART_CURSOR);
        lv_obj_set_style_border_color(state.textarea, theme.edit_text, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(state.textarea, LV_OPA_COVER, LV_PART_CURSOR);
        lv_obj_set_style_border_opa(state.textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(state.textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(state.textarea, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(state.textarea, theme.scrollbar_bg, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(state.textarea, theme.scrollbar_cursor, LV_PART_SCROLLBAR | LV_STATE_SCROLLED);
        
        lv_obj_t* label = lv_textarea_get_label(state.textarea);
        if (label) {
            lv_obj_set_style_bg_color(label, theme.selection, LV_PART_SELECTED);
            lv_obj_set_style_text_color(label, theme.edit_text, LV_PART_SELECTED);
        }
    }
    
    if (state.search_container) {
        lv_obj_set_style_bg_color(state.search_container, theme.search_input_bg, 0);
    }
    
    if (state.search_input) {
        lv_obj_set_style_text_color(state.search_input, theme.search_input_text, 0);
        lv_obj_set_style_border_color(state.search_input, theme.search_input_text, LV_PART_CURSOR);
        lv_obj_set_style_border_color(state.search_input, theme.search_input_text, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(state.search_input, LV_OPA_COVER, LV_PART_CURSOR);
        lv_obj_set_style_border_opa(state.search_input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(state.search_input, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(state.search_input, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    }
    
    if (state.search_icon.canvas) {
        update_magnifying_glass_icon_color(state.search_icon, theme.text_dim);
    }
    
    if (state.battery.body) {
        lv_color_t fill_color = state.battery.percent > 20 ? theme.battery_good : theme.battery_low;
        update_battery_icon_theme(state.battery, theme.battery_border, fill_color);
    }
}

void update_filename_display(EditorState& state) {
    std::string display_name;
    
    std::string user_files_dir = state.user_files_dir;
    if (state.current_file_path.find(user_files_dir) == 0) {
        std::string relative = state.current_file_path.substr(user_files_dir.size());
        if (!relative.empty() && relative[0] == '/') {
            relative = relative.substr(1);
        }
        
        size_t slash_pos = relative.find('/');
        if (slash_pos != std::string::npos) {
            std::string folder = relative.substr(0, slash_pos);
            std::string filename = relative.substr(slash_pos + 1);
            
            if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".txt") {
                filename = filename.substr(0, filename.size() - 4);
            }
            
            display_name = folder + " / " + filename;
        } else {
            display_name = display_document_name(relative, "");
        }
    } else {
        size_t last_slash = state.current_file_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            display_name = state.current_file_path.substr(last_slash + 1);
        } else {
            display_name = state.current_file_path;
        }
        
        if (display_name.size() > 4 && display_name.substr(display_name.size() - 4) == ".txt") {
            display_name = display_name.substr(0, display_name.size() - 4);
        }
    }
    
    lv_label_set_text(state.filename_label, display_name.c_str());
}

void process_pending_saves(EditorState& state, uint32_t debounce_delay, const std::set<std::string>& collapsed_folders) {
    uint32_t now = lv_tick_get();
    if (state.content_pending_save && (now - state.content_change_time >= debounce_delay)) {
        if (save_editor_content(state)) state.content_pending_save = false;
        else state.content_change_time = now; // Back off instead of retrying every UI iteration.
    }
    if (state.state_pending_save && (now - state.state_change_time >= debounce_delay)) {
        if (save_editor_state(state, collapsed_folders)) state.state_pending_save = false;
        else state.state_change_time = now;
    }
}

void set_search_input_callback(EditorState& state, lv_event_cb_t cb, void* user_data) {
    if (state.search_input) {
        lv_obj_add_event_cb(state.search_input, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
}

void update_battery_display(EditorState& state) {
    const Theme& theme = get_theme(state.dark_theme);
    
    lv_color_t fill_color = state.battery.percent > 20 ? theme.battery_good : theme.battery_low;
    update_battery_icon(state.battery, state.battery.percent, fill_color);
}

void update_editor_font(EditorState& state) {
    if (state.textarea) lv_obj_set_style_text_font(state.textarea, get_font_for_size_index(state.font_size_index), 0);
}
