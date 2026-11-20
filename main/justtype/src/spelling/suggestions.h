#pragma once
#include "english_lexicon.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace spelling {
// Incremental edit-distance search over the compact automaton. A separate
// stack frame is needed for every path because suffix states are shared.
class Suggestions {
    struct Frame { uint32_t arc = 0; std::array<uint8_t, 33> row{}; };
    std::array<Frame, 34> stack_{};
    std::array<char, 34> path_{};
    std::string query_;
    std::array<std::string, 2> best_{};
    std::array<unsigned, 2> scores_{{999, 999}};
    int depth_ = -1;
  public:
    void start(const std::string& query) {
        query_ = query; best_ = {}; scores_ = {{999, 999}};
        depth_ = query.empty() || query.size() > 32 ? -1 : 0;
        if (depth_ < 0) return;
        stack_[0].arc = english_lexicon::root;
        for (size_t i = 0; i <= query.size(); ++i) stack_[0].row[i] = uint8_t(i);
    }
    bool pending() const { return depth_ >= 0; }
    const std::array<std::string, 2>& results() const { return best_; }
    // Caller bounds each slice and yields to input/rendering between slices.
    void step(unsigned budget) {
        while (depth_ >= 0 && budget--) {
            auto &frame = stack_[depth_];
            const size_t offset = size_t(frame.arc) * 3;
            if (!frame.arc || offset + 2 >= sizeof(english_lexicon::arcs)) { --depth_; continue; }
            uint32_t arc = english_lexicon::arcs[offset] | (uint32_t(english_lexicon::arcs[offset+1]) << 8) | (uint32_t(english_lexicon::arcs[offset+2]) << 16);
            frame.arc = (arc & (1u << 23)) ? 0 : frame.arc + 1;
            unsigned code = (arc >> 17) & 31;
            if (code < 1 || code > 27 || depth_ >= 32) continue;
            char ch = code == 27 ? '\'' : char('a' + code - 1);
            auto &next = stack_[depth_ + 1];
            next.row[0] = uint8_t(depth_ + 1);
            unsigned minimum = next.row[0];
            for (size_t i = 1; i <= query_.size(); ++i) {
                next.row[i] = uint8_t(std::min({unsigned(next.row[i-1]) + 1, unsigned(frame.row[i]) + 1,
                    unsigned(frame.row[i-1]) + unsigned(query_[i-1] != ch)}));
                minimum = std::min(minimum, unsigned(next.row[i]));
            }
            path_[depth_] = ch;
            const unsigned distance = next.row[query_.size()];
            if ((arc & (1u << 22)) && distance && distance <= 2) {
                std::string candidate(path_.data(), size_t(depth_ + 1));
                unsigned prefix = 0;
                while (prefix < query_.size() && prefix < candidate.size() && query_[prefix] == candidate[prefix]) ++prefix;
                size_t matched = 0;
                for (char letter : candidate)
                    if (matched < query_.size() && query_[matched] == letter) ++matched;
                // Prefer completing an omitted letter over replacing a correct one.
                const unsigned score = distance * 100 + 32 - prefix - (matched == query_.size() ? 20 : 0);
                for (unsigned i = 0; i < 2; ++i) {
                    if (score < scores_[i] || (score == scores_[i] && candidate < best_[i])) {
                        if (i == 0) { best_[1] = best_[0]; scores_[1] = scores_[0]; }
                        best_[i] = candidate; scores_[i] = score; break;
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
