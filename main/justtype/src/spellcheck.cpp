#include "spellcheck.h"
#include "content.h"
#include "spelling/unicode_word.h"
#include "editor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "file_manager.h"
#include "international_font.h"
#include "spelling/spellcheck_core.h"
#include "spelling/tooltip_input.h"
#include "spelling/suggestions.h"
#include "spelling/viewport_checker.h"
#include "src/misc/lv_text_private.h"
#include "theme.h"
#include "utf8_positions.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <new>
#include <set>
#include <sys/stat.h>
namespace spellcheck {
namespace {
bool on = true;
std::set<std::string> personal;
std::string word;
lv_obj_t *text_label = nullptr;
lv_obj_t *textarea = nullptr;
using Buffers = std::array<spelling::ViewportChecker, 2>;
Buffers *buffers = nullptr;
size_t published = 0;
bool scan_complete = true, job_full_check = false, full_check_pending = true;
bool pending_edit = false, edit_delimiter = false;
uint32_t edit_start = 0, edit_end = 0, edit_inserted = 0;
spelling::ViewportChecker &front() { return (*buffers)[published]; }
spelling::ViewportChecker &worker() { return (*buffers)[1 - published]; }
bool document_dirty = true, viewport_dirty = true, show_annotations = false;
uint32_t last_activity = 0;
int theme_id = 1;
lv_obj_t *tooltip = nullptr;
lv_obj_t *tooltip_hint = nullptr;
std::string tooltip_word;
spelling::TooltipInput tooltip_input;
spelling::Suggestions suggestions;
std::string suggestion_word;
size_t tooltip_start = 0, tooltip_end = 0;
unsigned tooltip_selection = 0;
void update_tooltip_options() {
    if (!tooltip_hint) return;
    std::string options;
    const auto& results = suggestions.results();
    unsigned count = 0;
    if (suggestions.pending()) options = "Finding suggestions...\n";
    else for (const auto& result : results) {
        if (result.empty()) continue;
        options += (tooltip_selection == count ? "> " : "  ") + result + "\n";
        ++count;
    }
    options += (tooltip_selection == count ? "> " : "  ");
    options += "Add to dictionary\n";
    options += (tooltip_selection == count + 1 ? "> " : "  ");
    options += "Cancel";
    lv_label_set_text(tooltip_hint, options.c_str());
}
void close_tooltip() {
    if (tooltip)
        lv_obj_delete(tooltip);
    tooltip = tooltip_hint = nullptr;
    tooltip_word.clear();
}
void open_tooltip(EditorState &editor) {
    if (!buffers || !on || !content::dictionary_available())
        return;
    const char *text = lv_textarea_get_text(editor.textarea);
    const uint32_t cursor = lv_textarea_get_cursor_pos(editor.textarea);
    auto range = spelling::word_range_at(text, utf8_byte(text, cursor));
    auto candidate = spelling::normalize(
        std::string(text + range.start, range.end - range.start));
    if (candidate.empty() || personal.count(candidate) ||
        spelling::in_dictionary(candidate))
        return;
    uint32_t first = 0;
    for (size_t i = 0; i < range.start; ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
            ++first;
    auto *label = lv_textarea_get_label(editor.textarea);
    lv_point_t point{};
    lv_label_get_letter_pos(label, first, &point);
    lv_area_t coords, viewport;
    lv_obj_get_content_coords(label, &coords);
    lv_obj_get_content_coords(editor.textarea, &viewport);
    int32_t x = coords.x1 + point.x, y = coords.y1 + point.y;
    if (y < viewport.y1 || y > viewport.y2) {
        bool found=false;
        for(size_t i=0;i<front().segment_count();++i){
            const auto& segment=front().segments()[i];
            if(first<segment.word_start || first>=segment.word_end)continue;
            const int32_t segment_y=coords.y1+segment.y;
            if(segment_y<viewport.y1 || segment_y>viewport.y2)continue;
            x=coords.x1+segment.x1;
            y=segment_y-lv_obj_get_style_text_font(label,LV_PART_MAIN)->line_height;
            found=true;break;
        }
        if(!found)return;
    }
    tooltip_word = candidate;
    tooltip_start = range.start;
    tooltip_end = range.end;
    if (suggestion_word != candidate) { suggestion_word = candidate; suggestions.start(candidate); }
    tooltip_selection = 0;
    tooltip = lv_obj_create(lv_layer_top());
    lv_obj_set_size(tooltip, 360, 132);
    lv_obj_clear_flag(tooltip, LV_OBJ_FLAG_SCROLLABLE);
    const auto &theme = get_theme(editor.dark_theme);
    lv_obj_set_style_bg_color(tooltip, theme.sidebar_bg, 0);
    lv_obj_set_style_border_color(tooltip, theme.sidebar_selected, 0);
    lv_obj_set_style_border_width(tooltip, 2, 0);
    lv_obj_set_style_radius(tooltip, 7, 0);
    lv_obj_set_style_pad_all(tooltip, 10, 0);
    lv_obj_t *title = lv_label_create(tooltip);
    lv_label_set_text_fmt(title, "Spelling: %s", candidate.c_str());
    lv_obj_set_width(title, 336);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, get_ui_font(16), 0);
    lv_obj_set_style_text_color(title, theme.sidebar_btn_text, 0);
    tooltip_hint = lv_label_create(tooltip);
    lv_obj_set_style_text_font(tooltip_hint, get_ui_font(14), 0);
    lv_obj_set_style_text_color(tooltip_hint, theme.text_dim, 0);
    lv_obj_align_to(tooltip_hint, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    update_tooltip_options();
    const int32_t screen_width =
        lv_display_get_horizontal_resolution(lv_display_get_default());
    const int32_t screen_height =
        lv_display_get_vertical_resolution(lv_display_get_default());
    x = std::clamp<int32_t>(x, 4, std::max<int32_t>(4, screen_width - 364));
    int32_t popup_y = y - 140;
    if (popup_y < 4)
        popup_y = y +
                  lv_obj_get_style_text_font(label, LV_PART_MAIN)->line_height +
                  6;
    popup_y = std::clamp<int32_t>(popup_y, 4,
                                  std::max<int32_t>(4, screen_height - 136));
    lv_obj_set_pos(tooltip, x, popup_y);
}
struct CacheEntry {
    std::array<char, 97> word{};
    bool valid = false, known = false;
};
std::array<CacheEntry, 64> lookup_cache{};
size_t cache_next = 0;
struct LayoutContext {
    const lv_font_t *font = nullptr;
    int32_t width = 0, spacing = 0;
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT;
};
LayoutContext context;
spelling::LineLayout line_layout(void *raw, const char *text) {
    const auto &c = *static_cast<LayoutContext *>(raw);
    int32_t used = 0;
    uint32_t bytes = lv_text_get_next_line(text, c.font, c.spacing, c.width,
                                           &used, LV_TEXT_FLAG_NONE);
    return {bytes, c.align == LV_TEXT_ALIGN_CENTER  ? (c.width - used) / 2
                   : c.align == LV_TEXT_ALIGN_RIGHT ? c.width - used
                                                    : 0};
}
int32_t glyph_advance(void *raw, uint32_t code, uint32_t next) {
    const auto &c = *static_cast<LayoutContext *>(raw);
    int32_t width = lv_font_get_glyph_width(c.font, code, next);
    return width > 0 ? width + c.spacing : 0;
}
bool accepted(void *, const std::string &value, uint32_t start, uint32_t end) {
    if (!job_full_check)
        return !front().is_misspelling(start, end, value);
    if (personal.count(value))
        return true;
    for (const auto &entry : lookup_cache)
        if (entry.valid && value == entry.word.data())
            return entry.known;
    auto &entry = lookup_cache[cache_next++ % lookup_cache.size()];
    std::copy(value.begin(), value.end(), entry.word.begin());
    entry.word[value.size()] = 0;
    entry.valid = true;
    entry.known = spelling::in_dictionary(value);
    return entry.known;
}
void draw_underline(lv_event_t *e) {
    if (!show_annotations)
        return;
    lv_area_t origin, viewport;
    lv_obj_get_content_coords(text_label, &origin);
    lv_obj_get_content_coords(textarea, &viewport);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color =
        lv_color_hex(theme_id == 0 || theme_id == 4 ? 0xb3261e : 0xff9085);
    line.width = 1;
    line.dash_width = 2;
    line.dash_gap = 2;
    auto *layer = lv_event_get_layer(e);
    for (size_t i = 0; i < front().segment_count(); ++i) {
        const auto &segment = front().segments()[i];
        int32_t y = origin.y1 + segment.y;
        int32_t x1 = std::max(origin.x1 + segment.x1, viewport.x1),
                x2 = std::min(origin.x1 + segment.x2, viewport.x2);
        if (y < viewport.y1 || y > viewport.y2 || x1 >= x2)
            continue;
        line.p1.x = x1;
        line.p1.y = y;
        line.p2.x = x2;
        line.p2.y = y;
        lv_draw_line(layer, &line);
    }
}
constexpr size_t limit = 128;
const char *path = "/spiffs/user_files/.spelling";
bool persist() {
    std::string temp = std::string(path) + ".tmp";
    std::ofstream f(temp);
    f << (on ? 1 : 0) << '\n';
    for (const auto &w : personal)
        f << w << '\n';
    f.close();
    return f && replace_saved_file(temp, path);
}

} // namespace
void init() {
    // Two fixed snapshots live in PSRAM, preserving scarce internal/DMA memory.
    if (!buffers && content::dictionary_available()) {
        void *storage = heap_caps_malloc(sizeof(Buffers),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage) {
            on = false;
            return;
        }
        buffers = new (storage) Buffers{};
    }
    struct stat st{};
    if (stat(path, &st) != 0 || st.st_size > 128 * 98 + 32)
        return;
    std::ifstream f(path);
    int value;
    if (!(f >> value) || (value != 0 && value != 1))
        return;
    on = true;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line) && personal.size() < limit) {
        auto w = spelling::normalize_utf8(line);
        if (!w.empty())
            personal.insert(w);
    }
}
bool enabled() { return on && content::dictionary_available(); }
bool toggle() {
    if (!buffers)
        return false;
    on = !on;
    if (persist()) {
        viewport_dirty = true;
        return true;
    }
    on = !on;
    return false;
}
size_t personal_count() { return personal.size(); }
std::string current_word() { return word; }
bool add_current() {
    if (word.empty() || personal.size() >= limit)
        return false;
    auto result = personal.insert(word);
    if (!result.second)
        return true;
    if (persist()) {
        viewport_dirty = true;
        return true;
    }
    personal.erase(result.first);
    return false;
}
bool tooltip_active() { return tooltip != nullptr; }
bool suggestions_pending() { return suggestions.pending(); }
bool handle_event(const platform::PlatformEvent &event, EditorState &editor,
                  bool can_open) {
    using A = spelling::TooltipAction;
    switch (tooltip_input.route(event, tooltip != nullptr, can_open)) {
    case A::Pass:
        return false;
    case A::Open:
        open_tooltip(editor);
        return true;
    case A::Accept:
        if (!suggestions.pending()) {
            const auto& results = suggestions.results();
            unsigned count = 0;
            for (const auto& result : results) {
                if (result.empty()) continue;
                if (tooltip_selection == count) {
                    const char* text = lv_textarea_get_text(editor.textarea);
                    std::string original(text + tooltip_start, tooltip_end - tooltip_start);
                    std::string replacement = result;
                    if (!original.empty() && original[0] >= 'A' && original[0] <= 'Z') replacement[0] -= 'a' - 'A';
                    if (std::strlen(text) - original.size() + replacement.size() >= TEXT_BUFFER_SIZE) {
                        lv_label_set_text(tooltip_hint, "Document is too large for this replacement.\nEsc: close");
                        return true;
                    }
                    const uint32_t first = utf8_length(std::string(text, tooltip_start).c_str());
                    const uint32_t length = utf8_length(original.c_str());
                    close_tooltip();
                    lv_textarea_set_cursor_pos(editor.textarea, first + length);
                    for (uint32_t i = 0; i < length; ++i) lv_textarea_delete_char(editor.textarea);
                    lv_textarea_add_text(editor.textarea, replacement.c_str());
                    return true;
                }
                ++count;
            }
            if (tooltip_selection > count) { close_tooltip(); return true; }
        } else if (tooltip_selection > 0) { close_tooltip(); return true; }
        word = tooltip_word;
        if (!add_current()) {
            lv_label_set_text(
                tooltip_hint,
                "Could not save word.\nEnter: retry   Other key: dismiss");
            return true;
        }
        front().accept_word(word);
        if(text_label) lv_obj_invalidate(text_label);
        close_tooltip();
        return true;
    case A::Dismiss:
        close_tooltip();
        return true;
    case A::Up:
    case A::Down: {
        unsigned count = suggestions.pending() ? 0 : unsigned(!suggestions.results()[0].empty()) + unsigned(!suggestions.results()[1].empty());
        if (event.key == platform::KeyCode::Up && tooltip_selection) --tooltip_selection;
        if (event.key == platform::KeyCode::Down && tooltip_selection < count + 1) ++tooltip_selection;
        update_tooltip_options();
        return true;
    }
    case A::Consume:
        return true;
    }
    return false;
}
void update(EditorState &editor, bool visible) {
    if (!buffers && on && content::dictionary_available()) init();
    if (!buffers)
        return;
    static unsigned dictionary_revision = ~0u;
    const unsigned revision = content::revision();
    if (dictionary_revision != revision) {
        dictionary_revision = revision; lookup_cache = {};
        full_check_pending = document_dirty = true; scan_complete = true;
        suggestions.start(""); suggestion_word.clear(); close_tooltip();
    }
    if (!text_label) {
        textarea = editor.textarea;
        text_label = lv_textarea_get_label(textarea);
        lv_obj_add_event_cb(text_label, draw_underline, LV_EVENT_DRAW_MAIN_END,
                            nullptr);
        lv_obj_add_event_cb(
            textarea,
            [](lv_event_t *event) {
                const char *value =
                    static_cast<const char *>(lv_event_get_param(event));
                if (!value)
                    return;
                const uint32_t cursor = lv_textarea_get_cursor_pos(textarea);
                const bool deletion =
                    static_cast<unsigned char>(value[0]) == LV_KEY_DEL &&
                    value[1] == 0;
                edit_start = deletion && cursor ? cursor - 1 : cursor;
                edit_end = cursor;
                edit_inserted = deletion ? 0 : utf8_length(value);
                pending_edit = true;
                edit_delimiter = !deletion;
                for (const unsigned char *c =
                         reinterpret_cast<const unsigned char *>(value);
                     *c; ++c)
                    if (*c >= 128 || (*c >= 'a' && *c <= 'z') ||
                        (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                        *c == '\'')
                        edit_delimiter = false;
            },
            LV_EVENT_INSERT, nullptr);
        lv_obj_add_event_cb(
            textarea,
            [](lv_event_t *) {
                close_tooltip();
                if (pending_edit) {
                    front().edit(edit_start, edit_end, edit_inserted,
                                 edit_delimiter);
                    worker().reset_document();
                } else {
                    front().reset_document();
                    worker().reset_document();
                    show_annotations = false;
                }
                pending_edit = false;
                viewport_dirty = true;
                scan_complete = true;
                full_check_pending = true;
                word.clear();
                last_activity = lv_tick_get();
                lv_obj_invalidate(text_label);
            },
            LV_EVENT_VALUE_CHANGED, nullptr);
    }
    if (theme_id != editor.dark_theme) {
        theme_id = editor.dark_theme;
        lv_obj_invalidate(text_label);
    }
    if (!visible || !on || !content::dictionary_available()) {
        close_tooltip();
        suggestion_word.clear();
        suggestions.start({});
        if (show_annotations) {
            show_annotations = false;
            lv_obj_invalidate(text_label);
        }
        return;
    }
    static std::string last_path;
    static int32_t last_top = -1, last_bottom = -1, last_width = -1,
                   last_line_space = 0, last_spacing = 0;
    static const lv_font_t *last_font = nullptr;
    const auto *font = lv_obj_get_style_text_font(text_label, LV_PART_MAIN);
    const int32_t width = lv_obj_get_content_width(text_label),
                  spacing = lv_obj_get_style_text_letter_space(text_label,
                                                               LV_PART_MAIN),
                  line_space = lv_obj_get_style_text_line_space(text_label,
                                                                LV_PART_MAIN);
    lv_area_t origin, viewport;
    lv_obj_get_content_coords(text_label, &origin);
    lv_obj_get_content_coords(textarea, &viewport);
    int32_t top = std::max<int32_t>(0, viewport.y1 - origin.y1),
            bottom = viewport.y2 - origin.y1;
    if (last_path != editor.current_file_path || last_width != width ||
        last_font != font || last_line_space != line_space ||
        last_spacing != spacing) {
        last_path = editor.current_file_path;
        last_width = width;
        last_font = font;
        last_line_space = line_space;
        last_spacing = spacing;
        document_dirty = true;
        viewport_dirty = true;
        last_activity = lv_tick_get();
    }
    if (last_top != top || last_bottom != bottom) {
        full_check_pending = true;
        last_top = top;
        last_bottom = bottom;
        viewport_dirty = true;
        last_activity = lv_tick_get();
    }
    if (document_dirty) {
        front().reset_document();
        worker().reset_document();
        scan_complete = true;
        full_check_pending = true;
        document_dirty = false;
        show_annotations = false;
        lv_obj_invalidate(text_label);
    }
    if (!show_annotations && front().segment_count()) {
        show_annotations = true;
        lv_obj_invalidate(text_label);
    }
    const bool idle = lv_tick_get() - last_activity >= 600;
    if (idle && full_check_pending)
        viewport_dirty = true;
    context = {font, width, spacing,
               lv_obj_get_style_text_align(text_label, LV_PART_MAIN)};
    if (width <= 0)
        return;
    if (viewport_dirty) {
        worker() = front();
        worker().start(top, bottom);
        job_full_check = idle;
        if (job_full_check)
            full_check_pending = false;
        scan_complete = false;
        viewport_dirty = false;
    }
    spelling::ViewportLayout layout{
        &context,
        line_layout,
        glyph_advance,
        accepted,
        esp_timer_get_time,
        std::max<int32_t>(1, font->line_height + line_space),
        font->line_height - font->base_line - font->underline_position};
    if (!scan_complete) {
        scan_complete = worker().step(lv_textarea_get_text(textarea), layout);
        if (scan_complete) {
            published = 1 - published;
            show_annotations = true;
            lv_obj_invalidate(text_label);
        }
    }
    word = front().word_at_cursor(lv_textarea_get_cursor_pos(textarea));
    if (!tooltip) {
        const std::string candidate = !word.empty() && !personal.count(word) && !spelling::in_dictionary(word) ? word : std::string{};
        if (candidate != suggestion_word) {
            suggestion_word = candidate;
            suggestions.start(candidate);
        }
    }
    if (suggestions.pending()) {
        suggestions.step(128);
        if (!suggestions.pending() && tooltip) {
            tooltip_selection = 0;
            update_tooltip_options();
        }
    }
}
} // namespace spellcheck
