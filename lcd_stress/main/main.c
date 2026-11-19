#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define W 800
#define H 480
#define PHASE_SEC 10
static volatile uint32_t vsyncs;
static volatile uint32_t work[2];
static bool vsync_cb(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *e, void *ctx) { (void)p; (void)e; (void)ctx; __atomic_fetch_add(&vsyncs, 1, __ATOMIC_RELAXED); return false; }
static void worker(void *arg) { int cpu = (int)(intptr_t)arg; for (;;) { for (int i=0;i<100000;i++) __asm__ volatile("" ::: "memory"); uint32_t n = __atomic_add_fetch(&work[cpu], 1, __ATOMIC_RELAXED); if ((n & 63) == 0) vTaskDelay(1); } }
static esp_lcd_panel_handle_t make_panel(int bounce) {
    const int pins[16] = {15,7,6,5,4,9,46,3,8,16,1,14,21,47,48,45};
    esp_lcd_rgb_panel_config_t c = {0};
    c.clk_src=LCD_CLK_SRC_DEFAULT; c.timings.pclk_hz=16000000; c.timings.h_res=W; c.timings.v_res=H;
    c.timings.hsync_pulse_width=30; c.timings.hsync_back_porch=16; c.timings.hsync_front_porch=210;
    c.timings.vsync_pulse_width=13; c.timings.vsync_back_porch=10; c.timings.vsync_front_porch=22; c.timings.flags.pclk_active_neg=1;
    c.data_width=16; c.bits_per_pixel=16; c.num_fbs=1; c.bounce_buffer_size_px=bounce ? W*10 : 0; c.dma_burst_size=64;
    c.hsync_gpio_num=39; c.vsync_gpio_num=40; c.de_gpio_num=41; c.pclk_gpio_num=42; c.disp_gpio_num=GPIO_NUM_NC;
    for(int i=0;i<16;i++) { c.data_gpio_nums[i]=pins[i]; } c.flags.fb_in_psram=1;
    esp_lcd_panel_handle_t p; ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&c,&p));
    esp_lcd_rgb_panel_event_callbacks_t cb={.on_vsync=vsync_cb}; ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(p,&cb,NULL));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(p)); ESP_ERROR_CHECK(esp_lcd_panel_init(p));
    uint16_t *fb; ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(p,1,(void **)&fb));
    const uint16_t colors[]={0xffff,0xffe0,0x07ff,0x07e0,0xf81f,0xf800,0x001f,0x0000};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) fb[y*W+x] = (y < 400) ? colors[x/100] : (((x/8+y/8)&1) ? 0xffff : 0);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(p,0,0,W,H,fb)); return p;
}

// Five-by-seven glyphs, drawn at 3x scale on a dark status strip.
static const char *glyph(char c) {
    switch (c) {
#define G(ch, bits) case ch: return bits
    G('A',"01110100011000111111100011000110001"); G('B',"11110100011000111110100011000111110");
    G('C',"01111100001000010000100001000001111"); G('D',"11110100011000110001100011000111110");
    G('E',"11111100001000011110100001000011111"); G('F',"11111100001000011110100001000010000");
    G('G',"01111100001000010111100011000101111"); G('H',"10001100011000111111100011000110001");
    G('I',"11111001000010000100001000010011111"); G('J',"00111000100001000010100101001001100");
    G('K',"10001100101010011000101001001010001"); G('L',"10000100001000010000100001000011111");
    G('M',"10001110111010110101100011000110001"); G('N',"10001110011010110011100011000110001");
    G('O',"01110100011000110001100011000101110"); G('P',"11110100011000111110100001000010000");
    G('Q',"01110100011000110001101011001001101"); G('R',"11110100011000111110101001001010001");
    G('S',"01111100001000001110000010000111110"); G('T',"11111001000010000100001000010000100");
    G('U',"10001100011000110001100011000101110"); G('V',"10001100011000110001100010101000100");
    G('W',"10001100011000110101101011101110001"); G('X',"10001100010101000100010101000110001");
    G('Y',"10001100010101000100001000010000100"); G('Z',"11111000010001000100010001000011111");
    G('0',"01110100011001110101110011000101110"); G('1',"00100011000010000100001000010001110");
    G('2',"01110100010000100010001000100011111"); G('3',"11110000010000101110000010000111110");
    G('4',"00010001100101010010111110001000010"); G('5',"11111100001000011110000010000111110");
    G('6',"01110100001000011110100011000101110"); G('7',"11111000010001000100010000100001000");
    G('8',"01110100011000101110100011000101110"); G('9',"01110100011000101111000010000101110");
    G(':',"00000001000010000000001000010000000"); G('.',"00000000000000000000000000011000110");
    G('%',"11001110100010001000101100101100011"); G('-',"00000000000000011111000000000000000");
    G('/',"00001000100001000100010001000010000");
#undef G
    default: return "00000000000000000000000000000000000";
    }
}
static uint16_t status_line[W * 30];
static void status_text(esp_lcd_panel_handle_t panel, int row, const char *text, uint16_t color) {
    for (int i = 0; i < W * 30; ++i) status_line[i] = 0x0841;
    int x = 12;
    for (const char *c = text; *c && x + 15 < W; ++c, x += 18) {
        const char *g = glyph(*c);
        for (int y = 0; y < 7; ++y) for (int col = 0; col < 5; ++col) if (g[y * 5 + col] == '1')
            for (int dy = 0; dy < 3; ++dy) for (int dx = 0; dx < 3; ++dx)
                status_line[(4 + y * 3 + dy) * W + x + col * 3 + dx] = color;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, row * 30, W, (row + 1) * 30, status_line));
}
static void overlay(esp_lcd_panel_handle_t panel, int clock_mhz, bool bounce, double fps,
                    double cpu0, double cpu1, bool saving, bool flash_available, bool flash_phase) {
    char line[100];
    snprintf(line, sizeof(line), "PIXEL CLOCK: %d MHZ  %s", clock_mhz, bounce ? "BOUNCE" : "DIRECT");
    status_text(panel, 0, line, 0xffff);
    snprintf(line, sizeof(line), "REFRESH: %.1f HZ  CPU EST: %.0f/%.0f%%", fps, cpu0, cpu1);
    status_text(panel, 1, line, 0xffff);
    snprintf(line, sizeof(line), "FREE PSRAM: %lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    status_text(panel, 2, line, 0xffff);
    status_text(panel, 3, saving ? "SAVING..." : (flash_available ? (flash_phase ? "FLASH READY" : "SAVES OFF") : "FLASH DISABLED"), saving ? 0xffe0 : 0x07e0);
}
static double load_estimate(uint32_t iterations, uint32_t baseline, double seconds) {
    double load = 100.0 - 100.0 * iterations * 5.0 / (seconds * baseline);
    return load < 0 ? 0 : (load > 100 ? 100 : load);
}
static int flash_write(int fd, unsigned seq) {
    static uint8_t data[4096]; for (size_t i=0;i<sizeof(data);i++) data[i]=(uint8_t)(i+seq);
    if (lseek(fd,0,SEEK_SET)<0) return -1;
    for (int i=0;i<4;i++) if(write(fd,data,sizeof(data)) != sizeof(data)) return -1;
    return fsync(fd);
}
void app_main(void) {
    gpio_set_direction(2,GPIO_MODE_OUTPUT); gpio_set_level(2,0);
    xTaskCreatePinnedToCore(worker,"headroom0",2048,(void*)0,1,NULL,0);
    xTaskCreatePinnedToCore(worker,"headroom1",2048,(void*)1,1,NULL,1);
    vTaskDelay(pdMS_TO_TICKS(1000)); uint32_t a=work[0],b=work[1]; vTaskDelay(pdMS_TO_TICKS(5000));
    uint32_t base0=work[0]-a,base1=work[1]-b;
    printf("BASELINE 5s core0=%lu core1=%lu; headroom is low-priority loop throughput, not LVGL performance\n",(unsigned long)base0,(unsigned long)base1);
    esp_vfs_fat_mount_config_t m=VFS_FAT_MOUNT_DEFAULT_CONFIG(); m.format_if_mount_failed=false; m.max_files=4; wl_handle_t wl;
    int fd=-1; esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl("/stress", "documents", &m, &wl);
    if(err==ESP_OK) { char path[32]; snprintf(path,sizeof(path),"/stress/S%07lX.TMP",(unsigned long)(esp_random() & 0xfffffff)); fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600); printf("FLASH scratch=%s\n",path); if(fd<0) printf("FLASH disabled: scratch create failed errno=%d (existing file is preserved)\n",errno); }
    else printf("FLASH disabled: documents mount failed %s; no format attempted\n",esp_err_to_name(err));
    puts("Observe stripes/checkerboard for shifts, flicker or corruption. VSYNC is not an underrun detector. Runs indefinitely; serial output is CSV-like.");
    const int clocks[]={16,20,22,25,27,30}; unsigned seq=0;
    for (;;) {
        const int mode = 1;
        esp_lcd_panel_handle_t p=make_panel(mode); gpio_set_level(2,1);
        for(int c=0;c<6;c++) { ESP_ERROR_CHECK(esp_lcd_rgb_panel_set_pclk(p,clocks[c]*1000000)); vTaskDelay(pdMS_TO_TICKS(500));
            for(int flash=0;flash<2;flash++) { if(flash && fd<0) continue;
                printf("START mode=%s requested_mhz=%d flash=%d duration_s=%d\n",mode?"bounce":"direct",clocks[c],flash,PHASE_SEC); fflush(stdout); uint32_t v=__atomic_load_n(&vsyncs,__ATOMIC_RELAXED),w0=work[0],w1=work[1]; int errors=0,writes=0; int64_t start=esp_timer_get_time();
                uint32_t last_v = v, last_w0 = w0, last_w1 = w1;
                int64_t last_sample = start, next_write = start + 5000000;
                double fps = 0, cpu0 = 0, cpu1 = 0;
                overlay(p, clocks[c], mode, fps, cpu0, cpu1, false, fd >= 0, flash);
                while (esp_timer_get_time() - start < (int64_t)PHASE_SEC * 1000000) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    int64_t now = esp_timer_get_time();
                    uint32_t current_v = __atomic_load_n(&vsyncs, __ATOMIC_RELAXED);
                    uint32_t current_w0 = __atomic_load_n(&work[0], __ATOMIC_RELAXED);
                    uint32_t current_w1 = __atomic_load_n(&work[1], __ATOMIC_RELAXED);
                    double interval = (now - last_sample) / 1000000.0;
                    fps = (current_v - last_v) / interval;
                    cpu0 = load_estimate(current_w0 - last_w0, base0, interval);
                    cpu1 = load_estimate(current_w1 - last_w1, base1, interval);
                    last_v = current_v; last_w0 = current_w0; last_w1 = current_w1; last_sample = now;
                    bool saving = flash && now >= next_write;
                    overlay(p, clocks[c], mode, fps, cpu0, cpu1, saving, fd >= 0, flash);
                    if (saving) {
                        // Give the panel time to display the indicator before flash I/O begins.
                        vTaskDelay(pdMS_TO_TICKS(150));
                        writes++; if (flash_write(fd, seq++)) errors++;
                        vTaskDelay(pdMS_TO_TICKS(300));
                        overlay(p, clocks[c], mode, fps, cpu0, cpu1, false, fd >= 0, flash);
                        next_write += 5000000;
                    }
                }
                double secs=(esp_timer_get_time()-start)/1000000.0; uint32_t frames=__atomic_load_n(&vsyncs,__ATOMIC_RELAXED)-v;
                printf("RESULT mode=%s requested_mhz=%d flash=%d seconds=%.3f vsync=%lu fps=%.3f effective_pclk_mhz=%.3f core0_headroom_pct=%.1f core1_headroom_pct=%.1f writes=%d errors=%d free_psram_kb=%lu underruns=unknown\n",mode?"bounce":"direct",clocks[c],flash,secs,(unsigned long)frames,frames/secs,frames/secs*1056*525/1000000.0,100.0*(work[0]-w0)*5/(secs*base0),100.0*(work[1]-w1)*5/(secs*base1),writes,errors,(unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024)); fflush(stdout);
            }
        }
        gpio_set_level(2,0); ESP_ERROR_CHECK(esp_lcd_panel_del(p));
    }
}







