// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <functional>
#include <string>

// Small local control channel; video frames continue through ScreenCapture.
class BrowserBridge {
public:
	struct Request {
		std::string directory;
		bool image = false;
		bool half = true;
		bool mono = false;
		bool swap = false;
	};
	~BrowserBridge();
	BrowserBridge() = default;
	BrowserBridge(const BrowserBridge&) = delete;
	BrowserBridge& operator=(const BrowserBridge&) = delete;
	static std::filesystem::path runtimeDirectory();
	bool start(const std::filesystem::path& directory, std::string& error);
	// Called on the app thread. The handler returns an empty string on success.
	void poll(const std::function<std::string(const Request&)>& handler);
	void endSession();
	void stop();
private:
	int socket = -1;
	std::filesystem::path endpoint;
	std::filesystem::path session;
};
