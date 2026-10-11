// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"
#include "DiagnosticLog.h"

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
#include <format>
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

#ifdef __APPLE__
struct CoreMLModelInfo {
	std::string_view name;
	std::string_view archiveSha256;
	std::string_view directorySha256;
};

// Pinned hashes of the published -coreml.zip bytes and their compiled contents.
constexpr CoreMLModelInfo coreMLModels[] = {
	{"DA2-SMALL-280", "93bcc4bf0b45792bde87a22fd0879de850fc8709053fd4c9c92b0a3f2e33e042", "a28abba2dc86643972c01139172b936dabd3a89e508c119638408a0dba9e1975"},
	{"DA2-SMALL-336", "97b36daef191e0bba403251e35f76d384b56758da6f513fd2c56dcbb6e11925e", "a9e60bceb5efc707bbb8f65cb9c274e886462787146a609f1edf9032578dfd70"},
	{"DA2-SMALL-392", "1c23c4bc04f2badd6720367504736d19ad607e919241946711f7adff5c1fc480", "0d8483238c2a52d3728b5dc529a284c8defd5f02e5b615cf1a5310ef32523994"},
	{"DA2-SMALL-560", "4192fefd03c4937766432418ed4e02ea8863870b95639d36685b58ebd7e02030", "cc5085e393ec89167ca64346ce88bb25cca2703c304b3cd9978c03cf2d978152"},
	{"DA2-BASE-644", "30b73866dccd66acd5c248d7df66bcaddc8859a53fa400168d45ac962b9a635a", "4e3c8f759d39cad6972e03419e8d1f61b88f7f22c8ce0f6a03d1414f85b59c07"},
	{"DA2-LARGE-714", "6e1aee8edead455e25e1bd7a3a593de5dc918d03cb82ff3e74dfdfaae14af874", "d0bf9e49092a698e539f65c1d62e94a567985fe47f194a85684ac99495b7f218"},
	{"RFDN_x4", "81f60e98df9730e65bc13c567e3b5c13a316fe6c7fb6b24797146371538c9546", "dfd7105de83afce7ff23b697c20dce9ef52c39a188e92823873e5099f2d75999"},
};

constexpr std::array<std::string_view, 5> coreMLFiles = {
	"analytics/coremldata.bin", "coremldata.bin", "metadata.json",
	"model.mil", "weights/weight.bin"
};
#endif

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

#ifdef __APPLE__
// Hash the exact fixed set of compiled files, including their relative names.
bool sha256CoreMLDirectory(const std::filesystem::path& directory, std::string& digest) {
	if (std::filesystem::is_symlink(directory) || !std::filesystem::is_directory(directory))
		return false;
	std::string manifest;
	for (const auto relative : coreMLFiles) {
		const auto path = directory / relative;
		if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
			return false;
		std::string fileHash;
		if (!sha256File(path, fileHash)) return false;
		manifest += std::string(relative) + "  " + fileHash + "\n";
	}
	std::error_code filesystemError;
	size_t count = 0;
	for (std::filesystem::recursive_directory_iterator it(directory, filesystemError), end;
		it != end && !filesystemError; it.increment(filesystemError)) {
		if (it->is_symlink(filesystemError) ||
			(!it->is_directory(filesystemError) && !it->is_regular_file(filesystemError))) return false;
		if (it->is_regular_file(filesystemError)) ++count;
	}
	if (filesystemError || count != coreMLFiles.size()) return false;
	unsigned char bytes[EVP_MAX_MD_SIZE]{};
	unsigned length = 0;
	if (EVP_Digest(manifest.data(), manifest.size(), bytes, &length, EVP_sha256(), nullptr) != 1 ||
		length != 32) return false;
	constexpr char hex[] = "0123456789abcdef";
	digest.clear();
	for (unsigned i = 0; i < length; ++i) {
		digest += hex[bytes[i] >> 4];
		digest += hex[bytes[i] & 15];
	}
	return true;
}

// Extract only the five expected files beneath the named model directory.
bool extractCoreMLModel(const std::filesystem::path& archivePath,
	const std::filesystem::path& temporaryDirectory, std::string_view modelName,
	ModelDownloader::Progress* progress, std::string& error) {
	const std::string archiveName = archivePath.string();
	unzFile archive = unzOpen64(archiveName.c_str());
	if (archive == nullptr) { error = "Could not open Core ML model ZIP."; return false; }
	std::array<bool, coreMLFiles.size()> seen{};
	std::uint64_t total = 0;
	bool valid = unzGoToFirstFile(archive) == UNZ_OK;
	while (valid) {
		char member[256]{};
		unz_file_info64 entry{};
		if (unzGetCurrentFileInfo64(archive, &entry, member, sizeof(member),
			nullptr, 0, nullptr, 0) != UNZ_OK) { valid = false; break; }
		const std::string prefix = std::string(modelName) + ".mlmodelc/";
		const std::string filename(member);
		if (!filename.starts_with(prefix)) { valid = false; break; }
		const std::string_view relative(filename.data() + prefix.size(),
			filename.size() - prefix.size());
		const auto found = std::find(coreMLFiles.begin(), coreMLFiles.end(), relative);
		const auto index = static_cast<size_t>(found - coreMLFiles.begin());
		if (found == coreMLFiles.end() || seen[index] || (entry.flag & 1) ||
			(entry.compression_method != 0 && entry.compression_method != 8) ||
			((entry.external_fa >> 16) & 0170000) != 0100000 ||
			entry.uncompressed_size > maxModelBytes - total) { valid = false; break; }
		seen[index] = true;
		total += entry.uncompressed_size;
		const auto outputPath = temporaryDirectory / relative;
		std::error_code filesystemError;
		std::filesystem::create_directories(outputPath.parent_path(), filesystemError);
		if (filesystemError || unzOpenCurrentFile(archive) != UNZ_OK) { valid = false; break; }
		FILE* output = std::fopen(outputPath.c_str(), "wb");
		std::array<char, 64 * 1024> buffer{};
		std::uint64_t written = 0;
		int read = 0;
		bool fileValid = output != nullptr;
		while (fileValid && (!progress || !progress->cancel.load()) &&
			(read = unzReadCurrentFile(archive, buffer.data(),
				static_cast<unsigned>(buffer.size()))) > 0) {
			written += read;
			fileValid = written <= entry.uncompressed_size &&
				std::fwrite(buffer.data(), 1, read, output) == static_cast<size_t>(read);
		}
		const bool closed = output != nullptr && std::fclose(output) == 0;
		fileValid = fileValid && read == 0 && written == entry.uncompressed_size && closed &&
			(!progress || !progress->cancel.load()) && unzCloseCurrentFile(archive) == UNZ_OK;
		if (!fileValid) { valid = false; break; }
		const int next = unzGoToNextFile(archive);
		if (next == UNZ_END_OF_LIST_OF_FILE) break;
		valid = next == UNZ_OK;
	}
	valid = valid && std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
	unzClose(archive);
	if (!valid) error = progress && progress->cancel.load() ?
		"Core ML model download cancelled." : "Core ML ZIP contains invalid model files.";
	return valid;
}
#endif

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
	const DWORD connectionError = success ? 0 : GetLastError();
	DWORD status = 0;
	DWORD statusLength = sizeof(status);
	if (success && (!WinHttpQueryHeaders(request,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		&status, &statusLength, WINHTTP_NO_HEADER_INDEX) || status != 200))
		success = false;
	if (!success)
		DiagnosticLog::write("ERROR", "model_http", std::format("status={} win_error={}",
			status, connectionError));
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
	DWORD transferError = 0;
	const char* transferStage = "none";
	while (success) {
		if (progress != nullptr && progress->cancel.load()) {
			success = false;
			transferStage = "cancelled";
			break;
		}
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available)) {
			success = false;
			transferError = GetLastError();
			transferStage = "available";
			break;
		}
		if (available == 0) break;
		std::array<char, 64 * 1024> buffer{};
		DWORD received = 0;
		if (!WinHttpReadData(request, buffer.data(),
			std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &received)) {
			success = false;
			transferError = GetLastError();
			transferStage = "read";
			break;
		}
		if (received > maxBytes - receivedBytes ||
			std::fwrite(buffer.data(), 1, received, output) != received) {
			success = false;
			transferStage = "write_or_size";
			break;
		}
		receivedBytes += received;
		if (progress != nullptr) progress->received.fetch_add(received);
	}
	if (request != nullptr) WinHttpCloseHandle(request);
	if (connection != nullptr) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	if (!success && error.empty()) error = "Could not download model data or it exceeded the size limit.";
	if (!success) DiagnosticLog::write("ERROR", "model_transfer", std::format(
		"stage={} win_error={} received={}", transferStage, transferError, receivedBytes));
	else DiagnosticLog::write("INFO", "model_http", std::format("status={} received={}", status, receivedBytes));
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
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (result != CURLE_OK) {
		DiagnosticLog::write("ERROR", "model_http", std::format(
			"curl_code={} status={} received={}", static_cast<int>(result), status, sink.received));
		error = trustConfigured ? curl_easy_strerror(result) :
			"Bundled CA certificates are unavailable.";
		return false;
	}
	DiagnosticLog::write("INFO", "model_http", std::format("status={} received={}", status, sink.received));
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
	DiagnosticLog::write("INFO", "model_check", std::format("model={}", filename));
	if (std::filesystem::is_regular_file(destination, filesystemError) && !filesystemError) {
		std::string cachedHash;
		if (sha256File(destination, cachedHash) && cachedHash == model->modelSha256) {
			DiagnosticLog::write("INFO", "model_cached", std::format("model={}", filename));
			return destination;
		}
		DiagnosticLog::write("ERROR", "model_cached_hash", std::format("model={}", filename));
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
	DiagnosticLog::write("INFO", "model_sidecar_start", std::format("model={}", filename));
	if (checksumOutput == nullptr) {
		finish();
		error = "Could not create temporary model checksum file.";
		return {};
	}
	const bool checksumDownloaded = downloadFile(archiveFilename + ".sha256",
		checksumOutput, progress, 256, error);
	const bool checksumClosed = std::fclose(checksumOutput) == 0;
	if (!checksumDownloaded || !checksumClosed) {
		DiagnosticLog::write("ERROR", "model_sidecar_transfer", std::format("model={}", filename));
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
		DiagnosticLog::write("ERROR", "model_sidecar_mismatch", std::format("model={}", filename));
		finish();
		error = "Model ZIP checksum does not match this app release.";
		return {};
	}
	if (progress != nullptr) {
		progress->received = 0;
		progress->total = 0;
	}
	FILE* output = openOutput(temporaryArchive);
	DiagnosticLog::write("INFO", "model_archive_start", std::format("model={}", filename));
	if (output == nullptr) {
		finish();
		error = "Could not create temporary model ZIP: " + temporaryArchive.string();
		return {};
	}
	const bool downloadSucceeded = downloadFile(archiveFilename, output, progress,
		maxModelBytes, error);
	const bool closeSucceeded = std::fclose(output) == 0;

	if (!downloadSucceeded || !closeSucceeded) {
		DiagnosticLog::write("ERROR", "model_archive_transfer", std::format("model={}", filename));
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
			DiagnosticLog::write("ERROR", "model_archive_size", std::format(
				"model={} expected={} actual={}", filename, expectedSize, actualSize));
			finish();
			std::filesystem::remove(temporaryArchive, filesystemError);
			error = "The model ZIP download ended before the complete file was received.";
			return {};
		}
	}
	std::string archiveHash;
	if (!sha256File(temporaryArchive, archiveHash) || archiveHash != model->archiveSha256) {
		DiagnosticLog::write("ERROR", "model_archive_hash", std::format("model={}", filename));
		finish();
		std::filesystem::remove(temporaryArchive, filesystemError);
		error = "Downloaded model ZIP failed its SHA-256 integrity check.";
		return {};
	}

	DiagnosticLog::write("INFO", "model_extract_start", std::format("model={}", filename));
	const bool extracted = extractModel(temporaryArchive, temporaryModel, filename, progress, error);
	std::filesystem::remove(temporaryArchive, filesystemError);
	finish();
	if (filesystemError) {
		std::filesystem::remove(temporaryModel, filesystemError);
		error = "Could not remove downloaded model ZIP.";
		return {};
	}
	if (!extracted) {
		DiagnosticLog::write("ERROR", "model_extract", std::format("model={}", filename));
		std::filesystem::remove(temporaryModel, filesystemError);
		return {};
	}
	std::string modelHash;
	if (!sha256File(temporaryModel, modelHash) || modelHash != model->modelSha256) {
		DiagnosticLog::write("ERROR", "model_file_hash", std::format("model={}", filename));
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
	DiagnosticLog::write("INFO", "model_installed", std::format("model={}", filename));
	return destination;
}

#ifdef __APPLE__
std::filesystem::path ensureCoreMLAvailable(const std::filesystem::path& directory,
	const std::string& onnxFilename, std::string& error, Progress* progress) {
	if (progress != nullptr) {
		progress->active = false;
		progress->received = 0;
		progress->total = 0;
	}
	error.clear();
	const std::filesystem::path onnxName(onnxFilename);
	if (directory.empty() || onnxName.filename() != onnxName ||
		onnxName.extension() != ".onnx") {
		error = "Core ML model name or directory is invalid.";
		return {};
	}
	const std::string stem = onnxName.stem().string();
	const auto found = std::find_if(std::begin(coreMLModels), std::end(coreMLModels),
		[&](const CoreMLModelInfo& info) { return info.name == stem; });
	if (found == std::end(coreMLModels)) {
		error = "Core ML model is not supported: " + onnxFilename;
		return {};
	}
	std::error_code filesystemError;
	std::filesystem::create_directories(directory, filesystemError);
	if (filesystemError) {
		error = "Could not create Core ML model directory: " + filesystemError.message();
		return {};
	}
	const auto destination = directory / (stem + ".mlmodelc");
	if (std::filesystem::exists(destination, filesystemError)) {
		std::string cachedHash;
		if (!filesystemError && sha256CoreMLDirectory(destination, cachedHash) &&
			cachedHash == found->directorySha256) {
			DiagnosticLog::write("INFO", "coreml_model_cached", std::format("model={}", stem));
			return destination;
		}
		error = "Cached Core ML model failed its SHA-256 integrity check: " +
			destination.string();
		return {};
	}
	const std::string archiveFilename = stem + "-coreml.zip";
	const auto temporaryArchive = directory / (archiveFilename + ".download");
	const auto temporaryChecksum = directory / (archiveFilename + ".sha256.download");
	const auto temporaryModel = directory / (stem + ".mlmodelc.download");
	std::filesystem::remove(temporaryArchive, filesystemError);
	std::filesystem::remove(temporaryChecksum, filesystemError);
	std::filesystem::remove_all(temporaryModel, filesystemError);
	if (filesystemError || !std::filesystem::create_directory(temporaryModel, filesystemError)) {
		error = "Could not prepare Core ML model download: " + filesystemError.message();
		return {};
	}
	if (progress != nullptr) {
		progress->sequence.fetch_add(1);
		progress->active = true;
	}
	const auto finish = [&] {
		std::error_code ignored;
		std::filesystem::remove(temporaryArchive, ignored);
		std::filesystem::remove(temporaryChecksum, ignored);
		std::filesystem::remove_all(temporaryModel, ignored);
		if (progress != nullptr) progress->active = false;
	};
	FILE* output = std::fopen(temporaryChecksum.c_str(), "wb");
	if (output == nullptr) {
		error = "Could not write Core ML ZIP checksum.";
		finish();
		return {};
	}
	bool downloaded = downloadFile(archiveFilename + ".sha256", output, progress,
		256, error);
	const bool closed = std::fclose(output) == 0;
	if (!downloaded || !closed) {
		if (error.empty()) error = "Could not download Core ML ZIP checksum.";
		finish();
		return {};
	}
	std::ifstream checksumInput(temporaryChecksum, std::ios::binary);
	const std::string checksum((std::istreambuf_iterator<char>(checksumInput)),
		std::istreambuf_iterator<char>());
	const std::string expectedLine = std::string(found->archiveSha256) + "  " + archiveFilename;
	if (checksum != expectedLine + "\n" && checksum != expectedLine + "\r\n") {
		error = "Core ML ZIP checksum does not match this app release.";
		finish();
		return {};
	}
	if (progress != nullptr) {
		progress->received = 0;
		progress->total = 0;
	}
	output = std::fopen(temporaryArchive.c_str(), "wb");
	if (output == nullptr) {
		error = "Could not write Core ML model ZIP.";
		finish();
		return {};
	}
	downloaded = downloadFile(archiveFilename, output, progress, maxModelBytes, error);
	const bool archiveClosed = std::fclose(output) == 0;
	if (!downloaded || !archiveClosed) {
		if (error.empty()) error = "Could not download Core ML model ZIP.";
		finish();
		return {};
	}
	const auto expectedSize = progress != nullptr ? progress->total.load() : 0;
	if (expectedSize != 0 && std::filesystem::file_size(temporaryArchive, filesystemError) !=
		expectedSize) {
		error = "Core ML model ZIP download was incomplete.";
		finish();
		return {};
	}
	std::string archiveHash;
	if (!sha256File(temporaryArchive, archiveHash) ||
		archiveHash != found->archiveSha256) {
		error = "Downloaded Core ML model ZIP failed its SHA-256 integrity check.";
		finish();
		return {};
	}
	if (!extractCoreMLModel(temporaryArchive, temporaryModel, stem, progress, error)) {
		finish();
		return {};
	}
	std::string directoryHash;
	if (!sha256CoreMLDirectory(temporaryModel, directoryHash) ||
		directoryHash != found->directorySha256) {
		error = "Extracted Core ML model failed its SHA-256 integrity check.";
		finish();
		return {};
	}
	std::filesystem::rename(temporaryModel, destination, filesystemError);
	if (filesystemError) {
		error = "Could not install Core ML model: " + filesystemError.message();
		finish();
		return {};
	}
	finish();
	DiagnosticLog::write("INFO", "coreml_model_installed", std::format("model={}", stem));
	return destination;
}
#endif

}
