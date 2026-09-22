#include "LicenseStorage.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <fstream>
namespace Licensing {
// Validate and load the persisted activation record without requiring an online license check.
Result LicenseStorage::load() const {
    std::error_code ec;
    if (path_.empty()) return {Status::StorageError, "The license storage folder is unavailable."};
    const bool exists = std::filesystem::exists(path_, ec);
    if (ec) return {Status::StorageError, "Could not access the saved license."};
    if (!exists) return {};
    std::ifstream file(path_, std::ios::binary);
    if (!file) return {Status::StorageError, "Could not read the saved license."};
    std::string bytes(16385, '\0'); file.read(bytes.data(), bytes.size()); bytes.resize(file.gcount());
    rapidjson::Document doc;
    const auto corrupt = Result{Status::NotActivated, "The saved activation record is damaged. Re-enter your key, or contact support to recover the old computer slot."};
    if (bytes.size() > 16384 || doc.Parse(bytes.data(), bytes.size()).HasParseError() || !doc.IsObject() ||
        !doc.HasMember("schema") || !doc["schema"].IsInt() || doc["schema"].GetInt() != 1) return corrupt;
    const auto get = [&](const char* name) -> std::string {
        if (!doc.HasMember(name) || !doc[name].IsString()) return {};
        std::string value(doc[name].GetString(), doc[name].GetStringLength());
        return value.size() <= 4096 && value.find('\0') == std::string::npos ? value : std::string{};
    };
    if (get("state") == "inactive") return {};
    if (get("provider") != "lemonsqueezy" || get("state") != "activated") return corrupt;
    const auto id = [&](const char* name) -> std::uint64_t {
        return doc.HasMember(name) && doc[name].IsUint64() ? doc[name].GetUint64() : 0;
    };
    Record record{get("license_key"), get("instance_id"), get("activated_at"), id("store_id"), id("product_id"), id("variant_id")};
    if (!record.complete()) return corrupt;
    // Offline trust deliberately does not depend on current product IDs, time,
    // hardware, or provider availability. Keep old activations across migration.
    return {Status::Licensed, "Rendepth Pro is activated on this computer.", std::move(record), true};
}
// Serialize activation state through an already locked settings transaction.
bool LicenseStorage::save(SettingsFile& transaction, const Record& record, std::string& error) const {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
    json.StartObject(); json.Key("schema"); json.Int(1);
    json.Key("provider"); json.String("lemonsqueezy");
    json.Key("state"); json.String(record.complete() ? "activated" : "inactive");
    if (record.complete()) {
        const auto text = [&](const char* key, const std::string& value) {
            json.Key(key); json.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        };
        text("license_key", record.key); text("instance_id", record.instanceId); text("activated_at", record.activatedAt);
        json.Key("store_id"); json.Uint64(record.storeId);
        json.Key("product_id"); json.Uint64(record.productId);
        json.Key("variant_id"); json.Uint64(record.variantId);
    }
    json.EndObject();
    return transaction.write({buffer.GetString(), buffer.GetSize()}, error);
}
// Persist an inactive marker before removing the file so failed deletion cannot revive activation.
bool LicenseStorage::remove(SettingsFile& transaction, std::string& error) const {
    // Persist an inactive marker first, so a failed unlink cannot revive Pro.
    if (!save(transaction, {}, error)) return false;
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    return true; // An inactive marker is harmless if deletion was unavailable.
}
}
