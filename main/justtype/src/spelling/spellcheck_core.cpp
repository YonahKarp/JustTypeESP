#include "spellcheck_core.h"
#include "unicode_word.h"
#ifdef LEAF_CONTENT_RUNTIME
#include "content.h"
#else
#include "english_lexicon.h"
#endif
namespace spelling {
std::string normalize(const std::string &word) {
  auto result = normalize_utf8(word);
#ifdef LEAF_CONTENT_RUNTIME
  const auto language = content::dictionary_language();
#else
  const std::string language = "en";
#endif
  bool ascii = true;
  for (unsigned char c : result)
    if (c >= 128) {
      ascii = false;
      break;
    }
  if (ascii)
    return language == "ru" ? std::string{} : result;
  std::vector<unsigned> codes;
  if (!codepoints(result, codes))
    return {};
  for (unsigned c : codes) {
    if (c == '\'')
      continue;
    if (language == "en" && c >= 128)
      return {};
    if (language == "es" && c >= 0x400)
      return {};
    if (language == "ru" && c < 0x400)
      return {};
  }
  return result;
}
WordRange word_range_at(const char *text, size_t at) {
  auto token = [](unsigned char c) {
    return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '\'';
  };
  size_t original = at;
  if (!token(static_cast<unsigned char>(text[at]))) {
    while (at > 0 && original - at < 32 &&
           !token(static_cast<unsigned char>(text[at - 1])) &&
           text[at - 1] != '\n')
      --at;
  }
  size_t start = at;
  if (at > 0 && token(static_cast<unsigned char>(text[at - 1])))
    --start;
  while (start > 0 && at - start <= 96 &&
         token(static_cast<unsigned char>(text[start - 1])))
    --start;
  size_t end = start;
  while (text[end] && end - start <= 96 &&
         token(static_cast<unsigned char>(text[end])))
    ++end;
  // Exclude quotation marks while preserving apostrophes inside contractions.
  auto quote_size = [&](size_t index) -> size_t {
    if (text[index] == '\'')
      return 1;
    if (static_cast<unsigned char>(text[index]) == 0xe2 && index + 2 < end &&
        static_cast<unsigned char>(text[index + 1]) == 0x80 &&
        (static_cast<unsigned char>(text[index + 2]) == 0x98 ||
         static_cast<unsigned char>(text[index + 2]) == 0x99))
      return 3;
    return 0;
  };
  while (start < end) {
    auto count = quote_size(start);
    if (!count)
      break;
    start += count;
  }
  while (end > start) {
    if (text[end - 1] == '\'')
      --end;
    else if (end - start >= 3 && quote_size(end - 3) == 3)
      end -= 3;
    else
      break;
  }
  return {start, end};
}
std::string word_at(const char *text, size_t at) {
  auto range = word_range_at(text, at);
  return normalize(std::string(text + range.start, range.end - range.start));
}
bool in_dictionary(const std::string &word) {
#ifdef LEAF_CONTENT_RUNTIME
  return content::contains(word);
#else
  if (word.empty())
    return false;
  uint32_t node = english_lexicon::root;
  for (size_t i = 0; i < word.size(); ++i) {
    const unsigned label = word[i] == '\'' ? 27 : unsigned(word[i] - 'a' + 1);
    if (label < 1 || label > 27 || !node)
      return false;
    uint32_t arc;
    for (;; ++node) {
      size_t offset = size_t(node) * 3;
      if (offset + 2 >= sizeof(english_lexicon::arcs))
        return false;
      arc = english_lexicon::arcs[offset] |
            (uint32_t(english_lexicon::arcs[offset + 1]) << 8) |
            (uint32_t(english_lexicon::arcs[offset + 2]) << 16);
      if (((arc >> 17) & 31) == label)
        break;
      if (arc & (1u << 23))
        return false;
    }
    if (i + 1 == word.size())
      return (arc & (1u << 22)) != 0;
    node = arc & 0x1ffff;
  }
  return false;
#endif
}
} // namespace spelling
