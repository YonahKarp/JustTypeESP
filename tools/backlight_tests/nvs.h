#pragma once
#include "esp_err.h"
#include <cstdint>
#include <cstring>
using nvs_handle_t = int;
constexpr int NVS_READONLY=0, NVS_READWRITE=1;
inline bool fake_saved_exists=false, fake_legacy_exists=true;
inline uint8_t fake_legacy_value=30;
inline uint8_t fake_saved_value=100, fake_staged_value=100;
inline int fake_nvs_error=0, fake_commits=0;
inline int nvs_open(const char*, int mode, nvs_handle_t* handle) {
    if (mode==NVS_READONLY && !fake_saved_exists && !fake_legacy_exists) return ESP_ERR_NVS_NOT_FOUND;
    *handle=1; return ESP_OK;
}
inline int nvs_get_u8(int, const char* key, uint8_t* value) {
    if (!std::strcmp(key,"brightness") && fake_legacy_exists) { *value=fake_legacy_value; return ESP_OK; }
    if (std::strcmp(key,"brightness_v3") || !fake_saved_exists) return ESP_ERR_NVS_NOT_FOUND;
    *value=fake_saved_value; return ESP_OK;
}
inline int nvs_set_u8(int, const char* key, uint8_t value) {
    if (std::strcmp(key,"brightness_v3")) return ESP_FAIL;
    fake_staged_value=value; return ESP_OK;
}
inline int nvs_commit(int) {
    ++fake_commits;
    if (fake_nvs_error) return fake_nvs_error;
    fake_saved_value=fake_staged_value; fake_saved_exists=true; return ESP_OK;
}
inline void nvs_close(int) {}
