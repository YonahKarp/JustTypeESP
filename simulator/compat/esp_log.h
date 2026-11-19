#pragma once
#include <cstdio>
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
