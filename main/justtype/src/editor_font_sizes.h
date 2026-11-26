#pragma once
#include <algorithm>
#include <cstdlib>
// Keep legacy persisted indices so older documents/settings migrate without a
// separate marker. Ties choose the smaller size.
inline constexpr int editor_font_indices[] = {10, 1, 3, 5};
inline constexpr int editor_font_sizes[] = {16, 20, 24, 28};
inline int editor_font_choice(int legacy) {
  const int oldSizes[] = {18,20,22,24,26,28,30,32,36,40,16};
  const int size = oldSizes[std::clamp(legacy, 0, 10)];
  int best=0;
  for(int i=1;i<4;++i)
    if(std::abs(size-editor_font_sizes[i]) < std::abs(size-editor_font_sizes[best]))best=i;
  return best;
}
inline int editor_font_index(int legacy) { return editor_font_indices[editor_font_choice(legacy)]; }
