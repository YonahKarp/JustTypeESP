#pragma once
#include <algorithm>
#include <cctype>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace quick_files {
struct Entry {
    std::string path;
    time_t modified = 0;
};
inline std::string folded(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// Read names and dates only, including nested folders; never read document content.
inline bool scan(const std::string& root, std::vector<Entry>& entries,
                 const std::string& relative = {}, unsigned depth = 0) {
    if (depth > 16) return false;
    const std::string directory = root + (relative.empty() ? "" : "/" + relative);
    DIR* dir = opendir(directory.c_str());
    if (!dir) return false;
    bool ok = true;
    while (auto* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name.empty() || name[0] == '.' || name == "recently_deleted") continue;
        const std::string path = relative.empty() ? name : relative + "/" + name;
        struct stat st {};
        if (stat((root + "/" + path).c_str(), &st) != 0) { ok = false; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (!scan(root, entries, path, depth + 1)) ok = false;
        } else if (S_ISREG(st.st_mode) && name.size() > 4 && folded(name.substr(name.size() - 4)) == ".txt") {
            entries.push_back({path, st.st_mtime});
        }
    }
    closedir(dir);
    return ok;
}
inline void sort_recent(std::vector<Entry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        const auto left = folded(a.path), right = folded(b.path);
        return left == right ? a.path < b.path : left < right;
    });
}
inline std::vector<size_t> filter(const std::vector<Entry>& entries, const std::string& query) {
    const auto needle = folded(query);
    std::vector<size_t> matches;
    for (size_t i = 0; i < entries.size(); ++i)
        if (folded(entries[i].path).find(needle) != std::string::npos) matches.push_back(i);
    return matches;
}
inline std::string new_name(unsigned n) {
    return std::string("New File") + (n ? " " + std::to_string(n) : "") + ".txt";
}
} // namespace quick_files
