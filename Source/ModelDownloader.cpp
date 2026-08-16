// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <curl/curl.h>
#include <cstdio>
#include <system_error>

namespace {

constexpr const char* modelBaseUrl = "https://rendepth.com/models/";

size_t writeFile(void* data, size_t size, size_t count, void* stream) {
	return std::fwrite(data, size, count, static_cast<FILE*>(stream));
}

}

namespace ModelDownloader {

std::filesystem::path ensureAvailable(const std::filesystem::path& directory,
	const std::string& filename, std::string& error) {
	if (directory.empty() || filename.empty()) {
		error = "Model directory or filename is empty.";
		return {};
	}

	std::error_code filesystemError;
	std::filesystem::create_directories(directory, filesystemError);
	if (filesystemError) {
		error = "Could not create model directory: " + filesystemError.message();
		return {};
	}

	const auto destination = directory / filename;
	if (std::filesystem::is_regular_file(destination, filesystemError) && !filesystemError)
		return destination;

	const auto temporary = directory / (filename + ".download");
	std::filesystem::remove(temporary, filesystemError);
	FILE* output = std::fopen(temporary.string().c_str(), "wb");
	if (output == nullptr) {
		error = "Could not create temporary model file: " + temporary.string();
		return {};
	}

	const auto curlGlobal = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (curlGlobal != CURLE_OK) {
		std::fclose(output);
		std::filesystem::remove(temporary, filesystemError);
		error = "Could not initialize HTTPS downloader.";
		return {};
	}

	CURL* curl = curl_easy_init();
	CURLcode result = CURLE_FAILED_INIT;
	if (curl != nullptr) {
		const std::string url = std::string(modelBaseUrl) + filename;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, output);
		result = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
	}
	curl_global_cleanup();
	const bool closeSucceeded = std::fclose(output) == 0;

	if (result != CURLE_OK || !closeSucceeded) {
		std::filesystem::remove(temporary, filesystemError);
		error = curl_easy_strerror(result);
		return {};
	}

	std::filesystem::rename(temporary, destination, filesystemError);
	if (filesystemError) {
		std::filesystem::remove(temporary, filesystemError);
		error = "Could not install downloaded model: " + filesystemError.message();
		return {};
	}
	return destination;
}

}
