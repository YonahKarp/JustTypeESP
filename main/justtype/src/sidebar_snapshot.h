#pragma once
#include "lvgl.h"

// Temporary RGB565 backing for the sidebar only; the editor stays live.
// All operations, including destruction, belong to the UI task.
class SidebarSnapshot {
public:
    bool prepare(lv_obj_t* sidebar, int32_t start_x);
    void move(int32_t x);
    void reset();
    bool active() const { return layer_ != nullptr; }
    bool presented() const { return presented_; }
    bool presented_at(int32_t x) const { return presented_ && presented_x_ == x; }
private:
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* menu_image_ = nullptr;
    lv_draw_buf_t* menu_ = nullptr;
    bool presented_ = false;
    int32_t presented_x_ = 0;
};
