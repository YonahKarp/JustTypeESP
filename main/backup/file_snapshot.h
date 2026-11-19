#pragma once
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "storage.h"
#include <algorithm>
#include <vector>
namespace leaf_backup {
class FileSnapshot {
  uint64_t token_ = 0;
  static std::string hex(const unsigned char *bytes, size_t size) {
    std::string result;
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
      result += digits[bytes[i] >> 4];
      result += digits[bytes[i] & 15];
    }
    return result;
  }

public:
  size_t size = 0;
  uint64_t revision = 0;
  int64_t mtime = 0;
  std::string sha, md5, error;
  FileSnapshot() = default;
  FileSnapshot(const FileSnapshot &) = delete;
  FileSnapshot &operator=(const FileSnapshot &) = delete;
  ~FileSnapshot() {
    if (token_) {
      Snapshot job(Snapshot::Kind::Release);
      job.token = token_;
      transfer(job);
    }
  }
  int read(size_t offset, unsigned char *buffer, size_t count) {
    if (offset > size || count > size - offset || count > 4096)
      return -1;
    Snapshot job(Snapshot::Kind::Chunk);
    job.token = token_;
    job.position = offset;
    job.size = count;
    job.data = buffer;
    job.owns_data = false;
    if (!transfer(job)) {
      error = job.error;
      return -1;
    }
    return int(count);
  }
  static int stream(void *context, size_t offset, unsigned char *buffer,
                    size_t count) {
    return static_cast<FileSnapshot *>(context)->read(offset, buffer, count);
  }
  bool prepare(const std::string &path) {
    Snapshot job(Snapshot::Kind::Prepare);
    job.relative = path;
    if (!transfer(job)) {
      error = job.error;
      return false;
    }
    token_ = job.token;
    size = job.size;
    revision = job.revision;
    mtime = job.mtime;
    mbedtls_sha256_context sha_ctx;
    mbedtls_md5_context md5_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_md5_init(&md5_ctx);
    bool ok = mbedtls_sha256_starts(&sha_ctx, 0) == 0 &&
              mbedtls_md5_starts(&md5_ctx) == 0;
    std::vector<unsigned char> buffer(4096);
    for (size_t offset = 0; ok && offset < size;) {
      const size_t count = std::min<size_t>(buffer.size(), size - offset);
      ok = read(offset, buffer.data(), count) == int(count) &&
           mbedtls_sha256_update(&sha_ctx, buffer.data(), count) == 0 &&
           mbedtls_md5_update(&md5_ctx, buffer.data(), count) == 0;
      offset += count;
    }
    unsigned char sha_bytes[32], md5_bytes[16];
    if (ok)
      ok = mbedtls_sha256_finish(&sha_ctx, sha_bytes) == 0 &&
           mbedtls_md5_finish(&md5_ctx, md5_bytes) == 0;
    mbedtls_sha256_free(&sha_ctx);
    mbedtls_md5_free(&md5_ctx);
    if (!ok) {
      if (error.empty())
        error = "Backup snapshot checksum failed";
      return false;
    }
    sha = hex(sha_bytes, sizeof(sha_bytes));
    md5 = hex(md5_bytes, sizeof(md5_bytes));
    return true;
  }
};
} // namespace leaf_backup
