#include "justtype/src/app.h"
#include "ble_keyboard.h"
#include "keyboard_input.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void justtype_task(void *) {
    App app;
    app.window_width = 800;
    app.window_height = 480;

    if (!init_app(app)) {
        return;
    }

    run_app(app);
    shutdown_app(app);

    vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
    if (!keyboard_input::init()) {
        ESP_LOGE("justtype", "Keyboard event queue allocation failed");
    } else if (!start_ble_keyboard()) {
        ESP_LOGW("justtype", "BLE keyboard unavailable; continuing without input");
    }
    constexpr uint32_t kTaskStackBytes = 32768;
    // RGB interrupts are allocated on the initializing core. Keep initialization
    // and LVGL rendering on core 1 so they do not contend from separate cores.
    xTaskCreatePinnedToCore(
        justtype_task,
        "justtype",
        kTaskStackBytes,
        nullptr,
        5,
        nullptr,
        1);
}
