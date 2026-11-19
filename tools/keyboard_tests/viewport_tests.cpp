#include "justtype/src/spelling/viewport_checker.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
namespace {
struct Fake {
    unsigned columns = 20, lines = 0, glyphs = 0, lookups = 0, total_lines = 0;
};
int64_t tick = 0;
bool timed = false;
int64_t clock_us() { return timed ? tick += 100 : 0; }
spelling::LineLayout line(void *raw, const char *text) {
    auto &f = *static_cast<Fake *>(raw);
    ++f.lines;
    ++f.total_lines;
    size_t n = 0, last_space = 0;
    while (text[n] && n < f.columns) {
        if (text[n] == '\n')
            return {uint32_t(n + 1), 0};
        if (text[n] == ' ')
            last_space = n + 1;
        ++n;
    }
    if (text[n] && last_space)
        n = last_space;
    return {uint32_t(n), 0};
}
int32_t advance(void *raw, uint32_t c, uint32_t) {
    ++static_cast<Fake *>(raw)->glyphs;
    return c == '\n' ? 0 : 1;
}
bool accept(void *raw, const std::string &word, uint32_t, uint32_t) {
    ++static_cast<Fake *>(raw)->lookups;
    return word == "hello" || word == "world";
}
spelling::ViewportLayout layout(Fake &f) {
    return {&f, line, advance, accept, clock_us, 10, 8};
}
void run(spelling::ViewportChecker &checker, const std::string &text, Fake &f) {
    for (unsigned i = 0; i < 20000; ++i) {
        f.lines = f.glyphs = 0;
        bool done = checker.step(text.c_str(), layout(f));
        assert(f.lines <= 8);
        assert(f.glyphs <= 96);
        if (done)
            return;
    }
    assert(false);
}
} // namespace
void test_viewport() {
    static_assert(sizeof(spelling::ViewportChecker) < 40000,
                  "Bound viewport cache memory");
    spelling::ViewportChecker checker;
    Fake f;
    checker.start(0, 39);
    run(checker, "hello helo world wurld", f);
    assert(checker.segment_count() == 2);
    assert(checker.segments()[0].x1 == 6 && checker.segments()[0].x2 == 10 &&
           checker.segments()[0].y == 8);
    assert(checker.word_at_cursor(8) == "helo");
    assert(checker.word_at_cursor(5) == "hello");
    // Published geometry survives a replacement scan, and edits invalidate only
    // touched words.
    spelling::ViewportChecker stable = checker;
    spelling::ViewportChecker replacement = stable;
    replacement.start(0, 39);
    assert(stable.segment_count() == 2);
    stable.edit(21, 21, 1);
    assert(stable.segment_count() == 1); // append to wurld
    assert(stable.is_misspelling(6, 10, "helo"));
    stable = checker;
    stable.edit(0, 0, 1, true);
    assert(stable.segment_count() == 2);
    assert(stable.is_misspelling(7, 11, "helo"));
    stable = checker;
    stable.edit(10, 10, 1, true); // space after helo preserves its underline
    assert(stable.segment_count() == 2);
    assert(stable.is_misspelling(6, 10, "helo"));
    assert(stable.is_misspelling(18, 23, "wurld"));
    stable = checker;
    stable.edit(7, 8, 0);
    assert(stable.segment_count() == 1);
    assert(stable.is_misspelling(16, 21, "wurld"));
    stable = checker;
    stable.accept_word("helo");
    assert(stable.segment_count() == 1);
    assert(!stable.is_misspelling(6, 10, "helo"));
    assert(stable.is_misspelling(17, 22, "wurld"));
    unsigned lookups = f.lookups;
    checker.step("hello helo world wurld", layout(f));
    assert(f.lookups == lookups);
    checker.reset_document();
    f.columns = 5;
    checker.start(0, 39);
    run(checker, "abcdefghi ", f);
    assert(checker.segment_count() == 2);
    assert(checker.segments()[0].y == 8 && checker.segments()[1].y == 18);
    checker.reset_document();
    f.columns = 20;
    checker.start(10, 29);
    run(checker, "hello helo world wurld", f);
    assert(checker.segment_count() == 1);
    assert(checker.segments()[0].y == 18);
    checker.reset_document();
    checker.start(0, 39);
    run(checker, "hello world", f);
    assert(checker.segment_count() == 0);
    checker.reset_document();
    checker.start(0, 39);
    run(checker, u8"Привет helo ", f);
    assert(checker.word_at_cursor(9) == "helo");
    assert(checker.segment_count() == 1);
    assert(checker.segments()[0].x1 == 7);
    checker.reset_document();
    f.columns = 5;
    checker.start(0, 9);
    run(checker, "abcdefghijklmnop ", f);
    assert(checker.segment_count() == 1);
    f.columns = 20;
    std::string large;
    while (large.size() < 512000)
        large += "hello helo world wurld ";
    checker.reset_document();
    checker.start(100000, 100039);
    run(checker, large, f);
    assert(checker.segment_count() > 0);
    unsigned initial_lines = f.total_lines;
    checker.start(100000, 100039);
    run(checker, large, f);
    assert(f.total_lines - initial_lines < 100);
    checker.start(10, 39);
    run(checker, large, f);
    assert(checker.segment_count() > 0);
    checker.reset_document();
    checker.start(100000, 100039);
    checker.step(large.c_str(), layout(f));
    checker.reset_document();
    checker.start(0, 39);
    run(checker, "hello world", f);
    assert(checker.segment_count() == 0);
    timed = true;
    tick = 0;
    checker.start(0, 10000);
    f.lines = f.glyphs = 0;
    checker.step(large.c_str(), layout(f));
    assert(f.glyphs <= 20);
    timed = false;
    checker.reset_document();
    checker.start(0, 49);
    run(checker, std::string(4096, 'x') + " hello", f);
    assert(checker.segment_count() == 0);
    checker.reset_document();
    checker.start(0, 9);
    run(checker, std::string(4000, '\n') + "helo", f);
    assert(checker.segment_count() == 0);
    std::cout << "Viewport tests passed; fixed cache " << sizeof(checker)
              << " bytes\n";
}
