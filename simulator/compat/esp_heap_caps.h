#pragma once
#include <cstdlib>
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline void* heap_caps_malloc(size_t bytes, unsigned) { return std::malloc(bytes); }
inline void heap_caps_free(void* p) { std::free(p); }
// Simulator heap policy only; this does not model ESP32 memory pressure.
inline size_t heap_caps_get_free_size(unsigned) { return 16 * 1024 * 1024; }
