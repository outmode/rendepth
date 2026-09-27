// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <charconv>
#include <cctype>
#include <cstdio>
#include <string_view>
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

// Hostinger's model ETag starts with the file size in hex. Use it when the
// response omits Content-Length; otherwise leave progress indeterminate.
std::uint64_t modelSizeFromEtag(std::string_view etag) {
	while (!etag.empty() && (etag.front() == ' ' || etag.front() == '\t'))
		etag.remove_prefix(1);
	if (!etag.starts_with("W/\"") || etag.find(";gz\"") == etag.npos)
		return 0;
	etag.remove_prefix(3);
	const auto dash = etag.find('-');
	if (dash == etag.npos) return 0;
	std::uint64_t size = 0;
	const auto [end, error] = std::from_chars(etag.data(), etag.data() + dash, size, 16);
	return error == std::errc{} && end == etag.data() + dash ? size : 0;
}

#ifndef _WIN32
// Write downloaded response data into the model's temporary file.
size_t writeFile(void* data, size_t size, size_t count, void* stream) {
	return std::fwrite(data, size, count, static_cast<FILE*>(stream));
}

size_t readModelHeader(char* data, size_t size, size_t count, void* state) {
	const size_t length = size * count;
	std::string_view header(data, length);
	constexpr std::string_view name = "etag:";
	if (header.size() >= name.size()) {
		bool matches = true;
		for (size_t i = 0; i < name.size(); ++i)
			matches &= std::tolower(static_cast<unsigned char>(header[i])) == name[i];
		if (matches) {
			const auto expected = modelSizeFromEtag(header.substr(name.size()));
			if (expected != 0)
				static_cast<ModelDownloader::Progress*>(state)->total = expected;
		}
	}
	return length;
}

int reportCurlProgress(void* data, curl_off_t total, curl_off_t received,
	curl_off_t, curl_off_t) {
	auto& progress = *static_cast<ModelDownloader::Progress*>(data);
	if (total > 0) progress.total = static_cast<std::uint64_t>(total);
	progress.received = received > 0 ? static_cast<std::uint64_t>(received) : 0;
	return progress.cancel.load() ? 1 : 0;
}
#endif

}

namespace ModelDownloader {

#ifdef _WIN32
// Fetch a model over HTTPS using the Windows HTTP backend.
bool downloadFile(const std::string& filename, FILE* output, Progress* progress) {
	const std::wstring wideFilename(filename.begin(), filename.end());
	HINTERNET session = WinHttpOpen(L"Rendepth/3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr) return false;
	WinHttpSetTimeouts(session, 20000, 20000, 20000, 30000);
	HINTERNET connection = WinHttpConnect(session, L"rendepth.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET",
		(L"/models/" + wideFilename).c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	bool success = request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
		0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
	if (success && progress != nullptr) {
		DWORD total = 0;
		DWORD length = sizeof(total);
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &total, &length, WINHTTP_NO_HEADER_INDEX))
			progress->total = total;
		else {
			WCHAR etag[256]{};
			length = sizeof(etag);
			if (WinHttpQueryHeaders(request, WINHTTP_QUERY_ETAG,
				WINHTTP_HEADER_NAME_BY_INDEX, etag, &length, WINHTTP_NO_HEADER_INDEX)) {
				const std::wstring wideEtag(etag);
				progress->total = modelSizeFromEtag(
					std::string(wideEtag.begin(), wideEtag.end()));
			}
		}
	}
	while (success) {
		if (progress != nullptr && progress->cancel.load()) {
			success = false;
			break;
		}
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
		if (progress != nullptr) progress->received.fetch_add(received);
	}
	if (request != nullptr) WinHttpCloseHandle(request);
	if (connection != nullptr) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return success;
}
#endif

// Reuse an existing model or download to a temporary file before installing it at the requested path.
std::filesystem::path ensureAvailable(const std::filesystem::path& directory,
	const std::string& filename, std::string& error, Progress* progress) {
	if (progress != nullptr) {
		progress->active = false;
		progress->received = 0;
		progress->total = 0;
	}
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
	if (progress != nullptr) {
		progress->sequence.fetch_add(1);
		progress->active = true;
	}

	#ifdef _WIN32
	const bool downloadSucceeded = downloadFile(filename, output, progress);
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
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, output);
			if (progress != nullptr) {
				curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
				curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, readModelHeader);
				curl_easy_setopt(curl, CURLOPT_HEADERDATA, progress);
				curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportCurlProgress);
				curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
			}
			result = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
		}
	}
	curl_global_cleanup();
	const bool downloadSucceeded = result == CURLE_OK;
	#endif
	const bool closeSucceeded = std::fclose(output) == 0;
	if (progress != nullptr) progress->active = false;

	if (!downloadSucceeded || !closeSucceeded) {
		std::filesystem::remove(temporary, filesystemError);
		#ifdef _WIN32
		error = "Could not download depth model.";
		#else
		error = curl_easy_strerror(result);
		#endif
		return {};
	}
	const auto expectedSize = progress != nullptr ? progress->total.load() : 0;
	if (expectedSize != 0) {
		std::error_code sizeError;
		const auto actualSize = std::filesystem::file_size(temporary, sizeError);
		if (sizeError || actualSize != expectedSize) {
			std::filesystem::remove(temporary, filesystemError);
			error = "The depth model download ended before the complete file was received.";
			return {};
		}
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
