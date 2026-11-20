#pragma once
#include "esp_err.h"
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
inline int fake_gpio_level = 0;
inline int gpio_reset_pin(int) { fake_gpio_level=0; return ESP_OK; }
inline int gpio_set_direction(int, int) { return ESP_OK; }
inline int gpio_set_level(int, int level) { fake_gpio_level=level; return ESP_OK; }
