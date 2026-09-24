// Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT
#define NOMINMAX
#include <windows.h>
#include "BrowserBridge.h"
#include "rapidjson/document.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char* message) {
	if (!value) throw std::runtime_error(message);
}

int main() {
	wchar_t temporary[MAX_PATH];
	require(GetTempPathW(MAX_PATH, temporary) > 0, "No temporary directory");
	const auto base = std::filesystem::path(temporary);
	const auto id = std::to_string(GetCurrentProcessId());
	const auto root = base / ("rendepth-bridge-test-" + id);
	const auto first = base / ("rendepth-firefox-test-first-" + id);
	const auto second = base / ("rendepth-firefox-test-second-" + id);
	BrowserBridge bridge;
	try {
		std::filesystem::create_directories(root);
		std::filesystem::create_directory(first);
		std::filesystem::create_directory(second);
		std::ofstream(first / "offer.sdp").put('x');
		std::ofstream(second / "offer.sdp").put('x');
		std::string error;
		require(bridge.start(root / "instances", error), error.c_str());
		std::filesystem::path endpoint;
		for (const auto& entry : std::filesystem::directory_iterator(root / "instances"))
			if (std::filesystem::exists(entry.path() / "ready")) endpoint = entry.path();
		require(!endpoint.empty(), "No bridge endpoint");
		int accepted = 0;
		bool busy = false;
		BrowserBridge::Request received;
		int serial = 0;
		auto exchange = [&](const std::string& request) {
			const auto name = std::to_string(++serial);
			const auto pending = endpoint / ("request-" + name + ".pending");
			const auto sent = endpoint / ("request-" + name + ".json");
			std::ofstream(pending) << request;
			std::filesystem::rename(pending, sent);
			bridge.poll([&](const BrowserBridge::Request& value) -> std::string {
				if (busy) return "busy";
				received = value;
				++accepted;
				return {};
			});
			auto reply = endpoint / ("reply-" + name + ".json");
			require(std::filesystem::exists(reply), "No bridge response");
			std::ifstream input(reply);
			std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			input.close();
			std::filesystem::remove(reply);
			rapidjson::Document response;
			response.Parse(contents.c_str());
			require(!response.HasParseError(), "Invalid bridge response");
			return response;
		};
		auto request = [](const auto& path) {
			std::string escaped;
			for (const char value : path.string()) {
				if (value == '\\' || value == '"') escaped += '\\';
				escaped += value;
			}
			return "{\"directory\":\"" + escaped + "\",\"format\":\"sbs-full\",\"swap\":true}";
		};
		require(exchange(request(first)).HasMember("pid"), "First session rejected");
		require(received.directory == first.string() && !received.mono && !received.half && received.swap,
			"Stereo settings changed");
		require(exchange("{}").HasMember("error") && accepted == 1, "Malformed request reached handler");
		busy = true;
		require(exchange(request(second)).HasMember("error"), "Busy receiver accepted request");
		require(!std::filesystem::exists(first / "closed"), "Busy response closed active session");
		busy = false;
		require(exchange(request(second)).HasMember("pid"), "Second session rejected");
		require(std::filesystem::exists(first / "closed"), "Replaced session was not closed");
		auto image = request(first);
		image.insert(image.size() - 1, ",\"image\":true");
		require(exchange(image).HasMember("error"), "Missing image accepted");
		std::ofstream(first / "image.png").put('x');
		require(exchange(image).HasMember("pid") && received.image, "Photo request rejected");
		bridge.stop();
		require(std::filesystem::exists(second / "closed") && !std::filesystem::exists(endpoint),
			"Shutdown did not clean up bridge");
		std::filesystem::remove_all(root);
		std::filesystem::remove_all(first);
		std::filesystem::remove_all(second);
		std::cout << "Windows browser bridge: validation, photos, reuse, shutdown passed.\n";
		return 0;
	} catch (const std::exception& error) {
		bridge.stop();
		std::filesystem::remove_all(root);
		std::filesystem::remove_all(first);
		std::filesystem::remove_all(second);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
