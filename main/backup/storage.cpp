#include "storage.h"
#include "dirty_tracker.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "justtype/src/file_manager.h"
#include "policy.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
namespace leaf_backup {
namespace {
#ifndef LEAF_BACKUP_DOCUMENT_ROOT
#define LEAF_BACKUP_DOCUMENT_ROOT "/spiffs/user_files/"
#endif
constexpr const char *root = LEAF_BACKUP_DOCUMENT_ROOT;
constexpr const char *index_file = ".backup_index";
const std::string staged = std::string(root) + ".backup_snapshot";
DirtyTracker tracker(std::string(root) + ".backup_dirty", replace_saved_file);
QueueHandle_t requests = nullptr, completed = nullptr;
Snapshot *active = nullptr;
std::atomic<bool> pending{false};
FILE *file = nullptr, *copy = nullptr;
FILE *stream_reader = nullptr;
DIR *directory = nullptr;
std::vector<std::string> pending_dirs;
std::string current_dir;
std::string prune_cursor;
uint64_t next_token = 0, staged_token = 0;
void finish(bool ok, const char *error = "") {
  if (file) {
    if (active && active->kind == Snapshot::Kind::Chunk && ok) {
      // Keep the immutable snapshot's cursor between chunks: FAT seeking from
      // the beginning for every block would make long documents quadratic.
    } else {
      if (file == stream_reader)
        stream_reader = nullptr;
      if (fclose(file) != 0)
        ok = false;
    }
    file = nullptr;
  }
  if (copy) {
    if (fclose(copy) != 0)
      ok = false;
    copy = nullptr;
  }
  if (directory) {
    closedir(directory);
    directory = nullptr;
  }
  if (active) {
    if (active->kind == Snapshot::Kind::Prepare) {
      staged_token = ok ? active->token : 0;
      if (!ok)
        std::remove(staged.c_str());
    }
    active->ok = ok;
    active->error = !ok && !*error ? "Backup storage operation failed" : error;
    auto *done = active;
    active = nullptr;
    pending = false;
    xQueueSend(completed, &done, portMAX_DELAY);
  }
  pending_dirs.clear();
  prune_cursor.clear();
}
} // namespace
Snapshot::~Snapshot() {
  if (data && owns_data)
    heap_caps_free(data);
}
bool storage_init() {
  requests = xQueueCreate(1, sizeof(Snapshot *));
  completed = xQueueCreate(1, sizeof(Snapshot *));
  // A stale snapshot is disposable; source documents and the journal are
  // retained.
  std::remove(staged.c_str());
  staged_token = 0;
  tracker.init();
  return requests && completed;
}
bool transfer(Snapshot &job) {
  if (!requests)
    return false;
  Snapshot *pointer = &job;
  pending = true;
  if (xQueueSend(requests, &pointer, portMAX_DELAY) != pdTRUE) {
    pending = false;
    return false;
  }
  Snapshot *done = nullptr;
  return xQueueReceive(completed, &done, portMAX_DELAY) == pdTRUE &&
         done == pointer && job.ok;
}
void storage_cancel() {
  if (active && (active->kind == Snapshot::Kind::Prepare ||
                 active->kind == Snapshot::Kind::List))
    finish(false, "Documents changed; retry when idle");
}
bool storage_mark_dirty(const std::string &path) {
  const std::string prefix = root;
  if (path.rfind(prefix, 0) != 0 || !document_path(path.substr(prefix.size())))
    return true;
  storage_cancel();
  return tracker.mark(path.substr(prefix.size()));
}
bool storage_mark_tree_dirty() {
  storage_cancel();
  return tracker.force_rescan();
}
bool storage_pending() { return pending.load(); }
void storage_tick(bool dirty) {
  if (!requests)
    return;
  if (!active) {
    if (xQueueReceive(requests, &active, 0) != pdTRUE)
      return;
    using K = Snapshot::Kind;
    if (!tracker.init()) {
      finish(false, "Backup change journal unavailable");
      return;
    }
    if (active->kind == K::Clean) {
      bool ok =
          document_path(active->relative) &&
          tracker.clean(active->relative, active->revision, active->cleared);
      finish(ok, ok ? "" : "Could not checkpoint backup dirty state");
      return;
    }
    if (active->kind == K::Reconciled) {
      const bool ok = tracker.reconciled(active->edits, active->cleared);
      finish(ok, ok ? "" : "Could not checkpoint backup reconciliation");
      return;
    }
    if (active->kind == K::Release) {
      if (active->token == staged_token && staged_token) {
        if (stream_reader) {
          fclose(stream_reader);
          stream_reader = nullptr;
        }
        const bool ok = std::remove(staged.c_str()) == 0 || errno == ENOENT;
        staged_token = 0;
        finish(ok);
      } else
        finish(false, "Backup snapshot expired");
      return;
    }
    if (active->kind == K::Chunk) {
      if (!staged_token || active->token != staged_token ||
          active->size > 4096 || (active->size && !active->data) ||
          active->position > 4 * 1024 * 1024) {
        finish(false, "Invalid backup stream request");
        return;
      }
      if (!stream_reader)
        stream_reader = fopen(staged.c_str(), "rb");
      file = stream_reader;
      if (!file || (ftell(file) != long(active->position) &&
                    fseek(file, long(active->position), SEEK_SET) != 0)) {
        finish(false, "Backup snapshot read failed");
        return;
      }
    } else if (active->kind == K::List) {
      pending_dirs = {""};
      active->rescan = tracker.rescan();
      active->edits = tracker.edits();
    } else {
      const bool preparing = active->kind == K::Prepare;
      if (preparing && dirty) {
        finish(false, "Waiting for document autosave");
        return;
      }
      if ((!preparing && active->relative != index_file) ||
          (preparing && !document_path(active->relative))) {
        finish(false, "Invalid backup path");
        return;
      }
      const std::string path = std::string(root) + active->relative;
      if (active->kind == K::Read || preparing) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size < 0) {
          finish(false, "Backup file unavailable");
          return;
        }
        const size_t limit = preparing ? 4 * 1024 * 1024 : 512 * 1024;
        if (size_t(st.st_size) > limit) {
          finish(false, "Backup file too large");
          return;
        }
        active->size = st.st_size;
        active->mtime = st.st_mtime;
        if (preparing) {
          if (!tracker.track(active->relative)) {
            finish(false, "Could not persist backup dirty state");
            return;
          }
          active->revision = tracker.revision(active->relative);
          active->token = ++next_token;
          staged_token = 0;
          if (stream_reader) {
            fclose(stream_reader);
            stream_reader = nullptr;
          }
          copy = fopen(staged.c_str(), "wb");
          if (!copy) {
            finish(false, "Not enough storage for backup snapshot");
            return;
          }
        } else {
          active->data = static_cast<unsigned char *>(
              heap_caps_malloc(std::max<size_t>(1, active->size),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
          if (!active->data) {
            finish(false, "Not enough memory for backup metadata");
            return;
          }
        }
        file = fopen(path.c_str(), "rb");
      } else if (active->kind == K::Write && active->size <= 512 * 1024)
        file = fopen((path + ".tmp").c_str(), "wb");
      if (!file) {
        finish(false, "Backup state storage unavailable");
        return;
      }
    }
  }
  const int64_t deadline = esp_timer_get_time() + 2000;
  unsigned entries = 0;
  size_t bytes = 0;
  while (active && esp_timer_get_time() < deadline && entries < 32 &&
         bytes < 16384) {
    if (active->kind == Snapshot::Kind::List) {
      if (!directory) {
        if (pending_dirs.empty()) {
          std::string path;
          uint64_t revision = 0;
          if (tracker.next_dirty(prune_cursor, path, revision)) {
            prune_cursor = path;
            ++entries;
            struct stat status{};
            errno = 0;
            if (stat((std::string(root) + path).c_str(), &status) != 0 &&
                (errno == ENOENT || errno == ENOTDIR)) {
              bool cleared;
              if (!tracker.clean(path, revision, cleared)) {
                finish(false, "Could not prune obsolete backup dirty records");
                return;
              }
            }
            continue;
          }
          std::sort(active->files.begin(), active->files.end(),
                    [](const Document &a, const Document &b) {
                      return a.path < b.path;
                    });
          finish(true);
          return;
        }
        current_dir = std::move(pending_dirs.back());
        pending_dirs.pop_back();
        directory = opendir((std::string(root) + current_dir).c_str());
        if (!directory) {
          finish(false, "Document folder unavailable");
          return;
        }
      }
      errno = 0;
      auto *entry = readdir(directory);
      ++entries;
      if (!entry) {
        if (errno) {
          finish(false, "Document directory read failed");
          return;
        }
        closedir(directory);
        directory = nullptr;
        continue;
      }
      const std::string name = entry->d_name;
      if (name.empty() || name[0] == '.' || name == "recently_deleted")
        continue;
      const std::string relative =
          current_dir.empty() ? name : current_dir + "/" + name;
      if (!safe_relative(relative)) {
        finish(false, "Unsupported document path");
        return;
      }
      struct stat st{};
      if (stat((std::string(root) + relative).c_str(), &st) != 0) {
        finish(false, "Documents changed during scan");
        return;
      }
      if (S_ISDIR(st.st_mode)) {
        if (std::count(relative.begin(), relative.end(), '/') >= 16 ||
            pending_dirs.size() >= 128) {
          finish(false, "Too many backup folders");
          return;
        }
        pending_dirs.push_back(relative);
      } else if (S_ISREG(st.st_mode) && document_path(relative)) {
        if (active->files.size() >= 2048) {
          finish(false, "Backup supports up to 2048 documents");
          return;
        }
        active->files.push_back({relative, size_t(st.st_size),
                                 int64_t(st.st_mtime),
                                 tracker.dirty(relative)});
      }
    } else {
      const size_t count =
          std::min<size_t>(2048, active->size - active->offset);
      if (count) {
        unsigned char buffer[2048];
        const bool preparing = active->kind == Snapshot::Kind::Prepare;
        auto *destination = preparing ? buffer : active->data + active->offset;
        const size_t moved = active->kind == Snapshot::Kind::Write
                                 ? fwrite(destination, 1, count, file)
                                 : fread(destination, 1, count, file);
        if (moved != count ||
            (preparing && fwrite(buffer, 1, count, copy) != count)) {
          finish(false, "Backup storage read/write failed (check free space)");
          return;
        }
        active->offset += count;
        bytes += preparing ? count * 2 : count;
      }
      if (active->offset == active->size) {
        if (active->kind == Snapshot::Kind::Write) {
          if (fclose(file) != 0) {
            file = nullptr;
            finish(false, "Backup state write failed");
            return;
          }
          file = nullptr;
          const auto path = std::string(root) + index_file;
          if (!replace_saved_file(path + ".tmp", path)) {
            finish(false, "Backup state commit failed");
            return;
          }
        }
        finish(true);
        return;
      }
    }
  }
}
} // namespace leaf_backup
