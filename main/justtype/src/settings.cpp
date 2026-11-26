#include "settings.h"
#include "editor_font_sizes.h"
#include "backlight.h"
#include "backup/backup.h"
#include "content.h"
#include "editor.h"
#include "international_font.h"
#include "keyboard_config.h"
#include "sidebar.h"
#include "sidebar_dialog.h"
#include "src/libs/qrcode/lv_qrcode.h"
#include "textarea_utils.h"
#include "typing_indicator.h"
#include "theme.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace {
lv_obj_t *overlay = nullptr;
lv_obj_t *panel = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *hint_label = nullptr;
TypingIndicator content_loader;
lv_obj_t *content_scrim = nullptr, *content_toast = nullptr;
lv_obj_t *dialog_title = nullptr, *dialog_check = nullptr, *dialog_warning = nullptr;
bool dialog_result = false;
bool dialog_result_success = false;
bool dialog_loading = false;
uint32_t dialog_result_start = 0;
unsigned seen_content_result = 0;
bool link_requested = false;
unsigned link_start_generation = 0;
lv_obj_t *rows[7] = {};
lv_obj_t *tabs[4] = {};
lv_obj_t *tab_labels[4] = {};
lv_timer_t *refresh_timer = nullptr;
lv_obj_t *wifi_form = nullptr;
lv_obj_t *wifi_hint = nullptr;
lv_obj_t *wifi_status = nullptr;
bool wifi_feedback = false;
bool wifi_success = false;
lv_obj_t *wifi_fields[2] = {};
EditorState *editor_ptr = nullptr;
lv_obj_t *wifi_network_dropdown = nullptr, *wifi_eye = nullptr, *wifi_eye_label = nullptr;
std::vector<std::string> wifi_networks;
bool wifi_list_open = false, wifi_show_password = false, wifi_scan_seen = false;
int wifi_selection = 0;
void show_loading_dialog(const char *title, const Theme &theme) {
  dialog_result=false;
  if(dialog_loading) {
    if(std::string(lv_label_get_text(dialog_title)) != title)lv_label_set_text(dialog_title,title);
    return;
  }
  dialog_loading=true;
  lv_label_set_text(dialog_title,title);
  lv_obj_set_style_bg_color(content_toast,theme.sidebar_bg,0);
  lv_obj_set_style_border_color(content_toast,theme.sidebar_btn_hover,0);
  lv_obj_set_style_text_color(dialog_title,theme.sidebar_btn_text,0);
  lv_obj_add_flag(dialog_check,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dialog_warning,LV_OBJ_FLAG_HIDDEN);
  content_loader.set_color(theme.sidebar_btn_text);
  lv_obj_remove_flag(content_scrim,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(content_scrim);
  content_loader.start();
}
void show_result_dialog(bool success, const char *message, const Theme &theme) {
  content_loader.stop();
  dialog_loading=false;
  dialog_result_success=success;
  dialog_result=true;dialog_result_start=lv_tick_get();
  lv_obj_set_style_bg_color(content_toast,theme.sidebar_bg,0);
  lv_obj_set_style_border_color(content_toast,theme.sidebar_btn_hover,0);
  lv_obj_set_style_text_color(dialog_title,theme.sidebar_btn_text,0);
  lv_label_set_text(dialog_title,message && *message ? message : (success ? "Success" : "Something went wrong"));
  lv_obj_add_flag(success ? dialog_warning : dialog_check,LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(success ? dialog_check : dialog_warning,LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(content_scrim,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(content_scrim);
}
void hide_result_dialog() {
  dialog_result=false;
  dialog_loading=false;
  content_loader.stop();
  if(content_scrim)lv_obj_add_flag(content_scrim,LV_OBJ_FLAG_HIDDEN);
}
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
    auto *control = i == 0 ? wifi_network_dropdown : wifi_fields[i];
    if (i == wifi_selection)
      lv_obj_add_state(control, LV_STATE_FOCUSED);
    else
      lv_obj_remove_state(control, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(control, i == wifi_selection ? 2 : 1,
                                  0);
  }
  lv_obj_set_style_border_width(wifi_eye, wifi_selection == 2 ? 2 : 0, 0);
}
void close_wifi() {
  hide_result_dialog();
  if (wifi_form)
    lv_obj_delete(wifi_form);
  wifi_form = nullptr;
  wifi_hint = nullptr;
  wifi_status = nullptr;
  wifi_feedback = false;
  wifi_success = false;
  wifi_fields[0] = wifi_fields[1] = nullptr;
  wifi_network_dropdown = wifi_eye = wifi_eye_label = nullptr;
}
void open_wifi() {
  if (wifi_form)
    return;
  wifi_selection = 0;
  wifi_list_open=false; wifi_show_password=false; wifi_scan_seen=false;
  wifi_form = lv_obj_create(overlay);
  lv_obj_set_size(wifi_form, 590, 410);
  lv_obj_center(wifi_form);
  lv_obj_remove_flag(wifi_form, LV_OBJ_FLAG_SCROLLABLE);
  const auto &theme = get_theme(editor_ptr->dark_theme);
  lv_obj_set_style_bg_color(wifi_form, theme.sidebar_bg, 0);
  lv_obj_set_style_bg_opa(wifi_form, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(wifi_form, theme.sidebar_btn_hover, 0);
  lv_obj_set_style_border_width(wifi_form, 1, 0);
  lv_obj_set_style_radius(wifi_form, 10, 0);
  lv_obj_set_style_text_color(wifi_form, theme.sidebar_btn_text, 0);
  lv_obj_t *title = lv_label_create(wifi_form);
  lv_label_set_text(title, "Wi-Fi network (2.4 GHz)");
  lv_obj_set_style_text_font(title, get_ui_font(20), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  for (int i = 0; i < 2; ++i) {
    auto *caption = lv_label_create(wifi_form);
    lv_label_set_text(caption, i == 0
                                   ? "Network"
                                   : "Password (leave empty for open network)");
    lv_obj_set_style_text_font(caption, get_ui_font(14), 0);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, 38 + i * 80);
    wifi_fields[i] = lv_textarea_create(wifi_form);
    lv_textarea_set_one_line(wifi_fields[i], true);
    lv_obj_set_style_text_font(wifi_fields[i], get_ui_font(16), 0);
    lv_obj_set_size(wifi_fields[i], i == 1 ? 490 : 540, 42);
    lv_obj_set_style_bg_color(wifi_fields[i], theme.search_input_bg, 0);
    lv_obj_set_style_text_color(wifi_fields[i], theme.search_input_text, 0);
    lv_obj_set_style_border_color(wifi_fields[i], theme.sidebar_selected, 0);
    lv_obj_align(wifi_fields[i], LV_ALIGN_TOP_LEFT, 0, 58 + i * 80);
    if (i == 1)
      lv_textarea_set_password_mode(wifi_fields[i], true);
  }
  const auto credentials = backup::wifi_credentials();
  lv_textarea_set_text(wifi_fields[0], credentials.ssid.c_str());
  lv_textarea_set_text(wifi_fields[1], credentials.password.c_str());
  lv_obj_add_flag(wifi_fields[0], LV_OBJ_FLAG_HIDDEN);
  wifi_networks.clear();
  if (!credentials.ssid.empty()) wifi_networks.push_back(credentials.ssid);
  wifi_network_dropdown=lv_dropdown_create(wifi_form);
  lv_obj_set_size(wifi_network_dropdown,540,42);
  lv_obj_align(wifi_network_dropdown,LV_ALIGN_TOP_LEFT,0,58);
  lv_obj_set_style_text_font(wifi_network_dropdown,get_ui_font(16),0);
  lv_obj_set_style_bg_color(wifi_network_dropdown,theme.search_input_bg,0);
  lv_obj_set_style_text_color(wifi_network_dropdown,theme.search_input_text,0);
  lv_obj_set_style_border_color(wifi_network_dropdown,theme.sidebar_selected,0);
  auto *network_list=lv_dropdown_get_list(wifi_network_dropdown);
  lv_obj_set_style_bg_color(network_list,theme.search_input_bg,LV_PART_MAIN);
  lv_obj_set_style_text_color(network_list,theme.search_input_text,LV_PART_MAIN);
  lv_obj_set_style_bg_color(network_list,theme.sidebar_selected,LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_dropdown_set_options(wifi_network_dropdown,credentials.ssid.empty()?"Scanning...":credentials.ssid.c_str());
  lv_obj_add_event_cb(wifi_network_dropdown,[](lv_event_t*){
    auto n=lv_dropdown_get_selected(wifi_network_dropdown);
    if(n<wifi_networks.size())lv_textarea_set_text(wifi_fields[0],wifi_networks[n].c_str());
  },LV_EVENT_VALUE_CHANGED,nullptr);
  wifi_eye=lv_btn_create(wifi_form);
  lv_obj_set_size(wifi_eye,42,42);lv_obj_align(wifi_eye,LV_ALIGN_TOP_LEFT,498,138);
  lv_obj_set_style_bg_color(wifi_eye,theme.sidebar_btn_normal,0);
  lv_obj_set_style_text_color(wifi_eye,theme.sidebar_btn_text,0);
  lv_obj_set_style_border_color(wifi_eye,theme.sidebar_selected,0);
  wifi_eye_label=lv_label_create(wifi_eye);lv_label_set_text(wifi_eye_label,LV_SYMBOL_EYE_OPEN);lv_obj_center(wifi_eye_label);
  lv_obj_add_event_cb(wifi_eye,[](lv_event_t*){wifi_show_password=!wifi_show_password;lv_textarea_set_password_mode(wifi_fields[1],!wifi_show_password);lv_label_set_text(wifi_eye_label,wifi_show_password?LV_SYMBOL_EYE_CLOSE:LV_SYMBOL_EYE_OPEN);},LV_EVENT_CLICKED,nullptr);
  wifi_status = lv_label_create(wifi_form);
  lv_obj_set_size(wifi_status, 540, 70);
  lv_label_set_long_mode(wifi_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(wifi_status, get_ui_font(14), 0);
  lv_obj_set_style_text_color(wifi_status, theme.text_dim, 0);
  lv_obj_align(wifi_status, LV_ALIGN_TOP_LEFT, 0, 194);
  lv_label_set_text(wifi_status, "");
  auto *hint = wifi_hint = lv_label_create(wifi_form);
  lv_label_set_text(
      hint, "Enter: save & test   Esc or `: close");
  lv_obj_set_style_text_font(hint, get_ui_font(14), 0);
  lv_obj_set_style_text_color(hint, theme.text_dim, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  wifi_focus();
  wifi_scan_seen = backup::scan_wifi();
  if (!wifi_scan_seen) lv_label_set_text(wifi_status,"Wi-Fi scan unavailable");
}
void refresh_wifi() {
  if (!wifi_form)
    return;
  if (dialog_result) {
    if (lv_tick_elaps(dialog_result_start) >= (dialog_result_success ? 1200u : 3000u)) {
      hide_result_dialog();
      if (wifi_success) close_wifi();
    }
    return;
  }
  const auto state = backup::status();
  if(wifi_feedback && state.wifi_testing) {
    const char *stage="Connecting to Wi-Fi...";
    if(state.message.rfind("Connecting",0)==0 || state.message.rfind("Synchronizing",0)==0)stage=state.message.c_str();
    show_loading_dialog(stage,get_theme(editor_ptr->dark_theme));
  }
  if (wifi_feedback && !state.wifi_testing) {
    wifi_feedback=false;
    wifi_success=state.wifi_connected;
    show_result_dialog(wifi_success,wifi_success ? "Connection successful!" : state.wifi_message.c_str(),get_theme(editor_ptr->dark_theme));
    return;
  }
  if(state.wifi_scanning) { wifi_scan_seen=true; if(!wifi_feedback)lv_label_set_text(wifi_status,"Scanning for networks..."); }
  else if(wifi_scan_seen) {
    wifi_scan_seen=false;
    const std::string selected=lv_textarea_get_text(wifi_fields[0]);
    wifi_networks=state.wifi_networks;
    if(!selected.empty() && std::find(wifi_networks.begin(),wifi_networks.end(),selected)==wifi_networks.end())wifi_networks.insert(wifi_networks.begin(),selected);
    std::string options;int index=0;
    for(size_t i=0;i<wifi_networks.size();++i){if(i)options+='\n';options+=wifi_networks[i];if(wifi_networks[i]==selected)index=int(i);}
    lv_dropdown_set_options(wifi_network_dropdown,options.empty()?"No networks found":options.c_str());
    lv_dropdown_set_selected(wifi_network_dropdown,index);
    if(!wifi_networks.empty())lv_textarea_set_text(wifi_fields[0],wifi_networks[index].c_str());
    if(!wifi_feedback)lv_label_set_text(wifi_status,state.wifi_scan_message.c_str());
  }
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
    if(wifi_list_open){wifi_list_open=false;lv_dropdown_close(wifi_network_dropdown);return;}
    close_wifi();
    return;
  }
  if (wifi_success || dialog_result || backup::status().wifi_testing) return;
  if (backup::status().wifi_testing)
    return;
  auto *field = wifi_fields[wifi_selection == 1 ? 1 : 0];
  if (e.type == T::TextInput) {
    if(wifi_selection == 1) textarea_add_text_with_limit(field, e.text, 63);
    return;
  }
  if (e.type != T::KeyDown)
    return;
  if (e.key == K::Escape || e.key == K::Grave) {
    close_wifi();
    return;
  }
  if(wifi_list_open && (e.key==K::Up || e.key==K::Down)) {
    int n=int(lv_dropdown_get_selected(wifi_network_dropdown))+(e.key==K::Down?1:-1);
    n=std::max(0,std::min(int(wifi_networks.size())-1,n));lv_dropdown_set_selected(wifi_network_dropdown,n);return;
  }
  if(wifi_selection==1 && e.key==K::Right){wifi_selection=2;wifi_focus();return;}
  if(wifi_selection==2 && e.key==K::Left){wifi_selection=1;wifi_focus();return;}
  if (e.key == K::Tab || e.key == K::Up || e.key == K::Down) {
    wifi_selection = wifi_selection == 0 ? 1 : 0;
    wifi_focus();
    return;
  }
  if (e.key == K::Enter) {
    if (wifi_selection == 2) { lv_obj_send_event(wifi_eye,LV_EVENT_CLICKED,nullptr); }
    else if (wifi_selection == 0) {
      if(!wifi_list_open){wifi_list_open=true;lv_dropdown_open(wifi_network_dropdown);}
      else {wifi_list_open=false;lv_dropdown_close(wifi_network_dropdown);auto n=lv_dropdown_get_selected(wifi_network_dropdown);if(n<wifi_networks.size())lv_textarea_set_text(wifi_fields[0],wifi_networks[n].c_str());wifi_selection=1;wifi_focus();}
    } else if (backup::configure_wifi(lv_textarea_get_text(wifi_fields[0]),
                                      lv_textarea_get_text(wifi_fields[1]))) {
      wifi_feedback = true;
      show_loading_dialog("Connecting to Wi-Fi",get_theme(editor_ptr->dark_theme));
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
lv_obj_t *binding_panel = nullptr,
         *binding_rows[keyboard_config::action_count] = {};
lv_obj_t *binding_labels[keyboard_config::action_count] = {},
         *binding_values[keyboard_config::action_count] = {},
         *binding_hint = nullptr;
lv_obj_t *theme_dropdown = nullptr, *size_dropdown = nullptr,
         *layout_dropdown = nullptr;
lv_obj_t *brightness_slider = nullptr, *brightness_value = nullptr;
lv_obj_t *automatic_switch = nullptr, *automatic_value = nullptr;
lv_obj_t *content_dropdown = nullptr, *font_dropdown = nullptr,
         *dictionary_dropdown = nullptr;
std::vector<std::string> dictionary_ids;
std::string dictionary_options;
std::vector<content::Package> content_items;
std::vector<std::string> font_ids;
std::vector<content::ThemeChoice> theme_items;
std::string theme_options;
std::string content_options, font_options;
int content_selection = 0;
lv_obj_t *open_dropdown = nullptr;
void content_action(int row) {
  const std::string id = content_selection < int(content_items.size())
                             ? content_items[content_selection].id
                             : "";
  if (row == 5)
    content::refresh();
  else if (row == 1 && !id.empty())
    content::install(id);
  else if (row == 2 && !id.empty())
    content::enable(id);
  else if (row == 3 && !id.empty())
    content::disable(id);
  else if (row == 4 && !id.empty())
    content::remove(id);
}

bool capturing = false;
const char *notice = nullptr;
lv_obj_t *labels[7] = {};
SidebarState *sidebar_ptr = nullptr;
int selection = 0;
int content_resume_selection = -1;
struct ContentRow {
  int section;
  std::string id, name, detail;
  bool installed, more, builtin;
  uint64_t bytes;
};
std::vector<ContentRow> content_rows;
std::string previewed_theme;
unsigned previewed_theme_revision = 0;
bool expanded_sections[5] = {};
int content_cursor = 0;
bool storage_focus = false;
int storage_segment = 0;
uint64_t storage_amounts[8] = {};
int pending_section = -1;
int target_content_section = -1;
bool delete_confirm = false;
int delete_selection = 1;
lv_obj_t *delete_action_btn = nullptr, *delete_cancel_btn = nullptr;
std::string delete_id, content_render_key;
lv_obj_t *content_view = nullptr, *content_list = nullptr,
         *content_bar = nullptr, *content_legend = nullptr,
         *delete_popup = nullptr;
uint64_t document_bytes = 0;
uint64_t directory_bytes(const std::string &path, unsigned depth = 0) {
  if (depth > 16)
    return 0;
  DIR *dir = opendir(path.c_str());
  if (!dir)
    return 0;
  uint64_t bytes = 0;
  dirent *entry;
  while ((entry = readdir(dir))) {
    std::string n = entry->d_name;
    if (n == "." || n == ".." || (!n.empty() && n[0] == '.'))
      continue;
    std::string p = path + "/" + n;
    struct stat st{};
    if (stat(p.c_str(), &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode))
      bytes += directory_bytes(p, depth + 1);
    else if (S_ISREG(st.st_mode))
      bytes += st.st_size;
  }
  closedir(dir);
  return bytes;
}
uint64_t package_bytes(const content::Package &p) {
  uint64_t n = 0;
  for (const auto &f : p.files)
    n += f.size;
  return n;
}
std::string format_size(uint64_t n) {
  return n < 1024 ? std::to_string(n) + " B"
         : n < 1024 * 1024
             ? std::to_string((n + 1023) / 1024) + " KB"
             : std::to_string((n * 10 / (1024 * 1024)) / 10) + "." +
                   std::to_string((n * 10 / (1024 * 1024)) % 10) + " MB";
}
void render_content(const content::State &cs, const Theme &t) {
  if (!content_view)
    return;
  const char *types[] = {"theme", "font", "layout", "dictionary", "pack"};
  const char *names[] = {"Themes", "Fonts", "Keyboard layouts", "Dictionaries",
                         "Packs"};
  const uint32_t colors[] = {0x72a897, 0xc9986b, 0x829ac6, 0xb58bc0, 0x789ca9};
  std::string selected = content_cursor < int(content_rows.size())
                             ? content_rows[content_cursor].id
                             : "";
  std::vector<ContentRow> rows_new;
  uint64_t sizes[5] = {};
  for (int sec = 0; sec < 5; ++sec) {
    uint64_t total = 0;
    for (const auto &p : cs.installed)
      if (p.type == types[sec])
        total += package_bytes(p);
    sizes[sec] = total;
    rows_new.push_back({sec, "header" + std::to_string(sec), names[sec],
                        format_size(total), true, false, true, total});
    if (sec == 1)
      rows_new.push_back({sec, "builtin-font", "Montserrat", "Built in", true,
                          false, true, 0});
    if (sec == 2)
      for (int i = 0; i < 1; ++i)
        rows_new.push_back({sec, "builtin-layout" + std::to_string(i),
                            keyboard_config::layout_name(i), "Built in", true,
                            false, true, 0});
    for (const auto &p : cs.installed)
      if (p.type == types[sec])
        rows_new.push_back({sec, p.id, p.name, format_size(package_bytes(p)),
                            true, false, false, package_bytes(p)});
    if (expanded_sections[sec])
      for (const auto &p : cs.available)
        if (p.type == types[sec]) {
          auto old = std::find_if(cs.installed.begin(), cs.installed.end(),
                                  [&](const auto &v) { return v.id == p.id; });
          if (old == cs.installed.end() || old->version != p.version)
            rows_new.push_back({sec, "download:" + p.id, p.name,
                                format_size(package_bytes(p)), false, false,
                                false, package_bytes(p)});
        }
    rows_new.push_back(
        {sec, "more" + std::to_string(sec),
         expanded_sections[sec] ? "Refresh list..." : "Get more...", "", false,
         true, false, 0});
  }
  content_rows = std::move(rows_new);
  const bool jumping_to_section = target_content_section >= 0;
  if (target_content_section >= 0) {
    auto it = std::find_if(content_rows.begin(), content_rows.end(), [&](const auto &r) {
      return r.id == "header" + std::to_string(target_content_section);
    });
    if (it != content_rows.end()) content_cursor = std::min(int(it - content_rows.begin()) + 1, int(content_rows.size()) - 1);
    target_content_section = -1;
  } else if (!selected.empty()) {
    auto it = std::find_if(content_rows.begin(), content_rows.end(),
                           [&](const auto &r) { return r.id == selected; });
    if(it == content_rows.end() && selected.rfind("download:",0)==0) {
      const auto installed_id=selected.substr(9);
      it=std::find_if(content_rows.begin(),content_rows.end(),[&](const auto &r){return r.id==installed_id;});
    }
    if (it != content_rows.end())
      content_cursor = int(it - content_rows.begin());
  }
  content_cursor =
      std::max(0, std::min(content_cursor, int(content_rows.size()) - 1));
  if (content_rows[content_cursor].id.rfind("header",0)==0 && content_cursor+1<int(content_rows.size())) ++content_cursor;
  if (pending_section >= 0 && !cs.busy) {
    if (cs.message == "Content operation complete")
      expanded_sections[pending_section] = true;
    if (target_content_section < 0 && content_cursor < int(content_rows.size()) &&
        content_rows[content_cursor].id == "header" + std::to_string(pending_section))
      target_content_section = pending_section;
    pending_section = -1;
    content_render_key.clear();
    render_content(cs, t);
    return;
  }
  std::string key = std::to_string(content_cursor) + std::to_string(storage_focus) + ":" + std::to_string(storage_segment) + ":" + std::to_string(cs.free_bytes) + ":" + std::to_string(document_bytes) +
                    std::to_string(cs.revision) + std::to_string(cs.busy) +
                    cs.message;
  for (bool b : expanded_sections)
    key += b ? '1' : '0';
  if (key == content_render_key)
    return;
  content_render_key = key;
  lv_obj_clean(content_list);
  lv_obj_clean(content_bar);
  const uint64_t capacity = 0xbf0000,
                 used = capacity > cs.free_bytes ? capacity - cs.free_bytes : 0;
  uint64_t remaining = std::min(used, capacity),
           parts[6] = {document_bytes, sizes[0], sizes[1],
                       sizes[2],       sizes[3], sizes[4]};
  const uint32_t palette[6] = {0x638fa8,  colors[0], colors[1],
                               colors[2], colors[3], colors[4]};
  int x = 0;
  int segment_x[8] = {}, segment_w[8] = {};
  for (int i = 0; i < 6; ++i) {
    uint64_t amount = std::min(parts[i], remaining);
    storage_amounts[i] = amount;
    remaining -= amount;
    int w = int(amount * 520 / capacity);
    segment_x[i] = x; segment_w[i] = w;
    if (w > 0) {
      auto *piece = lv_obj_create(content_bar);
      lv_obj_remove_style_all(piece);
      lv_obj_set_pos(piece, x, 0);
      lv_obj_set_size(piece, w, 10);
      lv_obj_set_style_bg_color(piece, lv_color_hex(palette[i]), 0);
      lv_obj_set_style_bg_opa(piece, LV_OPA_COVER, 0);
      x += w;
    }
  }
  if (remaining) {
    auto *piece = lv_obj_create(content_bar);
    lv_obj_remove_style_all(piece);
    lv_obj_set_pos(piece, x, 0);
    lv_obj_set_size(piece, int(remaining * 520 / capacity), 10);
    lv_obj_set_style_bg_color(piece, t.text_dim, 0);
    lv_obj_set_style_bg_opa(piece, LV_OPA_COVER, 0);
  }
  segment_x[6]=x; segment_w[6]=int(remaining*520/capacity);
  segment_x[7]=x+segment_w[6]; segment_w[7]=std::max(0,520-segment_x[7]);
  storage_amounts[6]=remaining; storage_amounts[7]=std::min(cs.free_bytes,capacity);
  if(storage_focus) {
    auto *highlight=lv_obj_create(content_bar);
    lv_obj_remove_style_all(highlight);
    lv_obj_set_pos(highlight,segment_x[storage_segment],0);
    lv_obj_set_size(highlight,std::max(2,segment_w[storage_segment]),10);
    lv_obj_set_style_border_width(highlight,2,0);
    lv_obj_set_style_border_color(highlight,lv_color_white(),0);
  }
  const char *segment_names[] = {"Documents", "Themes", "Fonts", "Keyboard layouts", "Dictionaries", "Packs", "Other", "Free"};
  uint64_t segment_sizes[] = {document_bytes,sizes[0],sizes[1],sizes[2],sizes[3],sizes[4],remaining,cs.free_bytes};
  const int shown_segment=storage_focus ? storage_segment : 7;
  lv_label_set_text_fmt(content_legend, "%s: %s", segment_names[shown_segment], format_size(segment_sizes[shown_segment]).c_str());
  for (size_t i = 0; i < content_rows.size(); ++i) {
    const auto &r = content_rows[i];
    const bool is_header = r.id.rfind("header", 0) == 0;
    auto *line = lv_obj_create(content_list);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, is_header ? 520 : 504, 30);
    lv_obj_set_pos(line, is_header ? 0 : 16, int(i) * 31 + r.section * 10);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    if (is_header) lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(line,
                              int(i) == content_cursor && !storage_focus
                                  ? t.sidebar_selected
                                  : (r.id.rfind("header", 0) == 0
                                         ? t.sidebar_btn_hover
                                         : t.sidebar_btn_normal),
                              0);
    auto *label = lv_label_create(line);
    lv_obj_set_style_text_font(label, get_ui_font(is_header ? 16 : 14), 0);
    lv_obj_set_style_text_color(label, t.sidebar_btn_text, 0);
    lv_label_set_text_fmt(
        label, "%s%s", !r.installed && !r.more ? LV_SYMBOL_DOWNLOAD "  " : "",
        r.name.c_str());
    lv_obj_set_width(label, 300);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
    auto *detail = lv_label_create(line);
    lv_obj_set_style_text_font(detail, get_ui_font(14), 0);
    lv_obj_set_style_text_color(detail, t.text_dim, 0);
    lv_label_set_text(detail, r.detail.c_str());
    lv_obj_align(detail, LV_ALIGN_RIGHT_MID, -10, 0);
    if (r.id.rfind("header",0)==0) continue;
    lv_obj_add_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        line,
        [](lv_event_t *e) {
          content_cursor =
              int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
          storage_focus = false;
          if (content::busy() || content_cursor < 0 ||
              content_cursor >= int(content_rows.size()))
            return;
          const auto row = content_rows[content_cursor];
          if (row.more) {
            pending_section = row.section;
            content::refresh();
          } else if (!row.installed && row.id.rfind("download:", 0) == 0) {
            content::install(row.id.substr(9));
          }
          content_render_key.clear();
          refresh();
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(intptr_t(i)));
    if (int(i) == content_cursor && !jumping_to_section && !storage_focus)
      lv_obj_scroll_to_view(line, LV_ANIM_OFF);
  }
  if (jumping_to_section) {
    lv_obj_update_layout(content_list);
    lv_obj_scroll_to_y(content_list, content_cursor * 31 + content_rows[content_cursor].section * 10, LV_ANIM_OFF);
  }
}
void refresh_bindings() {
  if (!binding_panel || !editor_ptr)
    return;
  const auto &t = get_theme(editor_ptr->dark_theme);
  lv_obj_set_style_bg_color(binding_panel, t.sidebar_bg, 0);
  lv_obj_set_style_border_color(binding_panel, t.sidebar_btn_hover, 0);
  for (int i = 0; i < keyboard_config::action_count; ++i) {
    lv_obj_set_style_bg_color(
        binding_rows[i],
        i == action ? t.sidebar_selected : t.sidebar_btn_normal, 0);
    lv_obj_set_style_text_color(binding_labels[i], t.sidebar_btn_text, 0);
    lv_obj_set_style_text_color(binding_values[i], t.sidebar_btn_text, 0);
    lv_label_set_text(binding_labels[i], keyboard_config::action_name(i));
    const std::string binding = keyboard_config::binding_name(i);
    lv_label_set_text(binding_values[i], i == action && capturing
                                             ? "Press keys..."
                                             : binding.c_str());
  }
  lv_obj_set_style_text_color(binding_hint, t.text_dim, 0);
  lv_label_set_text(binding_hint,
                    notice ? notice
                    : capturing
                        ? "Press Ctrl and/or Opt + key. Esc / `: back"
                        : "Up/Down: select   Enter: edit   Esc / `: back");
}
void close_bindings() {
  if (binding_panel)
    lv_obj_delete(binding_panel);
  binding_panel = nullptr;
  binding_hint = nullptr;
  capturing = false;
  notice = nullptr;
  refresh();
}
void open_bindings() {
  if (binding_panel)
    return;
  action = 0;
  capturing = false;
  notice = nullptr;
  binding_panel = lv_obj_create(overlay);
  lv_obj_set_size(binding_panel, 590, 410);
  lv_obj_center(binding_panel);
  lv_obj_clear_flag(binding_panel, LV_OBJ_FLAG_SCROLLABLE);
  auto *title = lv_label_create(binding_panel);
  lv_label_set_text(title, "Edit key bindings");
  lv_obj_set_style_text_font(title, get_ui_font(20), 0);
  lv_obj_set_style_text_color(
      title, get_theme(editor_ptr->dark_theme).sidebar_btn_text, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);
  for (int i = 0; i < keyboard_config::action_count; ++i) {
    binding_rows[i] = lv_obj_create(binding_panel);
    lv_obj_remove_style_all(binding_rows[i]);
    lv_obj_set_size(binding_rows[i], 540, 23);
    lv_obj_align(binding_rows[i], LV_ALIGN_TOP_MID, 0, 32 + i * 24);
    lv_obj_set_style_bg_opa(binding_rows[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(binding_rows[i], 4, 0);
    binding_labels[i] = lv_label_create(binding_rows[i]);
    lv_obj_set_style_text_font(binding_labels[i], get_ui_font(14), 0);
    lv_obj_align(binding_labels[i], LV_ALIGN_LEFT_MID, 8, 0);
    binding_values[i] = lv_label_create(binding_rows[i]);
    lv_obj_set_style_text_font(binding_values[i], get_ui_font(14), 0);
    lv_obj_align(binding_values[i], LV_ALIGN_LEFT_MID, 230, 0);
  }
  binding_hint = lv_label_create(binding_panel);
  lv_obj_set_width(binding_hint, 540);
  lv_label_set_long_mode(binding_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(binding_hint, get_ui_font(14), 0);
  lv_obj_align(binding_hint, LV_ALIGN_BOTTOM_MID, 0, 0);
  refresh_bindings();
}
void open_choice(lv_obj_t *dropdown) {
  if (!dropdown)
    return;
  open_dropdown = dropdown;
  lv_dropdown_open(dropdown);
}
void cancel_choice() {
  if (open_dropdown)
    lv_dropdown_close(open_dropdown);
  open_dropdown = nullptr;
  refresh();
}
void apply_choice() {
  if (!open_dropdown || !editor_ptr)
    return;
  lv_obj_t *choice = open_dropdown;
  const int value = lv_dropdown_get_selected(choice);
  lv_dropdown_close(choice);
  open_dropdown = nullptr;
  const int section = choice == theme_dropdown && value == int(theme_items.size()) ? 0
                    : choice == font_dropdown && value == int(font_ids.size()) ? 1
                    : choice == layout_dropdown && value == int(content::layout_choices().size()) ? 2
                    : choice == dictionary_dropdown && value == int(dictionary_ids.size()) ? 3 : -1;
  if (section >= 0) {
    tab = 3;
    target_content_section = section;
    pending_section = section;
    content_render_key.clear();
    content::refresh();
    refresh();
    return;
  }
  if (choice == content_dropdown) {
    content_selection = value;
  } else if (choice == dictionary_dropdown) {
    if (value == 0)
      content::disable(content::state().dictionary);
    else if (value < int(dictionary_ids.size()))
      content::enable(dictionary_ids[value]);
    content::tick();
  } else if (choice == font_dropdown) {
    if (value == 0)
      content::disable(content::state().font);
    else if (value < int(font_ids.size()))
      content::enable(font_ids[value]);
  } else if (choice == layout_dropdown) {
    keyboard_config::set_layout(value);
    notice =
        keyboard_config::save() ? nullptr : "Could not save keyboard settings.";
  } else if (choice == theme_dropdown) {
    if (!content::select_theme(value, editor_ptr->dark_theme)) {
      notice = "Could not select theme.";
      refresh();
      return;
    }
    content::tick();
    update_editor_theme(*editor_ptr, editor_ptr->dark_theme);
    update_sidebar_theme(*sidebar_ptr, editor_ptr->dark_theme);
    refresh_file_list_ui(*sidebar_ptr, *editor_ptr);
    editor_ptr->state_pending_save = true;
    editor_ptr->state_change_time = lv_tick_get();
  } else {
    editor_ptr->font_size_index = editor_font_indices[std::clamp(value,0,3)];
    update_editor_font(*editor_ptr);
    editor_ptr->state_pending_save = true;
    editor_ptr->state_change_time = lv_tick_get();
  }
  refresh();
}
void refresh() {
  if (!overlay || !editor_ptr)
    return;
  const auto cs = content::state();
  std::string preview_id;
  if (tab==3 && !storage_focus && content_cursor>=0 && content_cursor<int(content_rows.size())) {
    const auto &row=content_rows[content_cursor];
    if(row.section==0 && !row.builtin && !row.more)preview_id=row.id;
  }
  if(preview_id!=previewed_theme || (!preview_id.empty() && previewed_theme_revision!=cs.revision)) {
    Theme preview;
    if(!preview_id.empty() && content::preview_theme(preview_id,preview))set_theme_preview(preview);
    else {clear_theme_preview();preview_id.clear();}
    previewed_theme=preview_id;previewed_theme_revision=cs.revision;
    content_render_key.clear();
    update_editor_theme(*editor_ptr,editor_ptr->dark_theme);
    if(sidebar_ptr)update_sidebar_theme(*sidebar_ptr,editor_ptr->dark_theme);
  }
  const Theme &t = get_theme(editor_ptr->dark_theme);
  if (binding_panel)
    refresh_bindings();
  if (cs.busy) {
    if (content_resume_selection < 0)
      content_resume_selection = selection;
    tab = 3;
    selection = 5;
  } else if (content_resume_selection >= 0) {
    selection = content_resume_selection;
    content_resume_selection = -1;
  }
  for (auto *button : tabs) {
    if (cs.busy)
      lv_obj_add_state(button, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(button, LV_STATE_DISABLED);
  }
  for (int i = 0; i < 7; ++i) {
    if (cs.busy && i != 5)
      lv_obj_add_state(rows[i], LV_STATE_DISABLED);
    else
      lv_obj_remove_state(rows[i], LV_STATE_DISABLED);
  }
  if (cs.busy)
    lv_obj_add_state(content_dropdown, LV_STATE_DISABLED);
  else
    lv_obj_remove_state(content_dropdown, LV_STATE_DISABLED);
  const std::string selected_content =
      content_selection < int(content_items.size())
          ? content_items[content_selection].id
          : "";
  if (open_dropdown != content_dropdown) {
    content_items = cs.installed;
    for (const auto &p : cs.available) {
      auto old = std::find_if(content_items.begin(), content_items.end(),
                              [&](const auto &v) { return v.id == p.id; });
      if (old == content_items.end())
        content_items.push_back(p);
      else
        *old = p;
    }
    auto selected =
        std::find_if(content_items.begin(), content_items.end(),
                     [&](const auto &p) { return p.id == selected_content; });
    if (selected != content_items.end())
      content_selection = int(selected - content_items.begin());
  }
  std::string choices;
  for (const auto &p : content_items) {
    if (!choices.empty())
      choices += '\n';
    choices += p.name + " (" + p.type + ")";
  }
  if (choices.empty())
    choices = "No content; refresh catalog";
  if (choices != content_options && open_dropdown != content_dropdown) {
    content_options = choices;
    lv_dropdown_set_options(content_dropdown, choices.c_str());
    content_selection =
        std::min(content_selection, std::max(0, int(content_items.size()) - 1));
    lv_dropdown_set_selected(content_dropdown, content_selection);
  }
  if (open_dropdown != theme_dropdown) {
    theme_items = content::theme_choices();
    std::string options;
    for (const auto &item : theme_items) {
      if (!options.empty())
        options += '\n';
      options += item.name;
    }
    options += "\nGet more...";
    if (options != theme_options) {
      theme_options = options;
      lv_dropdown_set_options(theme_dropdown, options.c_str());
    }
    lv_dropdown_set_selected(
        theme_dropdown, content::active_theme_index(editor_ptr->dark_theme));
  }
  if (open_dropdown != dictionary_dropdown) {
    dictionary_ids = {""};
    std::string options = "None";
    int selected = 0;
    for (const auto &p : cs.installed)
      if (p.type == "dictionary") {
        dictionary_ids.push_back(p.id);
        options += '\n' + p.name;
        if (p.id == cs.dictionary)
          selected = int(dictionary_ids.size()) - 1;
      }
    options += "\nGet more...";
    if (options != dictionary_options) {
      dictionary_options = options;
      lv_dropdown_set_options(dictionary_dropdown, options.c_str());
    }
    lv_dropdown_set_selected(dictionary_dropdown, selected);
  }
  if (tab == 0)
    lv_obj_remove_flag(dictionary_dropdown, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(dictionary_dropdown, LV_OBJ_FLAG_HIDDEN);
  font_ids = {""};
  std::string fonts = "Montserrat";
  int selected_font = 0;
  for (const auto &p : cs.installed)
    if (p.type == "font") {
      font_ids.push_back(p.id);
      fonts += '\n' + p.name;
      if (cs.font == p.id)
        selected_font = int(font_ids.size()) - 1;
    }
  fonts += "\nGet more...";
  if (font_options != fonts && open_dropdown != font_dropdown) {
    font_options = fonts;
    lv_dropdown_set_options(font_dropdown, fonts.c_str());
  }
  if (open_dropdown != font_dropdown)
    lv_dropdown_set_selected(font_dropdown, selected_font);
  lv_obj_add_flag(content_dropdown, LV_OBJ_FLAG_HIDDEN);
  if (tab == 3)
    lv_obj_remove_flag(content_view, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(content_view, LV_OBJ_FLAG_HIDDEN);
  if (tab == 0)
    lv_obj_remove_flag(font_dropdown, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(font_dropdown, LV_OBJ_FLAG_HIDDEN);
  for (auto *obj :
       {theme_dropdown, size_dropdown, brightness_slider, brightness_value}) {
    if (tab == 0)
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
  if (tab == 1)
    lv_obj_remove_flag(layout_dropdown, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(layout_dropdown, LV_OBJ_FLAG_HIDDEN);
  for (auto *obj : {automatic_switch, automatic_value}) {
    if (tab == 2)
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
  if (open_dropdown != size_dropdown)
    lv_dropdown_set_selected(size_dropdown, editor_font_choice(editor_ptr->font_size_index));
  if (open_dropdown != layout_dropdown) {
    std::string opts;for(const auto &item:content::layout_choices()){if(!opts.empty())opts+='\n';opts+=item.name;}opts+="\nGet more...";
    lv_dropdown_set_options(layout_dropdown,opts.c_str());
    lv_dropdown_set_selected(layout_dropdown, keyboard_config::layout());
  }
  lv_slider_set_value(brightness_slider, backlight::percent(), LV_ANIM_OFF);
  lv_label_set_text_fmt(brightness_value, "%d%%", backlight::percent());
  lv_obj_set_style_text_color(brightness_value, t.sidebar_btn_text, 0);
  lv_obj_set_style_bg_color(brightness_slider, t.sidebar_btn_hover,
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightness_slider, t.sidebar_new_file,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness_slider, t.sidebar_btn_text,
                            LV_PART_KNOB);
  const bool automatic_selected = tab == 2 && selection == 2;
  lv_obj_set_style_bg_color(automatic_switch, t.sidebar_btn_hover,
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(automatic_switch, t.sidebar_btn_hover,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(
      automatic_switch, t.sidebar_selected,
      static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
          static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
  lv_obj_set_style_bg_color(automatic_switch, t.sidebar_btn_text, LV_PART_KNOB);
  lv_obj_set_style_outline_color(automatic_switch, t.cursor, LV_PART_MAIN);
  lv_obj_set_style_outline_opa(automatic_switch, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_width(automatic_switch, automatic_selected ? 2 : 0,
                                 LV_PART_MAIN);
  lv_obj_set_style_outline_pad(automatic_switch, 2, LV_PART_MAIN);
  lv_obj_set_style_text_color(automatic_value, t.sidebar_btn_text, 0);
  for (auto *choice : {theme_dropdown, size_dropdown, layout_dropdown,
                       font_dropdown, content_dropdown, dictionary_dropdown}) {
    const bool selected =
        (tab == 0 && ((selection == 0 && choice == theme_dropdown) ||
                      (selection == 1 && choice == size_dropdown))) ||
        (tab == 1 && selection == 0 && choice == layout_dropdown) ||
        (tab == 0 && selection == 2 && choice == font_dropdown) ||
        (tab == 0 && selection == 4 && choice == dictionary_dropdown) ||
        (tab == 3 && selection == 0 && choice == content_dropdown);
    lv_obj_set_style_bg_color(choice,
                              selected ? t.sidebar_selected : t.search_input_bg,
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(choice, t.sidebar_btn_text, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        choice, selected ? t.sidebar_selected : t.sidebar_btn_hover,
        LV_PART_MAIN);
    auto *list = lv_dropdown_get_list(choice);
    lv_obj_set_style_bg_color(list, t.search_input_bg, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, t.sidebar_btn_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, get_ui_font(16), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        list, t.sidebar_selected,
        static_cast<lv_style_selector_t>(LV_PART_SELECTED) |
            static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
    lv_obj_set_style_max_height(list, 220, LV_PART_MAIN);
  }
  lv_obj_set_style_bg_color(panel, t.sidebar_bg, 0);
  lv_obj_set_style_text_color(title_label, t.sidebar_btn_text, 0);
  lv_obj_set_style_text_color(hint_label, t.text_dim, 0);
  lv_obj_set_style_border_color(panel, t.sidebar_btn_hover, 0);
  for (int i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_color(
        tabs[i], i == tab ? t.sidebar_selected : t.sidebar_btn_normal, 0);
    lv_obj_set_style_text_color(tab_labels[i], t.sidebar_btn_text, 0);
  }
  for (int i = 0; i < 7; ++i) {
    lv_obj_align(rows[i], LV_ALIGN_TOP_MID, 0, 48 + i * (tab == 3 ? 36 : 39));
    if (tab == 3 || i >= (tab == 2 ? 5 : tab == 1 ? 4 : 6))
      lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < 7; ++i) {
    const bool control_row = (tab == 0 && (i == 0 || i == 1)) ||
                             (tab == 1 && i == 0) || (tab == 2 && i == 2) ||
                             (tab == 0 && (i == 2 || i == 4)) ||
                             (tab == 3 && i == 0);
    lv_obj_set_style_bg_color(rows[i],
                              i == selection && !control_row
                                  ? t.sidebar_selected
                                  : t.sidebar_btn_normal,
                              0);
    lv_obj_set_style_text_color(labels[i], t.sidebar_btn_text, 0);
  }
  lv_label_set_text(hint_label, notice ? notice
                                       : "Tab: switch tab   Enter: open   "
                                         "Arrows: navigate   Esc / `: close");
  lv_obj_set_height(hint_label, tab == 3 ? 24 : LV_SIZE_CONTENT);
  lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_label_set_long_mode(hint_label,
                         tab == 3 ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
  if (tab == 3) {
    close_signin();
    render_content(cs, t);
    lv_label_set_text(hint_label, "");
    if (cs.busy) {
      const char *title = cs.message == "Queued content operation" ? "Waiting for network..." : cs.message.c_str();
      show_loading_dialog(title,t);
    } else if(cs.result_generation != seen_content_result) {
      seen_content_result=cs.result_generation;
      show_result_dialog(cs.last_success,cs.last_success ? "Content updated!" : cs.message.c_str(),t);
    } else if(dialog_result && lv_tick_elaps(dialog_result_start)>=(dialog_result_success ? 1200u : 3000u)) hide_result_dialog();
    return;
  }
  if(tab!=2 && !wifi_form) hide_result_dialog();
  if (tab == 2) {
    auto state = backup::status();
    if(link_requested) {
      if(state.link_generation != link_start_generation) {
        link_requested=false;
        show_result_dialog(state.link_success,state.link_success ? "Google Drive linked!" : state.message.c_str(),t);
      } else if(!state.user_code.empty()) {
        if(dialog_loading)hide_result_dialog();
      } else {
        const char *stage="Connecting to Google Drive...";
        if(state.message.rfind("Connecting",0)==0 || state.message.rfind("Synchronizing",0)==0 || state.message.rfind("Requesting Google",0)==0)stage=state.message.c_str();
        show_loading_dialog(stage,t);
      }
    } else if(dialog_result && lv_tick_elaps(dialog_result_start)>=(dialog_result_success ? 1200u : 3000u)) hide_result_dialog();
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
    lv_label_set_text(labels[2], "Automatic backup (every 30 min while idle)");
    lv_label_set_text(automatic_value, state.automatic ? "On" : "Off");
    if (state.automatic)
      lv_obj_add_state(automatic_switch, LV_STATE_CHECKED);
    else
      lv_obj_remove_state(automatic_switch, LV_STATE_CHECKED);
    lv_label_set_text_fmt(
        labels[3], "Back up now (%s)",
        keyboard_config::binding_name(keyboard_config::backup_action).c_str());
    lv_label_set_text(labels[4], "Close");
    const std::string status = state.user_code.empty()
                                   ? state.message
                                   : state.verification_url + "\nEnter " +
                                         state.user_code + " on your phone";
    lv_label_set_text(hint_label, status.c_str());
    return;
  }
  close_signin();
  if (tab == 1) {
    lv_label_set_text(labels[0], "Layout");
    lv_label_set_text(labels[1], "Edit key bindings");
    lv_label_set_text(labels[2], "Reset keyboard defaults");
    lv_label_set_text(labels[3], "Close");
    return;
  }
  lv_label_set_text(labels[0], "Theme");
  lv_label_set_text(labels[1], "Font size");
  lv_label_set_text(labels[2], "Font");
  lv_label_set_text(labels[3], "Brightness");
  lv_label_set_text(labels[4], "Spellcheck dictionary");
  lv_label_set_text(labels[5], "Close");
}
void close() {
  if (content::busy())
    return;
  if (!overlay)
    return;
  if(theme_preview_active()) {
    clear_theme_preview();previewed_theme.clear();
    update_editor_theme(*editor_ptr,editor_ptr->dark_theme);
    if(sidebar_ptr)update_sidebar_theme(*sidebar_ptr,editor_ptr->dark_theme);
  }
  if (refresh_timer) {
    lv_timer_delete(refresh_timer);
    refresh_timer = nullptr;
  }
  close_wifi();
  close_signin();
  link_requested=false;
  if (open_dropdown)
    lv_dropdown_close(open_dropdown);
  open_dropdown = binding_panel = nullptr;
  lv_obj_delete(overlay);
  overlay = panel = title_label = hint_label = nullptr;
  content_scrim = content_toast = nullptr;
  content_view = content_list = content_bar = content_legend = delete_popup =
      nullptr;
  sidebar_ptr = nullptr;
  editor_ptr = nullptr;
}
void change(int delta) {
  if (!editor_ptr || !sidebar_ptr)
    return;
  if (tab == 3) {
    if (selection == 5 && content::state().busy)
      content::cancel();
    else
      content_action(selection);
    refresh();
    return;
  }
  if (tab == 2) {
    if (selection == 0)
      open_wifi();
    else if (selection == 1) {
      link_requested=true;
      link_start_generation=backup::status().link_generation;
      show_loading_dialog("Connecting to Google Drive...",get_theme(editor_ptr->dark_theme));
      backup::request_link();
    }
    else if (selection == 2)
      backup::set_automatic(!backup::status().automatic);
    else if (selection == 3)
      backup::request_now();
    refresh();
    return;
  }
  if (tab == 1)
    return; // Layout changes only through its dropdown.
  if (selection == 0) {
    const int count = int(content::theme_choices().size());
    const int next =
        (content::active_theme_index(editor_ptr->dark_theme) + delta + count) %
        count;
    if (!content::select_theme(next, editor_ptr->dark_theme)) {
      notice = "Could not select theme.";
      refresh();
      return;
    }
    content::tick();
    update_editor_theme(*editor_ptr, editor_ptr->dark_theme);
    update_sidebar_theme(*sidebar_ptr, editor_ptr->dark_theme);
    refresh_file_list_ui(*sidebar_ptr, *editor_ptr);
  } else if (selection == 1) {
    editor_ptr->font_size_index = editor_font_indices[std::clamp(editor_font_choice(editor_ptr->font_size_index)+delta,0,3)];
    update_editor_font(*editor_ptr);
  } else if (selection == 2) {
    open_choice(font_dropdown);
    return;
  } else if (selection == 3) {
    notice = backlight::adjust(delta * 10) ? nullptr
                                           : "Brightness control unavailable.";
    refresh();
    return;
  } else
    return;
  editor_ptr->state_pending_save = true;
  editor_ptr->state_change_time = lv_tick_get();
  refresh();
}
} // namespace
bool settings_active() { return overlay != nullptr; }
void settings_refresh() { refresh(); }
bool settings_capturing_binding() { return binding_panel && capturing; }
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
  link_requested=false;
  seen_content_result = content::state().result_generation;
  dialog_result = false;
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
  lv_obj_set_style_text_font(title, get_ui_font(20), 0);
  lv_obj_set_style_text_color(title,
                              get_theme(editor.dark_theme).sidebar_btn_text, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 2);
  for (int i = 0; i < 4; ++i) {
    tabs[i] = lv_btn_create(panel);
    lv_obj_set_size(tabs[i], 108, 32);
    lv_obj_align(tabs[i], LV_ALIGN_TOP_RIGHT, -336 + i * 112, 0);
    lv_obj_add_event_cb(
        tabs[i],
        [](lv_event_t *e) {
          if (content::busy())
            return;
          const int next_tab = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
          if(next_tab == 3 && tab != 3) {target_content_section=0;storage_focus=false;content_render_key.clear();}
          tab = next_tab;
          selection = 0;
          capturing = false;
          notice = nullptr;
          refresh();
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    tab_labels[i] = lv_label_create(tabs[i]);
    lv_label_set_text(tab_labels[i], i == 0   ? "Editor"
                                     : i == 1 ? "Keyboard"
                                     : i == 2 ? "Backup"
                                              : "Content");
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
          if (content::busy()) {
            if (row == 5)
              content::cancel();
            refresh();
            return;
          }
          selection = row;
          notice = nullptr;
          if (row == (tab == 2 ? 4 : tab == 3 ? 6 : tab == 1 ? 3 : 5))
            close();
          else if (tab == 2 || tab == 3) {
            if (tab == 3 && row == 0)
              open_choice(content_dropdown);
            else
              change(1);
          } else if (tab == 1 && row == 0)
            open_choice(layout_dropdown);
          else if (tab == 1 && row == 1)
            open_bindings();
          else if (tab == 1 && row == 2) {
            keyboard_config::reset();
            if (!keyboard_config::save())
              notice = "Could not save keyboard settings.";
            refresh();
          } else if (tab == 0 && row == 0)
            open_choice(theme_dropdown);
          else if (tab == 0 && row == 1)
            open_choice(size_dropdown);
          else if (tab == 0 && row == 2)
            open_choice(font_dropdown);
          else if (tab == 0 && row == 4)
            open_choice(dictionary_dropdown);
          else
            refresh();
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    labels[i] = lv_label_create(rows[i]);
    lv_obj_set_style_text_font(labels[i], get_ui_font(16), 0);
    lv_obj_align(labels[i], LV_ALIGN_LEFT_MID, 12, 0);
  }
  auto dropdown = [](lv_obj_t *parent, const char *options) {
    auto *obj = lv_dropdown_create(parent);
    lv_dropdown_set_options(obj, options);
    lv_obj_set_size(obj, 220, 32);
    lv_obj_align(obj, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_text_font(obj, get_ui_font(14), 0);
    lv_obj_add_event_cb(
        obj,
        [](lv_event_t *event) {
          auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
          if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
            open_dropdown = target;
            apply_choice();
          } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
            open_dropdown = lv_dropdown_is_open(target) ? target : nullptr;
          }
        },
        LV_EVENT_ALL, nullptr);
    return obj;
  };
  theme_dropdown = dropdown(rows[0], "Light\nDark\nOcean\nForest\nSunset");
  theme_options.clear();
  size_dropdown = dropdown(rows[1], "16\n20\n24\n28");
  layout_dropdown = dropdown(
      rows[0],
      "US QWERTY\nDvorak\nDvorak right hand\nSpanish (Spain)\nRussian\nGet more...");
  brightness_slider = lv_slider_create(rows[3]);
  lv_obj_set_size(brightness_slider, 170, 8);
  lv_obj_align(brightness_slider, LV_ALIGN_RIGHT_MID, -55, 0);
  lv_slider_set_range(brightness_slider, 10, 100);
  lv_obj_clear_flag(brightness_slider, LV_OBJ_FLAG_CLICKABLE);
  font_dropdown = dropdown(rows[2], "Montserrat");
  dictionary_dropdown = dropdown(rows[4], "None");
  dictionary_options.clear();
  content_dropdown = dropdown(rows[0], "No content; refresh catalog");
  content_view = lv_obj_create(panel);
  lv_obj_remove_style_all(content_view);
  lv_obj_set_size(content_view, 540, 302);
  lv_obj_align(content_view, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_clear_flag(content_view, LV_OBJ_FLAG_SCROLLABLE);
  content_legend = lv_label_create(content_view);
  lv_obj_set_style_text_font(content_legend, get_ui_font(14), 0);
  lv_obj_set_style_text_color(content_legend,
                              get_theme(editor.dark_theme).text_dim, 0);
  lv_obj_set_width(content_legend, 530);
  lv_obj_set_pos(content_legend, 0, 17);
  lv_label_set_long_mode(content_legend, LV_LABEL_LONG_DOT);
  content_bar = lv_obj_create(content_view);
  lv_obj_remove_style_all(content_bar);
  lv_obj_set_pos(content_bar, 0, 0);
  lv_obj_set_size(content_bar, 520, 12);
  lv_obj_set_style_bg_color(content_bar,
                            get_theme(editor.dark_theme).sidebar_btn_hover, 0);
  lv_obj_set_style_bg_opa(content_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content_bar, 1, 0);
  lv_obj_set_style_border_color(content_bar, get_theme(editor.dark_theme).text_dim, 0);
  lv_obj_set_style_radius(content_bar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(content_bar, true, 0);
  lv_obj_clear_flag(content_bar, LV_OBJ_FLAG_SCROLLABLE);
  content_list = lv_obj_create(content_view);
  lv_obj_remove_style_all(content_list);
  lv_obj_set_pos(content_list, 0, 36);
  lv_obj_set_size(content_list, 530, 242);
  lv_obj_set_scroll_dir(content_list, LV_DIR_VER);
  lv_obj_add_flag(content_list, LV_OBJ_FLAG_SCROLLABLE);
  content_render_key.clear();
  content_rows.clear();
  content_cursor = 0;
  storage_focus = false; storage_segment = 0;
  delete_confirm = false;
  delete_popup = nullptr;
  pending_section = -1;
  target_content_section = -1;
  for (auto &b : expanded_sections)
    b = false;
  document_bytes = directory_bytes("/spiffs/user_files");

  content_options.clear();
  font_options.clear();
  brightness_value = lv_label_create(rows[3]);
  lv_obj_set_style_text_font(brightness_value, get_ui_font(14), 0);
  lv_obj_align(brightness_value, LV_ALIGN_RIGHT_MID, -8, 0);
  automatic_switch = lv_switch_create(rows[2]);
  lv_obj_set_size(automatic_switch, 48, 24);
  lv_obj_align(automatic_switch, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(
      automatic_switch,
      [](lv_event_t *event) {
        selection = 2;
        auto *toggle = static_cast<lv_obj_t *>(lv_event_get_target(event));
        backup::set_automatic(lv_obj_has_state(toggle, LV_STATE_CHECKED));
        refresh();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  automatic_value = lv_label_create(rows[2]);
  lv_obj_set_style_text_font(automatic_value, get_ui_font(14), 0);
  lv_obj_align(automatic_value, LV_ALIGN_RIGHT_MID, -67, 0);
  lv_obj_t *hint = hint_label = lv_label_create(panel);
  lv_label_set_text(hint,
                    "Up/Down select   Left/Right change   Cmd / Esc / ~ close");
  lv_obj_set_style_text_font(hint, get_ui_font(14), 0);
  lv_obj_set_style_text_color(hint, get_theme(editor.dark_theme).text_dim, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_width(hint, 540);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  content_scrim=lv_obj_create(overlay);
  lv_obj_remove_style_all(content_scrim);
  lv_obj_set_size(content_scrim,LV_PCT(100),LV_PCT(100));
  lv_obj_set_style_bg_color(content_scrim,lv_color_black(),0);
  lv_obj_set_style_bg_opa(content_scrim,LV_OPA_50,0);
  lv_obj_clear_flag(content_scrim,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(content_scrim,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(content_scrim,LV_OBJ_FLAG_HIDDEN);
  content_toast=lv_obj_create(content_scrim);
  lv_obj_set_size(content_toast,340,190);
  lv_obj_center(content_toast);
  lv_obj_clear_flag(content_toast,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(content_toast,10,0);
  lv_obj_set_style_border_width(content_toast,1,0);
  lv_obj_set_style_bg_opa(content_toast,LV_OPA_COVER,0);
  dialog_title=lv_label_create(content_toast);
  lv_obj_set_width(dialog_title,300);
  lv_obj_set_style_text_font(dialog_title,get_ui_font(16),0);
  lv_obj_set_style_text_align(dialog_title,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(dialog_title,LV_LABEL_LONG_WRAP);
  lv_obj_align(dialog_title,LV_ALIGN_TOP_MID,0,2);
  content_loader.create(content_toast);
  lv_obj_align(content_loader.object(),LV_ALIGN_CENTER,0,20);
  dialog_check=lv_obj_create(content_toast);
  lv_obj_remove_style_all(dialog_check);lv_obj_set_size(dialog_check,66,66);
  lv_obj_set_style_radius(dialog_check,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(dialog_check,lv_color_hex(0x00a653),0);
  lv_obj_set_style_bg_opa(dialog_check,LV_OPA_COVER,0);
  lv_obj_align(dialog_check,LV_ALIGN_CENTER,0,20);
  static const lv_point_precise_t check_points[]={{16,34},{27,45},{50,21}};
  auto *check=lv_line_create(dialog_check);lv_line_set_points(check,check_points,3);
  lv_obj_set_style_line_color(check,lv_color_white(),0);
  lv_obj_set_style_line_width(check,7,0);lv_obj_set_style_line_rounded(check,true,0);
  dialog_warning=lv_obj_create(content_toast);
  lv_obj_remove_style_all(dialog_warning);lv_obj_set_size(dialog_warning,72,66);
  lv_obj_align(dialog_warning,LV_ALIGN_CENTER,0,20);
  lv_obj_add_event_cb(dialog_warning,[](lv_event_t *e){
    lv_area_t area;lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(e)),&area);
    lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);
    d.bg_color=lv_color_hex(0xf4bf36);d.bg_opa=LV_OPA_COVER;
    d.p[0]={area.x1+36,area.y1+2};d.p[1]={area.x1+2,area.y1+63};d.p[2]={area.x1+70,area.y1+63};
    lv_draw_triangle(lv_event_get_layer(e),&d);
  },LV_EVENT_DRAW_MAIN,nullptr);
  auto *mark=lv_label_create(dialog_warning);lv_label_set_text(mark,"!");
  lv_obj_set_style_text_font(mark,get_ui_font(24),0);
  lv_obj_set_style_text_color(mark,lv_color_hex(0x292414),0);
  lv_obj_align(mark,LV_ALIGN_CENTER,0,8);
  lv_obj_add_flag(dialog_check,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dialog_warning,LV_OBJ_FLAG_HIDDEN);
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
  if (content::busy()) {
    if (e.type == platform::PlatformEvent::Type::KeyDown && !e.repeat &&
        (e.key == platform::KeyCode::Escape ||
         e.key == platform::KeyCode::Grave ||
         e.key == platform::KeyCode::Enter))
      content::cancel();
    // No editing, tab switching, shortcut dispatch or deferred input while
    // busy.
    refresh();
    return;
  }
  if (dialog_result) return;
  if (delete_confirm) {
    if (e.type == platform::PlatformEvent::Type::KeyDown) {
      if (e.key == platform::KeyCode::Left || e.key == platform::KeyCode::Right) {
        delete_selection = e.key == platform::KeyCode::Left ? 0 : 1;
        update_confirmation_selection(delete_action_btn, delete_cancel_btn, delete_selection);
        return;
      }
      if (e.key == platform::KeyCode::Enter) {
        delete_confirm = false;
        if (delete_popup)
          lv_obj_delete(delete_popup);
        delete_popup = nullptr;
        if (delete_selection == 0) content::remove(delete_id);
        content_render_key.clear();
        refresh();
      } else if (e.key == platform::KeyCode::Escape ||
                 e.key == platform::KeyCode::Grave) {
        delete_confirm = false;
        if (delete_popup)
          lv_obj_delete(delete_popup);
        delete_popup = nullptr;
        refresh();
      }
    }
    return;
  }
  // Command toggles the whole Settings overlay, including its nested forms.
  // Handle it before those forms or shortcut capture consume the key.
  if (e.type == platform::PlatformEvent::Type::KeyDown &&
      e.key == platform::KeyCode::Meta) {
    close();
    return;
  }
  if (wifi_form) {
    wifi_event(e);
    return;
  }
  if (e.type != platform::PlatformEvent::Type::KeyDown)
    return;
  using K = platform::KeyCode;
  if (binding_panel) {
    if (e.key == K::Escape || e.key == K::Grave) {
      close_bindings();
      return;
    }
    if (capturing) {
      if (e.hid_usage) {
        if (keyboard_config::bind(action, e.hid_usage, e.modifiers)) {
          capturing = false;
          notice = keyboard_config::save()
                       ? "Shortcut saved."
                       : "Could not save keyboard settings.";
        } else
          notice =
              "Use an unused Ctrl/Opt + key; reserved keys cannot be assigned.";
      }
    } else if (e.key == K::Up)
      action = (action + keyboard_config::action_count - 1) %
               keyboard_config::action_count;
    else if (e.key == K::Down)
      action = (action + 1) % keyboard_config::action_count;
    else if (e.key == K::Enter) {
      capturing = true;
      notice = nullptr;
    }
    refresh_bindings();
    return;
  }
  if (open_dropdown) {
    if (e.key == K::Escape || e.key == K::Grave) {
      cancel_choice();
      return;
    }
    if (e.key == K::Enter) {
      apply_choice();
      return;
    }
    if (e.key == K::Up || e.key == K::Down) {
      const int count = open_dropdown == theme_dropdown
                            ? int(theme_items.size()) + 1
                        : open_dropdown == size_dropdown ? 4
                        : open_dropdown == content_dropdown
                            ? std::max(1, int(content_items.size()))
                        : open_dropdown == font_dropdown ? int(font_ids.size()) + 1
                        : open_dropdown == dictionary_dropdown
                            ? int(dictionary_ids.size()) + 1
                            : int(content::layout_choices().size()) + 1;
      int value =
          lv_dropdown_get_selected(open_dropdown) + (e.key == K::Down ? 1 : -1);
      lv_dropdown_set_selected(open_dropdown,
                               std::max(0, std::min(count - 1, value)));
    }
    return;
  }
  if (e.shortcut && e.key == K::B) {
    backup::request_now();
    return;
  }
  if (signin_popup) {
    if (e.key == K::Escape || e.key == K::Grave || e.key == K::Enter)
      close_signin(true);
    return;
  }
  if (e.key == K::Escape || e.key == K::Grave) {
    close();
    return;
  }
  if (tab == 3 && e.key != K::Tab) {
    if (content_rows.empty())
      return;
    if (storage_focus) {
      if (e.key == K::Left || e.key == K::Right) {
        int step=e.key == K::Right ? 1 : -1, next=storage_segment+step;
        while(next>=0 && next<8 && storage_amounts[next]==0) next+=step;
        if(next>=0 && next<8) storage_segment=next;
      }
      else if (e.key == K::Down) storage_focus = false;
      content_render_key.clear(); refresh(); return;
    }
    auto row = content_rows[content_cursor];
    if (e.key == K::Up || e.key == K::Down) {
      if (e.modifiers.ctrl) {
        int step = e.key == K::Down ? 1 : -1;
        int n = content_cursor + step;
        while (n >= 0 && n < int(content_rows.size()) &&
               content_rows[n].section == row.section)
          n += step;
        if (n >= 0 && n < int(content_rows.size()))
          content_cursor = std::min(n+1,int(content_rows.size())-1);
      } else
      {
        int step=e.key == K::Down ? 1 : -1, n=content_cursor+step;
        while(n>=0 && n<int(content_rows.size()) && content_rows[n].id.rfind("header",0)==0) n+=step;
        if(n<0) {
          storage_focus=true;
          if(storage_amounts[storage_segment]==0)
            for(int i=0;i<8;++i) if(storage_amounts[i]) {storage_segment=i;break;}
        }
        else if(n<int(content_rows.size())) content_cursor=n;
      }
      content_render_key.clear();
      refresh();
      return;
    }
    if (e.key == K::Delete || e.key == K::Backspace) {
      if (row.installed && !row.builtin && !row.more) {
        delete_id = row.id;
        delete_confirm = true;
        const auto &theme = get_theme(editor_ptr->dark_theme);
        const std::string message = "Remove \"" + row.name + "\"?";
        lv_obj_t *label = nullptr;
        delete_selection = 1;
        delete_popup = create_confirmation_dialog(overlay, theme, 390, 120, message.c_str(), "Delete", theme.sidebar_delete, &label, &delete_action_btn, &delete_cancel_btn);
        lv_obj_set_width(label, 350);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        update_confirmation_selection(delete_action_btn, delete_cancel_btn, delete_selection);
        for (int n=0;n<2;++n) {
          auto *button=n==0?delete_action_btn:delete_cancel_btn;
          lv_obj_add_event_cb(button,[](lv_event_t *event){
            bool remove=reinterpret_cast<intptr_t>(lv_event_get_user_data(event))==0;
            delete_confirm=false;
            if(delete_popup)lv_obj_delete(delete_popup);
            delete_popup=nullptr;
            if(remove)content::remove(delete_id);
            content_render_key.clear();refresh();
          },LV_EVENT_CLICKED,reinterpret_cast<void*>(intptr_t(n)));
        }
      }
      return;
    }
    if (e.key == K::Enter) {
      if (row.more) {
        pending_section = row.section;
        content::refresh();
      } else if (!row.installed && row.id.rfind("download:", 0) == 0)
        content::install(row.id.substr(9));
      content_render_key.clear();
      refresh();
      return;
    }
    return;
  }
  notice = nullptr;
  const int count = tab == 2 ? 5 : tab == 3 ? 7 : tab == 1 ? 4 : 6;
  if (e.key == K::Tab) {
    tab = (tab + 1) % 4;
    if(tab == 3) {target_content_section=0;storage_focus=false;content_render_key.clear();}
    selection = 0;
  } else if (e.key == K::Up)
    selection = (selection + count - 1) % count;
  else if (e.key == K::Down)
    selection = (selection + 1) % count;
  else if (e.key == K::Left && (tab == 0 || tab == 2))
    change(-1);
  else if (e.key == K::Right && (tab == 0 || tab == 2))
    change(1);
  else if (e.key == K::Enter) {
    if (selection == count - 1) {
      close();
      return;
    }
    if (tab == 2 || tab == 3) {
      if (tab == 3 && selection == 0)
        open_choice(content_dropdown);
      else
        change(1);
    } else if (tab == 1 && selection == 0)
      open_choice(layout_dropdown);
    else if (tab == 1 && selection == 1)
      open_bindings();
    else if (tab == 1 && selection == 2) {
      keyboard_config::reset();
      if (!keyboard_config::save())
        notice = "Could not save keyboard settings.";
    } else if (tab == 0 && selection == 0)
      open_choice(theme_dropdown);
    else if (tab == 0 && selection == 1)
      open_choice(size_dropdown);
    else if (tab == 0 && selection == 2)
      open_choice(font_dropdown);
    else if (tab == 0 && selection == 4)
      open_choice(dictionary_dropdown);
  }
  refresh();
}
