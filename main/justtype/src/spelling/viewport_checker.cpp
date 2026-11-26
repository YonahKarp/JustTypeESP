#include "viewport_checker.h"
#include <algorithm>
#include <cstring>
namespace spelling {
namespace {
bool token(unsigned char c) {
  return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '\'';
}
uint32_t decode(const char *text, uint32_t &byte) {
  unsigned char c = text[byte++];
  if (c < 128)
    return c;
  unsigned count = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
  uint32_t value = c & ((1u << (6 - count)) - 1);
  while (count-- && text[byte])
    value = (value << 6) | (static_cast<unsigned char>(text[byte++]) & 63);
  return value;
}
} // namespace
void ViewportChecker::reset_document() {
  checkpoint_count_ = checkpoint_next_ = 0;
  done_ = true;
  segment_count_ = word_count_ = 0;
}
// Keep stable annotations while a replacement viewport is built off-screen.
void ViewportChecker::edit(uint32_t start, uint32_t end, uint32_t inserted,
                           bool boundary_only) {
  const int64_t delta = int64_t(inserted) - int64_t(end - start);
  auto touched = [&](uint32_t a, uint32_t b) {
    return start == end && boundary_only ? start > a && start < b
                                         : start <= b && end >= a;
  };
  size_t out = 0;
  for (size_t i = 0; i < word_count_; ++i) {
    auto w = words_[i];
    if (touched(w.start, w.end))
      continue;
    if (w.start >= end) {
      w.start = uint32_t(w.start + delta);
      w.end = uint32_t(w.end + delta);
    }
    words_[out++] = w;
  }
  word_count_ = out;
  out = 0;
  for (size_t i = 0; i < segment_count_; ++i) {
    auto s = segments_[i];
    if (touched(s.word_start, s.word_end))
      continue;
    if (s.word_start >= end) {
      s.word_start = uint32_t(s.word_start + delta);
      s.word_end = uint32_t(s.word_end + delta);
    }
    segments_[out++] = s;
  }
  segment_count_ = out;
  // Wrapping can move the edited word back to the previous visual line.
  out = 0;
  const uint32_t safe = start > 96 ? start - 96 : 0;
  for (size_t i = 0; i < checkpoint_count_; ++i)
    if (checkpoints_[i].character < safe)
      checkpoints_[out++] = checkpoints_[i];
  checkpoint_count_ = out;
  checkpoint_next_ = out % checkpoints_.size();
  done_ = true;
}
bool ViewportChecker::is_misspelling(uint32_t start, uint32_t end,
                                     const std::string &value) const {
  for (size_t i = 0; i < word_count_; ++i) {
    const auto &w = words_[i];
    if (w.start == start && w.end == end && w.misspelled &&
        value == (word_bytes_.data() + w.value))
      return true;
  }
  return false;
}
void ViewportChecker::accept_word(const std::string &value) {
  for (size_t i = 0; i < word_count_; ++i) {
    auto &w = words_[i];
    if (value != (word_bytes_.data() + w.value))
      continue;
    w.misspelled = false;
    size_t out = 0;
    for (size_t j = 0; j < segment_count_; ++j) {
      const auto &s = segments_[j];
      if (s.word_start == w.start && s.word_end == w.end)
        continue;
      segments_[out++] = s;
    }
    segment_count_ = out;
  }
}
void ViewportChecker::start(int32_t top, int32_t bottom) {
  top_ = std::max<int32_t>(0, top);
  bottom_ = bottom;
  Checkpoint best;
  // Begin at least one visual line above the viewport to finish wrapped
  // words.
  for (size_t i = 0; i < checkpoint_count_; ++i)
    if (checkpoints_[i].y < top_ && checkpoints_[i].y > best.y)
      best = checkpoints_[i];
  byte_ = best.byte;
  character_ = best.character;
  y_ = best.y;
  line_end_ = byte_;
  line_number_ = 0;
  x_ = 0;
  done_ = false;
  segment_count_ = word_count_ = glyph_count_ = pending_size_ = 0;
  word_bytes_used_ = 0;
  oversized_ = false;
}
void ViewportChecker::finish_word(const ViewportLayout &layout) {
  if (!pending_size_ || oversized_) {
    pending_size_ = glyph_count_ = 0;
    oversized_ = false;
    return;
  }
  pending_[pending_size_] = 0;
  auto range = word_range_at(pending_.data(), 0);
  auto value = normalize(
      std::string(pending_.data() + range.start, range.end - range.start));
  if (!value.empty()) {
    const bool missed =
        !layout.accepted(layout.context, value, pending_character_,
                         pending_character_ + uint32_t(glyph_count_));
    if (word_count_ < words_.size() &&
        word_bytes_used_ + value.size() + 1 <= word_bytes_.size()) {
      auto &w = words_[word_count_++];
      w.start = pending_character_;
      w.end = pending_character_ + uint32_t(glyph_count_);
      w.value = static_cast<uint16_t>(word_bytes_used_);
      std::copy(value.begin(), value.end(),
                word_bytes_.begin() + word_bytes_used_);
      word_bytes_used_ += value.size();
      word_bytes_[word_bytes_used_++] = 0;
      w.misspelled = missed;
    }
    if (missed) {
      bool joined = false;
      for (size_t i = 0; i < glyph_count_; ++i) {
        const auto &g = pending_glyphs_[i];
        if (g.byte < range.start || g.byte >= range.end || g.line.y < top_ ||
            g.line.y > bottom_)
          continue;
        if (joined && segment_count_ &&
            segments_[segment_count_ - 1].y == g.line.y)
          segments_[segment_count_ - 1].x2 = g.line.x2;
        else if (segment_count_ < segments_.size()) {
          auto segment = g.line;
          segment.word_start = pending_character_;
          segment.word_end = pending_character_ + uint32_t(glyph_count_);
          segments_[segment_count_++] = segment;
          joined = true;
        }
      }
    }
  }
  pending_size_ = glyph_count_ = 0;
  oversized_ = false;
}
bool ViewportChecker::step(const char *text, const ViewportLayout &layout) {
  if (done_)
    return true;
  if (line_number_ == 0 && byte_ > 0 &&
      token(static_cast<unsigned char>(text[byte_ - 1])) &&
      token(static_cast<unsigned char>(text[byte_])))
    oversized_ = true;
  const int64_t deadline = layout.clock_us() + 2000;
  unsigned glyphs = 0, lines = 0;
  while (glyphs < 96 && lines < 8 && layout.clock_us() < deadline) {
    if (!text[byte_] || (y_ > bottom_ + layout.line_step &&
                         (!pending_size_ || oversized_ ||
                          !token(static_cast<unsigned char>(text[byte_]))))) {
      finish_word(layout);
      done_ = true;
      break;
    }
    if (byte_ == line_end_) {
      if ((line_number_++ % 64) == 0) {
        checkpoints_[checkpoint_next_] = {byte_, character_, y_};
        checkpoint_next_ = (checkpoint_next_ + 1) % checkpoints_.size();
        checkpoint_count_ =
            std::min(checkpoint_count_ + 1, checkpoints_.size());
      }
      std::array<char, 2049> buffer{};
      size_t count = 0;
      while (count < 2048 && text[byte_ + count]) {
        buffer[count] = text[byte_ + count];
        ++count;
        if (buffer[count - 1] == '\n')
          break;
      }
      if (count == 2048) {
        while (count &&
               (static_cast<unsigned char>(text[byte_ + count]) & 0xc0) == 0x80)
          --count;
      }
      buffer[count] = 0;
      const auto line = layout.line(layout.context, buffer.data());
      ++lines;
      if (!line.bytes || line.bytes > count) {
        done_ = true;
        break;
      }
      line_end_ = byte_ + line.bytes;
      x_ = line.x;
    }
    uint32_t next_byte = byte_;
    const uint32_t code = decode(text, next_byte);
    uint32_t lookahead = next_byte;
    const uint32_t next = text[lookahead] ? decode(text, lookahead) : 0;
    const int32_t advance = layout.advance(layout.context, code, next);
    if (token(static_cast<unsigned char>(text[byte_]))) {
      if (!pending_size_ && !oversized_)
        pending_character_ = character_;
      const size_t bytes = next_byte - byte_;
      if (pending_size_ + bytes > 96 || glyph_count_ >= pending_glyphs_.size())
        oversized_ = true;
      if (!oversized_) {
        pending_glyphs_[glyph_count_++] = {uint32_t(pending_size_),
                                           {x_,
                                            x_ + std::max<int32_t>(0, advance),
                                            y_ + layout.underline_offset}};
        std::memcpy(pending_.data() + pending_size_, text + byte_, bytes);
        pending_size_ += bytes;
      }
    } else
      finish_word(layout);
    x_ += advance;
    byte_ = next_byte;
    ++character_;
    ++glyphs;
    if (byte_ >= line_end_) {
      y_ += layout.line_step;
    }
    // Skip off-screen lines without per-glyph work, once a complete
    // boundary is available.
    if (y_ + layout.line_step < top_ && byte_ < line_end_) {
      while (byte_ < line_end_) {
        if ((static_cast<unsigned char>(text[byte_]) & 0xc0) != 0x80)
          ++character_;
        ++byte_;
      }
      y_ += layout.line_step;
      pending_size_ = glyph_count_ = 0;
      oversized_ = token(static_cast<unsigned char>(text[byte_ - 1])) &&
                   token(static_cast<unsigned char>(text[byte_]));
    }
  }
  return done_;
}
std::string ViewportChecker::word_at_cursor(uint32_t cursor) const {
  const Word *previous = nullptr;
  for (size_t i = 0; i < word_count_; ++i) {
    const auto &w = words_[i];
    if (cursor >= w.start && cursor <= w.end)
      return (word_bytes_.data() + w.value);
    if (w.end < cursor)
      previous = &w;
  }
  return previous && cursor - previous->end <= 2
             ? std::string((word_bytes_.data() + previous->value))
             : std::string();
}
} // namespace spelling
