#include "backup/http_transport.h"
#include <iostream>
using namespace leaf_backup;
bool cancelled = false;
bool stop() { return cancelled; }
struct Stream {
  std::string content;
  size_t largest = 0, calls = 0;
  size_t sent = 0;
  bool fail = false;
  static void progress(void *context, size_t sent, size_t total) {
    auto &self = *static_cast<Stream *>(context);
    assert(sent > self.sent && sent <= total);
    assert(fake_http::requests.back().body.size() >= sent);
    self.sent = sent;
  }
  static int read(void *context, size_t offset, unsigned char *out,
                  size_t count) {
    auto &self = *static_cast<Stream *>(context);
    assert(count <= 4096 && offset + count <= self.content.size());
    self.largest = std::max(self.largest, count);
    ++self.calls;
    if (self.fail && offset >= 4096)
      return -1;
    std::memcpy(out, self.content.data() + offset, count);
    return int(count);
  }
};
const std::string url = "https://www.googleapis.com/drive/v3/files";
Response get(HttpTransport &t, const std::string &target = url,
             const std::string &bearer = {}) {
  return t.request(target, HTTP_METHOD_GET, {}, "application/json", bearer,
                   esp_timer_get_time() + 1000000, stop);
}
int main() {
  using namespace fake_http;
  clear();
  {
    HttpTransport t;
    replies.push_back({200, std::string(3000, 'x')});
    auto first = get(t, url, "Bearer token-one");
    assert(first.body == std::string(3000, 'x') && first.connections == 1);
    replies.push_back({200, "second"});
    auto second = get(t, url + "/another", "Bearer token-two");
    assert(second.status == 200 && second.body == "second" &&
           second.connections == 0);
    assert(initialized == 1 && cleaned == 0);
    assert(requests.back().headers.at("Authorization") == "Bearer token-two");
    const std::string text(5000, 'a');
    replies.push_back({});
    assert(t.request(url, HTTP_METHOD_POST, {{text.data(), text.size()}},
                     "text/plain", "Bearer token", 0, stop)
               .status == 200);
    assert(requests.back().body == text);
    replies.push_back({});
    assert(get(t).status == 200);
    assert(!requests.back().headers.count("Authorization"));
    assert(!requests.back().headers.count("Content-Length"));
    replies.push_back({});
    assert(get(t, "https://oauth2.googleapis.com/token").status == 200);
    assert(initialized == 2 && cleaned == 1);
    assert(!requests.back().headers.count("Authorization"));
    const auto count = requests.size();
    assert(get(t, "https://oauth2.googleapis.com/token", "Bearer forbidden")
               .status == 0);
    assert(get(t, "https://www.googleapis.com.evil/").status == 0);
    assert(requests.size() == count);
  }
  assert(initialized == cleaned);
  clear();
  {
    HttpTransport t;
    replies.push_back({});
    assert(get(t).status == 200);
    Reply partial;
    partial.body = "partial-body";
    partial.read_fail_after = 1;
    replies.push_back(partial);
    replies.push_back({200, "fresh"});
    auto r = get(t);
    assert(r.status == 200 && r.body == "fresh");
    assert(initialized == 2 && requests.size() == 3);
    // An ambiguous mutation is not automatically replayed on a broken socket.
    Reply broken;
    broken.open_fail = true;
    replies.push_back(broken);
    auto mutation = t.request(url, HTTP_METHOD_PATCH, {}, "text/plain",
                              "Bearer token", 0, stop);
    assert(mutation.status == 0 && requests.size() == 4 && replies.empty());
  }
  clear();
  {
    HttpTransport t;
    Reply closing;
    closing.close = true;
    replies.push_back(closing);
    assert(get(t).status == 200 && cleaned == 1);
    replies.push_back({});
    assert(get(t).status == 200 && initialized == 2);
    Reply limited;
    limited.status = 429;
    limited.retry = "9000";
    replies.push_back(limited);
    auto r = get(t);
    assert(r.status == 429 && r.retry == 3600);
  }
  clear();
  {
    HttpTransport t;
    Reply oversized;
    oversized.body = std::string(32769, 'x');
    replies.push_back(oversized);
    assert(get(t).status == 0 && cleaned == 1);
    replies.push_back({});
    assert(get(t).status == 200);
    Reply incomplete;
    incomplete.complete = false;
    replies.push_back(incomplete);
    replies.push_back({200, "recovered"});
    assert(get(t).body == "recovered");
    const auto count = requests.size();
    cancelled = true;
    assert(get(t).status == 0);
    cancelled = false;
    assert(requests.size() == count);
    assert(t.request(url, HTTP_METHOD_GET, {}, "text/plain", {},
                     esp_timer_get_time() - 1, stop)
               .status == 0);
    assert(requests.size() == count);
    t.reset();
    assert(initialized == cleaned);
  }
  std::cout << "HTTP reuse, body draining, credential isolation, safe retry, "
               "and failure tests passed\n";
  clear();
  {
    HttpTransport t;
    Stream source{std::string(2 * 1024 * 1024 + 17, 'x')};
    const std::string prefix = "multipart header", suffix = "multipart end";
    replies.push_back({});
    auto result = t.request(url, HTTP_METHOD_POST,
                            {{prefix.data(), prefix.size()},
                             {nullptr, source.content.size(), Stream::read,
                              &source, Stream::progress},
                             {suffix.data(), suffix.size()}},
                            "multipart/related", "Bearer token", 0, stop);
    assert(result.status == 200);
    assert(requests.back().body == prefix + source.content + suffix);
    assert(source.largest == 4096 && source.calls > 500);
    assert(source.sent == source.content.size());
    source.sent = 0;
    source.fail = true;
    replies.push_back({});
    result = t.request(url, HTTP_METHOD_PATCH,
                       {{nullptr, source.content.size(), Stream::read, &source,
                         Stream::progress}},
                       "text/plain", "Bearer token", 0, stop);
    assert(result.status == 0 && requests.back().body.size() == 4096);
    assert(source.sent == 4096);
    assert(replies.empty());
    replies.push_back({});
    assert(get(t).status == 200);
  }
  std::cout
      << "Bounded multipart streaming and local-read failure tests passed\n";
}
