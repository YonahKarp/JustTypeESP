// Content has a browser transport; physical radios and Google backup remain
// unavailable in this client-only demo.
#include "backup/backup.h"
#include "content.h"
#include "content_browser.h"
#include "backup/storage.h"
#include "serial_export.h"
namespace backup {
Progress progress() { return {}; }
Status status() { Status s; s.message = "Unavailable in browser simulator"; s.wifi_message = s.message; return s; }
WifiCredentials wifi_credentials() { return {}; }
bool test_saved_wifi() { return false; }
bool scan_wifi() { return false; }
void init() { browser_content::init(); }
void shutdown() {}
bool pending_storage() { return false; }
void tick(EditorState&) { browser_content::tick(); }
void note_input() {}
void before_storage_mutation() {}
void request_now() {}
void request_content() { browser_content::request(); }
void request_verify() {}
bool mark_document_dirty(const std::string&) { return true; }
bool mark_tree_dirty() { return true; }
void request_link() {}
void forget_google() {}
void set_automatic(bool) {}
bool configure_wifi(const std::string&, const std::string&) { return false; }
}
namespace leaf_backup {
// Persistence calls must succeed locally even though cloud backup is absent.
bool storage_mark_dirty(const std::string&) { return true; }
bool storage_mark_tree_dirty() { return true; }
}
namespace serial_export {
bool init() { return true; }
bool requested() { return false; }
void send_documents(const char*, bool) {}
}
