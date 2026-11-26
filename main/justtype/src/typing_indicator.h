#pragma once
#include "lvgl.h"

// Fixed-width cells keep the drawn dots and cursor aligned without font glyphs.
class TypingIndicator {
 public:
  void create(lv_obj_t *parent) {
    color_set_=false;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 82, 50);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    for (int row=0; row<2; ++row) for (int col=0; col<3; ++col) {
      auto *dot=lv_obj_create(root_);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot,6,6);
      lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
      lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
      lv_obj_set_pos(dot, col*18+3, row*24+12);
      dots_[row][col]=dot;
    }
    cursor_=lv_obj_create(root_);
    lv_obj_remove_style_all(cursor_);
    lv_obj_set_size(cursor_,3,20);
    lv_obj_set_style_bg_opa(cursor_,LV_OPA_COVER,0);
    timer_=lv_timer_create([](lv_timer_t *timer) {
      auto *self=static_cast<TypingIndicator *>(lv_timer_get_user_data(timer));
      self->frame_=(self->frame_+1)%6;
      self->draw();
    },250,this);
    lv_timer_pause(timer_);
    lv_obj_add_event_cb(root_,[](lv_event_t *event) {
      auto *self=static_cast<TypingIndicator *>(lv_event_get_user_data(event));
      if(self->timer_) lv_timer_delete(self->timer_);
      self->timer_=nullptr;self->root_=nullptr;self->running_=false;
    },LV_EVENT_DELETE,this);
    lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN);
  }
  void set_color(lv_color_t color) {
    if(!root_) return;
    if(color_set_ && lv_color_to_u32(color_)==lv_color_to_u32(color)) return;
    color_=color;color_set_=true;
    for(auto &row:dots_) for(auto *dot:row) lv_obj_set_style_bg_color(dot,color,0);
    lv_obj_set_style_bg_color(cursor_,color,0);
  }
  void start() {
    if(!root_ || running_) return;
    frame_=0;running_=true;draw();
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(timer_);lv_timer_resume(timer_);
  }
  void stop() {
    if(!root_ || !running_) return;
    running_=false;lv_timer_pause(timer_);lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t *object() const { return root_; }
 private:
  void draw() {
    static constexpr lv_opa_t opacity[] = {LV_OPA_COVER, LV_OPA_70, LV_OPA_40, LV_OPA_20, LV_OPA_TRANSP, LV_OPA_COVER};
    for(int i=0;i<3;++i) {
      lv_obj_set_style_bg_opa(dots_[0][i],opacity[frame_],0);
      lv_obj_set_style_bg_opa(dots_[1][i],frame_ < 5 && i < frame_ ? LV_OPA_COVER : LV_OPA_TRANSP,0);
    }
    lv_obj_set_pos(cursor_,(frame_==5 ? 3 : (frame_<3 ? frame_ : 3))*18,frame_==5 ? 1 : 25);
    lv_obj_invalidate(root_);
  }
  lv_obj_t *root_=nullptr, *dots_[2][3]={}, *cursor_=nullptr;
  lv_timer_t *timer_=nullptr;
  lv_color_t color_{};
  bool running_=false, color_set_=false;
  int frame_=0;
};
