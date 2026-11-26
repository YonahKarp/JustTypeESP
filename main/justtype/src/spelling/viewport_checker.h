#pragma once
#include "spellcheck_core.h"
#include <array>
#include <cstdint>
namespace spelling {
struct LineLayout {
    uint32_t bytes;
    int32_t x;
};
struct ViewportLayout {
    void *context;
    LineLayout (*line)(
        void *, const char *); // Input bounded to 2048 bytes, complete UTF-8.
    int32_t (*advance)(void *, uint32_t, uint32_t);
    bool (*accepted)(void *, const std::string &, uint32_t, uint32_t);
    int64_t (*clock_us)();
    int32_t line_step;
    int32_t underline_offset;
};
struct Underline {
    int32_t x1, x2, y;
    uint32_t word_start = 0, word_end = 0;
};
class ViewportChecker {
  public:
    static constexpr size_t max_segments = 512, max_words = 384;
    void reset_document();
    void accept_word(const std::string &value);
    void edit(uint32_t start, uint32_t end, uint32_t inserted,
              bool boundary_only = false);
    bool is_misspelling(uint32_t start, uint32_t end,
                        const std::string &value) const;
    void start(int32_t top, int32_t bottom);
    // <=96 glyphs, <=8 layout calls and a cooperative 2 ms deadline per call.
    bool step(const char *text, const ViewportLayout &layout);
    const std::array<Underline, max_segments> &segments() const {
        return segments_;
    }
    size_t segment_count() const { return segment_count_; }
    std::string word_at_cursor(uint32_t cursor) const;

  private:
    struct Checkpoint {
        uint32_t byte = 0, character = 0;
        int32_t y = 0;
    };
    struct Word {
        uint32_t start = 0, end = 0;
        uint16_t value = 0;
        bool misspelled = false;
    };
    struct Glyph {
        uint32_t byte;
        Underline line;
    };
    std::array<Checkpoint, 128> checkpoints_{};
    size_t checkpoint_count_ = 0, checkpoint_next_ = 0;
    std::array<Underline, max_segments> segments_{};
    size_t segment_count_ = 0;
    std::array<Word, max_words> words_{};
    std::array<char, max_words * 33> word_bytes_{};
    size_t word_bytes_used_ = 0;
    size_t word_count_ = 0;
    std::array<Glyph, 96> pending_glyphs_{};
    size_t glyph_count_ = 0;
    std::array<char, 97> pending_{};
    size_t pending_size_ = 0;
    uint32_t pending_character_ = 0;
    bool oversized_ = false;
    uint32_t byte_ = 0, character_ = 0, line_end_ = 0, line_number_ = 0;
    int32_t y_ = 0, x_ = 0, top_ = 0, bottom_ = 0;
    bool done_ = true;
    void finish_word(const ViewportLayout &);
};
} // namespace spelling
