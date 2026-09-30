// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "RuntimePackDownloader.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || (std::wstring(argv[1]) != L"cuda" && std::wstring(argv[1]) != L"directml")) {
        std::cerr << "Usage: RuntimePackDownloaderSmoke cuda|directml <new-pack-root>\n";
        return 2;
    }
    const auto pack = std::wstring(argv[1]) == L"cuda" ?
        RuntimePackDownloader::Pack::CUDA : RuntimePackDownloader::Pack::DirectML;
    std::atomic<bool> cancel{false};
    RuntimePackDownloader::Progress progress;
    std::string error;
    if (!RuntimePackDownloader::install(pack, std::filesystem::path(argv[2]), cancel, progress, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "Installed GPU pack.\n";
    return 0;
}
