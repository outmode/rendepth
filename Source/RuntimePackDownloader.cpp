// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "RuntimePackDownloader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <unzip.h>
#include <iowin32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace {
using RuntimePackDownloader::Pack;
using RuntimePackDownloader::Progress;
using RuntimePackDownloader::Stage;

struct PackInfo {
    const char* directory;
    const wchar_t* filename;
    const char* sha256;
};

constexpr PackInfo cudaPack{
    "cuda", L"rendepth-windows-x64-cuda-ort1.22.1-cuda12.9.2-cudnn9.24.zip",
    "4a2dba5e7a8817730cc3b9d7383920bc56c611115246db6afaf8556d9a8a376d"
};
constexpr PackInfo directmlPack{
    "directml", L"rendepth-windows-x64-directml-ort1.22.1-directml1.15.4.zip",
    "8fd765723b406222339f02d66257751f6ffc7241ee8d504c726aec4a273ca653"
};

const PackInfo& infoFor(Pack pack) { return pack == Pack::CUDA ? cudaPack : directmlPack; }

struct HttpCloser { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using HttpHandle = std::unique_ptr<void, HttpCloser>;

bool openRequest(const std::wstring& resource, HttpHandle& session,
    HttpHandle& connection, HttpHandle& request, std::string& error) {
    session.reset(WinHttpOpen(L"Rendepth/3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { error = "Could not start HTTPS connection."; return false; }
    WinHttpSetTimeouts(session.get(), 20000, 20000, 20000, 30000);
    connection.reset(WinHttpConnect(session.get(), L"rendepth.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) { error = "Could not connect to rendepth.com."; return false; }
    request.reset(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) { error = "Could not create pack download request."; return false; }
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) {
        error = "Could not secure GPU pack redirect policy.";
        return false;
    }
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) {
        error = "Could not download the GPU pack from rendepth.com (Windows error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) {
        error = "GPU pack download returned HTTP " + std::to_string(status) + ".";
        return false;
    }
    return true;
}

bool readResponse(HINTERNET request, const std::atomic<bool>& cancel,
    const std::function<bool(const char*, DWORD)>& consume, std::string& error) {
    std::array<char, 64 * 1024> buffer{};
    while (!cancel.load()) {
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received)) {
            error = "GPU pack download stopped (Windows error " + std::to_string(GetLastError()) + ").";
            return false;
        }
        if (received == 0) return true;
        if (!consume(buffer.data(), received)) return false;
    }
    error = "GPU pack download cancelled.";
    return false;
}

bool readChecksum(const PackInfo& info, const std::atomic<bool>& cancel, std::string& error) {
    HttpHandle session, connection, request;
    if (!openRequest(std::wstring(L"/packs/") + info.filename + L".sha256",
            session, connection, request, error)) return false;
    std::string sidecar;
    if (!readResponse(request.get(), cancel, [&](const char* bytes, DWORD count) {
            if (sidecar.size() + count > 256) { error = "GPU pack checksum file is too large."; return false; }
            sidecar.append(bytes, count);
            return true;
        }, error)) return false;
    std::string filename;
    for (const wchar_t* character = info.filename; *character; ++character)
        filename += static_cast<char>(*character); // Versioned archive names are ASCII.
    const std::string expected = std::string(info.sha256) + "  " + filename + "\n";
    if (sidecar != expected && sidecar != expected.substr(0, expected.size() - 1) + "\r\n") {
        error = "GPU pack checksum file does not match this Rendepth release.";
        return false;
    }
    return true;
}

bool downloadArchive(const PackInfo& info, const std::filesystem::path& destination,
    const std::atomic<bool>& cancel, Progress& progress, std::string& error) {
    HttpHandle session, connection, request;
    if (!openRequest(std::wstring(L"/packs/") + info.filename,
            session, connection, request, error)) return false;
    std::ofstream output(destination, std::ios::binary);
    if (!output) { error = "Could not create temporary GPU pack archive."; return false; }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, lengthSize = 0;
    std::vector<UCHAR> object;
    bool hashing = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength), &lengthSize, 0) == 0;
    if (hashing) {
        object.resize(objectLength);
        hashing = BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
            nullptr, 0, 0) == 0;
    }
    if (!hashing) { error = "Could not start SHA-256 verification."; }
    progress.stage = Stage::Download;
    progress.received = 0;
    wchar_t contentLength[64]{};
    DWORD contentLengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX, contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        try { progress.total = std::stoull(contentLength); } catch (...) { progress.total = 0; }
    }
    constexpr std::uint64_t maxArchiveBytes = 5ull * 1024 * 1024 * 1024;
    if (progress.total > maxArchiveBytes) { hashing = false; error = "GPU pack archive is too large."; }
    bool downloaded = hashing && readResponse(request.get(), cancel, [&](const char* bytes, DWORD count) {
        const auto received = progress.received.load();
        if (received + count > maxArchiveBytes) { error = "GPU pack archive is too large."; return false; }
        output.write(bytes, count);
        if (!output) { error = "Could not write downloaded GPU pack."; return false; }
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)), count, 0) != 0) {
            error = "Could not verify downloaded GPU pack."; return false;
        }
        progress.received = received + count;
        return true;
    }, error);
    output.close();
    if (!output && downloaded) { downloaded = false; error = "Could not finish writing GPU pack."; }
    std::array<UCHAR, 32> digest{};
    if (downloaded && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        downloaded = false; error = "Could not finish GPU pack verification.";
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!downloaded) return false;
    if (progress.total != 0 && progress.received != progress.total) {
        error = "GPU pack download is incomplete."; return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    for (UCHAR byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
    if (actual != info.sha256) {
        error = "GPU pack SHA-256 does not match the published archive."; return false;
    }
    return true;
}

bool safeEntry(const std::string& name, const std::string& root, bool directory) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        return false;
    if (name != root + "/" && name.rfind(root + "/", 0) != 0) return false;
    if (directory != (name.back() == '/')) return false;
    size_t start = 0;
    while (start < name.size()) {
        const size_t end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." ||
            !std::all_of(part.begin(), part.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-' || c == '.';
            })) return false;
        if (end == std::string::npos || end == name.size() - 1) break;
        start = end + 1;
    }
    return true;
}

bool extractArchive(const PackInfo& info, const std::filesystem::path& archivePath,
    const std::filesystem::path& stage, const std::atomic<bool>& cancel, std::string& error) {
    zlib_filefunc64_def fileFunctions{};
    fill_win32_filefunc64W(&fileFunctions);
    unzFile archive = unzOpen2_64(archivePath.c_str(), &fileFunctions);
    if (!archive) { error = "Could not open downloaded GPU pack ZIP."; return false; }
    const auto closeArchive = [&] { unzClose(archive); };
    unz_global_info64 global{};
    if (unzGetGlobalInfo64(archive, &global) != UNZ_OK || global.number_entry == 0 ||
        global.number_entry > 200 || unzGoToFirstFile(archive) != UNZ_OK) {
        error = "GPU pack ZIP has an invalid file list."; closeArchive(); return false;
    }
    std::set<std::string> seen;
    std::uint64_t totalUnpacked = 0;
    for (ZPOS64_T index = 0; index < global.number_entry; ++index) {
        if (cancel.load()) { error = "GPU pack installation cancelled."; closeArchive(); return false; }
        unz_file_info64 entry{};
        std::array<char, 512> nameBuffer{};
        if (unzGetCurrentFileInfo64(archive, &entry, nameBuffer.data(),
                static_cast<uLong>(nameBuffer.size()), nullptr, 0, nullptr, 0) != UNZ_OK ||
            entry.size_filename >= nameBuffer.size()) {
            error = "GPU pack ZIP contains an invalid filename."; closeArchive(); return false;
        }
        const std::string name(nameBuffer.data(), entry.size_filename);
        const bool directory = !name.empty() && name.back() == '/';
        const auto kind = (entry.external_fa >> 16) & 0170000;
        if (!safeEntry(name, info.directory, directory) || kind == 0120000 ||
            (entry.flag & 1) || (entry.compression_method != 0 && entry.compression_method != 8) ||
            entry.uncompressed_size > 1024ull * 1024 * 1024 ||
            totalUnpacked + entry.uncompressed_size > 8ull * 1024 * 1024 * 1024) {
            error = "GPU pack ZIP contains an unsafe or unsupported entry."; closeArchive(); return false;
        }
        std::string lowercase = name;
        std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(lowercase).second) {
            error = "GPU pack ZIP contains duplicate paths."; closeArchive(); return false;
        }
        totalUnpacked += entry.uncompressed_size;
        const auto destination = stage / std::filesystem::path(name);
        std::error_code filesystemError;
        if (directory) {
            std::filesystem::create_directories(destination, filesystemError);
        } else {
            std::filesystem::create_directories(destination.parent_path(), filesystemError);
            if (!filesystemError && unzOpenCurrentFile(archive) != UNZ_OK) {
                error = "Could not extract a GPU pack file."; closeArchive(); return false;
            }
            if (!filesystemError) {
                std::ofstream output(destination, std::ios::binary);
                std::array<char, 64 * 1024> buffer{};
                int count = 0;
                while (output && !cancel.load() && (count = unzReadCurrentFile(archive,
                        buffer.data(), static_cast<unsigned>(buffer.size()))) > 0)
                    output.write(buffer.data(), count);
                output.close();
                const int closeResult = unzCloseCurrentFile(archive);
                if (cancel.load() || count < 0 || !output || closeResult != UNZ_OK) {
                    error = cancel.load() ? "GPU pack installation cancelled." :
                        "GPU pack ZIP failed its file integrity check.";
                    closeArchive(); return false;
                }
            }
        }
        if (filesystemError) {
            error = "Could not create GPU pack directory: " + filesystemError.message();
            closeArchive(); return false;
        }
        if (index + 1 < global.number_entry && unzGoToNextFile(archive) != UNZ_OK) {
            error = "GPU pack ZIP ended unexpectedly."; closeArchive(); return false;
        }
    }
    closeArchive();
    const auto pack = stage / info.directory;
    const auto bin = pack / "bin";
    if (!std::filesystem::is_regular_file(pack / "pack.json") ||
        !std::filesystem::is_regular_file(bin / "onnxruntime.dll") ||
        (std::string(info.directory) == "cuda" &&
            (!std::filesystem::is_regular_file(bin / "onnxruntime_providers_cuda.dll") ||
             !std::filesystem::is_regular_file(bin / "onnxruntime_providers_shared.dll"))) ||
        (std::string(info.directory) == "directml" &&
            !std::filesystem::is_regular_file(bin / "DirectML.dll"))) {
        error = "Downloaded GPU pack is missing required runtime files."; return false;
    }
    return true;
}
}

namespace RuntimePackDownloader {
bool install(Pack pack, const std::filesystem::path& packsRoot,
    const std::atomic<bool>& cancel, Progress& progress, std::string& error) {
    const auto& info = infoFor(pack);
    if (packsRoot.empty()) { error = "GPU pack folder is unavailable."; return false; }
    if (std::filesystem::exists(packsRoot / info.directory)) {
        error = "A GPU pack folder already exists. Remove or move the incomplete pack before retrying.";
        return false;
    }
    if (!readChecksum(info, cancel, error)) return false;
    std::error_code filesystemError;
    std::filesystem::create_directories(packsRoot, filesystemError);
    if (filesystemError) { error = "Could not create GPU pack folder: " + filesystemError.message(); return false; }
    const auto stage = packsRoot / (std::string(".") + info.directory + ".install-" +
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(stage, filesystemError)) {
        error = "Could not create temporary GPU pack folder."; return false;
    }
    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(stage, ignored); };
    const auto archive = stage / "pack.zip";
    if (!downloadArchive(info, archive, cancel, progress, error)) { cleanup(); return false; }
    progress.stage = Stage::Extract;
    if (!extractArchive(info, archive, stage, cancel, error)) { cleanup(); return false; }
    std::filesystem::rename(stage / info.directory, packsRoot / info.directory, filesystemError);
    if (filesystemError) {
        error = "Could not install GPU pack: " + filesystemError.message(); cleanup(); return false;
    }
    progress.stage = Stage::Complete;
    cleanup();
    return true;
}
}
#endif
