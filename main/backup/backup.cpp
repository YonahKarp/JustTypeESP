#include "backup.h"
#include "backup_client_config.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_flash_encrypt.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "file_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "justtype/src/editor.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "policy.h"
#include "storage.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace backup {
namespace {
using namespace leaf_backup;
constexpr uint32_t Manual = 1, Link = 2, Forget = 4, Network = 8,
                   Automatic = 16, Verify = 32;
constexpr int64_t PeriodUs = 30LL * 60 * 1000000;
constexpr const char *Scope = "https://www.googleapis.com/auth/drive.file";
constexpr const char *Api = "https://www.googleapis.com/drive/v3/files";
constexpr const char *Upload =
    "https://www.googleapis.com/upload/drive/v3/files";
std::mutex mutex;
Status shared;
Progress shared_progress;
WifiCredentials configured_wifi;
std::string pending_ssid, pending_password;
std::string client_id = LEAF_GOOGLE_CLIENT_ID,
            client_secret = LEAF_GOOGLE_CLIENT_SECRET;
bool pending_auto = false;
TaskHandle_t task = nullptr;
EventGroupHandle_t wifi_events = nullptr;
std::atomic<uint32_t> commands{0};
std::atomic<bool> manual_pending{false}, verify_pending{false};
std::atomic<bool> stopping{false}, worker_stopped{false};
std::atomic<int64_t> last_input{0};
std::string ssid, password, refresh_token, access_token, device;
int64_t access_expires = 0, radio_deadline = 0;
bool secure = false, automatic = false, network_ready = false,
     clock_ready = false;
bool wifi_initialized = false, wifi_callback = false, ip_callback = false;
esp_netif_t *wifi_interface = nullptr;
std::atomic<unsigned> wifi_disconnect_reason{0};
bool terminal = false;
uint32_t server_retry = 0;
std::string error;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const std::string &text) {
  return Json(cJSON_ParseWithLength(text.data(), text.size()), cJSON_Delete);
}
Json object() { return Json(cJSON_CreateObject(), cJSON_Delete); }
std::string field(cJSON *j, const char *name) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsString(v) && v->valuestring ? std::string(v->valuestring)
                                             : std::string();
}
int number(cJSON *j, const char *name, int fallback) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsNumber(v) ? v->valueint : fallback;
}
std::string dump(cJSON *j) {
  char *text = cJSON_PrintUnformatted(j);
  if (!text)
    return {};
  std::string result = text;
  cJSON_free(text);
  return result;
}
void message(const std::string &value) {
  std::lock_guard<std::mutex> guard(mutex);
  shared.message = value;
}
bool fail(const std::string &value, bool fatal = false) {
  error = value;
  terminal = fatal;
  return false;
}
bool cancelled() {
  return stopping.load() || (commands.load() & (Forget | Network | Link)) != 0;
}
void sleep_ms(uint32_t ms) {
  for (uint32_t elapsed = 0; elapsed < ms && !cancelled(); elapsed += 100)
    vTaskDelay(pdMS_TO_TICKS(std::min<uint32_t>(100, ms - elapsed)));
}
void send(uint32_t command) {
  commands.fetch_or(command);
  if (task)
    xTaskNotifyGive(task);
}
std::string hash(const void *bytes, size_t size) {
  unsigned char output[32];
  if (mbedtls_sha256(static_cast<const unsigned char *>(bytes), size, output,
                     0) != 0)
    return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (auto c : output) {
    result += hex[c >> 4];
    result += hex[c & 15];
  }
  return result;
}
std::string hash(const std::string &value) {
  return hash(value.data(), value.size());
}
std::string read_secret(nvs_handle_t handle, const char *key) {
  size_t size = 0;
  if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || !size ||
      size > 4096)
    return {};
  std::vector<char> buffer(size);
  if (nvs_get_str(handle, key, buffer.data(), &size) != ESP_OK)
    return {};
  return buffer.data();
}
bool save_credentials() {
  // Prototype credentials persist in ordinary NVS. The same API uses encrypted
  // NVS when production storage is provisioned; encryption is not a save gate.
  nvs_handle_t handle;
  if (nvs_open("leaf_backup", NVS_READWRITE, &handle) != ESP_OK)
    return false;
  esp_err_t result = nvs_set_str(handle, "ssid", ssid.c_str());
  if (result == ESP_OK)
    result = nvs_set_str(handle, "wifi_key", password.c_str());
  if (result == ESP_OK)
    result = refresh_token.empty()
                 ? nvs_erase_key(handle, "refresh")
                 : nvs_set_str(handle, "refresh", refresh_token.c_str());
  if (result == ESP_ERR_NVS_NOT_FOUND)
    result = ESP_OK;
  if (result == ESP_OK)
    result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
void save_auto() {
  nvs_handle_t h;
  if (nvs_open("leaf_backup", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, "auto", automatic);
    nvs_commit(h);
    nvs_close(h);
  }
}
HttpTransport transport;
unsigned http_requests = 0, tls_connections = 0, completed_documents = 0;
unsigned uploaded_documents = 0, hashed_documents = 0;
bool followup_needed = false;
void report_progress(ProgressPhase phase, unsigned completed, unsigned total,
                     unsigned part_percent = 0) {
  std::lock_guard<std::mutex> guard(mutex);
  shared_progress.phase = phase;
  shared_progress.completed = completed;
  shared_progress.total = total;
  shared_progress.percent =
      total ? std::min<unsigned>(99, (completed * 100 + part_percent) / total)
            : 0;
}
void upload_progress(void *, size_t sent, size_t total) {
  std::lock_guard<std::mutex> guard(mutex);
  const unsigned part = total ? unsigned(uint64_t(sent) * 90 / total) : 0;
  shared_progress.phase = shared_progress.verification
                              ? ProgressPhase::Verifying
                              : ProgressPhase::Uploading;
  const int percent =
      shared_progress.total
          ? std::min<unsigned>(99, (completed_documents * 100 + part) /
                                       shared_progress.total)
          : 0;
  shared_progress.percent = std::max(shared_progress.percent, percent);
}
bool budget_exhausted = false;
Response http(const std::string &url, esp_http_client_method_t method,
              const std::vector<View> &parts, const char *content_type,
              bool authorized) {
  const int64_t start = esp_timer_get_time();
  if (radio_deadline && start >= radio_deadline) {
    budget_exhausted = true;
    fail("Backup session time limit reached; progress is saved");
    return {};
  }
  auto result = transport.request(url, method, parts, content_type,
                                  authorized ? "Bearer " + access_token : "",
                                  radio_deadline, cancelled);
  ++http_requests;
  tls_connections += result.connections;
  if (!result.status && radio_deadline &&
      esp_timer_get_time() >= radio_deadline) {
    budget_exhausted = true;
    fail("Backup session time limit reached; progress is saved");
  }
  ESP_LOGI("leaf_backup", "%s %s: HTTP %d, %lu ms, new TLS=%u",
           authorized ? "Drive" : "OAuth",
           method == HTTP_METHOD_GET ? "GET" : "WRITE", result.status,
           static_cast<unsigned long>((esp_timer_get_time() - start) / 1000),
           result.connections);
  server_retry = std::max(server_retry, result.retry);
  return result;
}
Response form(const char *url, const std::string &body) {
  return http(url, HTTP_METHOD_POST, {{body.data(), body.size()}},
              "application/x-www-form-urlencoded", false);
}
bool take_tokens(cJSON *j, bool require_refresh) {
  auto access = field(j, "access_token");
  auto refresh = field(j, "refresh_token");
  if (access.empty() || access.size() > 4096 || refresh.size() > 4095 ||
      (require_refresh && refresh.empty()))
    return fail("Google returned incomplete credentials");
  const auto granted = field(j, "scope");
  if ((require_refresh && granted.empty()) ||
      (!granted.empty() &&
       (" " + granted + " ").find(" " + std::string(Scope) + " ") ==
           std::string::npos))
    return fail("Drive permission was not granted", true);
  access_token = access;
  access_expires =
      esp_timer_get_time() +
      int64_t(std::max(0, std::clamp(number(j, "expires_in", 3600), 1, 86400) -
                              30)) *
          1000000;
  if (!refresh.empty())
    refresh_token = refresh;
  if (!save_credentials())
    return fail("Could not save credentials to NVS");
  {
    std::lock_guard<std::mutex> guard(mutex);
    shared.linked = !refresh_token.empty();
  }
  return true;
}
bool refresh_access() {
  if (!access_token.empty() && esp_timer_get_time() < access_expires)
    return true;
  if (refresh_token.empty())
    return fail("Link Google Drive in Backup settings", true);
  std::string body = "client_id=" + percent_encode(client_id) +
                     "&client_secret=" + percent_encode(client_secret) +
                     "&refresh_token=" + percent_encode(refresh_token) +
                     "&grant_type=refresh_token";
  auto response = form("https://oauth2.googleapis.com/token", body);
  auto j = parse(response.body);
  if (response.status == 200)
    return take_tokens(j.get(), false);
  if (field(j.get(), "error") == "invalid_grant") {
    refresh_token.clear();
    access_token.clear();
    save_credentials();
    {
      std::lock_guard<std::mutex> guard(mutex);
      shared.linked = false;
    }
    return fail("Google authorization expired; link Drive again", true);
  }
  return fail("Google token refresh failed",
              response.status == 400 || response.status == 401);
}
Response drive(const std::string &url, esp_http_client_method_t method,
               const std::vector<View> &parts = {},
               const char *type = "application/json") {
  if (!refresh_access())
    return {};
  auto result = http(url, method, parts, type, true);
  if (result.status == 401) {
    access_token.clear();
    if (refresh_access())
      result = http(url, method, parts, type, true);
  }
  return result;
}
bool response_ok(const Response &response) {
  if (response.status >= 200 && response.status < 300)
    return true;
  if (response.status == 403) {
    auto j = parse(response.body);
    auto *detail = cJSON_GetObjectItemCaseSensitive(j.get(), "error");
    auto *first = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(detail, "errors"), 0);
    const auto reason = field(first, "reason");
    if (reason == "rateLimitExceeded" || reason == "userRateLimitExceeded" ||
        reason == "dailyLimitExceeded" || reason == "backendError") {
      if (reason == "dailyLimitExceeded")
        server_retry = 3600;
      return fail("Google Drive is rate-limited; backup will retry");
    }
  }
  if (error.empty())
    fail(response.status ? "Drive request failed (HTTP " +
                               std::to_string(response.status) + ")"
                         : "Network interrupted or backup session timed out",
         response.status == 400 || response.status == 403);
  return false;
}
void wifi_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    xEventGroupSetBits(wifi_events, BIT0);
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
    wifi_disconnect_reason = event ? event->reason : 0;
    xEventGroupClearBits(wifi_events, BIT0);
    xEventGroupSetBits(wifi_events, BIT1);
  }
}
bool setup_network() {
  if (network_ready)
    return true;
  if (esp_netif_init() != ESP_OK)
    return fail("Network stack initialization failed", true);
  auto loop = esp_event_loop_create_default();
  if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE)
    return fail("Network event loop unavailable", true);
  if (!wifi_interface)
    wifi_interface = esp_netif_create_default_wifi_sta();
  if (!wifi_interface)
    return fail("Wi-Fi interface unavailable", true);
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  config.static_rx_buf_num = 6;
  config.dynamic_rx_buf_num = 12;
  config.dynamic_tx_buf_num = 8;
  config.static_tx_buf_num = 6;
  config.cache_tx_buf_num = 4;
  config.mgmt_sbuf_num = 6;
  if (!wifi_initialized) {
    auto result = esp_wifi_init(&config);
    if (result != ESP_OK) {
      ESP_LOGE("leaf_wifi", "Wi-Fi init: %s; internal free=%u largest=%u",
               esp_err_to_name(result),
               unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT)),
               unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                         MALLOC_CAP_8BIT)));
      return fail(std::string("Wi-Fi initialization failed: ") +
                      esp_err_to_name(result),
                  true);
    }
    wifi_initialized = true;
  }
  if (!wifi_events)
    wifi_events = xEventGroupCreate();
  if (!wifi_events)
    return fail("Wi-Fi event allocation failed", true);
  if (!wifi_callback)
    wifi_callback = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event, nullptr) == ESP_OK;
  if (!ip_callback)
    ip_callback = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             wifi_event, nullptr) == ESP_OK;
  if (!wifi_callback || !ip_callback)
    return fail("Wi-Fi callbacks unavailable", true);
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK)
    return fail("Could not protect Wi-Fi credential storage", true);
  network_ready = true;
  return true;
}
void wifi_off() {
  transport.reset();
  if (wifi_initialized) {
    esp_wifi_disconnect();
    esp_wifi_stop();
  }
  esp_netif_sntp_deinit();
}
bool connect(bool synchronize_clock = true) {
  if (!valid_wifi(ssid, password))
    return fail("Configure Wi-Fi in Backup settings", true);
  if (!setup_network())
    return false;
  wifi_config_t config{};
  std::memcpy(config.sta.ssid, ssid.data(), ssid.size());
  std::memcpy(config.sta.password, password.data(), password.size());
  config.sta.threshold.authmode =
      password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  wifi_disconnect_reason = 0;
  xEventGroupClearBits(wifi_events, BIT0 | BIT1);
  message("Connecting to Wi-Fi...");
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK ||
      esp_wifi_start() != ESP_OK)
    return fail("Could not start Wi-Fi");
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  ESP_LOGI(
      "leaf_wifi", "Connecting; internal free=%u largest DMA=%u",
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_largest_free_block(
          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
  if (esp_wifi_connect() != ESP_OK)
    return fail("Wi-Fi connection failed");
  bool joined = false;
  for (unsigned n = 0; n < 200 && !cancelled(); ++n) {
    auto bits = xEventGroupWaitBits(wifi_events, BIT0 | BIT1, pdFALSE, pdFALSE,
                                    pdMS_TO_TICKS(100));
    if (bits & BIT0) {
      joined = true;
      break;
    }
    if (bits & BIT1)
      break;
  }
  if (!joined) {
    ESP_LOGW("leaf_wifi",
             "Connection ended: reason=%u internal free=%u largest DMA=%u",
             wifi_disconnect_reason.load(),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                              MALLOC_CAP_8BIT)),
             unsigned(heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
    if (cancelled())
      return fail("Wi-Fi connection test cancelled");
    switch (wifi_disconnect_reason.load()) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return fail(
          "Wi-Fi authentication failed; check password and router security");
    case WIFI_REASON_NO_AP_FOUND:
      return fail("Network not found; check SSID and 2.4 GHz Wi-Fi");
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
      return fail("Router security is incompatible with this Wi-Fi setup");
    case 0:
      return fail(
          "Wi-Fi timed out waiting for an IP address; check router DHCP");
    default:
      return fail("Wi-Fi disconnected (reason " +
                  std::to_string(wifi_disconnect_reason.load()) + ")");
    }
  }
  if (synchronize_clock && !clock_ready) {
    message("Synchronizing clock for secure connection...");
    esp_sntp_config_t time = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
    if (esp_netif_sntp_init(&time) != ESP_OK)
      return fail("Secure clock synchronization unavailable");
    for (unsigned n = 0; n < 15 && !cancelled(); ++n)
      if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000)) == ESP_OK) {
        clock_ready = true;
        break;
      }
    esp_netif_sntp_deinit();
    if (!clock_ready)
      return fail("Clock synchronization failed; TLS remains enforced");
  }
  return !cancelled();
}
struct Node {
  std::string id, sha;
  int64_t size = -1, mtime = -1;
};
std::map<std::string, Node> index;
std::set<std::string> verified;
std::map<std::string, std::string> remote_md5;
std::set<std::string> audited;
std::vector<std::string> available_ids;
bool load_index() {
  index.clear();
  Snapshot job(Snapshot::Kind::Read);
  job.relative = ".backup_index";
  if (!transfer(job))
    return true;
  auto j = parse(std::string(reinterpret_cast<char *>(job.data), job.size));
  const int version = number(j.get(), "version", 0);
  if (version != 1 && version != 2)
    return true;
  auto *entries = cJSON_GetObjectItemCaseSensitive(j.get(), "entries");
  if (!cJSON_IsObject(entries))
    return true;
  for (auto *e = entries->child; e; e = e->next) {
    if (index.size() >= 4096)
      return fail("Backup index exceeds capacity", true);
    std::string key = e->string ? e->string : "";
    if (key != ":root" && !safe_relative(key))
      continue;
    auto id = field(e, "id"), sha = field(e, "sha");
    if ((!id.empty() && !valid_drive_id(id)) || sha.size() > 64)
      continue;
    index[key] = {id, sha, number(e, "size", -1), number(e, "mtime", -1)};
  }
  return true;
}
bool persist_index() {
  auto j = object();
  cJSON_AddNumberToObject(j.get(), "version", 2);
  auto *entries = cJSON_AddObjectToObject(j.get(), "entries");
  for (const auto &pair : index) {
    auto *entry = cJSON_AddObjectToObject(entries, pair.first.c_str());
    cJSON_AddStringToObject(entry, "id", pair.second.id.c_str());
    cJSON_AddStringToObject(entry, "sha", pair.second.sha.c_str());
    cJSON_AddNumberToObject(entry, "size", pair.second.size);
    cJSON_AddNumberToObject(entry, "mtime", pair.second.mtime);
  }
  auto text = dump(j.get());
  if (text.empty() || text.size() > 512 * 1024)
    return fail("Backup index could not be serialized");
  Snapshot job(Snapshot::Kind::Write);
  job.relative = ".backup_index";
  job.size = text.size();
  job.data = static_cast<unsigned char *>(
      heap_caps_malloc(job.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!job.data)
    return fail("Not enough memory for backup state");
  std::memcpy(job.data, text.data(), job.size);
  if (!transfer(job))
    return fail(job.error.empty() ? "Backup index write failed" : job.error);
  return true;
}
bool owned(cJSON *metadata, const std::string &key) {
  auto *props = cJSON_GetObjectItemCaseSensitive(metadata, "appProperties");
  return field(props, "leafDevice") == device &&
         field(props, "leafKey") == hash(key);
}
Json metadata(const std::string &key, bool folder, const std::string &parent) {
  auto j = object();
  auto &node = index[key];
  std::string name = key == ":root"
                         ? "Leaf Backup - " + device.substr(device.size() - 6)
                         : key.substr(key.find_last_of('/') == std::string::npos
                                          ? 0
                                          : key.find_last_of('/') + 1);
  cJSON_AddStringToObject(j.get(), "id", node.id.c_str());
  cJSON_AddStringToObject(j.get(), "name", name.c_str());
  cJSON_AddStringToObject(j.get(), "mimeType",
                          folder ? "application/vnd.google-apps.folder"
                                 : "text/plain");
  if (!parent.empty()) {
    auto *parents = cJSON_AddArrayToObject(j.get(), "parents");
    cJSON_AddItemToArray(parents, cJSON_CreateString(parent.c_str()));
  }
  auto *props = cJSON_AddObjectToObject(j.get(), "appProperties");
  cJSON_AddStringToObject(props, "leafDevice", device.c_str());
  auto key_hash = hash(key);
  cJSON_AddStringToObject(props, "leafKey", key_hash.c_str());
  return j;
}
bool ensure_node(const std::string &key, bool folder, const std::string &parent,
                 bool &exists, unsigned attempt = 0) {
  if (attempt > 1)
    return fail("Drive item changed during backup; retry later");
  if (index.size() >= 4096 && index.find(key) == index.end())
    return fail("Backup index capacity reached", true);
  auto &node = index[key];
  bool fresh_id = false;
  exists = false;
  if (verified.count(key)) {
    exists = true;
    return true;
  }
  if (node.id.empty()) {
    std::string query =
        "trashed = false and appProperties has { key='leafDevice' and "
        "value='" +
        device + "' } and appProperties has { key='leafKey' and value='" +
        hash(key) + "' }";
    query += " and mimeType='" +
             std::string(folder ? "application/vnd.google-apps.folder"
                                : "text/plain") +
             "'";
    if (!parent.empty())
      query += " and '" + parent + "' in parents";
    auto r = drive(std::string(Api) +
                       "?pageSize=2&fields=files(id,appProperties)&q=" +
                       percent_encode(query),
                   HTTP_METHOD_GET);
    if (!response_ok(r))
      return false;
    auto j = parse(r.body);
    auto *first = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(j.get(), "files"), 0);
    if (first && owned(first, key)) {
      node.id = field(first, "id");
      if (!valid_drive_id(node.id))
        return fail("Drive returned an invalid file ID");
    }
    if (node.id.empty()) {
      if (available_ids.empty()) {
        r = drive(std::string(Api) +
                      "/generateIds?count=32&space=drive&type=files",
                  HTTP_METHOD_GET);
        if (!response_ok(r))
          return false;
        j = parse(r.body);
        auto *ids = cJSON_GetObjectItemCaseSensitive(j.get(), "ids");
        const int count = cJSON_GetArraySize(ids);
        if (count < 1 || count > 32)
          return fail("Drive did not allocate valid file IDs");
        for (int i = 0; i < count; ++i) {
          auto *id = cJSON_GetArrayItem(ids, i);
          if (!cJSON_IsString(id) || !valid_drive_id(id->valuestring)) {
            available_ids.clear();
            return fail("Drive returned an invalid file ID");
          }
          available_ids.emplace_back(id->valuestring);
        }
      }
      node.id = std::move(available_ids.back());
      available_ids.pop_back();
      fresh_id = true;
    }
    // Persist the preallocated ID before creating anything remotely.
    // Retries reuse it.
    if (!persist_index())
      return false;
  }
  Response r;
  if (!fresh_id) {
    r = drive(
        std::string(Api) + "/" + percent_encode(node.id) +
            "?fields=id,mimeType,trashed,appProperties,parents,md5Checksum",
        HTTP_METHOD_GET);
    if (r.status == 200) {
      auto j = parse(r.body);
      auto *trashed = cJSON_GetObjectItemCaseSensitive(j.get(), "trashed");
      bool parent_matches = parent.empty();
      auto *parents = cJSON_GetObjectItemCaseSensitive(j.get(), "parents");
      for (int i = 0; i < cJSON_GetArraySize(parents); ++i) {
        auto *value = cJSON_GetArrayItem(parents, i);
        if (cJSON_IsString(value) && parent == value->valuestring)
          parent_matches = true;
      }
      const bool type_matches =
          field(j.get(), "mimeType") ==
          std::string(folder ? "application/vnd.google-apps.folder"
                             : "text/plain");
      if (!owned(j.get(), key) || cJSON_IsTrue(trashed) || !parent_matches ||
          !type_matches) {
        node.id.clear();
        node.sha.clear();
        if (!persist_index())
          return false;
        return ensure_node(key, folder, parent, exists, attempt + 1);
      }
      exists = true;
      if (folder && node.sha != "folder") {
        node.sha = "folder";
        if (!persist_index())
          return false;
      }
      if (!folder)
        remote_md5[key] = field(j.get(), "md5Checksum");
      verified.insert(key);
      return true;
    }
    if (r.status != 404)
      return response_ok(r);
    // A previously created item was deleted. Its ID cannot be reused.
    if (!node.sha.empty()) {
      node.id.clear();
      node.sha.clear();
      if (!persist_index())
        return false;
      return ensure_node(key, folder, parent, exists, attempt + 1);
    }
  }
  if (!folder)
    return true;
  auto j = metadata(key, true, parent);
  auto text = dump(j.get());
  r = drive(Api, HTTP_METHOD_POST, {{text.data(), text.size()}});
  if (r.status == 409) {
    auto check = drive(std::string(Api) + "/" + percent_encode(node.id) +
                           "?fields=id,appProperties",
                       HTTP_METHOD_GET);
    auto data = parse(check.body);
    if (!response_ok(check) || !owned(data.get(), key))
      return fail("Drive folder identity mismatch", true);
  } else if (!response_ok(r))
    return false;
  exists = true;
  node.sha = "folder";
  if (!persist_index())
    return false;
  verified.insert(key);
  return true;
}
bool parent_for(const std::string &path, std::string &parent) {
  bool exists = false;
  if (!ensure_node(":root", true, "", exists))
    return false;
  parent = index[":root"].id;
  for (size_t slash = path.find('/'); slash != std::string::npos;
       slash = path.find('/', slash + 1)) {
    auto key = path.substr(0, slash);
    if (!ensure_node(key, true, parent, exists))
      return false;
    parent = index[key].id;
  }
  return true;
}
bool checkpoint(const std::string &path, const FileSnapshot &snapshot) {
  auto &node = index[path];
  node.sha = snapshot.sha;
  node.size = snapshot.size;
  node.mtime = snapshot.mtime;
  if (!persist_index())
    return false;
  Snapshot clean(Snapshot::Kind::Clean);
  clean.relative = path;
  clean.revision = snapshot.revision;
  if (!transfer(clean))
    return fail(clean.error);
  followup_needed |= !clean.cleared;
  return true;
}
bool upload(const std::string &path, FileSnapshot &snapshot) {
  std::string parent;
  if (!parent_for(path, parent))
    return false;
  bool exists;
  if (!ensure_node(path, false, parent, exists))
    return false;
  auto &node = index[path];
  const auto &checksum = snapshot.md5;
  if (exists && remote_md5[path] == checksum)
    return checkpoint(path, snapshot);
  Response r;
  if (exists)
    r = drive(std::string(Upload) + "/" + percent_encode(node.id) +
                  "?uploadType=media&fields=id,size,md5Checksum",
              HTTP_METHOD_PATCH,
              {{nullptr, snapshot.size, FileSnapshot::stream, &snapshot,
                upload_progress}},
              "text/plain; charset=UTF-8");
  else {
    auto j = metadata(path, false, parent);
    auto text = dump(j.get());
    const std::string boundary =
        "leaf_" + std::to_string(esp_random()) + std::to_string(esp_random());
    const std::string first =
        "--" + boundary +
        "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + text +
        "\r\n--" + boundary +
        "\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n";
    const std::string last = "\r\n--" + boundary + "--\r\n";
    const std::string type = "multipart/related; boundary=" + boundary;
    r = drive(std::string(Upload) +
                  "?uploadType=multipart&fields=id,size,md5Checksum",
              HTTP_METHOD_POST,
              {{first.data(), first.size()},
               {nullptr, snapshot.size, FileSnapshot::stream, &snapshot,
                upload_progress},
               {last.data(), last.size()}},
              type.c_str());
    if (r.status == 409) {
      auto check = drive(std::string(Api) + "/" + percent_encode(node.id) +
                             "?fields=id,appProperties",
                         HTTP_METHOD_GET);
      auto data = parse(check.body);
      if (!response_ok(check) || !owned(data.get(), path))
        return fail("Drive file identity mismatch", true);
      r = drive(std::string(Upload) + "/" + percent_encode(node.id) +
                    "?uploadType=media&fields=id,size,md5Checksum",
                HTTP_METHOD_PATCH,
                {{nullptr, snapshot.size, FileSnapshot::stream, &snapshot,
                  upload_progress}},
                "text/plain; charset=UTF-8");
    }
  }
  if (!snapshot.error.empty())
    return fail(snapshot.error);
  if (!response_ok(r))
    return false;
  auto result = parse(r.body);
  if (field(result.get(), "id") != node.id ||
      field(result.get(), "size") != std::to_string(snapshot.size) ||
      field(result.get(), "md5Checksum") != checksum)
    return fail("Drive upload confirmation was incomplete");
  ++uploaded_documents;
  verified.insert(path);
  return checkpoint(path, snapshot);
}
bool run_backup(bool verify) {
  if (refresh_token.empty())
    return fail("Link Google Drive in Backup settings", true);
  if (!load_index())
    return false;
  verified.clear();
  remote_md5.clear();
  message("Checking saved document changes...");
  Snapshot list(Snapshot::Kind::List);
  if (!transfer(list))
    return fail(list.error);
  std::vector<Document> candidates;
  for (const auto &document : list.files) {
    auto found = index.find(document.path);
    const bool unknown = found == index.end() || found->second.id.empty() ||
                         found->second.sha.empty();
    if (unknown || document.dirty || list.rescan ||
        (!unknown && (found->second.size != int64_t(document.size) ||
                      found->second.mtime != document.mtime)) ||
        (verify && !audited.count(document.path)))
      candidates.push_back(document);
  }
  ESP_LOGI("leaf_backup", "Documents=%u candidates=%u verify=%d reconcile=%d",
           unsigned(list.files.size()), unsigned(candidates.size()), verify,
           list.rescan);
  bool connected = false;
  const unsigned total = unsigned(candidates.size());
  report_progress(ProgressPhase::Checking, 0, total);
  for (const auto &document : candidates) {
    if (cancelled())
      return false;
    FileSnapshot snapshot;
    report_progress(ProgressPhase::Checking, completed_documents, total);
    message("Checking changed document " +
            std::to_string(completed_documents + 1) + " of " +
            std::to_string(candidates.size()));
    if (!snapshot.prepare(document.path))
      return fail(snapshot.error);
    ++hashed_documents;
    auto found = index.find(document.path);
    if (!verify && found != index.end() && !found->second.id.empty() &&
        found->second.sha == snapshot.sha) {
      if (!checkpoint(document.path, snapshot))
        return false;
      ++completed_documents;
      report_progress(ProgressPhase::Checking, completed_documents, total);
      continue;
    }
    if (!connected) {
      radio_deadline = esp_timer_get_time() + 120LL * 1000000;
      report_progress(ProgressPhase::Connecting, completed_documents, total);
      if (!connect())
        return false;
      connected = true;
    }
    message(verify ? "Verifying backup copies..."
                   : "Uploading changed document...");
    report_progress(verify ? ProgressPhase::Verifying
                           : ProgressPhase::Uploading,
                    completed_documents, total);
    if (!upload(document.path, snapshot))
      return false;
    if (verify)
      audited.insert(document.path);
    ++completed_documents;
    report_progress(ProgressPhase::Checking, completed_documents, total);
  }
  report_progress(ProgressPhase::Finishing, completed_documents, total);
  Snapshot finish(Snapshot::Kind::Reconciled);
  finish.edits = list.edits;
  if (!transfer(finish))
    return fail(finish.error);
  followup_needed |= !finish.cleared;
  message(uploaded_documents
              ? "Uploaded " + std::to_string(uploaded_documents) +
                    " changed documents"
          : verify ? "Backup verified; no uploads needed"
                   : "No content changes to upload");
  if (followup_needed)
    message("Saved newer edits; they remain queued for backup");
  {
    std::lock_guard<std::mutex> guard(mutex);
    shared_progress.percent = 100;
  }
  return true;
}
bool authorize() {
  available_ids.clear();
  radio_deadline = esp_timer_get_time() + 600LL * 1000000;
  if (!connect())
    return false;
  message("Requesting Google sign-in code...");
  auto r = form("https://oauth2.googleapis.com/device/code",
                "client_id=" + percent_encode(client_id) +
                    "&scope=" + percent_encode(Scope));
  auto j = parse(r.body);
  if (r.status != 200)
    return fail("Google device authorization failed; check OAuth setup",
                r.status >= 400 && r.status < 500);
  auto code = field(j.get(), "device_code"), user = field(j.get(), "user_code"),
       url = field(j.get(), "verification_url");
  if (url.empty())
    url = field(j.get(), "verification_uri");
  if (code.empty() || user.empty() || user.size() > 64 || url.size() > 256 ||
      url.rfind("https://", 0) != 0)
    return fail("Invalid Google authorization response");
  uint32_t interval = std::max(5, number(j.get(), "interval", 5));
  int64_t expires = std::min(
      radio_deadline,
      esp_timer_get_time() +
          int64_t(std::max(1, number(j.get(), "expires_in", 600))) * 1000000);
  {
    std::lock_guard<std::mutex> guard(mutex);
    shared.verification_url = url;
    shared.user_code = user;
    shared.message = "On your phone, open the URL and enter the code";
  }
  while (esp_timer_get_time() < expires && !cancelled()) {
    sleep_ms(uint32_t(std::min<int64_t>(
        int64_t(interval) * 1000,
        std::max<int64_t>(0, (expires - esp_timer_get_time()) / 1000))));
    if (cancelled() || esp_timer_get_time() >= expires)
      break;
    r = form(
        "https://oauth2.googleapis.com/token",
        "client_id=" + percent_encode(client_id) +
            "&client_secret=" + percent_encode(client_secret) +
            "&device_code=" + percent_encode(code) + "&grant_type=" +
            percent_encode("urn:ietf:params:oauth:grant-type:device_code"));
    j = parse(r.body);
    if (r.status == 200) {
      index.clear();
      if (!persist_index())
        return false;
      if (!take_tokens(j.get(), true)) {
        refresh_token.clear();
        access_token.clear();
        save_credentials();
        return false;
      }
      automatic = true;
      save_auto();
      message("Google Drive linked and saved");
      return true;
    }
    switch (poll_error(field(j.get(), "error"), r.status)) {
    case Poll::Pending:
      break;
    case Poll::SlowDown:
      interval = std::min<uint32_t>(600, interval + 5);
      break;
    case Poll::Retry:
      interval = std::max(r.retry, std::min<uint32_t>(60, interval * 2));
      break;
    case Poll::Denied:
      return fail("Google sign-in was declined", true);
    case Poll::Expired:
      return fail("Google code expired; choose Link Google again", true);
    case Poll::Fatal:
      return fail("Google sign-in failed; check OAuth setup", true);
    }
  }
  return fail(cancelled() ? "Google sign-in cancelled"
                          : "Google sign-in timed out",
              true);
}
void publish(bool busy) {
  std::lock_guard<std::mutex> guard(mutex);
  shared.busy = busy;
  if (!busy)
    shared_progress.active = false;
  shared.linked = !refresh_token.empty();
  shared.automatic = automatic;
  shared.ssid = ssid;
  configured_wifi = {ssid, password};
}
void worker(void *) {
  secure = false;
#if CONFIG_NVS_ENCRYPTION
  secure = esp_flash_encryption_enabled();
#endif
  nvs_handle_t h;
  if (nvs_open("leaf_backup", NVS_READONLY, &h) == ESP_OK) {
    uint8_t value = 0;
    nvs_get_u8(h, "auto", &value);
    automatic = value;
    ssid = read_secret(h, "ssid");
    password = read_secret(h, "wifi_key");
    refresh_token = read_secret(h, "refresh");
    nvs_close(h);
  }
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char id[13];
  snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  device = id;
  {
    std::lock_guard<std::mutex> guard(mutex);
    shared.secure_storage = secure;
    shared.configured = !client_id.empty() && !client_secret.empty();
  }
  message(client_id.empty() || client_secret.empty()
              ? "Google OAuth client not configured; see BACKUP_SETUP.md"
          : refresh_token.empty() ? "Configure Wi-Fi and link Google Drive"
                                  : "Google Drive linked; backup ready");
  publish(false);
  int64_t next_due = esp_timer_get_time() + 30LL * 1000000;
  unsigned failures = 0;
  bool audit_pending = false;

  for (;;) {
    TickType_t wait = portMAX_DELAY;
    if (automatic && !refresh_token.empty()) {
      int64_t due = std::max(next_due, last_input.load() + 30LL * 1000000);
      wait = pdMS_TO_TICKS(
          std::clamp<int64_t>((due - esp_timer_get_time()) / 1000, 100, 30000));
    }
    ulTaskNotifyTake(pdTRUE, wait);
    if (stopping.load())
      break;
    uint32_t action = commands.exchange(0);
    if (action & Network) {
      std::lock_guard<std::mutex> guard(mutex);
      ssid = pending_ssid;
      password = pending_password;
      shared.ssid = ssid;
      next_due = esp_timer_get_time() + 5000000;
    }
    if (action & Automatic) {
      {
        std::lock_guard<std::mutex> guard(mutex);
        automatic = pending_auto;
        shared.automatic = automatic;
      }
      save_auto();
      next_due = esp_timer_get_time() + 5000000;
    }
    if (action & Network) {
      publish(true);
      terminal = false;
      error.clear();
      const bool saved = save_credentials();
      radio_deadline = esp_timer_get_time() + 30LL * 1000000;
      const bool connected = saved && !(action & Forget) && connect(false);
      wifi_off();
      const std::string result =
          !saved      ? "Could not save Wi-Fi to NVS; try again"
          : connected ? "Connected and saved. Wi-Fi will reconnect when needed."
          : !error.empty() ? error + ". Credentials saved; edit or retry."
                           : "Connection test cancelled; credentials saved";
      {
        std::lock_guard<std::mutex> guard(mutex);
        shared.wifi_testing = false;
        shared.wifi_saved = saved;
        shared.wifi_message = result;
      }
      ESP_LOGI("leaf_wifi", "%s", result.c_str());
      message(result);
      publish(false);
      if (!(action & (Manual | Link | Verify | Forget)))
        continue;
    }
    if (action & Forget) {
      refresh_token.clear();
      access_token.clear();
      index.clear();
      audited.clear();
      available_ids.clear();
      audit_pending = false;
      automatic = false;
      save_auto();
      bool saved = save_credentials();
      persist_index();
      message(saved ? "Google account forgotten on this Leaf"
                    : "Could not erase stored Google credentials");
      publish(false);
      continue;
    }
    const bool due = automatic && !refresh_token.empty() &&
                     esp_timer_get_time() >= next_due &&
                     esp_timer_get_time() - last_input.load() > 30LL * 1000000;
    if (!(action & (Manual | Link | Verify)) && !due) {
      publish(false);
      continue;
    }
    if (client_id.empty() || client_secret.empty()) {
      message("Google OAuth client not configured; see BACKUP_SETUP.md");
      publish(false);
      continue;
    }
    terminal = false;
    budget_exhausted = false;
    http_requests = tls_connections = completed_documents = 0;
    uploaded_documents = hashed_documents = 0;
    followup_needed = false;
    const int64_t attempt_start = esp_timer_get_time();
    server_retry = 0;
    error.clear();
    publish(true);
    const bool linking = (action & Link) != 0;
    if (action & Verify) {
      audited.clear();
      audit_pending = true;
    }
    if (linking) {
      audited.clear();
      audit_pending = false;
    }
    {
      std::lock_guard<std::mutex> guard(mutex);
      shared_progress = {};
      shared_progress.active = !linking;
      shared_progress.verification = audit_pending;
    }
    bool ok = linking ? authorize() : run_backup(audit_pending);
    wifi_off();
    ESP_LOGI("leaf_backup",
             "%s: ok=%d, %lu ms, requests=%u, TLS=%u, completed=%u, hashed=%u, "
             "uploaded=%u",
             linking ? "Link" : "Backup", ok,
             static_cast<unsigned long>((esp_timer_get_time() - attempt_start) /
                                        1000),
             http_requests, tls_connections, completed_documents,
             hashed_documents, uploaded_documents);
    {
      std::lock_guard<std::mutex> guard(mutex);
      shared.user_code.clear();
      shared.verification_url.clear();
    }
    if (ok) {
      failures = 0;
      if (!linking && audit_pending) {
        audit_pending = false;
        audited.clear();
      }
      next_due = esp_timer_get_time() +
                 (linking || followup_needed ? 5000000 : PeriodUs);
    } else if (budget_exhausted && !linking) {
      failures = 0;
      next_due = esp_timer_get_time() + 60000000;
      message("Backed up " + std::to_string(completed_documents) +
              (automatic ? " files; continuing in 1 min"
                         : " files; run Back up now to continue"));
    } else if (cancelled()) {
      message("Backup operation cancelled");
      next_due = esp_timer_get_time() + 60000000;
    } else {
      ++failures;
      const uint32_t wait =
          std::max(server_retry, retry_seconds(failures)) + esp_random() % 16;
      next_due = esp_timer_get_time() + int64_t(wait) * 1000000;
      if (terminal) {
        automatic = false;
        save_auto();
        message(error.empty() ? "Backup needs attention in Settings" : error);
      } else
        message((error.empty() ? "Backup interrupted" : error) + "; retry in " +
                std::to_string((wait + 59) / 60) + " min");
    }
    publish(false);
  }
  wifi_off();
  worker_stopped = true;
  vTaskDelete(nullptr);
}
} // namespace
void init() {
  if (task)
    return;
  if (!leaf_backup::storage_init()) {
    message("Backup queue allocation failed");
    return;
  }
  if (xTaskCreatePinnedToCore(worker, "leaf_backup", 16384, nullptr, 2, &task,
                              0) != pdPASS) {
    task = nullptr;
    message("Backup worker allocation failed");
  }
}
void shutdown() {
  if (!task)
    return;
  stopping = true;
  xTaskNotifyGive(task);
  while (!worker_stopped.load()) {
    leaf_backup::storage_tick(false);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void tick(EditorState &editor) {
  const bool verifying = verify_pending.exchange(false);
  if (manual_pending.exchange(false) || verifying) {
    if (flush_editor_content(editor))
      send(verifying ? Verify : Manual);
    else
      message("Save the current document before backing up");
  }
  leaf_backup::storage_tick(editor.content_pending_save);
}
void before_storage_mutation() { leaf_backup::storage_cancel(); }
bool pending_storage() { return leaf_backup::storage_pending(); }
void note_input() { last_input = esp_timer_get_time(); }
void request_now() { manual_pending = true; }
void request_verify() { verify_pending = true; }
bool mark_document_dirty(const std::string &path) {
  return leaf_backup::storage_mark_dirty(path);
}
bool mark_tree_dirty() { return leaf_backup::storage_mark_tree_dirty(); }
void request_link() { send(Link); }
void forget_google() { send(Forget); }
void set_automatic(bool enabled) {
  {
    std::lock_guard<std::mutex> guard(mutex);
    pending_auto = enabled;
  }
  send(Automatic);
}
bool configure_wifi(const std::string &network, const std::string &key) {
  if (!task || !valid_wifi(network, key))
    return false;
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (shared.wifi_testing)
      return false;
    pending_ssid = network;
    pending_password = key;
    shared.wifi_testing = true;
    shared.wifi_message = "Saving and testing Wi-Fi connection...";
  }
  send(Network);
  return true;
}
WifiCredentials wifi_credentials() {
  std::lock_guard<std::mutex> guard(mutex);
  return configured_wifi;
}
bool test_saved_wifi() {
  const auto credentials = wifi_credentials();
  return configure_wifi(credentials.ssid, credentials.password);
}
Status status() {
  std::lock_guard<std::mutex> guard(mutex);
  return shared;
}
Progress progress() {
  std::lock_guard<std::mutex> guard(mutex);
  return shared_progress;
}
} // namespace backup
