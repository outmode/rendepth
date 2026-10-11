// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_MODEL_DOWNLOADER_H
#define RENDEPTH_MODEL_DOWNLOADER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ModelDownloader {

struct Progress {
	std::atomic<bool> active{false};
	std::atomic<bool> cancel{false};
	std::atomic<std::uint64_t> sequence{0};
	std::atomic<std::uint64_t> received{0};
	std::atomic<std::uint64_t> total{0};
};

std::filesystem::path ensureAvailable(const std::filesystem::path& directory,
	const std::string& filename, std::string& error, Progress* progress = nullptr);

#ifdef __APPLE__
// Download and verify a compiled native Core ML model for an ONNX model name.
std::filesystem::path ensureCoreMLAvailable(const std::filesystem::path& directory,
	const std::string& onnxFilename, std::string& error, Progress* progress = nullptr);
#endif

}

#endif
