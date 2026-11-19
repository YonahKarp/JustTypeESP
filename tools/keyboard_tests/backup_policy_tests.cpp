#include "backup/policy.h"
#include <cassert>
#include <iostream>
void test_backup_policy() {
  using namespace leaf_backup;
  assert(document_path("Novel/Chapter 1.txt"));
  assert(document_path(u8"Заметки/Привет.txt"));
  for (const char *path : {"../secret.txt", "/absolute.txt", "C:/secret.txt",
                           "folder/../secret.txt", ".editor_state",
                           ".hidden/test.txt", "recently_deleted/old.txt",
                           "a\\b.txt", "folder//a.txt", "file.txt.tmp"})
    assert(!document_path(path));
  assert(!document_path(std::string("bad\0name.txt", 12)));
  assert(percent_encode("a b&c+/") == "a%20b%26c%2B%2F");
  assert(approved_url("https://www.googleapis.com/upload/drive/v3/files"));
  assert(approved_url("https://oauth2.googleapis.com/token"));
  for (const char *url :
       {"http://www.googleapis.com/", "https://www.googleapis.com.evil/",
        "https://evil/?https://www.googleapis.com/",
        "https://www.googleapis.com@evil/"})
    assert(!approved_url(url));
  assert(valid_drive_id("aB_12-3"));
  assert(!valid_drive_id("bad'id"));
  assert(!valid_drive_id("bad/id"));
  assert(!valid_drive_id(""));
  assert(valid_wifi("Leaf WiFi", "password"));
  assert(valid_wifi("Open", ""));
  assert(!valid_wifi("", "password"));
  assert(!valid_wifi("Network", "short"));
  assert(!valid_wifi(std::string("bad\0ssid", 8), "password"));
  assert(retry_seconds(1) == 60);
  assert(retry_seconds(2) == 120);
  assert(retry_seconds(50) == 3600);
  assert(poll_error("authorization_pending", 428) == Poll::Pending);
  assert(poll_error("slow_down", 403) == Poll::SlowDown);
  assert(poll_error("access_denied", 403) == Poll::Denied);
  assert(poll_error("expired_token", 400) == Poll::Expired);
  assert(poll_error("invalid_client", 401) == Poll::Fatal);
  assert(poll_error("", 429) == Poll::Retry);
  assert(poll_error("", 503) == Poll::Retry);
  assert(poll_error("", 0) == Poll::Retry);
  std::cout << "Backup policy tests passed\n";
}
