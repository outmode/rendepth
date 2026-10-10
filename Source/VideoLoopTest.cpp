// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <deque>
#include <iostream>

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

// Present with the application's bounded buffer and verify that EOF does not
// discard the tail before the next playback generation appears.
int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Usage: VideoLoopTest <short video>\n";
		return 1;
	}
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	if (!SDL_Init(SDL_INIT_AUDIO)) return 1;
	int result = 0;
	{
		VideoPlayer player;
		std::string error;
		if (!player.open(argv[1], error)) {
			std::cerr << error << '\n';
			SDL_Quit();
			return 1;
		}
		using Clock = std::chrono::steady_clock;
		const auto started = Clock::now();
		std::deque<std::shared_ptr<VideoFrame>> frames;
		std::uint64_t generation = player.generation();
		double maxPresented = -1.0;
		double mediaOrigin = 0.0;
		Clock::time_point wallOrigin{};
		bool primed = false;
		int completedLoops = 0;
		while (completedLoops < 2 && Clock::now() - started < std::chrono::seconds(25)) {
			if (auto message = player.takeError(); !message.empty()) {
				std::cerr << message << '\n';
				result = 1;
				break;
			}
			const auto now = Clock::now();
			while (frames.empty() || frames.back()->presentationTime -
				frames.front()->presentationTime < 1.0) {
				auto frame = player.takeFrame();
				if (!frame) break;
				if (frame->generation != generation) {
					const double tailLimit = player.duration() - 0.1;
					if (maxPresented < tailLimit) {
						std::cerr << "Loop discarded the tail: last presented " << maxPresented
							<< " of " << player.duration() << " seconds\n";
						result = 1;
						break;
					}
					++completedLoops;
					generation = frame->generation;
					frames.clear();
					primed = false;
					maxPresented = -1.0;
				}
				frames.push_back(std::move(frame));
			}
			if (result) break;
			if (!primed && !frames.empty() &&
				frames.back()->presentationTime - frames.front()->presentationTime >= 0.1) {
				primed = true;
				mediaOrigin = frames.front()->presentationTime;
				wallOrigin = now + std::chrono::duration_cast<Clock::duration>(
					std::chrono::duration<double>(player.audioDeviceLatency()));
				player.setAudioBuffering(false);
			}
			if (primed && now >= wallOrigin) {
				const double audioPosition = player.audioPlaybackPosition();
				const double playbackTime = audioPosition >= 0.0 ? audioPosition :
					mediaOrigin + std::chrono::duration<double>(now - wallOrigin).count();
				while (!frames.empty() && frames.front()->presentationTime <= playbackTime + 0.001) {
					maxPresented = frames.front()->presentationTime;
					player.setPresentedPosition(maxPresented);
					frames.pop_front();
				}
			}
			SDL_Delay(2);
		}
		if (!result && completedLoops != 2) {
			std::cerr << "Timed out waiting for two complete loops\n";
			result = 1;
		}
		player.close();
	}
	SDL_Quit();
	if (!result) std::cout << "PASS: final video frames reached presentation in two loops\n";
	return result;
}
