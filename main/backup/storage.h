#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace leaf_backup {
struct Document {
  std::string path;
  size_t size = 0;
  int64_t mtime = 0;
  bool dirty = false;
};
struct Snapshot {
  enum class Kind {
    List,
    Read,
    Write,
    Prepare,
    Chunk,
    Release,
    Clean,
    Reconciled
  };
  explicit Snapshot(Kind k) : kind(k) {}
  Snapshot(const Snapshot &) = delete;
  Snapshot &operator=(const Snapshot &) = delete;
  Kind kind;
  std::string relative, error;
  std::vector<Document> files;
  unsigned char *data = nullptr;
  size_t size = 0, offset = 0;
  size_t position = 0;
  int64_t mtime = 0;
  uint64_t token = 0, revision = 0, edits = 0;
  bool ok = false, owns_data = true, cleared = false, rescan = false;
  ~Snapshot();
};
bool storage_init();
bool storage_mark_dirty(const std::string &absolute_path);
bool storage_mark_tree_dirty();
bool storage_pending();
void storage_tick(bool editor_dirty);
void storage_cancel();
bool transfer(Snapshot &);
} // namespace leaf_backup
