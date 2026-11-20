#include "justtype/src/search.h"
#include "justtype/src/spelling/spellcheck_core.h"
#include "justtype/src/spelling/suggestions.h"
#include "justtype/src/spelling/tooltip_input.h"
#include "justtype/src/text_navigation.h"
#include "justtype/src/utf8_positions.h"
#include "keyboard_config.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
bool replace_saved_file(const std::string &, const std::string &) {
    return true;
}
void test_navigation();
void test_viewport();
void test_backup_policy();
int main() {
    {
        spelling::Suggestions suggestions;
        suggestions.start("helo");
        unsigned slices = 0;
        while (suggestions.pending() && ++slices < 100000) suggestions.step(128);
        assert(!suggestions.pending());
        assert(suggestions.results()[0] == "hello" || suggestions.results()[1] == "hello");
        suggestions.start("qzxqzxqzx");
        while (suggestions.pending()) suggestions.step(128);
        assert(suggestions.results()[0].empty());
        suggestions.start("helo");
        suggestions.step(1);
        suggestions.start("");
        assert(!suggestions.pending());
    }
    test_navigation();
    test_viewport();
    test_backup_policy();
    {
        spelling::TooltipInput input;
        using A = spelling::TooltipAction;
        using T = platform::PlatformEvent::Type;
        platform::PlatformEvent e{};
        e.type = T::KeyDown;
        e.hid_usage = 0x2c;
        e.modifiers.ctrl = true;
        assert(input.route(e, false, true) == A::Open);
        e.type = T::KeyUp;
        assert(input.route(e, true, true) == A::Consume);
        e.type = T::KeyDown;
        e.hid_usage = 0x28;
        e.key = platform::KeyCode::Enter;
        assert(input.route(e, true, true) == A::Accept);
        e.type = T::KeyUp;
        assert(input.route(e, false, true) == A::Consume);
        e.type = T::KeyDown;
        e.hid_usage = 4;
        e.key = platform::KeyCode::A;
        e.modifiers = {};
        assert(input.route(e, true, true) == A::Dismiss);
        e.type = T::TextInput;
        assert(input.route(e, false, true) == A::Consume);
        e.type = T::KeyUp;
        assert(input.route(e, false, true) == A::Consume);
        e.type = T::TextInput;
        assert(input.route(e, false, true) == A::Pass);
        e.type = T::KeyDown;
        e.hid_usage = 0x2c;
        e.modifiers.ctrl = true;
        assert(input.route(e, false, false) == A::Pass);
    }

    std::ifstream words("tools/spellcheck/expected_words.txt");
    assert(words);
    std::string item;
    unsigned checked_words = 0;
    while (std::getline(words, item)) {
        assert(spelling::in_dictionary(item));
        ++checked_words;
    }
    assert(checked_words > 49000);
    assert(spelling::word_at("helo ", 5) == "helo");
    assert(spelling::word_at("hello, ", 7) == "hello");
    assert(spelling::word_at("hello world", 8) == "world");
    assert(spelling::word_at(u8"Привет ", 13).empty());
    assert(spelling::word_at("", 0).empty());
    auto quoted = spelling::word_range_at("'helo' ", 7);
    assert(quoted.start == 1 && quoted.end == 5);
    auto after_unicode = spelling::word_range_at(u8"Привет helo ", 18);
    assert(after_unicode.start == 13 && after_unicode.end == 17);
    auto smart_quote = spelling::word_range_at(u8"‘helo’ ", 11);
    assert(smart_quote.start == 3 && smart_quote.end == 7);
    auto contraction = spelling::word_range_at("don't ", 6);
    assert(contraction.start == 0 && contraction.end == 5);

    assert(spelling::word_at("abc123 ", 7).empty());
    assert(spelling::in_dictionary("hello"));
    assert(spelling::in_dictionary(spelling::normalize("I")));
    assert(spelling::in_dictionary(spelling::normalize("we’ll")));
    assert(spelling::in_dictionary(spelling::normalize("backyard")));
    assert(spelling::in_dictionary(spelling::normalize("i")));
    assert(spelling::in_dictionary("running"));
    assert(spelling::in_dictionary("don't"));
    assert(!spelling::in_dictionary("helo"));
    assert(!spelling::in_dictionary("qzxqzx"));
    assert(spelling::normalize(u8"DON’T") == "don't");
    assert(spelling::normalize(u8"Привет").empty());
    assert(spelling::normalize("abc123").empty());
    assert(spelling::normalize("'Hello'") == "hello");
    using namespace keyboard_config;
    reset();
    assert(text(0x14, false, false) == "q");
    assert(text(4, false, false, true) == "A");
    set_layout(1);
    assert(text(0x14, false, false) == "'");
    assert(text(0x15, false, false) == "p");
    set_layout(2);
    assert(text(0x14, false, false) == "5");
    assert(text(4, false, false) == "7");
    assert(text(0x0b, false, false) == "h");
    set_layout(3);
    assert(text(0x33, false, false) == u8"ñ");
    assert(text(0x34, false, false).empty());
    assert(text(4, false, false) == u8"á");
    assert(text(0x34, true, false).empty());
    assert(text(0x18, false, false) == u8"ü");
    assert(text(0x1f, false, true) == "@");
    assert(text(0x34, false, false).empty());
    assert(cancel_composition());
    assert(text(4, false, false) == "a");
    set_layout(4);
    assert(text(0x14, false, false) == u8"й");
    assert(text(4, true, false) == u8"Ф");
    assert(text(0x35, false, false) == u8"ё");
    assert(!bind(0, 0x17));
    assert(!bind(0, 0x28));
    assert(bind(0, 0x07));
    assert(shortcut(0x07) == platform::KeyCode::F);
    assert(shortcut(0x09) == platform::KeyCode::Unknown);
    reset();
    assert(shortcut(0x05)==platform::KeyCode::B);
    assert(shortcut(0x09) == platform::KeyCode::F);
    for (int n = 0; n < layout_count; ++n) {
        set_layout(n);
        for (int u = 4; u < 0x65; ++u) {
            auto s = text(u, false, false);
            assert(s.size() < 10);
            cancel_composition();
        }
    }
    const char *s = u8"Привет мир";
    assert(utf8_length(s) == 10);
    assert(utf8_byte(s, 6) == 12);
    assert(find_prev_word_start(s, 10) == 7);
    assert(find_next_word_end(s, 10, 0) == 6);
    SearchState search;
    std::strcpy(search.query, u8"мир");
    search.query_len = 6;
    perform_search(search, s, int(std::strlen(s)));
    assert(search.matches.size() == 1);
    assert(search.matches[0].start == 7 && search.matches[0].end == 10);
    std::cout << "Keyboard and UTF-8 tests passed\n";
}
