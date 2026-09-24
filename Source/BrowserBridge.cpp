// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "BrowserBridge.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// Close the browser bridge and its current session on destruction.
BrowserBridge::~BrowserBridge() { stop(); }

// Choose a per-user runtime directory for the local browser bridge.
std::filesystem::path BrowserBridge::runtimeDirectory() {
#if defined(__linux__)
	if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
		return std::filesystem::path(runtime) / "rendepth-browser";
	return std::filesystem::path("/tmp") / ("rendepth-browser-" + std::to_string(getuid()));
#elif defined(_WIN32)
	wchar_t local[MAX_PATH];
	const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
	if (size == 0 || size >= MAX_PATH) return {};
	return std::filesystem::path(local) / L"Rendepth" / L"browser-bridge";
#else
	return {};
#endif
}

// Validate a private runtime directory and bind the nonblocking browser-request socket.
bool BrowserBridge::start(const std::filesystem::path& directory, std::string& error) {
#if defined(__linux__)
	stop();
	if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
		error = std::strerror(errno);
		return false;
	}
	struct stat info{};
	if (lstat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) ||
		info.st_uid != getuid() || (info.st_mode & 0077)) {
		error = "Browser bridge requires a private runtime directory.";
		return false;
	}
	const auto path = directory / (std::to_string(getpid()) + ".sock");
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	if (path.string().size() >= sizeof(address.sun_path)) {
		error = "Browser bridge socket path is too long.";
		return false;
	}
	std::strcpy(address.sun_path, path.c_str());
	socket = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (socket < 0) { error = std::strerror(errno); return false; }
	// A stale socket with our PID can only belong to a previous process.
	unlink(path.c_str());
	if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
		error = std::strerror(errno);
		::close(socket);
		socket = -1;
		return false;
	}
	endpoint = path;
	return true;
#elif defined(_WIN32)
	stop();
	if (directory.empty()) { error = "Could not find the Windows user data directory."; return false; }
	std::error_code ec;
	std::filesystem::create_directories(directory, ec);
	if (ec) { error = ec.message(); return false; }
	std::random_device random;
	endpoint = directory / ("instance-" + std::to_string(GetCurrentProcessId()) + "-" +
		std::to_string(random()));
	if (!std::filesystem::create_directory(endpoint, ec)) {
		error = ec ? ec.message() : "Could not create the browser bridge endpoint.";
		endpoint.clear();
		return false;
	}
	std::ofstream ready(endpoint / "ready");
	ready << GetCurrentProcessId();
	ready.close();
	if (!ready) {
		error = "Could not publish the browser bridge endpoint.";
		std::filesystem::remove_all(endpoint, ec);
		endpoint.clear();
		return false;
	}
	return true;
#else
	error = "The Firefox bridge currently supports Linux only.";
	return false;
#endif
}

// Validate one incoming browser request, dispatch it to the application, and send an acknowledgement.
void BrowserBridge::poll(const std::function<std::string(const Request&)>& handler) {
#if defined(__linux__)
	if (socket < 0) return;
	char buffer[4096];
	sockaddr_un sender{};
	socklen_t senderSize = sizeof(sender);
	const auto count = recvfrom(socket, buffer, sizeof(buffer), MSG_TRUNC,
		reinterpret_cast<sockaddr*>(&sender), &senderSize);
	if (count < 0) return;
	std::string error = "Invalid browser capture request.";
	if (count > 0 && count <= static_cast<ssize_t>(sizeof(buffer))) {
		rapidjson::Document document;
		document.Parse(buffer, static_cast<size_t>(count));
		if (!document.HasParseError() && document.IsObject() &&
			document.HasMember("directory") && document["directory"].IsString() &&
			document.HasMember("format") && document["format"].IsString() &&
			document.HasMember("swap") && document["swap"].IsBool() &&
			(!document.HasMember("image") || document["image"].IsBool())) {
			Request request;
			request.directory.assign(document["directory"].GetString(), document["directory"].GetStringLength());
			const std::string format(document["format"].GetString(), document["format"].GetStringLength());
			if (document.HasMember("image")) {
				request.image = document["image"].GetBool();
			}
			struct stat info{};
			std::error_code ec;
			const auto path = std::filesystem::path(request.directory);
			if ((format == "2d" || format == "sbs-half" || format == "sbs-full") &&
				request.directory.find('\0') == std::string::npos && path.is_absolute() &&
				path.filename().string().starts_with("rendepth-firefox-") &&
				lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
				info.st_uid == getuid() && !(info.st_mode & 0077) &&
				std::filesystem::is_regular_file(path / (request.image ? "image.png" : "offer.sdp"), ec)) {
				request.half = format == "sbs-half";
				request.mono = format == "2d";
				request.swap = document["swap"].GetBool();
				error = handler(request);
				if (error.empty()) {
					if (session != path) endSession();
					if (!request.image) session = path;
				}
			}
		}
	}
	rapidjson::StringBuffer response;
	rapidjson::Writer<rapidjson::StringBuffer> writer(response);
	writer.StartObject();
	if (!error.empty()) { writer.Key("error"); writer.String(error.c_str()); }
	else { writer.Key("pid"); writer.Int(getpid()); }
	writer.EndObject();
	sendto(socket, response.GetString(), response.GetSize(), MSG_NOSIGNAL,
		reinterpret_cast<sockaddr*>(&sender), senderSize);
#elif defined(_WIN32)
	if (endpoint.empty()) return;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(endpoint, ec)) {
		if (ec) break;
		const auto filename = entry.path().filename().string();
		if (!filename.starts_with("request-") || !filename.ends_with(".json") ||
			!entry.is_regular_file(ec) || entry.file_size(ec) > 4096 || ec) continue;
		std::string error = "Invalid browser capture request.";
		std::ifstream input(entry.path(), std::ios::binary);
		std::string buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		input.close();
		if (input && !buffer.empty() && buffer.size() <= 4096) {
			try {
			rapidjson::Document document;
			document.Parse(buffer.data(), buffer.size());
			if (!document.HasParseError() && document.IsObject() &&
				document.HasMember("directory") && document["directory"].IsString() &&
				document.HasMember("format") && document["format"].IsString() &&
				document.HasMember("swap") && document["swap"].IsBool() &&
				(!document.HasMember("image") || document["image"].IsBool())) {
				Request request;
				request.directory.assign(document["directory"].GetString(), document["directory"].GetStringLength());
				const std::string format(document["format"].GetString(), document["format"].GetStringLength());
				if (document.HasMember("image")) request.image = document["image"].GetBool();
				const auto path = std::filesystem::u8path(request.directory);
				wchar_t temporary[MAX_PATH];
				const DWORD length = GetTempPathW(MAX_PATH, temporary);
				const auto parent = std::filesystem::weakly_canonical(path.parent_path(), ec);
				const auto expected = length > 0 && length < MAX_PATH ?
					std::filesystem::weakly_canonical(std::filesystem::path(temporary), ec) : std::filesystem::path();
				const auto attributes = GetFileAttributesW(path.c_str());
				if ((format == "2d" || format == "sbs-half" || format == "sbs-full") &&
					request.directory.find('\0') == std::string::npos && path.is_absolute() &&
					path.filename().string().starts_with("rendepth-firefox-") &&
					!ec && !expected.empty() && parent == expected &&
					attributes != INVALID_FILE_ATTRIBUTES &&
					(attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
					std::filesystem::is_regular_file(path / (request.image ? "image.png" : "offer.sdp"), ec)) {
					request.half = format == "sbs-half";
					request.mono = format == "2d";
					request.swap = document["swap"].GetBool();
					error = handler(request);
					if (error.empty()) {
						if (session != path) endSession();
						if (!request.image) session = path;
					}
				}
			}
			} catch (const std::exception&) {
				error = "Invalid browser capture request.";
			}
		}
		rapidjson::StringBuffer response;
		rapidjson::Writer<rapidjson::StringBuffer> writer(response);
		writer.StartObject();
		if (!error.empty()) { writer.Key("error"); writer.String(error.c_str()); }
		else { writer.Key("pid"); writer.Uint(GetCurrentProcessId()); }
		writer.EndObject();
		const auto reply = endpoint / ("reply-" + filename.substr(8));
		auto pending = reply;
		pending += L".pending";
		std::ofstream output(pending, std::ios::binary);
		output.write(response.GetString(), response.GetSize());
		output.close();
		if (output) std::filesystem::rename(pending, reply, ec);
		std::filesystem::remove(entry.path(), ec);
		break;
	}
#endif
}

// Signal the native host that the active browser session has ended.
void BrowserBridge::endSession() {
	if (session.empty()) return;
	// The native host owns this private directory and removes it on disconnect.
	std::ofstream(session / "closed").close();
	session.clear();
}

// End the active session and remove the bridge's socket endpoint.
void BrowserBridge::stop() {
	endSession();
#if defined(__linux__)
	if (socket >= 0) { ::close(socket); socket = -1; }
	if (!endpoint.empty()) { unlink(endpoint.c_str()); endpoint.clear(); }
#elif defined(_WIN32)
	if (!endpoint.empty()) {
		std::error_code ec;
		std::filesystem::remove_all(endpoint, ec);
		endpoint.clear();
	}
#endif
}
