#pragma once
#include "esp_err.h"
#include <cstdint>
constexpr int LEDC_LOW_SPEED_MODE=0, LEDC_TIMER_0=0, LEDC_CHANNEL_0=0,
              LEDC_TIMER_10_BIT=10, LEDC_USE_XTAL_CLK=1, LEDC_INTR_DISABLE=0;
struct ledc_timer_config_t { int speed_mode=0, duty_resolution=0, timer_num=0; uint32_t freq_hz=0; int clk_cfg=0; };
struct ledc_channel_config_t { int gpio_num=0, speed_mode=0, channel=0, intr_type=0, timer_sel=0; uint32_t duty=0; };
inline ledc_timer_config_t fake_timer;
inline uint32_t fake_duty=0, fake_pending_duty=0;
inline int fake_timer_error=0, fake_update_error=0, fake_updates=0, fake_stops=0;
inline int ledc_timer_config(const ledc_timer_config_t* config) { fake_timer=*config; return fake_timer_error; }
inline int ledc_channel_config(const ledc_channel_config_t* config) { fake_duty=config->duty; return ESP_OK; }
inline int ledc_set_duty(int, int, uint32_t duty) { fake_pending_duty=duty; return ESP_OK; }
inline int ledc_update_duty(int, int) {
    ++fake_updates;
    if (fake_update_error) return fake_update_error;
    fake_duty=fake_pending_duty; return ESP_OK;
}
inline int ledc_stop(int, int, int) { ++fake_stops; return ESP_OK; }
