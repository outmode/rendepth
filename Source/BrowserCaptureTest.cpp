// Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT
#include "ScreenCapture.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

static void require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

// Used by Browser/Firefox/test_stream.py against real Firefox WebRTC output.
static int receive(const char* directory, double seconds, bool prepareDepth = false) {
	ScreenCapture capture;
	std::string error;
	require(capture.startBrowser(directory, error, prepareDepth), error.c_str());
	using Clock = std::chrono::steady_clock;
	const auto deadline = Clock::now() + std::chrono::seconds(45);
	auto first = Clock::time_point{};
	int frames = 0, width = 0, height = 0, inferenceFrames = 0;
	bool stereo = false;
	while (capture.running() && Clock::now() < deadline) {
		if (auto frame = capture.takeFrame()) {
			if (first == Clock::time_point{}) first = Clock::now();
			const double elapsed = std::chrono::duration<double>(Clock::now() - first).count();
			width = frame->width; height = frame->height;
			require(frame->format == VideoFrame::Format::YUV420P, "Expected native planar YUV frames");
			require(frame->planes[0].size() == static_cast<size_t>(width) * height, "Invalid luma plane");
			if (!frame->inferenceRGBA.empty()) {
				require(prepareDepth, "Stereo capture unexpectedly prepared depth input");
				require(frame->inferenceWidth > 0 && frame->inferenceHeight > 0 &&
					frame->inferenceWidth <= 392 && frame->inferenceHeight <= 392,
					"Invalid depth input dimensions");
				require(frame->inferenceRGBA.size() == static_cast<size_t>(frame->inferenceWidth) * frame->inferenceHeight * 4,
					"Invalid depth RGB buffer");
				const int leftRGB = (frame->inferenceWidth / 8) * 4;
				const int rightRGB = (frame->inferenceWidth * 7 / 8) * 4;
				require(frame->inferenceRGBA[leftRGB] > frame->inferenceRGBA[leftRGB + 2] + 20 &&
					frame->inferenceRGBA[rightRGB + 2] > frame->inferenceRGBA[rightRGB] + 20,
					"Depth input RGB colors do not match the video");
				++inferenceFrames;
			}
			// Fixture has a red background in the left eye and blue in the right.
			const int chromaWidth = (width + 1) / 2;
			const int left = chromaWidth / 8, right = chromaWidth * 7 / 8;
			stereo = frame->planes[2][left] > frame->planes[2][right] + 20 &&
				frame->planes[1][right] > frame->planes[1][left] + 20;
			if (elapsed >= 3) ++frames; // Allow WebRTC bitrate adaptation to settle.
			if (elapsed >= seconds + 3) {
				std::cout << "{\"fps\":" << frames / (elapsed - 3)
					<< ",\"frames\":" << frames << ",\"width\":" << width
					<< ",\"height\":" << height << ",\"stereo\":" << (stereo ? "true" : "false") << "}\n";
				require(!prepareDepth || inferenceFrames > 0, "Mono capture never supplied depth input");
				capture.stop();
				require(!capture.running() && !capture.takeFrame(), "Stop retained a frame");
				return stereo ? 0 : 1;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::ifstream failure(std::filesystem::path(directory) / "error");
	std::string text((std::istreambuf_iterator<char>(failure)), {});
	throw std::runtime_error(text.empty() ? "Timed out receiving Firefox video" : text);
}

// The integration fixture replaces the whole browser document, waits with the
// next video paused, resumes, then explicitly stops or closes the capture tab.
static int navigation(const std::filesystem::path& directory) {
	ScreenCapture capture;
	std::string error;
	require(capture.startBrowser(directory.string(), error), error.c_str());
	using Clock = std::chrono::steady_clock;
	const auto deadline = Clock::now() + std::chrono::seconds(60);
	auto waitingSince = Clock::time_point{};
	bool first = false, resumed = false;
	while (Clock::now() < deadline) {
		if (!capture.running()) {
			require(resumed && !std::filesystem::exists(directory), "Receiver ended before explicit stop after resume");
			capture.stop();
			std::cout << "Navigation: retained session while paused, replaced peer, and stopped cleanly.\n";
			return 0;
		}
		std::ifstream stateFile(directory / "state");
		std::string state;
		stateFile >> state;
		if (state == "waiting" && waitingSince == Clock::time_point{}) waitingSince = Clock::now();
		if (auto frame = capture.takeFrame()) {
			if (!first) {
				require(frame->generation == 1, "Missing initial peer frames");
				first = true;
				std::ofstream(directory / "test-first-frame").close();
			}
			if (frame->generation > 1 && !resumed) {
				require(waitingSince != Clock::time_point{} && Clock::now() - waitingSince > std::chrono::seconds(5),
					"Navigation fixture did not exercise the disconnect timeout");
				resumed = true;
				std::ofstream(directory / "test-resumed-frame").close();
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	throw std::runtime_error("Navigation test timed out");
}

int main(int argc, char** argv) {
	try {
		if (argc == 3 && std::string(argv[2]) == "navigation") return navigation(argv[1]);
		if (argc == 3 || argc == 4) return receive(argv[1], std::stod(argv[2]), argc == 4 && std::string(argv[3]) == "2d");
		char pattern[] = "/tmp/rendepth-browser-test-XXXXXX";
		const char* temporary = mkdtemp(pattern);
		require(temporary != nullptr, "Could not create test directory");
		const std::filesystem::path directory(temporary);
		ScreenCapture capture;
		std::string error;
		require(!capture.startBrowser(directory.string(), error), "Accepted missing offer");
		std::ofstream(directory / "offer.sdp") << "invalid SDP";
		require(capture.startBrowser(directory.string(), error), error.c_str());
		for (int attempt = 0; attempt < 200 && capture.running(); ++attempt)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		require(!capture.running() && std::filesystem::exists(directory / "error"), "Malformed SDP was not reported");
		capture.stop();
		require(!capture.takeFrame(), "Stop retained a frame");
		require(capture.startBrowser(directory.string(), error), error.c_str());
		const auto before = std::chrono::steady_clock::now();
		capture.stop();
		require(std::chrono::steady_clock::now() - before < std::chrono::seconds(2), "Cancellation blocked");
		std::filesystem::remove_all(directory);
		std::cout << "Browser capture: missing/invalid offer, restart, cancellation and cleanup passed.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
