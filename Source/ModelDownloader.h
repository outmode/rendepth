// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_MODEL_DOWNLOADER_H
#define RENDEPTH_MODEL_DOWNLOADER_H

#include <filesystem>
#include <string>

namespace ModelDownloader {

std::filesystem::path ensureAvailable(const std::filesystem::path& directory,
	const std::string& filename, std::string& error);

}

#endif
