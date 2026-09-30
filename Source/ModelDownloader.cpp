// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <unzip.h>
#ifdef _WIN32
#include <iowin32.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#else
#include <curl/curl.h>
#include <openssl/evp.h>
#include "CurlTrust.h"
#endif

namespace {

constexpr const char* modelBaseUrl = "https://rendepth.com/models/";
constexpr std::uint64_t maxModelBytes = 8ull * 1024 * 1024 * 1024;

struct ModelInfo {
	std::string_view filename;
	std::string_view archiveSha256;
	std::string_view modelSha256;
};

// These release pins are independent of the sidecars served with the ZIPs.
constexpr ModelInfo models[] = {
	{"DA2-SMALL-280.onnx", "5a70a0ccb6589d37293f800b4be1998d681e29b53b8e051f73f34d9adb69262b", "5a61a42a6c298f1d61b2f25e082b9e46cf0b81ecb20162494617c427750e93b2"},
	{"DA2-SMALL-336.onnx", "db7fd6803db169f62d1356c33ad36e7c852b3a5f97f6707f5bedaf38fdf484ba", "5a61a42a6c298f1d61b2f25e082b9e46cf0b81ecb20162494617c427750e93b2"},
	{"DA2-SMALL-392.onnx", "4a7953a0baef717a96814baebccb6c5512ff37f5b7c0498f7f9370828c36910d", "5a61a42a6c298f1d61b2f25e082b9e46cf0b81ecb20162494617c427750e93b2"},
	{"DA2-SMALL-560.onnx", "b868c372510a935aa58407f560396500c233e30d1da37677acf67b7de560dda5", "5a61a42a6c298f1d61b2f25e082b9e46cf0b81ecb20162494617c427750e93b2"},
	{"DA2-BASE-644.onnx", "7fd7a84277787240569d63f99bf3af4eba8c448bc2e9cf66c9aafb241856f7f5", "8dab08dab1a5525f350bea4fb4cdc352f2f2d3cdbd32ac09c6ed589528c72c29"},
	{"DA2-LARGE-714.onnx", "5d7cdbee20ff52439dab3372660315e94da8ca55d9afd6ac6bf2313533270a52", "795474460d9b1bdf823be70cead343f80dd0a956c72fc319420cda089060430b"},
	{"RFDN_x4.onnx", "62117fd1de33bc27d16d24edc82ce2f6e4c6c07a3bd8b510fbfce3b21445a1f0", "727b539a5d3abc311aa6546b005c47c778003f8ac6815df2c7686de7f7c099ee"},
};

const ModelInfo* modelInfoFor(std::string_view filename) {
	for (const auto& model : models)
		if (model.filename == filename) return &model;
	return nullptr;
}

// Hash the exact downloaded archive bytes and the extracted model using the
// same SHA-256 primitive as the GPU runtime-pack verifier.
bool sha256File(const std::filesystem::path& path, std::string& digest) {
	std::ifstream input(path, std::ios::binary);
	if (!input) return false;
	std::array<char, 64 * 1024> buffer{};
	std::array<unsigned char, 32> bytes{};
	#ifdef _WIN32
		struct HashState {
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			~HashState() {
				if (hash) BCryptDestroyHash(hash);
				if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
			}
		} state;
		DWORD objectSize = 0, sizeRead = 0;
		if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
			BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &sizeRead, 0) != 0)
			return false;
		std::vector<UCHAR> object(objectSize);
		if (BCryptCreateHash(state.algorithm, &state.hash, object.data(), objectSize,
			nullptr, 0, 0) != 0) return false;
	#else
		std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> state(
			EVP_MD_CTX_new(), EVP_MD_CTX_free);
		if (!state || EVP_DigestInit_ex(state.get(), EVP_sha256(), nullptr) != 1)
			return false;
	#endif
	for (;;) {
		input.read(buffer.data(), buffer.size());
		const auto count = input.gcount();
		if (count > 0) {
			#ifdef _WIN32
				if (BCryptHashData(state.hash, reinterpret_cast<PUCHAR>(buffer.data()),
					static_cast<ULONG>(count), 0) != 0) return false;
			#else
				if (EVP_DigestUpdate(state.get(), buffer.data(), static_cast<size_t>(count)) != 1)
					return false;
			#endif
		}
		if (input.eof()) break;
		if (!input) return false;
	}
	#ifdef _WIN32
		if (BCryptFinishHash(state.hash, bytes.data(), bytes.size(), 0) != 0)
			return false;
	#else
		unsigned length = 0;
		if (EVP_DigestFinal_ex(state.get(), bytes.data(), &length) != 1 ||
			length != bytes.size()) return false;
	#endif
	constexpr char hex[] = "0123456789abcdef";
	digest.clear();
	for (const auto byte : bytes) {
		digest += hex[byte >> 4];
		digest += hex[byte & 15];
	}
	return true;
}

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
struct DownloadSink {
	FILE* output;
	std::uint64_t received = 0;
	std::uint64_t limit;
};

// Write downloaded response data into the model's temporary file.
size_t writeFile(void* data, size_t size, size_t count, void* stream) {
	auto& sink = *static_cast<DownloadSink*>(stream);
	if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
	const size_t length = size * count;
	if (length > sink.limit - sink.received ||
		std::fwrite(data, 1, length, sink.output) != length) return 0;
	sink.received += length;
	return length;
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
bool downloadFile(const std::string& filename, FILE* output, Progress* progress,
	std::uint64_t maxBytes, std::string& error) {
	const std::wstring wideFilename(filename.begin(), filename.end());
	HINTERNET session = WinHttpOpen(L"Rendepth/3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr) return false;
	WinHttpSetTimeouts(session, 20000, 20000, 20000, 30000);
	HINTERNET connection = WinHttpConnect(session, L"rendepth.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET",
		(L"/models/" + wideFilename).c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	if (request != nullptr && !WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
		&redirects, sizeof(redirects))) {
		error = "Could not secure model download redirects.";
		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		return false;
	}
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
	std::uint64_t receivedBytes = 0;
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
		std::array<char, 64 * 1024> buffer{};
		DWORD received = 0;
		if (!WinHttpReadData(request, buffer.data(),
			std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &received) ||
			received > maxBytes - receivedBytes ||
			std::fwrite(buffer.data(), 1, received, output) != received) {
			success = false;
			break;
		}
		receivedBytes += received;
		if (progress != nullptr) progress->received.fetch_add(received);
	}
	if (request != nullptr) WinHttpCloseHandle(request);
	if (connection != nullptr) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	if (!success && error.empty()) error = "Could not download model data or it exceeded the size limit.";
	return success;
}
#else
bool downloadFile(const std::string& filename, FILE* output, Progress* progress,
	std::uint64_t maxBytes, std::string& error) {
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		error = "Could not initialize HTTPS model download.";
		return false;
	}
	CURL* curl = curl_easy_init();
	if (curl == nullptr) {
		curl_global_cleanup();
		error = "Could not create HTTPS model download.";
		return false;
	}
	const bool trustConfigured = configureCurlTrust(curl);
	const std::string url = std::string(modelBaseUrl) + filename;
	DownloadSink sink{output, 0, maxBytes};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	#if LIBCURL_VERSION_NUM >= 0x075500
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	#else
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
	#endif
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maxBytes));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	if (progress != nullptr) {
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, readModelHeader);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportCurlProgress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
	}
	const CURLcode result = trustConfigured ? curl_easy_perform(curl) : CURLE_SSL_CACERT_BADFILE;
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (result != CURLE_OK) {
		error = trustConfigured ? curl_easy_strerror(result) :
			"Bundled CA certificates are unavailable.";
		return false;
	}
	return true;
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
	const auto* model = modelInfoFor(filename);
	if (directory.empty() || modelName.filename() != modelName || model == nullptr) {
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
	if (std::filesystem::is_regular_file(destination, filesystemError) && !filesystemError) {
		std::string cachedHash;
		if (sha256File(destination, cachedHash) && cachedHash == model->modelSha256)
			return destination;
		error = "Cached model failed its SHA-256 integrity check: " + destination.string();
		return {};
	}

	const auto archiveFilename = std::filesystem::path(filename).replace_extension(".zip").string();
	const auto temporaryArchive = directory / (archiveFilename + ".download");
	const auto temporaryModel = directory / (filename + ".download");
	const auto temporaryChecksum = directory / (archiveFilename + ".sha256.download");
	std::filesystem::remove(temporaryArchive, filesystemError);
	std::filesystem::remove(temporaryModel, filesystemError);
	std::filesystem::remove(temporaryChecksum, filesystemError);
	if (filesystemError) {
		error = "Could not clear temporary model files: " + filesystemError.message();
		return {};
	}
	if (progress != nullptr) {
		progress->sequence.fetch_add(1);
		progress->active = true;
	}
	const auto finish = [&] {
		if (progress != nullptr) progress->active = false;
	};
	const auto openOutput = [](const std::filesystem::path& path) -> FILE* {
#ifdef _WIN32
		return _wfopen(path.c_str(), L"wb");
#else
		return std::fopen(path.c_str(), "wb");
#endif
	};
	FILE* checksumOutput = openOutput(temporaryChecksum);
	if (checksumOutput == nullptr) {
		finish();
		error = "Could not create temporary model checksum file.";
		return {};
	}
	const bool checksumDownloaded = downloadFile(archiveFilename + ".sha256",
		checksumOutput, progress, 256, error);
	const bool checksumClosed = std::fclose(checksumOutput) == 0;
	if (!checksumDownloaded || !checksumClosed) {
		finish();
		std::filesystem::remove(temporaryChecksum, filesystemError);
		if (error.empty()) error = "Could not download model ZIP checksum.";
		return {};
	}
	std::ifstream checksumInput(temporaryChecksum, std::ios::binary);
	const std::string checksum((std::istreambuf_iterator<char>(checksumInput)),
		std::istreambuf_iterator<char>());
	checksumInput.close();
	std::filesystem::remove(temporaryChecksum, filesystemError);
	const std::string expectedLine = std::string(model->archiveSha256) + "  " + archiveFilename;
	if (filesystemError || (checksum != expectedLine + "\n" &&
		checksum != expectedLine + "\r\n")) {
		finish();
		error = "Model ZIP checksum does not match this app release.";
		return {};
	}
	if (progress != nullptr) {
		progress->received = 0;
		progress->total = 0;
	}
	FILE* output = openOutput(temporaryArchive);
	if (output == nullptr) {
		finish();
		error = "Could not create temporary model ZIP: " + temporaryArchive.string();
		return {};
	}
	const bool downloadSucceeded = downloadFile(archiveFilename, output, progress,
		maxModelBytes, error);
	const bool closeSucceeded = std::fclose(output) == 0;

	if (!downloadSucceeded || !closeSucceeded) {
		finish();
		std::filesystem::remove(temporaryArchive, filesystemError);
		if (!closeSucceeded) error = "Could not finish writing model ZIP.";
		else if (error.empty()) error = "Could not download model ZIP.";
		return {};
	}
	const auto expectedSize = progress != nullptr ? progress->total.load() : 0;
	if (expectedSize != 0) {
		std::error_code sizeError;
		const auto actualSize = std::filesystem::file_size(temporaryArchive, sizeError);
		if (sizeError || actualSize != expectedSize) {
			finish();
			std::filesystem::remove(temporaryArchive, filesystemError);
			error = "The model ZIP download ended before the complete file was received.";
			return {};
		}
	}
	std::string archiveHash;
	if (!sha256File(temporaryArchive, archiveHash) || archiveHash != model->archiveSha256) {
		finish();
		std::filesystem::remove(temporaryArchive, filesystemError);
		error = "Downloaded model ZIP failed its SHA-256 integrity check.";
		return {};
	}

	const bool extracted = extractModel(temporaryArchive, temporaryModel, filename, progress, error);
	std::filesystem::remove(temporaryArchive, filesystemError);
	finish();
	if (filesystemError) {
		std::filesystem::remove(temporaryModel, filesystemError);
		error = "Could not remove downloaded model ZIP.";
		return {};
	}
	if (!extracted) {
		std::filesystem::remove(temporaryModel, filesystemError);
		return {};
	}
	std::string modelHash;
	if (!sha256File(temporaryModel, modelHash) || modelHash != model->modelSha256) {
		std::filesystem::remove(temporaryModel, filesystemError);
		error = "Extracted model failed its SHA-256 integrity check.";
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
