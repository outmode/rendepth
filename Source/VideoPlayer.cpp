// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "VideoPlayer.h"
#include "AudioCdReader.h"
#include "DiscSource.h"
#include "BlurayReader.h"
#include "MvcDecoder.h"
#include "DvdReader.h"
#include "Core.h"

#include <SDL3_image/SDL_image.h>

#ifdef RENDEPTH_ENABLE_FFMPEG
#include "DiscReadAhead.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/motion_vector.h>
#include <libavutil/pixdesc.h>
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
#include <utility>
#include <vector>

namespace {
// SDL audio backends can block while waiting for their system audio server.
// Keep at most one such probe alive so a broken backend cannot accumulate threads.
std::atomic<bool> audioDeviceInitInProgress = false;
constexpr int noSubtitleTrack = -1;
constexpr int noSubtitleRequest = -2;

// Normalize file extensions for supported-format checks.
std::string lowerExtension(const std::filesystem::path& path) {
	auto extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension;
}

#ifdef RENDEPTH_ENABLE_FFMPEG
// Convert an FFmpeg error code into a readable diagnostic.
std::string ffmpegError(int code) {
	char message[AV_ERROR_MAX_STRING_SIZE]{};
	av_strerror(code, message, sizeof(message));
	return message;
}


struct HardwareDecodeState {
	std::atomic<bool> failed{false};
};

// Prefer VA-API when usable, otherwise select a software pixel format offered by the decoder.
enum AVPixelFormat selectHardwareFormat(AVCodecContext* context, const enum AVPixelFormat* formats) {
	auto* state = static_cast<HardwareDecodeState*>(context->opaque);
	const bool failed = state != nullptr && state->failed.load();
	if (!failed) {
		for (const auto* format = formats; *format != AV_PIX_FMT_NONE; ++format)
			if (*format == AV_PIX_FMT_VAAPI) return *format;
	}
	for (const auto* format = formats; *format != AV_PIX_FMT_NONE; ++format)
		if ((av_pix_fmt_desc_get(*format)->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) return *format;
	return formats[0];
}

// Rotate a pitched pixel plane into tightly packed output storage.
template <typename T>
void rotateBufferFromPitch(int rotation, const T* src, int srcPitchElements, int srcW, int srcH, std::vector<std::uint8_t>& dstBytes) {
	dstBytes.resize(static_cast<size_t>(srcW) * srcH * sizeof(T));
	T* out = reinterpret_cast<T*>(dstBytes.data());
	if (rotation == 90) {
		for (int y = 0; y < srcH; ++y) {
			const T* row = src + y * srcPitchElements;
			for (int x = 0; x < srcW; ++x) {
				out[x * srcH + (srcH - 1 - y)] = row[x];
			}
		}
	} else if (rotation == 180) {
		for (int y = 0; y < srcH; ++y) {
			const T* row = src + y * srcPitchElements;
			for (int x = 0; x < srcW; ++x) {
				out[(srcH - 1 - y) * srcW + (srcW - 1 - x)] = row[x];
			}
		}
	} else if (rotation == 270) {
		for (int y = 0; y < srcH; ++y) {
			const T* row = src + y * srcPitchElements;
			for (int x = 0; x < srcW; ++x) {
				out[(srcW - 1 - x) * srcH + y] = row[x];
			}
		}
	}
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
	struct QueuedVideoFrame {
		std::shared_ptr<VideoFrame> frame;
		bool isPreview = false;
	};
	std::deque<QueuedVideoFrame> pendingFrames;
	static constexpr size_t maxQueuedVideoFrames = 24;
	std::optional<SeekRequest> requestedSeek;
	std::string runtimeError;
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> isPlaying{false};
	std::atomic<bool> isReady{false};
	std::atomic<bool> atEnd{false};
	std::atomic<double> currentPosition{0.0};
	std::atomic<double> presentedPosition{0.0};
	std::atomic<double> totalDuration{0.0};
	std::atomic<double> audioVolume{1.0};
	std::atomic<bool> audioSeekPending{false};
	bool audioOnly = false;
	mutable std::mutex waveformMutex;
	AudioWaveform waveform;
	SDL_Surface* albumArt = nullptr;
	std::atomic<int> selectedAudioTrack{0};
	std::atomic<int> requestedAudioTrack{-1};
	std::vector<int> audioStreamIndices;
	std::vector<int> subtitleStreamIndices;
	std::atomic<int> selectedSubtitleTrack{noSubtitleTrack};
	std::atomic<int> requestedSubtitleTrack{noSubtitleRequest};
	struct SubtitleCue {
		double start = 0.0;
		double end = 0.0;
		std::shared_ptr<const VideoSubtitle> subtitle;
	};
	mutable std::mutex subtitleMutex;
	mutable std::deque<SubtitleCue> subtitleCues;
	std::atomic<int> videoWidth{0};
	std::atomic<int> videoHeight{0};
	std::atomic<int> rotation{0};
	std::atomic<int> outputMaxWidth{0};
	std::atomic<int> outputMaxHeight{0};
	std::atomic<int> inferenceMaxDimension{0};
	std::atomic<double> inferenceFramesPerSecond{10.0};
	std::atomic<std::uint64_t> playbackGeneration{1};
	double lastInferencePosition = -1.0;
	double nextInferencePosition = -1.0;
	std::uint64_t lastInferenceGeneration = 0;

#ifdef RENDEPTH_ENABLE_FFMPEG
	AVIOContext* avioContext = nullptr;
	std::unique_ptr<DiscReadAhead> discReadAhead;
	std::shared_ptr<std::mutex> discReadMutex;
	AVBufferRef* hwDeviceContext = nullptr;
	HardwareDecodeState hardwareDecodeState{};
	bool retryVideoPacketAfterHardwareFallback = false;
	struct AudioState {
		std::mutex mutex;
		SDL_AudioStream* output = nullptr;
		std::deque<std::vector<float>> pending;
		int pendingBytes = 0;
		bool closing = false;
		bool started = false;
		std::atomic<bool> buffering{true};
		std::atomic<bool> playing = false;
		std::atomic<float> volume = 1.0f;

		// Detach the audio stream under its lock, then stop and destroy it outside the lock.
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
	std::unique_ptr<BlurayReader> blurayReader;
	std::unique_ptr<MvcDecoder> mvcDecoder;
	std::unique_ptr<DvdReader> dvdReader;
	std::unique_ptr<AudioCdReader> audioCdReader;
	std::vector<double> discChapterTimes;
	AVCodecContext* codec = nullptr;
	const AVCodec* videoDecoder = nullptr;
	AVStream* stream = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	SwsContext* scaler = nullptr;
	SwsContext* inferenceScaler = nullptr;
	int streamIndex = -1;
	double streamStart = 0.0;
	double fallbackFrameDuration = 1.0 / 30.0;
	std::shared_ptr<AudioState> audioState = std::make_shared<AudioState>();
	AVCodecContext* audioCodec = nullptr;
	AVStream* audioStream = nullptr;
	AVFrame* audioFrame = nullptr;
	SwrContext* audioResampler = nullptr;
	int audioStreamIndex = -1;
	std::atomic<double> audioQueuePosition{0.0};
	std::atomic<bool> audioQueuePositionValid{false};
	AVCodecContext* subtitleCodec = nullptr;
	int subtitleStreamIndex = -1;
	static constexpr int audioSampleRate = 48000;
	static constexpr int audioChannels = 2;
	static constexpr int audioBytesPerFrame = audioChannels * static_cast<int>(sizeof(float));
	static constexpr int maxQueuedAudioBytes = audioSampleRate * audioBytesPerFrame * 3;

	// Decode embedded cover art into an oriented surface suitable for the audio display.
	SDL_Surface* decodeAlbumArt() {
		if (format == nullptr) return nullptr;
		for (unsigned int index = 0; index < format->nb_streams; ++index) {
			const auto* artStream = format->streams[index];
			if (artStream->attached_pic.size <= 0 || artStream->attached_pic.data == nullptr)
				continue;
			SDL_IOStream* io = SDL_IOFromConstMem(artStream->attached_pic.data,
				static_cast<size_t>(artStream->attached_pic.size));
			if (io == nullptr) continue;
			SDL_Surface* loaded = IMG_Load_IO(io, true);
			if (loaded == nullptr) continue;
			loaded = Core::orientSurface(loaded);
			if (loaded == nullptr) continue;
			if (loaded->format == SDL_PIXELFORMAT_ABGR8888) return loaded;
			SDL_Surface* result = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
			SDL_DestroySurface(loaded);
			if (result != nullptr) return result;
		}
		return nullptr;
	}

	// Open the requested audio stream decoder and prepare conversion to the playback sample format.
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
		return true;
	}

	// Open the selected subtitle decoder, leaving subtitles disabled when no track is selected.
	bool prepareSubtitleDecoder() {
		if (selectedSubtitleTrack.load() == noSubtitleTrack || subtitleStreamIndices.empty()) {
			subtitleStreamIndex = -1;
			return false;
		}
		const int track = std::clamp(selectedSubtitleTrack.load(), 0,
			static_cast<int>(subtitleStreamIndices.size()) - 1);
		subtitleStreamIndex = subtitleStreamIndices[track];
		const auto* streamInfo = format->streams[subtitleStreamIndex];
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

	// Extract displayable text from plain or ASS subtitle records, stripping formatting tags.
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

	// Composite paletted subtitle rectangles into a cropped RGBA overlay with video-relative
	// positioning.
	static std::shared_ptr<VideoSubtitle> subtitleBitmap(const AVSubtitle& subtitle,
		int videoWidth, int videoHeight) {
		int canvasWidth = std::max(videoWidth, 1);
		int canvasHeight = std::max(videoHeight, 1);
		int left = canvasWidth;
		int top = canvasHeight;
		int right = 0;
		int bottom = 0;
		bool foundBitmap = false;

		for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
			const auto* rect = subtitle.rects[index];
			if (rect == nullptr || rect->type != SUBTITLE_BITMAP ||
				rect->data[0] == nullptr || rect->data[1] == nullptr ||
				rect->w <= 0 || rect->h <= 0) continue;
			canvasWidth = std::max(canvasWidth, rect->x + rect->w);
			canvasHeight = std::max(canvasHeight, rect->y + rect->h);
		}

		for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
			const auto* rect = subtitle.rects[index];
			if (rect == nullptr || rect->type != SUBTITLE_BITMAP ||
				rect->data[0] == nullptr || rect->data[1] == nullptr ||
				rect->w <= 0 || rect->h <= 0) continue;
			const int rectLeft = std::clamp(rect->x, 0, canvasWidth);
			const int rectTop = std::clamp(rect->y, 0, canvasHeight);
			const int rectRight = std::clamp(rect->x + rect->w, 0, canvasWidth);
			const int rectBottom = std::clamp(rect->y + rect->h, 0, canvasHeight);
			if (rectRight <= rectLeft || rectBottom <= rectTop) continue;
			foundBitmap = true;
			left = std::min(left, rectLeft);
			top = std::min(top, rectTop);
			right = std::max(right, rectRight);
			bottom = std::max(bottom, rectBottom);
		}
		if (!foundBitmap || right <= left || bottom <= top) return nullptr;

		auto result = std::make_shared<VideoSubtitle>();
		result->format = VideoSubtitle::Format::Bitmap;
		result->canvasWidth = canvasWidth;
		result->canvasHeight = canvasHeight;
		result->x = left;
		result->y = top;
		result->width = right - left;
		result->height = bottom - top;
		result->rgba.assign(static_cast<size_t>(result->width) * result->height * 4, 0);

		for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
			const auto* rect = subtitle.rects[index];
			if (rect == nullptr || rect->type != SUBTITLE_BITMAP ||
				rect->data[0] == nullptr || rect->data[1] == nullptr) continue;
			const int rectLeft = std::clamp(rect->x, left, right);
			const int rectTop = std::clamp(rect->y, top, bottom);
			const int rectRight = std::clamp(rect->x + rect->w, left, right);
			const int rectBottom = std::clamp(rect->y + rect->h, top, bottom);
			const auto* palette = rect->data[1];
			for (int y = rectTop; y < rectBottom; ++y) {
				const int sourceY = y - rect->y;
				const auto* source = rect->data[0] +
					static_cast<ptrdiff_t>(sourceY) * rect->linesize[0] + (rectLeft - rect->x);
				for (int x = rectLeft; x < rectRight; ++x) {
					const auto* color = palette + static_cast<size_t>(*source) * 4;
					auto* destination = result->rgba.data() +
						(static_cast<size_t>(y - top) * result->width + x - left) * 4;
					std::copy_n(color, 4, destination);
					++source;
				}
			}
		}
		return result;
	}

	// End the most recent open-ended bitmap cue when a later subtitle supplies its boundary.
	void closeOpenBitmapSubtitle(double end) {
		std::lock_guard lock(subtitleMutex);
		for (auto cue = subtitleCues.rbegin(); cue != subtitleCues.rend(); ++cue) {
			if (cue->start <= end && std::isinf(cue->end)) {
				cue->end = end;
				break;
			}
		}
	}

	// Decode subtitle packets into timed text or bitmap cues for presentation.
	void decodeSubtitlePacket(AVPacket* subtitlePacket) {
		if (subtitleCodec == nullptr) return;
		AVSubtitle subtitle{};
		int gotSubtitle = 0;
		if (avcodec_decode_subtitle2(subtitleCodec, &subtitle, &gotSubtitle, subtitlePacket) >= 0 && gotSubtitle) {
			const auto* streamInfo = format->streams[subtitleStreamIndex];
			const double packetStart = subtitle.pts != AV_NOPTS_VALUE
				? subtitle.pts / static_cast<double>(AV_TIME_BASE) - streamStart
				: subtitlePacket->pts == AV_NOPTS_VALUE ? currentPosition.load() :
					subtitlePacket->pts * av_q2d(streamInfo->time_base) - streamStart;
			const double start = packetStart + subtitle.start_display_time / 1000.0;
			const bool bitmapSubtitle = subtitle.format == 0;
			std::string text;
			for (unsigned int i = 0; i < subtitle.num_rects; ++i) {
				const auto line = subtitleRectText(subtitle.rects[i]);
				if (!line.empty()) {
					if (!text.empty()) text += "\n";
					text += line;
				}
			}
			auto decodedSubtitle = subtitleBitmap(subtitle, videoWidth.load(), videoHeight.load());
			if (decodedSubtitle == nullptr && !text.empty()) {
				decodedSubtitle = std::make_shared<VideoSubtitle>();
				decodedSubtitle->format = VideoSubtitle::Format::Text;
				decodedSubtitle->text = std::move(text);
			}
			if (decodedSubtitle == nullptr) {
				if (bitmapSubtitle) closeOpenBitmapSubtitle(start);
				avsubtitle_free(&subtitle);
				return;
			}
			double duration = std::numeric_limits<double>::infinity();
			if (!bitmapSubtitle)
				duration = (subtitle.end_display_time - subtitle.start_display_time) / 1000.0;
			if (!bitmapSubtitle && duration <= 0.0 && subtitlePacket->duration > 0)
				duration = subtitlePacket->duration * av_q2d(streamInfo->time_base);
			if (!bitmapSubtitle && duration <= 0.0) {
				const auto readableDuration = decodedSubtitle->format == VideoSubtitle::Format::Bitmap
					? 7.0 : 1.5 + decodedSubtitle->text.size() * 0.045;
				duration = std::clamp(readableDuration, 2.0, 7.0);
			}
			if (bitmapSubtitle) closeOpenBitmapSubtitle(start);
			{
				std::lock_guard lock(subtitleMutex);
				while (!subtitleCues.empty() && subtitleCues.front().end <= presentedPosition.load())
					subtitleCues.pop_front();
				subtitleCues.push_back({ start, start + duration, std::move(decodedSubtitle) });
				while (subtitleCues.size() > 256) subtitleCues.pop_front();
			}
		}
		avsubtitle_free(&subtitle);
	}

	// Open the SDL audio stream and transfer samples that accumulated while the device was
	// initializing.
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
			// Keep the state lock while transferring the pre-device queue. A seek
			// clears the SDL stream under this same lock; releasing it between
			// samples allowed a seek to clear the stream and then have this stale
			// startup audio inserted afterwards.
			for (const auto& samples : queuedAudio) {
				if (state->closing || !SDL_PutAudioStreamData(openedAudio, samples.data(),
					static_cast<int>(samples.size() * sizeof(float)))) break;
			}
		}
		if (state->playing) startAudio(state);
	}

	// Publish the first playback error and wake waiting work after marking the player unavailable.
	void setRuntimeError(const std::string& message) {
		{
			std::lock_guard lock(stateMutex);
			if (runtimeError.empty()) runtimeError = message;
		}
		isPlaying = false;
		isReady = false;
		stateChanged.notify_all();
	}

	// Resume the audio device once playback is active and buffering is complete.
	static void startAudio(const std::shared_ptr<AudioState>& state) {
		std::lock_guard lock(state->mutex);
		if (state->closing || state->output == nullptr || state->started ||
			!state->playing || state->buffering)
			return;
		if (!SDL_ResumeAudioStreamDevice(state->output)) {
			SDL_Log("Could not start video audio: %s", SDL_GetError());
			return;
		}
		state->started = true;
	}

	// Pause the audio device and mark its playback clock as stopped.
	static void pauseAudio(const std::shared_ptr<AudioState>& state) {
		std::lock_guard lock(state->mutex);
		if (state->output != nullptr) SDL_PauseAudioStreamDevice(state->output);
		state->started = false;
	}

	// Queue converted samples, waiting for space when audio-only playback must preserve every sample.
	bool queueAudio(const float* samples, int sampleFrames) {
		const auto state = audioState;
		if (sampleFrames <= 0) return true;
		if (audioSeekPending) return false;
		const int sampleBytes = sampleFrames * audioBytesPerFrame;
		// Video playback can keep decoding while its audio queue is full because
		// video frames provide their own timing. Audio-only playback must retain
		// every decoded sample, otherwise it can reach EOF several seconds early
		// and the loop handler will seek back to the beginning.
		const bool waitForAudioSpace = audioOnly;
		const auto waitStart = std::chrono::steady_clock::now();
		for (;;) {
			// Pausing is not a decode failure. Retain this frame until playback
			// resumes, or discard it only when a seek/stop supersedes it.
			{
				std::unique_lock lock(stateMutex);
				stateChanged.wait(lock, [&] {
					return stopRequested || requestedSeek.has_value() || audioSeekPending || isPlaying;
				});
				if (stopRequested || requestedSeek.has_value() || audioSeekPending) return false;
			}
			bool waitForSpace = false;
			{
				std::lock_guard lock(state->mutex);
				if (state->closing || audioSeekPending) return false;
				if (!isPlaying) continue;
				if (state->output == nullptr) {
					if (state->pendingBytes + sampleBytes <= maxQueuedAudioBytes) {
						state->pending.emplace_back(samples, samples +
							static_cast<size_t>(sampleFrames) * audioChannels);
						state->pendingBytes += sampleBytes;
					} else {
						waitForSpace = waitForAudioSpace;
					}
					if (!waitForSpace) return true;
				}
				if (state->output != nullptr &&
					SDL_GetAudioStreamQueued(state->output) <= maxQueuedAudioBytes) {
					if (!SDL_PutAudioStreamData(state->output, samples, sampleBytes)) {
						setRuntimeError(std::string("Could not queue video audio: ") + SDL_GetError());
						return false;
					}
					return true;
				}
				if (state->output != nullptr) waitForSpace = true;
			}
			if (!waitForSpace) return true;
			if (!waitForAudioSpace && std::chrono::steady_clock::now() - waitStart >
				std::chrono::milliseconds(100))
				return true;
			if (stopRequested || interrupted()) return false;
			std::unique_lock lock(stateMutex);
			stateChanged.wait_for(lock, std::chrono::milliseconds(10), [&] {
				return stopRequested || !isPlaying || requestedSeek.has_value();
			});
		}
	}

	// Check whether decoding should yield to shutdown or a pending seek.
	bool interrupted() {
		std::lock_guard lock(stateMutex);
		return stopRequested || requestedSeek.has_value() || audioSeekPending;
	}

	// Recreate the video decoder in software after hardware decoding fails.
	bool disableHardwareDecoding() {
		if (hwDeviceContext == nullptr || codec == nullptr || videoDecoder == nullptr)
			return false;
		hardwareDecodeState.failed = true;
		av_buffer_unref(&hwDeviceContext);

		// A failed VA-API negotiation cannot reliably be repaired by just
		// removing hw_device_ctx: the codec context may still be configured for
		// AV_PIX_FMT_VAAPI. Recreate it from the stream parameters so FFmpeg
		// starts a genuinely software-only decoder.
		AVCodecContext* softwareCodec = avcodec_alloc_context3(videoDecoder);
		if (softwareCodec == nullptr || avcodec_parameters_to_context(
			softwareCodec, stream->codecpar) < 0) {
			avcodec_free_context(&softwareCodec);
			return false;
		}
		softwareCodec->thread_count = codec->thread_count;
		softwareCodec->thread_type = codec->thread_type;
		softwareCodec->skip_frame = codec->skip_frame;
		softwareCodec->flags2 |= AV_CODEC_FLAG2_EXPORT_MVS;
		if (avcodec_open2(softwareCodec, videoDecoder, nullptr) < 0) {
			avcodec_free_context(&softwareCodec);
			return false;
		}
		avcodec_free_context(&codec);
		codec = softwareCodec;
		codec->opaque = &hardwareDecodeState;
		// receiveFrames() tells the packet loop to submit the packet that
		// triggered the failure to this new software decoder.
		retryVideoPacketAfterHardwareFallback = true;
		SDL_Log("Video: VA-API could not decode this stream; falling back to software decoding.");
		return true;
	}

	// Convert a decoded timestamp to source-relative seconds, estimating it when timestamps are
	// missing.
	double framePosition(const AVFrame* decodedFrame) const {
		if (decodedFrame->best_effort_timestamp == AV_NOPTS_VALUE)
			return currentPosition.load() + fallbackFrameDuration;
		const double timestamp = decodedFrame->best_effort_timestamp * av_q2d(stream->time_base);
		return std::max(0.0, timestamp - streamStart);
	}

	// Convert a decoded frame for display and inference, then queue it with playback timing and
	// generation metadata.
	bool publishFrame(AVFrame* decodedFrame, bool& clockValid,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin,
			std::optional<double>& seekFloor, bool& seekPreviewPending,
		bool& fastPreviewPending) {
		const double position = framePosition(decodedFrame);
		const auto frameGeneration = playbackGeneration.load();
		if (seekFloor.has_value() && position + fallbackFrameDuration < *seekFloor) return true;
		seekFloor.reset();
		const bool pausedSeekFrame = seekPreviewPending;
		const bool fastPreviewFrame = fastPreviewPending;
		if (pausedSeekFrame) {
			if (stopRequested) return false;
			clockValid = false;
		} else {
			std::unique_lock lock(stateMutex);
			while (!stopRequested && !requestedSeek.has_value() && isPlaying.load() &&
			       pendingFrames.size() >= maxQueuedVideoFrames) {
				stateChanged.wait(lock);
			}
			if (stopRequested || requestedSeek.has_value()) return false;
			while (!stopRequested && !requestedSeek.has_value() && !isPlaying.load() && !seekPreviewPending) {
				stateChanged.wait(lock);
			}
			if (stopRequested || requestedSeek.has_value()) return false;
		}
		struct TransferredFrame {
			AVFrame* frame = nullptr;
			// Release a temporary CPU frame transferred from hardware decoding.
			~TransferredFrame() { av_frame_free(&frame); }
		} transferredFrame;
		if (decodedFrame->format == AV_PIX_FMT_VAAPI) {
			transferredFrame.frame = av_frame_alloc();
			if (transferredFrame.frame == nullptr ||
				av_hwframe_transfer_data(transferredFrame.frame, decodedFrame, 0) < 0) {
				if (disableHardwareDecoding()) {
					av_frame_unref(decodedFrame);
					return true;
				}
				setRuntimeError("FFmpeg could not transfer the hardware video frame.");
				return false;
			}
			av_frame_copy_props(transferredFrame.frame, decodedFrame);
			decodedFrame = transferredFrame.frame;
		}

		constexpr int previewMaxDimension = 720;
		const int rot = rotation.load();
		const int rawWidth = decodedFrame->width;
		const int rawHeight = decodedFrame->height;
		const bool swapDims = (rot == 90 || rot == 270);
		const int sourceWidth = swapDims ? rawHeight : rawWidth;
		const int sourceHeight = swapDims ? rawWidth : rawHeight;
		const int sourceMaxDimension = std::max(sourceWidth, sourceHeight);
		const double previewScale = fastPreviewFrame && sourceMaxDimension > previewMaxDimension
			? static_cast<double>(previewMaxDimension) / sourceMaxDimension : 1.0;
		const int maxWidth = outputMaxWidth.load();
		const int maxHeight = outputMaxHeight.load();
		const double displayScale = maxWidth > 0 && maxHeight > 0
			? std::min({static_cast<double>(maxWidth) / sourceWidth,
				static_cast<double>(maxHeight) / sourceHeight}) : 1.0;
		double scale = previewScale * displayScale;
		const int outputWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
		const int outputHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
		auto sourceFormat = static_cast<AVPixelFormat>(decodedFrame->format);
		int sourceRange = decodedFrame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
		switch (sourceFormat) {
		case AV_PIX_FMT_YUVJ411P:
			sourceFormat = AV_PIX_FMT_YUV411P;
			sourceRange = 1;
			break;
		case AV_PIX_FMT_YUVJ420P:
			sourceFormat = AV_PIX_FMT_YUV420P;
			sourceRange = 1;
			break;
		case AV_PIX_FMT_YUVJ422P:
			sourceFormat = AV_PIX_FMT_YUV422P;
			sourceRange = 1;
			break;
		case AV_PIX_FMT_YUVJ444P:
			sourceFormat = AV_PIX_FMT_YUV444P;
			sourceRange = 1;
			break;
		case AV_PIX_FMT_YUVJ440P:
			sourceFormat = AV_PIX_FMT_YUV440P;
			sourceRange = 1;
			break;
		default:
			break;
		}

		auto converted = std::make_shared<VideoFrame>();
		converted->width = sourceWidth;
		converted->height = sourceHeight;
		converted->outputWidth = outputWidth;
		converted->outputHeight = outputHeight;
		converted->presentationTime = position;
		converted->generation = frameGeneration;
		const auto motionSideData = av_frame_get_side_data(
			decodedFrame, AV_FRAME_DATA_MOTION_VECTORS);
		if (motionSideData != nullptr && motionSideData->size >= sizeof(AVMotionVector) &&
			rawWidth > 0 && rawHeight > 0) {
			const auto* vectors = reinterpret_cast<const AVMotionVector*>(motionSideData->data);
			const size_t vectorCount = motionSideData->size / sizeof(AVMotionVector);
			converted->motionVectors.reserve(vectorCount);
			const auto orientPoint = [rot](float x, float y) {
				switch (rot) {
					case 90: return std::pair<float, float>{1.0f - y, x};
					case 180: return std::pair<float, float>{1.0f - x, 1.0f - y};
					case 270: return std::pair<float, float>{y, 1.0f - x};
					default: return std::pair<float, float>{x, y};
				}
			};
			for (size_t index = 0; index < vectorCount; ++index) {
				const auto& vector = vectors[index];
				// A held depth map cannot supply a future reference for B-frames.
				if (vector.source >= 0 || vector.w == 0 || vector.h == 0 ||
					vector.motion_scale == 0) continue;
				const float rawDestinationX = static_cast<float>(vector.dst_x) / rawWidth;
				const float rawDestinationY = static_cast<float>(vector.dst_y) / rawHeight;
				// src_x/src_y are integer-rounded; retain the codec's subpixel displacement.
				const float rawSourceX = (vector.dst_x +
					static_cast<float>(vector.motion_x) / vector.motion_scale) / rawWidth;
				const float rawSourceY = (vector.dst_y +
					static_cast<float>(vector.motion_y) / vector.motion_scale) / rawHeight;
				const auto [destinationX, destinationY] = orientPoint(rawDestinationX, rawDestinationY);
				const auto [sourceX, sourceY] = orientPoint(rawSourceX, rawSourceY);
				const float vectorWidth = rot == 90 || rot == 270
					? static_cast<float>(vector.h) / rawHeight : static_cast<float>(vector.w) / rawWidth;
				const float vectorHeight = rot == 90 || rot == 270
					? static_cast<float>(vector.w) / rawWidth : static_cast<float>(vector.h) / rawHeight;
				if (destinationX + vectorWidth * 0.5f <= 0.0f ||
					destinationY + vectorHeight * 0.5f <= 0.0f ||
					destinationX - vectorWidth * 0.5f >= 1.0f ||
					destinationY - vectorHeight * 0.5f >= 1.0f) continue;
				converted->motionVectors.push_back({destinationX, destinationY, sourceX, sourceY,
					vectorWidth, vectorHeight});
			}
		}
		converted->fullRange = sourceRange != 0;
		switch (decodedFrame->colorspace) {
		case AVCOL_SPC_BT2020_NCL:
		case AVCOL_SPC_BT2020_CL:
			converted->colorSpace = VideoFrame::ColorSpace::BT2020;
			break;
		case AVCOL_SPC_BT709:
			converted->colorSpace = VideoFrame::ColorSpace::BT709;
			break;
		default:
			converted->colorSpace = sourceHeight >= 720
				? VideoFrame::ColorSpace::BT709 : VideoFrame::ColorSpace::BT601;
			break;
		}

		const int inferenceMaximum = inferenceMaxDimension.load();
		const double inferenceRate = inferenceFramesPerSecond.load();
		const double inferenceInterval = inferenceRate > 0.0 ? 1.0 / inferenceRate : 0.0;
		const bool resetInferenceSchedule = frameGeneration != lastInferenceGeneration ||
			lastInferencePosition < 0.0 || position < lastInferencePosition ||
			nextInferencePosition < 0.0 ||
			(inferenceInterval > 0.0 && position - nextInferencePosition > inferenceInterval);
		const bool prepareInference = inferenceMaximum > 0 && inferenceInterval > 0.0 &&
			(resetInferenceSchedule ||
			 position + fallbackFrameDuration * 0.5 >= nextInferencePosition);
		if (prepareInference) {
			const double inferenceScale = std::min(1.0,
				static_cast<double>(inferenceMaximum) / std::max(sourceWidth, sourceHeight));
			converted->inferenceWidth = std::max(1,
				static_cast<int>(std::lround(sourceWidth * inferenceScale)));
			converted->inferenceHeight = std::max(1,
				static_cast<int>(std::lround(sourceHeight * inferenceScale)));
			const int rawInfW = swapDims ? converted->inferenceHeight : converted->inferenceWidth;
			const int rawInfH = swapDims ? converted->inferenceWidth : converted->inferenceHeight;
			inferenceScaler = sws_getCachedContext(inferenceScaler,
				decodedFrame->width, decodedFrame->height, sourceFormat,
				rawInfW, rawInfH, AV_PIX_FMT_RGBA,
				SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (inferenceScaler == nullptr) {
				setRuntimeError("FFmpeg could not create the depth-inference color converter.");
				return false;
			}
			const int* colorspace = sws_getCoefficients(SWS_CS_DEFAULT);
			if (sws_setColorspaceDetails(inferenceScaler, colorspace, sourceRange,
					colorspace, 0, 0, 1 << 16, 1 << 16) < 0) {
				setRuntimeError("FFmpeg could not configure depth-inference video color.");
				return false;
			}
			if (rot == 0) {
				converted->inferenceRGBA.resize(
					static_cast<size_t>(converted->inferenceWidth) * converted->inferenceHeight * 4);
				uint8_t* inferenceData[] = {
					converted->inferenceRGBA.data(), nullptr, nullptr, nullptr };
				int inferenceLines[] = { converted->inferenceWidth * 4, 0, 0, 0 };
				if (sws_scale(inferenceScaler, decodedFrame->data, decodedFrame->linesize,
						0, decodedFrame->height, inferenceData, inferenceLines) <= 0) {
					setRuntimeError("FFmpeg could not prepare a video frame for depth inference.");
					return false;
				}
			} else {
				std::vector<std::uint8_t> rawRGBA(static_cast<size_t>(rawInfW) * rawInfH * 4);
				uint8_t* inferenceData[] = {
					rawRGBA.data(), nullptr, nullptr, nullptr };
				int inferenceLines[] = { rawInfW * 4, 0, 0, 0 };
				if (sws_scale(inferenceScaler, decodedFrame->data, decodedFrame->linesize,
						0, decodedFrame->height, inferenceData, inferenceLines) <= 0) {
					setRuntimeError("FFmpeg could not prepare a video frame for depth inference.");
					return false;
				}
				rotateBufferFromPitch(rot, reinterpret_cast<const uint32_t*>(rawRGBA.data()),
					rawInfW, rawInfW, rawInfH, converted->inferenceRGBA);
			}
			lastInferencePosition = position;
			lastInferenceGeneration = frameGeneration;
			if (resetInferenceSchedule) {
				nextInferencePosition = position + inferenceInterval;
			} else {
				do nextInferencePosition += inferenceInterval;
				while (nextInferencePosition <= position);
			}
		}

		auto copyPlane = [](std::vector<std::uint8_t>& destination,
				const std::uint8_t* source, int sourcePitch, int rowBytes, int rows) {
			destination.resize(static_cast<size_t>(rowBytes) * rows);
			for (int row = 0; row < rows; ++row)
				SDL_memcpy(destination.data() + static_cast<size_t>(row) * rowBytes,
					source + static_cast<ptrdiff_t>(row) * sourcePitch, rowBytes);
		};
		const int rawChromaWidth = (rawWidth + 1) / 2;
		const int rawChromaHeight = (rawHeight + 1) / 2;
		if (sourceFormat == AV_PIX_FMT_NV12) {
			converted->format = VideoFrame::Format::NV12;
			if (rot == 0) {
				copyPlane(converted->planes[0], decodedFrame->data[0], decodedFrame->linesize[0],
					rawWidth, rawHeight);
				copyPlane(converted->planes[1], decodedFrame->data[1], decodedFrame->linesize[1],
					rawChromaWidth * 2, rawChromaHeight);
			} else {
				rotateBufferFromPitch(rot, decodedFrame->data[0], decodedFrame->linesize[0],
					rawWidth, rawHeight, converted->planes[0]);
				rotateBufferFromPitch(rot, reinterpret_cast<const uint16_t*>(decodedFrame->data[1]),
					decodedFrame->linesize[1] / 2, rawChromaWidth, rawChromaHeight, converted->planes[1]);
			}
		} else if (sourceFormat == AV_PIX_FMT_YUV420P) {
			converted->format = VideoFrame::Format::YUV420P;
			if (rot == 0) {
				copyPlane(converted->planes[0], decodedFrame->data[0], decodedFrame->linesize[0],
					rawWidth, rawHeight);
				copyPlane(converted->planes[1], decodedFrame->data[1], decodedFrame->linesize[1],
					rawChromaWidth, rawChromaHeight);
				copyPlane(converted->planes[2], decodedFrame->data[2], decodedFrame->linesize[2],
					rawChromaWidth, rawChromaHeight);
			} else {
				rotateBufferFromPitch(rot, decodedFrame->data[0], decodedFrame->linesize[0],
					rawWidth, rawHeight, converted->planes[0]);
				rotateBufferFromPitch(rot, decodedFrame->data[1], decodedFrame->linesize[1],
					rawChromaWidth, rawChromaHeight, converted->planes[1]);
				rotateBufferFromPitch(rot, decodedFrame->data[2], decodedFrame->linesize[2],
					rawChromaWidth, rawChromaHeight, converted->planes[2]);
			}
		} else {
			// Less common pixel formats retain the general swscale fallback.
			// The common VA-API NV12 and software YUV420P paths avoid CPU RGB
			// conversion and are scaled by the GPU instead.
			constexpr double maxConvertedPixelRate = 1920.0 * 1080.0 * 60.0;
			const double maxConvertedPixels = maxConvertedPixelRate * fallbackFrameDuration;
			const double convertedPixels = sourceWidth * static_cast<double>(sourceHeight) * scale * scale;
			if (convertedPixels > maxConvertedPixels)
				scale *= std::sqrt(maxConvertedPixels / convertedPixels);
			converted->outputWidth = std::max(1,
				static_cast<int>(std::lround(sourceWidth * scale)));
			converted->outputHeight = std::max(1,
				static_cast<int>(std::lround(sourceHeight * scale)));
			converted->format = VideoFrame::Format::RGBA;
			const int rawOutW = swapDims ? converted->outputHeight : converted->outputWidth;
			const int rawOutH = swapDims ? converted->outputWidth : converted->outputHeight;
			scaler = sws_getCachedContext(scaler,
				decodedFrame->width, decodedFrame->height,
				sourceFormat,
				rawOutW, rawOutH, AV_PIX_FMT_RGBA,
				fastPreviewFrame ? SWS_FAST_BILINEAR : SWS_BILINEAR,
				nullptr, nullptr, nullptr);
			if (scaler == nullptr) {
				setRuntimeError("FFmpeg could not create the video color converter.");
				return false;
			}
			const int* colorspace = sws_getCoefficients(SWS_CS_DEFAULT);
			if (sws_setColorspaceDetails(scaler, colorspace, sourceRange,
				colorspace, 0, 0, 1 << 16, 1 << 16) < 0) {
				setRuntimeError("FFmpeg could not configure the video color range.");
				return false;
			}

			if (rot == 0) {
				converted->planes[0].resize(static_cast<size_t>(converted->outputWidth) *
					converted->outputHeight * 4);
				uint8_t* outputData[] = { converted->planes[0].data(), nullptr, nullptr, nullptr };
				int outputLines[] = { converted->outputWidth * 4, 0, 0, 0 };
				const int convertedRows = sws_scale(scaler, decodedFrame->data,
					decodedFrame->linesize, 0, decodedFrame->height, outputData, outputLines);
				if (convertedRows <= 0) {
					setRuntimeError("FFmpeg could not convert the decoded video frame.");
					return false;
				}
			} else {
				std::vector<std::uint8_t> rawRGBA(static_cast<size_t>(rawOutW) * rawOutH * 4);
				uint8_t* outputData[] = { rawRGBA.data(), nullptr, nullptr, nullptr };
				int outputLines[] = { rawOutW * 4, 0, 0, 0 };
				const int convertedRows = sws_scale(scaler, decodedFrame->data,
					decodedFrame->linesize, 0, decodedFrame->height, outputData, outputLines);
				if (convertedRows <= 0) {
					setRuntimeError("FFmpeg could not convert the decoded video frame.");
					return false;
				}
				rotateBufferFromPitch(rot, reinterpret_cast<const uint32_t*>(rawRGBA.data()),
					rawOutW, rawOutW, rawOutH, converted->planes[0]);
			}
		}

		if (frameGeneration != playbackGeneration.load() && !pausedSeekFrame) return true;
		{
			std::lock_guard lock(stateMutex);
			if (pausedSeekFrame || fastPreviewFrame) {
				pendingFrames.clear();
			}
			pendingFrames.push_back({std::move(converted), fastPreviewFrame});
		}
		videoWidth = sourceWidth;
		videoHeight = sourceHeight;
		currentPosition = position;
		if (isPlaying) startAudio(audioState);
		seekPreviewPending = false;
		fastPreviewPending = false;
		return true;
	}

	// Drain available video frames from FFmpeg or MVC decoding into the presentation queue.
	bool receiveFrames(bool& clockValid,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin,
			std::optional<double>& seekFloor, bool& seekPreviewPending,
			bool& fastPreviewPending) {
		for (;;) {
			AVFrame* mvcFrame = mvcDecoder ? mvcDecoder->receive() : nullptr;
			if (mvcDecoder && !mvcFrame) return true;
			const int result = mvcDecoder ? 0 : avcodec_receive_frame(codec, frame);
			if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
			if (result < 0) {
				if (disableHardwareDecoding()) return true;
				if (result == AVERROR(ENOMEM)) {
					setRuntimeError("FFmpeg video decode failed: " + ffmpegError(result));
					return false;
				}
				return true;
			}
			const bool previewWasPending = seekPreviewPending;
			const bool published = publishFrame(mvcFrame ? mvcFrame : frame, clockValid, clockOrigin, mediaOrigin,
				seekFloor, seekPreviewPending, fastPreviewPending);
			av_frame_free(&mvcFrame);
			av_frame_unref(frame);
			if (!published) return false;
			if (previewWasPending && !seekPreviewPending) return true;
		}
	}

	// Drain decoded audio, trim samples before a seek target, and resample them for playback.
	bool receiveAudioFrames(std::optional<double>& audioSeekFloor, bool discardAudio) {
		for (;;) {
			const int result = avcodec_receive_frame(audioCodec, audioFrame);
			if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
			if (result < 0) {
				if (result == AVERROR(ENOMEM)) {
					setRuntimeError("FFmpeg audio decode failed: " + ffmpegError(result));
					return false;
				}
				return true;
			}

			const double position = audioFrame->best_effort_timestamp == AV_NOPTS_VALUE
				? currentPosition.load()
				: audioFrame->best_effort_timestamp *
					av_q2d(audioStream->time_base) - streamStart;
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
					int firstFrame = 0;
					if (!audioQueuePositionValid) {
						constexpr int syncToleranceFrames = audioSampleRate / 1000;
						const double diff = position - audioQueuePosition.load();
						if (std::abs(diff) > 0.5 || audioQueuePosition.load() == 0.0) {
							audioQueuePosition = position;
						} else {
							const int syncFrames = static_cast<int>(std::clamp<long long>(
								std::llround(diff * audioSampleRate),
								-audioSampleRate * 2LL, audioSampleRate * 2LL));
							if (syncFrames > syncToleranceFrames) {
								std::vector<float> silence(
									static_cast<size_t>(syncFrames) * audioChannels, 0.0f);
								if (!queueAudio(silence.data(), syncFrames)) {
									av_frame_unref(audioFrame);
									return false;
								}
								audioQueuePosition.fetch_add(
									syncFrames / static_cast<double>(audioSampleRate));
							} else if (syncFrames < -syncToleranceFrames) {
								firstFrame = std::min(converted, -syncFrames);
							}
						}
						audioQueuePositionValid = true;
					}
					const int queuedFrames = converted - firstFrame;
					if (queuedFrames > 0 && !queueAudio(
						samples.data() + static_cast<size_t>(firstFrame) * audioChannels,
						queuedFrames)) {
						av_frame_unref(audioFrame);
						return false;
					}
					if (audioOnly && queuedFrames > 0) {
						std::lock_guard lock(waveformMutex);
						waveform.append(samples.data() + static_cast<size_t>(firstFrame) * audioChannels,
							queuedFrames, audioChannels, audioSampleRate, audioQueuePosition.load());
					}
					audioQueuePosition.fetch_add(
						queuedFrames / static_cast<double>(audioSampleRate));
				}
			}
			av_frame_unref(audioFrame);
		}
	}

	// Reposition the source and flush decoder, subtitle, and audio state for the new timeline position.
	bool applySeek(const SeekRequest& request, std::optional<double>& seekFloor,
				std::optional<double>& audioSeekFloor) {
		if (audioOnly || !audioStreamIndices.empty()) audioSeekPending = true;
		{
			std::lock_guard lock(waveformMutex);
			waveform.clear();
		}
		const int64_t target = static_cast<int64_t>(
			(request.seconds + streamStart) / av_q2d(stream->time_base));
		int result = 0;
		if (blurayReader || dvdReader) {
			if (discReadAhead) discReadAhead->reset();
			// Disc readers seek to access units. FFmpeg's arbitrary byte search
			// can repeatedly land on the same packet and never make progress.
			const bool sought = blurayReader ? blurayReader->seekTime(request.seconds) : dvdReader->seekTime(request.seconds);
			if (sought) {
				avio_flush(avioContext);
				avioContext->pos = blurayReader ? blurayReader->bytePosition() : dvdReader->bytePosition();
				if (discReadAhead) discReadAhead->setPosition(avioContext->pos);
				avioContext->eof_reached = 0;
				avioContext->error = 0;
				result = avformat_flush(format);
			} else result = AVERROR(EIO);
		} else {
			result = av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD);
			if (result < 0) {
				const int64_t defaultTarget = static_cast<int64_t>((request.seconds + streamStart) * AV_TIME_BASE);
				result = av_seek_frame(format, -1, defaultTarget, AVSEEK_FLAG_BACKWARD);
			}
			if (result < 0) result = avformat_seek_file(format, streamIndex, INT64_MIN, target, INT64_MAX, 0);
		}
		if (result < 0) {
			setRuntimeError("FFmpeg seek failed: " + ffmpegError(result));
			return false;
		}
		if (codec != nullptr) avcodec_flush_buffers(codec);
		if (mvcDecoder) mvcDecoder->flush();
		if (audioCodec != nullptr) avcodec_flush_buffers(audioCodec);
		if (subtitleCodec != nullptr) avcodec_flush_buffers(subtitleCodec);
		if (audioResampler != nullptr) {
			swr_close(audioResampler);
			if (swr_init(audioResampler) < 0) {
				setRuntimeError("FFmpeg could not reset the video audio converter.");
				return false;
			}
		}
		if (codec != nullptr)
			codec->skip_frame = request.fastPreview ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
		av_packet_unref(packet);
		if (frame != nullptr) av_frame_unref(frame);
		if (audioFrame != nullptr) av_frame_unref(audioFrame);
		{
			std::lock_guard lock(audioState->mutex);
			audioState->pending.clear();
			audioState->pendingBytes = 0;
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
		{
			std::lock_guard lock(stateMutex);
			pendingFrames.clear();
		}
		stateChanged.notify_all();
		currentPosition = request.seconds;
		presentedPosition = request.seconds;
		audioQueuePosition = request.seconds;
		audioQueuePositionValid = false;
		atEnd = false;
		audioState->buffering = true;
		seekFloor = request.fastPreview ? std::nullopt : std::optional(request.seconds);
		audioSeekFloor = audioCodec != nullptr ? std::optional(request.seconds) : std::nullopt;
		audioSeekPending = false;
		return true;
	}

	// Estimate queued audio duration from pending samples or the SDL stream queue.
	double queuedAudioDuration() const {
		std::lock_guard lock(audioState->mutex);
		int bytes = audioState->pendingBytes;
		if (audioState->output != nullptr)
			bytes = std::max(0, SDL_GetAudioStreamQueued(audioState->output));
		return bytes / static_cast<double>(audioSampleRate * audioBytesPerFrame);
	}

	// Demux and decode on the worker thread while handling seeks, track changes, buffering, and end of
	// stream.
	void decodeLoop() {
		if (prepareAudioDecoder()) {
			const auto state = audioState;
			// Do not join this thread: some backends never return from device open.
			std::thread(&Impl::initializeAudioDevice, state).detach();
		}
		if (!audioOnly) prepareSubtitleDecoder();
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
				audioState->pending.clear();
				audioState->pendingBytes = 0;
				if (audioState->output != nullptr) SDL_ClearAudioStream(audioState->output);
				if (audioCodec != nullptr) avcodec_free_context(&audioCodec);
				av_frame_free(&audioFrame);
				swr_free(&audioResampler);
				audioStreamIndex = -1;
				audioQueuePosition = presentedPosition.load();
				audioQueuePositionValid = false;
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
				if (requestedSeek.has_value()) {
					seek.swap(requestedSeek);
				}
			}
			if (seek.has_value()) {
				if (!applySeek(*seek, seekFloor, audioSeekFloor)) break;
				// Fast previews are meaningful for video frames only. Audio-only
				// seeks must still queue samples as soon as playback resumes, or the
				// decoder will run to EOF silently and restart at zero.
				discardAudio = !audioOnly && seek->fastPreview;
				clockValid = false;
				// Audio has no preview frame to publish while scrubbing is paused.
				// Leave the decoder parked at the requested position until playback
				// resumes instead of reading to EOF and entering the loop path.
				seekPreviewPending = !audioOnly && (!isPlaying.load() || seek->fastPreview);
				fastPreviewPending = !audioOnly && seek->fastPreview;
			}
			if (!isPlaying && !seekPreviewPending) continue;

			const int readResult = av_read_frame(format, packet);
			if (readResult == AVERROR_EOF) {
				if (audioOnly) {
					const int flushResult = audioCodec != nullptr
						? avcodec_send_packet(audioCodec, nullptr) : AVERROR(EIO);
					if (audioCodec == nullptr) {
						setRuntimeError("Audio playback is unavailable.");
						break;
					}
					if (flushResult >= 0 && !receiveAudioFrames(audioSeekFloor, false)) {
						if (interrupted()) continue;
						break;
					}
					if (totalDuration > 0.0) {
						const auto drainDeadline = std::chrono::steady_clock::now() +
							std::chrono::duration<double>(std::max(2.0, totalDuration.load() + 1.0));
						while (isPlaying && queuedAudioDuration() > 0.02 &&
							std::chrono::steady_clock::now() < drainDeadline) {
							std::unique_lock lock(stateMutex);
							const bool interrupted = stateChanged.wait_for(lock,
								std::chrono::milliseconds(20), [&] {
									return stopRequested || !isPlaying || requestedSeek.has_value();
								});
							if (interrupted) break;
						}
						if (!isPlaying || interrupted()) continue;
						// Queue the automatic restart through the same seek path as user
						// requests. A seek arriving at the loop boundary must replace this
						// restart rather than being applied before it and then overwritten.
						{
							std::lock_guard lock(stateMutex);
							if (requestedSeek.has_value()) continue;
							playbackGeneration.fetch_add(1);
							requestedSeek = SeekRequest{0.0, false};
						}
						stateChanged.notify_all();
						continue;
					}
					atEnd = true;
					isPlaying = false;
					audioState->playing = false;
					if (totalDuration > 0.0) currentPosition = totalDuration.load();
					continue;
				}
				closeOpenBitmapSubtitle(totalDuration > 0.0
					? totalDuration.load() : currentPosition.load());
				std::string mvcError;
				const int flushResult = mvcDecoder ? (mvcDecoder->finish(mvcError) ? 0 : AVERROR(EIO))
					: avcodec_send_packet(codec, nullptr);
				if (flushResult < 0 && flushResult != AVERROR_EOF) {
					setRuntimeError(mvcDecoder ? mvcError : "FFmpeg could not finish decoding the video: " +
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
					playbackGeneration.fetch_add(1);
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

            if (mvcDecoder) {
                // libbluray concatenates clip bytes; each transport stream can
                // restart its timestamps. Keep video, audio and subtitles on
                // the playlist timeline, including after a seek into a clip.
                const int64_t offset = av_rescale_q(mvcDecoder->timestampOffset(packet->pos),
                    AVRational{1, 90000}, format->streams[packet->stream_index]->time_base);
                if (packet->pts != AV_NOPTS_VALUE) packet->pts += offset;
                if (packet->dts != AV_NOPTS_VALUE) packet->dts += offset;
            }

			if (!audioOnly && packet->stream_index == streamIndex) {
				if (mvcDecoder) {
					std::string error;
					const bool decoded = mvcDecoder->send(*packet, error);
					av_packet_unref(packet);
					if (!decoded) {
						if (interrupted()) continue;
						setRuntimeError(error); break;
					}
					if (!receiveFrames(clockValid, clockOrigin, mediaOrigin, seekFloor,
						seekPreviewPending, fastPreviewPending) && !interrupted()) break;
					continue;
				}
				bool packetHandled = false;
				while (!packetHandled && !stopRequested && !interrupted()) {
					const int sendResult = avcodec_send_packet(codec, packet);
					if (sendResult == AVERROR(EAGAIN)) {
						if (!receiveFrames(clockValid, clockOrigin, mediaOrigin, seekFloor,
								seekPreviewPending, fastPreviewPending)) {
							if (interrupted()) break;
							break;
						}
						continue;
					}
					if (sendResult < 0) {
						if (disableHardwareDecoding()) continue;
						break;
					}
					if (!receiveFrames(clockValid, clockOrigin, mediaOrigin, seekFloor,
							seekPreviewPending, fastPreviewPending)) {
						if (interrupted()) break;
						break;
					}
					if (!retryVideoPacketAfterHardwareFallback) {
						packetHandled = true;
					} else {
						retryVideoPacketAfterHardwareFallback = false;
					}
				}
				av_packet_unref(packet);
			} else if (packet->stream_index == audioStreamIndex && audioCodec != nullptr) {
				if (fastPreviewPending) {
					av_packet_unref(packet);
				} else {
					int sendResult = avcodec_send_packet(audioCodec, packet);
					while (sendResult == AVERROR(EAGAIN) && !stopRequested && !interrupted()) {
						if (!receiveAudioFrames(audioSeekFloor, discardAudio)) break;
						sendResult = avcodec_send_packet(audioCodec, packet);
					}
					av_packet_unref(packet);
					if (!receiveAudioFrames(audioSeekFloor, discardAudio)) {
						if (interrupted()) continue;
						break;
					}
				}
			} else if (packet->stream_index == subtitleStreamIndex) {
				if (fastPreviewPending) {
					av_packet_unref(packet);
				} else {
					decodeSubtitlePacket(packet);
					av_packet_unref(packet);
				}
			} else {
				av_packet_unref(packet);
			}
		}
	}
#endif
};

// Recognize supported video extensions and candidate optical-disc sources.
bool VideoPlayer::supported(const std::filesystem::path& path) {
	if (DiscSource::candidate(path)) return true;
	const auto extension = lowerExtension(path);
	return extension == ".mp4" || extension == ".m4v" || extension == ".mov" ||
		extension == ".mkv" || extension == ".webm" || extension == ".avi" ||
		extension == ".wmv" || extension == ".mpeg" || extension == ".mpg" ||
		extension == ".vob" || extension == ".ifo" || extension == ".iso" ||
		extension == ".m2ts" || extension == ".ts" ||
		extension == ".gif";
}

// Allocate the player's private decoding and playback state.
VideoPlayer::VideoPlayer() : impl(std::make_unique<Impl>()) {}

// Stop playback and release owned resources when the player is destroyed.
VideoPlayer::~VideoPlayer() { close(); }

// Open a file or disc source, select its streams and decoding path, and start the decode worker.
bool VideoPlayer::open(const std::filesystem::path& path, std::string& error, int discTitle) {
	close();
#ifndef RENDEPTH_ENABLE_FFMPEG
	error = "FFmpeg video playback was not enabled in this build.";
	(void)path;
	return false;
#else
	int result = 0;
	const auto discType = DiscSource::detect(path);
	const bool isBluray = discType == DiscSource::Type::Bluray;
	const bool isDvd = discType == DiscSource::Type::Dvd;
	const bool isCd = discType == DiscSource::Type::AudioCd;
	if (isBluray || isDvd || isCd) {
		const AVInputFormat* inputFormat = av_find_input_format(isCd ? "wav" : (isBluray ? "mpegts" : "mpeg"));
		if (!inputFormat) {
			error = "Required FFmpeg disc demuxer is not available in this build.";
			return false;
		}
		if (isCd) {
			impl->audioCdReader = std::make_unique<AudioCdReader>();
			if (!impl->audioCdReader->open(path, error)) { close(); return false; }
			impl->avioContext = impl->audioCdReader->stream().createAVIOContext();
		} else if (isBluray) {
			impl->blurayReader = std::make_unique<BlurayReader>();
			if (!impl->blurayReader->open(path, error)) { close(); return false; }
			if (discTitle >= 0 && !impl->blurayReader->selectTitle(discTitle, error)) { close(); return false; }
			const auto& titles = impl->blurayReader->titles();
			const auto selected = std::find_if(titles.begin(), titles.end(), [&](const BlurayTitle& title) {
				return title.index == impl->blurayReader->activeTitle();
			});
			if (selected != titles.end() && selected->mvc) {
				impl->discReadMutex = std::make_shared<std::mutex>();
				impl->mvcDecoder = std::make_unique<MvcDecoder>();
				if (!impl->mvcDecoder->open(path, impl->blurayReader->activeTitle(),
					[this] { return impl->stopRequested || impl->interrupted(); }, error, impl->discReadMutex)) {
					close(); return false;
				}
			}
			// MVC reads two interleaved files on the same optical drive. Read
            // ahead in larger chunks instead of alternating tiny reads.
            impl->avioContext = impl->blurayReader->createAVIOContext(impl->mvcDecoder ? 2 * 1024 * 1024 : 32768);
			if (impl->mvcDecoder && impl->avioContext) {
				auto* reader = impl->blurayReader.get();
				impl->discReadAhead = std::make_unique<DiscReadAhead>(
					[reader, mutex = impl->discReadMutex](uint8_t* data, int size) {
						std::lock_guard lock(*mutex);
						return BlurayReader::readPacket(reader, data, size);
					},
					[reader](int64_t offset, int whence) { return BlurayReader::seekPacket(reader, offset, whence); },
					2 * 1024 * 1024, reader->bytePosition());
				impl->avioContext->opaque = impl->discReadAhead.get();
				impl->avioContext->read_packet = [](void* p, uint8_t* data, int size) {
					return static_cast<DiscReadAhead*>(p)->read(data, size);
				};
				impl->avioContext->seek = [](void* p, int64_t offset, int whence) {
					return static_cast<DiscReadAhead*>(p)->seek(offset, whence);
				};
			}
		} else {
			impl->dvdReader = std::make_unique<DvdReader>();
			if (!impl->dvdReader->open(path, error)) { close(); return false; }
			if (discTitle >= 0 && !impl->dvdReader->selectTitle(discTitle, error)) { close(); return false; }
			impl->avioContext = impl->dvdReader->createAVIOContext();
		}
		impl->format = avformat_alloc_context();
		if (!impl->avioContext || !impl->format) {
			error = "Could not allocate disc input buffers.";
			close();
			return false;
		}
		impl->format->pb = impl->avioContext;
		impl->format->flags |= AVFMT_FLAG_CUSTOM_IO;
		impl->format->skip_estimate_duration_from_pts = 1;
		// Opening initializes the demuxer's private state and reads its header.
		result = avformat_open_input(&impl->format, nullptr, inputFormat, nullptr);
	} else {
		result = avformat_open_input(&impl->format, path.string().c_str(), nullptr, nullptr);
	}
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

	// MPEG-TS may omit languages that are present in the Blu-ray playlist.
	// Match by PID: playlist order need not match FFmpeg's stream order.
	if (impl->blurayReader) {
		for (const auto& title : impl->blurayReader->titles()) {
			if (title.index != impl->blurayReader->activeTitle()) continue;
			for (unsigned i = 0; i < impl->format->nb_streams; ++i) {
				auto* stream = impl->format->streams[i];
				const auto type = stream->codecpar->codec_type;
				if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE) continue;
				const auto* existing = av_dict_get(stream->metadata, "language", nullptr, 0);
				if (existing && existing->value && existing->value[0] &&
					std::string(existing->value) != "und") continue;
				const auto& languages = type == AVMEDIA_TYPE_AUDIO
					? title.audioStreamLanguages : title.subtitleStreamLanguages;
				if (const auto language = languages.find(stream->id); language != languages.end())
					av_dict_set(&stream->metadata, "language", language->second.c_str(), 0);
			}
			break;
		}
	}

	const AVCodec* decoder = nullptr;
	impl->streamIndex = av_find_best_stream(
		impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (impl->streamIndex >= 0 &&
		impl->format->streams[impl->streamIndex]->attached_pic.size > 0) {
		impl->streamIndex = -1;
		decoder = nullptr;
	}
	if (impl->streamIndex < 0 || decoder == nullptr) {
		decoder = nullptr;
		impl->streamIndex = av_find_best_stream(
			impl->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
		if (impl->streamIndex < 0 || decoder == nullptr) {
			error = "FFmpeg could not find a supported audio or video stream.";
			close();
			return false;
		}
		impl->audioOnly = true;
	} else {
		impl->audioOnly = false;
	}
	impl->stream = impl->format->streams[impl->streamIndex];
	// Disc playlists can also carry secondary video. We only decode the
	// selected video stream; parsing the others wastes work and can report
	// missing parameter sets after seeks into those unselected streams.
	for (unsigned int i = 0; i < impl->format->nb_streams; ++i) {
		if (static_cast<int>(i) != impl->streamIndex &&
			impl->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			impl->format->streams[i]->discard = AVDISCARD_ALL;
	}
	impl->audioStreamIndices.clear();
	impl->subtitleStreamIndices.clear();
	for (unsigned int i = 0; i < impl->format->nb_streams; ++i) {
		if (impl->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			impl->audioStreamIndices.push_back(static_cast<int>(i));
		else if (impl->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
			impl->subtitleStreamIndices.push_back(static_cast<int>(i));
	}
	impl->selectedAudioTrack = 0;
	if (impl->audioStreamIndices.size() > 1) {
		int englishTrack = -1;
		for (int track = 0; track < static_cast<int>(impl->audioStreamIndices.size()); ++track) {
			const auto* audioStream = impl->format->streams[impl->audioStreamIndices[track]];
			const auto* language = av_dict_get(audioStream->metadata, "language", nullptr, 0);
			std::string code = language != nullptr && language->value != nullptr ? language->value : "";
			std::transform(code.begin(), code.end(), code.begin(),
				[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			if (code == "eng" || code == "en") {
				englishTrack = track;
				break;
			}
		}
		if (englishTrack < 0) {
			for (int track = 0; track < static_cast<int>(impl->audioStreamIndices.size()); ++track) {
				const auto* audioStream = impl->format->streams[impl->audioStreamIndices[track]];
				for (const char* key : {"title", "handler_name"}) {
					const auto* label = av_dict_get(audioStream->metadata, key, nullptr, 0);
					if (label != nullptr && label->value != nullptr) {
						std::string name = label->value;
						std::transform(name.begin(), name.end(), name.begin(),
							[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
						if (name.find("english") != std::string::npos) {
							englishTrack = track;
							break;
						}
					}
				}
				if (englishTrack >= 0) break;
			}
		}
		if (englishTrack >= 0) impl->selectedAudioTrack = englishTrack;
	}
	impl->requestedAudioTrack = -1;
	impl->selectedSubtitleTrack = noSubtitleTrack;
	impl->requestedSubtitleTrack = noSubtitleRequest;
	if (!impl->audioOnly && impl->stream->codecpar->codec_id == AV_CODEC_ID_AV1) {
		decoder = avcodec_find_decoder_by_name("libdav1d");
		if (decoder == nullptr) {
			error = "FFmpeg was built without the libdav1d AV1 decoder.";
			close();
			return false;
		}
	}
	if (!impl->audioOnly && !impl->mvcDecoder) {
		impl->codec = avcodec_alloc_context3(decoder);
		if (impl->codec == nullptr) {
			error = "FFmpeg could not allocate the video decoder.";
			close();
			return false;
		}
		result = avcodec_parameters_to_context(impl->codec, impl->stream->codecpar);
		impl->videoDecoder = decoder;
		impl->hardwareDecodeState.failed = false;
		impl->retryVideoPacketAfterHardwareFallback = false;
		impl->codec->opaque = &impl->hardwareDecodeState;
		impl->codec->thread_count = static_cast<int>(
			std::clamp(std::thread::hardware_concurrency(), 1u, 16u));
		impl->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
		// Export codec-provided block motion vectors when the decoder supports
		// them. This adds side data but avoids a second optical-flow pass.
		impl->codec->flags2 |= AV_CODEC_FLAG2_EXPORT_MVS;
		if (av_hwdevice_ctx_create(&impl->hwDeviceContext, AV_HWDEVICE_TYPE_VAAPI,
			nullptr, nullptr, 0) >= 0) {
			impl->codec->hw_device_ctx = av_buffer_ref(impl->hwDeviceContext);
			impl->codec->get_format = selectHardwareFormat;
			SDL_Log("Video: VA-API hardware decoding enabled.");
		}
		if (result >= 0)
			result = avcodec_open2(impl->codec, decoder, nullptr);
		if (result < 0 && impl->hwDeviceContext != nullptr &&
			impl->disableHardwareDecoding()) {
			result = 0;
		}
		if (result < 0) {
			error = "FFmpeg could not start the video decoder: " + ffmpegError(result);
			close();
			return false;
		}
	}

	impl->packet = av_packet_alloc();
	impl->frame = impl->audioOnly ? nullptr : av_frame_alloc();
	if (impl->packet == nullptr || (!impl->audioOnly && impl->frame == nullptr)) {
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
	if (impl->blurayReader != nullptr)
		impl->totalDuration = std::max(impl->totalDuration.load(), impl->blurayReader->duration());
	if (impl->dvdReader != nullptr)
		impl->totalDuration = std::max(impl->totalDuration.load(), impl->dvdReader->duration());
	// The UI must not acquire a disc I/O mutex merely to display chapter labels.
	// This metadata stays fixed for the lifetime of the selected title.
	if (impl->blurayReader) {
		for (const auto& chapter : impl->blurayReader->chapters())
			impl->discChapterTimes.push_back(chapter.startTime);
	} else if (impl->dvdReader) {
		for (const auto& chapter : impl->dvdReader->chapters())
			impl->discChapterTimes.push_back(chapter.startTime);
	}
	if (impl->audioCdReader) {
		impl->totalDuration = impl->audioCdReader->stream().duration();
		impl->discChapterTimes = impl->audioCdReader->stream().chapters();
	}
	if (impl->audioOnly) impl->albumArt = impl->decodeAlbumArt();

	if (!impl->audioOnly) {
		const AVRational frameRate = av_guess_frame_rate(impl->format, impl->stream, nullptr);
		if (frameRate.num > 0 && frameRate.den > 0)
			impl->fallbackFrameDuration = av_q2d(av_inv_q(frameRate));
	}

	impl->stopRequested = false;
	impl->isPlaying = true;
	impl->audioState->playing = true;
	impl->isReady = true;
	impl->atEnd = false;
	int detectedRotation = 0;
	if (impl->stream != nullptr && impl->stream->codecpar != nullptr) {
		const AVPacketSideData* sd = av_packet_side_data_get(
			impl->stream->codecpar->coded_side_data,
			impl->stream->codecpar->nb_coded_side_data,
			AV_PKT_DATA_DISPLAYMATRIX);
		if (sd != nullptr && sd->data != nullptr && sd->size >= static_cast<int>(sizeof(int32_t) * 9)) {
			double theta = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
			int angle = static_cast<int>(std::round(theta)) % 360;
			if (angle < 0) angle += 360;
			if (angle >= 45 && angle < 135) detectedRotation = 90;
			else if (angle >= 135 && angle < 225) detectedRotation = 180;
			else if (angle >= 225 && angle < 315) detectedRotation = 270;
		} else {
			const AVDictionaryEntry* rotateTag = av_dict_get(impl->stream->metadata, "rotate", nullptr, 0);
			if (rotateTag != nullptr && rotateTag->value != nullptr) {
				try {
					int angle = static_cast<int>(std::round(std::stod(rotateTag->value))) % 360;
					if (angle < 0) angle += 360;
					if (angle >= 45 && angle < 135) detectedRotation = 90;
					else if (angle >= 135 && angle < 225) detectedRotation = 180;
					else if (angle >= 225 && angle < 315) detectedRotation = 270;
				} catch (...) {}
			}
		}
	}
	impl->rotation = detectedRotation;
	const int widthVal = (impl->codec && impl->codec->width > 0 ? impl->codec->width : impl->stream->codecpar->width) * (impl->mvcDecoder ? 2 : 1);
	const int heightVal = impl->codec && impl->codec->height > 0 ? impl->codec->height : impl->stream->codecpar->height;
	const bool swapDims = (detectedRotation == 90 || detectedRotation == 270);
	impl->videoWidth = impl->audioOnly ? 0 : (swapDims ? heightVal : widthVal);
	impl->videoHeight = impl->audioOnly ? 0 : (swapDims ? widthVal : heightVal);
	impl->decodeThread = std::thread(&Impl::decodeLoop, impl.get());
	return true;
#endif
}

// Stop the decode worker, invalidate outstanding frames, and release all source and playback resources.
void VideoPlayer::close() {
	impl->playbackGeneration.fetch_add(1);
	impl->stopRequested = true;
	impl->stateChanged.notify_all();
	if (impl->decodeThread.joinable()) impl->decodeThread.join();
	{
		std::lock_guard lock(impl->waveformMutex);
		impl->waveform.clear();
	}
	{
		std::lock_guard lock(impl->stateMutex);
		impl->pendingFrames.clear();
		impl->requestedSeek.reset();
		impl->runtimeError.clear();
	}
	impl->stateChanged.notify_all();
#ifdef RENDEPTH_ENABLE_FFMPEG
	impl->rotation = 0;
	SDL_DestroySurface(impl->albumArt);
	impl->albumArt = nullptr;
	impl->audioState->shutdown();
	impl->audioState = std::make_shared<Impl::AudioState>();
	impl->audioState->volume = static_cast<float>(impl->audioVolume.load());
	swr_free(&impl->audioResampler);
	av_frame_free(&impl->audioFrame);
	avcodec_free_context(&impl->audioCodec);
	impl->audioStream = nullptr;
	impl->audioStreamIndex = -1;
	impl->audioQueuePosition = 0.0;
	impl->audioQueuePositionValid = false;
	sws_freeContext(impl->scaler);
	impl->scaler = nullptr;
	sws_freeContext(impl->inferenceScaler);
	impl->inferenceScaler = nullptr;
	av_buffer_unref(&impl->hwDeviceContext);
	av_frame_free(&impl->frame);
	av_packet_free(&impl->packet);
	avcodec_free_context(&impl->codec);
	avformat_close_input(&impl->format);
	if (impl->avioContext != nullptr) {
		av_freep(&impl->avioContext->buffer);
		avio_context_free(&impl->avioContext);
		impl->avioContext = nullptr;
	}
	// Release disc reader sessions after the input format and AVIO context are
	// closed, so no AVIO callback can fire against a destroyed reader.
	impl->discReadAhead.reset();
	impl->blurayReader.reset();
	impl->mvcDecoder.reset();
	impl->discReadMutex.reset();
	impl->dvdReader.reset();
	impl->audioCdReader.reset();
	impl->discChapterTimes.clear();
	impl->stream = nullptr;
	impl->streamIndex = -1;
	impl->audioOnly = false;
	impl->videoDecoder = nullptr;
	impl->audioStreamIndices.clear();
	impl->subtitleStreamIndices.clear();
	impl->selectedAudioTrack = 0;
	impl->requestedAudioTrack = -1;
	impl->selectedSubtitleTrack = noSubtitleTrack;
	impl->requestedSubtitleTrack = noSubtitleRequest;
	#ifdef RENDEPTH_ENABLE_FFMPEG
	avcodec_free_context(&impl->subtitleCodec);
	impl->subtitleStreamIndex = -1;
	#endif
	{
		std::lock_guard lock(impl->subtitleMutex);
		impl->subtitleCues.clear();
	}
	impl->streamStart = 0.0;
	impl->fallbackFrameDuration = 1.0 / 30.0;
	impl->hardwareDecodeState.failed = false;
#endif
	impl->isPlaying = false;
	impl->isReady = false;
	impl->audioSeekPending = false;
	impl->atEnd = false;
	impl->currentPosition = 0.0;
	impl->presentedPosition = 0.0;
	impl->totalDuration = 0.0;
	impl->videoWidth = 0;
	impl->videoHeight = 0;
}

// Pause or resume playback, restarting from the beginning when resuming after end of stream.
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

// Hold or release audio playback while the presentation buffer catches up.
void VideoPlayer::setAudioBuffering(bool buffering) {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (!buffering && impl->audioSeekPending) return;
	impl->audioState->buffering = buffering;
	if (buffering) Impl::pauseAudio(impl->audioState);
	else if (impl->audioState->playing) Impl::startAudio(impl->audioState);
#else
	(void)buffering;
#endif
}

// Transfer ownership of the decoded album-art surface to the caller.
SDL_Surface* VideoPlayer::takeAlbumArt() {
	auto result = impl->albumArt;
	impl->albumArt = nullptr;
	return result;
}

// Report whether the source exposes any audio tracks.
bool VideoPlayer::hasAudio() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	return !impl->audioStreamIndices.empty();
#else
	return false;
#endif
}

// Report whether the current source plays without a video stream.
bool VideoPlayer::audioOnly() const { return impl->audioOnly; }

// Sample waveform bars at the audible position, suppressing them during pauses and seeks.
AudioWaveform::Bars VideoPlayer::audioWaveform() const {
	if (!impl->audioOnly || !impl->isPlaying || impl->audioSeekPending) return {};
	const double position = audioPlaybackPosition();
	std::lock_guard lock(impl->waveformMutex);
	if (impl->audioSeekPending) return {};
	return impl->waveform.at(position, static_cast<float>(impl->audioVolume.load()));
}

// Identify playback backed by an audio-CD reader.
bool VideoPlayer::audioCd() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
    return impl->audioCdReader != nullptr;
#else
    return false;
#endif
}

// Identify native MVC stereo playback that does not need inferred depth.
bool VideoPlayer::nativeStereo() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	return impl->mvcDecoder != nullptr;
#else
	return false;
#endif
}

// Report whether playback uses one of the optical-disc readers.
bool VideoPlayer::discSource() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	return impl->blurayReader != nullptr || impl->dvdReader != nullptr || impl->audioCdReader != nullptr;
#else
	return false;
#endif
}

// Report whether the asynchronous audio-device initialization has produced an output stream.
bool VideoPlayer::audioReady() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	std::lock_guard lock(impl->audioState->mutex);
	return impl->audioState->output != nullptr;
#else
	return false;
#endif
}

// Estimate the duration of audio waiting to be played.
double VideoPlayer::bufferedAudioDuration() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	std::lock_guard lock(impl->audioState->mutex);
	int bytes = impl->audioState->pendingBytes;
	if (impl->audioState->output != nullptr)
		bytes = std::max(0, SDL_GetAudioStreamQueued(impl->audioState->output));
	return bytes / static_cast<double>(Impl::audioSampleRate * Impl::audioBytesPerFrame);
#else
	return 0.0;
#endif
}

// Estimate the audio device's buffering latency from its sample-block size.
double VideoPlayer::audioDeviceLatency() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	std::lock_guard lock(impl->audioState->mutex);
	if (impl->audioState->output == nullptr) return 0.0;
	SDL_AudioSpec spec{};
	int sampleFrames = 0;
	const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(impl->audioState->output);
	if (device == 0 || !SDL_GetAudioDeviceFormat(device, &spec, &sampleFrames) ||
		spec.freq <= 0 || sampleFrames <= 0) return 0.0;
	return sampleFrames / static_cast<double>(spec.freq);
#else
	return 0.0;
#endif
}

// Estimate the audible timeline position after subtracting queued samples and device latency.
double VideoPlayer::audioPlaybackPosition() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (impl->audioSeekPending) return -1.0;
	std::lock_guard lock(impl->audioState->mutex);
	if (impl->audioSeekPending || impl->audioState->output == nullptr || !impl->audioState->started ||
		impl->audioState->buffering) return -1.0;
	// A track change clears the audio queue before the decoder reaches the new
	// track's samples. Let presentation use its wall clock during that gap;
	// a frozen audio clock would fill the video queues and block the decoder.
	if (!impl->audioQueuePositionValid) return -1.0;
	const int queuedBytes = SDL_GetAudioStreamQueued(impl->audioState->output);
	if (queuedBytes < 0) return -1.0;
	double deviceLatency = 0.0;
	SDL_AudioSpec spec{};
	int sampleFrames = 0;
	const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(impl->audioState->output);
	if (device != 0 && SDL_GetAudioDeviceFormat(device, &spec, &sampleFrames) &&
		spec.freq > 0 && sampleFrames > 0)
		deviceLatency = sampleFrames / static_cast<double>(spec.freq);
	const double queuedDuration = queuedBytes /
		static_cast<double>(Impl::audioSampleRate * Impl::audioBytesPerFrame);
	return std::max(0.0,
		impl->audioQueuePosition.load() - queuedDuration - deviceLatency);
#else
	return -1.0;
#endif
}

// Record the timestamp actually presented by the renderer.
void VideoPlayer::setPresentedPosition(double seconds) {
	impl->presentedPosition = std::max(0.0, seconds);
}

// Apply a clamped volume to current and future audio output, treating near-zero values as mute.
void VideoPlayer::setVolume(double volume) {
	volume = std::clamp(volume, 0.0, 1.0);
	if (volume < 0.01) volume = 0.0;
	impl->audioVolume = volume;
#ifdef RENDEPTH_ENABLE_FFMPEG
	impl->audioState->volume = static_cast<float>(volume);
	bool resume = false;
	{
		std::lock_guard lock(impl->audioState->mutex);
		if (impl->audioState->output != nullptr) {
			SDL_SetAudioStreamGain(impl->audioState->output, static_cast<float>(volume));
			if (impl->audioState->playing && !impl->audioState->started) {
				resume = true;
			}
		}
	}
	if (resume) Impl::startAudio(impl->audioState);
#endif
}

// Queue selection of the next audio track for the decode worker.
void VideoPlayer::cycleAudioTrack() {
	if (!impl->isReady || impl->audioStreamIndices.size() < 2) return;
	const int requested = impl->requestedAudioTrack.load();
	const int current = requested >= 0 ? requested : impl->selectedAudioTrack.load();
	const int next = (current + 1) %
		static_cast<int>(impl->audioStreamIndices.size());
	impl->requestedAudioTrack = next;
	impl->stateChanged.notify_all();
}

// Cycle through subtitle tracks and the disabled state.
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

// Request a return to the source's first audio track.
void VideoPlayer::resetAudioTrack() {
	if (!impl->isReady || impl->audioStreamIndices.empty()) return;
	impl->requestedAudioTrack = 0;
	impl->stateChanged.notify_all();
}

// Request that subtitle decoding be disabled.
void VideoPlayer::resetSubtitleTrack() {
	if (!impl->isReady || impl->subtitleStreamIndices.empty()) return;
	impl->requestedSubtitleTrack = noSubtitleTrack;
	impl->stateChanged.notify_all();
}

// Find the subtitle cue covering the displayed timestamp and discard expired cues.
std::shared_ptr<const VideoSubtitle> VideoPlayer::subtitle() const {
	const double position = impl->presentedPosition.load();
	std::lock_guard lock(impl->subtitleMutex);
	while (!impl->subtitleCues.empty() && impl->subtitleCues.front().end <= position)
		impl->subtitleCues.pop_front();
	for (const auto& cue : impl->subtitleCues) {
		if (cue.start > position) break;
		if (position < cue.end) return cue.subtitle;
	}
	return nullptr;
}

// Return the current textual subtitle, or an empty string for bitmap or absent cues.
std::string VideoPlayer::subtitleText() const {
	const auto current = subtitle();
	return current != nullptr && current->format == VideoSubtitle::Format::Text
		? current->text : std::string{};
}

namespace {
#ifdef RENDEPTH_ENABLE_FFMPEG
// Build a readable track label from title, handler, or language metadata.
std::string streamLanguage(const AVStream* stream, bool includeHandlerName = true) {
	if (stream == nullptr) return "Unknown";
	if (const AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0))
		if (title->value != nullptr && title->value[0] != '\0') return title->value;
	if (includeHandlerName)
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
		return code.empty() || code == "und" ? "Unknown" : code;
	}
	return "Unknown";
}
#endif
}

// Describe the selected or pending audio track for the playback UI.
std::string VideoPlayer::audioLanguage() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (impl->format != nullptr && !impl->audioStreamIndices.empty()) {
		const auto requested = impl->requestedAudioTrack.load();
		const auto track = requested >= 0 ? requested : impl->selectedAudioTrack.load();
		const auto selected = std::clamp(track, 0, static_cast<int>(impl->audioStreamIndices.size()) - 1);
		const auto index = impl->audioStreamIndices[selected];
		const auto language = streamLanguage(impl->format->streams[index], false);
		const auto number = std::to_string(selected + 1) + "/" + std::to_string(impl->audioStreamIndices.size());
		if (language == "Unknown" || language == "unknown" || language == "SoundHandler")
			return "Track " + number;
		return language + " (Track " + number + ")";
	}
#endif
	return "N/A";
}

// Describe the selected subtitle track or the disabled state for the playback UI.
std::string VideoPlayer::subtitleLanguage() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (impl->format != nullptr && !impl->subtitleStreamIndices.empty()) {
		const auto requested = impl->requestedSubtitleTrack.load();
		const auto track = requested != noSubtitleRequest ? requested : impl->selectedSubtitleTrack.load();
		if (track == noSubtitleTrack)
			return "Off (" + std::to_string(impl->subtitleStreamIndices.size()) + " available)";
		const auto selected = std::clamp(track, 0, static_cast<int>(impl->subtitleStreamIndices.size()) - 1);
		const auto index = impl->subtitleStreamIndices[selected];
		const auto language = streamLanguage(impl->format->streams[index], false);
		const auto number = std::to_string(selected + 1) + "/" + std::to_string(impl->subtitleStreamIndices.size());
		if (language == "Unknown" || language == "unknown" || language == "SubtitleHandler")
			return "Captions " + number;
		return language + " (Captions " + number + ")";
	}
#endif
	return "Off";
}

// Queue a seek and invalidate old presentation data before waking the decoder.
void VideoPlayer::seek(double seconds, bool fastPreview) {
	if (!impl->isReady || (fastPreview && discSource())) return;
	seconds = std::clamp(seconds, 0.0,
		impl->totalDuration > 0.0 ? impl->totalDuration.load() : seconds);
	#ifdef RENDEPTH_ENABLE_FFMPEG
	const bool hasAudioPlayback = impl->audioOnly || !impl->audioStreamIndices.empty();
	#endif
	{
		std::lock_guard lock(impl->stateMutex);
		#ifdef RENDEPTH_ENABLE_FFMPEG
		if (hasAudioPlayback) impl->audioSeekPending = true;
		#endif
		impl->playbackGeneration.fetch_add(1);
		impl->requestedSeek = Impl::SeekRequest{seconds, fastPreview};
		impl->currentPosition = seconds;
		impl->presentedPosition = seconds;
		impl->atEnd = false;
	}
	#ifdef RENDEPTH_ENABLE_FFMPEG
	if (hasAudioPlayback) {
		std::lock_guard lock(impl->audioState->mutex);
		impl->audioState->buffering = true;
		impl->audioState->pending.clear();
		impl->audioState->pendingBytes = 0;
		if (impl->audioState->output != nullptr) {
			SDL_PauseAudioStreamDevice(impl->audioState->output);
			SDL_ClearAudioStream(impl->audioState->output);
			impl->audioState->started = false;
		}
	}
	#endif
	impl->stateChanged.notify_all();
}

// Advance the audio-only presentation clock and release its initial buffering hold when ready.
void VideoPlayer::update() {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (!impl->audioOnly) return;
	if (impl->audioSeekPending) return;
	if (impl->isPlaying && impl->audioState->buffering &&
		bufferedAudioDuration() >= 0.12)
		setAudioBuffering(false);
	const double playbackPosition = audioPlaybackPosition();
	if (playbackPosition >= 0.0) impl->presentedPosition = playbackPosition;
#endif
}

// Remove the next decoded frame and wake the producer when queue space becomes available.
std::shared_ptr<VideoFrame> VideoPlayer::takeFrame(bool* preview) {
	std::lock_guard lock(impl->stateMutex);
	if (impl->pendingFrames.empty()) return nullptr;
	auto item = std::move(impl->pendingFrames.front());
	impl->pendingFrames.pop_front();
	if (preview != nullptr) *preview = item.frame != nullptr && item.isPreview;
	impl->stateChanged.notify_all();
	return item.frame;
}

// Consume the pending playback error so it is reported only once.
std::string VideoPlayer::takeError() {
	std::lock_guard lock(impl->stateMutex);
	std::string result;
	result.swap(impl->runtimeError);
	return result;
}

// Set the maximum dimensions used when preparing display frames.
void VideoPlayer::setOutputSize(int maxWidth, int maxHeight) {
	impl->outputMaxWidth = std::max(0, maxWidth);
	impl->outputMaxHeight = std::max(0, maxHeight);
}

// Set the size and sampling rate of RGBA frames prepared for depth inference.
void VideoPlayer::setInferenceSize(int maxDimension, double framesPerSecond) {
	impl->inferenceMaxDimension = std::max(0, maxDimension);
	impl->inferenceFramesPerSecond = std::max(0.0, framesPerSecond);
}

// Report whether playback is currently advancing.
bool VideoPlayer::playing() const { return impl->isPlaying; }
// Report whether a source is ready for playback controls.
bool VideoPlayer::ready() const { return impl->isReady; }
// Report whether chapter or audio-CD track navigation is available.
bool VideoPlayer::hasChapters() const { return chapterCount() > 0; }

// Return chapter count from the disc reader or container metadata.
int VideoPlayer::chapterCount() const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (discSource()) return static_cast<int>(impl->discChapterTimes.size());
	if (impl->format != nullptr) return static_cast<int>(impl->format->nb_chapters);
#endif
	return 0;
}

// Find the chapter containing the last presented position.
int VideoPlayer::currentChapter() const {
	return chapterAtTime(position());
}

// Find the chapter at a timeline position, allowing the disc-specific boundary tolerance.
int VideoPlayer::chapterAtTime(double seconds) const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (discSource()) {
		const int count = chapterCount();
		for (int i = count - 1; i >= 0; --i) {
			const double start = impl->discChapterTimes[i];
			if (seconds >= start - (audioCd() ? 1.0 / 44100.0 : 0.5)) return i;
		}
		return 0;
	}
	if (impl->format != nullptr && impl->format->nb_chapters > 0) {
		for (int i = static_cast<int>(impl->format->nb_chapters) - 1; i >= 0; --i) {
			const AVChapter* ch = impl->format->chapters[i];
			if (ch != nullptr) {
				const double start = static_cast<double>(ch->start) * av_q2d(ch->time_base);
				if (seconds >= start - (audioCd() ? 1.0 / 44100.0 : 0.5)) return i;
			}
		}
	}
#endif
	return 0;
}

// Return a chapter's start time from disc or container metadata.
double VideoPlayer::chapterTime(int chapterIndex) const {
#ifdef RENDEPTH_ENABLE_FFMPEG
	if (discSource()) {
		return chapterIndex >= 0 && chapterIndex < static_cast<int>(impl->discChapterTimes.size())
			? impl->discChapterTimes[chapterIndex] : 0.0;
	}
	if (impl->format != nullptr && chapterIndex >= 0 &&
		chapterIndex < static_cast<int>(impl->format->nb_chapters)) {
		const AVChapter* ch = impl->format->chapters[chapterIndex];
		if (ch != nullptr) return static_cast<double>(ch->start) * av_q2d(ch->time_base);
	}
#endif
	return 0.0;
}

// Seek to a chapter after clamping its index to the available range.
void VideoPlayer::seekChapter(int chapterIndex) {
	const int count = chapterCount();
	if (count <= 0) return;
	const int target = std::clamp(chapterIndex, 0, count - 1);
	const double targetTime = chapterTime(target);
	seek(targetTime);
}

// Advance to the next chapter when one exists.
void VideoPlayer::nextChapter() {
	const int count = chapterCount();
	if (count <= 0) return;
	const int current = currentChapter();
	if (current + 1 < count) {
		seekChapter(current + 1);
	}
}

// Restart the current chapter after three seconds of playback, otherwise move to the previous one.
void VideoPlayer::previousChapter() {
	const int count = chapterCount();
	if (count <= 0) return;
	const int current = currentChapter();
	const double currentPos = position();
	const double currentChapterStart = chapterTime(current);
	if (currentPos - currentChapterStart > 3.0 || current == 0) {
		seekChapter(current);
	} else {
		seekChapter(current - 1);
	}
}

// Return the last presented timeline position in seconds.
double VideoPlayer::position() const { return impl->presentedPosition; }
// Return the known source duration in seconds.
double VideoPlayer::duration() const { return impl->totalDuration; }
// Expose the generation used to reject frames from earlier opens or seeks.
std::uint64_t VideoPlayer::generation() const { return impl->playbackGeneration; }
// Return the source width after applying display rotation.
int VideoPlayer::width() const { return impl->videoWidth; }
// Return the source height after applying display rotation.
int VideoPlayer::height() const { return impl->videoHeight; }
// Return the source's display rotation in degrees.
int VideoPlayer::rotation() const { return impl->rotation; }
