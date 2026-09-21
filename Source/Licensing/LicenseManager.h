#pragma once
#include "LicenseStorage.h"
#include "LemonSqueezyClient.h"
namespace Licensing {
class LicenseManager {
public:
    LicenseManager(Config config, std::filesystem::path path, Transport transport = {});
    Result load() const;
    Result activate(std::string key);
    Result deactivate();
private:
    Config config_;
    LicenseStorage storage_;
    LemonSqueezyClient client_;
};
}
