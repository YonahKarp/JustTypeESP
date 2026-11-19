#pragma once
#include "policy.h"
#include <cstdio>
#include <map>
#include <string>
#include <sys/stat.h>

namespace leaf_backup {
// UI-task owner. D is durable before a document mutation; C is durable only
// after checksum verification and a version-matching backup checkpoint.
class DirtyTracker {
  std::string file_;
  bool (*replace_)(const std::string &, const std::string &);
  std::map<std::string, uint64_t> dirty_;
  uint64_t ticket_ = 0, edits_ = 0;
  size_t bytes_ = 0;
  bool loaded_ = false, healthy_ = false, rescan_ = true;
  static std::string encode(const std::string &path) {
    std::string out;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : path) {
      out += hex[c >> 4];
      out += hex[c & 15];
    }
    return out;
  }
  static int nibble(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
  }
  static bool decode(const std::string &hex, std::string &out) {
    if (hex.size() % 2)
      return false;
    for (size_t i = 0; i < hex.size(); i += 2) {
      const int a = nibble(hex[i]), b = nibble(hex[i + 1]);
      if (a < 0 || b < 0)
        return false;
      out += char((a << 4) | b);
    }
    return document_path(out);
  }
  bool compact() {
    FILE *f = fopen((file_ + ".tmp").c_str(), "wb");
    if (!f)
      return false;
    size_t bytes = 0;
    auto put = [&](const std::string &s) {
      bytes += s.size();
      return fwrite(s.data(), 1, s.size(), f) == s.size();
    };
    bool ok = put("LEAFDIRTY1\n") && put(rescan_ ? "R\n" : "r\n");
    for (const auto &entry : dirty_)
      if (ok)
        ok = put("D " + encode(entry.first) + "\n");
    if (fclose(f) != 0)
      ok = false;
    if (ok)
      ok = replace_(file_ + ".tmp", file_);
    if (ok) {
      bytes_ = bytes;
      healthy_ = true;
    }
    return ok;
  }
  bool append(const std::string &record) {
    if (!init())
      return false;
    size_t live = 16;
    for (const auto &entry : dirty_)
      live += entry.first.size() * 2 + 3;
    if (bytes_ > 65536 && bytes_ > live * 2 && !compact())
      return false;
    FILE *f = fopen(file_.c_str(), "ab");
    if (!f) {
      healthy_ = false;
      rescan_ = true;
      return false;
    }
    bool ok = fwrite(record.data(), 1, record.size(), f) == record.size();
    if (fclose(f) != 0)
      ok = false;
    if (!ok) {
      healthy_ = false;
      rescan_ = true;
      return false;
    }
    bytes_ += record.size();
    return true;
  }

public:
  DirtyTracker(std::string file,
               bool (*replace)(const std::string &, const std::string &))
      : file_(std::move(file)), replace_(replace) {}
  bool init() {
    if (loaded_)
      return healthy_ || compact();
    loaded_ = true;
    FILE *f = fopen(file_.c_str(), "rb");
    if (!f)
      return compact();
    char line[1024];
    bool valid =
        fgets(line, sizeof(line), f) && std::string(line) == "LEAFDIRTY1\n";
    bytes_ = valid ? 11 : 0;
    while (valid && fgets(line, sizeof(line), f)) {
      const std::string record = line;
      bytes_ += record.size();
      if (record == "R\n")
        rescan_ = true;
      else if (record == "r\n")
        rescan_ = false;
      else {
        std::string path;
        valid = record.size() >= 4 && record[1] == ' ' &&
                record.back() == '\n' &&
                decode(record.substr(2, record.size() - 3), path);
        if (valid && record[0] == 'D')
          dirty_[path] = ++ticket_;
        else if (valid && record[0] == 'C')
          dirty_.erase(path);
        else
          valid = false;
      }
      if (dirty_.size() > 4096 || bytes_ > 4 * 1024 * 1024)
        valid = false;
    }
    if (ferror(f))
      valid = false;
    fclose(f);
    if (!valid) {
      rescan_ = true;
      return compact();
    }
    healthy_ = true;
    return true;
  }
  bool mark(const std::string &path, bool document_edit = true) {
    if (!document_path(path) || !init())
      return false;
    if (!dirty_.count(path) && dirty_.size() >= 4096)
      return false;
    if (!dirty_.count(path) && !append("D " + encode(path) + "\n"))
      return false;
    dirty_[path] = ++ticket_;
    if (document_edit)
      ++edits_;
    return true;
  }
  bool track(const std::string &path) {
    return dirty_.count(path) || mark(path, false);
  }
  uint64_t revision(const std::string &path) const {
    auto found = dirty_.find(path);
    return found == dirty_.end() ? 0 : found->second;
  }
  bool dirty(const std::string &path) const { return dirty_.count(path) != 0; }
  bool next_dirty(const std::string &after, std::string &path,
                  uint64_t &revision) const {
    auto found = dirty_.upper_bound(after);
    if (found == dirty_.end())
      return false;
    path = found->first;
    revision = found->second;
    return true;
  }
  bool rescan() const { return rescan_; }
  uint64_t edits() const { return edits_; }
  bool clean(const std::string &path, uint64_t revision, bool &cleared) {
    cleared = false;
    if (!init())
      return false;
    if (this->revision(path) != revision)
      return true;
    if (dirty_.count(path) && !append("C " + encode(path) + "\n"))
      return false;
    dirty_.erase(path);
    cleared = true;
    return true;
  }
  bool force_rescan() {
    if (!init())
      return false;
    if (!rescan_ && !append("R\n"))
      return false;
    rescan_ = true;
    ++edits_;
    return true;
  }
  bool reconciled(uint64_t edits, bool &current) {
    current = this->edits() == edits;
    if (!init())
      return false;
    if (current && rescan_) {
      if (!append("r\n"))
        return false;
      rescan_ = false;
    }
    return true;
  }
};
} // namespace leaf_backup
