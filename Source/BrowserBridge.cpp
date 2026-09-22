// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "BrowserBridge.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
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
#endif
}
