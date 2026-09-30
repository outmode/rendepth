#pragma once
#include "LicenseTypes.h"
#include "../SettingsFile.h"
#include <filesystem>
namespace Licensing {
class LicenseStorage {
public:
    explicit LicenseStorage(std::filesystem::path path) : path_(std::move(path)) {}
    const std::filesystem::path& path() const { return path_; }
    Result load() const;
    bool save(SettingsFile& transaction, const Record& record, std::string& error) const;
    bool remove(SettingsFile& transaction, std::string& error) const;
private:
    std::filesystem::path path_;
};
}
