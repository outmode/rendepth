// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "RuntimePackDownloader.h"

#ifdef __linux__
#include <curl/curl.h>
#include <openssl/evp.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <linux/fs.h>
#include <memory>
#include <spawn.h>
#include <signal.h>
#include <string>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {
using RuntimePackDownloader::Pack;
using RuntimePackDownloader::Progress;
using RuntimePackDownloader::Stage;

struct PackInfo {
    const char* directory;
    const char* filename;
    const char* sha256;
};

// These are release inputs, not hashes supplied by the download server.
constexpr PackInfo rocmPack{
    "rocm", "rendepth-rocm-linux-x64-ort1.22.2-rocm7.1.1.tar.gz",
    "ee446200d262beabc432acd207efbcbc598620986b3e5aaef27a767ff76fd2fd"
};
constexpr PackInfo cudaPack{
    "cuda", "rendepth-cuda-linux-x64-ort1.22.0-cuda12-cudnn9.tar.gz",
    "a365a3a2668041d78722eb9883555c83ef3f7278680e891681970cc3e6949dd3"
};
const PackInfo& infoFor(Pack pack) { return pack == Pack::CUDA ? cudaPack : rocmPack; }

struct Transfer {
    const std::atomic<bool>& cancel;
    Progress* progress;
    std::function<bool(const char*, size_t)> consume;
};

size_t receive(char* bytes, size_t size, size_t count, void* opaque) {
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (transfer.cancel.load() || (size && count > SIZE_MAX / size)) return 0;
    const size_t length = size * count;
    return transfer.consume(bytes, length) ? length : 0;
}

int reportProgress(void* opaque, curl_off_t total, curl_off_t, curl_off_t, curl_off_t) {
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (transfer.progress && total > 0)
        transfer.progress->total = static_cast<std::uint64_t>(total);
    return transfer.cancel.load() ? 1 : 0;
}

bool request(const std::string& filename, Transfer& transfer, std::string& error) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) { error = "Could not start HTTPS connection."; return false; }
    const std::string url = "https://rendepth.com/packs/" + filename;
    std::array<char, CURL_ERROR_SIZE> curlError{};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Rendepth/3.0");
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, curlError.data());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    const CURLcode result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) {
        if (error.empty()) error = transfer.cancel.load() ? "GPU pack download cancelled." :
            "Could not download the GPU pack from rendepth.com: " +
            std::string(curlError[0] ? curlError.data() : curl_easy_strerror(result));
        return false;
    }
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) {
        error = "GPU pack download returned HTTP " + std::to_string(status) + ".";
        return false;
    }
    return true;
}

bool readChecksum(const PackInfo& info, const std::atomic<bool>& cancel, std::string& error) {
    std::string sidecar;
    Transfer transfer{cancel, nullptr, [&](const char* bytes, size_t length) {
        if (length > 256 - sidecar.size()) {
            error = "GPU pack checksum file is too large.";
            return false;
        }
        sidecar.append(bytes, length);
        return true;
    }};
    if (!request(std::string(info.filename) + ".sha256", transfer, error)) return false;
    const std::string expected = std::string(info.sha256) + "  " + info.filename + "\n";
    if (sidecar != expected && sidecar != expected.substr(0, expected.size() - 1) + "\r\n") {
        error = "GPU pack checksum file does not match this Rendepth release.";
        return false;
    }
    return true;
}

bool downloadArchive(const PackInfo& info, const std::filesystem::path& destination,
    const std::atomic<bool>& cancel, Progress& progress, std::string& error) {
    std::ofstream output(destination, std::ios::binary);
    if (!output) { error = "Could not create temporary GPU pack archive."; return false; }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1) {
        error = "Could not start SHA-256 verification.";
        return false;
    }
    constexpr std::uint64_t maxArchiveBytes = 5ull * 1024 * 1024 * 1024;
    progress.stage = Stage::Download;
    progress.received = 0;
    progress.total = 0;
    Transfer transfer{cancel, &progress, [&](const char* bytes, size_t length) {
        const auto received = progress.received.load();
        if (length > maxArchiveBytes - received) {
            error = "GPU pack archive is too large.";
            return false;
        }
        output.write(bytes, static_cast<std::streamsize>(length));
        if (!output) { error = "Could not write downloaded GPU pack."; return false; }
        if (EVP_DigestUpdate(hash.get(), bytes, length) != 1) {
            error = "Could not verify downloaded GPU pack.";
            return false;
        }
        progress.received = received + length;
        return true;
    }};
    const bool downloaded = request(info.filename, transfer, error);
    output.close();
    if (!downloaded) return false;
    if (!output) { error = "Could not finish writing GPU pack."; return false; }
    if (progress.total != 0 && progress.received != progress.total) {
        error = "GPU pack download is incomplete.";
        return false;
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned digestLength = 0;
    if (EVP_DigestFinal_ex(hash.get(), digest.data(), &digestLength) != 1 || digestLength != 32) {
        error = "Could not finish GPU pack verification.";
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    for (unsigned i = 0; i < digestLength; ++i) {
        actual += hex[digest[i] >> 4];
        actual += hex[digest[i] & 15];
    }
    if (actual != info.sha256) {
        error = "GPU pack SHA-256 does not match the published archive.";
        return false;
    }
    return true;
}

bool extractArchive(const PackInfo& info, const std::filesystem::path& archive,
    const std::filesystem::path& stage, const std::atomic<bool>& cancel, std::string& error) {
    const std::string archiveName = archive.string();
    const std::string stageName = stage.string();
    char* arguments[] = {const_cast<char*>("tar"), const_cast<char*>("-xzf"),
        const_cast<char*>(archiveName.c_str()), const_cast<char*>("-C"),
        const_cast<char*>(stageName.c_str()), const_cast<char*>("--no-same-owner"),
        const_cast<char*>("--no-same-permissions"), nullptr};
    pid_t child = 0;
    const int spawnError = posix_spawnp(&child, "tar", nullptr, nullptr, arguments, environ);
    if (spawnError != 0) {
        error = "Could not start GPU pack extraction: " + std::string(std::strerror(spawnError));
        return false;
    }
    int status = 0;
    while (true) {
        if (cancel.load()) kill(child, SIGTERM);
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0 && errno != EINTR) {
            error = "Could not finish GPU pack extraction.";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (cancel.load()) { error = "GPU pack installation cancelled."; return false; }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "GPU pack extraction failed.";
        return false;
    }
    const auto pack = stage / info.directory;
    const auto lib = pack / "lib";
    if (!std::filesystem::is_regular_file(pack / "pack.json") ||
        !std::filesystem::is_regular_file(lib / "libonnxruntime.so.1") ||
        !std::filesystem::is_regular_file(lib / "libonnxruntime_providers_shared.so") ||
        !std::filesystem::is_regular_file(lib /
            (std::string("libonnxruntime_providers_") + info.directory + ".so"))) {
        error = "Downloaded GPU pack is missing required runtime files.";
        return false;
    }
    return true;
}
}

namespace RuntimePackDownloader {
bool install(Pack pack, const std::filesystem::path& packsRoot,
    const std::atomic<bool>& cancel, Progress& progress, std::string& error) {
    const auto& info = infoFor(pack);
    if (packsRoot.empty()) { error = "GPU pack folder is unavailable."; return false; }
    std::error_code filesystemError;
    if (std::filesystem::exists(packsRoot / info.directory, filesystemError) || filesystemError) {
        error = "A GPU pack folder already exists or cannot be checked. Move it before retrying.";
        return false;
    }
    progress.stage = Stage::Checksum;
    if (!readChecksum(info, cancel, error)) return false;
    std::filesystem::create_directories(packsRoot, filesystemError);
    if (filesystemError) { error = "Could not create GPU pack folder: " + filesystemError.message(); return false; }
    const auto stage = packsRoot / (std::string(".") + info.directory + ".install-" +
        std::to_string(getpid()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(stage, filesystemError)) {
        error = "Could not create temporary GPU pack folder.";
        return false;
    }
    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(stage, ignored); };
    const auto archive = stage / "pack.tar.gz";
    if (!downloadArchive(info, archive, cancel, progress, error)) { cleanup(); return false; }
    progress.stage = Stage::Extract;
    if (!extractArchive(info, archive, stage, cancel, error)) { cleanup(); return false; }
    const auto destination = packsRoot / info.directory;
    if (syscall(SYS_renameat2, AT_FDCWD, (stage / info.directory).c_str(),
            AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) != 0) {
        error = "Could not install GPU pack: " + std::string(std::strerror(errno));
        cleanup();
        return false;
    }
    progress.stage = Stage::Complete;
    cleanup();
    return true;
}
}
#endif
