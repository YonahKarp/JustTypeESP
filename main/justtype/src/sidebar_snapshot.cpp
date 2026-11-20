#include "sidebar_snapshot.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "src/core/lv_refr_private.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/display/lv_display_private.h"
#include "src/draw/lv_draw_private.h"

namespace {
// Clipped equivalent of LVGL 9.2's snapshot draw path. The public snapshot API
// sizes its buffer to the whole object; this captures the menu's exact bounds.
void capture_region(lv_obj_t* object, const lv_area_t& area, lv_draw_buf_t* buffer) {
    lv_draw_buf_clear(buffer, nullptr);
    lv_layer_t layer{};
    layer.draw_buf = buffer;
    layer.buf_area = area;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer._clip_area = area;
    layer.phy_clip_area = area;
    auto* display = lv_obj_get_display(object);
    auto* previous_display = lv_refr_get_disp_refreshing();
    auto* previous_layer = display->layer_head;
    display->layer_head = &layer;
    lv_refr_set_disp_refreshing(display);
    lv_obj_redraw(&layer, object);
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }
    display->layer_head = previous_layer;
    lv_refr_set_disp_refreshing(previous_display);
}
}

bool SidebarSnapshot::prepare(lv_obj_t* sidebar, int32_t start_x) {
    reset();
    lv_obj_t* root = lv_obj_get_parent(sidebar);
    lv_obj_update_layout(root);
    const int32_t width = lv_obj_get_width(sidebar);
    const int32_t height = lv_obj_get_height(sidebar);
    if (width <= 0 || height <= 0) return false;
    const size_t bytes = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565) * height;
    // Never borrow scarce internal/DMA memory or exhaust PSRAM for an effect.
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 512 * 1024) return false;
    menu_ = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!menu_ || !esp_ptr_external_ram(menu_->data)) {
        reset();
        return false;
    }

    const bool hidden = lv_obj_has_flag(sidebar, LV_OBJ_FLAG_HIDDEN);
    const int32_t old_x = lv_obj_get_style_x(sidebar, LV_PART_MAIN);
    lv_obj_set_x(sidebar, 0);
    lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(root);
    lv_area_t menu_area;
    lv_obj_get_coords(sidebar, &menu_area);
    capture_region(sidebar, menu_area, menu_);
    lv_obj_set_x(sidebar, old_x);
    if (hidden) lv_obj_add_flag(sidebar, LV_OBJ_FLAG_HIDDEN);

    layer_ = lv_obj_create(root);
    lv_obj_add_event_cb(layer_, [](lv_event_t* event) {
        auto* snapshot = static_cast<SidebarSnapshot*>(lv_event_get_user_data(event));
        snapshot->presented_ = true;
        snapshot->presented_x_ = lv_obj_get_x(snapshot->menu_image_);
    }, LV_EVENT_DRAW_POST_END, this);
    lv_obj_remove_style_all(layer_);
    lv_obj_set_size(layer_, width, height);
    lv_obj_set_pos(layer_, 0, 0);
    // This is only a clipping parent. Exposed pixels must come from the live
    // editor, including its document/scroll animation, never a cached backdrop.
    lv_obj_set_style_bg_opa(layer_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_CLICKABLE);
    menu_image_ = lv_image_create(layer_);
    lv_image_set_src(menu_image_, menu_);
    lv_obj_set_pos(menu_image_, start_x, 0);
    lv_obj_remove_flag(menu_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI("sidebar_snapshot", "Menu-only PSRAM backing: %u bytes", unsigned(bytes));
    return true;
}

void SidebarSnapshot::move(int32_t x) {
    if (menu_image_) lv_obj_set_x(menu_image_, x);
}

void SidebarSnapshot::reset() {
    const bool allocated = menu_ != nullptr;
    if (layer_) lv_obj_delete(layer_);
    layer_ = menu_image_ = nullptr;
    presented_ = false;
    presented_x_ = 0;
    if (menu_) lv_draw_buf_destroy(menu_);
    menu_ = nullptr;
    if (allocated) ESP_LOGI("sidebar_snapshot", "Backing released; free PSRAM %u bytes",
                           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
