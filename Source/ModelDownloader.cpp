// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <unzip.h>
#ifdef _WIN32
#include <iowin32.h>
#endif

#include <array>
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
constexpr std::uint64_t maxModelBytes = 8ull * 1024 * 1024 * 1024;

// Only install the expected model file from the archive, through a temporary path.
bool extractModel(const std::filesystem::path& archivePath,
	const std::filesystem::path& temporaryModel, const std::string& filename,
	ModelDownloader::Progress* progress, std::string& error) {
#ifdef _WIN32
	zlib_filefunc64_def fileFunctions{};
	fill_win32_filefunc64W(&fileFunctions);
	unzFile archive = unzOpen2_64(archivePath.c_str(), &fileFunctions);
#else
	const std::string archiveName = archivePath.string();
	unzFile archive = unzOpen64(archiveName.c_str());
#endif
	if (!archive) { error = "Could not open downloaded model ZIP."; return false; }
	if (unzLocateFile(archive, filename.c_str(), 1) != UNZ_OK) {
		error = "Model ZIP does not contain " + filename + ".";
		unzClose(archive);
		return false;
	}
	unz_file_info64 entry{};
	if (unzGetCurrentFileInfo64(archive, &entry, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
		entry.uncompressed_size == 0 || entry.uncompressed_size > maxModelBytes ||
		(entry.flag & 1) || (entry.compression_method != 0 && entry.compression_method != 8) ||
		((entry.external_fa >> 16) & 0170000) == 0120000) {
		error = "Model ZIP contains an unsafe or unsupported model file.";
		unzClose(archive);
		return false;
	}
	if (unzOpenCurrentFile(archive) != UNZ_OK) {
		error = "Could not extract model from ZIP.";
		unzClose(archive);
		return false;
	}
#ifdef _WIN32
	FILE* output = _wfopen(temporaryModel.c_str(), L"wb");
#else
	FILE* output = std::fopen(temporaryModel.c_str(), "wb");
#endif
	std::array<char, 64 * 1024> buffer{};
	int count = 0;
	std::uint64_t extracted = 0;
	bool written = output != nullptr;
	while (written && (!progress || !progress->cancel.load()) &&
		(count = unzReadCurrentFile(archive, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0) {
		extracted += count;
		written = extracted <= entry.uncompressed_size &&
			std::fwrite(buffer.data(), 1, count, output) == static_cast<size_t>(count);
	}
	const bool closedOutput = output && std::fclose(output) == 0;
	const int closeResult = unzCloseCurrentFile(archive);
	const bool valid = written && count == 0 && extracted == entry.uncompressed_size &&
		closedOutput && (!progress || !progress->cancel.load()) && closeResult == UNZ_OK;
	if (!valid) {
		error = progress && progress->cancel.load() ? "Model download cancelled." :
			"Model ZIP failed extraction or its integrity check.";
	}
	unzClose(archive);
	return valid;
}

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
	DWORD status = 0;
	DWORD statusLength = sizeof(status);
	if (success && (!WinHttpQueryHeaders(request,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		&status, &statusLength, WINHTTP_NO_HEADER_INDEX) || status != 200))
		success = false;
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
	const std::filesystem::path modelName(filename);
	if (directory.empty() || modelName.filename() != modelName || modelName.extension() != ".onnx") {
		error = "Model directory or ONNX filename is invalid.";
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

	const auto archiveFilename = std::filesystem::path(filename).replace_extension(".zip").string();
	const auto temporaryArchive = directory / (archiveFilename + ".download");
	const auto temporaryModel = directory / (filename + ".download");
	std::filesystem::remove(temporaryArchive, filesystemError);
	std::filesystem::remove(temporaryModel, filesystemError);
#ifdef _WIN32
	FILE* output = _wfopen(temporaryArchive.c_str(), L"wb");
#else
	FILE* output = std::fopen(temporaryArchive.c_str(), "wb");
#endif
	if (output == nullptr) {
		error = "Could not create temporary model ZIP: " + temporaryArchive.string();
		return {};
	}
	if (progress != nullptr) {
		progress->sequence.fetch_add(1);
		progress->active = true;
	}

#ifdef _WIN32
	const bool downloadSucceeded = downloadFile(archiveFilename, output, progress);
#else
	const auto curlGlobal = curl_global_init(CURL_GLOBAL_DEFAULT);
	CURLcode result = CURLE_FAILED_INIT;
	if (curlGlobal == CURLE_OK) {
		CURL* curl = curl_easy_init();
		if (curl != nullptr) {
			const std::string url = std::string(modelBaseUrl) + archiveFilename;
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

	if (!downloadSucceeded || !closeSucceeded) {
		if (progress != nullptr) progress->active = false;
		std::filesystem::remove(temporaryArchive, filesystemError);
#ifdef _WIN32
		error = "Could not download model ZIP.";
#else
		error = closeSucceeded ? curl_easy_strerror(result) : "Could not finish writing model ZIP.";
#endif
		return {};
	}
	const auto expectedSize = progress != nullptr ? progress->total.load() : 0;
	if (expectedSize != 0) {
		std::error_code sizeError;
		const auto actualSize = std::filesystem::file_size(temporaryArchive, sizeError);
		if (sizeError || actualSize != expectedSize) {
			if (progress != nullptr) progress->active = false;
			std::filesystem::remove(temporaryArchive, filesystemError);
			error = "The model ZIP download ended before the complete file was received.";
			return {};
		}
	}

	const bool extracted = extractModel(temporaryArchive, temporaryModel, filename, progress, error);
	std::filesystem::remove(temporaryArchive, filesystemError);
	if (progress != nullptr) progress->active = false;
	if (filesystemError) {
		std::filesystem::remove(temporaryModel, filesystemError);
		error = "Could not remove downloaded model ZIP.";
		return {};
	}
	if (!extracted) {
		std::filesystem::remove(temporaryModel, filesystemError);
		return {};
	}
	std::filesystem::rename(temporaryModel, destination, filesystemError);
	if (filesystemError) {
		const std::string reason = filesystemError.message();
		std::filesystem::remove(temporaryModel, filesystemError);
		error = "Could not install downloaded model: " + reason;
		return {};
	}
	return destination;
}

}
