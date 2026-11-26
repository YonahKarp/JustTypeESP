#pragma once
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "justtype/src/content.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cstdio>
#include <unistd.h>
namespace content {
// Called only by the existing radio worker, after its Wi-Fi/clock setup.
inline int64_t session_deadline = 0;
inline bool download_once(const std::string &relative,
                          const std::string &destination, uint32_t maximum,
                          const std::string &expected, bool &retryable) {
  download_progress(-1, "Connecting to content server...");
  const std::string url =
      "https://justtypeleaf.com/api/leaf/content/v1/" + relative;
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 10000;
  config.buffer_size = 2048;
  config.disable_auto_redirect = true;
  auto client = esp_http_client_init(&config);
  if (!client)
    return false;
  bool ok = esp_http_client_open(client, 0) == ESP_OK;
  int64_t length = ok ? esp_http_client_fetch_headers(client) : -1;
  const int status = esp_http_client_get_status_code(client);
  retryable = status == 0 || status == 408 || status == 429 || status >= 500;
  ok = ok && status == 200 && (length < 0 || uint64_t(length) <= maximum);
  if (ok) download_progress(-1, relative == "catalog" ? "Fetching catalog..." : "Downloading content...");
  FILE *file = ok ? fopen(destination.c_str(), "wb") : nullptr;
  ok = ok && file;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts(&sha, 0) != 0)
    ok = false;
  const int64_t deadline =
      std::min(esp_timer_get_time() + 180LL * 1000000,
               session_deadline ? session_deadline : INT64_MAX);
  uint32_t count = 0;
  bool read_failed = false;
  unsigned char buffer[2048];
  while (ok && !cancelled() && esp_timer_get_time() < deadline) {
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(buffer),
                                       sizeof buffer);
    if (n < 0) {
      read_failed = true;
      ok = false;
      break;
    }
    if (!n)
      break;
    if (uint64_t(count) + n > maximum ||
        fwrite(buffer, 1, n, file) != size_t(n) ||
        mbedtls_sha256_update(&sha, buffer, n) != 0) {
      ok = false;
      break;
    }
    count += n;
    download_progress(expected.empty() ? -1
                                       : int(uint64_t(count) * 100 / maximum));
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  const bool complete = esp_http_client_is_complete_data_received(client);
  if (status == 200 && (read_failed || !complete || (length >= 0 && count != uint64_t(length))))
    retryable = true;
  ok = ok && !cancelled() && esp_timer_get_time() < deadline && complete;
  if (length >= 0)
    ok = ok && count == uint64_t(length);
  if (!expected.empty())
    ok = ok && count == maximum;
  if (ok) download_progress(-1, "Verifying download...");
  unsigned char hash[32];
  char hex[65]{};
  if (mbedtls_sha256_finish(&sha, hash) != 0)
    ok = false;
  for (unsigned i = 0; i < 32; ++i)
    snprintf(hex + i * 2, 3, "%02x", hash[i]);
  if (!expected.empty())
    ok = ok && expected == hex;
  mbedtls_sha256_free(&sha);
  if (file) {
    if (fflush(file) != 0 || fsync(fileno(file)) != 0)
      ok = false;
    if (fclose(file) != 0)
      ok = false;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (!ok)
    std::remove(destination.c_str());
  if (!ok)
    ESP_LOGW("leaf_content", "Download failed: HTTP %d, received %u, expected %lld, complete=%d, retry=%d", status, unsigned(count), static_cast<long long>(length), int(complete), int(retryable));
  return ok;
}
inline bool download(const std::string &relative,
                     const std::string &destination, uint32_t maximum,
                     const std::string &expected) {
  for (unsigned attempt = 0; attempt < 3 && !cancelled(); ++attempt) {
    bool retryable = true;
    if (download_once(relative, destination, maximum, expected, retryable))
      return true;
    if (!retryable ||
        (session_deadline && esp_timer_get_time() >= session_deadline))
      break;
    vTaskDelay(pdMS_TO_TICKS(200u << attempt));
  }
  return false;
}
} // namespace content
