#include "CubeViCalibration.h"
#include "rapidjson/document.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

namespace CubeViCalibration {
namespace {
constexpr size_t maxFileSize = 64 * 1024;

bool decode(std::string_view encoded, std::string& decoded, std::string& error) {
#ifdef _WIN32
    // Public CubeVi SDK compatibility: CryptoJS/OpenSSL salted AES-256-CBC,
    // EVP_BytesToKey-style MD5 derivation, one iteration, PKCS#7 padding.
    // https://github.com/CubeVi/CubeVi-Swizzle-Unity/blob/main/Scripts/BatchCameraManager.cs
    constexpr char passphrase[] = "3f5e1a2b4c6d7e8f9a0b1c2d3e4f5a6b";
    DWORD size = 0;
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr)) {
        error = "invalid calibration Base64";
        return false;
    }
    std::vector<unsigned char> bytes(size);
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, bytes.data(), &size, nullptr, nullptr) ||
        size < 32 || (size - 16) % 16 != 0 || std::memcmp(bytes.data(), "Salted__", 8) != 0) {
        error = "invalid salted calibration envelope";
        return false;
    }
    struct Crypto {
        BCRYPT_ALG_HANDLE md5 = nullptr, aes = nullptr;
        BCRYPT_KEY_HANDLE key = nullptr;
        ~Crypto() {
            if (key) BCryptDestroyKey(key);
            if (aes) BCryptCloseAlgorithmProvider(aes, 0);
            if (md5) BCryptCloseAlgorithmProvider(md5, 0);
        }
    } crypto;
    error = "could not decrypt CubeVi calibration";
    if (BCryptOpenAlgorithmProvider(&crypto.md5, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0 ||
        BCryptOpenAlgorithmProvider(&crypto.aes, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
        return false;
    unsigned char derived[48]{};
    for (size_t offset = 0; offset < sizeof(derived); offset += 16) {
        std::vector<unsigned char> input;
        if (offset != 0) input.insert(input.end(), derived + offset - 16, derived + offset);
        input.insert(input.end(), passphrase, passphrase + sizeof(passphrase) - 1);
        input.insert(input.end(), bytes.begin() + 8, bytes.begin() + 16);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(crypto.md5, &hash, nullptr, 0, nullptr, 0, 0) < 0) return false;
        const bool ok = BCryptHashData(hash, input.data(), static_cast<ULONG>(input.size()), 0) >= 0 &&
            BCryptFinishHash(hash, derived + offset, 16, 0) >= 0;
        BCryptDestroyHash(hash);
        if (!ok) return false;
    }
    if (BCryptSetProperty(crypto.aes, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC), 0) < 0 ||
        BCryptGenerateSymmetricKey(crypto.aes, &crypto.key, nullptr, 0, derived, 32, 0) < 0)
        return false;
    std::vector<unsigned char> plain(size);
    ULONG written = 0;
    if (BCryptDecrypt(crypto.key, bytes.data() + 16, size - 16, nullptr,
            derived + 32, 16, plain.data(), static_cast<ULONG>(plain.size()),
            &written, BCRYPT_BLOCK_PADDING) < 0) return false;
    decoded.assign(reinterpret_cast<const char*>(plain.data()), written);
    error.clear();
    return true;
#else
    error = "encrypted CubeVi calibration is currently supported on Windows; use decoded JSON";
    return false;
#endif
}

bool number(const rapidjson::Value& value, const char* key, float& result) {
    if (!value.HasMember(key) || !value[key].IsNumber()) return false;
    const double candidate = value[key].GetDouble();
    result = static_cast<float>(candidate);
    return std::isfinite(candidate) && std::isfinite(result);
}
}

bool parse(std::string_view json, Optics& optics, std::string& error) {
    error.clear();
    if (json.empty() || json.size() > maxFileSize) {
        error = "empty or oversized calibration";
        return false;
    }
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()) {
        error = "invalid calibration JSON";
        return false;
    }
    if (document.HasMember("config") && document["config"].IsString()) {
        std::string decoded;
        if (!decode({document["config"].GetString(), document["config"].GetStringLength()},
                decoded, error)) return false;
        document.Parse(decoded.data(), decoded.size());
        if (document.HasParseError() || !document.IsObject()) {
            error = "invalid decoded calibration JSON";
            return false;
        }
    }
    const auto& value = document.HasMember("config") ? document["config"] : document;
    Optics loaded;
    if (!value.IsObject() || !number(value, "lineNumber", loaded.interval) ||
        !number(value, "obliquity", loaded.obliquity) ||
        !number(value, "deviation", loaded.deviation) || loaded.interval < 0.001f ||
        loaded.interval > 100000.0f || std::abs(loaded.obliquity) > 100.0f ||
        std::abs(loaded.deviation) > 100000.0f) {
        error = "missing or out-of-range CubeVi optical parameters";
        return false;
    }
    optics = loaded;
    return true;
}

bool load(const std::filesystem::path& path, Optics& optics, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > maxFileSize) {
        error = "calibration file is missing, empty or oversized";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    std::string json(static_cast<size_t>(size), '\0');
    if (!file.read(json.data(), static_cast<std::streamsize>(size))) {
        error = "could not read calibration file";
        return false;
    }
    return parse(json, optics, error);
}

std::filesystem::path find() {
    if (const char* requested = std::getenv("RENDEPTH_NATIVE_CALIBRATION")) return requested;
    if (const char* root = std::getenv("APPDATA")) {
        for (const char* folder : {"Cubestage", "OpenstageAI"}) {
            auto path = std::filesystem::path(root) / folder / "deviceConfig.json";
            std::error_code ec;
            if (std::filesystem::is_regular_file(path, ec)) return path;
        }
    }
    return {};
}
}
