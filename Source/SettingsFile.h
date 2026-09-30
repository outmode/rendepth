#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// Hold this transaction across reading the previous settings, merging explicit
// preferences, and writing. All instances use the same persistent lock file.
class SettingsFile {
public:
    SettingsFile(const std::filesystem::path& path, std::string& error);
    ~SettingsFile();
    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;
    bool locked() const { return locked_; }
    bool write(std::string_view contents, std::string& error);
private:
    std::filesystem::path path_;
    bool locked_ = false;
#ifdef _WIN32
    void* lock_ = nullptr;
#else
    int lock_ = -1;
#endif
};
