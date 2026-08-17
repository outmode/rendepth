// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "VideoPlayer.h"

#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {
// SDL audio backends can block while waiting for their system audio server.
// Keep at most one such probe alive so a broken backend cannot accumulate threads.
std::atomic<bool> audioDeviceInitInProgress = false;
constexpr int noSubtitleTrack = -1;
constexpr int noSubtitleRequest = -2;

std::string lowerExtension(const std::filesystem::path& path) {
	auto extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension;
}

#ifdef RENDEPTH_ENABLE_FFMPEG
std::string ffmpegError(int code) {
	char message[AV_ERROR_MAX_STRING_SIZE]{};
	av_strerror(code, message, sizeof(message));
	return message;
}
#endif
}

struct VideoPlayer::Impl {
	struct SeekRequest {
		double seconds;
		bool fastPreview;
	};

	std::thread decodeThread;
	std::mutex stateMutex;
	std::condition_variable stateChanged;
	SDL_Surface* pendingFrame = nullptr;
	bool pendingFrameIsPreview = false;
	std::optional<SeekRequest> requestedSeek;
	std::string runtimeError;
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> isPlaying{false};
	std::atomic<bool> isReady{false};
	std::atomic<bool> atEnd{false};
	std::atomic<double> currentPosition{0.0};
	std::atomic<double> totalDuration{0.0};
	std::atomic<double> audioVolume{1.0};
	std::atomic<int> selectedAudioTrack{0};
	std::atomic<int> requestedAudioTrack{-1};
	std::vector<int> audioStreamIndices;
	std::vector<int> subtitleStreamIndices;
	std::atomic<int> selectedSubtitleTrack{noSubtitleTrack};
	std::atomic<int> requestedSubtitleTrack{noSubtitleRequest};
	struct SubtitleCue {
		double start = 0.0;
		double end = 0.0;
		std::string text;
	};
	mutable std::mutex subtitleMutex;
	mutable std::deque<SubtitleCue> subtitleCues;
	std::atomic<int> videoWidth{0};
	std::atomic<int> videoHeight{0};

#ifdef RENDEPTH_ENABLE_FFMPEG
	struct AudioState {
		std::mutex mutex;
		SDL_AudioStream* output = nullptr;
		std::deque<std::vector<float>> pending;
		int pendingBytes = 0;
		double queuedEnd = 0.0;
		bool clockValid = false;
		double lastClockPosition = 0.0;
		std::chrono::steady_clock::time_point lastClockUpdate{};
		bool closing = false;
		bool started = false;
		std::atomic<bool> playing = false;
		std::atomic<float> volume = 1.0f;

		void shutdown() {
			SDL_AudioStream* stream = nullptr;
			{
				std::lock_guard lock(mutex);
				closing = true;
				playing = false;
				stream = output;
				output = nullptr;
				pending.clear();
				pendingBytes = 0;
				started = false;
			}
			if (stream != nullptr) {
				SDL_PauseAudioStreamDevice(stream);
				SDL_DestroyAudioStream(stream);
			}
		}
	};

	AVFormatContext* format = nullptr;
	AVCodecContext* codec = nullptr;
	AVStream* stream = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	SwsContext* scaler = nullptr;
	int streamIndex = -1;
	double streamStart = 0.0;
	double fallbackFrameDuration = 1.0 / 30.0;
	std::shared_ptr<AudioState> audioState = std::make_shared<AudioState>();
	AVCodecContext* audioCodec = nullptr;
	AVStream* audioStream = nullptr;
	AVFrame* audioFrame = nullptr;
	SwrContext* audioResampler = nullptr;
	int audioStreamIndex = -1;
	AVCodecContext* subtitleCodec = nullptr;
	int subtitleStreamIndex = -1;
	double subtitleStreamStart = 0.0;
	double audioStreamStart = 0.0;
	static constexpr int audioSampleRate = 48000;
	static constexpr int audioChannels = 2;
	static constexpr int audioBytesPerFrame = audioChannels * static_cast<int>(sizeof(float));
	static constexpr int maxQueuedAudioBytes = audioSampleRate * audioBytesPerFrame * 3;

	bool prepareAudioDecoder() {
		if (SDL_WasInit(SDL_INIT_AUDIO) == 0) return false;

		const AVCodec* audioDecoder = nullptr;
		if (audioStreamIndices.empty()) return false;
		const int requested = requestedAudioTrack.load();
		const int track = requested >= 0 ? requested : selectedAudioTrack.load();
		audioStreamIndex = audioStreamIndices[std::clamp(track, 0,
			static_cast<int>(audioStreamIndices.size()) - 1)];
		audioDecoder = avcodec_find_decoder(format->streams[audioStreamIndex]->codecpar->codec_id);
		if (audioStreamIndex < 0 || audioDecoder == nullptr) {
			audioStreamIndex = -1;
			return false;
		}
		audioStream = format->streams[audioStreamIndex];

		audioCodec = avcodec_alloc_context3(audioDecoder);
		int result = audioCodec == nullptr ? AVERROR(ENOMEM) :
			avcodec_parameters_to_context(audioCodec, audioStream->codecpar);
		if (result >= 0 && audioCodec->sample_rate > 0 &&
			audioCodec->ch_layout.nb_channels > 0)
			result = avcodec_open2(audioCodec, audioDecoder, nullptr);

		AVChannelLayout inputLayout{};
		AVChannelLayout outputLayout{};
		if (result >= 0 && av_channel_layout_copy(&inputLayout,
			&audioCodec->ch_layout) >= 0) {
			av_channel_layout_default(&outputLayout, audioChannels);
			result = swr_alloc_set_opts2(&audioResampler,
				&outputLayout, AV_SAMPLE_FMT_FLT, audioSampleRate,
				&inputLayout, audioCodec->sample_fmt,
				audioCodec->sample_rate, 0, nullptr);
			if (result >= 0) result = swr_init(audioResampler);
		}
		av_channel_layout_uninit(&inputLayout);
		av_channel_layout_uninit(&outputLayout);
		audioFrame = result >= 0 ? av_frame_alloc() : nullptr;
		if (result < 0 || audioFrame == nullptr) {
			SDL_Log("Video audio is unavailable: %s", result < 0 ?
				ffmpegError(result).c_str() : "could not allocate decoder buffers");
			av_frame_free(&audioFrame);
			avcodec_free_context(&audioCodec);
			swr_free(&audioResampler);
			audioStream = nullptr;
			audioStreamIndex = -1;
			return false;
		}
		if (audioStream->start_time != AV_NOPTS_VALUE)
			audioStreamStart = audioStream->start_time * av_q2d(audioStream->time_base);
		return true;
	}

	bool prepareSubtitleDecoder() {
		if (selectedSubtitleTrack.load() == noSubtitleTrack || subtitleStreamIndices.empty()) {
			subtitleStreamIndex = -1;
			subtitleStreamStart = 0.0;
			return false;
		}
		const int track = std::clamp(selectedSubtitleTrack.load(), 0,
			static_cast<int>(subtitleStreamIndices.size()) - 1);
		subtitleStreamIndex = subtitleStreamIndices[track];
		const auto* streamInfo = format->streams[subtitleStreamIndex];
		subtitleStreamStart = streamInfo->start_time != AV_NOPTS_VALUE
			? streamInfo->start_time * av_q2d(streamInfo->time_base) : 0.0;
		const AVCodec* decoder = avcodec_find_decoder(streamInfo->codecpar->codec_id);
		if (decoder == nullptr) {
			subtitleStreamIndex = -1;
			return false;
		}
		subtitleCodec = avcodec_alloc_context3(decoder);
		if (subtitleCodec == nullptr || avcodec_parameters_to_context(
			subtitleCodec, streamInfo->codecpar) < 0 || avcodec_open2(subtitleCodec, decoder, nullptr) < 0) {
			avcodec_free_context(&subtitleCodec);
			subtitleStreamIndex = -1;
			return false;
		}
		return true;
	}

	static std::string subtitleRectText(const AVSubtitleRect* rect) {
		if (rect == nullptr) return {};
		std::string result = rect->text != nullptr ? rect->text :
			(rect->ass != nullptr ? rect->ass : "");
		if (rect->text == nullptr && !result.empty()) {
			// FFmpeg-generated ASS records use eight commas before the text;
			// raw Dialogue records use nine fields before the text.
			const bool fullDialogue = result.rfind("Dialogue:", 0) == 0;
			if (fullDialogue) result.erase(0, result.find(':') + 1);
			const int fieldsBeforeText = fullDialogue ? 9 : 8;
			size_t textStart = 0;
			for (int field = 0; field < fieldsBeforeText; ++field) {
				const auto comma = result.find(',', textStart);
				if (comma == std::string::npos) {
					textStart = 0;
					break;
				}
				textStart = comma + 1;
			}
			if (textStart > 0) result = result.substr(textStart);
		}
		for (size_t pos = 0; (pos = result.find('{', pos)) != std::string::npos;) {
			const auto end = result.find('}', pos);
			if (end == std::string::npos) break;
			result.erase(pos, end - pos + 1);
		}
		for (size_t pos = 0; (pos = result.find("\\N", pos)) != std::string::npos; ++pos)
			result.replace(pos, 2, "\n");
		return result;
	}

	void decodeSubtitlePacket(AVPacket* subtitlePacket) {
		if (subtitleCodec == nullptr) return;
		AVSubtitle subtitle{};
		int gotSubtitle = 0;
		if (avcodec_decode_subtitle2(subtitleCodec, &subtitle, &gotSubtitle, subtitlePacket) >= 0 && gotSubtitle) {
			std::string text;
			for (unsigned int i = 0; i < subtitle.num_rects; ++i) {
				const auto line = subtitleRectText(subtitle.rects[i]);
				if (!line.empty()) {
					if (!text.empty()) text += "\n";
					text += line;
				}
			}
			const auto* streamInfo = format->streams[subtitleStreamIndex];
			const double packetStart = subtitlePacket->pts == AV_NOPTS_VALUE ? currentPosition.load() :
				subtitlePacket->pts * av_q2d(streamInfo->time_base) - subtitleStreamStart;
			const double start = packetStart + subtitle.start_display_time / 1000.0;
			double duration = (subtitle.end_display_time - subtitle.start_display_time) / 1000.0;
			if (duration <= 0.0 && subtitlePacket->duration > 0)
				duration = subtitlePacket->duration * av_q2d(streamInfo->time_base);
			if (duration <= 0.0) {
				const auto readableDuration = 1.5 + text.size() * 0.045;
				duration = std::clamp(readableDuration, 2.0, 7.0);
			}
			{
				std::lock_guard lock(subtitleMutex);
				while (!subtitleCues.empty() && subtitleCues.front().end <= currentPosition.load())
					subtitleCues.pop_front();
				subtitleCues.push_back({ start, start + duration, text });
				while (subtitleCues.size() > 256) subtitleCues.pop_front();
			}
		}
		avsubtitle_free(&subtitle);
	}

	static void initializeAudioDevice(const std::shared_ptr<AudioState>& state) {
		if (audioDeviceInitInProgress.exchange(true)) return;
		SDL_AudioSpec audioSpec{ SDL_AUDIO_F32, audioChannels, audioSampleRate };
		SDL_AudioStream* openedAudio = SDL_OpenAudioDeviceStream(
			SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, nullptr, nullptr);
		audioDeviceInitInProgress = false;
		if (openedAudio == nullptr) {
			SDL_Log("Could not open video audio device: %s", SDL_GetError());
			return;
		}

		std::deque<std::vector<float>> queuedAudio;
		{
			std::lock_guard lock(state->mutex);
			if (state->closing) {
				SDL_DestroyAudioStream(openedAudio);
				return;
			}
			state->output = openedAudio;
			SDL_SetAudioStreamGain(openedAudio, state->volume.load());
			queuedAudio.swap(state->pending);
			state->pendingBytes = 0;
		}
		for (const auto& samples : queuedAudio) {
			std::lock_guard lock(state->mutex);
			if (state->closing || !SDL_PutAudioStreamData(openedAudio, samples.data(),
				static_cast<int>(samples.size() * sizeof(float)))) break;
		}
		if (state->playing) startAudio(state);
	}

	void setRuntimeError(const std::string& message) {
		{
			std::lock_guard lock(stateMutex);
			if (runtimeError.empty()) runtimeError = message;
		}
		isPlaying = false;
		isReady = false;
		stateChanged.notify_all();
	}

	static void startAudio(const std::shared_ptr<AudioState>& state) {
		std::lock_guard lock(state->mutex);
		if (state->closing || state->output == nullptr || state->started || !state->playing)
			return;
		if (!SDL_ResumeAudioStreamDevice(state->output)) {
			SDL_Log("Could not start video audio: %s", SDL_GetError());
			return;
		}
		state->started = true;
	}

	static void pauseAudio(const std::shared_ptr<AudioState>& state) {
		std::lock_guard lock(state->mutex);
		if (state->output != nullptr) SDL_PauseAudioStreamDevice(state->output);
	}

	bool getAudioClock(double& position) {
		const auto state = audioState;
		std::lock_guard lock(state->mutex);
		if (!state->clockValid || state->output == nullptr || !state->started ||
			!state->playing || audioVolume.load() == 0.0)
			return false;
		const int queuedBytes = SDL_GetAudioStreamQueued(state->output);
		if (queuedBytes < 0) return false;
		const auto now = std::chrono::steady_clock::now();
		const double clock = state->queuedEnd - queuedBytes /
			static_cast<double>(audioSampleRate * audioBytesPerFrame);
		if (state->lastClockUpdate.time_since_epoch().count() == 0 ||
			clock > state->lastClockPosition + 0.0005) {
			state->lastClockPosition = clock;
			state->lastClockUpdate = now;
		}
		if (now - state->lastClockUpdate > std::chrono::milliseconds(250))
			return false;
		position = clock;
		return true;
	}

	bool queueAudio(const float* samples, int sampleFrames, double endPosition) {
		const auto state = audioState;
		if (sampleFrames <= 0 || !isPlaying) return true;
		const int sampleBytes = sampleFrames * audioBytesPerFrame;
		const auto waitStart = std::chrono::steady_clock::now();
		for (;;) {
			bool waitForSpace = false;
			{
				std::lock_guard lock(state->mutex);
				if (state->closing || !isPlaying) return false;
				if (state->volume.load() == 0.0f) return true;
				if (state->output == nullptr) {
					if (state->pendingBytes + sampleBytes <= maxQueuedAudioBytes) {
						state->pending.emplace_back(samples, samples +
							static_cast<size_t>(sampleFrames) * audioChannels);
						state->pendingBytes += sampleBytes;
						state->queuedEnd = endPosition;
						state->clockValid = true;
					}
					return true;
				}
				if (SDL_GetAudioStreamQueued(state->output) <= maxQueuedAudioBytes) {
					if (!SDL_PutAudioStreamData(state->output, samples, sampleBytes)) {
						setRuntimeError(std::string("Could not queue video audio: ") + SDL_GetError());
						return false;
					}
					state->queuedEnd = endPosition;
					state->clockValid = true;
					return true;
				}
				waitForSpace = true;
			}
			if (!waitForSpace) return true;
			if (std::chrono::steady_clock::now() - waitStart >
				std::chrono::milliseconds(100))
				return true;
			if (stopRequested || !isPlaying || interrupted()) return false;
			std::unique_lock lock(stateMutex);
			stateChanged.wait_for(lock, std::chrono::milliseconds(10), [&] {
				return stopRequested || !isPlaying || requestedSeek.has_value();
			});
		}
	}

	bool interrupted() {
		std::lock_guard lock(stateMutex);
		return stopRequested || requestedSeek.has_value();
	}

	double framePosition(const AVFrame* decodedFrame) const {
		if (decodedFrame->best_effort_timestamp == AV_NOPTS_VALUE)
			return currentPosition.load() + fallbackFrameDuration;
		const double timestamp = decodedFrame->best_effort_timestamp * av_q2d(stream->time_base);
		return std::max(0.0, timestamp - streamStart);
	}

	bool waitForPresentation(double position, bool& clockValid, bool& audioClockActive,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin) {
		std::unique_lock lock(stateMutex);
		audioClockActive = false;
		for (;;) {
			if (stopRequested || requestedSeek.has_value()) return false;
			if (!isPlaying) {
				clockValid = false;
				stateChanged.wait(lock, [&] {
					return stopRequested || requestedSeek.has_value() || isPlaying.load();
				});
				continue;
			}
			double audioPosition = 0.0;
			if (getAudioClock(audioPosition)) {
				audioClockActive = true;
				if (position <= audioPosition + 0.005) return true;
				stateChanged.wait_for(lock, std::chrono::milliseconds(10), [&] {
					return stopRequested || requestedSeek.has_value() || !isPlaying.load();
				});
				continue;
			}

			const auto now = std::chrono::steady_clock::now();
			if (!clockValid || position < mediaOrigin || position - mediaOrigin > 60.0) {
				clockOrigin = now;
				mediaOrigin = position;
				clockValid = true;
			}
			const auto target = clockOrigin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(position - mediaOrigin));
			if (now >= target) return true;
			stateChanged.wait_until(lock, target, [&] {
				return stopRequested || requestedSeek.has_value() || !isPlaying.load();
			});
		}
	}

	bool publishFrame(AVFrame* decodedFrame, bool& clockValid,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin,
			std::optional<double>& seekFloor, bool& seekPreviewPending,
		bool& fastPreviewPending) {
		const double position = framePosition(decodedFrame);
		if (seekFloor.has_value() && position + fallbackFrameDuration < *seekFloor) return true;
		seekFloor.reset();
		const bool pausedSeekFrame = seekPreviewPending;
		const bool fastPreviewFrame = fastPreviewPending;
		bool audioClockActive = false;
		if (pausedSeekFrame) {
			if (interrupted()) return false;
			clockValid = false;
		} else if (!waitForPresentation(position, clockValid, audioClockActive,
			clockOrigin, mediaOrigin)) {
			return false;
		}

		if (!pausedSeekFrame && !audioClockActive) {
			const auto target = clockOrigin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(position - mediaOrigin));
			const auto presentationNow = std::chrono::steady_clock::now();
			const double lateness = std::chrono::duration<double>(
				presentationNow - target).count();
			if (lateness > std::max(0.1, fallbackFrameDuration * 3.0)) {
				clockOrigin = presentationNow;
				mediaOrigin = position;
				currentPosition = position;
				return true;
			}
		}

		constexpr int previewMaxDimension = 720;
		const int sourceMaxDimension = std::max(decodedFrame->width, decodedFrame->height);
		const double previewScale = fastPreviewFrame && sourceMaxDimension > previewMaxDimension
			? static_cast<double>(previewMaxDimension) / sourceMaxDimension : 1.0;
		const int outputWidth = std::max(1,
			static_cast<int>(std::lround(decodedFrame->width * previewScale)));
		const int outputHeight = std::max(1,
			static_cast<int>(std::lround(decodedFrame->height * previewScale)));
		scaler = sws_getCachedContext(scaler,
			decodedFrame->width, decodedFrame->height,
			static_cast<AVPixelFormat>(decodedFrame->format),
			outputWidth, outputHeight, AV_PIX_FMT_RGBA,
			fastPreviewFrame ? SWS_FAST_BILINEAR : SWS_BILINEAR,
			nullptr, nullptr, nullptr);
		if (scaler == nullptr) {
			setRuntimeError("FFmpeg could not create the video color converter.");
			return false;
		}

		SDL_Surface* converted = SDL_CreateSurface(
			outputWidth, outputHeight, SDL_PIXELFORMAT_RGBA32);
		if (converted == nullptr) {
			setRuntimeError(std::string("Could not allocate a video frame: ") + SDL_GetError());
			return false;
		}
		uint8_t* outputData[] = { static_cast<uint8_t*>(converted->pixels), nullptr, nullptr, nullptr };
		int outputLines[] = { converted->pitch, 0, 0, 0 };
		const int convertedRows = sws_scale(scaler, decodedFrame->data,
			decodedFrame->linesize, 0, decodedFrame->height, outputData, outputLines);
		if (convertedRows <= 0) {
			SDL_DestroySurface(converted);
			setRuntimeError("FFmpeg could not convert the decoded video frame.");
			return false;
		}

		{
			std::lock_guard lock(stateMutex);
			if (pendingFrame != nullptr) SDL_DestroySurface(pendingFrame);
			pendingFrame = converted;
			pendingFrameIsPreview = fastPreviewFrame;
		}
		videoWidth = decodedFrame->width;
		videoHeight = decodedFrame->height;
		currentPosition = position;
		if (isPlaying) startAudio(audioState);
		seekPreviewPending = false;
		fastPreviewPending = false;
		return true;
	}

	bool receiveFrames(bool& clockValid,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin,
			std::optional<double>& seekFloor, bool& seekPreviewPending,
			bool& fastPreviewPending) {
		for (;;) {
			const int result = avcodec_receive_frame(codec, frame);
			if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
			if (result < 0) {
				setRuntimeError("FFmpeg video decode failed: " + ffmpegError(result));
				return false;
			}
			const bool previewWasPending = seekPreviewPending;
			const bool published = publishFrame(frame, clockValid, clockOrigin, mediaOrigin,
				seekFloor, seekPreviewPending, fastPreviewPending);
			av_frame_unref(frame);
			if (!published) return false;
			if (previewWasPending && !seekPreviewPending) return true;
		}
	}

	bool receiveAudioFrames(std::optional<double>& audioSeekFloor, bool discardAudio) {
		for (;;) {
			const int result = avcodec_receive_frame(audioCodec, audioFrame);
			if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
			if (result < 0) {
				setRuntimeError("FFmpeg audio decode failed: " + ffmpegError(result));
				return false;
			}

			const double position = audioFrame->best_effort_timestamp == AV_NOPTS_VALUE
				? currentPosition.load()
				: std::max(0.0, audioFrame->best_effort_timestamp *
					av_q2d(audioStream->time_base) - audioStreamStart);
			const double frameDuration = audioFrame->sample_rate > 0
				? audioFrame->nb_samples / static_cast<double>(audioFrame->sample_rate)
				: 0.0;
			if (audioSeekFloor.has_value()) {
				if (position + frameDuration < *audioSeekFloor) {
					av_frame_unref(audioFrame);
					continue;
				}
				audioSeekFloor.reset();
			}

			if (isPlaying && !discardAudio && audioResampler != nullptr) {
				const int64_t requiredSamples = av_rescale_rnd(
					swr_get_delay(audioResampler, audioCodec->sample_rate) + audioFrame->nb_samples,
					audioSampleRate, audioCodec->sample_rate, AV_ROUND_UP);
				const int outputSamples = static_cast<int>(std::min<int64_t>(
					requiredSamples, std::numeric_limits<int>::max() / audioBytesPerFrame));
				if (outputSamples > 0) {
					std::vector<float> samples(static_cast<size_t>(outputSamples) * audioChannels);
					uint8_t* output[] = { reinterpret_cast<uint8_t*>(samples.data()) };
					const int converted = swr_convert(audioResampler, output, outputSamples,
						audioFrame->extended_data, audioFrame->nb_samples);
					if (converted < 0) {
						setRuntimeError("FFmpeg could not convert the video audio.");
						av_frame_unref(audioFrame);
						return false;
					}
					if (!queueAudio(samples.data(), converted,
						position + converted / static_cast<double>(audioSampleRate))) {
						av_frame_unref(audioFrame);
						return false;
					}
				}
			}
			av_frame_unref(audioFrame);
		}
	}

	bool applySeek(const SeekRequest& request, std::optional<double>& seekFloor,
			std::optional<double>& audioSeekFloor) {
		const int64_t target = static_cast<int64_t>(
			(request.seconds + streamStart) / av_q2d(stream->time_base));
		const int result = av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD);
		if (result < 0) {
			setRuntimeError("FFmpeg seek failed: " + ffmpegError(result));
			return false;
		}
		avcodec_flush_buffers(codec);
		if (audioCodec != nullptr) avcodec_flush_buffers(audioCodec);
		if (audioResampler != nullptr) {
			swr_close(audioResampler);
			if (swr_init(audioResampler) < 0) {
				setRuntimeError("FFmpeg could not reset the video audio converter.");
				return false;
			}
		}
		codec->skip_frame = request.fastPreview ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
		av_packet_unref(packet);
		av_frame_unref(frame);
		if (audioFrame != nullptr) av_frame_unref(audioFrame);
		{
			std::lock_guard lock(audioState->mutex);
			audioState->pending.clear();
			audioState->pendingBytes = 0;
			audioState->queuedEnd = request.seconds;
			audioState->clockValid = false;
			audioState->lastClockUpdate = {};
			if (audioState->output != nullptr) {
				SDL_PauseAudioStreamDevice(audioState->output);
				SDL_ClearAudioStream(audioState->output);
				audioState->started = false;
			}
		}
		{
			std::lock_guard lock(subtitleMutex);
			subtitleCues.clear();
		}
		currentPosition = request.seconds;
		atEnd = false;
		seekFloor = request.seconds;
		audioSeekFloor = audioCodec != nullptr ? std::optional(request.seconds) : std::nullopt;
		return true;
	}

	void decodeLoop() {
		if (prepareAudioDecoder()) {
			const auto state = audioState;
			// Do not join this thread: some backends never return from device open.
			std::thread(&Impl::initializeAudioDevice, state).detach();
		}
		prepareSubtitleDecoder();
		bool clockValid = false;
		auto clockOrigin = std::chrono::steady_clock::now();
		double mediaOrigin = 0.0;
		std::optional<double> seekFloor;
		std::optional<double> audioSeekFloor;
		bool seekPreviewPending = false;
		bool fastPreviewPending = false;
		bool discardAudio = false;

		while (!stopRequested) {
			const int requestedTrack = requestedAudioTrack.exchange(-1);
			if (requestedTrack >= 0 && requestedTrack != selectedAudioTrack.load()) {
				selectedAudioTrack = requestedTrack;
				std::lock_guard lock(audioState->mutex);
				if (audioState->output != nullptr) SDL_ClearAudioStream(audioState->output);
				audioState->queuedEnd = currentPosition.load();
				audioState->clockValid = false;
				audioState->lastClockUpdate = {};
				if (audioCodec != nullptr) avcodec_free_context(&audioCodec);
				av_frame_free(&audioFrame);
				swr_free(&audioResampler);
				audioStreamIndex = -1;
				prepareAudioDecoder();
			}
			const int requestedSubtitle = requestedSubtitleTrack.exchange(noSubtitleRequest);
			if (requestedSubtitle != noSubtitleRequest && requestedSubtitle != selectedSubtitleTrack.load()) {
				selectedSubtitleTrack = requestedSubtitle;
				avcodec_free_context(&subtitleCodec);
				subtitleStreamIndex = -1;
				prepareSubtitleDecoder();
				std::lock_guard lock(subtitleMutex);
				subtitleCues.clear();
			}
			std::optional<SeekRequest> seek;
			{
				std::unique_lock lock(stateMutex);
				stateChanged.wait(lock, [&] {
					return stopRequested || requestedSeek.has_value() || isPlaying.load() ||
						seekPreviewPending;
				});
				if (stopRequested) break;
				seek.swap(requestedSeek);
			}
			if (seek.has_value()) {
				if (!applySeek(*seek, seekFloor, audioSeekFloor)) break;
				discardAudio = seek->fastPreview;
				clockValid = false;
				seekPreviewPending = !isPlaying.load();
				fastPreviewPending = seek->fastPreview;
			}
			if (!isPlaying && !seekPreviewPending) continue;

			const int readResult = av_read_frame(format, packet);
			if (readResult == AVERROR_EOF) {
				const int flushResult = avcodec_send_packet(codec, nullptr);
				if (flushResult < 0 && flushResult != AVERROR_EOF) {
					setRuntimeError("FFmpeg could not finish decoding the video: " +
						ffmpegError(flushResult));
					break;
				}
				if (flushResult >= 0 && !receiveFrames(clockValid, clockOrigin,
						mediaOrigin, seekFloor, seekPreviewPending, fastPreviewPending)) {
					if (interrupted()) continue;
					break;
				}
				if (totalDuration > 0.0) {
					if (!applySeek({0.0, false}, seekFloor, audioSeekFloor)) break;
					clockValid = false;
					seekPreviewPending = false;
					fastPreviewPending = false;
					continue;
				}
				atEnd = true;
				isPlaying = false;
				seekPreviewPending = false;
				fastPreviewPending = false;
				if (totalDuration > 0.0) currentPosition = totalDuration.load();
				continue;
			}
			if (readResult < 0) {
				setRuntimeError("FFmpeg could not read the video: " + ffmpegError(readResult));
				break;
			}

			if (packet->stream_index == streamIndex) {
				const int sendResult = avcodec_send_packet(codec, packet);
				av_packet_unref(packet);
				if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
					setRuntimeError("FFmpeg could not submit a video packet: " + ffmpegError(sendResult));
					break;
				}
				if (!receiveFrames(clockValid, clockOrigin, mediaOrigin, seekFloor,
						seekPreviewPending, fastPreviewPending)) {
					if (interrupted()) continue;
					break;
				}
			} else if (packet->stream_index == audioStreamIndex && audioCodec != nullptr) {
				const int sendResult = avcodec_send_packet(audioCodec, packet);
				av_packet_unref(packet);
				if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
					setRuntimeError("FFmpeg could not submit an audio packet: " + ffmpegError(sendResult));
					break;
				}
				if (!receiveAudioFrames(audioSeekFloor, discardAudio)) {
					if (interrupted()) continue;
					break;
				}
			} else if (packet->stream_index == subtitleStreamIndex) {
				decodeSubtitlePacket(packet);
				av_packet_unref(packet);
			} else {
				av_packet_unref(packet);
			}
		}
	}
#endif
};

bool VideoPlayer::supported(const std::filesystem::path& path) {
	const auto extension = lowerExtension(path);
	return extension == ".mp4" || extension == ".m4v" || extension == ".mov" ||
		extension == ".mkv" || extension == ".webm" || extension == ".avi" ||
		extension == ".wmv" || extension == ".mpeg" || extension == ".mpg";
}

VideoPlayer::VideoPlayer() : impl(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() { close(); }

bool VideoPlayer::open(const std::filesystem::path& path, std::string& error) {
	close();
#ifndef RENDEPTH_ENABLE_FFMPEG
	error = "FFmpeg video playback was not enabled in this build.";
	(void)path;
	return false;
#else
	int result = avformat_open_input(&impl->format, path.string().c_str(), nullptr, nullptr);
	if (result < 0) {
		error = "FFmpeg could not open the video: " + ffmpegError(result);
		close();
		return false;
	}
	result = avformat_find_stream_info(impl->format, nullptr);
	if (result < 0) {
		error = "FFmpeg could not inspect the video: " + ffmpegError(result);
		close();
		return false;
	}

	const AVCodec* decoder = nullptr;
	impl->streamIndex = av_find_best_stream(
		impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (impl->streamIndex < 0 || decoder == nullptr) {
		error = "FFmpeg could not find a supported video stream.";
		close();
		return false;
	}
	impl->stream = impl->format->streams[impl->streamIndex];
	impl->audioStreamIndices.clear();
	impl->subtitleStreamIndices.clear();
	for (unsigned int i = 0; i < impl->format->nb_streams; ++i) {
		if (impl->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			impl->audioStreamIndices.push_back(static_cast<int>(i));
		else if (impl->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
			impl->subtitleStreamIndices.push_back(static_cast<int>(i));
	}
	impl->selectedAudioTrack = 0;
	impl->requestedAudioTrack = -1;
	impl->selectedSubtitleTrack = noSubtitleTrack;
	impl->requestedSubtitleTrack = noSubtitleRequest;
	if (impl->stream->codecpar->codec_id == AV_CODEC_ID_AV1) {
		decoder = avcodec_find_decoder_by_name("libdav1d");
		if (decoder == nullptr) {
			error = "FFmpeg was built without the libdav1d AV1 decoder.";
			close();
			return false;
		}
	}
	impl->codec = avcodec_alloc_context3(decoder);
	if (impl->codec == nullptr) {
		error = "FFmpeg could not allocate the video decoder.";
		close();
		return false;
	}
	result = avcodec_parameters_to_context(impl->codec, impl->stream->codecpar);
	impl->codec->thread_count = static_cast<int>(
		std::clamp(std::thread::hardware_concurrency(), 1u, 16u));
	impl->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	if (result < 0 || (result = avcodec_open2(impl->codec, decoder, nullptr)) < 0) {
		error = "FFmpeg could not start the video decoder: " + ffmpegError(result);
		close();
		return false;
	}
	impl->packet = av_packet_alloc();
	impl->frame = av_frame_alloc();
	if (impl->packet == nullptr || impl->frame == nullptr) {
		error = "FFmpeg could not allocate decoder buffers.";
		close();
		return false;
	}

	if (impl->stream->start_time != AV_NOPTS_VALUE)
		impl->streamStart = impl->stream->start_time * av_q2d(impl->stream->time_base);
	if (impl->format->duration != AV_NOPTS_VALUE && impl->format->duration > 0)
		impl->totalDuration = static_cast<double>(impl->format->duration) / AV_TIME_BASE;
	else if (impl->stream->duration != AV_NOPTS_VALUE && impl->stream->duration > 0)
		impl->totalDuration = impl->stream->duration * av_q2d(impl->stream->time_base);

	const AVRational frameRate = av_guess_frame_rate(impl->format, impl->stream, nullptr);
	if (frameRate.num > 0 && frameRate.den > 0)
		impl->fallbackFrameDuration = av_q2d(av_inv_q(frameRate));

	impl->stopRequested = false;
	impl->isPlaying = true;
	impl->audioState->playing = true;
	impl->isReady = true;
	impl->atEnd = false;
	impl->videoWidth = impl->codec->width;
	impl->videoHeight = impl->codec->height;
	impl->decodeThread = std::thread(&Impl::decodeLoop, impl.get());
	return true;
#endif
}

void VideoPlayer::close() {
	impl->stopRequested = true;
	impl->stateChanged.notify_all();
	if (impl->decodeThread.joinable()) impl->decodeThread.join();
	{
		std::lock_guard lock(impl->stateMutex);
		if (impl->pendingFrame != nullptr) {
			SDL_DestroySurface(impl->pendingFrame);
			impl->pendingFrame = nullptr;
		}
		impl->pendingFrameIsPreview = false;
		impl->requestedSeek.reset();
		impl->runtimeError.clear();
	}
#ifdef RENDEPTH_ENABLE_FFMPEG
	impl->audioState->shutdown();
	impl->audioState = std::make_shared<Impl::AudioState>();
	impl->audioState->volume = static_cast<float>(impl->audioVolume.load());
	swr_free(&impl->audioResampler);
	av_frame_free(&impl->audioFrame);
	avcodec_free_context(&impl->audioCodec);
	impl->audioStream = nullptr;
	impl->audioStreamIndex = -1;
	impl->audioStreamStart = 0.0;
	sws_freeContext(impl->scaler);
	impl->scaler = nullptr;
	av_frame_free(&impl->frame);
	av_packet_free(&impl->packet);
	avcodec_free_context(&impl->codec);
	avformat_close_input(&impl->format);
	impl->stream = nullptr;
	impl->streamIndex = -1;
	impl->audioStreamIndices.clear();
	impl->subtitleStreamIndices.clear();
	impl->selectedAudioTrack = 0;
	impl->requestedAudioTrack = -1;
	impl->selectedSubtitleTrack = noSubtitleTrack;
	impl->requestedSubtitleTrack = noSubtitleRequest;
	#ifdef RENDEPTH_ENABLE_FFMPEG
	avcodec_free_context(&impl->subtitleCodec);
	impl->subtitleStreamIndex = -1;
	impl->subtitleStreamStart = 0.0;
	#endif
	{
		std::lock_guard lock(impl->subtitleMutex);
		impl->subtitleCues.clear();
	}
	impl->streamStart = 0.0;
	impl->fallbackFrameDuration = 1.0 / 30.0;
#endif
	impl->isPlaying = false;
	impl->isReady = false;
	impl->atEnd = false;
	impl->currentPosition = 0.0;
	impl->totalDuration = 0.0;
	impl->videoWidth = 0;
	impl->videoHeight = 0;
}

void VideoPlayer::setPlaying(bool playing) {
	if (!impl->isReady) return;
	{
		std::lock_guard lock(impl->stateMutex);
		if (playing && impl->atEnd) impl->requestedSeek = Impl::SeekRequest{0.0, false};
		impl->isPlaying = playing;
	}
#ifdef RENDEPTH_ENABLE_FFMPEG
	impl->audioState->playing = playing;
	if (playing) Impl::startAudio(impl->audioState);
	else Impl::pauseAudio(impl->audioState);
#endif
	impl->stateChanged.notify_all();
}

void VideoPlayer::setVolume(double volume) {
	volume = std::clamp(volume, 0.0, 1.0);
	if (volume < 0.01) volume = 0.0;
	impl->audioVolume = volume;
#ifdef RENDEPTH_ENABLE_FFMPEG
	impl->audioState->volume = static_cast<float>(volume);
	bool resume = false;
	{
		std::lock_guard lock(impl->audioState->mutex);
		if (volume == 0.0) {
			impl->audioState->pending.clear();
			impl->audioState->pendingBytes = 0;
			impl->audioState->clockValid = false;
			impl->audioState->lastClockUpdate = {};
		}
		if (impl->audioState->output != nullptr) {
			SDL_SetAudioStreamGain(impl->audioState->output, static_cast<float>(volume));
			if (volume == 0.0) {
				SDL_ClearAudioStream(impl->audioState->output);
				SDL_PauseAudioStreamDevice(impl->audioState->output);
				impl->audioState->started = false;
			} else if (impl->audioState->playing && !impl->audioState->started) {
				resume = true;
			}
		}
	}
	if (resume) Impl::startAudio(impl->audioState);
#endif
}

void VideoPlayer::cycleAudioTrack() {
	if (!impl->isReady || impl->audioStreamIndices.size() < 2) return;
	const int requested = impl->requestedAudioTrack.load();
	const int current = requested >= 0 ? requested : impl->selectedAudioTrack.load();
	const int next = (current + 1) %
		static_cast<int>(impl->audioStreamIndices.size());
	impl->requestedAudioTrack = next;
	impl->stateChanged.notify_all();
}

void VideoPlayer::cycleSubtitleTrack() {
	if (!impl->isReady || impl->subtitleStreamIndices.empty()) return;
	const int requested = impl->requestedSubtitleTrack.load();
	const int current = requested != noSubtitleRequest ? requested :
		impl->selectedSubtitleTrack.load();
	const int next = current == noSubtitleTrack ? 0 :
		current + 1 < static_cast<int>(impl->subtitleStreamIndices.size())
			? current + 1 : noSubtitleTrack;
	impl->requestedSubtitleTrack = next;
	impl->stateChanged.notify_all();
}

std::string VideoPlayer::subtitleText() const {
	const double position = impl->currentPosition.load();
	std::lock_guard lock(impl->subtitleMutex);
	while (!impl->subtitleCues.empty() && impl->subtitleCues.front().end <= position)
		impl->subtitleCues.pop_front();
	for (const auto& cue : impl->subtitleCues) {
		if (cue.start > position) break;
		if (position < cue.end) return cue.text;
	}
	return {};
}

namespace {
#ifdef RENDEPTH_ENABLE_FFMPEG
std::string streamLanguage(const AVStream* stream) {
	if (stream == nullptr) return "Unknown";
	if (const AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0))
		if (title->value != nullptr && title->value[0] != '\0') return title->value;
	if (const AVDictionaryEntry* handler = av_dict_get(stream->metadata, "handler_name", nullptr, 0))
		if (handler->value != nullptr && handler->value[0] != '\0') return handler->value;
	if (const AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0)) {
		std::string code = language->value != nullptr ? language->value : "";
		std::transform(code.begin(), code.end(), code.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		if (code == "eng" || code == "en") return "English";
		if (code == "spa" || code == "es") return "Spanish";
		if (code == "fra" || code == "fre" || code == "fr") return "French";
		if (code == "deu" || code == "ger" || code == "de") return "German";
		if (code == "ita" || code == "it") return "Italian";
		if (code == "por" || code == "pt") return "Portuguese";
		if (code == "rus" || code == "ru") return "Russian";
		if (code == "jpn" || code == "ja") return "Japanese";
		if (code == "kor" || code == "ko") return "Korean";
		if (code == "zho" || code == "chi" || code == "zh") return "Chinese";
		if (code == "nld" || code == "dut" || code == "nl") return "Dutch";
		if (code == "pol" || code == "pl") return "Polish";
		if (code == "tur" || code == "tr") return "Turkish";
		return code.empty() ? "Unknown" : code;
	}
	return "Unknown";
}
#endif
}

std::string VideoPlayer::audioLanguage() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (impl->format != nullptr && !impl->audioStreamIndices.empty()) {
		const auto requested = impl->requestedAudioTrack.load();
		const auto track = requested >= 0 ? requested : impl->selectedAudioTrack.load();
		const auto index = impl->audioStreamIndices[std::clamp(track, 0,
			static_cast<int>(impl->audioStreamIndices.size()) - 1)];
		return streamLanguage(impl->format->streams[index]);
	}
#endif
	return "Unknown";
}

std::string VideoPlayer::subtitleLanguage() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (impl->format != nullptr && !impl->subtitleStreamIndices.empty()) {
		const auto requested = impl->requestedSubtitleTrack.load();
		const auto track = requested != noSubtitleRequest ? requested : impl->selectedSubtitleTrack.load();
		if (track == noSubtitleTrack) return "None";
		const auto index = impl->subtitleStreamIndices[std::clamp(track, 0,
			static_cast<int>(impl->subtitleStreamIndices.size()) - 1)];
		return streamLanguage(impl->format->streams[index]);
	}
	return "None";
#endif
	return "None";
}

void VideoPlayer::seek(double seconds, bool fastPreview) {
	if (!impl->isReady) return;
	seconds = std::clamp(seconds, 0.0,
		impl->totalDuration > 0.0 ? impl->totalDuration.load() : seconds);
	{
		std::lock_guard lock(impl->stateMutex);
		impl->requestedSeek = Impl::SeekRequest{seconds, fastPreview};
		impl->currentPosition = seconds;
		impl->atEnd = false;
	}
	impl->stateChanged.notify_all();
}

void VideoPlayer::update() {}

SDL_Surface* VideoPlayer::takeFrame(bool* preview) {
	std::lock_guard lock(impl->stateMutex);
	SDL_Surface* result = impl->pendingFrame;
	if (preview != nullptr) *preview = result != nullptr && impl->pendingFrameIsPreview;
	impl->pendingFrame = nullptr;
	impl->pendingFrameIsPreview = false;
	return result;
}

std::string VideoPlayer::takeError() {
	std::lock_guard lock(impl->stateMutex);
	std::string result;
	result.swap(impl->runtimeError);
	return result;
}

bool VideoPlayer::playing() const { return impl->isPlaying; }
bool VideoPlayer::ready() const { return impl->isReady; }
double VideoPlayer::position() const { return impl->currentPosition; }
double VideoPlayer::duration() const { return impl->totalDuration; }
int VideoPlayer::width() const { return impl->videoWidth; }
int VideoPlayer::height() const { return impl->videoHeight; }
