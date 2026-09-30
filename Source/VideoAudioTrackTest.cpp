// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <string_view>

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

// Exercise track changes with the same bounded, audio-clock-driven presentation
// queue as the application. Draining frames unconditionally hides playback stalls.
int main(int argc, char** argv) {
	if (argc != 2 && argc != 4) {
		std::cerr << "Usage: VideoAudioTrackTest <video with multiple audio tracks> [--seek seconds]\n";
		return 1;
	}
	const bool seekPlayback = argc == 4 && std::string_view(argv[2]) == "--seek";
	if (argc == 4 && !seekPlayback) return 1;
	const double seekTime = seekPlayback ? std::stod(argv[3]) : 0.0;
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	if (!SDL_Init(SDL_INIT_AUDIO)) return 1;
	int result = 0;
	{
		VideoPlayer player;
		std::string error;
		if (!player.open(argv[1], error)) {
			std::cerr << error << '\n';
			return 1;
		}
		if (seekPlayback) player.seek(seekTime);
		using Clock = std::chrono::steady_clock;
		const auto start = Clock::now();
		auto lastPresented = start;
		std::deque<std::shared_ptr<VideoFrame>> frames;
		auto generation = player.generation();
		bool primed = false;
		double wallOrigin = 0.0;
		double mediaOrigin = 0.0;
		double presented = 0.0;
		double switchPosition = 0.0;
		int switches = 0;
		int presentationGaps = 0;
		double longestGap = 0.0;
		double firstPresentationElapsed = -1.0;
		while (seekPlayback ? presented < seekTime + 12.0 :
			switches < 5 || presented < switchPosition + 2.0) {
			const auto now = Clock::now();
			const double elapsed = std::chrono::duration<double>(now - start).count();
			player.update();
			if (auto message = player.takeError(); !message.empty()) {
				std::cerr << message << '\n'; result = 1; break;
			}
			while (frames.empty() || frames.back()->presentationTime -
				frames.front()->presentationTime < 1.0) {
				auto frame = player.takeFrame();
				if (!frame) break;
				if (frame->generation != generation) {
					frames.clear();
					primed = false;
					generation = frame->generation;
				}
				frames.push_back(std::move(frame));
			}
			if (!primed && !frames.empty() &&
				frames.back()->presentationTime - frames.front()->presentationTime >= 0.2 &&
				player.audioReady() && player.bufferedAudioDuration() >= 0.12) {
				primed = true;
				mediaOrigin = frames.front()->presentationTime;
				wallOrigin = elapsed + player.audioDeviceLatency();
				player.setAudioBuffering(false);
			}
			const double audioPosition = player.audioPlaybackPosition();
			const double presentationTime = audioPosition >= 0.0 ? audioPosition :
				mediaOrigin + elapsed - wallOrigin;
			if (primed && elapsed >= wallOrigin) {
				while (!frames.empty() && frames.front()->presentationTime <= presentationTime + 0.001) {
					if (firstPresentationElapsed < 0.0) firstPresentationElapsed = elapsed;
					const double gap = std::chrono::duration<double>(now - lastPresented).count();
					if (presented > 0.0 && gap > 0.25) {
						++presentationGaps;
						longestGap = std::max(longestGap, gap);
						std::cout << "Presentation gap " << gap << " seconds at "
							<< presented << " -> " << frames.front()->presentationTime
							<< ", audio=" << audioPosition
							<< ", queuedAudio=" << player.bufferedAudioDuration() << std::endl;
					}
					presented = frames.front()->presentationTime;
					player.setPresentedPosition(presented);
					frames.pop_front();
					lastPresented = now;
				}
			}
			if (!seekPlayback && switches < 5 && presented >= switchPosition + 2.0) {
				if (audioPosition < 0.0) {
					std::cerr << "Audio clock did not resume\n"; result = 1; break;
				}
				const auto previous = player.audioLanguage();
				if (switches == 4) player.resetAudioTrack();
				else player.cycleAudioTrack();
				if (switches == 0 && player.audioLanguage() == previous) {
					std::cerr << "Input must contain multiple audio tracks\n"; result = 1; break;
				}
				++switches;
				switchPosition = presented;
				std::cout << "Track change " << switches << " at " << presented << ": "
					<< previous << " -> " << player.audioLanguage() << std::endl;
			}
			if ((firstPresentationElapsed >= 0.0 &&
				std::chrono::duration<double>(now - lastPresented).count() > 4.0) ||
				elapsed > (seekPlayback ? 45.0 : 30.0)) {
				std::cerr << "Playback stalled: presented=" << presented << " audio=" << audioPosition
					<< " queuedAudio=" << player.bufferedAudioDuration() << " nextVideo="
					<< (frames.empty() ? -1.0 : frames.front()->presentationTime) << '\n';
				result = 1; break;
			}
			SDL_Delay(2);
		}
		if (seekPlayback) std::cout << "First presentation after " << firstPresentationElapsed
			<< " seconds. Presented " << presented - seekTime
			<< " seconds after seek, gaps over 250 ms: " << presentationGaps
			<< ", longest: " << longestGap << " seconds\n";
		if (seekPlayback && presentationGaps >= 3 && longestGap >= 0.5) {
			std::cerr << "Repeated playback hitches after seek\n";
			result = 1;
		}
		if (!result && player.audioPlaybackPosition() < 0.0) {
			std::cerr << (seekPlayback ? "Audio clock did not start after seek\n" :
				"Audio clock did not resume after resetting the track\n");
			result = 1;
		}
		player.close();
	}
	SDL_Quit();
	if (!result) std::cout << (seekPlayback ? "PASS: playback advances after seek\n" :
		"PASS: playback advances after cycling and resetting audio tracks\n");
	return result;
}
