#pragma once
#ifdef LEAF_CONTENT_RUNTIME
#include "content.h"
#else
#include "english_lexicon.h"
#endif
#include "unicode_word.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace spelling {
// Incremental edit-distance search over the compact automaton. A separate
// stack frame is needed for every path because suffix states are shared.
class Suggestions {
  struct Frame {
    uint32_t arc = 0;
    std::array<uint8_t, 33> row{};
  };
  std::array<Frame, 34> stack_{};
  std::array<char, 34> path_{};
  std::string query_;
  std::array<std::string, 2> best_{};
  std::array<unsigned, 2> scores_{{999, 999}};
  int depth_ = -1;
#ifdef LEAF_CONTENT_RUNTIME
  size_t scan_ = 0;
  bool list_search_ = false;
  std::vector<unsigned> query_codes_;
#endif
public:
  void start(const std::string &query) {
    query_ = query;
    best_ = {};
    scores_ = {{999, 999}};
    depth_ = query.empty() || query.size() > 96 ? -1 : 0;
#ifdef LEAF_CONTENT_RUNTIME
    list_search_ = false;
#endif
    if (depth_ < 0)
      return;
#ifdef LEAF_CONTENT_RUNTIME
    list_search_ = content::dictionary_words() > 0;
    scan_ = 0;
    codepoints(query_, query_codes_);
    if (list_search_)
      return;
    stack_[0].arc = content::dictionary_root();
    if (!stack_[0].arc || query_.size() > 32) {
      depth_ = -1;
      return;
    }
#else
    stack_[0].arc = english_lexicon::root;
#endif
    for (size_t i = 0; i <= query.size(); ++i)
      stack_[0].row[i] = uint8_t(i);
  }
  bool pending() const { return depth_ >= 0; }
  const std::array<std::string, 2> &results() const { return best_; }
  // Caller bounds each slice and yields to input/rendering between slices.
  void step(unsigned budget) {
#ifdef LEAF_CONTENT_RUNTIME
    if (list_search_) {
      // UTF-8 edit distance uses characters, not bytes; at most 16 words per
      // slice.
      budget = std::min(budget, 16u);
      while (depth_ >= 0 && budget-- && scan_ < content::dictionary_words()) {
        const auto candidate = content::dictionary_word(scan_++);
        std::vector<unsigned> codes;
        if (!codepoints(candidate, codes))
          continue;
        std::array<unsigned, 33> row{}, next{};
        for (size_t i = 0; i <= query_codes_.size(); ++i)
          row[i] = i;
        for (size_t n = 0; n < codes.size(); ++n) {
          next[0] = n + 1;
          for (size_t i = 1; i <= query_codes_.size(); ++i)
            next[i] = std::min(
                {next[i - 1] + 1, row[i] + 1,
                 row[i - 1] + unsigned(query_codes_[i - 1] != codes[n])});
          row = next;
        }
        unsigned distance = row[query_codes_.size()];
        if (!distance || distance > 2)
          continue;
        unsigned score = distance * 100 + unsigned(codes.size());
        for (unsigned i = 0; i < 2; ++i)
          if (score < scores_[i]) {
            if (i == 0) {
              scores_[1] = scores_[0];
              best_[1] = best_[0];
            }
            scores_[i] = score;
            best_[i] = candidate;
            break;
          }
      }
      if (scan_ >= content::dictionary_words())
        depth_ = -1;
      return;
    }
#endif
    while (depth_ >= 0 && budget--) {
      auto &frame = stack_[depth_];
#ifdef LEAF_CONTENT_RUNTIME
      uint32_t arc = 0;
      if (!content::dictionary_arc(frame.arc, arc)) {
        --depth_;
        continue;
      }
#else
      const size_t offset = size_t(frame.arc) * 3;
      if (!frame.arc || offset + 2 >= sizeof(english_lexicon::arcs)) {
        --depth_;
        continue;
      }
      uint32_t arc = english_lexicon::arcs[offset] |
                     (uint32_t(english_lexicon::arcs[offset + 1]) << 8) |
                     (uint32_t(english_lexicon::arcs[offset + 2]) << 16);
#endif
      frame.arc = (arc & (1u << 23)) ? 0 : frame.arc + 1;
      unsigned code = (arc >> 17) & 31;
      if (code < 1 || code > 27 || depth_ >= 32)
        continue;
      char ch = code == 27 ? '\'' : char('a' + code - 1);
      auto &next = stack_[depth_ + 1];
      next.row[0] = uint8_t(depth_ + 1);
      unsigned minimum = next.row[0];
      for (size_t i = 1; i <= query_.size(); ++i) {
        next.row[i] = uint8_t(std::min(
            {unsigned(next.row[i - 1]) + 1, unsigned(frame.row[i]) + 1,
             unsigned(frame.row[i - 1]) + unsigned(query_[i - 1] != ch)}));
        minimum = std::min(minimum, unsigned(next.row[i]));
      }
      path_[depth_] = ch;
      const unsigned distance = next.row[query_.size()];
      if ((arc & (1u << 22)) && distance && distance <= 2) {
        std::string candidate(path_.data(), size_t(depth_ + 1));
        unsigned prefix = 0;
        while (prefix < query_.size() && prefix < candidate.size() &&
               query_[prefix] == candidate[prefix])
          ++prefix;
        size_t matched = 0;
        for (char letter : candidate)
          if (matched < query_.size() && query_[matched] == letter)
            ++matched;
        // Prefer completing an omitted letter over replacing a correct one.
        const unsigned score =
            distance * 100 + 32 - prefix - (matched == query_.size() ? 20 : 0);
        for (unsigned i = 0; i < 2; ++i) {
          if (score < scores_[i] ||
              (score == scores_[i] && candidate < best_[i])) {
            if (i == 0) {
              best_[1] = best_[0];
              scores_[1] = scores_[0];
            }
            best_[i] = candidate;
            scores_[i] = score;
            break;
          }
        }
      }
      if (minimum <= 2 && (arc & 0x1ffff)) {
        next.arc = arc & 0x1ffff;
        ++depth_;
      }
    }
  }
};
} // namespace spelling
