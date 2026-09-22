#include "LemonSqueezyClient.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace Licensing {
namespace {
constexpr size_t responseLimit = 65536;
// Percent-encode a form value before sending it to the licensing API.
std::string encode(const std::string& input) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
// Send a bounded licensing API request through the platform HTTP backend.
HttpResponse post(std::string_view endpoint, const std::string& form) {
    HttpResponse result;
#ifdef _WIN32
    // Close each Windows HTTP handle automatically when the request scope ends.
    struct Handle { HINTERNET value; ~Handle() { if (value) WinHttpCloseHandle(value); } };
    Handle session{WinHttpOpen(L"Rendepth/3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return result;
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 15000);
    Handle connection{WinHttpConnect(session.value, L"api.lemonsqueezy.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return result;
    std::wstring path(endpoint.begin(), endpoint.end());
    Handle request{WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return result;
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects);
    const wchar_t* headers = L"Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\n";
    if (!WinHttpSendRequest(request.value, headers, DWORD(-1), const_cast<char*>(form.data()),
            static_cast<DWORD>(form.size()), static_cast<DWORD>(form.size()), 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return result;
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return result;
    for (;;) {
        char buffer[4096]; DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof buffer, &read)) return {};
        if (!read) break;
        if (result.body.size() + read > responseLimit) return {};
        result.body.append(buffer, read);
    }
    result.status = static_cast<int>(status);
#else
    // Keep one process-lifetime reference so model downloads can initialize and
    // clean up their own curl reference without invalidating licensing requests.
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) return result;
    CURL* curl = curl_easy_init();
    if (!curl) return result;
    auto* headers = curl_slist_append(nullptr, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    const std::string url = "https://api.lemonsqueezy.com" + std::string(endpoint);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Rendepth/3.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* bytes, size_t size, size_t count, void* data) -> size_t {
        auto& body = *static_cast<std::string*>(data);
        if (size && count > (responseLimit - body.size()) / size) return 0;
        body.append(bytes, size * count); return size * count;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    if (curl_easy_perform(curl) == CURLE_OK) {
        long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        result.status = static_cast<int>(status);
    }
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
#endif
    return result;
}
// Read a bounded JSON string without accepting embedded null characters.
std::string string(const rapidjson::Value& object, const char* name) {
    if (!object.IsObject() || !object.HasMember(name) || !object[name].IsString()) return {};
    const auto& value = object[name];
    std::string text(value.GetString(), value.GetStringLength());
    if (text.size() > 4096 || text.find('\0') != std::string::npos) return {};
    return text;
}
// Read an unsigned identifier from a licensing response, defaulting to zero when absent or invalid.
std::uint64_t id(const rapidjson::Value& object, const char* name) {
    return object.IsObject() && object.HasMember(name) && object[name].IsUint64() ? object[name].GetUint64() : 0;
}
// Translate HTTP and JSON results into activation status and validate perpetual-license response
// fields.
Result parse(const HttpResponse& response, bool activation) {
    if (!response.status) return {Status::NetworkError,
        "Could not contact the licensing service. Check your connection. If activation was interrupted, contact support before repeated attempts."};
    if (response.status == 429) return {Status::ServerError, "Too many requests. Please wait a minute and try again."};
    if (response.status >= 500) return {Status::ServerError, "The licensing service is temporarily unavailable. Please try again later."};
    rapidjson::Document doc;
    if (response.body.size() > responseLimit || doc.Parse(response.body.data(), response.body.size()).HasParseError() || !doc.IsObject())
        return {Status::ServerError, "The licensing service returned an unreadable response. Please contact support if activation was interrupted."};
    const char* flag = activation ? "activated" : "deactivated";
    if (response.status < 200 || response.status >= 300 || !doc.HasMember(flag) || !doc[flag].IsBool() || !doc[flag].GetBool()) {
        auto error = string(doc, "error");
        std::transform(error.begin(), error.end(), error.begin(), [](unsigned char c) { return std::tolower(c); });
        if (error.find("activation limit") != std::string::npos)
            return {Status::ActivationLimitReached, "All computer slots are in use. Deactivate another computer, or contact support if it is no longer accessible."};
        // Do not display/log arbitrary server text: it may contain a license key.
        return {Status::InvalidLicense, activation ? "This key could not be activated. Check the key or contact support." :
            "Deactivation was not confirmed. Your local activation has been kept. Please try again or contact support."};
    }
    Result result; result.ok = true;
    if (!activation) return result;
    if (!doc.HasMember("license_key") || !doc.HasMember("instance") || !doc.HasMember("meta"))
        return {Status::ServerError, "The activation response is incomplete. Please contact support."};
    const auto& license = doc["license_key"];
    const auto& instance = doc["instance"];
    const auto& meta = doc["meta"];
    result.record = {string(license, "key"), string(instance, "id"), string(instance, "created_at"),
        id(meta, "store_id"), id(meta, "product_id"), id(meta, "variant_id")};
    if (!result.record.complete() || string(license, "status") != "active" ||
        !license.HasMember("expires_at") || !license["expires_at"].IsNull())
        return {Status::InvalidLicense, "A valid perpetual activation was not returned. Please contact support."};
    result.status = Status::Licensed;
    return result;
}
}
// Use an injected transport for tests or the real HTTP transport by default.
LemonSqueezyClient::LemonSqueezyClient(Transport transport) : transport_(transport ? std::move(transport) : post) {}
// Submit a license key for activation and interpret the service response.
Result LemonSqueezyClient::activate(const std::string& key) {
    return parse(transport_("/v1/licenses/activate", "license_key=" + encode(key) + "&instance_name=" + encode("Rendepth")), true);
}
// Release the activation instance described by the saved license record.
Result LemonSqueezyClient::deactivate(const Record& record) {
    return parse(transport_("/v1/licenses/deactivate", "license_key=" + encode(record.key) + "&instance_id=" + encode(record.instanceId)), false);
}
}
