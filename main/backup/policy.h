#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace leaf_backup {
inline bool safe_relative(const std::string &path) {
  if (path.empty() || path.size() > 480 || path.front() == '/' ||
      path.back() == '/')
    return false;
  size_t first = 0;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i < path.size() && (static_cast<unsigned char>(path[i]) < 32 ||
                            path[i] == '\\' || path[i] == ':'))
      return false;
    if (i == path.size() || path[i] == '/') {
      auto part = path.substr(first, i - first);
      if (part.empty() || part[0] == '.' || part == "recently_deleted")
        return false;
      first = i + 1;
    }
  }
  return true;
}
inline bool document_path(const std::string &path) {
  return safe_relative(path) && path.size() > 4 &&
         path.substr(path.size() - 4) == ".txt";
}
inline bool valid_drive_id(const std::string &id) {
  if (id.empty() || id.size() > 128)
    return false;
  for (unsigned char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  return true;
}
inline bool valid_wifi(const std::string &ssid, const std::string &password) {
  return !ssid.empty() && ssid.size() <= 32 &&
         ssid.find('\0') == std::string::npos &&
         password.find('\0') == std::string::npos &&
         (password.empty() || (password.size() >= 8 && password.size() <= 63));
}
inline uint32_t retry_seconds(unsigned failures) {
  return std::min<uint32_t>(
      3600, 60u << std::min<unsigned>(failures ? failures - 1 : 0, 6));
}
inline std::string percent_encode(const std::string &input) {
  const char *hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : input) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
      out += char(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
inline bool approved_url(const std::string &url) {
  for (const char *prefix :
       {"https://oauth2.googleapis.com/", "https://www.googleapis.com/"})
    if (url.rfind(prefix, 0) == 0)
      return true;
  return false;
}
enum class Poll { Pending, SlowDown, Denied, Expired, Retry, Fatal };
inline Poll poll_error(const std::string &error, int status) {
  if (error == "authorization_pending")
    return Poll::Pending;
  if (error == "slow_down")
    return Poll::SlowDown;
  if (error == "access_denied")
    return Poll::Denied;
  if (error == "expired_token" || error == "invalid_grant")
    return Poll::Expired;
  if (status == 0 || status == 429 || status >= 500)
    return Poll::Retry;
  return Poll::Fatal;
}
} // namespace leaf_backup
