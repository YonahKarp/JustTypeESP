/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

#define LCD_H_RES      (800)
#define LCD_V_RES      (480)
#define LCD_BKLT_GPIO  (2)

static const char *TAG = "hello_lcd";
static uint16_t s_line_buf[LCD_H_RES];

static bool get_glyph_5x7(char c, uint8_t out_rows[7])
{
    switch (c) {
        case 'H':
            out_rows[0] = 0x11; out_rows[1] = 0x11; out_rows[2] = 0x1F; out_rows[3] = 0x11;
            out_rows[4] = 0x11; out_rows[5] = 0x11; out_rows[6] = 0x11; return true;
        case 'E':
            out_rows[0] = 0x1F; out_rows[1] = 0x10; out_rows[2] = 0x1E; out_rows[3] = 0x10;
            out_rows[4] = 0x10; out_rows[5] = 0x10; out_rows[6] = 0x1F; return true;
        case 'L':
            out_rows[0] = 0x10; out_rows[1] = 0x10; out_rows[2] = 0x10; out_rows[3] = 0x10;
            out_rows[4] = 0x10; out_rows[5] = 0x10; out_rows[6] = 0x1F; return true;
        case 'O':
            out_rows[0] = 0x0E; out_rows[1] = 0x11; out_rows[2] = 0x11; out_rows[3] = 0x11;
            out_rows[4] = 0x11; out_rows[5] = 0x11; out_rows[6] = 0x0E; return true;
        case 'W':
            out_rows[0] = 0x11; out_rows[1] = 0x11; out_rows[2] = 0x11; out_rows[3] = 0x15;
            out_rows[4] = 0x15; out_rows[5] = 0x1B; out_rows[6] = 0x11; return true;
        case 'R':
            out_rows[0] = 0x1E; out_rows[1] = 0x11; out_rows[2] = 0x11; out_rows[3] = 0x1E;
            out_rows[4] = 0x14; out_rows[5] = 0x12; out_rows[6] = 0x11; return true;
        case 'D':
            out_rows[0] = 0x1E; out_rows[1] = 0x11; out_rows[2] = 0x11; out_rows[3] = 0x11;
            out_rows[4] = 0x11; out_rows[5] = 0x11; out_rows[6] = 0x1E; return true;
        case ' ':
            memset(out_rows, 0, 7);
            return true;
        default:
            return false;
    }
}

static void draw_square(esp_lcd_panel_handle_t panel, int x, int y, int size, uint16_t color)
{
    uint16_t px[64];
    for (int i = 0; i < size * size; i++) {
        px[i] = color;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y, x + size, y + size, px));
}

static void draw_text_5x7_scaled(esp_lcd_panel_handle_t panel, int x, int y, const char *text, int scale, uint16_t color)
{
    uint8_t rows[7];
    for (size_t idx = 0; idx < strlen(text); idx++) {
        if (!get_glyph_5x7(text[idx], rows)) {
            continue;
        }
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if ((rows[row] >> (4 - col)) & 0x01) {
                    draw_square(panel, x + (int)idx * (6 * scale) + col * scale, y + row * scale, scale, color);
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing RGB panel and backlight");

    ESP_ERROR_CHECK(gpio_set_direction(LCD_BKLT_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LCD_BKLT_GPIO, 1));

    const int rgb_pins[16] = {
        15, 7, 6, 5, 4,    /* B0..B4 */
        9, 46, 3, 8, 16, 1,/* G0..G5 */
        14, 21, 47, 48, 45 /* R0..R4 */
    };

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 12000000,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 16,
            .hsync_front_porch = 210,
            .hsync_pulse_width = 30,
            .vsync_back_porch = 10,
            .vsync_front_porch = 22,
            .vsync_pulse_width = 13,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .num_fbs = 1,
        .hsync_gpio_num = 39,
        .vsync_gpio_num = 40,
        .de_gpio_num = 41,
        .pclk_gpio_num = 42,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            rgb_pins[0], rgb_pins[1], rgb_pins[2], rgb_pins[3],
            rgb_pins[4], rgb_pins[5], rgb_pins[6], rgb_pins[7],
            rgb_pins[8], rgb_pins[9], rgb_pins[10], rgb_pins[11],
            rgb_pins[12], rgb_pins[13], rgb_pins[14], rgb_pins[15],
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    for (int i = 0; i < LCD_H_RES; i++) {
        s_line_buf[i] = 0x0000;
    }
    for (int y = 0; y < LCD_V_RES; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + 1, s_line_buf));
    }

    const char *msg = "HELLO WORLD";
    const int scale = 8;
    const int msg_w = ((int)strlen(msg) * (5 * scale)) + (((int)strlen(msg) - 1) * scale);
    const int msg_h = 7 * scale;
    const int start_x = (LCD_H_RES - msg_w) / 2;
    const int start_y = (LCD_V_RES - msg_h) / 2;

    draw_text_5x7_scaled(panel, start_x, start_y, msg, scale, 0xFFFF);
    ESP_LOGI(TAG, "Displayed: %s", msg);
}
