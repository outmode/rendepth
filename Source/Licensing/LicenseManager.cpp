#include "LicenseManager.h"
#include <algorithm>
#include <cctype>
namespace Licensing {
// Combine product configuration, local license storage, and the licensing transport.
LicenseManager::LicenseManager(Config config, std::filesystem::path path, Transport transport)
    : config_(std::move(config)), storage_(std::move(path)), client_(std::move(transport)) {}
// Load the locally persisted activation state for offline use.
Result LicenseManager::load() const { return storage_.load(); }
// Validate and activate a key under the storage lock, rolling back the server slot if persistence
// fails.
Result LicenseManager::activate(std::string key) {
    if (!config_.configured()) return {Status::NotConfigured, "Pro activation is not available in this build yet."};
    auto first = key.find_first_not_of(" \t\r\n"), last = key.find_last_not_of(" \t\r\n");
    key = first == std::string::npos ? "" : key.substr(first, last - first + 1);
    if (key.empty() || key.size() > 256 || std::any_of(key.begin(), key.end(), [](unsigned char c) { return c < 33 || c > 126; }))
        return {Status::InvalidLicense, "Enter the license key from your purchase email."};
    if (storage_.path().empty()) return {Status::StorageError, "The license storage folder is unavailable."};
    std::string error;
    SettingsFile transaction(storage_.path(), error);
    if (!transaction.locked()) return {Status::StorageError, "Could not lock the license file. Check the folder permissions."};
    auto current = storage_.load();
    if (current.status == Status::Licensed || current.status == Status::StorageError) return current;
    // Check persistence before consuming a server-side computer slot.
    if (!storage_.save(transaction, {}, error)) return {Status::StorageError, "Could not save a license record. Check the folder permissions."};
    auto result = client_.activate(key);
    if (!result.ok) return result;
    const auto& record = result.record;
    const bool matches = record.key == key && record.storeId == config_.storeId && record.productId == config_.productId &&
        (!config_.variantId || record.variantId == config_.variantId);
    if (!matches) {
        // Release only the instance just created by this request, never another key.
        if (record.key == key) client_.deactivate(record);
        return {Status::InvalidLicense, "This key does not belong to Rendepth Pro. Check the purchase email or contact support."};
    }
    if (!storage_.save(transaction, record, error)) {
        const auto rollback = client_.deactivate(record);
        return {Status::StorageError, rollback.ok ?
            "Could not save activation. The new computer slot was released. Check folder permissions before trying again." :
            "Could not save activation or release its computer slot. Contact support to recover the slot before trying again."};
    }
    result.message = "Rendepth Pro is activated. This computer can now use its license offline indefinitely.";
    return result;
}
// Release the server activation and remove its local record only after successful deactivation.
Result LicenseManager::deactivate() {
    if (storage_.path().empty()) return {Status::StorageError, "The license storage folder is unavailable."};
    std::string error;
    SettingsFile transaction(storage_.path(), error);
    if (!transaction.locked()) return {Status::StorageError, "Could not lock the license file. Please try again."};
    auto current = storage_.load();
    if (current.status != Status::Licensed) return current;
    auto result = client_.deactivate(current.record);
    if (!result.ok) { result.record = current.record; return result; }
    if (!storage_.remove(transaction, error)) return {Status::StorageError,
        "The server released this computer, but its local record could not be removed. Contact support before activating again.", current.record};
    return {Status::NotActivated, "This computer has been deactivated. Its slot is available for another computer.", {}, true};
}
}
