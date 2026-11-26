#pragma once
#include "lvgl.h"
#include <cstdint>
#include <string>
#include <vector>
struct Theme;
namespace content {
struct File {
  std::string name, sha256;
  uint32_t size = 0;
};
struct Package {
  std::string id, version, type, name, language, description, preview;
  std::vector<File> files;
  std::vector<std::string> dependencies;
};
struct State {
  std::vector<Package> available, installed;
  std::string message, dictionary, font, theme, layout;
  bool busy = false;
  bool last_success = false;
  unsigned result_generation = 0;
  int percent = -1;
  uint64_t free_bytes = 0;
  unsigned revision = 0;
};
void init();
State state();
bool busy();
unsigned revision();
// All resource activation is performed by the UI task, never the network
// worker.
void tick();
bool enable(const std::string &id);
bool disable(const std::string &id);
bool remove(const std::string &id);
void refresh();
void install(const std::string &id);
void cancel();
bool dictionary_available();
std::string dictionary_language();
bool contains(const std::string &word);
size_t dictionary_words();
std::string dictionary_word(size_t index);
uint32_t dictionary_root();
bool dictionary_arc(uint32_t node, uint32_t &arc);
const lv_font_t *editor_font(int size, const lv_font_t *fallback);
const lv_font_t *international_font(int size);
bool selected_theme(Theme &theme);
bool preview_theme(const std::string &id, Theme &theme);
struct LayoutChoice { std::string name, id; };
std::vector<LayoutChoice> layout_choices();
bool layout_key(size_t index, bool shifted, uint32_t &value, bool &spanish);
struct ThemeChoice { std::string name, id; int builtin = -1; };
std::vector<ThemeChoice> theme_choices();
int active_theme_index(int builtin);
bool select_theme(int index, int &builtin);
// Worker-only package operations, serialized with Drive backup by its worker.
using Download = bool (*)(const std::string &, const std::string &, uint32_t,
                          const std::string &);
struct DownloadRequest {
  std::string relative, sha256;
  uint32_t maximum = 0;
};
bool download_plan(std::vector<DownloadRequest> &requests, std::string &error);
// Browser downloads can already occupy staging space on this filesystem.
bool process(Download download, bool files_already_staged = false);
bool needs_network();
void network_error(const std::string &message);
void download_progress(int percent, const std::string &message = {});
bool cancelled();
bool validate_catalog(const std::string &json, std::vector<Package> &packages,
                      std::string &error);
const char *root();
} // namespace content
