#include "file_manager.h"
#include "backup/storage.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <ctime>
#include <cstdio>

std::string display_document_name(const std::string& filename, const std::string& folder) {
    if (folder.empty() && filename == "JustType - Leaf.txt") return "JustType | Leaf";
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".txt")
        return filename.substr(0, filename.size() - 4);
    return filename;
}

void ensure_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        #ifdef _WIN32
        mkdir(path.c_str());
        #else
        mkdir(path.c_str(), 0755);
        #endif
    }
}

void ensure_shortcuts_file(const std::string& user_files_dir) {
    std::string shortcuts_path = user_files_dir + "/Shortcuts.txt";
    
    std::ifstream test(shortcuts_path);
    if (test.good()) {
        test.close();
        return;
    }
    
    std::ofstream file(shortcuts_path);
    if (file) {
        file << "JustType Keyboard Shortcuts\n";
        file << "===========================\n\n";
        file << "Ctrl + F          Search in document\n";
        file << "Ctrl + T          Toggle dark/light theme\n";
        file << "Ctrl + P          Export as QR codes\n";
        file << "Ctrl + +          Increase font size\n";
        file << "Ctrl + -          Decrease font size\n";
        file << "Ctrl + Up         Jump to previous paragraph\n";
        file << "Ctrl + Down       Jump to next paragraph\n";
        file << "Alt + Up          Jump to top of document\n";
        file << "Alt + Down        Jump to bottom of document\n";
        file << "Ctrl + A          Select all\n";
        file << "Ctrl + Z          Undo\n";
        file << "Ctrl + Backspace  Delete word\n";
        file << "Meta (GUI key)    Toggle sidebar\n";
        file << "\n";
        file << "Search Mode:\n";
        file << "  Enter           Next match\n";
        file << "  Shift + Enter   Previous match\n";
        file << "  Escape          Close search\n";
        file.close();
    }
}

void cleanup_old_deleted_files(const std::string& recently_deleted_dir) {
    constexpr int MAX_AGE_DAYS = 30;
    
    DIR* dir = opendir(recently_deleted_dir.c_str());
    if (!dir) return;
    
    time_t now = time(nullptr);
    time_t max_age = MAX_AGE_DAYS * 24 * 60 * 60;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        std::string file_path = recently_deleted_dir + "/" + name;
        
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0) {
            time_t age = now - st.st_mtime;
            if (age > max_age) {
                std::remove(file_path.c_str());
            }
        }
    }
    
    closedir(dir);
}

void recover_saved_file(const std::string& path) {
    const std::string backup = path + ".bak";
    const std::string temporary = path + ".tmp";
    struct stat current, saved, pending;
    bool has_current = stat(path.c_str(), &current) == 0;
    bool has_backup = stat(backup.c_str(), &saved) == 0;
    if (!has_current && has_backup) {
        if (!leaf_backup::storage_mark_dirty(path)) return;
        if (std::rename(backup.c_str(), path.c_str()) == 0) { has_current = true; current = saved; }
    } else if (has_current && has_backup) {
        std::remove(backup.c_str());
    }
    // Earlier firmware left completed saves in .tmp when FAT refused replacement.
    // Recover these only when the original is empty, retaining the original as a backup.
    if (has_current && current.st_size == 0 &&
        stat(temporary.c_str(), &pending) == 0 && pending.st_size > 0) {
        if (!leaf_backup::storage_mark_dirty(path)) return;
        if (std::rename(path.c_str(), backup.c_str()) == 0) {
            if (std::rename(temporary.c_str(), path.c_str()) == 0) std::remove(backup.c_str());
            else std::rename(backup.c_str(), path.c_str());
        }
    }
}

bool replace_saved_file(const std::string& temporary, const std::string& path) {
    if (!leaf_backup::storage_mark_dirty(path)) return false;
    const std::string backup = path + ".bak";
    struct stat st;
    bool had_original = stat(path.c_str(), &st) == 0;
    if (had_original && std::rename(path.c_str(), backup.c_str()) != 0) return false;
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        if (had_original) std::rename(backup.c_str(), path.c_str());
        return false;
    }
    if (had_original) std::remove(backup.c_str());
    return true;
}

void recover_document_saves(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) return;
    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string path = directory + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) recover_document_saves(path);
        else if (name.size() > 4 && (name.substr(name.size() - 4) == ".tmp" || name.substr(name.size() - 4) == ".bak"))
            recover_saved_file(path.substr(0, path.size() - 4));
    }
    closedir(dir);
}
