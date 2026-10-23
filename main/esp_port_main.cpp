#include "justtype/src/app.h"
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
    constexpr uint32_t kTaskStackBytes = 32768;
    xTaskCreatePinnedToCore(
        justtype_task,
        "justtype",
        kTaskStackBytes,
        nullptr,
        5,
        nullptr,
        tskNO_AFFINITY);
}
