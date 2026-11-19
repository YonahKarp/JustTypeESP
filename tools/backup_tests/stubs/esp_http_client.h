#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum esp_http_client_method_t {
  HTTP_METHOD_GET,
  HTTP_METHOD_POST,
  HTTP_METHOD_PATCH
};
enum Event {
  HTTP_EVENT_ON_CONNECTED,
  HTTP_EVENT_ON_HEADER,
  HTTP_EVENT_DISCONNECTED
};
struct esp_http_client_event_t {
  Event event_id;
  void *user_data;
  const char *header_key = nullptr, *header_value = nullptr;
};
struct esp_http_client_config_t {
  const char *url = nullptr;
  int (*crt_bundle_attach)(void *) = nullptr;
  int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
  bool disable_auto_redirect = false, keep_alive_enable = false;
  int (*event_handler)(esp_http_client_event_t *) = nullptr;
  void *user_data = nullptr;
};
namespace fake_http {
struct Reply {
  int status = 200;
  std::string body = "ok";
  bool complete = true, close = false, open_fail = false;
  int read_fail_after = -1;
  std::string retry;
};
struct Request {
  std::string url;
  esp_http_client_method_t method;
  std::map<std::string, std::string> headers;
  std::string body;
};
inline std::deque<Reply> replies;
inline std::vector<Request> requests;
inline int initialized = 0, cleaned = 0;
inline void clear() {
  replies.clear();
  requests.clear();
  initialized = cleaned = 0;
}
} // namespace fake_http
struct FakeClient {
  esp_http_client_config_t cfg;
  std::string url;
  esp_http_client_method_t method = HTTP_METHOD_GET;
  std::map<std::string, std::string> headers;
  fake_http::Reply reply;
  size_t offset = 0;
  bool connected = false;
  void event(Event id, const char *key = nullptr, const char *value = nullptr) {
    esp_http_client_event_t e{id, cfg.user_data, key, value};
    cfg.event_handler(&e);
  }
};
using esp_http_client_handle_t = FakeClient *;
inline auto esp_http_client_init(const esp_http_client_config_t *cfg) {
  assert(cfg->crt_bundle_attach && cfg->disable_auto_redirect &&
         cfg->keep_alive_enable);
  ++fake_http::initialized;
  auto *client = new FakeClient;
  client->cfg = *cfg;
  return client;
}
inline int esp_http_client_cleanup(FakeClient *c) {
  ++fake_http::cleaned;
  c->event(HTTP_EVENT_DISCONNECTED);
  delete c;
  return 0;
}
inline int esp_http_client_set_url(FakeClient *c, const char *url) {
  c->url = url;
  return 0;
}
inline int esp_http_client_set_method(FakeClient *c,
                                      esp_http_client_method_t method) {
  c->method = method;
  return 0;
}
inline int esp_http_client_set_header(FakeClient *c, const char *k,
                                      const char *v) {
  c->headers[k] = v;
  return 0;
}
inline int esp_http_client_delete_header(FakeClient *c, const char *k) {
  c->headers.erase(k);
  return 0;
}
inline int esp_http_client_set_timeout_ms(FakeClient *, int n) {
  assert(n > 0 && n <= 12000);
  return 0;
}
inline int esp_http_client_open(FakeClient *c, int size) {
  assert(!fake_http::replies.empty());
  c->reply = fake_http::replies.front();
  fake_http::replies.pop_front();
  c->offset = 0;
  if (size || c->method != HTTP_METHOD_GET)
    c->headers["Content-Length"] = std::to_string(size);
  fake_http::requests.push_back({c->url, c->method, c->headers, {}});
  if (c->reply.open_fail)
    return -1;
  if (!c->connected) {
    c->connected = true;
    c->event(HTTP_EVENT_ON_CONNECTED);
  }
  return 0;
}
inline int esp_http_client_write(FakeClient *, const char *data, int size) {
  fake_http::requests.back().body.append(data, size);
  return size;
}
inline int esp_http_client_fetch_headers(FakeClient *c) {
  if (c->reply.close)
    c->event(HTTP_EVENT_ON_HEADER, "Connection", "close");
  if (!c->reply.retry.empty())
    c->event(HTTP_EVENT_ON_HEADER, "Retry-After", c->reply.retry.c_str());
  return int(c->reply.body.size());
}
inline int esp_http_client_get_status_code(FakeClient *c) {
  return c->reply.status;
}
inline int esp_http_client_read(FakeClient *c, char *data, int size) {
  if (c->reply.read_fail_after >= 0 &&
      c->offset >= unsigned(c->reply.read_fail_after))
    return -1;
  const auto count = std::min<size_t>(
      {size_t(size), size_t(307), c->reply.body.size() - c->offset});
  std::memcpy(data, c->reply.body.data() + c->offset, count);
  c->offset += count;
  return int(count);
}
// Simulates the parser having consumed the wire body before the cached body is
// drained.
inline bool esp_http_client_is_complete_data_received(FakeClient *c) {
  return c->reply.complete;
}
