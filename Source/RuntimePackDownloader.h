// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace RuntimePackDownloader {
enum class Pack { CUDA, DirectML };
enum class Stage { Checksum, Download, Extract, Complete };

struct Progress {
    std::atomic<Stage> stage{Stage::Checksum};
    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> total{0};
};

// Downloads a versioned Windows pack and installs it in a new provider directory.
// The caller runs this on a worker thread and keeps cancel/progress alive until it returns.
bool install(Pack pack, const std::filesystem::path& packsRoot,
    const std::atomic<bool>& cancel, Progress& progress, std::string& error);
}
#endif
