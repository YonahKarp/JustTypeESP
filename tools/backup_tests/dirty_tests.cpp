#include "backup/dirty_tracker.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
bool fail_commit = false;
bool replace(const std::string &tmp, const std::string &path) {
  if (fail_commit)
    return false;
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}
int main() {
  using namespace leaf_backup;
  const std::string path = "tools/backup_tests/documents/.journal_test";
  std::filesystem::remove(path);
  std::filesystem::remove(path + ".tmp");
  {
    DirtyTracker t(path, replace);
    assert(t.init() && t.rescan());
    bool clean;
    assert(t.reconciled(t.edits(), clean) && clean && !t.rescan());
    assert(t.mark("Novel/Chapter.txt"));
    const auto revision = t.revision("Novel/Chapter.txt"), edits = t.edits();
    const auto journal_bytes = std::filesystem::file_size(path);
    assert(t.mark("Novel/Chapter.txt"));
    assert(std::filesystem::file_size(path) == journal_bytes);
    assert(t.clean("Novel/Chapter.txt", revision, clean) && !clean);
    assert(t.reconciled(edits, clean) && !clean);
  }
  {
    DirtyTracker t(path, replace);
    assert(t.init() && t.dirty("Novel/Chapter.txt"));
    bool clean;
    assert(
        t.clean("Novel/Chapter.txt", t.revision("Novel/Chapter.txt"), clean) &&
        clean);
    assert(!t.mark("../secret.txt"));
    assert(!t.mark(".backup_index"));
    assert(t.mark(u8"Заметки/Привет.txt"));
  }
  {
    DirtyTracker t(path, replace);
    assert(t.init());
    assert(!t.dirty("Novel/Chapter.txt") && t.dirty(u8"Заметки/Привет.txt"));
  }
  // A torn record cannot make the implementation trust previously clean files.
  {
    std::ofstream f(path, std::ios::binary | std::ios::app);
    f << "C 4e6f76";
  }
  {
    DirtyTracker t(path, replace);
    assert(t.init() && t.rescan());
    assert(t.dirty(u8"Заметки/Привет.txt"));
    assert(t.mark("Recovered.txt"));
  }
  {
    DirtyTracker t(path, replace);
    assert(t.init() && t.rescan() && t.dirty("Recovered.txt"));
    bool clean;
    assert(t.reconciled(t.edits(), clean) && clean);
    const auto old = t.edits();
    assert(t.force_rescan());
    assert(t.reconciled(old, clean) && !clean && t.rescan());
  }
  // Failed journal creation prevents accepting an untracked mutation.
  const std::string broken = "tools/backup_tests/documents/.journal_failure";
  std::filesystem::remove(broken);
  fail_commit = true;
  {
    DirtyTracker t(broken, replace);
    assert(!t.init());
    assert(!t.mark("Unsaved.txt"));
  }
  fail_commit = false;
  const std::string compacted =
      "tools/backup_tests/documents/.journal_compaction";
  std::filesystem::remove(compacted);
  {
    DirtyTracker t(compacted, replace);
    assert(t.init());
    bool clean;
    assert(t.reconciled(t.edits(), clean) && clean);
    const std::string document = "Novel/" + std::string(200, 'a') + ".txt";
    for (int i = 0; i < 100; ++i) {
      assert(t.mark(document));
      assert(t.clean(document, t.revision(document), clean) && clean);
    }
    assert(std::filesystem::file_size(compacted) < 65536);
  }
  {
    DirtyTracker t(compacted, replace);
    assert(t.init() && !t.rescan());
  }
  std::cout << "Dirty-journal persistence, version checks, UTF-8, and "
               "torn-write recovery tests passed\n";
}
