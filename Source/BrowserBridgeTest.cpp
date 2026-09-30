// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "BrowserBridge.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "rapidjson/document.h"

// Fail a bridge test immediately with its diagnostic message.
static void require(bool value, const char* error) {
	if (!value) throw std::runtime_error(error);
}

// Exercise local bridge requests, responses, session signaling, and invalid request handling.
int main() {
	char pattern[] = "/tmp/rendepth-bridge-test-XXXXXX";
	const char* temporary = mkdtemp(pattern);
	if (!temporary) return 1;
	const std::filesystem::path root(temporary);
	BrowserBridge bridge;
	int client = -1;
	try {
		std::string error;
		require(bridge.start(root / "instances", error), error.c_str());
		const auto endpoint = root / "instances" / (std::to_string(getpid()) + ".sock");
		client = socket(AF_UNIX, SOCK_DGRAM, 0);
		require(client >= 0, "Could not create test socket");
		timeval timeout{1, 0};
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		sockaddr_un sender{};
		sender.sun_family = AF_UNIX;
		std::strcpy(sender.sun_path, (root / "reply.sock").c_str());
		require(bind(client, reinterpret_cast<sockaddr*>(&sender), sizeof(sender)) == 0, "Could not bind test socket");
		sockaddr_un target{};
		target.sun_family = AF_UNIX;
		std::strcpy(target.sun_path, endpoint.c_str());
		int accepted = 0;
		bool busy = false;
		BrowserBridge::Request received;
		auto exchange = [&](const std::string& request) {
			require(sendto(client, request.data(), request.size(), 0,
				reinterpret_cast<sockaddr*>(&target), sizeof(target)) >= 0, "Request failed");
			bridge.poll([&](const BrowserBridge::Request& value) -> std::string {
				if (busy) return "busy";
				received = value;
				++accepted;
				return {};
			});
			char bytes[4096];
			const auto count = recv(client, bytes, sizeof(bytes), 0);
			require(count > 0, "No bridge response");
			rapidjson::Document response;
			response.Parse(bytes, static_cast<size_t>(count));
			require(!response.HasParseError(), "Invalid bridge response");
			return response;
		};
		const auto first = root / "rendepth-firefox-first";
		const auto second = root / "rendepth-firefox-second";
		for (const auto& path : {first, second}) {
			std::filesystem::create_directory(path);
			std::filesystem::permissions(path, std::filesystem::perms::owner_all);
			std::ofstream(path / "offer.sdp").put('x');
		}
		auto request = [](const auto& path) {
			return "{\"directory\":\"" + path.string() + "\",\"format\":\"sbs-full\",\"swap\":true}";
		};
		require(exchange(request(first)).HasMember("pid"), "First session was rejected");
		require(!received.mono && !received.half && received.swap && received.directory == first, "Stereo settings changed in transit");
		require(exchange("{}").HasMember("error") && accepted == 1, "Malformed request reached app handler");
		busy = true;
		require(exchange(request(second)).HasMember("error"), "Busy app accepted a session");
		require(!std::filesystem::exists(first / "closed"), "Rejected request ended current session");
		busy = false;
		require(exchange(request(second)).HasMember("pid") && accepted == 2, "Second session failed");
		require(std::filesystem::exists(first / "closed"), "Replacing capture did not notify old host");
		auto monoRequest = request(second);
		monoRequest.replace(monoRequest.find("sbs-full"), 8, "2d");
		require(exchange(monoRequest).HasMember("pid") && received.mono && !received.half,
			"2D video did not reach the app as mono");
		auto invalidRequest = request(second);
		invalidRequest.replace(invalidRequest.find("sbs-full"), 8, "unknown");
		require(exchange(invalidRequest).HasMember("error"), "Unknown format was accepted");
		auto imageRequest = request(first);
		imageRequest.insert(imageRequest.size() - 1, ",\"image\":true");
		require(exchange(imageRequest).HasMember("error"), "Missing image was accepted");
		std::ofstream(first / "image.png").put('x');
		require(exchange(imageRequest).HasMember("pid") && received.image && !received.mono && !received.half,
			"SBS image settings did not reach the handler");
		imageRequest.replace(imageRequest.find("sbs-full"), 8, "sbs-half");
		require(exchange(imageRequest).HasMember("pid") && received.image && received.half && !received.mono,
			"Half SBS photo did not retain its aspect-ratio setting");
		imageRequest.replace(imageRequest.find("sbs-half"), 8, "2d");
		require(exchange(imageRequest).HasMember("pid") && received.image && received.mono,
			"2D photo settings did not reach the handler");
		imageRequest.replace(imageRequest.find("\"image\":true"), 12, "\"image\":123");
		require(exchange(imageRequest).HasMember("error"), "Invalid image discriminator accepted");
		bridge.stop();
		require(std::filesystem::exists(second / "closed") && !std::filesystem::exists(endpoint), "Quit did not clean up bridge");
		close(client);
		std::filesystem::remove_all(root);
		std::cout << "Browser bridge: settings, rejection, replacement, and shutdown passed.\n";
		return 0;
	} catch (const std::exception& error) {
		bridge.stop();
		if (client >= 0) close(client);
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
