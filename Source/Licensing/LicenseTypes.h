#pragma once
#include <cstdint>
#include <string>

namespace Licensing {
enum class Status { NotActivated, Licensed, InvalidLicense, ActivationLimitReached,
    NetworkError, ServerError, StorageError, NotConfigured };
struct Config {
    std::uint64_t storeId = 0, productId = 0, variantId = 0;
    std::string purchaseUrl, supportUrl;
    bool configured() const { return storeId && productId; }
};
struct Record {
    std::string key, instanceId, activatedAt;
    std::uint64_t storeId = 0, productId = 0, variantId = 0;
    bool complete() const {
        return !key.empty() && !instanceId.empty() && !activatedAt.empty() && storeId && productId;
    }
};
struct Result {
    Status status = Status::NotActivated;
    std::string message;
    Record record;
    bool ok = false;
};
}
