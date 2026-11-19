#pragma once
#include <string>
struct EditorState;
namespace backup {
enum class ProgressPhase {
  Checking,
  Connecting,
  Uploading,
  Verifying,
  Finishing
};
struct Progress {
  bool active = false, verification = false;
  ProgressPhase phase = ProgressPhase::Checking;
  unsigned completed = 0, total = 0;
  int percent = -1; // Unknown while the document list is being prepared.
};
Progress progress();
struct Status {
  bool configured = false, linked = false, automatic = false, busy = false,
       secure_storage = false, wifi_testing = false, wifi_saved = false;
  std::string wifi_message;
  std::string message = "Backup not configured", ssid, verification_url,
              user_code;
};
struct WifiCredentials {
  std::string ssid, password;
};
WifiCredentials
wifi_credentials(); // UI only; keep secrets out of public status/logs.
bool test_saved_wifi();
void init();
void shutdown();
bool pending_storage();
void tick(EditorState &); // UI task: bounded filesystem snapshots and pending
                          // manual flush.
void note_input();
void before_storage_mutation(); // UI task: abort an in-progress snapshot before
                                // any mutation.
void request_now();
void request_verify();
bool mark_document_dirty(const std::string &absolute_path);
bool mark_tree_dirty();
void request_link();
void forget_google();
void set_automatic(bool);
bool configure_wifi(const std::string &ssid, const std::string &password);
Status status();
} // namespace backup
