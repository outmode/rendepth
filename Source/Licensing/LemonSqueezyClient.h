#pragma once
#include "LicenseTypes.h"
#include <functional>
#include <string_view>
namespace Licensing {
struct HttpResponse { int status = 0; std::string body; };
using Transport = std::function<HttpResponse(std::string_view endpoint, const std::string& form)>;
class LemonSqueezyClient {
public:
    explicit LemonSqueezyClient(Transport transport = {});
    Result activate(const std::string& key);
    Result deactivate(const Record& record);
private:
    Transport transport_;
};
}
