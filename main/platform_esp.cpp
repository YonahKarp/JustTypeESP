#include "justtype/platforms/platform.h"
#include "keyboard_input.h"
#include "keyboard_timing.h"
#include "backlight.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

namespace platform {
namespace {

constexpr int LCD_H_RES = 800;
constexpr int LCD_V_RES = 480;
constexpr int LCD_BKLT_GPIO = 2;

// Prototype clock: November 7, 2026, 8:56:09 PM America/New_York.
// Each boot starts here; replace with the battery-backed RTC when available.
constexpr time_t kBaseStartupTime = 1794102969;
constexpr const char *kLocalTimezone = "EST5EDT,M3.2.0/2,M11.1.0/2";

static const char *TAG = "platform_esp";

lv_display_t *g_display = nullptr;
lv_indev_t *g_keyboard = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
esp_timer_handle_t g_tick_timer = nullptr;
std::string g_clipboard;
PlatformEvent g_last_event {};
wl_handle_t g_documents_wl = WL_INVALID_HANDLE;
SemaphoreHandle_t g_frame_complete = nullptr;
DRAM_ATTR StaticSemaphore_t g_frame_complete_storage;
uint32_t g_frame_sequence = 0;

struct AnimationTrace {
    const char *name = nullptr;
    int64_t started_us = 0, refresh_us = 0, wait_us = 0;
    int64_t refresh_started_us = 0, refresh_wait_before_us = 0;
    int64_t layout_us = 0, draw_started_us = 0;
    int64_t previous_present_us = 0, max_gap_us = 0, first_present_us = 0;
    uint32_t frames = 0, refresh_frames_before = 0;
    bool finish_requested = false, finish_presented = false;
} g_animation_trace;

void animation_refresh_event(lv_event_t *event) {
    auto &trace = g_animation_trace;
    if (!trace.name) return;
    if (lv_event_get_code(event) == LV_EVENT_REFR_START) {
        trace.refresh_started_us = esp_timer_get_time();
        trace.refresh_wait_before_us = trace.wait_us;
        trace.refresh_frames_before = trace.frames;
    } else if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        trace.draw_started_us = esp_timer_get_time();
        trace.layout_us += trace.draw_started_us - trace.refresh_started_us;
    } else if (lv_event_get_code(event) == LV_EVENT_REFR_READY &&
               trace.frames != trace.refresh_frames_before) {
        // Layout, drawing and direct-buffer copies, excluding the LCD wait.
        trace.refresh_us += esp_timer_get_time() - trace.refresh_started_us -
                            (trace.wait_us - trace.refresh_wait_before_us);
    }
}

void lvgl_tick_cb(void *) {
    lv_tick_inc(2);
}

// In bounce mode this callback runs after the driver has finished reading a
// framebuffer and selected the source for the next frame.
bool IRAM_ATTR frame_complete_cb(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    __atomic_fetch_add(&g_frame_sequence, 1, __ATOMIC_RELAXED);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(g_frame_complete, &woken);
    return woken == pdTRUE;
}

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *, uint8_t *px_map) {
    // LVGL direct mode may flush multiple invalidated areas in one refresh.
    // Present only after the last area has been rendered into the back buffer.
    if (lv_display_flush_is_last(disp)) {
        const int64_t wait_started_us = g_animation_trace.name ? esp_timer_get_time() : 0;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_H_RES, LCD_V_RES, px_map));
        // A callback after this snapshot guarantees the driver has selected
        // the submitted buffer, even if a frame ended during draw_bitmap().
        uint32_t sequence = __atomic_load_n(&g_frame_sequence, __ATOMIC_RELAXED);
        while (__atomic_load_n(&g_frame_sequence, __ATOMIC_RELAXED) == sequence) {
            if (xSemaphoreTake(g_frame_complete, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGE(TAG, "Timed out waiting for LCD framebuffer completion");
                // Never let LVGL reuse a framebuffer while its ownership is unknown.
                continue;
            }
        }
        if (g_animation_trace.name) {
            auto &trace = g_animation_trace;
            const int64_t now = esp_timer_get_time();
            trace.wait_us += now - wait_started_us;
            if (trace.previous_present_us)
                trace.max_gap_us = std::max(trace.max_gap_us, now - trace.previous_present_us);
            else
                trace.first_present_us = now - trace.started_us;
            trace.previous_present_us = now;
            ++trace.frames;
            if (trace.finish_requested) trace.finish_presented = true;
        }
    }
    lv_display_flush_ready(disp);
}

void keyboard_read_cb(lv_indev_t *, lv_indev_data_t *data) {
    keyboard_input::read_lvgl(data);
}

bool init_spiffs() {
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/legacy",
        .partition_label = nullptr,
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Legacy SPIFFS mount unavailable: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool documents_partition_is_blank(const esp_partition_t *partition) {
    uint8_t block[4096];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
        size_t count = std::min(sizeof(block), static_cast<size_t>(partition->size) - offset);
        if (esp_partition_read(partition, offset, block, count) != ESP_OK) return false;
        for (size_t i = 0; i < count; ++i) {
            if (block[i] != 0xff) return false;
        }
    }
    return true;
}

bool make_parent_dirs(const std::string &path) {
    for (size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        std::string parent = path.substr(0, slash);
        struct stat st;
        if (stat(parent.c_str(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) return false;
        } else if (mkdir(parent.c_str(), 0755) != 0) {
            return false;
        }
    }
    return true;
}

bool migrate_legacy_files() {
    const char *marker = "/spiffs/.legacy_migrated";
    struct stat st;
    if (stat(marker, &st) == 0) return true;

    DIR *dir = opendir("/legacy/user_files");
    if (!dir) return true;

    bool success = true;
    unsigned copied = 0;
    while (struct dirent *entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || name.empty()) continue;
        std::string source = "/legacy/user_files/" + name;
        std::string target = "/spiffs/user_files/" + name;
        if (stat(target.c_str(), &st) == 0) continue;
        if (!make_parent_dirs(target)) { success = false; break; }

        std::ifstream input(source, std::ios::binary);
        if (!input) { success = false; break; }
        std::string temporary = target + ".migrating";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { success = false; break; }
        char buffer[1024];
        while (input) {
            input.read(buffer, sizeof(buffer));
            output.write(buffer, input.gcount());
        }
        output.close();
        if (!input.eof() || !output || std::rename(temporary.c_str(), target.c_str()) != 0) {
            std::remove(temporary.c_str());
            success = false;
            break;
        }
        ++copied;
    }
    closedir(dir);
    if (success) {
        std::ofstream done(marker);
        success = static_cast<bool>(done);
    }
    if (success) ESP_LOGI(TAG, "Legacy migration complete: %u files copied", copied);
    if (!success) ESP_LOGE(TAG, "Legacy document migration incomplete; originals remain untouched");
    return success;
}

bool init_documents_storage(bool legacy_available) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "documents");
    if (!partition) {
        ESP_LOGE(TAG, "Documents partition not found");
        return false;
    }
    esp_vfs_fat_mount_config_t config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    config.max_files = 12;
    config.format_if_mount_failed = documents_partition_is_blank(partition);
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/spiffs", "documents", &config, &g_documents_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Documents FAT mount failed (not formatting existing data): %s", esp_err_to_name(err));
        return false;
    }
    return !legacy_available || migrate_legacy_files();
}

}  // namespace

bool init(int32_t width, int32_t height) {
    setenv("TZ", kLocalTimezone, 1);
    tzset();
    timeval startup_time {};
    startup_time.tv_sec = kBaseStartupTime;
    if (settimeofday(&startup_time, nullptr) != 0) {
        ESP_LOGE(TAG, "Failed to initialize prototype clock");
        return false;
    }
    ESP_LOGI(TAG, "Prototype clock starts at %lld (Unix seconds)",
             static_cast<long long>(kBaseStartupTime));

    if (width != LCD_H_RES || height != LCD_V_RES) {
        ESP_LOGW(TAG, "Ignoring requested size %ldx%ld, using %dx%d", (long)width, (long)height, LCD_H_RES, LCD_V_RES);
    }

    bool legacy_available = init_spiffs();
    if (!init_documents_storage(legacy_available)) {
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
    timings.h_res = LCD_H_RES;
    timings.v_res = LCD_V_RES;
#if CONFIG_LEAF_LCD_TIMING_COMPARISON
    constexpr const char *timing_preset = "comparison";
    // Controlled comparison with LovyanGFX's ESP32-8048S070 board preset:
    // https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/LGFX_Sunton_ESP32-8048S070.h
    timings.pclk_hz = 14000000;
    timings.hsync_pulse_width = 4;
    timings.hsync_back_porch = 16;
    timings.hsync_front_porch = 80;
    timings.vsync_pulse_width = 4;
    timings.vsync_back_porch = 4;
    timings.vsync_front_porch = 22;
    timings.flags.pclk_idle_high = 1;
#else
    constexpr const char *timing_preset = "baseline";
    timings.pclk_hz = 16000000;
    timings.hsync_pulse_width = 30;
    timings.hsync_back_porch = 16;
    timings.hsync_front_porch = 210;
    timings.vsync_pulse_width = 13;
    timings.vsync_back_porch = 10;
    timings.vsync_front_porch = 22;
#endif
    timings.flags.pclk_active_neg = 1;
    const uint32_t total_pixels =
        (LCD_H_RES + timings.hsync_pulse_width + timings.hsync_back_porch + timings.hsync_front_porch) *
        (LCD_V_RES + timings.vsync_pulse_width + timings.vsync_back_porch + timings.vsync_front_porch);
    ESP_LOGI(TAG, "LCD timing %s: requested PCLK %lu Hz, nominal refresh %.2f Hz, clock idle %s",
             timing_preset,
             static_cast<unsigned long>(timings.pclk_hz),
             static_cast<double>(timings.pclk_hz) / total_pixels,
             timings.flags.pclk_idle_high ? "high" : "low");

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings = timings;
    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 2;
    // Twelve lines retain a refill margin while leaving 25.6 KiB more internal
    // DMA memory for Wi-Fi alongside BLE. This must divide the frame height.
    panel_config.bounce_buffer_size_px = LCD_H_RES * 12;
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

    // The LCD ISR must keep refilling bounce buffers during document flash writes.
    // Keep its semaphore in internal RAM, accessible throughout those operations.
    g_frame_complete = xSemaphoreCreateBinaryStatic(&g_frame_complete_storage);
    if (!g_frame_complete) {
        ESP_LOGE(TAG, "LCD frame semaphore allocation failed");
        return false;
    }
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &g_panel));
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_frame_buf_complete = frame_complete_cb;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(g_panel, &callbacks, nullptr));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));

    g_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!g_display) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return false;
    }

    // The driver starts scanning framebuffer 0. Let LVGL render first into
    // framebuffer 1, then alternate after each completed presentation.
    void *front = nullptr;
    void *back = nullptr;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(g_panel, 2, &front, &back));
    const uint32_t frame_bytes = LCD_H_RES * LCD_V_RES * 2;
    std::memset(front, 0, frame_bytes);
    std::memset(back, 0, frame_bytes);
    lv_display_set_buffers(g_display, back, front, frame_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(g_display, lvgl_flush_cb);
    lv_display_add_event_cb(g_display, animation_refresh_event, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(g_display, animation_refresh_event, LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(g_display, animation_refresh_event, LV_EVENT_REFR_READY, nullptr);
    lv_display_set_default(g_display);

    g_keyboard = lv_indev_create();
    if (g_keyboard) {
        lv_indev_set_type(g_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(g_keyboard, keyboard_read_cb);
        configure_keyboard_repeat(g_keyboard);
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

    backlight::init(LCD_BKLT_GPIO);
    return true;
}

void shutdown() {
    backlight::shutdown();
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
    g_last_event = {};
    if (!keyboard_input::poll(event, timeout_ms)) return false;
    g_last_event = event;
    return true;
}

void requeue_last_event() {
    // Deliver each edge before a following release can replace it. Waiting for
    // the periodic input timer adds latency and can miss short key presses.
    if (g_keyboard && keyboard_input::release_lvgl_before(g_last_event)) lv_indev_read(g_keyboard);
    if (keyboard_input::forward_to_lvgl(g_last_event) && g_keyboard) {
        lv_indev_read(g_keyboard);
    }
}

void pump_events() {
    backlight::service(!g_animation_trace.name);
}

void begin_ui_animation(const char *name) {
    g_animation_trace.finish_presented = true;
    end_ui_animation(); // A reversal starts a separate sample.
    g_animation_trace = {};
    g_animation_trace.name = name;
    g_animation_trace.started_us = esp_timer_get_time();
}

void finish_ui_animation() {
    g_animation_trace.finish_requested = true;
}

void end_ui_animation() {
    if (!g_animation_trace.name) return;
    const auto trace = g_animation_trace;
    if (!trace.finish_presented && esp_timer_get_time() - trace.started_us < 1000000) return;
    g_animation_trace.name = nullptr;
    const double frames = std::max<uint32_t>(trace.frames, 1);
    ESP_LOGI("ui_animation", "%s: %lu presentations, elapsed %.1f ms, first %.1f ms, "
             "work/frame %.1f ms (layout/copy %.1f), LCD wait/frame %.1f ms, max gap %.1f ms",
             trace.name, static_cast<unsigned long>(trace.frames),
             (esp_timer_get_time() - trace.started_us) / 1000.0,
             trace.first_present_us / 1000.0, trace.refresh_us / frames / 1000.0,
             trace.layout_us / frames / 1000.0,
             trace.wait_us / frames / 1000.0, trace.max_gap_us / 1000.0);
}

KeyModifiers get_key_modifiers() {
    return keyboard_input::modifiers();
}

bool is_key_pressed(KeyCode key) {
    return keyboard_input::pressed(key);
}

bool was_key_pressed(KeyCode key) {
    return g_last_event.type == PlatformEvent::Type::KeyDown &&
           g_last_event.key == key && !g_last_event.repeat;
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

