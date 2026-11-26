#include "content.h"
#include "backup/backup.h"
#include "cJSON.h"
#include "content_font_validation.h"
#include "spelling/unicode_word.h"
#include "theme.h"
#include "international_font.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <sys/stat.h>
#include <type_traits>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define fsync _commit
#define fileno _fileno
#else
#include <unistd.h>
#endif
#ifdef __EMSCRIPTEN__
extern "C" double leaf_browser_content_free();
#endif
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#endif

namespace content {
namespace {
constexpr size_t MaxMetadata = 64 * 1024, Reserve = 1024 * 1024;
#ifndef LEAF_CONTENT_ROOT
#define LEAF_CONTENT_ROOT "/spiffs/leaf_content"
#endif
const std::string base = LEAF_CONTENT_ROOT;
std::mutex guard;
State current;
std::map<std::string, std::set<std::string>> owners;
std::atomic<bool> abort_job{false};
enum class Operation { None, Catalog, Install, Remove };
Operation operation = Operation::None;
std::string requested;
unsigned generation = 0, activated = ~0u;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const std::string &s) {
  return Json(cJSON_ParseWithLength(s.data(), s.size()), cJSON_Delete);
}
std::string text(cJSON *j, const char *key) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
bool identifier(const std::string &s) {
  if (s.empty() || s.size() > 64 || s == "." || s == "..")
    return false;
  return std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
  });
}
uint32_t crc(const std::string &s) {
  uint32_t n = ~0u;
  for (unsigned char c : s) {
    n ^= c;
    for (int b = 0; b < 8; ++b)
      n = (n >> 1) ^ (0xedb88320u & (0u - (n & 1)));
  }
  return ~n;
}
std::string read(const std::string &p, size_t limit = MaxMetadata) {
  FILE *f = fopen(p.c_str(), "rb");
  if (!f)
    return {};
  std::string out;
  char block[1024];
  size_t n;
  while ((n = fread(block, 1, sizeof block, f))) {
    if (out.size() + n > limit) {
      out.clear();
      break;
    }
    out.append(block, n);
  }
  if (ferror(f))
    out.clear();
  fclose(f);
  return out;
}
bool write(const std::string &p, const std::string &s) {
  FILE *f = fopen(p.c_str(), "wb");
  if (!f)
    return false;
  bool ok = fwrite(s.data(), 1, s.size(), f) == s.size() && fflush(f) == 0;
  if (ok && fsync(fileno(f)) != 0)
    ok = false;
  return fclose(f) == 0 && ok;
}
std::string dump(cJSON *j) {
  char *s = cJSON_PrintUnformatted(j);
  if (!s)
    return {};
  std::string out(s);
  cJSON_free(s);
  return out;
}
cJSON *package_json(const Package &p) {
  auto *j = cJSON_CreateObject();
  for (const auto &pair : {std::make_pair("id", p.id),
                           {"version", p.version},
                           {"type", p.type},
                           {"name", p.name},
                           {"language", p.language},
                           {"description", p.description}})
    cJSON_AddStringToObject(j, pair.first, pair.second.c_str());
  cJSON_AddNumberToObject(j, "format", 1);
  auto *files = cJSON_AddArrayToObject(j, "files");
  for (const auto &f : p.files) {
    auto *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "name", f.name.c_str());
    cJSON_AddStringToObject(v, "sha256", f.sha256.c_str());
    cJSON_AddNumberToObject(v, "size", f.size);
    cJSON_AddItemToArray(files, v);
  }
  auto *deps = cJSON_AddArrayToObject(j, "requires");
  for (const auto &dep : p.dependencies)
    cJSON_AddItemToArray(deps, cJSON_CreateString(dep.c_str()));
  return j;
}
std::string directory(const Package &p) {
  return base + "/packages/" + p.id + "/" + p.version;
}
bool leaves(const Package &p, const std::vector<Package> &installed,
            std::vector<const Package *> &out, unsigned depth = 0) {
  if (depth > 8)
    return false;
  if (p.type != "pack") {
    out.push_back(&p);
    return true;
  }
  for (const auto &dependency : p.dependencies) {
    auto child =
        std::find_if(installed.begin(), installed.end(), [&](const Package &v) {
          return dependency == v.id + "@" + v.version;
        });
    if (child == installed.end() || !leaves(*child, installed, out, depth + 1))
      return false;
  }
  return true;
}
void cleanup_transaction() {
  std::vector<Package> pending;
  std::string error;
  if (validate_catalog(read(base + "/staging/transaction.json"), pending,
                       error)) {
    for (const auto &p : pending) {
      const bool retained =
          std::any_of(current.installed.begin(), current.installed.end(),
                      [&](const Package &v) {
                        return p.id == v.id && p.version == v.version;
                      });
      for (const auto &f : p.files) {
        std::remove((directory(p) + "/" + f.name + ".part").c_str());
        if (!retained)
          std::remove((directory(p) + "/" + f.name).c_str());
      }
      if (!retained) {
        rmdir(directory(p).c_str());
        rmdir((base + "/packages/" + p.id).c_str());
      }
    }
  }
  std::remove((base + "/staging/transaction.json").c_str());
}
bool resolve_install(const State &before, const std::string &id,
                     std::vector<Package> &plan, std::string &error) {
  std::set<std::string> visiting, planned;
  std::map<std::string, std::string> versions;
  const auto resolve = [&](auto &&self, const std::string &key,
                           unsigned depth) -> bool {
    if (depth > 8 || visiting.count(key)) {
      error = "Cyclic or deep pack dependencies";
      return false;
    }
    if (planned.count(key))
      return true;
    auto at = key.find('@');
    const auto name = key.substr(0, at);
    auto p =
        std::find_if(before.available.begin(), before.available.end(),
                     [&](const Package &p) {
                       return p.id == name && (at == std::string::npos ||
                                               p.version == key.substr(at + 1));
                     });
    if (p == before.available.end()) {
      error = "Package/dependency unavailable; refresh catalog";
      return false;
    }
    if (versions.count(p->id) && versions[p->id] != p->version) {
      error = "Pack requires conflicting versions";
      return false;
    }
    versions[p->id] = p->version;
    visiting.insert(key);
    for (const auto &d : p->dependencies)
      if (!self(self, d, depth + 1))
        return false;
    visiting.erase(key);
    planned.insert(key);
    plan.push_back(*p);
    return true;
  };
  return resolve(resolve, id, 0);
}
uint64_t free_space() {
#ifdef ESP_PLATFORM
  uint64_t total = 0, free = 0;
  if (esp_vfs_fat_info("/spiffs", &total, &free) != ESP_OK)
    return 0;
  return free;
#elif defined(__EMSCRIPTEN__)
  return static_cast<uint64_t>(leaf_browser_content_free());
#else
  return 12 * 1024 * 1024;
#endif
}
// Two registry slots: a failed write never overwrites the current valid slot.
bool save() {
  Json j(cJSON_CreateObject(), cJSON_Delete);
  cJSON_AddNumberToObject(j.get(), "schema", 1);
  cJSON_AddNumberToObject(j.get(), "generation", generation + 1);
  cJSON_AddStringToObject(j.get(), "dictionary", current.dictionary.c_str());
  cJSON_AddStringToObject(j.get(), "font", current.font.c_str());
  cJSON_AddStringToObject(j.get(), "theme", current.theme.c_str());
  cJSON_AddStringToObject(j.get(), "layout", current.layout.c_str());
  auto *items = cJSON_AddArrayToObject(j.get(), "packages");
  for (const auto &p : current.installed) {
    auto *v = package_json(p);
    auto *a = cJSON_AddArrayToObject(v, "owners");
    for (const auto &o : owners[p.id])
      cJSON_AddItemToArray(a, cJSON_CreateString(o.c_str()));
    cJSON_AddItemToArray(items, v);
  }
  auto payload = dump(j.get());
  if (payload.empty() || payload.size() > MaxMetadata - 128)
    return false;
  Json envelope(cJSON_CreateObject(), cJSON_Delete);
  cJSON_AddStringToObject(envelope.get(), "payload", payload.c_str());
  cJSON_AddNumberToObject(envelope.get(), "crc32", crc(payload));
  const std::string path =
      base + "/registry/" + std::to_string((generation + 1) % 2) + ".json";
  const auto encoded = dump(envelope.get());
  if (encoded.empty() || !write(path, encoded)) {
    std::remove(path.c_str());
    return false;
  }
  ++generation;
  ++current.revision;
  return true;
}
void load_registry() {
  for (int slot = 0; slot < 2; ++slot) {
    auto envelope = parse(read(
        base + "/registry/" + std::to_string(slot) + ".json", MaxMetadata * 2));
    if (!envelope)
      continue;
    const auto payload = text(envelope.get(), "payload");
    auto *checksum = cJSON_GetObjectItemCaseSensitive(envelope.get(), "crc32");
    if (!cJSON_IsNumber(checksum) || checksum->valuedouble != crc(payload))
      continue;
    auto j = parse(payload);
    if (!j)
      continue;
    auto *g = cJSON_GetObjectItemCaseSensitive(j.get(), "generation");
    if (!cJSON_IsNumber(g) || g->valuedouble <= generation)
      continue;
    std::vector<Package> items;
    std::string error;
    if (!validate_catalog(payload, items, error))
      continue;
    generation = unsigned(g->valuedouble);
    current.installed = items;
    current.dictionary = text(j.get(), "dictionary");
    current.font = text(j.get(), "font");
    current.theme = text(j.get(), "theme");
    current.layout = text(j.get(), "layout");
    owners.clear();
    auto *a = cJSON_GetObjectItemCaseSensitive(j.get(), "packages");
    cJSON *v;
    cJSON_ArrayForEach(v, a) {
      auto *o = cJSON_GetObjectItemCaseSensitive(v, "owners");
      cJSON *x;
      cJSON_ArrayForEach(x, o) if (cJSON_IsString(x) && x->valuestring)
          owners[text(v, "id")]
              .insert(x->valuestring);
    }
  }
}
std::vector<unsigned char> dictionary;
std::vector<uint32_t> offsets;
uint32_t dawg_root = 0;
std::string language;
lv_font_t *font_loaded = nullptr;
std::string international_path;
std::map<int,lv_font_t*> international_cache;
std::string font_path;
int font_size = 0;
uint32_t layout_plain[49]{},layout_shifted[49]{};
bool layout_ready=false,layout_spanish=false;
Theme theme_loaded;
bool theme_ready = false;
bool validate_asset(const Package &p, const File &f) {
  const auto path = directory(p) + "/" + f.name;
  if (p.type == "dictionary") {
    FILE *in = fopen(path.c_str(), "rb");
    if (!in)
      return false;
    unsigned char h[12]{};
    size_t n = fread(h, 1, 12, in);
    fclose(in);
    if (n != 12)
      return false;
    if (!memcmp(h, "LDWG", 4)) {
      uint32_t root = uint32_t(h[4]) | uint32_t(h[5]) << 8 |
                      uint32_t(h[6]) << 16 | uint32_t(h[7]) << 24;
      return p.language == "en" && f.size >= 15 && (f.size - 12) % 3 == 0 &&
             root && root < (f.size - 12) / 3;
    }
    // LTXT + UTF-8, one normalized, sorted word per line; deeper checks on
    // activation.
    return !memcmp(h, "LTXT\n", 5);
  }
  if(p.type=="layout") {
    auto j=parse(read(path,4096));if(!j || !cJSON_IsObject(j.get()))return false;
    auto *format=cJSON_GetObjectItemCaseSensitive(j.get(),"format");auto *spanish=cJSON_GetObjectItemCaseSensitive(j.get(),"spanish");
    if(!cJSON_IsNumber(format)||format->valueint!=1||!cJSON_IsBool(spanish))return false;
    for(const char *key:{"plain","shifted"}){auto *a=cJSON_GetObjectItemCaseSensitive(j.get(),key);if(!cJSON_IsArray(a)||cJSON_GetArraySize(a)!=49)return false;cJSON *v;cJSON_ArrayForEach(v,a){if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||v->valueint<32||v->valueint>65535||(v->valueint>=0xd800&&v->valueint<=0xdfff))return false;}}
    return true;
  }
  if (p.type == "theme") {
    auto j = parse(read(path, 4096));
    if (!j || !cJSON_IsObject(j.get()))
      return false;
    cJSON *v;
    cJSON_ArrayForEach(v, j.get()) {
      if (!cJSON_IsString(v) || !v->valuestring ||
          strlen(v->valuestring) != 7 || v->valuestring[0] != '#')
        return false;
      for (int i = 1; i < 7; ++i)
        if (!isxdigit(static_cast<unsigned char>(v->valuestring[i])))
          return false;
    }
    return true;
  }
  if (p.type == "font" || p.type == "glyphs") {
    return valid_font(read(path, 128 * 1024), unsigned(atoi(f.name.c_str())));
  }
  return false;
}
void load_dictionary(const Package &p) {
  language.clear();
  dawg_root = 0;
  dictionary.clear();
  offsets.clear();
  if (p.files.size() != 1)
    return;
  const auto bytes = read(directory(p) + "/" + p.files[0].name, 1024 * 1024);
  if (bytes.size() != p.files[0].size || !validate_asset(p, p.files[0]))
    return;
  dictionary.assign(bytes.begin(), bytes.end());
  if (!memcmp(dictionary.data(), "LDWG", 4)) {
    dawg_root = uint32_t(dictionary[4]) | uint32_t(dictionary[5]) << 8 |
                uint32_t(dictionary[6]) << 16 | uint32_t(dictionary[7]) << 24;
    const auto count = (dictionary.size() - 12) / 3;
    for (size_t n = 1; n < count; ++n) {
      const size_t at = 12 + n * 3;
      const uint32_t arc = dictionary[at] | uint32_t(dictionary[at + 1]) << 8 |
                           uint32_t(dictionary[at + 2]) << 16;
      if ((arc & 0x1ffff) >= count || ((arc >> 17) & 31) < 1 ||
          ((arc >> 17) & 31) > 27) {
        dictionary.clear();
        dawg_root = 0;
        return;
      }
    }
  } else {
    std::string previous;
    size_t start = 5;
    for (size_t i = 5; i < dictionary.size(); ++i)
      if (dictionary[i] == '\n') {
        if (i == start || i - start > 96 || offsets.size() >= 100000) {
          dictionary.clear();
          offsets.clear();
          return;
        }
        std::string word(reinterpret_cast<char *>(dictionary.data() + start),
                         i - start);
        if (spelling::normalize_utf8(word) != word ||
            word.find('\0') != std::string::npos ||
            (!previous.empty() && word <= previous)) {
          dictionary.clear();
          offsets.clear();
          return;
        }
        offsets.push_back(start);
        previous = word;
        dictionary[i] = 0;
        start = i + 1;
      }
    if (start != dictionary.size() || offsets.empty()) {
      dictionary.clear();
      offsets.clear();
      return;
    }
  }
  language = p.language;
}
} // namespace
const char *root() { return base.c_str(); }
bool validate_catalog(const std::string &json, std::vector<Package> &out,
                      std::string &error) {
  out.clear();
  auto j = parse(json);
  if (!j || json.size() > MaxMetadata) {
    error = "Invalid or oversized catalog";
    return false;
  }
  auto *schema = cJSON_GetObjectItemCaseSensitive(j.get(), "schema");
  auto *a = cJSON_GetObjectItemCaseSensitive(j.get(), "packages");
  if (!cJSON_IsNumber(schema) || schema->valueint != 1 || !cJSON_IsArray(a) ||
      cJSON_GetArraySize(a) > 64) {
    error = "Unsupported catalog schema";
    return false;
  }
  std::set<std::string> ids;
  cJSON *v;
  cJSON_ArrayForEach(v, a) {
    Package p;
    p.id = text(v, "id");
    p.version = text(v, "version");
    p.type = text(v, "type");
    p.name = text(v, "name");
    p.language = text(v, "language");
    p.description = text(v, "description");
    p.preview = text(v, "preview");
    if (p.preview.size() > 4096 || (!p.preview.empty() && p.type != "theme")) {
      error = "Invalid theme preview"; return false;
    }
    auto *format = cJSON_GetObjectItemCaseSensitive(v, "format");
    if (!identifier(p.id) || !identifier(p.version) ||
        !ids.insert(p.id).second || p.name.empty() || p.name.size() > 80 ||
        p.description.size() > 256 || !cJSON_IsNumber(format) ||
        format->valueint != 1 ||
        (p.type != "dictionary" && p.type != "font" && p.type != "theme" &&
         p.type != "pack" && p.type != "layout" && p.type != "glyphs")) {
      error = "Invalid or incompatible package";
      return false;
    }
    if (p.type == "dictionary" && p.language != "en" && p.language != "es" &&
        p.language != "ru") {
      error = "Unsupported dictionary language";
      return false;
    }
    auto *files = cJSON_GetObjectItemCaseSensitive(v, "files");
    auto *deps = cJSON_GetObjectItemCaseSensitive(v, "requires");
    if (!cJSON_IsArray(files) || cJSON_GetArraySize(files) > 10 ||
        !cJSON_IsArray(deps) || cJSON_GetArraySize(deps) > 16) {
      error = "Invalid package file list";
      return false;
    }
    std::set<std::string> names;
    cJSON *f;
    cJSON_ArrayForEach(f, files) {
      File entry;
      entry.name = text(f, "name");
      entry.sha256 = text(f, "sha256");
      auto *size = cJSON_GetObjectItemCaseSensitive(f, "size");
      const size_t max = p.type == "dictionary" ? 1024 * 1024
                         : (p.type == "font" || p.type == "glyphs") ? 128 * 1024
                                                : 4096;
      if (!identifier(entry.name) || !names.insert(entry.name).second ||
          entry.sha256.size() != 64 ||
          !std::all_of(entry.sha256.begin(), entry.sha256.end(),
                       [](unsigned char c) {
                         return (c >= '0' && c <= '9') ||
                                (c >= 'a' && c <= 'f');
                       }) ||
          !cJSON_IsNumber(size) || size->valuedouble < 1 ||
          size->valuedouble > max || size->valuedouble != size->valueint) {
        error = "Invalid package file";
        return false;
      }
      entry.size = size->valueint;
      p.files.push_back(entry);
    }
    cJSON_ArrayForEach(f, deps) {
      if (!cJSON_IsString(f) || !f->valuestring) {
        error = "Invalid dependency";
        return false;
      }
      std::string d = f->valuestring;
      auto at = d.find('@');
      if (at == std::string::npos || !identifier(d.substr(0, at)) ||
          !identifier(d.substr(at + 1))) {
        error = "Dependencies must pin id@version";
        return false;
      }
      p.dependencies.push_back(d);
    }
    if ((p.type == "pack" && (!p.files.empty() || p.dependencies.empty())) ||
        (p.type != "pack" && (p.files.empty() || (p.type != "layout" && !p.dependencies.empty()))) ||
        ((p.type == "dictionary" || p.type == "theme" || p.type == "layout") &&
         p.files.size() != 1)) {
      error = "Invalid package payload";
      return false;
    }
    if (p.type == "font" || p.type == "glyphs")
      for (const auto &file : p.files) {
        const int size = atoi(file.name.c_str());
        if (size < 14 || size > 40 ||
            file.name != std::to_string(size) + ".bin") {
          error = "Font filenames must be <size>.bin (14..40)";
          return false;
        }
      }
    if (p.type == "glyphs") {
      const std::set<std::string> required = {"14.bin", "16.bin", "18.bin", "20.bin", "22.bin", "24.bin", "26.bin", "30.bin"};
      const std::set<std::string> reduced = {"14.bin", "16.bin", "18.bin", "20.bin", "24.bin", "28.bin"};
      if ((p.id != "org.justtype.glyphs.international" && p.id != "org.justtype.glyphs.international.v2") || (names != required && names != reduced)) {
        error = "Incomplete international character support";
        return false;
      }
    }
    out.push_back(p);
  }
  return true;
}
void init() {
  for (const auto &p : {base, base + "/packages", base + "/staging",
                        base + "/registry", base + "/catalog"})
    mkdir(p.c_str(), 0755);
  std::lock_guard<std::mutex> lock(guard);
  load_registry();
  cleanup_transaction();
  // Optional first-boot seed supplied by the provisioning tool, never firmware.
  if (!generation) {
    std::vector<Package> seed;
    std::string error;
    if (validate_catalog(read(base + "/seed.json"), seed, error)) {
      bool valid = true;
      for (const auto &p : seed) {
        for (const auto &f : p.files)
          valid = valid && validate_asset(p, f);
        owners[p.id].insert("user");
      }
      if (valid) {
        current.installed = seed;
        for (const auto &p : seed)
          if (p.type == "pack") {
            std::vector<const Package *> items;
            if (!leaves(p, seed, items)) {
              valid = false;
              break;
            }
            for (const auto *item : items)
              owners[item->id].insert(p.id);
          }
        if (!valid) {
          current.installed.clear();
          owners.clear();
        }
      }
      if (valid) {
        for (const auto &p : seed)
          if (p.type == "dictionary") {
            current.dictionary = p.id;
            break;
          }
        save();
      }
    }
  }
  std::remove((base + "/catalog/catalog.part").c_str());
  std::string error;
  validate_catalog(read(base + "/catalog/catalog.json"), current.available,
                   error);
  current.message = "Installed content works offline";
  const bool old_international_layout =
      std::any_of(current.installed.begin(), current.installed.end(), [](const Package &p) {
        return (p.id == "org.justtype.layout.spanish" || p.id == "org.justtype.layout.russian") && p.dependencies.empty();
      });
  if (old_international_layout)
    current.message = "Update Spanish/Russian layouts to install character support";
  current.free_bytes = free_space();
}
unsigned revision() {
  std::lock_guard<std::mutex> lock(guard);
  return activated;
}
bool busy() {
  std::lock_guard<std::mutex> lock(guard);
  return current.busy;
}
State state() {
  std::lock_guard<std::mutex> lock(guard);
  return current;
}
void tick() {
  State s;
  {
    std::lock_guard<std::mutex> lock(guard);
    if (current.busy || activated == current.revision)
      return;
    s = current;
  }
  activated = s.revision;
  std::string new_international_path;
  for(const auto &p:s.installed)if(p.type=="glyphs" && p.id=="org.justtype.glyphs.international")new_international_path=directory(p);
  for(const auto &p:s.installed)if(p.type=="glyphs" && p.id=="org.justtype.glyphs.international.v2")new_international_path=directory(p);
  if(international_path!=new_international_path){
    reset_ui_font_fallbacks();
    for(auto &entry:international_cache)if(entry.second)lv_binfont_destroy(entry.second);
    international_cache.clear();international_path=new_international_path;
  }

  dictionary.clear();
  dictionary.shrink_to_fit();
  offsets.clear();
  offsets.shrink_to_fit();
  language.clear();
  dawg_root = 0;
  theme_ready = false;layout_ready=false;layout_spanish=false;
  for (const auto &p : s.installed) {
    if(p.id==s.layout && p.type=="layout" && p.files.size()==1 && validate_asset(p,p.files[0])){
      auto j=parse(read(directory(p)+"/"+p.files[0].name,4096));
      if(j){auto *a=cJSON_GetObjectItemCaseSensitive(j.get(),"plain");auto *b=cJSON_GetObjectItemCaseSensitive(j.get(),"shifted");
        for(int i=0;i<49;++i){layout_plain[i]=cJSON_GetArrayItem(a,i)->valueint;layout_shifted[i]=cJSON_GetArrayItem(b,i)->valueint;}
        layout_spanish=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j.get(),"spanish"));layout_ready=true;}
    }
    if (p.id == s.dictionary && p.type == "dictionary")
      load_dictionary(p);
    if (p.id == s.theme && p.type == "theme") {
      auto j = parse(read(directory(p) + "/" + p.files[0].name, 4096));
      if (!j || !validate_asset(p, p.files[0]))
        continue;
      theme_loaded = get_dark_theme();
#define COLOR(field)                                                           \
  {                                                                            \
    auto value = text(j.get(), #field);                                        \
    if (!value.empty())                                                        \
      theme_loaded.field =                                                     \
          lv_color_hex(strtoul(value.c_str() + 1, nullptr, 16));               \
  }
      COLOR(background)
      COLOR(text)
      COLOR(text_dim)
      COLOR(edit_bg)
      COLOR(edit_text) COLOR(cursor) COLOR(selection) COLOR(sidebar_bg)
          COLOR(sidebar_btn_normal) COLOR(sidebar_btn_hover)
              COLOR(sidebar_btn_active) COLOR(sidebar_btn_text)
                  COLOR(sidebar_btn_text_hover) COLOR(sidebar_selected)
                      COLOR(sidebar_new_file) COLOR(sidebar_delete)
                          COLOR(sidebar_cancel) COLOR(battery_border)
                              COLOR(battery_good) COLOR(battery_low)
                                  COLOR(scrollbar_bg) COLOR(scrollbar_cursor)
                                      COLOR(search_input_bg)
                                          COLOR(search_input_text)
                                              COLOR(search_highlight)
#undef COLOR
                                                  theme_ready = true;
    }
  }
}
bool dictionary_available() { return !language.empty(); }
std::string dictionary_language() { return language; }
uint32_t dictionary_root() { return dawg_root; }
bool dictionary_arc(uint32_t node, uint32_t &arc) {
  size_t at = 12 + size_t(node) * 3;
  if (!dawg_root || !node || at + 2 >= dictionary.size())
    return false;
  arc = dictionary[at] | uint32_t(dictionary[at + 1]) << 8 |
        uint32_t(dictionary[at + 2]) << 16;
  return true;
}
size_t dictionary_words() { return offsets.size(); }
std::string dictionary_word(size_t n) {
  return n < offsets.size()
             ? reinterpret_cast<char *>(dictionary.data() + offsets[n])
             : "";
}
bool contains(const std::string &word) {
  if (!dictionary_available())
    return false;
  if (!dawg_root)
    return std::binary_search(
        offsets.begin(), offsets.end(), word, [](const auto &a, const auto &b) {
          // Heterogeneous binary_search needs both comparison directions.
          if constexpr (std::is_integral_v<std::decay_t<decltype(a)>>)
            return std::string_view(reinterpret_cast<const char *>(
                       dictionary.data() + a)) < b;
          else
            return a < std::string_view(reinterpret_cast<const char *>(
                           dictionary.data() + b));
        });
  uint32_t node = dawg_root;
  for (size_t i = 0; i < word.size(); ++i) {
    unsigned label = word[i] == '\'' ? 27 : unsigned(word[i] - 'a' + 1);
    if (label < 1 || label > 27)
      return false;
    uint32_t arc = 0;
    unsigned budget = 28;
    while (budget--) {
      if (!dictionary_arc(node, arc))
        return false;
      if (((arc >> 17) & 31) == label)
        break;
      if (arc & (1u << 23))
        return false;
      ++node;
    }
    if (((arc >> 17) & 31) != label)
      return false;
    if (i + 1 == word.size())
      return arc & (1u << 22);
    node = arc & 0x1ffff;
  }
  return false;
}
const lv_font_t *international_font(int size) {
  if(international_path.empty())return nullptr;
  auto found=international_cache.find(size);if(found!=international_cache.end())return found->second;
  const std::string path=international_path+"/"+std::to_string(size)+".bin";
  auto *loaded=lv_binfont_create(("A:"+path).c_str());
  if(!loaded && size==28)loaded=lv_binfont_create(("A:"+international_path+"/26.bin").c_str());
  international_cache[size]=loaded;return loaded;
}
const lv_font_t *editor_font(int size, const lv_font_t *fallback) {
  const auto s = state();
  std::string path;
  for (const auto &p : s.installed)
    if (p.id == s.font && p.type == "font")
      for (const auto &f : p.files)
        if (f.name == std::to_string(size) + ".bin")
          path = directory(p) + "/" + f.name;
  if (path != font_path || size != font_size) {
    if (font_loaded)
      lv_binfont_destroy(font_loaded);
    font_loaded = nullptr;
    font_path = path;
    font_size = size;
    if (!path.empty())
      font_loaded = lv_binfont_create(("A:" + path).c_str());
  }
  if (font_loaded) {
    font_loaded->fallback = fallback;
    return font_loaded;
  }
  return fallback;
}
std::vector<LayoutChoice> layout_choices(){std::vector<LayoutChoice> choices={{"US QWERTY",""}};for(const auto&p:state().installed)if(p.type=="layout")choices.push_back({p.name,p.id});return choices;}
bool layout_key(size_t index,bool shifted,uint32_t &value,bool &spanish){if(!layout_ready||index>=49)return false;value=shifted?layout_shifted[index]:layout_plain[index];spanish=layout_spanish;return true;}
std::vector<ThemeChoice> theme_choices() {
  std::vector<ThemeChoice> choices={{"Light","",0},{"Dark","",1}};
  for(const auto &p:state().installed)if(p.type=="theme"){
    auto name=p.name;for(auto &c:name)if(c=='\n'||c=='\r')c=' ';
    choices.push_back({name,p.id,-1});
  }
  return choices;
}
int active_theme_index(int builtin) {
  const auto id=state().theme;
  const auto choices=theme_choices();
  if(!id.empty())for(size_t n=0;n<choices.size();++n)if(choices[n].id==id)return int(n);
  return std::clamp(builtin,0,1);
}
bool select_theme(int index,int &builtin) {
  const auto choices=theme_choices();
  if(index<0 || index>=int(choices.size()))return false;
  const auto &choice=choices[index];
  if(choice.builtin<0)return enable(choice.id);
  if(!disable(state().theme))return false;
  builtin=choice.builtin;return true;
}
bool selected_theme(Theme &t) {
  if (!theme_ready)
    return false;
  t = theme_loaded;
  return true;
}
bool preview_theme(const std::string &id, Theme &t) {
  const auto s=state();
  const bool catalog=id.rfind("download:",0)==0;
  const auto key=catalog ? id.substr(9) : id;
  const auto it=std::find_if(s.installed.begin(),s.installed.end(),[&](const Package &p){return p.id==key && p.type=="theme";});
  std::string data;
  if(!catalog && it!=s.installed.end() && it->files.size()==1 && validate_asset(*it,it->files[0]))data=read(directory(*it)+"/"+it->files[0].name,4096);
  else {
    const auto available=std::find_if(s.available.begin(),s.available.end(),[&](const Package &p){return p.id==key && p.type=="theme";});
    if(available!=s.available.end())data=available->preview;
  }
  auto j=parse(data);
  if(!j || !cJSON_IsObject(j.get()))return false;
  for(auto *item=j->child;item;item=item->next) {
    if(!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring)!=7 || item->valuestring[0]!='#' || !std::all_of(item->valuestring+1,item->valuestring+7,[](unsigned char c){return std::isxdigit(c);}))return false;
  }
  t=get_dark_theme();
#define PREVIEW_COLOR(field) {auto value=text(j.get(),#field);if(!value.empty())t.field=lv_color_hex(strtoul(value.c_str()+1,nullptr,16));}
  PREVIEW_COLOR(background) PREVIEW_COLOR(text) PREVIEW_COLOR(text_dim)
  PREVIEW_COLOR(edit_bg) PREVIEW_COLOR(edit_text) PREVIEW_COLOR(cursor) PREVIEW_COLOR(selection)
  PREVIEW_COLOR(sidebar_bg) PREVIEW_COLOR(sidebar_btn_normal) PREVIEW_COLOR(sidebar_btn_hover)
  PREVIEW_COLOR(sidebar_btn_active) PREVIEW_COLOR(sidebar_btn_text) PREVIEW_COLOR(sidebar_btn_text_hover)
  PREVIEW_COLOR(sidebar_selected) PREVIEW_COLOR(sidebar_new_file) PREVIEW_COLOR(sidebar_delete)
  PREVIEW_COLOR(sidebar_cancel) PREVIEW_COLOR(battery_border) PREVIEW_COLOR(battery_good)
  PREVIEW_COLOR(battery_low) PREVIEW_COLOR(scrollbar_bg) PREVIEW_COLOR(scrollbar_cursor)
  PREVIEW_COLOR(search_input_bg) PREVIEW_COLOR(search_input_text) PREVIEW_COLOR(search_highlight)
#undef PREVIEW_COLOR
  return true;
}
bool enable(const std::string &id) {
  std::lock_guard<std::mutex> lock(guard);
  if (current.busy)
    return false;
  auto old = current;
  auto found = std::find_if(current.installed.begin(), current.installed.end(),
                            [&](const Package &p) { return p.id == id; });
  if (found == current.installed.end()) {
    current.message = "Install this item before enabling it";
    return false;
  }
  const auto apply = [](const Package &p) {
    if (p.type == "dictionary")
      current.dictionary = p.id;
    else if (p.type == "font")
      current.font = p.id;
    else if (p.type == "theme")
      current.theme = p.id;
    else if (p.type == "layout")
      current.layout = p.id;
  };
  std::vector<const Package *> items;
  if (!leaves(*found, current.installed, items)) {
    current.message = "Pack dependencies are missing; reinstall the pack";
    return false;
  }
  for (const auto *p : items)
    apply(*p);
  if (!save()) {
    current = old;
    current.message = "Could not save content settings";
    return false;
  }
  return true;
}
bool disable(const std::string &id) {
  std::lock_guard<std::mutex> lock(guard);
  if (current.busy)
    return false;
  if (id.empty())
    return true;
  auto old = current;
  for (auto *value : {&current.dictionary, &current.font, &current.theme, &current.layout})
    if (*value == id)
      value->clear();
  for (const auto &pack : current.installed)
    if (pack.id == id && pack.type == "pack") {
      std::vector<const Package *> items;
      leaves(pack, current.installed, items);
      for (const auto *p : items)
        for (auto *value : {&current.dictionary, &current.font, &current.theme, &current.layout})
          if (*value == p->id)
            value->clear();
    }
  if (old.dictionary == current.dictionary && old.font == current.font &&
      old.theme == current.theme && old.layout == current.layout)
    return true;
  if (!save()) {
    current = old;
    return false;
  }
  return true;
}
void queue(Operation op, const std::string &id) {
  {
    std::lock_guard<std::mutex> lock(guard);
    if (current.busy)
      return;
    operation = op;
    requested = id;
    abort_job = false;
    current.busy = true;
    current.percent = -1;
    current.message = "Queued content operation";
  }
  backup::request_content();
}
void refresh() { queue(Operation::Catalog, ""); }
void install(const std::string &id) { queue(Operation::Install, id); }
bool remove(const std::string &id) {
  if (state().busy)
    return false;
  queue(Operation::Remove, id);
  return true;
}
void cancel() { abort_job = true; }
bool cancelled() { return abort_job.load(); }
bool needs_network() {
  std::lock_guard<std::mutex> lock(guard);
  return operation != Operation::Remove;
}
void download_progress(int n, const std::string &message) {
  std::lock_guard<std::mutex> lock(guard);
  current.percent = n;
  if (!message.empty())
    current.message = message;
}
void network_error(const std::string &error) {
  std::lock_guard<std::mutex> lock(guard);
  current.busy = false;
  operation = Operation::None;
  current.message = error;
  current.last_success = false;
  ++current.result_generation;
}
bool download_plan(std::vector<DownloadRequest> &requests, std::string &error) {
  std::lock_guard<std::mutex> lock(guard);
  requests.clear();
  if (operation == Operation::Catalog) {
    requests.push_back({"catalog", "", MaxMetadata});
    return true;
  }
  if (operation != Operation::Install)
    return true;
  std::vector<Package> plan;
  if (!resolve_install(current, requested, plan, error))
    return false;
  uint64_t size = 0;
  for (const auto &p : plan) {
    const bool present =
        std::any_of(current.installed.begin(), current.installed.end(),
                    [&](const Package &v) {
                      return p.id == v.id && p.version == v.version;
                    });
    if (present)
      continue;
    for (const auto &f : p.files) {
      requests.push_back(
          {"files/" + p.id + "/" + p.version + "/" + f.name, f.sha256, f.size});
      size += f.size;
    }
  }
  if (size && free_space() < size + Reserve) {
    error = "Not enough space; 1 MiB is reserved for documents";
    return false;
  }
  return true;
}
bool process(Download download, bool files_already_staged) {
  Operation op;
  std::string id;
  State before;
  std::map<std::string, std::set<std::string>> before_owners;
  {
    std::lock_guard<std::mutex> lock(guard);
    op = operation;
    id = requested;
    before = current;
    before_owners = owners;
  }
  std::string error;
  {
    std::lock_guard<std::mutex> lock(guard);
    current.message = op == Operation::Catalog  ? "Refreshing catalog"
                      : op == Operation::Remove ? "Removing content"
                                                : "Installing content";
  }
  bool ok = false;
  if (op == Operation::Catalog) {
    const auto path = base + "/catalog/catalog.part";
    if (download("catalog", path, MaxMetadata, "")) {
      std::vector<Package> catalog;
      if (validate_catalog(read(path), catalog, error)) {
        // Cache replacement can fail without damaging the installed registry.
        std::remove((base + "/catalog/catalog.json").c_str());
        ok = std::rename(path.c_str(),
                         (base + "/catalog/catalog.json").c_str()) == 0;
        if (ok) {
          std::lock_guard<std::mutex> lock(guard);
          current.available = std::move(catalog);
          ++current.revision;
        }
      }
    }
  } else if (op == Operation::Install) {
    std::vector<Package> plan;
    ok = resolve_install(before, id, plan, error);
    if (ok) {
      auto proposed = before.installed;
      for (const auto &p : plan) {
        auto old = std::find_if(proposed.begin(), proposed.end(),
                                [&](const Package &v) { return v.id == p.id; });
        if (old != proposed.end() && old->version != p.version) {
          for (const auto &owner : before_owners[p.id])
            if (owner != "user" && owner != id) {
              ok = false;
              error = "Another pack pins this version; remove it first";
            }
        }
        if (old == proposed.end())
          proposed.push_back(p);
        else
          *old = p;
      }
      if (proposed.size() > 64) {
        ok = false;
        error = "Installed package limit reached";
      }
    }
    if (ok) {
      Json journal(cJSON_CreateObject(), cJSON_Delete);
      cJSON_AddNumberToObject(journal.get(), "schema", 1);
      auto *items = cJSON_AddArrayToObject(journal.get(), "packages");
      for (const auto &p : plan)
        cJSON_AddItemToArray(items, package_json(p));
      ok = write(base + "/staging/transaction.json", dump(journal.get()));
    }
    uint64_t bytes = 0;
    for (const auto &p : plan) {
      const bool present =
          std::any_of(before.installed.begin(), before.installed.end(),
                      [&](const Package &v) {
                        return v.id == p.id && v.version == p.version;
                      });
      if (!present)
        for (const auto &f : p.files)
          bytes += f.size;
    }
    if (ok && bytes &&
        free_space() < (files_already_staged ? 0 : bytes) + Reserve) {
      ok = false;
      error = "Not enough space; 1 MiB is reserved for documents";
    }
    for (const auto &p : plan) {
      if (!ok || cancelled()) {
        ok = false;
        break;
      }
      auto old = std::find_if(before.installed.begin(), before.installed.end(),
                              [&](const Package &v) { return v.id == p.id; });
      if (old != before.installed.end() && old->version == p.version) {
        Json a(package_json(*old), cJSON_Delete),
            b(package_json(p), cJSON_Delete);
        if (dump(a.get()) != dump(b.get())) {
          ok = false;
          error = "Published package version changed; publish a new version";
        }
        continue;
      }
      mkdir((base + "/packages/" + p.id).c_str(), 0755);
      mkdir(directory(p).c_str(), 0755);
      for (const auto &f : p.files) {
        {
          std::lock_guard<std::mutex> lock(guard);
          current.message = "Downloading " + p.name + " / " + f.name;
        }
        const auto path = directory(p) + "/" + f.name;
        const auto part = path + ".part";
        if (free_space() <
                (files_already_staged ? 0 : uint64_t(f.size)) + Reserve ||
            !download("files/" + p.id + "/" + p.version + "/" + f.name, part,
                      f.size, f.sha256)) {
          ok = false;
          break;
        }
        std::remove(path.c_str());
        if (std::rename(part.c_str(), path.c_str()) != 0 ||
            !validate_asset(p, f)) {
          ok = false;
          error = "Invalid package asset";
          break;
        }
      }
    }
    if (ok && cancelled()) {
      ok = false;
      error = "Cancelled";
    }
    if (ok) {
      std::lock_guard<std::mutex> lock(guard);
      // Replace pack ownership as a unit, then add the pinned new set.
      for (auto &pair : owners)
        pair.second.erase(id);
      for (const auto &p : plan) {
        auto old =
            std::find_if(current.installed.begin(), current.installed.end(),
                         [&](const Package &v) { return v.id == p.id; });
        if (old == current.installed.end())
          current.installed.push_back(p);
        else
          *old = p;
        owners[p.id].insert(p.id == id ? "user" : id);
      }
      current.installed.erase(std::remove_if(current.installed.begin(),
                                             current.installed.end(),
                                             [&](const Package &p) {
                                               return owners[p.id].empty();
                                             }),
                              current.installed.end());
      for (auto *active : {&current.dictionary, &current.font, &current.theme, &current.layout})
        if (!active->empty() &&
            std::none_of(current.installed.begin(), current.installed.end(),
                         [&](const Package &p) { return p.id == *active; }))
          active->clear();
      // Activate only the item the user installed, not its supporting assets
      // or the members of a pack. Commit selection with the same registry.
      const auto target=std::find_if(current.installed.begin(),current.installed.end(),[&](const Package &p){return p.id==id;});
      if(target!=current.installed.end()) {
        if(target->type=="dictionary")current.dictionary=id;
        else if(target->type=="font")current.font=id;
        else if(target->type=="theme")current.theme=id;
        else if(target->type=="layout")current.layout=id;
      }
      if (ok)
        ok = save();
    }
  } else if (op == Operation::Remove) {
    std::lock_guard<std::mutex> lock(guard);
    if (owners[id].size() > 1 ||
        (owners[id].size() == 1 && !owners[id].count("user")))
      error = "Required by a pack; remove the pack first";
    else {
      owners[id].clear();
      for (auto &pair : owners)
        pair.second.erase(id);
      current.installed.erase(std::remove_if(current.installed.begin(),
                                             current.installed.end(),
                                             [&](const Package &p) {
                                               return owners[p.id].empty();
                                             }),
                              current.installed.end());
      for (auto *active : {&current.dictionary, &current.font, &current.theme, &current.layout})
        if (!active->empty() &&
            std::none_of(current.installed.begin(), current.installed.end(),
                         [&](const Package &p) { return p.id == *active; }))
          active->clear();
      ok = save();
    }
  }
  {
    std::lock_guard<std::mutex> lock(guard);
    if (!ok && op != Operation::Catalog) {
      current = before;
      owners = before_owners;
    }
    if (ok && op != Operation::Catalog) {
      // Only remove known package paths after the registry commit. No recursive
      // deletion.
      for (const auto &p : before.installed) {
        bool retained =
            std::any_of(current.installed.begin(), current.installed.end(),
                        [&](const Package &v) {
                          return v.id == p.id && v.version == p.version;
                        });
        if (!retained) {
          for (const auto &f : p.files)
            std::remove((directory(p) + "/" + f.name).c_str());
          rmdir(directory(p).c_str());
          rmdir((base + "/packages/" + p.id).c_str());
        }
      }
    }
    if (op == Operation::Install)
      cleanup_transaction();
    current.busy = false;
    operation = Operation::None;
    current.last_success = ok;
    ++current.result_generation;
    current.percent = ok ? 100 : -1;
    current.free_bytes = free_space();
    current.message =
        ok            ? "Content operation complete"
        : cancelled() ? "Cancelled; installed content retained"
        : error.empty()
            ? "Download failed; installed content retained. Retry when online."
            : error;
  }
  return ok;
}
} // namespace content
