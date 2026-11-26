#pragma once
#include <cstring>
#include <string>
#include <vector>
namespace content {
// Restrict downloadable LVGL fonts to uncompressed, unkerned format-1 files.
// Bound every table and glyph before passing a trusted publisher file to LVGL.
inline bool valid_font(const std::string &s, unsigned size) {
  const auto u8 = [&](size_t p) -> unsigned {
    return p < s.size() ? static_cast<unsigned char>(s[p]) : 0;
  };
  const auto u16 = [&](size_t p) -> unsigned {
    return u8(p) | (u8(p + 1) << 8);
  };
  const auto u32 = [&](size_t p) -> uint32_t {
    return u16(p) | (uint32_t(u16(p + 2)) << 16);
  };
  if (s.size() < 56 || s.size() > 128 * 1024 || u32(0) != 48 || u32(8) != 1 ||
      memcmp(s.data() + 4, "head", 4) || u16(12) != 3 || u16(14) != size ||
      u8(35) > 1 || u8(36) > 1)
    return false;
  const int descent = static_cast<int16_t>(u16(18));
  if (u16(16) > 96 || descent > 0 || descent < -96 ||
      int(u16(16)) - descent > 96 || u16(46) > 8)
    return false;
  // Header fields begin at offset 8. bpp=37, xy=38, wh=39, advance=40,
  // compression=41; require short bit fields and no compressed glyphs.
  if ((u8(37) != 1 && u8(37) != 2 && u8(37) != 4) || u8(38) > 8 || u8(39) > 8 ||
      u8(40) > 16 || u8(41) != 0 || u8(42) != 0)
    return false;
  std::vector<size_t> tables;
  size_t at = 0;
  for (const auto *label : {"head", "cmap", "loca", "glyf"}) {
    if (at + 8 > s.size() || memcmp(s.data() + at + 4, label, 4))
      return false;
    const auto length = u32(at);
    if (length < 8 || length > s.size() - at)
      return false;
    tables.push_back(at);
    at += length;
  }
  if (at != s.size())
    return false;
  const size_t cmap = tables[1], loca = tables[2], glyph = tables[3];
  const uint32_t count = u32(loca + 8), index_width = u8(34) == 0 ? 2 : 4;
  if (u8(34) > 1 || count < 2 || count > 2048 ||
      12 + uint64_t(count) * index_width > u32(loca))
    return false;
  const unsigned metric_bits = u8(40) + 2 * u8(38) + 2 * u8(39);
  std::vector<uint32_t> positions;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t p =
        index_width == 2 ? u16(loca + 12 + i * 2) : u32(loca + 12 + i * 4);
    if (p < 8 || p > u32(glyph) || (!positions.empty() && p < positions.back()))
      return false;
    positions.push_back(p);
  }
  positions.push_back(u32(glyph));
  for (unsigned i = 1; i < count; ++i) {
    const size_t start = positions[i], end = positions[i + 1];
    if (end - start < (metric_bits + 7) / 8)
      return false;
    unsigned bit = u8(40) + 2 * u8(38);
    const auto bits = [&](unsigned n) {
      unsigned v = 0;
      while (n--) {
        v = (v << 1) | ((u8(glyph + start + bit / 8) >> (7 - bit % 8)) & 1);
        ++bit;
      }
      return v;
    };
    const unsigned w = bits(u8(39)), h = bits(u8(39));
    if (w > 96 || h > 96 ||
        (uint64_t(w) * h * u8(37) + metric_bits + 7) / 8 > end - start)
      return false;
  }
  const uint32_t maps = u32(cmap + 8);
  if (!maps || maps > 32 || 12 + uint64_t(maps) * 16 > u32(cmap))
    return false;
  for (unsigned i = 0; i < maps; ++i) {
    const size_t t = cmap + 12 + i * 16;
    const auto offset = u32(t);
    const unsigned range = u16(t + 8), first = u16(t + 10),
                   entries = u16(t + 12), format = u8(t + 14);
    if (!range || !first || first >= count || format > 3 || offset > u32(cmap))
      return false;
    if (format == 2) {
      if (uint64_t(first) + range > count)
        return false;
      continue;
    }
    if (format == 0) {
      if (entries != range || entries > 255 || offset + entries > u32(cmap))
        return false;
      for (unsigned n = 0; n < entries; ++n)
        if (first + u8(cmap + offset + n) >= count)
          return false;
    } else {
      if (!entries ||
          uint64_t(offset) + entries * (format == 1 ? 4 : 2) > u32(cmap))
        return false;
      unsigned previous = 0;
      for (unsigned n = 0; n < entries; ++n) {
        const unsigned code = u16(cmap + offset + n * 2);
        if (code >= range || (n && code <= previous))
          return false;
        previous = code;
        const unsigned id =
            format == 1 ? u16(cmap + offset + entries * 2 + n * 2) : n;
        if (first + id >= count)
          return false;
      }
    }
  }
  return true;
}
} // namespace content
