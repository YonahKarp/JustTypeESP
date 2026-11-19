#pragma once
#include <cstdint>
#include <emscripten.h>
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(emscripten_get_now() * 1000); }
