#define LEAF_BACKUP_DOCUMENT_ROOT "tools/backup_tests/documents/"
#include "backup/storage.cpp"
#include <atomic>
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
bool fail_commit = false;
bool replace_saved_file(const std::string &temp, const std::string &target) {
  if (fail_commit)
    return false;
  std::error_code error;
  std::filesystem::remove(target, error);
  return std::rename(temp.c_str(), target.c_str()) == 0;
}
void write(const std::string &path, const std::string &text) {
  std::ofstream f(path, std::ios::binary);
  f << text;
  assert(f.good());
}
void round_trip(leaf_backup::Snapshot &job, bool expected, bool dirty = false,
                bool cancel = false) {
  std::atomic<bool> done = false;
  bool result = false;
  std::thread worker([&] {
    result = leaf_backup::transfer(job);
    done = true;
  });
  for (unsigned i = 0; i < 100000 && !done; ++i) {
    leaf_backup::storage_tick(dirty);
    if (cancel && job.offset > 0)
      leaf_backup::storage_cancel();
    std::this_thread::yield();
  }
  worker.join();
  assert(!leaf_backup::storage_pending());
  assert(result == expected);
}
int main() {
  namespace fs = std::filesystem;
  using namespace leaf_backup;
  const fs::path root = "tools/backup_tests/documents";
  fs::create_directories(root / "Nested");
  fs::create_directories(root / "recently_deleted");
  for (const char *name :
       {".backup_dirty", ".backup_dirty.tmp", ".backup_dirty.bak",
        ".backup_snapshot", "Oversized.txt"})
    fs::remove(root / name);
  const std::string original(1024 * 1024 + 17, 'x');
  write((root / "Test.txt").string(), original);
  write((root / "Nested/Empty.txt").string(), "");
  write((root / "recently_deleted/Old.txt").string(), "old");
  write((root / ".secret.txt").string(), "hidden");
  write((root / "Test.txt.tmp").string(), "temporary");
  assert(storage_init());
  write((root / "Deleted.txt").string(), "never backed up");
  assert(storage_mark_dirty((root / "Deleted.txt").generic_string()));
  fs::remove(root / "Deleted.txt");
  Snapshot list(Snapshot::Kind::List);
  round_trip(list, true);
  assert(list.files.size() == 2 && list.files[0].path == "Nested/Empty.txt" &&
         list.rescan);
  assert(storage_mark_dirty((root / "Test.txt").generic_string()));
  Snapshot prepared(Snapshot::Kind::Prepare);
  prepared.relative = "Test.txt";
  round_trip(prepared, true);
  assert(prepared.size == original.size() && prepared.data == nullptr &&
         prepared.revision != 0);
  // A newer edit must not alter the immutable upload or be cleared by its
  // result.
  assert(storage_mark_dirty((root / "Test.txt").generic_string()));
  write((root / "Test.txt").string(), std::string(original.size(), 'y'));
  std::vector<unsigned char> buffer(4096);
  std::string streamed;
  for (size_t offset = 0; offset < prepared.size;) {
    Snapshot chunk(Snapshot::Kind::Chunk);
    chunk.token = prepared.token;
    chunk.position = offset;
    chunk.size = std::min(buffer.size(), prepared.size - offset);
    chunk.data = buffer.data();
    chunk.owns_data = false;
    round_trip(chunk, true,
               true); // Streaming continues even while the editor is dirty.
    streamed.append(reinterpret_cast<char *>(buffer.data()), chunk.size);
    offset += chunk.size;
  }
  assert(streamed == original);
  Snapshot clean_old(Snapshot::Kind::Clean);
  clean_old.relative = "Test.txt";
  clean_old.revision = prepared.revision;
  round_trip(clean_old, true);
  assert(!clean_old.cleared);
  Snapshot release(Snapshot::Kind::Release);
  release.token = prepared.token;
  round_trip(release, true);
  Snapshot expired(Snapshot::Kind::Chunk);
  expired.token = prepared.token;
  expired.size = 1;
  expired.data = buffer.data();
  expired.owns_data = false;
  round_trip(expired, false);
  Snapshot updated(Snapshot::Kind::Prepare);
  updated.relative = "Test.txt";
  round_trip(updated, true);
  assert(updated.revision != prepared.revision);
  Snapshot clean(Snapshot::Kind::Clean);
  clean.relative = "Test.txt";
  clean.revision = updated.revision;
  round_trip(clean, true);
  assert(clean.cleared);
  Snapshot release_updated(Snapshot::Kind::Release);
  release_updated.token = updated.token;
  round_trip(release_updated, true);
  Snapshot stale_reconcile(Snapshot::Kind::Reconciled);
  stale_reconcile.edits = list.edits;
  round_trip(stale_reconcile, true);
  assert(!stale_reconcile.cleared);
  Snapshot current(Snapshot::Kind::List);
  round_trip(current, true);
  Snapshot reconciled(Snapshot::Kind::Reconciled);
  reconciled.edits = current.edits;
  round_trip(reconciled, true);
  assert(reconciled.cleared);
  Snapshot next(Snapshot::Kind::List);
  round_trip(next, true);
  assert(!next.rescan && !next.files[1].dirty);
  // Canceled staging closes both handles without modifying the source.
  Snapshot interrupted(Snapshot::Kind::Prepare);
  interrupted.relative = "Test.txt";
  round_trip(interrupted, false, false, true);
  assert(!interrupted.error.empty());
  assert(fs::file_size(root / "Test.txt") == original.size());
  assert(std::rename((root / "Test.txt").string().c_str(),
                     (root / "Moved.txt").string().c_str()) == 0);
  assert(std::rename((root / "Moved.txt").string().c_str(),
                     (root / "Test.txt").string().c_str()) == 0);
  Snapshot busy(Snapshot::Kind::Prepare);
  busy.relative = "Test.txt";
  round_trip(busy, false, true);
  Snapshot empty(Snapshot::Kind::Prepare);
  empty.relative = "Nested/Empty.txt";
  round_trip(empty, true);
  assert(empty.size == 0 && !empty.data);
  Snapshot release_empty(Snapshot::Kind::Release);
  release_empty.token = empty.token;
  round_trip(release_empty, true);
  Snapshot invalid(Snapshot::Kind::Prepare);
  invalid.relative = "../secret.txt";
  round_trip(invalid, false);
  const auto large = root / "Oversized.txt";
  write(large.string(), std::string(4 * 1024 * 1024 + 1, 'z'));
  Snapshot oversized(Snapshot::Kind::Prepare);
  oversized.relative = "Oversized.txt";
  round_trip(oversized, false);
  assert(!oversized.data && fs::file_size(large) == 4 * 1024 * 1024 + 1);
  fs::remove(large);
  Snapshot state(Snapshot::Kind::Write);
  state.relative = ".backup_index";
  state.size = 3;
  state.data = static_cast<unsigned char *>(heap_caps_malloc(3, 0));
  std::memcpy(state.data, "new", 3);
  round_trip(state, true);
  fail_commit = true;
  Snapshot failed(Snapshot::Kind::Write);
  failed.relative = ".backup_index";
  failed.size = 3;
  failed.data = static_cast<unsigned char *>(heap_caps_malloc(3, 0));
  std::memcpy(failed.data, "bad", 3);
  round_trip(failed, false);
  fail_commit = false;
  std::ifstream input(root / ".backup_index");
  std::string value;
  input >> value;
  assert(value == "new");
  // Independently reload the actual journal: dirty state survives reboot.
  DirtyTracker rebooted((root / ".backup_dirty").generic_string(),
                        replace_saved_file);
  assert(rebooted.init() && rebooted.dirty("Test.txt"));
  assert(!rebooted.dirty("Deleted.txt"));
  assert(!rebooted.dirty("../secret.txt"));
  std::cout << "Backup bounded-stream, snapshot, edit-race, and storage "
               "handoff tests passed\n";
}
