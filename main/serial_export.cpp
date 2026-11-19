#include "serial_export.h"
#include "backup/backup.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace serial_export {
namespace {
bool ready = false;
std::string command;
std::string token;
std::string hex(const unsigned char* bytes, size_t size) {
    const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
void frame(const std::string& message) {
    const std::string line = "\n@JT " + token + " " + message + "\n";
    uart_write_bytes(UART_NUM_0, line.data(), line.size());
}
bool walk(const std::string& root, const std::string& relative, unsigned depth) {
    if (depth > 16) return false;
    const std::string directory = root + (relative.empty() ? "" : "/" + relative);
    DIR* dir = opendir(directory.c_str());
    if (!dir) return false;
    bool ok = true;
    while (auto* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name == "recently_deleted") continue;
        const std::string rel = relative.empty() ? name : relative + "/" + name;
        const std::string path = root + "/" + rel;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) {
            if (!walk(root, rel, depth + 1)) { ok = false; break; }
            continue;
        }
        if (!S_ISREG(st.st_mode) || name.size() < 4 || name.substr(name.size() - 4) != ".txt") continue;
        FILE* input = fopen(path.c_str(), "rb");
        if (!input) { ok = false; break; }
        frame("FILE " + hex(reinterpret_cast<const unsigned char*>(rel.data()), rel.size()) + " " + std::to_string(st.st_size));
        unsigned char buffer[256];
        uint32_t crc = 0xffffffff;
        size_t total = 0;
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
            for (size_t i = 0; i < count; ++i) {
                crc ^= buffer[i];
                for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
            }
            frame("DATA " + std::to_string(total) + " " + hex(buffer, count));
            total += count;
        }
        ok = !ferror(input) && total == static_cast<size_t>(st.st_size);
        fclose(input);
        if (!ok) break;
        frame("END " + std::to_string(crc ^ 0xffffffff));
    }
    closedir(dir);
    return ok;
}
}
bool init() {
    ready = uart_driver_install(UART_NUM_0, 1024, 2048, 0, nullptr, 0) == ESP_OK;
    if (!ready) ESP_LOGE("serial_export", "USB serial export unavailable");
    return ready;
}
bool requested() {
    if (!ready) return false;
    unsigned char ch;
    while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
        if (ch == '\n') {
            if (command == "JTBACKUP") {
                command.clear();
                backup::request_now();
                ESP_LOGI("leaf_backup", "Manual backup requested over USB");
                continue;
            }
            if (command == "JTWIFITEST") {
                command.clear();
                ESP_LOGI("leaf_wifi", "%s", backup::test_saved_wifi()
                    ? "Testing saved Wi-Fi credentials"
                    : "Wi-Fi test unavailable; configure Wi-Fi or wait for current test");
                continue;
            }
            const bool valid = command.size() == 25 && command.substr(0, 9) == "JTEXPORT ";
            if (valid) {
                token = command.substr(9);
                for (char c : token) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { command.clear(); return false; }
            }
            command.clear();
            if (valid) return true;
        } else if (ch != '\r') {
            if (command.size() < 80) command += static_cast<char>(ch);
            else command.clear();
        }
    }
    return false;
}
void send_documents(const char* root, bool saved) {
    frame("BEGIN");
    if (!saved) frame("ERROR Save_failed");
    else if (!walk(root, "", 0)) frame("ERROR Read_failed");
    else frame("DONE");
    uart_wait_tx_done(UART_NUM_0, portMAX_DELAY);
}
}
