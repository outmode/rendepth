// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "VideoPlayer.h"

#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <mutex>
#include <optional>
#include <thread>

namespace {
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
	std::atomic<int> videoWidth{0};
	std::atomic<int> videoHeight{0};

#ifdef RENDEPTH_ENABLE_FFMPEG
	AVFormatContext* format = nullptr;
	AVCodecContext* codec = nullptr;
	AVStream* stream = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	SwsContext* scaler = nullptr;
	int streamIndex = -1;
	double streamStart = 0.0;
	double fallbackFrameDuration = 1.0 / 30.0;

	void setRuntimeError(const std::string& message) {
		{
			std::lock_guard lock(stateMutex);
			if (runtimeError.empty()) runtimeError = message;
		}
		isPlaying = false;
		isReady = false;
		stateChanged.notify_all();
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

	bool waitForPresentation(double position, bool& clockValid,
			std::chrono::steady_clock::time_point& clockOrigin, double& mediaOrigin) {
		std::unique_lock lock(stateMutex);
		for (;;) {
			if (stopRequested || requestedSeek.has_value()) return false;
			if (!isPlaying) {
				clockValid = false;
				stateChanged.wait(lock, [&] {
					return stopRequested || requestedSeek.has_value() || isPlaying.load();
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
		if (pausedSeekFrame) {
			if (interrupted()) return false;
			clockValid = false;
		} else if (!waitForPresentation(position, clockValid, clockOrigin, mediaOrigin)) {
			return false;
		}

		if (!pausedSeekFrame) {
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

	bool applySeek(const SeekRequest& request, std::optional<double>& seekFloor) {
		const int64_t target = static_cast<int64_t>(
			(request.seconds + streamStart) / av_q2d(stream->time_base));
		const int result = av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD);
		if (result < 0) {
			setRuntimeError("FFmpeg seek failed: " + ffmpegError(result));
			return false;
		}
		avcodec_flush_buffers(codec);
		codec->skip_frame = request.fastPreview ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
		av_packet_unref(packet);
		av_frame_unref(frame);
		currentPosition = request.seconds;
		atEnd = false;
		seekFloor = request.seconds;
		return true;
	}

	void decodeLoop() {
		bool clockValid = false;
		auto clockOrigin = std::chrono::steady_clock::now();
		double mediaOrigin = 0.0;
		std::optional<double> seekFloor;
		bool seekPreviewPending = false;
		bool fastPreviewPending = false;

		while (!stopRequested) {
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
				if (!applySeek(*seek, seekFloor)) break;
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
					if (!applySeek({0.0, false}, seekFloor)) break;
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
	sws_freeContext(impl->scaler);
	impl->scaler = nullptr;
	av_frame_free(&impl->frame);
	av_packet_free(&impl->packet);
	avcodec_free_context(&impl->codec);
	avformat_close_input(&impl->format);
	impl->stream = nullptr;
	impl->streamIndex = -1;
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
	impl->stateChanged.notify_all();
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
