#pragma once
#include <string>
#include <vector>
namespace spelling {
inline bool codepoints(const std::string &s, std::vector<unsigned> &out) {
  out.clear();
  for (size_t i = 0; i < s.size();) {
    unsigned c = static_cast<unsigned char>(s[i++]), n = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      n = 1;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      n = 2;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      n = 3;
      minimum = 0x10000;
    } else if (c >= 128)
      return false;
    while (n--) {
      if (i == s.size())
        return false;
      unsigned b = static_cast<unsigned char>(s[i++]);
      if ((b & 0xc0) != 0x80)
        return false;
      c = (c << 6) | (b & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
    out.push_back(c);
    if (out.size() > 32)
      return false;
  }
  return true;
}
inline std::string normalize_utf8(const std::string &word) {
  bool ascii = true;
  for (unsigned char c : word)
    if (c >= 128) {
      ascii = false;
      break;
    }
  if (ascii) {
    if (word.size() > 32)
      return {};
    std::string result;
    result.reserve(word.size());
    for (unsigned char c : word) {
      if (c >= 'A' && c <= 'Z')
        c += 32;
      if (!((c >= 'a' && c <= 'z') || c == '\''))
        return {};
      result += char(c);
    }
    while (!result.empty() && result.front() == '\'')
      result.erase(0, 1);
    while (!result.empty() && result.back() == '\'')
      result.pop_back();
    return result;
  }
  std::vector<unsigned> codes;
  if (word.size() > 96 || !codepoints(word, codes))
    return {};
  std::string result;
  for (unsigned c : codes) {
    if (c == 0x2018 || c == 0x2019)
      c = '\'';
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if ((c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xde))
      c += 32;
    if (c >= 0x410 && c <= 0x42f)
      c += 32;
    if (c == 0x401)
      c = 0x451;
    if (!((c >= 'a' && c <= 'z') || c == '\'' ||
          (c >= 0xdf && c <= 0xff && c != 0xf7) || (c >= 0x430 && c <= 0x44f) ||
          c == 0x451))
      return {};
    if (c < 128)
      result += char(c);
    else if (c < 0x800) {
      result += char(0xc0 | (c >> 6));
      result += char(0x80 | (c & 63));
    }
  }
  while (!result.empty() && result.front() == '\'')
    result.erase(0, 1);
  while (!result.empty() && result.back() == '\'')
    result.pop_back();
  return result;
}
} // namespace spelling
