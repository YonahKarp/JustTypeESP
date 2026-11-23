#include "backlight.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <algorithm>

namespace backlight {
namespace {
constexpr int minimum = 10, maximum = 100, step = 10;
constexpr uint32_t full_duty = 1u << 10;
constexpr int64_t save_delay_us = 750000, retry_delay_us = 5000000;
constexpr auto speed = LEDC_LOW_SPEED_MODE;
constexpr auto timer = LEDC_TIMER_0;
constexpr auto channel = LEDC_CHANNEL_0;
constexpr const char* tag = "backlight";
// Reset the previous PWM preference once: low duties could leave this board dark.
constexpr const char* brightness_key = "brightness_v3";
int current = maximum, saved = maximum;
int64_t save_at = 0;
bool ready = false, pending = false;

uint32_t duty(int value) { return (value * full_duty + 50) / 100; }

void persist() {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("leaf_display", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, brightness_key, static_cast<uint8_t>(current));
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        saved = current;
        pending = false;
    } else {
        ESP_LOGW(tag, "Brightness save failed: %s; will retry", esp_err_to_name(result));
        save_at = esp_timer_get_time() + retry_delay_us;
    }
}
}

bool init(int gpio) {
    current = saved = maximum;
    pending = ready = false;
    nvs_handle_t handle;
    if (nvs_open("leaf_display", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = maximum;
        if (nvs_get_u8(handle, brightness_key, &value) == ESP_OK && value >= minimum && value <= maximum)
            current = saved = value;
        nvs_close(handle);
    }

    ledc_timer_config_t config{};
    config.speed_mode = speed;
    config.timer_num = timer;
    config.duty_resolution = LEDC_TIMER_10_BIT;
    // Allow longer enable pulses at low duty on the board's boost driver.
    // 20 kHz blanked below 80%; 1.2 kHz still blanked at 20%.
    config.freq_hz = 300;
    config.clk_cfg = LEDC_USE_XTAL_CLK;
    esp_err_t result = ledc_timer_config(&config);
    if (result == ESP_OK) {
        ledc_channel_config_t output{};
        output.gpio_num = gpio;
        output.speed_mode = speed;
        output.channel = channel;
        output.timer_sel = timer;
        output.intr_type = LEDC_INTR_DISABLE;
        output.duty = duty(current);
        result = ledc_channel_config(&output);
    }
    if (result != ESP_OK) {
        // Keep the prototype readable if PWM setup fails. Do not overwrite the
        // saved preference or prevent the rest of the application from starting.
        gpio_reset_pin(static_cast<gpio_num_t>(gpio));
        gpio_set_direction(static_cast<gpio_num_t>(gpio), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(gpio), 1);
        current = maximum;
        ESP_LOGE(tag, "PWM unavailable: %s; using full brightness", esp_err_to_name(result));
        return false;
    }
    ready = true;
    ESP_LOGI(tag, "Brightness %d%%; hardware PWM 300 Hz on GPIO %d", current, gpio);
    return true;
}

int percent() { return current; }

bool adjust(int delta) {
    if (!ready) return false;
    const int value = std::clamp(current + delta, minimum, maximum);
    if (value == current) return true;
    esp_err_t result = ledc_set_duty(speed, channel, duty(value));
    if (result == ESP_OK) result = ledc_update_duty(speed, channel);
    if (result != ESP_OK) {
        ESP_LOGW(tag, "Brightness adjustment failed: %s", esp_err_to_name(result));
        return false;
    }
    current = value;
    pending = current != saved;
    save_at = esp_timer_get_time() + save_delay_us;
    ESP_LOGI(tag, "Brightness %d%%", current);
    return true;
}

bool handle_event(const platform::PlatformEvent& event) {
    if (!event.modifiers.alt || event.modifiers.ctrl || event.modifiers.meta) return false;
    if (event.type == platform::PlatformEvent::Type::KeyDown) {
        if (event.hid_usage == 0x2d || (!event.hid_usage && event.key == platform::KeyCode::Minus)) { adjust(-step); return true; }
        if (event.hid_usage == 0x2e || (!event.hid_usage && event.key == platform::KeyCode::Equals)) { adjust(step); return true; }
    }
    // Right Option/AltGr can also generate text. Consume only the text paired
    // with these physical keys, including inside the Wi-Fi password form.
    return event.type == platform::PlatformEvent::Type::TextInput &&
           (event.hid_usage == 0x2d || event.hid_usage == 0x2e);
}

void service(bool allow_save) {
    if (allow_save && pending && esp_timer_get_time() >= save_at) persist();
}

void shutdown() {
    if (pending) persist();
    if (ready) ledc_stop(speed, channel, 0);
    ready = false;
}
}
