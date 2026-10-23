#include "justtype/platforms/platform.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace platform {
namespace {

constexpr int LCD_H_RES = 800;
constexpr int LCD_V_RES = 480;
constexpr int LCD_BKLT_GPIO = 2;

static const char *TAG = "platform_esp";

lv_display_t *g_display = nullptr;
lv_indev_t *g_keyboard = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
esp_timer_handle_t g_tick_timer = nullptr;
std::string g_clipboard;

void lvgl_tick_cb(void *) {
    lv_tick_inc(2);
}

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(g_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

void keyboard_read_cb(lv_indev_t *, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;
}

bool init_spiffs() {
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace

bool init(int32_t width, int32_t height) {
    if (width != LCD_H_RES || height != LCD_V_RES) {
        ESP_LOGW(TAG, "Ignoring requested size %ldx%ld, using %dx%d", (long)width, (long)height, LCD_H_RES, LCD_V_RES);
    }

    if (!init_spiffs()) {
        return false;
    }

    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)LCD_BKLT_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)LCD_BKLT_GPIO, 0));

    const int rgb_pins[16] = {
        15, 7, 6, 5, 4,
        9, 46, 3, 8, 16, 1,
        14, 21, 47, 48, 45
    };

    esp_lcd_rgb_timing_t timings = {};
    timings.pclk_hz = 12000000;
    timings.h_res = LCD_H_RES;
    timings.v_res = LCD_V_RES;
    timings.hsync_pulse_width = 30;
    timings.hsync_back_porch = 16;
    timings.hsync_front_porch = 210;
    timings.vsync_pulse_width = 13;
    timings.vsync_back_porch = 10;
    timings.vsync_front_porch = 22;
    timings.flags.pclk_active_neg = 1;

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings = timings;
    panel_config.data_width = 16;
    panel_config.in_color_format = LCD_COLOR_FMT_RGB565;
    panel_config.out_color_format = LCD_COLOR_FMT_RGB565;
    panel_config.num_fbs = 2;
    panel_config.bounce_buffer_size_px = LCD_H_RES * 10;
    panel_config.dma_burst_size = 64;
    panel_config.hsync_gpio_num = (gpio_num_t)39;
    panel_config.vsync_gpio_num = (gpio_num_t)40;
    panel_config.de_gpio_num = (gpio_num_t)41;
    panel_config.pclk_gpio_num = (gpio_num_t)42;
    panel_config.disp_gpio_num = GPIO_NUM_NC;
    for (int i = 0; i < 16; i++) {
        panel_config.data_gpio_nums[i] = (gpio_num_t)rgb_pins[i];
    }
    panel_config.flags.fb_in_psram = 1;
    panel_config.flags.double_fb = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));

    g_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!g_display) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return false;
    }

    const uint32_t draw_buf_lines = 40;
    const uint32_t draw_buf_px = LCD_H_RES * draw_buf_lines;
    const uint32_t draw_buf_bytes = draw_buf_px * sizeof(lv_color_t);

    void *buf1 = heap_caps_malloc(draw_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(draw_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "LVGL draw buffers alloc failed");
        return false;
    }

    lv_display_set_buffers(g_display, buf1, buf2, draw_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_display, lvgl_flush_cb);
    lv_display_set_default(g_display);

    g_keyboard = lv_indev_create();
    if (g_keyboard) {
        lv_indev_set_type(g_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(g_keyboard, keyboard_read_cb);
    }

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lv_tick",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(g_tick_timer, 2000));

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)LCD_BKLT_GPIO, 1));
    return true;
}

void shutdown() {
    if (g_tick_timer) {
        esp_timer_stop(g_tick_timer);
        esp_timer_delete(g_tick_timer);
        g_tick_timer = nullptr;
    }

    g_keyboard = nullptr;
    g_display = nullptr;
    g_clipboard.clear();
}

lv_display_t *get_display() {
    return g_display;
}

lv_indev_t *get_keyboard_indev() {
    return g_keyboard;
}

bool poll_event(PlatformEvent &event, uint32_t timeout_ms) {
    event = {};
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    return false;
}

void requeue_last_event() {
}

void pump_events() {
}

KeyModifiers get_key_modifiers() {
    return {};
}

bool is_key_pressed(KeyCode) {
    return false;
}

bool clipboard_set(const char *text) {
    g_clipboard = text ? text : "";
    return true;
}

char *clipboard_get() {
    char *text = static_cast<char *>(std::malloc(g_clipboard.size() + 1));
    if (!text) {
        return nullptr;
    }
    std::memcpy(text, g_clipboard.c_str(), g_clipboard.size() + 1);
    return text;
}

void clipboard_free(char *text) {
    std::free(text);
}

uint32_t get_ticks_ms() {
    return lv_tick_get();
}

}  // namespace platform
