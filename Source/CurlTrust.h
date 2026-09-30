#pragma once

#include <curl/curl.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <cstdint>
#include <filesystem>
#include <vector>

// The release app bundles Homebrew's OpenSSL-based libcurl, whose compiled-in
// CA path points back to the build machine. Use the CA bundle shipped in the
// app for every HTTPS request made by Rendepth.
inline bool configureCurlTrust(CURL* curl) {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> executablePath(size);
    if (_NSGetExecutablePath(executablePath.data(), &size) != 0) return false;
    const auto executableDirectory =
        std::filesystem::path(executablePath.data()).parent_path().lexically_normal();
    const auto contents = executableDirectory.parent_path();
    if (executableDirectory.filename() != "MacOS" ||
        contents.filename() != "Contents" ||
        contents.parent_path().extension() != ".app") return true;

    const auto certificates = contents / "Resources" / "cacert.pem";
    std::error_code error;
    if (!std::filesystem::is_regular_file(certificates, error)) return false;
    return curl_easy_setopt(curl, CURLOPT_CAINFO, certificates.string().c_str()) == CURLE_OK;
}
#else
inline bool configureCurlTrust(CURL*) { return true; }
#endif
