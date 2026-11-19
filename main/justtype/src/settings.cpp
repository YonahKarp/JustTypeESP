#include "settings.h"
#include "backup/backup.h"
#include "editor.h"
#include "international_font.h"
#include "keyboard_config.h"
#include "sidebar.h"
#include "src/libs/qrcode/lv_qrcode.h"
#include "textarea_utils.h"
#include "theme.h"
#include <algorithm>
#include <cstdio>

namespace {
lv_obj_t *overlay = nullptr;
lv_obj_t *panel = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *hint_label = nullptr;
lv_obj_t *rows[7] = {};
lv_obj_t *tabs[3] = {};
lv_obj_t *tab_labels[3] = {};
lv_timer_t *refresh_timer = nullptr;
lv_obj_t *wifi_form = nullptr;
lv_obj_t *wifi_hint = nullptr;
lv_obj_t *wifi_status = nullptr;
bool wifi_feedback = false;
lv_obj_t *wifi_fields[2] = {};
int wifi_selection = 0;
lv_obj_t *signin_popup = nullptr;
std::string signin_url, signin_code, dismissed_signin;
void close_signin(bool dismissed = false) {
  if (dismissed)
    dismissed_signin = signin_code;
  if (signin_popup)
    lv_obj_delete(signin_popup);
  signin_popup = nullptr;
  signin_url.clear();
  signin_code.clear();
}
void update_signin(const backup::Status &state, const Theme &theme) {
  if (state.user_code.empty() || state.verification_url.empty()) {
    close_signin();
    dismissed_signin.clear();
    return;
  }
  if (state.user_code == dismissed_signin ||
      (signin_popup && signin_code == state.user_code &&
       signin_url == state.verification_url))
    return;
  close_signin();
  signin_code = state.user_code;
  signin_url = state.verification_url;
  signin_popup = lv_obj_create(overlay);
  lv_obj_set_size(signin_popup, 590, 410);
  lv_obj_center(signin_popup);
  lv_obj_remove_flag(signin_popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(signin_popup, theme.sidebar_bg, 0);
  lv_obj_set_style_text_color(signin_popup, theme.sidebar_btn_text, 0);
  auto *title = lv_label_create(signin_popup);
  lv_label_set_text(title, "Link Google Drive");
  lv_obj_set_style_text_font(title, get_ui_font(24), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  // Fixed black-on-white colors and a quiet zone keep every theme scannable.
  auto *frame = lv_obj_create(signin_popup);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, 284, 284);
  lv_obj_align(frame, LV_ALIGN_TOP_LEFT, 0, 42);
  lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  auto *qr = lv_qrcode_create(frame);
  lv_qrcode_set_size(qr, 204);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_obj_center(qr);
  const bool ready =
      lv_qrcode_update(qr, signin_url.data(),
                       static_cast<uint32_t>(signin_url.size())) ==
      LV_RESULT_OK;
  if (!ready) {
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    auto *error = lv_label_create(frame);
    lv_label_set_text(error, "QR unavailable\nUse the URL shown");
    lv_obj_set_style_text_color(error, lv_color_black(), 0);
    lv_obj_set_style_text_font(error, get_ui_font(16), 0);
    lv_obj_center(error);
  }
  auto *instructions = lv_label_create(signin_popup);
  lv_obj_set_width(instructions, 240);
  lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(instructions, get_ui_font(16), 0);
  lv_label_set_text(
      instructions,
      ready ? "Scan with your phone, then enter the code below."
            : "Open the URL on your phone, then enter the code below.");
  lv_obj_align(instructions, LV_ALIGN_TOP_LEFT, 300, 46);
  auto *caption = lv_label_create(signin_popup);
  lv_obj_set_style_text_font(caption, get_ui_font(16), 0);
  lv_label_set_text(caption, "Enter this code:");
  lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 300, 126);
  auto *code = lv_label_create(signin_popup);
  lv_obj_set_width(code, 240);
  lv_label_set_long_mode(code, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(code, get_ui_font(24), 0);
  lv_label_set_text(code, signin_code.c_str());
  lv_obj_align(code, LV_ALIGN_TOP_LEFT, 300, 151);
  auto *url = lv_label_create(signin_popup);
  lv_obj_set_size(url, 240, 100);
  lv_label_set_long_mode(url, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(url, get_ui_font(14), 0);
  lv_label_set_text(url, signin_url.c_str());
  lv_obj_align(url, LV_ALIGN_TOP_LEFT, 300, 225);
  auto *hint = lv_label_create(signin_popup);
  lv_obj_set_style_text_font(hint, get_ui_font(14), 0);
  lv_label_set_text(hint,
                    "Esc or ` closes\nSign-in continues in the background");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  auto *button = lv_btn_create(signin_popup);
  lv_obj_set_size(button, 110, 34);
  lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_add_event_cb(
      button, [](lv_event_t *) { close_signin(true); }, LV_EVENT_CLICKED,
      nullptr);
  auto *label = lv_label_create(button);
  lv_obj_set_style_text_font(label, get_ui_font(16), 0);
  lv_label_set_text(label, "Close");
  lv_obj_center(label);
}
void refresh();
void wifi_focus() {
  for (int i = 0; i < 2; ++i) {
    if (i == wifi_selection)
      lv_obj_add_state(wifi_fields[i], LV_STATE_FOCUSED);
    else
      lv_obj_remove_state(wifi_fields[i], LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(wifi_fields[i], i == wifi_selection ? 2 : 1,
                                  0);
  }
}
void close_wifi() {
  if (wifi_form)
    lv_obj_delete(wifi_form);
  wifi_form = nullptr;
  wifi_hint = nullptr;
  wifi_status = nullptr;
  wifi_feedback = false;
  wifi_fields[0] = wifi_fields[1] = nullptr;
}
void open_wifi() {
  if (wifi_form)
    return;
  wifi_selection = 0;
  wifi_form = lv_obj_create(overlay);
  lv_obj_set_size(wifi_form, 520, 350);
  lv_obj_center(wifi_form);
  lv_obj_remove_flag(wifi_form, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = lv_label_create(wifi_form);
  lv_label_set_text(title, "Wi-Fi network (2.4 GHz)");
  lv_obj_set_style_text_font(title, get_ui_font(20), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  for (int i = 0; i < 2; ++i) {
    auto *caption = lv_label_create(wifi_form);
    lv_label_set_text(caption, i == 0
                                   ? "Network name"
                                   : "Password (leave empty for open network)");
    lv_obj_set_style_text_font(caption, get_ui_font(14), 0);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, 38 + i * 80);
    wifi_fields[i] = lv_textarea_create(wifi_form);
    lv_textarea_set_one_line(wifi_fields[i], true);
    lv_obj_set_style_text_font(wifi_fields[i], get_ui_font(16), 0);
    lv_obj_set_size(wifi_fields[i], 480, 42);
    lv_obj_align(wifi_fields[i], LV_ALIGN_TOP_LEFT, 0, 58 + i * 80);
    if (i == 1)
      lv_textarea_set_password_mode(wifi_fields[i], true);
  }
  const auto credentials = backup::wifi_credentials();
  lv_textarea_set_text(wifi_fields[0], credentials.ssid.c_str());
  lv_textarea_set_text(wifi_fields[1], credentials.password.c_str());
  wifi_status = lv_label_create(wifi_form);
  lv_obj_set_size(wifi_status, 480, 70);
  lv_label_set_long_mode(wifi_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(wifi_status, get_ui_font(14), 0);
  lv_obj_align(wifi_status, LV_ALIGN_TOP_LEFT, 0, 194);
  lv_label_set_text(
      wifi_status,
      credentials.password.empty()
          ? "Enter your network credentials, then save and test."
          : "Saved password loaded (masked). Edit it or test the connection.");
  auto *hint = wifi_hint = lv_label_create(wifi_form);
  lv_label_set_text(
      hint, "Enter: next / save & test   Tab: switch field\nEsc or `: close");
  lv_obj_set_style_text_font(hint, get_ui_font(14), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  wifi_focus();
}
void refresh_wifi() {
  if (!wifi_form)
    return;
  const auto state = backup::status();
  if (state.wifi_testing)
    wifi_feedback = true;
  if (wifi_feedback && !state.wifi_message.empty())
    lv_label_set_text(wifi_status, state.wifi_message.c_str());
  for (auto *field : wifi_fields) {
    if (state.wifi_testing)
      lv_obj_add_state(field, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(field, LV_STATE_DISABLED);
  }
}
void wifi_event(const platform::PlatformEvent &e) {
  using T = platform::PlatformEvent::Type;
  using K = platform::KeyCode;
  if (e.type == T::KeyDown && (e.key == K::Escape || e.key == K::Grave)) {
    close_wifi();
    return;
  }
  if (backup::status().wifi_testing)
    return;
  auto *field = wifi_fields[wifi_selection];
  if (e.type == T::TextInput) {
    textarea_add_text_with_limit(field, e.text, wifi_selection == 0 ? 32 : 63);
    return;
  }
  if (e.type != T::KeyDown)
    return;
  if (e.key == K::Escape || e.key == K::Grave) {
    close_wifi();
    return;
  }
  if (e.key == K::Tab || e.key == K::Up || e.key == K::Down) {
    wifi_selection = 1 - wifi_selection;
    wifi_focus();
    return;
  }
  if (e.key == K::Enter) {
    if (wifi_selection == 0) {
      wifi_selection = 1;
      wifi_focus();
    } else if (backup::configure_wifi(lv_textarea_get_text(wifi_fields[0]),
                                      lv_textarea_get_text(wifi_fields[1]))) {
      wifi_feedback = true;
      refresh_wifi();
    } else if (wifi_status) {
      wifi_feedback = false;
      lv_label_set_text(wifi_status, "SSID: 1-32 bytes. Password: 8-63 "
                                     "bytes,\nor empty for an open network.");
    }
    return;
  }
  if (e.key == K::Backspace)
    lv_textarea_delete_char(field);
  else if (e.key == K::Delete)
    lv_textarea_delete_char_forward(field);
  else if (e.key == K::Left)
    lv_textarea_set_cursor_pos(
        field, std::max<int32_t>(0, lv_textarea_get_cursor_pos(field) - 1));
  else if (e.key == K::Right)
    lv_textarea_set_cursor_pos(field, lv_textarea_get_cursor_pos(field) + 1);
  else if (e.key == K::Home)
    lv_textarea_set_cursor_pos(field, 0);
  else if (e.key == K::End)
    lv_textarea_set_cursor_pos(field, LV_TEXTAREA_CURSOR_LAST);
}
int tab = 0;
int action = 0;
bool capturing = false;
const char *notice = nullptr;
lv_obj_t *labels[7] = {};
SidebarState *sidebar_ptr = nullptr;
EditorState *editor_ptr = nullptr;
int selection = 0;
const char *themes[] = {"Light", "Dark", "Ocean", "Forest", "Sunset"};
const int sizes[] = {18, 20, 22, 24, 26, 28, 30, 32, 36, 40};
void refresh() {
  if (!overlay || !editor_ptr)
    return;
  const Theme &t = get_theme(editor_ptr->dark_theme);
  lv_obj_set_style_bg_color(panel, t.sidebar_bg, 0);
  lv_obj_set_style_text_color(title_label, t.sidebar_btn_text, 0);
  lv_obj_set_style_text_color(hint_label, t.text_dim, 0);
  lv_obj_set_style_border_color(panel, t.sidebar_btn_hover, 0);
  for (int i = 0; i < 3; ++i) {
    lv_obj_set_style_bg_color(
        tabs[i], i == tab ? t.sidebar_selected : t.sidebar_btn_normal, 0);
    lv_obj_set_style_text_color(tab_labels[i], t.sidebar_btn_text, 0);
  }
  for (int i = 0; i < 7; ++i) {
    if (i >= (tab == 0 ? 4 : tab == 1 ? 5 : 7))
      lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < 7; ++i) {
    lv_obj_set_style_bg_color(
        rows[i], i == selection ? t.sidebar_selected : t.sidebar_btn_normal, 0);
    lv_obj_set_style_text_color(labels[i], t.sidebar_btn_text, 0);
  }
  lv_label_set_text(
      hint_label,
      notice ? notice
      : capturing
          ? "Press Ctrl + a letter, - or =. Esc or ` closes."
          : "Tab: switch tab   Arrows: select/change   Esc or `: close");
  if (tab == 2) {
    auto state = backup::status();
    update_signin(state, t);
    lv_label_set_text_fmt(labels[0], "Wi-Fi: %s",
                          state.ssid.empty() ? "Configure network"
                                             : state.ssid.c_str());
    lv_label_set_text_fmt(
        labels[1],
        state.user_code.empty() ? "Google Drive: %s" : "Google code: %s",
        state.user_code.empty() ? (state.linked ? "Linked (Enter to relink)"
                                                : "Link with your phone")
                                : state.user_code.c_str());
    lv_label_set_text_fmt(labels[2], "Automatic backup: < %s >",
                          state.automatic ? "On (every 30 min while idle)"
                                          : "Off");
    lv_label_set_text_fmt(labels[3], "Back up now (%s)",
                          keyboard_config::binding_name(9).c_str());
    lv_label_set_text(labels[4], "Verify backup copies (checks all files)");
    lv_label_set_text(labels[5], "Forget Google account on this Leaf");
    lv_label_set_text(labels[6], "Close");
    const std::string status = state.user_code.empty()
                                   ? state.message
                                   : state.verification_url + "\nEnter " +
                                         state.user_code + " on your phone";
    lv_label_set_text(hint_label, status.c_str());
    return;
  }
  close_signin();
  if (tab == 1) {
    lv_label_set_text_fmt(
        labels[0], "Layout:  < %s >",
        keyboard_config::layout_name(keyboard_config::layout()));
    lv_label_set_text_fmt(labels[1], "Shortcut:  < %s >",
                          keyboard_config::action_name(action));
    lv_label_set_text_fmt(labels[2], "%s:  %s",
                          capturing ? "Waiting" : "Enter to remap",
                          keyboard_config::binding_name(action).c_str());
    lv_label_set_text(labels[3], "Reset keyboard defaults");
    lv_label_set_text(labels[4], "Close");
    return;
  }
  char text[96];
  snprintf(text, sizeof(text), "Theme                  <  %s  >",
           themes[editor_ptr->dark_theme]);
  lv_label_set_text(labels[0], text);
  snprintf(text, sizeof(text), "Font size                <  %d  >",
           sizes[editor_ptr->font_size_index]);
  lv_label_set_text(labels[1], text);
  lv_label_set_text(labels[2], "Font                         Montserrat");
  lv_label_set_text(labels[3], "Close");
}
void close() {
  if (!overlay)
    return;
  if (refresh_timer) {
    lv_timer_delete(refresh_timer);
    refresh_timer = nullptr;
  }
  close_wifi();
  close_signin();
  lv_obj_delete(overlay);
  overlay = panel = title_label = hint_label = nullptr;
  sidebar_ptr = nullptr;
  editor_ptr = nullptr;
}
void change(int delta) {
  if (!editor_ptr || !sidebar_ptr)
    return;
  if (tab == 2) {
    if (selection == 0)
      open_wifi();
    else if (selection == 1)
      backup::request_link();
    else if (selection == 2)
      backup::set_automatic(!backup::status().automatic);
    else if (selection == 3)
      backup::request_now();
    else if (selection == 4)
      backup::request_verify();
    else if (selection == 5)
      backup::forget_google();
    refresh();
    return;
  }
  if (tab == 1) {
    notice = nullptr;
    if (selection == 0)
      keyboard_config::set_layout(keyboard_config::layout() + delta);
    else if (selection == 1) {
      action = (action + delta + keyboard_config::action_count) %
               keyboard_config::action_count;
      refresh();
      return;
    } else
      return;
    if (!keyboard_config::save())
      notice = "Could not save keyboard settings. Try again.";
    refresh();
    return;
  }
  if (selection == 0) {
    editor_ptr->dark_theme = (editor_ptr->dark_theme + delta + 5) % 5;
    update_editor_theme(*editor_ptr, editor_ptr->dark_theme);
    update_sidebar_theme(*sidebar_ptr, editor_ptr->dark_theme);
    refresh_file_list_ui(*sidebar_ptr, *editor_ptr);
  } else if (selection == 1) {
    editor_ptr->font_size_index =
        std::max(0, std::min(9, editor_ptr->font_size_index + delta));
    update_editor_font(*editor_ptr);
  } else
    return;
  editor_ptr->state_pending_save = true;
  editor_ptr->state_change_time = lv_tick_get();
  refresh();
}
} // namespace
bool settings_active() { return overlay != nullptr; }
void settings_open(SidebarState &sidebar, EditorState &editor) {
  if (overlay)
    return;
  sidebar_ptr = &sidebar;
  editor_ptr = &editor;
  selection = 0;
  tab = 0;
  capturing = false;
  notice = nullptr;
  dismissed_signin.clear();
  overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  panel = lv_obj_create(overlay);
  lv_obj_set_size(panel, 590, 410);
  lv_obj_center(panel);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_t *title = title_label = lv_label_create(panel);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_font(title, get_ui_font(24), 0);
  lv_obj_set_style_text_color(title,
                              get_theme(editor.dark_theme).sidebar_btn_text, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 2);
  for (int i = 0; i < 3; ++i) {
    tabs[i] = lv_btn_create(panel);
    lv_obj_set_size(tabs[i], 115, 34);
    lv_obj_align(tabs[i], LV_ALIGN_TOP_RIGHT, -242 + i * 120, 0);
    lv_obj_add_event_cb(
        tabs[i],
        [](lv_event_t *e) {
          tab = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
          selection = 0;
          capturing = false;
          notice = nullptr;
          refresh();
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    tab_labels[i] = lv_label_create(tabs[i]);
    lv_label_set_text(tab_labels[i], i == 0   ? "Appearance"
                                     : i == 1 ? "Keyboard"
                                              : "Backup");
    lv_obj_set_style_text_font(tab_labels[i], get_ui_font(16), 0);
    lv_obj_center(tab_labels[i]);
  }
  for (int i = 0; i < 7; ++i) {
    rows[i] = lv_obj_create(panel);
    lv_obj_remove_style_all(rows[i]);
    lv_obj_set_size(rows[i], 540, 34);
    lv_obj_align(rows[i], LV_ALIGN_TOP_MID, 0, 48 + i * 39);
    lv_obj_set_style_bg_opa(rows[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rows[i], 5, 0);
    lv_obj_add_flag(rows[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        rows[i],
        [](lv_event_t *e) {
          int row = static_cast<int>(
              reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
          selection = row;
          notice = nullptr;
          if (row == (tab == 0 ? 3 : tab == 1 ? 4 : 6))
            close();
          else if (tab == 2)
            change(1);
          else if (tab == 1 && row == 2) {
            capturing = true;
            refresh();
          } else if (tab == 1 && row == 3) {
            keyboard_config::reset();
            if (!keyboard_config::save())
              notice = "Could not save keyboard settings.";
            refresh();
          } else if (row < 2)
            change(1);
          else
            refresh();
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    labels[i] = lv_label_create(rows[i]);
    lv_obj_set_style_text_font(labels[i], get_ui_font(16), 0);
    lv_obj_align(labels[i], LV_ALIGN_LEFT_MID, 12, 0);
  }
  lv_obj_t *hint = hint_label = lv_label_create(panel);
  lv_label_set_text(hint,
                    "Up/Down select   Left/Right change   Esc or ~ close");
  lv_obj_set_style_text_font(hint, get_ui_font(14), 0);
  lv_obj_set_style_text_color(hint, get_theme(editor.dark_theme).text_dim, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_width(hint, 540);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  refresh_timer = lv_timer_create(
      [](lv_timer_t *) {
        if (overlay) {
          if (wifi_form)
            refresh_wifi();
          else
            refresh();
        }
      },
      500, nullptr);
  refresh();
}
void settings_handle_event(const platform::PlatformEvent &e, SidebarState &,
                           EditorState &) {
  if (!overlay)
    return;
  if (wifi_form) {
    wifi_event(e);
    return;
  }
  if (e.type != platform::PlatformEvent::Type::KeyDown)
    return;
  if (!capturing && e.modifiers.ctrl && e.key == platform::KeyCode::B) {
    backup::request_now();
    return;
  }
  using K = platform::KeyCode;
  if (signin_popup) {
    if (e.key == K::Escape || e.key == K::Grave || e.key == K::Enter)
      close_signin(true);
    return;
  }
  if (e.key == K::Escape || e.key == K::Grave) {
    close();
    return;
  }
  if (capturing) {
    if (e.hid_usage) {
      if (e.modifiers.ctrl && !e.modifiers.alt && !e.modifiers.meta &&
          !e.modifiers.shift && keyboard_config::bind(action, e.hid_usage)) {
        capturing = false;
        notice = keyboard_config::save() ? "Shortcut saved."
                                         : "Could not save keyboard settings.";
      } else
        notice = "Use an unused Ctrl + letter, - or =. Esc or ` closes.";
    }
    refresh();
    return;
  }
  notice = nullptr;
  const int count = tab == 0 ? 4 : tab == 1 ? 5 : 7;
  if (e.key == K::Tab) {
    tab = (tab + 1) % 3;
    selection = 0;
  } else if (e.key == K::Up)
    selection = (selection + count - 1) % count;
  else if (e.key == K::Down)
    selection = (selection + 1) % count;
  else if (e.key == K::Left)
    change(-1);
  else if (e.key == K::Right)
    change(1);
  else if (e.key == K::Enter) {
    if (selection == count - 1) {
      close();
      return;
    }
    if (tab == 2)
      change(1);
    else if (tab == 1 && selection == 2)
      capturing = true;
    else if (tab == 1 && selection == 3) {
      keyboard_config::reset();
      if (!keyboard_config::save())
        notice = "Could not save keyboard settings.";
    } else
      change(1);
  }
  refresh();
}
