#pragma once

#include <string>

void ensure_directory(const std::string& path);
void ensure_shortcuts_file(const std::string& user_files_dir);
void cleanup_old_deleted_files(const std::string& recently_deleted_dir);

// FAT does not permit a vertical bar in a filename. Present the welcome
// document with its intended title while keeping its on-disk name compatible.
std::string display_document_name(const std::string& filename, const std::string& folder);

// Recover interrupted saves, and replace a file on filesystems that cannot overwrite on rename.
void recover_saved_file(const std::string& path);
bool replace_saved_file(const std::string& temporary, const std::string& path);
void recover_document_saves(const std::string& directory);
