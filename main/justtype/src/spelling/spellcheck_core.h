#pragma once
#include <string>
namespace spelling {
std::string normalize(const std::string &word);
struct WordRange {
    size_t start;
    size_t end;
};
WordRange word_range_at(const char *text, size_t byte_cursor);
std::string word_at(const char *text, size_t byte_cursor);
bool in_dictionary(const std::string &word);
} // namespace spelling
