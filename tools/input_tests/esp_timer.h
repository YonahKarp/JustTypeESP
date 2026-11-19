#pragma once
#include <cstdint>
inline int64_t esp_timer_get_time() { static int64_t now = 10000000; return ++now; }
