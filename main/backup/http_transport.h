#pragma once
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "policy.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace leaf_backup {
struct View {
  const void *data;
  size_t size;
  int (*read)(void *, size_t, unsigned char *, size_t) = nullptr;
  void *context = nullptr;
  void (*progress)(void *, size_t, size_t) = nullptr;
};
struct Response {
  int status = 0;
  std::string body;
  uint32_t retry = 0;
  unsigned connections = 0;
};

// Owned by the backup worker. Connections live only for one awake radio
// session.
class HttpTransport {
  esp_http_client_handle_t client_ = nullptr;
  Response *response_ = nullptr;
  std::string origin_;
  bool server_close_ = false;
  static bool equal(const char *a, const char *b) {
    while (*a && *b) {
      if (std::tolower(static_cast<unsigned char>(*a++)) !=
          std::tolower(static_cast<unsigned char>(*b++)))
        return false;
    }
    return *a == *b;
  }
  static esp_err_t event(esp_http_client_event_t *event) {
    auto *self = static_cast<HttpTransport *>(event->user_data);
    if (!self || !self->response_)
      return ESP_OK;
    auto &out = *self->response_;
    if (event->event_id == HTTP_EVENT_ON_CONNECTED)
      ++out.connections;
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
      if (equal(event->header_key, "Connection") &&
          equal(event->header_value, "close"))
        self->server_close_ = true;
      if (equal(event->header_key, "Retry-After")) {
        char *end = nullptr;
        const long value = std::strtol(event->header_value, &end, 10);
        if (end && *end == 0 && value > 0)
          out.retry = std::min<long>(3600, value);
      }
    }
    return ESP_OK;
  }

public:
  HttpTransport() = default;
  HttpTransport(const HttpTransport &) = delete;
  HttpTransport &operator=(const HttpTransport &) = delete;
  ~HttpTransport() { reset(); }
  void reset() {
    response_ = nullptr;
    if (client_)
      esp_http_client_cleanup(client_);
    client_ = nullptr;
    origin_.clear();
  }
  Response request(const std::string &url, esp_http_client_method_t method,
                   const std::vector<View> &parts, const char *content_type,
                   const std::string &bearer, int64_t deadline,
                   bool (*cancelled)()) {
    Response result;
    if (!approved_url(url))
      return result;
    const std::string origin =
        url.rfind("https://www.googleapis.com/", 0) == 0 ? "drive" : "oauth";
    // A Drive access token must never be carried to another origin.
    if (!bearer.empty() && origin != "drive")
      return result;
    if (origin != origin_)
      reset();
    const bool reused = client_ != nullptr;
    auto stopped = [&] {
      return cancelled() || (deadline && esp_timer_get_time() >= deadline);
    };
    auto timeout = [&] {
      return deadline ? int(std::clamp<int64_t>(
                            (deadline - esp_timer_get_time() + 999) / 1000, 1,
                            12000))
                      : 12000;
    };
    // Only a GET may be retried immediately after a stale persistent
    // connection.
    for (unsigned attempt = 0; attempt < 2 && !stopped(); ++attempt) {
      result.status = 0;
      result.body.clear();
      response_ = &result;
      server_close_ = false;
      if (!client_) {
        esp_http_client_config_t cfg{};
        cfg.url = url.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = timeout();
        cfg.disable_auto_redirect = true;
        cfg.keep_alive_enable = true;
        cfg.buffer_size = 2048;
        cfg.buffer_size_tx = 2048;
        cfg.event_handler = event;
        cfg.user_data = this;
        client_ = esp_http_client_init(&cfg);
        origin_ = origin;
      }
      if (!client_) {
        response_ = nullptr;
        break;
      }
      result.status = 0;
      result.body.clear();
      result.retry = 0;
      server_close_ = false;
      size_t total = 0;
      bool ok = true;
      for (const auto &part : parts) {
        if (part.size > 8 * 1024 * 1024 - total) {
          ok = false;
          break;
        }
        total += part.size;
      }
      ok = ok && esp_http_client_set_url(client_, url.c_str()) == ESP_OK &&
           esp_http_client_set_method(client_, method) == ESP_OK &&
           esp_http_client_set_header(client_, "Content-Type", content_type) ==
               ESP_OK;
      esp_http_client_delete_header(client_, "Authorization");
      esp_http_client_delete_header(client_, "Content-Length");
      esp_http_client_delete_header(client_, "Transfer-Encoding");
      if (!bearer.empty())
        ok = ok && esp_http_client_set_header(client_, "Authorization",
                                              bearer.c_str()) == ESP_OK;
      esp_http_client_set_timeout_ms(client_, timeout());
      ok = ok && !stopped() &&
           esp_http_client_open(client_, int(total)) == ESP_OK;
      std::vector<unsigned char> stream_buffer;
      for (const auto &part : parts) {
        size_t offset = 0;
        while (ok && offset < part.size) {
          if (stopped()) {
            ok = false;
            break;
          }
          esp_http_client_set_timeout_ms(client_, timeout());
          const size_t count = std::min<size_t>(4096, part.size - offset);
          const char *data = static_cast<const char *>(part.data);
          if (part.read) {
            if (stream_buffer.empty())
              stream_buffer.resize(4096);
            if (part.read(part.context, offset, stream_buffer.data(), count) !=
                int(count)) {
              ok = false;
              break;
            }
            data = reinterpret_cast<const char *>(stream_buffer.data());
          } else if (data)
            data += offset;
          else {
            ok = false;
            break;
          }
          size_t written = 0;
          while (ok && written < count) {
            if (stopped()) {
              ok = false;
              break;
            }
            esp_http_client_set_timeout_ms(client_, timeout());
            const int moved =
                esp_http_client_write(client_, data + written,
                                      std::min<size_t>(2048, count - written));
            if (moved <= 0) {
              ok = false;
              break;
            }
            written += moved;
            if (part.progress)
              part.progress(part.context, offset + written, part.size);
          }
          offset += written;
        }
      }
      esp_http_client_set_timeout_ms(client_, timeout());
      if (ok && !stopped() && esp_http_client_fetch_headers(client_) >= 0) {
        result.status = esp_http_client_get_status_code(client_);
        char buffer[1024];
        // Drain cached body bytes too, even if the HTTP parser is already
        // complete.
        for (;;) {
          if (stopped()) {
            result.status = 0;
            break;
          }
          esp_http_client_set_timeout_ms(client_, timeout());
          const int count =
              esp_http_client_read(client_, buffer, sizeof(buffer));
          if (count < 0) {
            result.status = 0;
            break;
          }
          if (!count)
            break;
          if (result.body.size() + count > 32768) {
            result.status = 0;
            result.body.clear();
            break;
          }
          result.body.append(buffer, count);
        }
        if (!esp_http_client_is_complete_data_received(client_))
          result.status = 0;
      }
      const bool retry = !result.status && !result.retry && reused &&
                         !attempt && method == HTTP_METHOD_GET && !stopped();
      response_ = nullptr;
      if (!result.status || server_close_)
        reset();
      if (!retry)
        break;
    }
    response_ = nullptr;
    return result;
  }
};
} // namespace leaf_backup
