#include "LicenseManager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <atomic>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace Licensing;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::string activated(int store = 1, int product = 2, int variant = 3) {
    return "{\"activated\":true,\"license_key\":{\"key\":\"test-key\",\"status\":\"active\",\"expires_at\":null},"
        "\"instance\":{\"id\":\"instance-123\",\"created_at\":\"2001-01-01T00:00:00Z\"},"
        "\"meta\":{\"store_id\":" + std::to_string(store) + ",\"product_id\":" + std::to_string(product) + ",\"variant_id\":" + std::to_string(variant) + "}}";
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("rendepth-license-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{root};
    try {
        int calls = 0; bool deactivate = false;
        HttpResponse reply{200, activated()};
        Transport transport = [&](std::string_view endpoint, const std::string& body) {
            ++calls; deactivate = endpoint == "/v1/licenses/deactivate";
            require(body.find("license_key=test-key") != std::string::npos, "key form");
            if (deactivate) {
                require(body.find("instance_id=instance-123") != std::string::npos, "instance form");
                return HttpResponse{200, "{\"deactivated\":true}"};
            }
            require(body.find("instance_name=Rendepth") != std::string::npos, "installation label");
            return reply;
        };
        Config config{1,2,3};
        const auto path = root / "License.json";
        LicenseManager manager(config, path, transport);
        require(manager.load().status == Status::NotActivated && calls == 0, "missing record offline");
        require(manager.activate(" \n test-key\t ").ok && calls == 1, "valid activation");
        require(manager.load().status == Status::Licensed && calls == 1, "old activation stays licensed offline");
        require(manager.activate("test-key").ok && calls == 1, "no duplicate slot");
#ifndef _WIN32
        struct stat st{}; require(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "private record permissions");
#endif
        LicenseManager migrated({}, path, [&](auto, auto) -> HttpResponse { throw std::runtime_error("startup network"); });
        require(migrated.load().status == Status::Licensed, "provider configuration changes preserve offline activation");
        LicenseManager offline(config, path, [](auto, auto) { return HttpResponse{}; });
        require(offline.deactivate().status == Status::NetworkError && offline.load().status == Status::Licensed, "failed deactivation preserves record");
        require(manager.deactivate().ok && deactivate && !std::filesystem::exists(path), "confirmed deactivation removes record");
        const int before = calls;
        require(manager.load().status == Status::NotActivated && calls == before, "offline load after deactivate");
        LicenseManager unconfigured({}, path, transport);
        require(unconfigured.activate("test-key").status == Status::NotConfigured && calls == before, "missing product configuration");
        for (auto response : {activated(8), activated(1,8), activated(1,2,8)}) {
            reply = {200, response};
            auto r = manager.activate("test-key");
            require(r.status == Status::InvalidLicense && deactivate && manager.load().status != Status::Licensed, "wrong product cleanup");
        }
        reply = {400, "{\"activated\":false,\"error\":\"This license key has reached the activation limit.\"}"};
        require(manager.activate("test-key").status == Status::ActivationLimitReached, "limit error");
        reply = {422, "{\"activated\":false,\"error\":\"Invalid test-key\"}"};
        auto invalid = manager.activate("test-key");
        require(invalid.status == Status::InvalidLicense && invalid.message.find("test-key") == std::string::npos, "no server key disclosure");
        reply = {500, "server error"}; require(manager.activate("test-key").status == Status::ServerError, "server failure");
        reply = {429, ""}; require(manager.activate("test-key").status == Status::ServerError, "rate limit");
        reply = {}; require(manager.activate("test-key").status == Status::NetworkError, "offline activation");
        for (const auto& malformed : {"[]", "not json", "{\"activated\":true}", "{\"activated\":1}"}) {
            reply = {200, malformed}; require(!manager.activate("test-key").ok, "malformed response rejected");
        }
        reply = {200, activated()};
        require(manager.activate("test-key").ok, "reactivate");
        LicenseManager notConfirmed(config, path, [](auto, auto) { return HttpResponse{200,"{\"deactivated\":false}"}; });
        require(!notConfirmed.deactivate().ok && manager.load().status == Status::Licensed, "HTTP success alone cannot deactivate");
        std::ofstream(path) << "{\"licensed\":true}";
        require(manager.load().status == Status::NotActivated, "trivial record rejected");
        std::ofstream(path) << "{broken";
        require(manager.load().status == Status::NotActivated, "corrupt record handled");
        // Two running copies must not consume separate slots for the same record.
        std::atomic<int> concurrentCalls{0};
        auto sharedTransport = [&](auto, auto) {
            ++concurrentCalls; return HttpResponse{200, activated()};
        };
        LicenseManager copyOne(config, root / "shared", sharedTransport);
        LicenseManager copyTwo(config, root / "shared", sharedTransport);
        auto one = std::async(std::launch::async, [&] { return copyOne.activate("test-key"); });
        auto two = std::async(std::launch::async, [&] { return copyTwo.activate("test-key"); });
        require(one.get().ok && two.get().ok && concurrentCalls == 1, "concurrent copies use one activation slot");
        // Simulate the seller's five-computer limit and manual support reset.
        int slots = 0;
        auto limited = [&](std::string_view endpoint, const std::string&) -> HttpResponse {
            if (endpoint.ends_with("deactivate")) { --slots; return {200,"{\"deactivated\":true}"}; }
            if (slots == 5) return {400,"{\"activated\":false,\"error\":\"activation limit\"}"};
            ++slots; return {200,activated()};
        };
        for (int i = 0; i < 5; ++i) {
            LicenseManager computer(config, root / ("computer"+std::to_string(i)), limited);
            require(computer.activate("test-key").ok, "first five computers succeed");
        }
        LicenseManager sixth(config, root / "sixth", limited);
        require(sixth.activate("test-key").status == Status::ActivationLimitReached, "sixth rejected");
        --slots; require(sixth.activate("test-key").ok, "support reset allows replacement");
        // Persistence failure after a successful server activation must release its slot.
        const auto failurePath = root / "failure";
        bool rolledBack = false;
        LicenseManager failing(config, failurePath, [&](std::string_view endpoint, const std::string&) -> HttpResponse {
            if (endpoint.ends_with("deactivate")) { rolledBack = true; return {200,"{\"deactivated\":true}"}; }
            std::filesystem::remove(failurePath); std::filesystem::create_directory(failurePath);
            return {200, activated()};
        });
        require(failing.activate("test-key").status == Status::StorageError && rolledBack, "persistence failure rolls back slot");
        std::cout << "PASS: activation, offline trust, metadata, errors, storage, deactivation, computer limit and recovery\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
