#include "text_navigation.h"
#include "utf8_positions.h"

namespace {
// Walk UTF-8 boundaries directly. Navigation used to allocate and populate a
// document-sized string on every action, even when moving just one word.
struct Cursor {
    const char* text;
    uint32_t byte;
    int32_t pos;

    Cursor(const char* value, int32_t position)
        : text(value), byte(utf8_byte(value, position)), pos(position) {}

    uint32_t previous_byte() const {
        uint32_t previous = byte;
        if (previous) --previous;
        while (previous && (static_cast<unsigned char>(text[previous]) & 0xc0) == 0x80)
            --previous;
        return previous;
    }
    void previous() { byte = previous_byte(); --pos; }
    void next() {
        if (!text[byte]) return;
        ++byte;
        while ((static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) ++byte;
        ++pos;
    }
    char value() const { return text[byte]; }
};
bool word_space(char value) {
    return value == ' ' || value == '\n' || value == '\t';
}
} // namespace

int32_t find_paragraph_start(const char* text, int32_t pos) {
    if (pos <= 0) return 0;
    Cursor cursor(text, pos);
    cursor.previous();
    while (cursor.pos > 0 && cursor.value() != '\n') cursor.previous();
    if (cursor.pos > 0) {
        cursor.previous();
        while (cursor.pos > 0 && cursor.value() != '\n') cursor.previous();
        if (cursor.pos > 0) cursor.next();
    }
    return cursor.pos;
}

int32_t find_paragraph_end(const char* text, int32_t text_len, int32_t pos) {
    Cursor cursor(text, pos);
    while (cursor.pos < text_len && cursor.value() != '\n') cursor.next();
    if (cursor.pos < text_len) cursor.next();
    while (cursor.pos < text_len && cursor.value() != '\n') cursor.next();
    return cursor.pos;
}

int32_t find_prev_word_start(const char* text, int32_t pos) {
    if (pos <= 0) return 0;
    Cursor cursor(text, pos);
    cursor.previous();
    while (cursor.pos > 0 && word_space(cursor.value())) cursor.previous();
    while (cursor.pos > 0 && !word_space(text[cursor.previous_byte()])) cursor.previous();
    return cursor.pos;
}

int32_t find_next_word_end(const char* text, int32_t text_len, int32_t pos) {
    if (pos >= text_len) return text_len;
    Cursor cursor(text, pos);
    while (cursor.pos < text_len && word_space(cursor.value())) cursor.next();
    while (cursor.pos < text_len && !word_space(cursor.value())) cursor.next();
    return cursor.pos;
}

int32_t find_line_start(const char* text, int32_t pos) {
    if (pos <= 0) return 0;
    Cursor cursor(text, pos);
    cursor.previous();
    while (cursor.pos > 0 && cursor.value() != '\n') cursor.previous();
    if (cursor.value() == '\n') cursor.next();
    return cursor.pos;
}

int32_t find_line_end(const char* text, int32_t text_len, int32_t pos) {
    Cursor cursor(text, pos);
    while (cursor.pos < text_len && cursor.value() != '\n') cursor.next();
    return cursor.pos;
}

bool KeyRepeatState::should_act(bool is_pressed, uint32_t now) {
    if (!is_pressed) {
        last_pressed = false;
        acted_on_press = false;
        return false;
    }
    
    bool is_new_press = !last_pressed;
    last_pressed = true;
    
    if (is_new_press) {
        press_time = now;
        last_action_time = now;
        acted_on_press = true;
        return true;
    }
    
    if (acted_on_press && 
        (now - press_time >= INITIAL_DELAY) && 
        (now - last_action_time >= REPEAT_DELAY)) {
        last_action_time = now;
        return true;
    }
    
    return false;
}
