// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <cstdio>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {

constexpr const char* modelBaseUrl = "https://rendepth.com/models/";

#ifndef _WIN32
// Write downloaded response data into the model's temporary file.
size_t writeFile(void* data, size_t size, size_t count, void* stream) {
	return std::fwrite(data, size, count, static_cast<FILE*>(stream));
}
#endif

}

namespace ModelDownloader {

#ifdef _WIN32
// Fetch a model over HTTPS using the Windows HTTP backend.
bool downloadFile(const std::string& filename, FILE* output) {
	const std::wstring wideFilename(filename.begin(), filename.end());
	HINTERNET session = WinHttpOpen(L"Rendepth/3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr) return false;
	HINTERNET connection = WinHttpConnect(session, L"rendepth.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET",
		(L"/models/" + wideFilename).c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	bool success = request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
		0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
	while (success) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available)) {
			success = false;
			break;
		}
		if (available == 0) break;
		std::vector<char> buffer(available);
		DWORD received = 0;
		if (!WinHttpReadData(request, buffer.data(), available, &received) ||
			std::fwrite(buffer.data(), 1, received, output) != received) {
			success = false;
			break;
		}
	}
	if (request != nullptr) WinHttpCloseHandle(request);
	if (connection != nullptr) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return success;
}
#endif

// Reuse an existing model or download to a temporary file before installing it at the requested path.
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

	#ifdef _WIN32
	const bool downloadSucceeded = downloadFile(filename, output);
	#else
	const auto curlGlobal = curl_global_init(CURL_GLOBAL_DEFAULT);
	CURLcode result = CURLE_FAILED_INIT;
	if (curlGlobal == CURLE_OK) {
		CURL* curl = curl_easy_init();
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
	}
	curl_global_cleanup();
	const bool downloadSucceeded = result == CURLE_OK;
	#endif
	const bool closeSucceeded = std::fclose(output) == 0;

	if (!downloadSucceeded || !closeSucceeded) {
		std::filesystem::remove(temporary, filesystemError);
		#ifdef _WIN32
		error = "Could not download depth model.";
		#else
		error = curl_easy_strerror(result);
		#endif
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
