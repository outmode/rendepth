// Copyright (c) 2026 Outmode
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define SDL_HINT_RENDER_VSYNC "SDL_RENDER_VSYNC"
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_image/SDL_image.h>
#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#define QOI_MALLOC SDL_malloc
#define QOI_FREE SDL_free
#include "../ThirdParty/SDL_image/src/qoi.h"
#undef QOI_FREE
#undef QOI_MALLOC
#undef QOI_IMPLEMENTATION
#undef QOI_NO_STDIO
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <filesystem>
#include <format>
#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cctype>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <cstdlib>
#include <regex>
#include <random>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Core.h"
#include "Utils.h"
#include "Image.h"
#include "DepthEstimator.h"
#include "SuperResolution.h"
#include "ModelDownloader.h"
#include "VideoPlayer.h"
#include "VideoDepthProcessor.h"
#include "ScreenCapture.h"

Context context{};
Image imageView{};
static VideoPlayer videoPlayer;
static ScreenCapture screenCapture;
static VideoDepthProcessor videoDepthProcessor;
static std::shared_ptr<VideoFrame> lastVideoFrame;
struct BufferedVideoFrame {
	std::shared_ptr<VideoFrame> frame;
	bool preview = false;
};
static std::deque<BufferedVideoFrame> bufferedVideoFrames;
static std::deque<std::unique_ptr<VideoDepthFrame>> bufferedVideoDepthFrames;
static bool videoPlaybackBufferReady = false;
static bool videoPresentationClockValid = false;
static double videoBufferStartedAt = -1.0;
static double videoPresentationWallOrigin = 0.0;
static double videoPresentationMediaOrigin = 0.0;
static std::uint64_t videoPlaybackGeneration = 0;
// During a playback-generation transition, keep presenting the previous
// frame until the first matching depth map for the new generation is ready.
static std::uint64_t videoDepthGeneration = 0;
static std::uint64_t videoDepthTransitionGeneration = 0;
static bool videoFileTransitionActive = false;
static bool videoFileTransitionNeedsDepth = false;
// Keep enough decoded media ahead of the audio clock for depth inference to
// finish without making color or audio wait on the depth worker.
static constexpr double videoDepthLookaheadDuration = 0.1;
struct VideoDepthBlendState {
	std::vector<std::uint16_t> displayed;
	std::vector<std::uint16_t> source;
	std::vector<std::uint16_t> target;
	int width = 0;
	int height = 0;
	double startTime = -1.0;
	double lastUploadTime = -1.0;
	double presentationTime = -1.0;
	bool active = false;

	void clear() {
		displayed.clear();
		source.clear();
		target.clear();
		width = 0;
		height = 0;
		startTime = -1.0;
		lastUploadTime = -1.0;
		presentationTime = -1.0;
		active = false;
	}
};
static VideoDepthBlendState videoDepthBlend;
static bool activeVideo = false;
static bool activeScreenCapture = false;
static bool videoFrameLoaded = false;
static bool videoDepthFrameLoaded = false;
static SDL_Surface* audioAlbumArt = nullptr;
static int activeVideoDepthModelOption = -1;
static bool videoDepthRestartPending = false;
static double getTimeNow();
static void updateVideoSlider();
static void setVideoControlsVisible(bool visible);
static void finishSliderDrag();
static Icon& getIcon(IconType type);
static void checkMouseState();
static void refreshDisplay3D(StereoFormat type);

static void clearAudioAlbumArt() {
	SDL_DestroySurface(audioAlbumArt);
	audioAlbumArt = nullptr;
}

static bool isSupportedVideo(const std::string& path) {
	return VideoPlayer::supported(std::filesystem::path(path));
}

static bool isSupportedAudio(const std::string& path) {
	auto extension = std::filesystem::path(path).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return extension == ".aac" || extension == ".flac" || extension == ".m4a" ||
		extension == ".mp3" || extension == ".oga" || extension == ".ogg" ||
		extension == ".opus" || extension == ".wav";
}

static std::string stringLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c){ return std::tolower(c); });
	return s;
}

static const std::vector<std::string> supportedExts { ".jpeg", ".jpg", ".jpe",
	".jfif", ".jif", ".jps", ".png", ".pns", ".tga", ".bmp", ".dib", ".tif",
	".tiff", ".ico", ".cur", ".qoi", ".avif", ".avifs", ".jxl", ".webp"
};
bool isSupportedImage(const std::string& path) {
	auto filePath = std::filesystem::path(path);
	auto fileExt = stringLower(filePath.extension().string());
	for (const auto& ext : supportedExts) {
		if (fileExt == ext) return true;
	}
	return false;
}

static bool isSupportedMedia(const std::string& path) {
	return isSupportedVideo(path) || isSupportedAudio(path);
}

enum class MediaType {
	Image,
	Video,
	Audio,
	Unknown
};

static MediaType getMediaType(const std::string& path) {
	if (isSupportedAudio(path)) return MediaType::Audio;
	if (isSupportedVideo(path)) return MediaType::Video;
	if (isSupportedImage(path)) return MediaType::Image;
	return MediaType::Unknown;
}

static void serviceVideo();
static void serviceScreenCapture();
static void serviceVideoDepth();
static void resetVideoPlaybackBuffer(bool holdAudio = true, bool clearDepth = true);
static bool startVideoDepth(bool preserveTexture = false);
static void stopVideoDepth(bool disable3D = true, bool clearTexture = true);
static void seekVideo(double seconds, bool fastPreview = false);
static void failVideoLoad(const std::string& error);
static bool skipVideoBy(double seconds);
static glm::ivec2 getNativeDepthSize(int sourceWidth, int sourceHeight);
static SDL_Surface* prepareNativeColorSurface(SDL_Surface* color,
	StereoFormat sourceType);
static std::shared_ptr<VideoFrame> prepareVideoFrameForDisplay(
	const std::shared_ptr<VideoFrame>& frame);
static void setDisplay3D(bool display);
static void setStereoMode(ViewMode mode);
static void setShowStereoSettings(bool show);
static void hideUI(bool hideCustomMouse = true, bool hideRealMouse = false);
static void endPreload(bool success);
static void resetRapidBrowseState();

glm::vec2 windowSize{};
auto switchedImage = false;
auto isFullscreen = false;
auto isMaximized = false;
auto quitAppNextFrame = false;
auto currentVisibility = 1.0;
auto targetVisibility = 1.0;
auto currentZoom = 1.0;
auto targetZoom = 1.0;
auto wheelSpeed = 0;
auto depthEffect = 1.0;
auto currentOffset = glm::vec2(0.0);
auto targetOffset = glm::vec2(0.0);
auto lastTime = 0.0;
auto lastClick = 0.0;
auto lastClickTime = 0.0;
auto doubleClickTime = 0.25;
static bool leftClickConsumedByUI = false;
auto lastSwitchTime = 0.0;
auto mouseLastActive = 0.0;
auto mouseMoveWait = 3.0;
auto displayInfoTime = 0.0;
auto displayInfoEnabled = true;
auto showDisplayInfoOnce = false;
auto currentStereoMode = Native;
auto preferredStereoMode = Anaglyph_Accurate;
auto defaultStereoMode = Anaglyph_Accurate;
glm::vec2 pixelMotion = {0.0, 0.0 };
bool isDragging = false;
bool isIconCaptured = false;
const auto mouseDelayCount = 1;
auto mouseMoveDelay = mouseDelayCount;
auto mouseValueNull = -128.0f;
bool isConverting = false;
bool justConverted = false;
static bool keep3DForDepthReload = false;
SDL_Thread* depthGenThread = nullptr;
std::atomic<bool> depthGenAlive (false);
std::atomic<bool> doneLoadingImage (false);
std::atomic<bool> depthGenerationError (false);
std::atomic<bool> doingFileOp (false);
std::atomic<bool> doingPreload (false);
std::vector<std::function<void()>> callbackQueue{};
std::string qualityMode = "0";
bool display3D = false;
bool prevNextKeyDown = false;
Icon* currentSlider = nullptr;
static bool videoSliderScrubbing = false;
static bool videoSliderWasPlaying = false;
static double videoScrubLastPreviewTime = 0.0;
static constexpr double videoScrubPreviewInterval = 0.075;
static bool videoControlsVisible = false;
auto currentSliderValue = 0.0;
auto showingStereoSettings = false;
auto isPlayingSlideshow = false;
auto slideshowWaitTime = 8.0;
auto lastSlideshowTime = 0.0;
auto displayTipTime = 0.0;
auto displayTipWait = 2.0;
auto swapLeftRight = false;
auto mouseLeftWindow = false;
auto mouseStateInitialized = false;
auto mouseIsDown = false;
auto showGoFullScreenOnce = true;
auto deltaIndex = 0;
const int deltaCount = 16;
std::array<double, deltaCount> deltaTimes{};
std::string openFolderResult;
std::string upscaleResolution = "1920";
std::string depthSize = "540";
StereoFormat exportFormat = Color_Anaglyph;
std::string exportTag = "anaglyph";
EyesFormat eyesFormat = Left_Right;
SortOrder sortOrder = Alpha_Ascending;

std::string exportFolderName = "3D Export";
static std::filesystem::path batchFolderPath;
static std::vector<std::filesystem::path> batchInputPaths;
static size_t batchExportIndex = 0;
static bool batchDepthGeneration = false;
static bool batchExportActive = false;
static bool losslessDepthmaps = true;
static std::filesystem::path runtimeDepthOutputPath(const std::filesystem::path& input);
std::string infoSettingKey = "Display Info";
std::string borderlessSettingKey = "Borderless Window";
std::string currentInfoLabel;
Style style;

std::vector<FileInfo> fileList{};
static std::vector<std::string> deferredMediaFiles{};
auto fileIndex = 0;
static void failVideoLoad(const std::string& error) {
	if (!error.empty()) {
		const char* path = fileList.empty() ? "" : fileList[fileIndex].link.c_str();
		SDL_Log("Could not load video %s: %s", path, error.c_str());
	}

	std::string errorLower = error;
	std::transform(errorLower.begin(), errorLower.end(), errorLower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	const char* displayText = "Could Not Load Video";
	if (errorLower.find("encrypt") != std::string::npos ||
		errorLower.find("aacs") != std::string::npos ||
		errorLower.find("bd+") != std::string::npos ||
		errorLower.find("bdplus") != std::string::npos ||
		errorLower.find("protect") != std::string::npos ||
		errorLower.find("blu-ray") != std::string::npos ||
		errorLower.find("bluray") != std::string::npos ||
		errorLower.find("dvd") != std::string::npos) {
		displayText = "Encrypted Disc Not Supported";
	}

	stopVideoDepth();
	videoPlayer.close();
	clearAudioAlbumArt();
	resetVideoPlaybackBuffer(false);
	context.chapterMarkers.clear();
	lastVideoFrame.reset();
	activeVideo = false;
	videoFrameLoaded = false;
	setVideoControlsVisible(false);
	setDisplay3D(false);
	setStereoMode(Native);
	setShowStereoSettings(false);
	isPlayingSlideshow = false;
	context.displayMenu = false;
	context.gotoPrev = false;
	context.gotoNext = false;
	context.gotoRand = false;
	isConverting = false;
	justConverted = false;
	resetRapidBrowseState();
	currentVisibility = 0.0;
	targetVisibility = 0.0;
	context.visibility = 0.0f;
	Image::displayTip = false;
	Image::infoTargetVisibility = 0.0f;
	Image::infoCurrentVisibility = 0.0f;
	currentInfoLabel.clear();
	currentSlider = nullptr;
	mouseIsDown = false;
	isDragging = false;
	if (doingPreload) endPreload(false);

	for (auto& file : fileList) {
		SDL_DestroySurface(file.preload);
		file.preload = nullptr;
	}
	fileList.clear();
	fileIndex = -1;

	FileInfo emptyFile{};
	Image::load(&context, emptyFile, nullptr);
	context.loading = false;
	doneLoadingImage = true;
	context.fileLink.clear();
	context.fileName.clear();
	context.infoText.clear();
	SDL_SetWindowTitle(context.window, "Rendepth");

	Core::drawText(&context, displayText, Image::helpFont,
		Image::helpTexture, Image::helpTextSize, "Help Texture");
	Image::displayHelp = true;
	Image::displayInfo = false;

	getIcon(IconType::Play).image = IconType::Play;
	mouseLastActive = 0.0;
	hideUI();
	checkMouseState();
}

static void resetVideoPlaybackBuffer(bool holdAudio, bool clearDepth) {
	bufferedVideoFrames.clear();
	bufferedVideoDepthFrames.clear();
	videoDepthBlend.clear();
	videoPlaybackBufferReady = false;
	videoPresentationClockValid = false;
	videoBufferStartedAt = -1.0;
	videoPlaybackGeneration = videoPlayer.generation();
	if (clearDepth && videoDepthProcessor.running()) {
		Image::clearVideoDepth(&context);
		videoDepthFrameLoaded = false;
		videoDepthGeneration = 0;
		videoDepthTransitionGeneration = 0;
		if (!fileList.empty()) context.imageType = fileList[fileIndex].type;
	}
	if (holdAudio && videoPlayer.ready()) videoPlayer.setAudioBuffering(true);
}

static void presentVideoFrame(const std::shared_ptr<VideoFrame>& frame, bool preview) {
	if (frame == nullptr) return;
	lastVideoFrame = frame;
	const auto processedFrame = prepareVideoFrameForDisplay(frame);
	const VideoFrame& displayFrame = processedFrame != nullptr ? *processedFrame : *frame;
	if (activeVideo) videoPlayer.setPresentedPosition(frame->presentationTime);
	const bool completingVideoFileTransition = videoFileTransitionActive &&
		activeVideo && frame->generation == videoPlaybackGeneration;
	const bool firstFrame = !videoFrameLoaded || completingVideoFileTransition;
	if (firstFrame) {
		const bool transitionNeedsDepth = completingVideoFileTransition &&
			videoFileTransitionNeedsDepth;
		if (completingVideoFileTransition && !transitionNeedsDepth) {
			// A source-stereo replacement does not use the previous video's inferred
			// depth map. Retain it until this frame is ready, then switch atomically.
			Image::clearVideoDepth(&context);
			videoDepthFrameLoaded = false;
			videoDepthGeneration = 0;
		}
		const bool isConverted3D = (completingVideoFileTransition && transitionNeedsDepth && videoDepthFrameLoaded) ||
			(activeVideo && display3D && videoDepthProcessor.running() && videoDepthFrameLoaded);
		context.imageType = isConverted3D ? Color_Plus_Depth :
			(activeScreenCapture ? Color_Only : fileList[fileIndex].type);
		if (context.imageType == Light_Field_LKG)
			context.gridSize = Core::getGridInfo(fileList[fileIndex].base);
		context.fileName = activeScreenCapture ? "Screen Capture" : fileList[fileIndex].base;
		context.loading = false;
		Image::displayHelp = false;
		std::string windowTitle = activeScreenCapture ? "Rendepth - Screen Capture" : fileList[fileIndex].name;
		SDL_SetWindowTitle(context.window, windowTitle.c_str());
		context.fileLink = activeScreenCapture ? "screen-capture" : fileList[fileIndex].link;
		Image::updateVideoFrame(&context, displayFrame, true,
			activeScreenCapture ? displayFrame.width : videoPlayer.width(),
			activeScreenCapture ? displayFrame.height : videoPlayer.height(), true);
		if (fileList[fileIndex].type != Color_Only) {
			if (!display3D) setDisplay3D(true);
			refreshDisplay3D(context.imageType);
		} else if (isConverted3D) {
			refreshDisplay3D(Color_Plus_Depth);
			videoFileTransitionActive = false;
			videoFileTransitionNeedsDepth = false;
		}
		std::string infoText = activeScreenCapture ? "Live screen capture" : Core::getFileText(fileList[fileIndex], context.imageSize);
		context.infoText = infoText;
		videoFrameLoaded = true;
		if (activeScreenCapture) {
			Image::updateSize(&context);
			doneLoadingImage = false;
		} else {
			doneLoadingImage = true;
		}
		checkMouseState();
		if (completingVideoFileTransition) {
			videoFileTransitionActive = false;
			videoFileTransitionNeedsDepth = false;
		}
	} else {
		Image::updateVideoFrame(&context, displayFrame, false,
			videoPlayer.width(), videoPlayer.height(), !preview);
	}
}

static void serviceScreenCapture() {
	if (!activeScreenCapture) return;
	if (auto frame = screenCapture.takeFrame()) {
		if (videoFrameLoaded && (frame->width != context.imageSize.x || frame->height != context.imageSize.y))
			videoFrameLoaded = false;
		presentVideoFrame(frame, false);
		if (videoDepthProcessor.running()) videoDepthProcessor.submit(frame);
	}
}

static void serviceVideo() {
	if (!activeVideo) return;
	if (videoPlayer.audioOnly()) {
		videoPlayer.update();
		if (const std::string error = videoPlayer.takeError(); !error.empty()) {
			failVideoLoad(error);
			return;
		}
		getIcon(IconType::Play).image = videoPlayer.playing() ? IconType::Pause : IconType::Play;
		updateVideoSlider();
		return;
	}
	// Decode color frames at the requested output resolution rather than at
	// the window size. The depth worker remains independent and continues to
	// infer at its configured model resolution.
	const int sourceMaxDimension = std::max(videoPlayer.width(), videoPlayer.height());
	const bool prepareVideoColor = !fileList.empty() &&
		fileList[fileIndex].type != Color_Only &&
		fileList[fileIndex].type != Color_Plus_Depth &&
		sourceMaxDimension > 0 && sourceMaxDimension <= 1280;
	if (prepareVideoColor) {
		// Keep a source-resolution RGBA copy available for the shared color
		// pipeline. Depth inference can still resize this same copy internally.
		videoPlayer.setInferenceSize(1280, 60.0);
	} else if (!videoDepthProcessor.running()) {
		videoPlayer.setInferenceSize(0);
	}
	const auto videoOutputSize = getNativeDepthSize(videoPlayer.width(), videoPlayer.height());
	if (videoOutputSize.x > 0 && videoOutputSize.y > 0)
		videoPlayer.setOutputSize(videoOutputSize.x, videoOutputSize.y);
	else
		videoPlayer.setOutputSize(static_cast<int>(context.windowSize.x),
			static_cast<int>(context.windowSize.y));
	videoPlayer.update();
	if (const std::string error = videoPlayer.takeError(); !error.empty()) {
		failVideoLoad(error);
		return;
	}
	getIcon(IconType::Play).image = videoPlayer.playing() ? IconType::Pause : IconType::Play;
	const double now = getTimeNow();
	bool previewFrame = false;
	constexpr double maximumBufferDuration = 1.0;
	while (bufferedVideoFrames.empty() ||
		bufferedVideoFrames.back().frame->presentationTime -
		bufferedVideoFrames.front().frame->presentationTime < maximumBufferDuration) {
		auto frame = videoPlayer.takeFrame(&previewFrame);
		if (!frame) break;
		if (previewFrame) {
			const bool preserveDepthPresentation = activeVideo && display3D &&
				videoDepthProcessor.running() && videoDepthFrameLoaded;
			resetVideoPlaybackBuffer(false, !preserveDepthPresentation);
			videoPlaybackGeneration = frame->generation;
			presentVideoFrame(frame, true);
			if (preserveDepthPresentation)
				videoDepthProcessor.submit(frame);
			break;
		} else if (!videoPlayer.playing()) {
			// A normal pause may deliver one final decoded frame. Clear stale
			// presentation data, but retain the inferred depth texture so the
			// current frame remains in stereo while paused.
			resetVideoPlaybackBuffer(true, false);
			videoPlaybackGeneration = frame->generation;
			presentVideoFrame(frame, true);
			break;
		} else {
			if (frame->generation != videoPlaybackGeneration) {
				const bool holdForDepth = display3D && videoDepthProcessor.running() &&
					lastVideoFrame != nullptr;
				// A loop/seek starts a new media generation. Keep the previous
				// depth texture alive while the replacement generation is prepared.
				resetVideoPlaybackBuffer(false, !holdForDepth);
				videoPlaybackGeneration = frame->generation;
				if (holdForDepth)
					videoDepthTransitionGeneration = frame->generation;
			}
			if (videoBufferStartedAt < 0.0) videoBufferStartedAt = now;
			bufferedVideoFrames.push_back({frame, false});
			if (display3D && videoDepthProcessor.running())
				videoDepthProcessor.submit(frame);
		}
	}

	constexpr double targetBufferDuration = videoDepthLookaheadDuration;
	if (!videoPlaybackBufferReady && !bufferedVideoFrames.empty()) {
		const double bufferedDuration = bufferedVideoFrames.back().frame->presentationTime -
			bufferedVideoFrames.front().frame->presentationTime;
		const bool audioBuffered = !videoPlayer.hasAudio() ||
			(videoPlayer.audioReady() && videoPlayer.bufferedAudioDuration() >= 0.12);
		const bool startupTimeout = now - videoBufferStartedAt >= 1.0;
		if ((bufferedDuration >= targetBufferDuration && audioBuffered) || startupTimeout) {
			videoPlaybackBufferReady = true;
			videoPresentationMediaOrigin = bufferedVideoFrames.front().frame->presentationTime;
			videoPresentationWallOrigin = now + videoPlayer.audioDeviceLatency();
			videoPresentationClockValid = true;
			videoPlayer.setAudioBuffering(false);
		}
	}

	const bool playing = videoPlayer.playing();
	if (!playing) {
		videoPresentationClockValid = false;
	} else if (videoPlaybackBufferReady && !videoPresentationClockValid &&
		!bufferedVideoFrames.empty()) {
		videoPresentationMediaOrigin = bufferedVideoFrames.front().frame->presentationTime;
		videoPresentationWallOrigin = now + videoPlayer.audioDeviceLatency();
		videoPresentationClockValid = true;
	}
	if (playing && videoPlaybackBufferReady && videoPresentationClockValid &&
		now >= videoPresentationWallOrigin) {
		const double audioPosition = videoPlayer.audioPlaybackPosition();
		const double presentationTime = audioPosition >= 0.0 ? audioPosition :
			videoPresentationMediaOrigin + (now - videoPresentationWallOrigin);
		BufferedVideoFrame selected;
		auto canPresent = [](const std::shared_ptr<VideoFrame>& candidate) {
			return !display3D || videoDepthTransitionGeneration == 0 ||
				candidate->generation != videoDepthTransitionGeneration ||
				videoDepthGeneration == candidate->generation;
		};
		while (!bufferedVideoFrames.empty() &&
			bufferedVideoFrames.front().frame->presentationTime <= presentationTime + 0.001) {
			if (!canPresent(bufferedVideoFrames.front().frame)) break;
			selected = std::move(bufferedVideoFrames.front());
			bufferedVideoFrames.pop_front();
		}
		if (selected.frame != nullptr) {
			if (selected.frame->generation == videoDepthTransitionGeneration)
				videoDepthTransitionGeneration = 0;
			presentVideoFrame(selected.frame, selected.preview);
		}
	}
	updateVideoSlider();
	static std::shared_ptr<const VideoSubtitle> shownSubtitle;
	const auto subtitle = videoPlayer.subtitle();
	if (subtitle != shownSubtitle) {
		Image::updateVideoSubtitle(&context, subtitle);
		shownSubtitle = subtitle;
	}
	serviceVideoDepth();
}
auto preloadDir = 1;
static SDL_Thread* preloadThread = nullptr;
static AsyncData asyncData{};
static int preloadDepthIndex = -1;
static bool isSpeculativeDepth = false;
static int pendingPreloadNavigation = -1;
static bool preloadNavigationReady = false;
static bool navigationLoadingIndicator = false;
static constexpr double minimumSwitchTime2D = 0.125;
static constexpr double minimumSwitchTime3D = 0.250;
static constexpr double rapidBrowseIdleTime = 1.5;
static constexpr int rapidBrowseClickThreshold = 2;
static int rapidBrowseClicks = 0;
static bool rapidBrowseMode = false;
static bool rapidBrowseRestore3D = false;
static double lastRapidBrowseNavigation = 0.0;
static double getMinimumSwitchTime() {
	return display3D ? minimumSwitchTime3D : minimumSwitchTime2D;
}

static DepthEstimator nativeDepthEstimator;
static bool nativeDepthEstimatorLoaded = false;
static std::string nativeDepthEstimatorError;
static SuperResolution nativeSuperResolution;
static bool nativeSuperResolutionAttempted = false;
static bool nativeSuperResolutionLoaded = false;
static std::string nativeSuperResolutionError;
static std::mutex nativeSuperResolutionMutex;
static std::uint64_t nextDepthGeneration = 0;
static std::uint64_t activeDepthGeneration = 0;

std::filesystem::path exePath = std::filesystem::path(
	SDL_GetBasePath()).parent_path().parent_path();
std::filesystem::path homeDir = Core::getHomeDirectory();
std::filesystem::path homePath = homeDir / ".Rendepth";
static std::string modelDirectory;

static std::random_device randDevice;
static std::mt19937 randGen(randDevice());
static std::uniform_int_distribution randEffect(0, 1);
static std::uniform_int_distribution randImage(0);
static int nextRandIndex = -1;
static int currentRandIndex = -1;

enum GenMode {
	SINGLE_IMAGE = 0,
	BATCH_FOLDER = 1,
	REAL_TIME = 2,
	VIDEO_FRAME = 3
};

static glm::vec2 refreshWindowSize();
static void setMaximize(bool maximize = true);
static void toggleMaximized();
static void showCustomCursor(bool show);
static double getTimeNow() {
	return (double)SDL_GetTicks() / 1000.0;
}

static bool compileShadersForReload() {
	const std::filesystem::path executableDirectory = SDL_GetBasePath();
	std::filesystem::path shaderDirectory;
	for (auto directory = executableDirectory; !directory.empty(); directory = directory.parent_path()) {
		const auto candidate = directory / "Shaders";
		if (std::filesystem::exists(candidate / "compile_linux.sh") ||
			std::filesystem::exists(candidate / "compile_macos.sh") ||
			std::filesystem::exists(candidate / "compile_windows.bat")) {
			shaderDirectory = candidate;
			break;
		}
		const auto parent = directory.parent_path();
		if (parent == directory) break;
	}
	if (shaderDirectory.empty()) {
		SDL_Log("Shader reload failed: no shader script directory found.");
		return false;
	}

	std::string scriptName;
	#ifdef _WIN32
		scriptName = "compile_windows.bat";
	#elif defined(__APPLE__)
		scriptName = "compile_macos.sh";
	#else
		scriptName = "compile_linux.sh";
	#endif
	#ifdef _WIN32
		const std::string command = "cd /d \"" + shaderDirectory.string() +
			"\" && \"" + scriptName + "\"";
	#else
		const std::string command = "cd \"" + shaderDirectory.string() +
			"\" && ./" + scriptName;
	#endif
	SDL_Log("Compiling shaders from GLSL: %s", command.c_str());
	const int result = std::system(command.c_str());
	if (result != 0) {
		SDL_Log("Shader compilation failed with status %d.", result);
		return false;
	}
	return true;
}

static std::string formatFileSize(std::uintmax_t size) {
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	int unitIndex = 0;

	while (size >= 1024 && unitIndex < 4) {
		size /= 1024;
		++unitIndex;
	}

	return std::to_string(size).substr(0, 5) + units[unitIndex];
}
static void callDepthGen(int imageIndex, bool speculative = false,
	SDL_Surface* inputSurface = nullptr);

int previousFileIndex() {
	if (fileList.empty() || fileIndex < 0) return -1;
	const auto currentType = getMediaType(fileList[fileIndex].link);
	for (int offset = 1; offset <= static_cast<int>(fileList.size()); ++offset) {
		const int index = (fileIndex - offset + static_cast<int>(fileList.size())) %
			static_cast<int>(fileList.size());
		if (getMediaType(fileList[index].link) == currentType) return index;
	}
	return fileIndex;
}

int nextFileIndex() {
	if (fileList.empty() || fileIndex < 0) return -1;
	const auto currentType = getMediaType(fileList[fileIndex].link);
	for (int offset = 1; offset <= static_cast<int>(fileList.size()); ++offset) {
		const int index = (fileIndex + offset) % static_cast<int>(fileList.size());
		if (getMediaType(fileList[index].link) == currentType) return index;
	}
	return fileIndex;
}

static void setSlideshow(bool slide);
static void cancelSlideshow();
static Icon& getIcon(IconType type);
static void setDisplay3D(bool display);
static int loadImage(void* ptr = nullptr);

static void resetRapidBrowseState() {
	rapidBrowseClicks = 0;
	rapidBrowseMode = false;
	rapidBrowseRestore3D = false;
	lastRapidBrowseNavigation = 0.0;
}

static void cancelPendingWorkForRapidBrowse() {
	activeDepthGeneration = ++nextDepthGeneration;
	nativeDepthEstimator.cancel();
	isConverting = false;
	isSpeculativeDepth = false;
	preloadDepthIndex = -1;
	justConverted = false;
	keep3DForDepthReload = false;
	navigationLoadingIndicator = false;

	if (doingPreload) {
		endPreload(false);
	}
	pendingPreloadNavigation = -1;
	preloadNavigationReady = false;
}

void gotoPreviousImage(bool seekActiveVideo = true) {
	if (activeScreenCapture) return;
	if (seekActiveVideo && skipVideoBy(-15.0)) return;
	if (fileList.empty()) return;

	if (rapidBrowseMode) {
		lastRapidBrowseNavigation = getTimeNow();
		lastSwitchTime = getTimeNow();
		context.offset = { 0, 0 };
		fileIndex = previousFileIndex();
		loadImage(nullptr);
		return;
	}

	if (isConverting) {
		if (++rapidBrowseClicks >= rapidBrowseClickThreshold) {
			cancelPendingWorkForRapidBrowse();
			rapidBrowseRestore3D = true;
			rapidBrowseMode = true;
			setDisplay3D(false);
			lastRapidBrowseNavigation = getTimeNow();
			lastSwitchTime = getTimeNow();
			context.offset = { 0, 0 };
			fileIndex = previousFileIndex();
			loadImage(nullptr);
		}
		return;
	}

	if (justConverted) return;
	if (pendingPreloadNavigation >= 0) return;
	if (doingFileOp) return;
	if (context.loading && !rapidBrowseMode) return;
	if (!rapidBrowseMode && lastSwitchTime > 0.0 && getTimeNow() - lastSwitchTime < getMinimumSwitchTime()) return;
	if (isPlayingSlideshow) cancelSlideshow();

	preloadDir = -1;
	auto previousIndex = previousFileIndex();
	if (previousIndex == fileIndex) return;
	if (doingPreload) {
		if (asyncData.fileIndex == previousIndex) {
			pendingPreloadNavigation = previousIndex;
		}
		return;
	}
	navigationLoadingIndicator = true;
	if (display3D) {
		if (fileList[previousIndex].type == Color_Only &&
			!isSupportedMedia(fileList[previousIndex].link)) {
			lastSwitchTime = getTimeNow();
			fileIndex = previousIndex;
			SDL_DestroySurface(fileList[fileIndex].preload);
			fileList[fileIndex].preload = nullptr;
			rapidBrowseClicks = 1;
			callDepthGen(previousIndex);
			return;
		}
	}
	context.gotoPrev = true;
	switchedImage = true;
}

void gotoNextImage(bool seekActiveVideo = true) {
	if (activeScreenCapture) return;
	if (seekActiveVideo && skipVideoBy(15.0)) return;
	if (fileList.empty()) return;

	if (rapidBrowseMode) {
		lastRapidBrowseNavigation = getTimeNow();
		lastSwitchTime = getTimeNow();
		context.offset = { 0, 0 };
		fileIndex = nextFileIndex();
		loadImage(nullptr);
		return;
	}

	if (isConverting) {
		if (++rapidBrowseClicks >= rapidBrowseClickThreshold) {
			cancelPendingWorkForRapidBrowse();
			rapidBrowseRestore3D = true;
			rapidBrowseMode = true;
			setDisplay3D(false);
			lastRapidBrowseNavigation = getTimeNow();
			lastSwitchTime = getTimeNow();
			context.offset = { 0, 0 };
			fileIndex = nextFileIndex();
			loadImage(nullptr);
		}
		return;
	}

	if (justConverted) return;
	if (pendingPreloadNavigation >= 0) return;
	if (doingFileOp) return;
	if (context.loading && !rapidBrowseMode) return;
	if (!rapidBrowseMode && lastSwitchTime > 0.0 && getTimeNow() - lastSwitchTime < getMinimumSwitchTime()) return;
	if (isPlayingSlideshow) cancelSlideshow();

	preloadDir = 1;
	auto nextIndex = nextFileIndex();
	if (nextIndex == fileIndex) return;
	if (doingPreload) {
		if (asyncData.fileIndex == nextIndex) {
			pendingPreloadNavigation = nextIndex;
		}
		return;
	}
	navigationLoadingIndicator = true;
	if (display3D) {
		if (fileList[nextIndex].type == Color_Only &&
			!isSupportedMedia(fileList[nextIndex].link)) {
			lastSwitchTime = getTimeNow();
			fileIndex = nextIndex;
			SDL_DestroySurface(fileList[fileIndex].preload);
			fileList[fileIndex].preload = nullptr;
			rapidBrowseClicks = 1;
			callDepthGen(nextIndex);
			return;
		}
	}
	context.gotoNext = true;
	switchedImage = true;
}

bool isStereoImage(StereoFormat format) {
	return !(format == Color_Only || format == Color_Plus_Depth || format == Unknown_Format);
}

int getRandImageIndex() {
	auto randIndex = randImage(randGen);
	auto randAttempts = 64;
	while ((randIndex == fileIndex || randIndex == nextRandIndex ||
			isStereoImage(fileList[randIndex].type) || getMediaType(fileList[randIndex].link) != MediaType::Image) && --randAttempts > 0) {
		randIndex = randImage(randGen);
	}
	return randIndex;
}

void gotoRandomImage() {
	if (fileList.empty()) return;

	if (rapidBrowseMode) {
		lastRapidBrowseNavigation = getTimeNow();
		lastSwitchTime = getTimeNow();
		context.offset = { 0, 0 };
		auto randIndex = nextRandIndex;
		if (randIndex < 0) randIndex = getRandImageIndex();
		nextRandIndex = getRandImageIndex();
		fileIndex = randIndex;
		loadImage(nullptr);
		return;
	}

	if (isConverting) {
		if (++rapidBrowseClicks >= rapidBrowseClickThreshold) {
			cancelPendingWorkForRapidBrowse();
			rapidBrowseRestore3D = true;
			rapidBrowseMode = true;
			setDisplay3D(false);
			lastRapidBrowseNavigation = getTimeNow();
			lastSwitchTime = getTimeNow();
			context.offset = { 0, 0 };
			auto randIndex = nextRandIndex;
			if (randIndex < 0) randIndex = getRandImageIndex();
			nextRandIndex = getRandImageIndex();
			fileIndex = randIndex;
			loadImage(nullptr);
		}
		return;
	}
	if (doingFileOp) return;
	if (context.loading && !rapidBrowseMode) return;
	if (!rapidBrowseMode && lastSwitchTime > 0.0 && getTimeNow() - lastSwitchTime < getMinimumSwitchTime()) return;
	if (fileList.empty()) return;
	auto randIndex = nextRandIndex;
	if (randIndex < 0) {
		randIndex = getRandImageIndex();
	}
	nextRandIndex = getRandImageIndex();
	preloadDir = 0;

	if (display3D || preferredStereoMode == Depth_Zoom) {
		if (fileList[randIndex].type == Color_Only &&
			getMediaType(fileList[randIndex].link) == MediaType::Image) {
			lastSwitchTime = getTimeNow();
			fileIndex = randIndex;
			SDL_DestroySurface(fileList[randIndex].preload);
			fileList[randIndex].preload = nullptr;
			rapidBrowseClicks = 1;
			callDepthGen(randIndex);
			return;
		}
	}
	currentRandIndex = randIndex;
	context.gotoRand = true;
	switchedImage = true;
}

static void cancelSlideshow() {
	if (preferredStereoMode == Depth_Zoom) {
		preferredStereoMode = defaultStereoMode;
	}
	setSlideshow(false);
}

static void setDisplay3D(bool display);
static void setStereoMode(ViewMode mode);
static void refreshDisplay3D(StereoFormat type);
static void toggleFullscreen();
static void toggleScreenCapture();
static void toggleSlideshow();
static void openFile();
static void openFolder();
static void saveFile();
static void saveOptions();
static void loadOptions();
static void toggleOptions();
static void toggleStereo();
static void toggleStereoSettings();
static void parseFileList(const std::vector<std::string>& filesToLoad);
static int callDepthGenOnce(const std::string& fileFolderPath, int genMode, int imageId = -1,
	SDL_Surface* inputSurface = nullptr);
static int loadAudioDepthImage();
static int loadImage(void* ptr);
static void conversionCompleted(const char* path, int imageId, std::uint64_t generation);
static int nativeDepthRun(void* ptr);
static void serviceNativeGpuUpscale();
static void deferMediaLoad(const std::vector<std::string>& files);
static void serviceDeferredMediaLoad();
static void serviceDeferredDepthReload();
static void serviceDepthCompletions();

struct NativeGpuUpscaleRequest;
static std::mutex nativeGpuUpscaleMutex;
static std::condition_variable nativeGpuUpscaleCondition;
static NativeGpuUpscaleRequest* pendingNativeGpuUpscale = nullptr;

static auto actionSize = 16.0;
static auto actionMargin = 5.0;
static glm::vec2 sizeStandard { style.getIconRadius(Style::getCurrentScale()), style.getIconRadius(Style::getCurrentScale()) };
static glm::vec2 positionZero { 0.0, 0.0 };
static float positionEdge = style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale());
static float positionEdgeLarge = style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale()) * 2.0f;
static glm::vec2 alignCenter { 0.5, 0.5 };
static glm::vec2 alignRightTop { 1.0, 0.0 };
static glm::vec2 alignLeftBottom { 0.0, 1.0 };
static glm::vec2 alignCenterBottom { 0.5, 1.0 };
static glm::vec2 alignLeftCenter { 0.0, 0.5 };
static glm::vec2 alignRightCenter { 1.0, 0.5 };
static glm::vec4 areaTopLeft { 0.0, 0.0, 0.0, 0.0 };
static glm::vec4 areaTopLeftImage { 0.0, style.getInfo(Style::getCurrentScale()), 0.0, 0.0 };
static glm::vec4 areaBottomRight { 0.0, -style.getInfo(Style::getCurrentScale()), 1.0, 1.0 };
static glm::vec4 areaBottomRightFull { 0.0, 0.0, 1.0, 1.0 };
static glm::vec4 areaTopBar { 0.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0 };
static glm::vec4 areaLeftSide { positionEdge * 4.0, -style.getInfo(Style::getCurrentScale()), 0.0, 1.0 };
static glm::vec4 areaRightSide { -positionEdge * 4.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0 };

static void updateButtonCanvasSizes() {
	sizeStandard = { style.getIconRadius(Style::getCurrentScale()), style.getIconRadius(Style::getCurrentScale()) };
	positionEdge = style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale());
	positionEdgeLarge = style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale()) * 2.0f;
	areaTopLeftImage = { 0.0, style.getInfo(Style::getCurrentScale()), 0.0, 0.0 };
	areaBottomRight = { 0.0, -style.getInfo(Style::getCurrentScale()), 1.0, 1.0 };
	areaTopBar = { 0.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0 };
	areaLeftSide = { positionEdge * 4.0, -style.getInfo(Style::getCurrentScale()), 0.0, 1.0 };
	areaRightSide = { -positionEdge * 4.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0 };
}

Icon IconLogo = {
	IconType::Logo_White,
	IconType::Logo_White,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Rendepth",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			positionZero,
			alignCenter, areaTopLeft, areaBottomRight };
	},
	[]() {},
	1.0,
	true,
	false
};

Icon IconBack = {
	IconType::Back,
	IconType::Back,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Previous Image",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			{ positionEdge, 0.0 },
			alignLeftCenter, areaTopLeftImage, areaLeftSide };},
	[]() { gotoPreviousImage(false); },
	1.0,
	true,
	false
};

Icon IconForward = {
	IconType::Forward,
	IconType::Forward,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Next Image",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			{ -positionEdge, 0.0 },
			alignRightCenter, areaRightSide, areaBottomRight };
	},
	[]() { gotoNextImage(false); },
	1.0,
	true,
	false
};

Icon IconStereo3D = {
	IconType::Stereo_3D,
	IconType::Stereo_3D,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle 2D/3D",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
		{ 0.0, -positionEdgeLarge },
		alignCenterBottom,
		{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
		areaBottomRightFull };
	},
	[]() {
		toggleStereo();
	},
	1.0,
	false,
	false
};

Icon IconLoading = {
	IconType::Loading,
	IconType::Loading,
	IconGroup::None,
	IconMode::Button,
	IconState::Over,
	"Loading...",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
		{ 0.0, 0.0 },
		alignCenter, areaTopLeft, areaBottomRight };
	},
	[]() {},
	0.0,
	false,
	false
};

Icon IconMinimize = {
	IconType::Minimize,
	IconType::Minimize,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Minimize Window",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
{ -(positionEdge + style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale()) +
	style.getIconSpacer(Style::getCurrentScale()) * 2.0),
	style.getIconRadius(Style::getCurrentScale()) + style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		SDL_MinimizeWindow(context.window);
	},
	1.0,
	false,
	false
};

Icon IconFullscreen = {
	IconType::Fullscreen,
	IconType::Fullscreen,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle Full Screen",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
	{ -(positionEdge + style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale()) +
		style.getIconSpacer(Style::getCurrentScale()) * 2.0),
		style.getIconRadius(Style::getCurrentScale()) * 3.0 +
		style.getIconSpacer(Style::getCurrentScale()) * 2.0 +
		style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		if (isConverting) return;
		toggleFullscreen();
	},
	1.0,
	false,
	false
};

Icon IconScreenCapture = {
	IconType::Crop,
	IconType::Crop,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Capture Screen",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			{ -positionEdge, style.getIconRadius(Style::getCurrentScale()) * 3.0 +
				style.getIconSpacer(Style::getCurrentScale()) * 2.0 +
				style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		if (isConverting) return;
		toggleScreenCapture();
	},
	1.0,
	false,
	false
};

Icon IconClose = {
	IconType::Close,
	IconType::Close,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Close Application",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
		{ -positionEdge,
			style.getIconRadius(Style::getCurrentScale()) + style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		quitAppNextFrame = true;
	},
	1.0,
	false,
	false
};

Icon IconHelp = {
	IconType::Info,
	IconType::Info,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle Tooltips",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
	{ -positionEdge, style.getIconRadius(Style::getCurrentScale()) * 3.0 +
		style.getIconSpacer(Style::getCurrentScale()) * 2.0 +
		style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		displayInfoEnabled = !displayInfoEnabled;
		saveOptions();
	},
	1.0,
	false,
	false
};

Icon IconPlay = {
	IconType::Play,
	IconType::Play,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle Slideshow",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
				{ -positionEdge, -positionEdgeLarge },
				{ 1.0, 1.0 },
				{-positionEdge * 3.0, -positionEdgeLarge * 2.0, 1.0, 1.0},
			areaBottomRight };
	},
	[]() {
		toggleSlideshow();
	},
	0.0,
	false,
	false
};

static auto zoomMin = 1.00;
static auto zoomMax = 4.31;

Icon IconOpen = {
	IconType::File,
	IconType::File,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Open 2D/3D Image",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
		{ positionEdge, style.getIconRadius(Style::getCurrentScale()) + style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale())},
		{ 0.0, 0.0 },
		{0.0, style.getInfo(Style::getCurrentScale()), 0.0, 0.0},
	{positionEdge * 3.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
		0.0, 0.0}};
	},
	[]() {
		openFile();
	},
	1.0,
	true,
	false
};

Icon IconSave = {
	IconType::Save,
	IconType::Save,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Export 3D Image",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
	{ (positionEdge + style.getIconRadius(Style::getCurrentScale()) + style.getIconGutter(Style::getCurrentScale()) +
		style.getIconSpacer(Style::getCurrentScale()) * 2.0), style.getIconRadius(Style::getCurrentScale()) + style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale()) },
		{ 0.0, 0.0 },
		{0.0, style.getInfo(Style::getCurrentScale()), 0.0, 0.0},
	{positionEdge * 3.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
		0.0, 0.0}};
	},
	[]() {
		saveFile();
	},
	1.0,
	false,
	false
};

Icon IconBatch = {
	IconType::Folder,
	IconType::Folder,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Convert Folder to RGBD",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
	{ positionEdge, style.getIconRadius(Style::getCurrentScale()) * 3.0 + style.getIconSpacer(Style::getCurrentScale()) * 2.0 + style.getInfo(Style::getCurrentScale()) },
		{ 0.0, 0.0 },
		{0.0, style.getInfo(Style::getCurrentScale()), 0.0, 0.0},
	{positionEdge * 3.0, positionEdge * 3.0 + style.getInfo(Style::getCurrentScale()),
		0.0, 0.0}};
	},
	[]() {
		openFolder();
	},
	1.0,
	false,
	false
};

Icon IconOptions = {
	IconType::Options,
	IconType::Options,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Show Options",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			{ positionEdge, -positionEdgeLarge },
			alignLeftBottom,
			{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
		areaLeftSide };
	},
	[]() {
		toggleOptions();
	},
	1.0,
	false,
	false
};

Icon IconSettings = {
	IconType::Settings,
	IconType::Settings,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle 3D Settings",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
			{ (positionEdge + style.getIconRadius(Style::getCurrentScale()) +
				style.getIconGutter(Style::getCurrentScale()) +
				style.getIconSpacer(Style::getCurrentScale()) * 2.0), -positionEdgeLarge },
		alignLeftBottom,
			{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
		areaLeftSide };
	},
	[]() {
		toggleStereoSettings();
	},
	1.0,
	false,
	false
};

Choice ChoiceStereo {
	"3D Mode",
	{ "Natural Color", "Vivid Color", "SBS Full", "SBS Half", "Color + Depth",
		"Horizontal", "Vertical", "Checkerboard", "Free View", "Disabled" },
};

Choice ChoiceExport {
	"Export Format",
	{ "Anaglyph", "Color + Depth", "SBS Full", "SBS Half",
		"Free View", "Free View LRL", "Light Field LKG" },
};

Choice ChoiceModel {
	"Depth Conversion",
	{ "Performance", "Balanced", "Quality" },
};

Choice ChoiceResolution {
	"Upscale Resolution",
	{ "Full HD", "Quad HD", "Ultra HD" },
};

Choice ChoiceBackground {
	"Background Color",
	{ "Blur", "Solid", "Light", "Dark" },
};

Choice ChoiceSorting {
	"Sort Order",
	{"A-Z", "Z-A", "Newest", "Oldest" },
};

Choice ChoiceSlideshow {
	"Slideshow Delay",
	{ "8", "10", "12", "14" },
};

Choice ChoiceTags {
	"Default 3D Detection",
	{ "SBS Full", "SBS Half", "TAB Full", "TAB Half", "Color Only", "Anaglyph" },
};

Choice ChoiceEyes {
	"Swap Eyes",
	{ "Left / Right", "Right / Left" },
};

static std::vector menuChoices = { ChoiceStereo, ChoiceExport, ChoiceModel, ChoiceResolution,
	ChoiceBackground, ChoiceSorting, ChoiceSlideshow,ChoiceEyes, ChoiceTags  };

static std::unordered_map<std::string, int> menuSelection = {
	{ ChoiceStereo.label, 0 },
	{ ChoiceExport.label, 0 },
	{ ChoiceModel.label, 0 },
	{ ChoiceResolution.label, 0 },
	{ ChoiceBackground.label, 0 },
	{ ChoiceSorting.label, 0 },
	{ ChoiceSlideshow.label, 0 },
	{ ChoiceTags.label, 4 },
	{ ChoiceEyes.label, 0 },
};

static std::unordered_map<std::string, int> menuRollover = {
	{ ChoiceStereo.label, -1 },
	{ ChoiceExport.label, -1 },
	{ ChoiceModel.label, -1 },
	{ ChoiceResolution.label, -1 },
	{ ChoiceBackground.label, -1 },
	{ ChoiceSorting.label, -1 },
	{ ChoiceSlideshow.label, -1 },
	{ ChoiceTags.label, -1 },
	{ ChoiceEyes.label, -1 },
};

static void setPreferredStereo(ViewMode mode, bool saveMode = true);
static void setShowStereoSettings(bool show);
static std::array stereoModes = {
	Anaglyph_Accurate, Anaglyph_Vivid, SBS_Full, SBS_Half, RGB_Depth,
	Horizontal, Vertical, Checkerboard, Free_View_Grid, Mono };
static void changeStereo(int option) {
	if (stereoModes[option] == Mono) setDisplay3D(false);
	setPreferredStereo(stereoModes[option], true);
	Image::saveMenuLayout(&context);
	setShowStereoSettings(false);
	checkMouseState();
}

static std::array exportFormats = {
	Color_Anaglyph, Color_Plus_Depth, Side_By_Side_Full, Side_By_Side_Half,
	Stereo_Free_View_Grid, Stereo_Free_View_LRL, Light_Field_LKG };
static std::array exportTags = { "anaglyph",  "rgbd", "sbs", "sbs_half_width",
	"free_view", "free_view_lrl", "qs" };
static const std::string& getExportDisplayName();
static void changeExport(int option) {
	option = std::clamp(option, 0, static_cast<int>(exportFormats.size()) - 1);
	exportFormat = exportFormats[option];
	exportTag = exportTags[option];
	checkMouseState();
}

static const std::string& getExportDisplayName() {
	const auto format = std::find(exportFormats.begin(), exportFormats.end(), exportFormat);
	if (format != exportFormats.end()) {
		const auto index = static_cast<size_t>(format - exportFormats.begin());
		return ChoiceExport.options[index];
	}
	return ChoiceExport.options.front();
}

static std::string removeFileTags(const std::string& fileName) {
	std::string tagPattern = "(";
	for (auto& tag : tagType) {
		if (tag.second != Side_By_Side_Swap)
			tagPattern += tag.first + "|";
	}
	tagPattern.pop_back();
	tagPattern += ")";
	std::regex pattern(tagPattern);
	return std::regex_replace(fileName, pattern, "");
}

static bool addStereoTag(const std::string& link, const std::string& tag) {
	std::string cleanLink = removeFileTags(link);
	auto dotPos = cleanLink.find_last_of('.');
	auto baseName = cleanLink.substr(0, dotPos);
	auto ext = cleanLink.substr(dotPos);
	std::string addTag;
	if (!tag.empty()) addTag = "_" + tag;
	auto tagLink = baseName + addTag + ext;
	auto success = SDL_RenamePath(link.c_str(), tagLink.c_str());
	parseFileList({ tagLink });
	loadImage(nullptr);
	return success;
}

static std::array importTags = { Side_By_Side_Full, Side_By_Side_Half,
	Top_And_Bottom_Full, Top_And_Bottom_Half, Color_Only, Color_Anaglyph };
static void changeImport(int option, bool init) {
	Core::defaultImportFormat = importTags[option];
	if (!init && Core::defaultImportFormat == Color_Only) {
		// Changing the fallback to color-only must leave 3D before reparsing the
		// current file. Otherwise an untagged file is reclassified as Color_Only
		// while display3D is still true and is accidentally sent to conversion.
		setDisplay3D(false);
	}
	if (!init && !fileList.empty()) {
		const bool restoreVideoPosition = activeVideo;
		const double videoPosition = restoreVideoPosition ? videoPlayer.position() : 0.0;
		const bool videoWasPlaying = restoreVideoPosition && videoPlayer.playing();
		parseFileList({ fileList[fileIndex].link });
		if (Core::defaultImportFormat == Color_Only && !fileList.empty())
			refreshDisplay3D(fileList[fileIndex].type);
		loadImage(nullptr);
		if (restoreVideoPosition && activeVideo) {
			videoPlayer.setPlaying(videoWasPlaying);
			videoPlayer.seek(videoPosition, !videoWasPlaying);
			// seek() starts a new decoder generation. Keep the frame and depth
			// transition state aligned with that generation after the reload.
			resetVideoPlaybackBuffer(false, false);
			if (videoFileTransitionNeedsDepth)
				videoDepthTransitionGeneration = videoPlaybackGeneration;
		}
	}
}

static std::array eyesFormats = {
	Left_Right, Right_Left };
static void changeEyes(int option) {
	eyesFormat = eyesFormats[option];
	swapLeftRight = eyesFormat == Right_Left;
}

static void endPreload(bool success);
static auto depthRegenerated = false;
static void waitForDepthThread() {
	if (depthGenThread != nullptr) {
		while (depthGenAlive.load(std::memory_order_acquire)) {
			serviceNativeGpuUpscale();
			SDL_Delay(1);
		}
		SDL_WaitThread(depthGenThread, nullptr);
		depthGenThread = nullptr;
	}
}

static void resetDepthGeneration() {
	// Invalidate any completion that may already be queued by the worker. The
	// current conversion is still joined below, but its result must not restore
	// an image after the user changes the depth settings.
	activeDepthGeneration = ++nextDepthGeneration;
	isSpeculativeDepth = false;
	nativeDepthEstimator.cancel();
	waitForDepthThread();
	depthRegenerated = true;
	endPreload(false);
	for (auto& file : fileList) {
		if (Core::getImageType(file.link) == Color_Only ||
			(!isSupportedMedia(file.link) && file.path != file.link)) {
			file.path = file.link;
			file.type = Color_Only;
			SDL_DestroySurface(file.preload);
			file.preload = nullptr;
		}
	}
}

static std::array<std::string, 3> depthQuality = { "0", "1", "2" };
static std::array<std::string, 3> depthSizes = { "560", "644", "714" };
static std::array<std::string, 3> depthModelFiles = {
	"DA2-SMALL-560.onnx",
	"DA2-BASE-644.onnx",
	"DA2-LARGE-714.onnx"
};
static std::array<int, 3> depthProcessSizes = { 560, 644, 714 };
static std::array<std::string, 3> videoDepthModelFiles = {
	"DA2-SMALL-280.onnx",
	"DA2-SMALL-336.onnx",
	"DA2-SMALL-392.onnx"
};
static std::array<int, 3> videoDepthProcessSizes = { 280, 336, 392 };
static constexpr std::array<double, 3> videoDepthRates = { 20.0, 15.0, 12.0 };

static void setVideoDepthFallbackMode() {
	if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half ||
		preferredStereoMode == Anaglyph_Accurate || preferredStereoMode == Anaglyph_Vivid)
		setStereoMode(preferredStereoMode);
	else
		setStereoMode(Native);
}

static void stopVideoDepth(bool disable3D, bool clearTexture) {
	videoPlayer.setInferenceSize(0);
	videoDepthProcessor.stop();
	bufferedVideoDepthFrames.clear();
	videoDepthBlend.clear();
	videoDepthTransitionGeneration = 0;
	if (clearTexture) {
		Image::clearVideoDepth(&context);
		videoDepthFrameLoaded = false;
		videoDepthGeneration = 0;
	}
	videoFileTransitionActive = false;
	videoFileTransitionNeedsDepth = false;
	activeVideoDepthModelOption = -1;
	videoDepthRestartPending = false;
	if (clearTexture) {
		if (activeScreenCapture)
			context.imageType = Color_Only;
		else if (activeVideo && !fileList.empty())
			context.imageType = fileList[fileIndex].type;
	}
	if (disable3D) {
		setDisplay3D(false);
		if (activeVideo) setStereoMode(Native);
	}
}

static bool startVideoDepth(bool preserveTexture) {
	if ((!activeVideo && !activeScreenCapture) ||
		(activeVideo && (!videoPlayer.ready() || fileList.empty()))) return false;
	if (activeVideo && videoPlayer.audioOnly()) return false;
	if (activeVideo && fileList[fileIndex].type != Color_Only) {
		// Only untagged mono video can use inferred depth. Tagged native-stereo
		// and already-converted RGB-D video must never start this model, even if
		// a stale toggle or model-restart request reaches this function.
		stopVideoDepth(false);
		return false;
	}
	stopVideoDepth(false, !preserveTexture);
	const int modelOption = std::clamp(menuSelection[ChoiceModel.label], 0,
		static_cast<int>(videoDepthModelFiles.size()) - 1);
	VideoDepthProcessor::Config config;
	config.modelDirectory = modelDirectory.empty()
		? homePath / "Models" : std::filesystem::path(modelDirectory);
	config.modelFilename = videoDepthModelFiles[modelOption];
	config.processSize = videoDepthProcessSizes[modelOption];
	config.targetFramesPerSecond = videoDepthRates[modelOption];
	#ifdef RENDEPTH_ENABLE_CUDA
	config.provider = DepthEstimator::Provider::CUDA;
	#elif defined(RENDEPTH_ENABLE_ROCM)
	config.provider = DepthEstimator::Provider::ROCM;
	#endif
	if (activeVideo) videoPlayer.setInferenceSize(config.processSize, config.targetFramesPerSecond);
	if (!videoDepthProcessor.start(config)) {
		videoPlayer.setInferenceSize(0);
		return false;
	}
	activeVideoDepthModelOption = modelOption;
	if (!preserveTexture || !videoDepthFrameLoaded) {
		context.imageType = activeScreenCapture ? Color_Only : fileList[fileIndex].type;
		setDisplay3D(true);
		setVideoDepthFallbackMode();
	} else {
		context.imageType = Color_Plus_Depth;
		setDisplay3D(true);
		refreshDisplay3D(Color_Plus_Depth);
	}
	Core::drawText(&context, "Loading Video Depth Model", Image::helpFont,
		Image::helpTexture, Image::helpTextSize, "Help Texture");
	Image::displayTip = true;
	displayTipTime = getTimeNow();
	if (lastVideoFrame != nullptr &&
		lastVideoFrame->generation == videoPlayer.generation())
		videoDepthProcessor.submit(lastVideoFrame);
	if (activeVideo && !videoPlayer.playing()) videoPlayer.seek(videoPlayer.position(), false);
	return true;
}

static void sampleVideoDepthBlend(double now) {
	if (!videoDepthBlend.active) return;
	constexpr double blendDuration = 0.05;
	const float blend = static_cast<float>(std::clamp(
		(now - videoDepthBlend.startTime) / blendDuration, 0.0, 1.0));
	if (blend >= 1.0f) {
		videoDepthBlend.displayed = std::move(videoDepthBlend.target);
		videoDepthBlend.source.clear();
		videoDepthBlend.active = false;
		return;
	}
	for (size_t index = 0; index < videoDepthBlend.displayed.size(); ++index) {
		const float source = videoDepthBlend.source[index];
		const float target = videoDepthBlend.target[index];
		videoDepthBlend.displayed[index] = static_cast<std::uint16_t>(
			std::lround(source + (target - source) * blend));
	}
}

static void serviceVideoDepth() {
	if (!activeVideo && !activeScreenCapture) return;
	if (activeVideo && (fileList.empty() || fileList[fileIndex].type != Color_Only)) {
		if (videoDepthProcessor.running() || videoDepthFrameLoaded)
			stopVideoDepth(false);
		return;
	}
	if (const auto error = videoDepthProcessor.takeError(); !error.empty()) {
		SDL_Log("Video depth inference failed: %s", error.c_str());
		stopVideoDepth();
		Core::drawText(&context, "Could Not Generate Video Depth", Image::helpFont,
			Image::helpTexture, Image::helpTextSize, "Help Texture");
		Image::displayTip = true;
		displayTipTime = getTimeNow();
		return;
	}
	auto completed = videoDepthProcessor.takeFrame();
	if (completed != nullptr && completed->valid() &&
		(activeScreenCapture || completed->generation == videoPlayer.generation())) {
		bufferedVideoDepthFrames.push_back(std::move(completed));
		while (bufferedVideoDepthFrames.size() > 24)
			bufferedVideoDepthFrames.pop_front();
	}

	std::unique_ptr<VideoDepthFrame> selected;
	std::shared_ptr<VideoFrame> depthReference = lastVideoFrame;
	if (videoDepthTransitionGeneration != 0) {
		// The old color frame is intentionally still displayed. Use the first
		// buffered frame from the new generation as the depth worker's target so
		// its result can be uploaded before the color frame is released.
		depthReference.reset();
		for (const auto& buffered : bufferedVideoFrames) {
			if (buffered.frame->generation == videoDepthTransitionGeneration) {
				depthReference = buffered.frame;
				break;
			}
			if (buffered.frame->generation > videoDepthTransitionGeneration) break;
		}
	}
	if (depthReference != nullptr) {
		// Only use depth at or immediately before the displayed color frame. A
		// future depth map can make moving silhouettes visibly misregister.
		const double selectionTime = depthReference->presentationTime + 0.001;
		const bool transitionDepth = videoDepthTransitionGeneration != 0 &&
			depthReference->generation == videoDepthTransitionGeneration;
		while (!bufferedVideoDepthFrames.empty() &&
			bufferedVideoDepthFrames.front()->generation < depthReference->generation)
			bufferedVideoDepthFrames.pop_front();
		while (!bufferedVideoDepthFrames.empty() &&
			bufferedVideoDepthFrames.front()->generation == depthReference->generation &&
			(transitionDepth || bufferedVideoDepthFrames.front()->presentationTime <= selectionTime)) {
			selected = std::move(bufferedVideoDepthFrames.front());
			bufferedVideoDepthFrames.pop_front();
		}
	}

	constexpr double uploadInterval = 1.0 / 60.0;
	const double now = getTimeNow();
	bool upload = false;
	if (selected != nullptr) {
		const double selectedPresentationTime = selected->presentationTime;
		const bool compatible = !videoDepthBlend.displayed.empty() &&
			videoDepthBlend.width == selected->width &&
			videoDepthBlend.height == selected->height &&
			videoDepthBlend.displayed.size() == selected->values.size();
		if (!compatible) {
			videoDepthBlend.clear();
			videoDepthBlend.displayed = std::move(selected->values);
			videoDepthBlend.width = selected->width;
			videoDepthBlend.height = selected->height;
		} else {
			sampleVideoDepthBlend(now);
			videoDepthBlend.source = videoDepthBlend.displayed;
			videoDepthBlend.target = std::move(selected->values);
			videoDepthBlend.startTime = now - uploadInterval;
			videoDepthBlend.active = true;
			sampleVideoDepthBlend(now);
		}
		videoDepthBlend.presentationTime = selectedPresentationTime;
		videoDepthGeneration = selected->generation;
		upload = true;
	} else if (videoDepthBlend.active &&
		(now - videoDepthBlend.lastUploadTime >= uploadInterval)) {
		sampleVideoDepthBlend(now);
		upload = true;
	}
	if (!upload) return;
	if (Image::updateVideoDepth(&context, videoDepthBlend.displayed,
			videoDepthBlend.width, videoDepthBlend.height) != 0) {
		SDL_Log("Could not upload inferred video depth: %s", SDL_GetError());
		return;
	}
	videoDepthBlend.lastUploadTime = now;
	const bool deferVideoFileTransition = activeVideo && videoFileTransitionActive &&
		videoDepthTransitionGeneration != 0;
	if (!videoDepthFrameLoaded) {
		SDL_Log("Video depth stream ready: %dx%d at %.3f seconds.",
			videoDepthBlend.width, videoDepthBlend.height,
			videoDepthBlend.presentationTime);
		videoDepthFrameLoaded = true;
		if (!deferVideoFileTransition) {
			checkMouseState();
			context.imageType = Color_Plus_Depth;
			// Capture has no file-backed source type to refresh from. Once the first
			// inferred map is uploaded, make the capture source explicitly RGB-D and
			// keep the requested 3D view enabled.
			if (activeScreenCapture && !display3D) setDisplay3D(true);
			if (display3D) refreshDisplay3D(Color_Plus_Depth);
		}
	}
	if (!deferVideoFileTransition) context.imageType = Color_Plus_Depth;
}

static void changeModel(int option, bool init) {
	if (!init && (activeVideo || activeScreenCapture)) {
		videoDepthRestartPending = display3D &&
			((activeVideo && !fileList.empty() && fileList[fileIndex].type == Color_Only) ||
			 activeScreenCapture) &&
			option != activeVideoDepthModelOption;
	}
	qualityMode = depthQuality[option];
	depthSize = depthSizes[option];
	if (!init) {
		resetDepthGeneration();
		nativeDepthEstimator.unload();
		nativeDepthEstimatorLoaded = false;
		nativeDepthEstimatorError.clear();
	}
}

static std::array<std::string, 3> upscaleResolutions = { "1920", "2560", "3840" };
static void changeResolution(int option, bool init) {
	upscaleResolution = upscaleResolutions[option];
	if (!init) {
		// Resolution only changes the color/output scale. Do not throw away the
		// depth estimator or its temporal video state.
		if (activeVideo) resetVideoPlaybackBuffer(true, false);
		else resetDepthGeneration();
	}
}

static std::array backgroundStyles = { Blur, Solid, Light, Dark };
static void changeBackground(int option) {
	context.backgroundStyle = backgroundStyles[option];
}

static std::array sortOrders = { Alpha_Ascending, Alpha_Descending, Date_Descending, Date_Ascending };
static void changeSorting(int option, bool init) {
	sortOrder = sortOrders[option];
	if (!init && !fileList.empty()) {
		parseFileList({ fileList[fileIndex].link });
		resetDepthGeneration();
	}
}

static std::array<float, 4> slideshowWaitTimes = { 8.0, 10.0, 12.0, 14.0 };
static void changeSlideshow(int option) {
	slideshowWaitTime = slideshowWaitTimes[option];
}

static bool firstInit = true;
static std::unordered_map<std::string, std::function<void(int)>> menuCallback = {
	{ ChoiceStereo.label, [](int option) { changeStereo(option); } },
	{ ChoiceExport.label, [](int option) { changeExport(option); } },
	{ ChoiceModel.label, [](int option) { changeModel(option, firstInit); } },
	{ ChoiceResolution.label, [](int option) { changeResolution(option, firstInit); } },
	{ ChoiceBackground.label,[](int option) { changeBackground(option); } },
	{ ChoiceSorting.label, [](int option) { changeSorting(option, firstInit); } },
	{ ChoiceSlideshow.label, [](int option) { changeSlideshow(option); } },
    { ChoiceTags.label, [](int option) { changeImport(option, firstInit); } },
	{ ChoiceEyes.label, [](int option) { changeEyes(option); } }
};

static double sliderStart = 0.5;
static double currentStereoStrength = sliderStart * 0.8 + 0.1;
static double currentStereoDepth = sliderStart * 0.5 + 0.25;
static double currentStereoOffset = (1.0 - sliderStart) / 50.0;
static double currentGridAngle = sliderStart;

static double getSliderPercent(const Icon& icon) {
	if (icon.slider.size.x <= 0.0f) return 0.0;
	return glm::clamp(icon.slider.position.x / icon.slider.size.x + 0.5, 0.0, 1.0);
}

static void setSliderPercent(Icon& icon, double percent) {
	icon.slider.position.x = icon.slider.size.x *
		static_cast<float>(glm::clamp(percent, 0.0, 1.0) - 0.5);
}

static void showInfoTip(const std::string& text) {
	currentInfoLabel = text;
	Core::drawText(&context, currentInfoLabel, Image::infoFont, Image::infoTexture,
		Image::infoTextSize, "Info Texture");
	Image::displayInfo = true;
	Image::infoTargetVisibility = 1.0;
	displayInfoTime = getTimeNow();
}

static std::string formatVideoTimecode(const Icon& timeline) {
	const double duration = std::max(0.0, videoPlayer.duration());
	const double position = glm::clamp(
		getSliderPercent(timeline) * duration, 0.0, duration);
	const bool showHours = duration >= 3600.0;
	auto formatTime = [showHours](double value) {
		const auto seconds = static_cast<long long>(std::max(0.0, value));
		if (showHours) {
			return std::format("{}:{:02}:{:02}", seconds / 3600,
				(seconds / 60) % 60, seconds % 60);
		}
		return std::format("{}:{:02}", seconds / 60, seconds % 60);
	};
	std::string timecode = "Video Location : " + formatTime(position) + " / " + formatTime(duration);
	if (videoPlayer.hasChapters()) {
		timecode += " (Chapter " + std::to_string(videoPlayer.chapterAtTime(position) + 1) + " of " +
			std::to_string(videoPlayer.chapterCount()) + ")";
	}
	return timecode;
}

static std::string filterInfoFontText(const std::string& text) {
	if (Image::infoFont == nullptr) return text;
	std::string filtered;
	filtered.reserve(text.size());
	for (size_t index = 0; index < text.size();) {
		const auto first = static_cast<unsigned char>(text[index]);
		Uint32 codepoint = 0;
		size_t codepointLength = 0;
		if (first < 0x80) {
			codepoint = first;
			codepointLength = 1;
		} else if ((first & 0xE0) == 0xC0) {
			codepoint = first & 0x1F;
			codepointLength = 2;
		} else if ((first & 0xF0) == 0xE0) {
			codepoint = first & 0x0F;
			codepointLength = 3;
		} else if ((first & 0xF8) == 0xF0) {
			codepoint = first & 0x07;
			codepointLength = 4;
		} else {
			++index;
			continue;
		}

		if (index + codepointLength > text.size()) break;
		bool valid = true;
		for (size_t offset = 1; offset < codepointLength; ++offset) {
			const auto continuation = static_cast<unsigned char>(text[index + offset]);
			if ((continuation & 0xC0) != 0x80) {
				valid = false;
				break;
			}
			codepoint = (codepoint << 6) | (continuation & 0x3F);
		}
		const bool validRange = valid &&
			((codepointLength != 2 || codepoint >= 0x80) &&
			 (codepointLength != 3 || codepoint >= 0x800) &&
			 (codepointLength != 4 || codepoint >= 0x10000) &&
			 codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF));
		if (validRange && TTF_FontHasGlyph(Image::infoFont, codepoint))
			filtered.append(text, index, codepointLength);
		else
			filtered += '?';
		index += valid ? codepointLength : 1;
	}
	return filtered;
}

static float videoTimelineWidth = 0.0f;
static float videoVolumeWidth = 0.0f;
static float videoTrackButtonWidth = 0.0f;
static float videoControlGap = 0.0f;
static double currentVideoVolume = 1.0;

static float videoControlWidth() {
	const auto sliderThumbDiameter = style.getIconSlider(Style::getCurrentScale()) * 2.0f;
	return videoTimelineWidth + videoVolumeWidth + videoTrackButtonWidth * 2.0f +
		sliderThumbDiameter * 2.0f +
		videoControlGap * 3.0f;
}

static float videoControlCenter(float precedingWidth, float controlWidth) {
	return precedingWidth + controlWidth * 0.5f - videoControlWidth() * 0.5f;
}

static void updateChapterMarkers() {
	context.chapterMarkers.clear();
	if (activeVideo && videoPlayer.ready() && videoPlayer.hasChapters() && videoPlayer.duration() > 0.0) {
		const int count = videoPlayer.chapterCount();
		const double duration = videoPlayer.duration();
		context.chapterMarkers.reserve(count);
		for (int i = 0; i < count; ++i) {
			const double t = videoPlayer.chapterTime(i) / duration;
			context.chapterMarkers.push_back(std::clamp(t, 0.0, 1.0));
		}
	}
}

static double getSnappedSeekPercent(double rawPercent) {
	if (!activeVideo || !videoPlayer.hasChapters() || videoPlayer.duration() <= 0.0) {
		return rawPercent;
	}
	const auto& timeline = getIcon(IconType::VideoSeek);
	const float trackWidth = timeline.slider.size.x;
	if (trackWidth <= 0.0f) return rawPercent;

	const double duration = videoPlayer.duration();
	const double snapPercentThreshold = static_cast<double>(8.0f / trackWidth);
	const int count = videoPlayer.chapterCount();

	double bestSnapPercent = rawPercent;
	double minDistance = snapPercentThreshold;

	for (int i = 0; i < count; ++i) {
		const double chTime = videoPlayer.chapterTime(i);
		const double chPercent = std::clamp(chTime / duration, 0.0, 1.0);
		const double dist = std::abs(rawPercent - chPercent);
		if (dist < minDistance) {
			minDistance = dist;
			bestSnapPercent = chPercent;
		}
	}
	return bestSnapPercent;
}

static void seekVideoFromSlider() {
	if (!activeVideo || currentSlider == nullptr) return;
	double percent = getSliderPercent(*currentSlider);
	if (currentSlider->type == IconType::VideoSeek && videoPlayer.hasChapters()) {
		percent = getSnappedSeekPercent(percent);
	}
	const double target = percent * videoPlayer.duration();
	if (videoSliderScrubbing) {
		if (displayInfoEnabled) {
			showInfoTip(formatVideoTimecode(*currentSlider));
		}
		const double now = getTimeNow();
		if (now - videoScrubLastPreviewTime >= videoScrubPreviewInterval) {
			seekVideo(target, true);
			videoScrubLastPreviewTime = now;
		}
		return;
	}
	seekVideo(target, false);
}

static void seekVideo(double seconds, bool fastPreview) {
	const bool preserveDepthPresentation = activeVideo && display3D &&
		videoDepthProcessor.running() && videoDepthFrameLoaded;
	if (videoDepthProcessor.running()) {
		videoDepthProcessor.reset();
		videoDepthBlend.clear();
		videoDepthTransitionGeneration = 0;
		if (!preserveDepthPresentation) {
			Image::clearVideoDepth(&context);
			if (!fileList.empty()) context.imageType = fileList[fileIndex].type;
			setVideoDepthFallbackMode();
		}
	}
	// Keep the current 3D source and presentation alive until the newly sought
	// frame has its own depth map. The normal-generation path will hold the new
	// color frame until serviceVideoDepth has produced a matching result.
	resetVideoPlaybackBuffer(false, !preserveDepthPresentation);
	if (!preserveDepthPresentation) lastVideoFrame.reset();
	videoPlayer.seek(seconds, fastPreview);
}

static bool skipVideoBy(double seconds) {
	if (!activeVideo) return false;
	if (videoPlayer.ready()) seekVideo(videoPlayer.position() + seconds);
	return true;
}

static void updateStrengthSlider() {
	currentStereoStrength = getSliderPercent(*currentSlider) * 0.8 + 0.1;
}

static void updateDepthSlider() {
	currentStereoDepth = getSliderPercent(*currentSlider) * 0.5 + 0.25;
}

static void updateOffsetSlider() {
	currentStereoOffset = (1.0 - getSliderPercent(*currentSlider)) / 50.0;
	currentGridAngle = getSliderPercent(*currentSlider);
}

static std::string conversionStrength = "Separation";
static std::string conversionDepth = "Depth";
static std::string conversionOffset = "Parallax";

static std::unordered_map<std::string, std::pair<IconType, std::function<void()>>> conversionOptions = {
	{ conversionStrength, { IconType::Glasses, []() { updateStrengthSlider(); }}},
	{ conversionDepth, { IconType::Focus, []() { updateDepthSlider(); }}},
	{ conversionOffset, { IconType::Layers, []() { updateOffsetSlider(); }}}
};

Icon IconStrength = {
	IconType::Glasses,
	IconType::Glasses,
	IconGroup::SettingsDepth,
	IconMode::Slider,
	IconState::Idle,
	"Stereo Strength",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
		{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
		{ -style.getIconSliderSpace(Style::getCurrentScale()), -style.getIconSlider(Style::getCurrentScale()) - 10.0f },
		alignCenterBottom,
			{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
			areaBottomRightFull };
	},
	[]() { updateStrengthSlider(); },
	1.0,
	true,
	false,
{
	{ style.getIconDragger(Style::getCurrentScale()), style.getIconBar(Style::getCurrentScale()) },
	{ 0.0, 0.0 },
	alignCenter,
	areaTopLeft, areaBottomRightFull },
};

Icon IconDepth = {
	IconType::Focus,
	IconType::Focus,
	IconGroup::SettingsDepth,
	IconMode::Slider,
	IconState::Idle,
	"Depth Range",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
			{ 0.0, -style.getIconSlider(Style::getCurrentScale()) - 10.0 },
			alignCenterBottom,
				{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
				areaBottomRightFull };
	},
		[]() { updateDepthSlider(); },
		0.0,
		true,
	false,
	{
		{ style.getIconDragger(Style::getCurrentScale()), style.getIconBar(Style::getCurrentScale()) },
		{ 0.0, 0.0 },
		alignCenter,
		areaTopLeft, areaBottomRightFull },
	};

Icon IconOffset = {
	IconType::Layers,
	IconType::Layers,
	IconGroup::SettingsDepth,
	IconMode::Slider,
	IconState::Idle,
	"Parallax Offset",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
				{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
				{ style.getIconSliderSpace(Style::getCurrentScale()), -style.getIconSlider(Style::getCurrentScale()) - 10.0 },
				alignCenterBottom,
					{0.0, -positionEdgeLarge * 2.0, 0.0, 1.0},
					areaBottomRightFull };
	},
			[]() { updateOffsetSlider(); },
			1.0,
			true,
		false,
		{
			{ style.getIconDragger(Style::getCurrentScale()), style.getIconBar(Style::getCurrentScale()) },
			{ 0.0, 0.0 },
			alignCenter,
			areaTopLeft, areaBottomRightFull },
		};

static Icon IconVideoSeek = {
	IconType::VideoSeek, IconType::Maximize, IconGroup::None, IconMode::Slider, IconState::Idle,
	"Video Position", style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
			{ videoControlCenter(0.0f, videoTimelineWidth +
				style.getIconSlider(Style::getCurrentScale()) * 2.0f),
				-style.getIconSlider(Style::getCurrentScale()) - 10.0f }, alignCenterBottom,
			{ 0.0f, -positionEdgeLarge * 2.0f, 0.0f, 1.0f }, areaBottomRightFull};
	},
	[]() { seekVideoFromSlider(); }, 1.0, false, false,
	{{ videoTimelineWidth,
		style.getIconBar(Style::getCurrentScale()) }, { 0.0f, 0.0f }, alignCenter,
		areaTopLeft, areaBottomRightFull}
};

static Icon IconVideoVolume = {
	IconType::VideoVolume, IconType::Minimize, IconGroup::None, IconMode::Slider, IconState::Idle,
	"Video Volume", style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
			{ videoControlCenter(videoTimelineWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f +
				videoControlGap, videoVolumeWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f),
				-style.getIconSlider(Style::getCurrentScale()) - 10.0f }, alignCenterBottom,
			{ 0.0f, -positionEdgeLarge * 2.0f, 0.0f, 1.0f }, areaBottomRightFull};
	},
	[]() {
		if (currentSlider != nullptr) {
			const auto percent = getSliderPercent(*currentSlider);
			currentVideoVolume = percent <= 0.01 ? 0.0 :
				percent >= 0.99 ? 1.0 : percent;
			setSliderPercent(*currentSlider, currentVideoVolume);
			videoPlayer.setVolume(currentVideoVolume);
		}
	}, 1.0, false, false,
	{{ videoVolumeWidth, style.getIconBar(Style::getCurrentScale()) }, { 0.0f, 0.0f }, alignCenter,
		areaTopLeft, areaBottomRightFull}
};

static Icon IconVideoAudio = {
	IconType::VideoAudio, IconType::Loading, IconGroup::None, IconMode::Button, IconState::Idle,
	"Audio Language", style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
			{ videoControlCenter(videoTimelineWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f +
				videoControlGap + videoVolumeWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f +
				videoControlGap,
				videoTrackButtonWidth),
				-style.getIconSlider(Style::getCurrentScale()) - 10.0f }, alignCenterBottom,
			{ 0.0f, -positionEdgeLarge * 2.0f, 0.0f, 1.0f }, areaBottomRightFull};
	}, []() { videoPlayer.cycleAudioTrack(); }, 1.0, false, false
};

static Icon IconVideoCaption = {
	IconType::VideoCaption, IconType::Loading, IconGroup::None, IconMode::Button, IconState::Idle,
	"Caption Language", style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {{ style.getIconSlider(Style::getCurrentScale()), style.getIconSlider(Style::getCurrentScale()) },
			{ videoControlCenter(videoTimelineWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f +
				videoControlGap + videoVolumeWidth + style.getIconSlider(Style::getCurrentScale()) * 2.0f +
				videoControlGap +
				videoTrackButtonWidth + videoControlGap, videoTrackButtonWidth),
				-style.getIconSlider(Style::getCurrentScale()) - 10.0f }, alignCenterBottom,
			{ 0.0f, -positionEdgeLarge * 2.0f, 0.0f, 1.0f }, areaBottomRightFull};
	}, []() { videoPlayer.cycleSubtitleTrack(); }, 1.0, false, false
};

static void jumpNextChapter() {
	if (!activeVideo || !videoPlayer.hasChapters()) return;
	const int count = videoPlayer.chapterCount();
	const int current = videoPlayer.currentChapter();
	if (current + 1 < count) {
		const double targetTime = videoPlayer.chapterTime(current + 1);
		seekVideo(targetTime, false);
		showInfoTip("Chapter " + std::to_string(current + 2) + " of " + std::to_string(count));
	}
}

static void jumpPreviousChapter() {
	if (!activeVideo || !videoPlayer.hasChapters()) return;
	const int count = videoPlayer.chapterCount();
	const int current = videoPlayer.currentChapter();
	const double currentPos = videoPlayer.position();
	const double currentChapterStart = videoPlayer.chapterTime(current);
	const int target = (currentPos - currentChapterStart > 3.0 || current == 0) ? current : std::max(0, current - 1);
	const double targetTime = videoPlayer.chapterTime(target);
	seekVideo(targetTime, false);
	showInfoTip("Chapter " + std::to_string(target + 1) + " of " + std::to_string(count));
}

// Screen capture remains implemented but is temporarily unavailable from the
// UI until after the 3.0.0 launch.
static std::vector appIcons = { IconLoading, IconMinimize, IconFullscreen, IconOpen, IconBatch, IconSave,
	IconOptions, IconSettings, IconBack, IconForward, IconStereo3D,
	IconStrength, IconDepth, IconOffset, IconPlay, IconVideoSeek, IconVideoVolume,
	IconVideoAudio, IconVideoCaption, IconHelp, IconClose };

static void finishSliderDrag() {
	const bool volumeSliderChanged = currentSlider != nullptr &&
		currentSlider->type == IconType::VideoVolume;
	if (currentSlider != nullptr) {
		currentSlider->state = IconState::Idle;
		currentSlider->shown = false;
	}
	if (videoSliderScrubbing) {
		if (activeVideo && videoPlayer.ready()) {
			auto& timeline = getIcon(IconType::VideoSeek);
			double percent = getSliderPercent(timeline);
			if (videoPlayer.hasChapters()) {
				percent = getSnappedSeekPercent(percent);
				setSliderPercent(timeline, percent);
			}
			seekVideo(percent * videoPlayer.duration(), false);
			videoPlayer.setPlaying(videoSliderWasPlaying);
		}
		videoSliderScrubbing = false;
		videoSliderWasPlaying = false;
	}
	currentSlider = nullptr;
	if (volumeSliderChanged) saveOptions();
	mouseIsDown = false;
	isDragging = false;
	isIconCaptured = false;
	SDL_CaptureMouse(false);
}

static void setVideoControlsVisible(bool visible) {
	videoControlsVisible = activeVideo && visible;
	auto& timeline = getIcon(IconType::VideoSeek);
	auto& volume = getIcon(IconType::VideoVolume);
	timeline.active = videoControlsVisible;
	if (!videoControlsVisible) {
		if (currentSlider == &timeline || currentSlider == &volume || videoSliderScrubbing)
			finishSliderDrag();
		if (activeVideo) getIcon(IconType::Play).active = false;
		volume.active = false;
		getIcon(IconType::VideoAudio).active = false;
		getIcon(IconType::VideoCaption).active = false;
	} else {
		getIcon(IconType::Play).active = true;
		volume.active = true;
		getIcon(IconType::VideoAudio).active = true;
		getIcon(IconType::VideoCaption).active = !videoPlayer.audioOnly();
	}
}

static void updateVideoSlider() {
	if (!activeVideo) return;
	auto& timeline = getIcon(IconType::VideoSeek);
	const auto scale = std::max(context.displayScale, 0.01f);
	// SBS Full doubles the displayed horizontal view. Keep the video control
	// group in the same logical half-width so its buttons do not spread across
	// both views.
	const auto controlWidthScale = preferredStereoMode == SBS_Full ? 0.5f : 1.0f;
	const auto preferredWidth = style.getIconDragger(Style::getCurrentScale()) *
		12.0f * controlWidthScale;
	const auto thumbDiameter = style.getIconSlider(Style::getCurrentScale()) * 2.0f;
	const auto availableWidth = std::max(thumbDiameter,
		(windowSize.x / scale - positionEdgeLarge * 2.0f) * controlWidthScale);
	const auto totalWidth = std::min(preferredWidth, availableWidth);
	// Keep the language buttons close together while retaining the same gap
	// between the volume slider and the first language button.
	videoControlGap = style.getIconSpacer(Style::getCurrentScale()) * 0.75f;
	videoTrackButtonWidth = thumbDiameter;
	videoVolumeWidth = thumbDiameter * 1.4f;
	const auto minimumControlsWidth = thumbDiameter * 6.4f + videoControlGap * 3.0f;
	const auto controlsWidth = std::max(minimumControlsWidth, totalWidth);
	videoTimelineWidth = controlsWidth - videoVolumeWidth - videoTrackButtonWidth * 2.0f -
		thumbDiameter * 2.0f - videoControlGap * 3.0f;
	videoTimelineWidth = std::max(thumbDiameter, videoTimelineWidth);
	timeline.slider.size.x = videoTimelineWidth;
	timeline.slider.size.y = style.getIconBar(Style::getCurrentScale());
	getIcon(IconType::VideoVolume).slider.size = { videoVolumeWidth, timeline.slider.size.y };
	setSliderPercent(getIcon(IconType::VideoVolume), currentVideoVolume);
	if (!videoSliderScrubbing && videoPlayer.duration() > 0.0)
		setSliderPercent(timeline, videoPlayer.position() / videoPlayer.duration());
	updateChapterMarkers();
}

static constexpr const char* videoFilterExtensions =
	"mp4;m4v;mov;mkv;webm;avi;wmv;mpeg;mpg;vob;ifo;iso;ssif;m2ts;ts;gif";
static constexpr const char* audioFilterExtensions =
	"aac;flac;m4a;mp3;oga;ogg;opus;wav";
static constexpr const char* imageFilterExtensions =
	"jpg;jpeg;jpe;jfif;jif;jps;png;pns;tga;bmp;dib;tif;tiff;ico;cur;qoi"
	";avif;avifs;jxl;webp";
static constexpr const char* allMediaFilterExtensions =
	"jpg;jpeg;jpe;jfif;jif;jps;png;pns;tga;bmp;dib;tif;tiff;ico;cur;qoi"
	";avif;avifs;jxl;webp"
	";mp4;m4v;mov;mkv;webm;avi;wmv;mpeg;mpg;vob;ifo;iso;ssif;m2ts;ts;gif"
	";aac;flac;m4a;mp3;oga;ogg;opus;wav";

static const SDL_DialogFileFilter filters[] = {
	{ "All Media", allMediaFilterExtensions },
	{ "Just Images", imageFilterExtensions },
	{ "Just Videos", videoFilterExtensions },
	{ "Just Audio", audioFilterExtensions }
};

static void SDLCALL openFileCallback(void* userdata, const char* const* filelist, int filter) {
	if (!filelist) {
		doingFileOp = false;
		return;
	}
	if (!*filelist) {
		doingFileOp = false;
		return;
	}

	std::vector<std::string> fileNames{};

	while (*filelist) {
		if (isSupportedImage(*filelist) || isSupportedMedia(*filelist)) fileNames.push_back(*filelist);
		filelist++;
	}

	if (!fileNames.empty()) {
		deferMediaLoad(fileNames);
	}

	doingFileOp = false;
}

static void SDLCALL openFolderCallback(void* userdata, const char* const* filelist, int filter) {
	if (!filelist || !*filelist) {
		*static_cast<std::string*>(userdata) = "ERROR";
	} else {
		*static_cast<std::string*>(userdata) = *filelist;
	}
}

static void openFile() {
	doingFileOp = true;
	auto filterNum = sizeof(filters) / sizeof(filters[0]);
	SDL_ShowOpenFileDialog(openFileCallback, nullptr, context.window, filters, filterNum, nullptr, true);
}

static void openFolder() {
	doingFileOp = true;
	SDL_ShowOpenFolderDialog(openFolderCallback, &openFolderResult, context.window, nullptr, false);
}

static std::string formatVideoTimeTag(double presentationTime) {
	if (!std::isfinite(presentationTime) || presentationTime < 0.0) return {};

	const auto totalMilliseconds = static_cast<long long>(
		std::llround(presentationTime * 1000.0));
	const auto hours = totalMilliseconds / (60 * 60 * 1000);
	const auto minutes = (totalMilliseconds / (60 * 1000)) % 60;
	const auto seconds = (totalMilliseconds / 1000) % 60;
	const auto milliseconds = totalMilliseconds % 1000;
	return std::format("_t{:02}h{:02}m{:02}s{:03}ms", hours, minutes,
		seconds, milliseconds);
}

static void saveFile() {
	doingFileOp = true;
	// The save callback can run between video-frame updates. Refresh the GPU
	// source from the frame currently being presented so export reads the
	// complete packed frame (both eyes for native SBS video).
	if (activeVideo && lastVideoFrame != nullptr &&
		lastVideoFrame->generation == videoPlayer.generation()) {
		if (Image::updateVideoFrame(&context, *lastVideoFrame, false,
			lastVideoFrame->width, lastVideoFrame->height, false) != 0) {
			doingFileOp = false;
			return;
		}
	}
	auto renderResult = Image::renderStereoImage(&context, exportFormat);
	if (renderResult != 0) {
		doingFileOp = false;
		return;
	}

	auto data = Image::getExportTexture(&context);
	if (data == nullptr) {
		SDL_Log("Export readback failed: %s", SDL_GetError());
		doingFileOp = false;
		return;
	}

	std::string outFileName;
	std::string outputPathString;
	try {
		auto exportDir = std::filesystem::path(context.fileLink).parent_path();
		if (exportDir.filename() != exportFolderName) exportDir /= exportFolderName;

		std::error_code directoryError;
		std::filesystem::create_directories(exportDir, directoryError);
		if (directoryError) {
			SDL_Log("Could not create export directory %s: %s", exportDir.string().c_str(),
				directoryError.message().c_str());
			SDL_DestroySurface(data);
			doingFileOp = false;
			return;
		}

		std::string gridInfo;
		if (exportFormat == Light_Field_LKG) {
			const std::string aspect = std::format("{:.3f}",
				context.imageSize.x / context.imageSize.y);
			gridInfo = "9x8a" + aspect;
		}

		outFileName = removeFileTags(context.fileName);
		if (activeVideo && lastVideoFrame != nullptr &&
			lastVideoFrame->generation == videoPlayer.generation()) {
			outFileName += formatVideoTimeTag(lastVideoFrame->presentationTime);
		}
		const std::filesystem::path outFilePath = outFileName + "_" + exportTag +
			gridInfo + ".jpg";
		outputPathString = (exportDir / outFilePath).string();
	} catch (const std::exception& error) {
		SDL_Log("Could not prepare export path: %s", error.what());
		SDL_DestroySurface(data);
		doingFileOp = false;
		return;
	} catch (...) {
		SDL_Log("Could not prepare export path: unknown filesystem error");
		SDL_DestroySurface(data);
		doingFileOp = false;
		return;
	}

	const bool saved = IMG_SaveJPG(data, outputPathString.c_str(), 65);
	SDL_DestroySurface(data);
	if (!saved) {
		SDL_Log("Could not save export to %s: %s", outputPathString.c_str(), SDL_GetError());
		doingFileOp = false;
		return;
	}

	auto nameMaxLen = 26;
	auto displayName = outFileName;
	if (displayName.length() > nameMaxLen) {
		displayName = displayName.substr(0, nameMaxLen - 3) + "...";
	}
	std::string toType = " to 3D";
	if (exportFormat == Color_Only) toType = " to 2D";
	Core::drawText(&context, "Saved " + displayName + toType,
		Image::helpFont, Image::helpTexture,
		Image::helpTextSize, "Help Texture");
	Image::displayTip = true;
	displayTipTime = getTimeNow();
}

static bool saveExportSurface(SDL_Surface* data, const std::filesystem::path& sourcePath,
		const std::filesystem::path& exportDir) {
	if (data == nullptr) return false;
	try {
		std::error_code directoryError;
		std::filesystem::create_directories(exportDir, directoryError);
		if (directoryError) {
			SDL_Log("Could not create batch export directory %s: %s",
				exportDir.string().c_str(), directoryError.message().c_str());
			return false;
		}

		std::string gridInfo;
		if (exportFormat == Light_Field_LKG) {
			const std::string aspect = std::format("{:.3f}",
				context.imageSize.x / context.imageSize.y);
			gridInfo = "9x8a" + aspect;
		}

		const auto outFileName = removeFileTags(sourcePath.stem().string());
		const auto outputPath = exportDir /
			(outFileName + "_" + exportTag + gridInfo + ".jpg");
		return IMG_SaveJPG(data, outputPath.string().c_str(), 65);
	} catch (const std::exception& error) {
		SDL_Log("Could not prepare batch export path: %s", error.what());
	} catch (...) {
		SDL_Log("Could not prepare batch export path: unknown filesystem error");
	}
	return false;
}

static void processBatchExport() {
	if (!batchExportActive || context.loading || batchExportIndex >= batchInputPaths.size()) {
		if (batchExportActive && batchExportIndex >= batchInputPaths.size()) {
			batchExportActive = false;
			isConverting = false;
			Core::drawText(&context, "Batch Export Complete", Image::helpFont,
				Image::helpTexture, Image::helpTextSize, "Help Texture");
			Image::displayTip = true;
			displayTipTime = getTimeNow();
		}
		return;
	}

	const auto& inputPath = batchInputPaths[batchExportIndex++];
	auto depthPath = runtimeDepthOutputPath(inputPath);
	if (!exists(depthPath)) {
		const auto altExt = losslessDepthmaps ? ".jpg" : ".qoi";
		const auto altPath = depthPath.parent_path() / (inputPath.stem().string() + "_rgbd" + altExt);
		if (exists(altPath)) {
			depthPath = altPath;
		} else {
			return;
		}
	}

	SDL_Surface* batchImage = Core::loadImageDirect(depthPath.string());
	if (batchImage == nullptr || batchImage->w < 2 || batchImage->h < 1) {
		SDL_DestroySurface(batchImage);
		return;
	}

	SDL_GPUTexture* batchTexture = nullptr;
	Image::uploadTexture(&context, batchImage, &batchTexture, "Batch Image Texture");
	Context batchContext = context;
	batchContext.imageType = Color_Plus_Depth;
	batchContext.imageSize = { batchImage->w * 0.5f, static_cast<float>(batchImage->h) };
	SDL_DestroySurface(batchImage);

	if (batchTexture == nullptr || Image::renderStereoImage(&batchContext, exportFormat,
			batchTexture) != 0) {
		if (batchTexture != nullptr) SDL_ReleaseGPUTexture(context.device, batchTexture);
		return;
	}
	SDL_Surface* data = Image::getExportTexture(&batchContext);
	if (data != nullptr) {
		saveExportSurface(data, inputPath, batchFolderPath / exportFolderName);
		SDL_DestroySurface(data);
	}
	SDL_ReleaseGPUTexture(context.device, batchTexture);
}

static Icon& getIcon(IconType type) {
	for (auto& icon : appIcons) {
		if (icon.type == type) return icon;
	}
	return appIcons[0];
}

std::filesystem::path optionsPath =  "Settings.json";
std::filesystem::path optionsAbsPath = homePath / optionsPath;
static std::string optionsFilePath = optionsAbsPath.string();

void saveOptions() {
    char dataBuffer[65536];
    rapidjson::MemoryPoolAllocator<> allocator (dataBuffer, sizeof dataBuffer);
    rapidjson::Document document(&allocator, 256);
    document.SetObject();

	std::vector<const char*> nameCache{};
    for (const auto& setting : menuSelection) {
        if (setting.first == ChoiceTags.label) continue;
        auto settingName = setting.first.c_str();
        document.AddMember(rapidjson::GenericStringRef(settingName), setting.second, allocator);
        nameCache.push_back(settingName);
    }
	for (const auto& conversion : conversionOptions) {
		auto convertName = conversion.first.c_str();
		document.AddMember(rapidjson::GenericStringRef(convertName),
			getSliderPercent(getIcon(conversion.second.first)), allocator);
		nameCache.push_back(convertName);
	}

	auto infoSetting = infoSettingKey.c_str();
	document.AddMember(rapidjson::GenericStringRef(infoSetting),
		displayInfoEnabled, allocator);
	nameCache.push_back(infoSetting);

	auto borderlessSetting = borderlessSettingKey.c_str();
	document.AddMember(rapidjson::GenericStringRef(borderlessSetting),
		Image::useBorderlessWindow, allocator);
	nameCache.push_back(borderlessSetting);
	rapidjson::Value modelDirValue;
	modelDirValue.SetString(modelDirectory.c_str(), allocator);
	document.AddMember(rapidjson::StringRef("modelDirectory"), modelDirValue, allocator);
	document.AddMember(rapidjson::StringRef("videoVolume"), currentVideoVolume, allocator);
	document.AddMember(rapidjson::StringRef("losslessDepthmaps"), losslessDepthmaps, allocator);

    rapidjson::StringBuffer output;
    rapidjson::PrettyWriter writer(output);
    document.Accept(writer);

	auto optionsFolderPath = std::filesystem::path(optionsFilePath).parent_path();
	if (!exists(optionsFolderPath)) create_directories(optionsFolderPath);

	SDL_IOStream* optionsFile = SDL_IOFromFile(optionsFilePath.c_str(), "w" );
	if (optionsFile) {
		SDL_WriteIO(optionsFile, output.GetString(), output.GetSize() + 1);
		SDL_CloseIO(optionsFile);
	}
}

void loadOptions() {
	rapidjson::Document document;
	char dataBuffer[65536];
	SDL_IOStream* optionsFile = SDL_IOFromFile(optionsFilePath.c_str(), "r" );
	size_t dataSize = 0;
	if (optionsFile) {
		dataSize = SDL_ReadIO(optionsFile, dataBuffer, sizeof(dataBuffer) - 1);
		SDL_CloseIO(optionsFile);
	}
	if (dataSize == 0) return;
	dataBuffer[dataSize] = '\0';
	if (document.Parse(dataBuffer).HasParseError()) return;
	if (!document.IsObject()) return;

	for (auto& setting : menuSelection) {
		if (setting.first == ChoiceTags.label) continue;
		auto settingName = setting.first.c_str();
		if (document.HasMember(settingName)) {
			setting.second = document[settingName].GetInt();
			if (menuCallback[settingName]) menuCallback[settingName](setting.second);
		}
	}

	auto stereoValue = 0.0;
	for (const auto& conversion : conversionOptions) {
		auto convertName = conversion.first.c_str();
		if (document.HasMember(convertName)) {
			currentSlider = &getIcon(conversion.second.first);
			stereoValue = document[convertName].GetDouble();
			setSliderPercent(*currentSlider, stereoValue);
			if (conversion.second.second) conversion.second.second();
		}
	}

	auto infoSetting = infoSettingKey.c_str();
	if (document.HasMember(infoSetting)) {
		displayInfoEnabled = document[infoSetting].GetBool();
	}

	auto borderlessSetting = borderlessSettingKey.c_str();
	if (document.HasMember(borderlessSetting)) {
		Image::useBorderlessWindow = document[borderlessSetting].GetBool();
	}
	if (document.HasMember("modelDirectory") && document["modelDirectory"].IsString()) {
		modelDirectory = document["modelDirectory"].GetString();
	}
	if (document.HasMember("videoVolume") && document["videoVolume"].IsNumber()) {
		currentVideoVolume = glm::clamp(document["videoVolume"].GetDouble(), 0.0, 1.0);
		setSliderPercent(getIcon(IconType::VideoVolume), currentVideoVolume);
		videoPlayer.setVolume(currentVideoVolume);
	}
	if (document.HasMember("losslessDepthmaps")) {
		if (document["losslessDepthmaps"].IsBool()) {
			losslessDepthmaps = document["losslessDepthmaps"].GetBool();
		} else if (document["losslessDepthmaps"].IsInt()) {
			losslessDepthmaps = document["losslessDepthmaps"].GetInt() != 0;
		}
	}

}

static void updateStereoIcon() {
	auto& icon = getIcon(IconType::Stereo_3D);
	icon.image = display3D ? IconType::Stereo_2D : IconType::Stereo_3D;
	if (preferredStereoMode == Depth_Zoom) icon.image = IconType::Stereo_2D;
	if (preferredStereoMode == Mono) icon.image = display3D ? IconType::Mono_SD : IconType::Mono_SR;
}

static void setShowStereoSettings(bool show) {
	showingStereoSettings = show;
	auto& icon = getIcon(IconType::Settings);
	icon.image = show ? IconType::Close : IconType::Settings;
	if (activeVideo) setVideoControlsVisible(!show && !context.displayMenu);
}

static void toggleStereoSettings() {
	setShowStereoSettings(!showingStereoSettings);
}

static bool pendingDepthModelReload = false;

static void serviceDeferredDepthReload() {
	if (!pendingDepthModelReload) return;
	if (context.loading || doingPreload) return;
	pendingDepthModelReload = false;
	if (videoDepthRestartPending) {
		const bool restartVideoDepth = (activeVideo || activeScreenCapture) && display3D;
		videoDepthRestartPending = false;
		if (restartVideoDepth) startVideoDepth(true);
	}
	if (depthRegenerated) {
		const bool reloadDepthIn3D = !activeVideo && !activeScreenCapture && display3D;
		depthRegenerated = false;
		if (!activeVideo && !activeScreenCapture && !fileList.empty()) {
			const bool isColorSource = Core::getImageType(fileList[fileIndex].link) == Color_Only ||
				fileList[fileIndex].path != fileList[fileIndex].link ||
				fileList[fileIndex].type == Color_Only;
			if (reloadDepthIn3D && isColorSource) {
				keep3DForDepthReload = true;
				callDepthGen(fileIndex);
			} else {
				isConverting = false;
			}
		} else if (activeVideo && videoPlayer.audioOnly() && display3D && audioAlbumArt != nullptr) {
			auto* depthInput = SDL_DuplicateSurface(audioAlbumArt);
			if (depthInput != nullptr) {
				keep3DForDepthReload = true;
				callDepthGen(fileIndex, false, depthInput);
			} else {
				isConverting = false;
			}
		} else {
			isConverting = false;
		}
	}
}

static void toggleOptions() {
	context.displayMenu = !context.displayMenu;
	if (activeVideo) setVideoControlsVisible(!context.displayMenu);
	auto& icon = getIcon(IconType::Options);
	icon.image = context.displayMenu ? IconType::Close : IconType::Options;
	setShowStereoSettings(showingStereoSettings && !context.displayMenu);
	if (!context.displayMenu) {
		if (videoDepthRestartPending || depthRegenerated) {
			pendingDepthModelReload = true;
			if (videoDepthRestartPending && (activeVideo || activeScreenCapture) && display3D) {
				Core::drawText(&context, "Loading Video Depth Model", Image::helpFont,
					Image::helpTexture, Image::helpTextSize, "Help Texture");
				Image::displayTip = true;
				displayTipTime = getTimeNow();
			} else if (depthRegenerated && display3D &&
				((!activeVideo && !activeScreenCapture && !fileList.empty()) ||
				 (activeVideo && videoPlayer.audioOnly() && audioAlbumArt != nullptr))) {
				Core::drawText(&context, "Loading Depth Model, Please Wait", Image::helpFont,
					Image::helpTexture, Image::helpTextSize, "Help Texture");
				Image::displayTip = true;
				displayTipTime = getTimeNow();
				isConverting = true;
			}
		}
		saveOptions();
	}
}

static void setDisplay3D(bool display) {
	display3D = display;
	context.display3D = display;
	updateStereoIcon();
	setShowStereoSettings(showingStereoSettings && display3D);
}

static void refreshDisplay3D(StereoFormat type) {
	if (preferredStereoMode == Lenticular) {
		const bool isLenticular2View = (Image::nativeDisplayConfig.viewCount == 2);
		const bool supportedSource = type == Color_Plus_Depth ||
			type == Light_Field_LKG || (isLenticular2View && type != Color_Only);
		Image::setNativeOutputActive(&context, true);
		if (!display3D || !supportedSource) {
			// Native stereo and other tagged sources are intentionally shown as
			// mono on the light-field output, but remain in the user's 3D state
			// so loading another compatible source does not reset the mode.
			if (!supportedSource && type == Color_Only) setDisplay3D(false);
			setStereoMode(type == Color_Only ? Native : Mono);
			return;
		}
		setStereoMode(Lenticular);
		return;
	}
	if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half) {
		// SBS is a two-view presentation and is only valid in fullscreen. On
		// the initial load the window is still windowed, so keep the normal
		// single-view mode until fullscreen is entered.
		setStereoMode(isFullscreen ? preferredStereoMode :
			(type == Color_Only ? Native : Mono));
		if (type == Color_Only) setDisplay3D(false);
		return;
	}
	if (type == Color_Only) {
		setStereoMode(Native);
		setDisplay3D(false);
	} else {
		if (display3D) {
			if (preferredStereoMode == Lenticular && !Image::nativeOutputAvailable())
				setStereoMode(Mono);
			else
				setStereoMode(preferredStereoMode);
		} else {
			setStereoMode(Mono);
		}
	}
}

static void setStereoMode(ViewMode mode) {
	currentStereoMode = mode;
	context.mode = currentStereoMode;
	if (mode == Lenticular) Image::setNativeOutputActive(&context, true);
	Image::updateSize(&context);
}

static void setPreferredStereo(ViewMode mode, bool saveMode) {
	if (saveMode) defaultStereoMode = mode;
	preferredStereoMode = mode;
	if (saveMode && mode != Lenticular)
		Image::setNativeOutputActive(&context, false);
	updateStereoIcon();
	if (activeScreenCapture) {
		refreshDisplay3D(videoDepthFrameLoaded || videoDepthProcessor.running()
			? Color_Plus_Depth : Color_Only);
		Image::updateSize(&context);
		Image::updateRatio(&context, windowSize);
	} else if (!fileList.empty()) {
		const auto sourceType = activeVideo &&
			(videoDepthFrameLoaded || videoDepthProcessor.running())
			? Color_Plus_Depth : fileList[fileIndex].type;
		refreshDisplay3D(sourceType);
		Image::updateSize(&context);
		Image::updateRatio(&context, windowSize);
	}
}

static int naturalCompare(const std::string& a, const std::string& b, bool foldCase) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		const auto isDigit = [](unsigned char c) { return std::isdigit(c) != 0; };
		if (isDigit(static_cast<unsigned char>(a[i])) &&
			isDigit(static_cast<unsigned char>(b[j]))) {
			size_t iEnd = i, jEnd = j;
			while (iEnd < a.size() && isDigit(static_cast<unsigned char>(a[iEnd]))) ++iEnd;
			while (jEnd < b.size() && isDigit(static_cast<unsigned char>(b[jEnd]))) ++jEnd;

			auto significantStart = [](const std::string& value, size_t begin, size_t end) {
				size_t result = begin;
				while (result < end && value[result] == '0') ++result;
				return result;
			};
			auto aSignificant = significantStart(a, i, iEnd);
			auto bSignificant = significantStart(b, j, jEnd);
			auto aLength = iEnd - aSignificant;
			auto bLength = jEnd - bSignificant;
			if (aLength != bLength) return aLength < bLength ? -1 : 1;
			if (aLength > 0) {
				const auto result = a.compare(aSignificant, aLength, b, bSignificant, bLength);
				if (result != 0) return result < 0 ? -1 : 1;
			}

			if (iEnd - i != jEnd - j)
				return iEnd - i < jEnd - j ? -1 : 1;

			i = iEnd;
			j = jEnd;
		} else {
			const auto charA = foldCase ?
				static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i]))) :
				static_cast<unsigned char>(a[i]);
			const auto charB = foldCase ?
				static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[j]))) :
				static_cast<unsigned char>(b[j]);
			if (charA != charB) return charA < charB ? -1 : 1;
			++i;
			++j;
		}
	}

	if (i != a.size() || j != b.size()) return i == a.size() ? -1 : 1;
	return 0;
}

bool naturalLess(const std::string& a, const std::string& b) {
	const auto foldedResult = naturalCompare(a, b, true);
	if (foldedResult != 0) return foldedResult < 0;
	return naturalCompare(a, b, false) < 0;
}

bool nameAscending(const FileInfo& f1, const FileInfo& f2) {
	return naturalLess(f1.name, f2.name);
}

bool nameDescending(const FileInfo& f1, const FileInfo& f2) {
	return naturalLess(f2.name, f1.name);
}

bool timeAscending(const FileInfo& f1, const FileInfo& f2) {
	return f1.modified < f2.modified;
}

bool timeDescending(const FileInfo& f1, const FileInfo& f2) {
	return f2.modified < f1.modified;
}

static glm::vec2 refreshWindowSize() {
	int windowWidth, windowHeight;
	SDL_GetWindowSizeInPixels(context.window, &windowWidth, &windowHeight);
	windowSize = { (float)windowWidth, (float)windowHeight };
	context.windowSize = windowSize;
	return windowSize;
}

static glm::vec2 refreshWindowSizeBase() {
	int windowWidth, windowHeight;
	SDL_GetWindowSize(context.window, &windowWidth, &windowHeight);
	auto windowSizeBase = glm::vec2(windowWidth, windowHeight);
	context.windowSizeBase = windowSizeBase;
	return windowSizeBase;
}

static void updateFullscreenState() {
	auto& icon = getIcon(IconType::Fullscreen);
	icon.image = isFullscreen ? IconType::Window : IconType::Fullscreen;
	context.offset = glm::vec2(0.0f);
	targetZoom = 1.0f;
	context.fullscreen = isFullscreen;
	context.maximized = isMaximized;
	Image::updateSize(&context);
	if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half)
		refreshDisplay3D(context.imageType);
}

static void setFullscreen(bool fullscreen = true) {
	if (fullscreen && context.window != nullptr) {
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(context.window);
		if (displayID != 0) {
			Image::configureFullscreenMode(context.window, displayID);
		}
	}
	SDL_SetWindowFullscreen(context.window, fullscreen);
	SDL_SyncWindow(context.window);
	updateFullscreenState();
}

static void toggleFullscreen() {
	isFullscreen = !isFullscreen;
	setFullscreen(isFullscreen);
}

static void toggleScreenCapture() {
#if defined(__linux__)
		if (activeScreenCapture) {
			stopVideoDepth();
			screenCapture.stop();
			activeScreenCapture = false;
			activeVideo = false;
			videoFrameLoaded = false;
			lastVideoFrame.reset();
			resetVideoPlaybackBuffer(false);
			setVideoControlsVisible(false);
			setDisplay3D(false);
			setStereoMode(Native);
			setShowStereoSettings(false);
			context.displayMenu = false;
			context.gotoPrev = false;
			context.gotoNext = false;
			context.gotoRand = false;
			doneLoadingImage = false;
			switchedImage = false;
			isConverting = false;
			justConverted = false;
			currentVisibility = 0.0;
			targetVisibility = 0.0;
			context.visibility = 0.0f;
		Image::displayTip = false;
		Image::infoTargetVisibility = 0.0f;
		Image::infoCurrentVisibility = 0.0f;
		currentInfoLabel.clear();
		currentSlider = nullptr;
			mouseIsDown = false;
			isDragging = false;
			if (doingPreload) endPreload(false);
			for (auto& file : fileList) {
				SDL_DestroySurface(file.preload);
				file.preload = nullptr;
			}
			fileList.clear();
			fileIndex = -1;
		// Return to the same empty state used at startup instead of leaving the
		// last captured frame as the current image.
		FileInfo emptyFile{};
		Image::load(&context, emptyFile, nullptr);
		context.loading = false;
		context.fileLink.clear();
		context.fileName.clear();
		context.infoText.clear();
		Image::displayInfo = false;
		SDL_SetWindowTitle(context.window, "Rendepth");
		// Do not recalculate hover state here: the mouse is often still over a
		// photo control when capture is stopped, which would immediately reveal
		// the normal image UI over the blank state.
		mouseLastActive = 0.0;
		hideUI();
		return;
	}
	if (activeVideo || isConverting || context.loading) return;
	std::string error;
	if (!screenCapture.start(context.window, error)) {
		SDL_Log("Could not start screen capture: %s", error.c_str());
		return;
	}
	activeScreenCapture = true;
	videoFrameLoaded = false;
	lastVideoFrame.reset();
	resetVideoPlaybackBuffer(false);
	setVideoControlsVisible(false);
	context.loading = true;
#elif defined(_WIN32)
	SDL_Log("Windows screen capture requested; Windows Graphics Capture backend is not initialized yet");
#elif defined(__APPLE__)
	SDL_Log("macOS screen capture requested; ScreenCaptureKit backend is not initialized yet");
#else
	SDL_Log("Screen capture is not supported on this platform yet");
#endif
}

void toggleStereo() {
	resetRapidBrowseState();
	if (preferredStereoMode == Mono) return;
	if (isConverting) return;
	if (activeScreenCapture) {
		if (display3D) stopVideoDepth();
		else startVideoDepth();
		return;
	}
	if (fileList.empty()) return;
	if (activeVideo) {
		if (videoPlayer.audioOnly()) {
			auto& audioFile = fileList[fileIndex];
			if (display3D) {
				// Return to the original album art while keeping the audio decoder
				// and playback controls alive.
				if (audioAlbumArt == nullptr) return;
				auto* artwork = SDL_DuplicateSurface(audioAlbumArt);
				if (artwork == nullptr) return;
				if (Image::load(&context, audioFile, artwork, Color_Only) != 0) {
					SDL_DestroySurface(artwork);
					return;
				}
				audioFile.type = Color_Only;
				setDisplay3D(false);
				setStereoMode(Native);
				Image::updateSize(&context);
				checkMouseState();
				return;
			}

			// A completed conversion can be reused without invoking the depth
			// model again after switching back to the album-art view.
			if (audioFile.path != audioFile.link &&
				std::filesystem::exists(audioFile.path)) {
				if (loadAudioDepthImage() == 0) {
					setDisplay3D(true);
					refreshDisplay3D(Color_Plus_Depth);
					checkMouseState();
				}
				return;
			}

			if (audioAlbumArt == nullptr) return;
			auto* depthInput = SDL_DuplicateSurface(audioAlbumArt);
			if (depthInput == nullptr) return;
			callDepthGen(fileIndex, false, depthInput);
			// Keep the normal 2D presentation visible while depth is generated.
			// The completion handoff will load the generated RGB-D image and turn
			// stereo on, matching the still-image workflow.
			refreshDisplay3D(Color_Only);
			return;
		}
		if (fileList[fileIndex].type != Color_Only) {
			setDisplay3D(!display3D);
			context.imageType = fileList[fileIndex].type;
			refreshDisplay3D(context.imageType);
			return;
		}
		if (display3D) {
			// Keep the running depth processor and its latest frame cached while
			// viewing the video in 2D, so returning to 3D does not reload the model.
			setDisplay3D(false);
			setStereoMode(Native);
		} else if (videoDepthProcessor.running() || videoDepthFrameLoaded) {
			setDisplay3D(true);
			refreshDisplay3D(Color_Plus_Depth);
		} else {
			startVideoDepth();
		}
		return;
	}
	if (!display3D && fileList[fileIndex].type == Color_Only && !activeVideo)
		callDepthGen(fileIndex);
	if (display3D && fileList[fileIndex].type == Color_Plus_Depth &&
		preferredStereoMode == Mono) {
		fileList[fileIndex].path = fileList[fileIndex].link;
		switchedImage = true;
	}
	setDisplay3D(!display3D);
	refreshDisplay3D(fileList[fileIndex].type);
}

static void setSlideshow(bool slide) {
	isPlayingSlideshow = slide;
	auto& icon = getIcon(IconType::Play);
	icon.image = slide ? IconType::Pause : IconType::Play;
	depthEffect = slide ? 0.0 : 1.0;
	targetZoom = 1.0;
	if (slide) {
		preloadDir = 0;
		nextRandIndex = getRandImageIndex();
	} else {
		preloadDir = 1;
		nextRandIndex = -1;
	}
	lastSlideshowTime = getTimeNow();
	if (doingPreload) endPreload(true);
	if (fileList.empty()) return;
	if (slide) {
		if (!isConverting && fileList[fileIndex].type == Color_Only) {
			callDepthGen(fileIndex);
		} else {
			currentVisibility = 0.0;
		}
		if (fileList[fileIndex].type == Color_Only ||
			(fileList[fileIndex].type == Color_Plus_Depth && !display3D) ||
			preferredStereoMode == Mono) {
			setPreferredStereo(Depth_Zoom, false);
			setDisplay3D(true);
			refreshDisplay3D(fileList[fileIndex].type);
		}
	} else {
		if (currentStereoMode == Depth_Zoom) {
			if (defaultStereoMode == Mono) {
				fileList[fileIndex].path = fileList[fileIndex].link;
				fileList[fileIndex].type = Color_Only;
				SDL_DestroySurface(fileList[fileIndex].preload);
				fileList[fileIndex].preload = nullptr;
				loadImage(nullptr);
			}
			setStereoMode(Mono);
			setDisplay3D(false);
			setPreferredStereo(defaultStereoMode, false);
			refreshDisplay3D(fileList[fileIndex].type);
		}
	}
	updateStereoIcon();
}

static void toggleSlideshow() {
	if (activeScreenCapture) return;
	if (activeVideo) {
		videoPlayer.setPlaying(!videoPlayer.playing());
		getIcon(IconType::Play).image = videoPlayer.playing() ? IconType::Pause : IconType::Play;
		return;
	}
	setSlideshow(!isPlayingSlideshow);
}

static void setMaximize(bool maximize) {
	if (maximize) {
		SDL_MaximizeWindow(context.window);
	} else {
		SDL_RestoreWindow(context.window);
	}
	SDL_SyncWindow(context.window);
	isMaximized = maximize;
	showCustomCursor(false);
	mouseMoveDelay = mouseDelayCount;
	context.fullscreen = isFullscreen;
	context.maximized = isMaximized;
	context.offset = glm::vec2(0.0f);
	targetZoom = 1.0f;
	Image::updateSize(&context);
}

static void toggleMaximized() {
	if (!isFullscreen) {
		isMaximized = !isMaximized;
		setMaximize(isMaximized);
	} else {
		isFullscreen = false;
		setFullscreen(false);
	}
}

static void pushFileInfo(const std::filesystem::path& filePath) {
	std::string fileName = filePath.string();
	if (isSupportedImage(fileName) || isSupportedMedia(fileName)) {
		if (fileName.empty()) return;
		if (filePath.filename().string().empty() && !isSupportedMedia(fileName)) return;
		std::error_code ec;
		std::filesystem::file_time_type modifiedTime{};
		uintmax_t fileSize = 0;
		if (std::filesystem::is_regular_file(filePath, ec)) {
			modifiedTime = std::filesystem::last_write_time(filePath, ec);
			fileSize = std::filesystem::file_size(filePath, ec);
		} else if (std::filesystem::is_directory(filePath, ec)) {
			modifiedTime = std::filesystem::last_write_time(filePath, ec);
		}
		auto sizeText = formatFileSize(fileSize);
		FileInfo info;
		info.link = fileName;
		info.path = fileName;
		info.name = filePath.filename().string();
		if (info.name.empty()) info.name = filePath.string();
		info.base = filePath.stem().string();
		if (info.base.empty()) info.base = info.name;
		info.size = sizeText;
		info.date = std::format("{:%Y-%m-%d}", modifiedTime);
		info.modified = modifiedTime;
		info.preload = nullptr;
		info.type = isSupportedAudio(fileName) ? Color_Only : Core::getImageType(info.name);
		if (info.type == Unknown_Format) info.type = Core::defaultImportFormat;
		fileList.push_back(info);
	}
}

static void parseFileList(const std::vector<std::string>& filesToLoad) {
	resetRapidBrowseState();
	if (doingPreload) endPreload(true);
	const std::filesystem::path filePath = filesToLoad[0];
	auto parentPath = filePath.parent_path();
	fileList.clear();
	fileIndex = -1;

	auto fileListLength = filesToLoad.size();
	if (fileListLength > 1) {
		for (const auto& fileToLoad : filesToLoad) {
			pushFileInfo(fileToLoad);
		}
	} else {
		if (!parentPath.empty()) {
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(parentPath, ec)) {
				pushFileInfo(entry.path());
			}
		}
	}
	if (fileList.empty()) {
		pushFileInfo(filePath);
	}

	if (!fileList.empty()) {
		if (sortOrder == Alpha_Ascending)
			std::sort(fileList.begin(), fileList.end(), nameAscending);
		else if (sortOrder == Alpha_Descending)
			std::sort(fileList.begin(), fileList.end(), nameDescending);
		else if (sortOrder == Date_Ascending)
			std::sort(fileList.begin(), fileList.end(), timeAscending);
		else if (sortOrder == Date_Descending)
			std::sort(fileList.begin(), fileList.end(), timeDescending);

		auto sortIndex = 0;
		for (const auto& file : fileList) {
			if (filePath.string() == file.link) {
				fileIndex = sortIndex;
				break;
			}
			sortIndex++;
		}
		randImage.param(std::uniform_int_distribution<>::param_type(0, (int)fileList.size() - 1));
		currentRandIndex = -1;
		nextRandIndex = -1;
	}
	preloadDir = 1;
}

void preloadComplete(SDL_Surface* preloadData, FileInfo& preloadFile) {
	SDL_DestroySurface(preloadFile.preload);
	preloadFile.preload = preloadData;
}

void preloadImage(int overrideId = -1) {
	auto nextId = nextFileIndex();
	auto prevId = previousFileIndex();
	if (preloadDir == 0 && overrideId == -1) return;
	auto preloadId = preloadDir > 0 ? nextId : prevId;
	if (overrideId >= 0) preloadId = overrideId;
	if (doingPreload) return;
	if (preloadId == fileIndex) return;
	if (fileList[preloadId].preload != nullptr) return;
	if (getMediaType(fileList[preloadId].link) != MediaType::Image) return;
	asyncData.path = fileList[preloadId].path;
	asyncData.surface = nullptr;
	asyncData.fileIndex = preloadId;
	asyncData.done.store(false, std::memory_order_relaxed);
	if (!isSpeculativeDepth) preloadDepthIndex = -1;
	doingPreload = true;
	preloadThread = Core::loadImageAsync(asyncData);
	if (preloadThread == nullptr) endPreload(false);
}

static void endPreload(bool success) {
	if (preloadThread == nullptr) {
		doingPreload = false;
		preloadDepthIndex = -1;
		isSpeculativeDepth = false;
		pendingPreloadNavigation = -1;
		preloadNavigationReady = false;
		return;
	}
	if (success) {
		int asyncId;
		SDL_WaitThread(preloadThread, &asyncId);
		if (asyncData.surface != nullptr && asyncData.fileIndex >= 0 && !fileList.empty()) {
			const int loadedIndex = asyncData.fileIndex;
			preloadComplete(asyncData.surface, fileList[loadedIndex]);
			asyncData.surface = nullptr;
			if (pendingPreloadNavigation == loadedIndex)
				preloadNavigationReady = true;
			if (loadedIndex != fileIndex && fileList[loadedIndex].type == Color_Only &&
				(display3D || isPlayingSlideshow || preferredStereoMode == Depth_Zoom) &&
				!isConverting && !depthGenAlive && !rapidBrowseMode) {
				preloadDepthIndex = loadedIndex;
				isSpeculativeDepth = true;
				callDepthGen(loadedIndex, true);
			}
		} else if (pendingPreloadNavigation == asyncData.fileIndex) {
			pendingPreloadNavigation = -1;
			preloadNavigationReady = false;
		}
	} else {
		SDL_WaitThread(preloadThread, nullptr);
		SDL_DestroySurface(asyncData.surface);
		pendingPreloadNavigation = -1;
		preloadNavigationReady = false;
	}
	asyncData.surface = nullptr;
	asyncData.fileIndex = -1;
	asyncData.path = "";
	preloadThread = nullptr;
	doingPreload = false;
}

static void updateDisplayScale() {
	Style::calculateScale(context.virtualSize / context.displayScale);
	Image::initFonts(&context);
	Image::initMenuTexture();
	Image::createMenuAssets(&context);
	Image::saveMenuLayout(&context);
	updateButtonCanvasSizes();
	Core::drawText(&context, Core::lastDrawnText, Image::helpFont,
			Image::helpTexture, Image::helpTextSize, "Help Texture");
}

static auto grabMargin = 32.0;
static auto resizeMargin = 12.0;
static auto windowDraggable = false;
static SDL_HitTestResult windowHitCallback(SDL_Window* window,
	const SDL_Point* area, void *data) {
	auto context = (Context*)data;
	windowDraggable = false;

	auto topBarSize = grabMargin * context->displayScale / context->pixelDensity;
	auto sideEdgeSize = resizeMargin * context->displayScale / context->pixelDensity;

	if (area->x < sideEdgeSize) {
		if (area->y < sideEdgeSize) {
			return SDL_HITTEST_RESIZE_TOPLEFT;
		}
		if (area->y > context->windowSizeBase.y - sideEdgeSize) {
			return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		}
		return SDL_HITTEST_RESIZE_LEFT;
	}

	if (area->x > context->windowSizeBase.x - sideEdgeSize) {
		if (area->y < sideEdgeSize) {
			return SDL_HITTEST_RESIZE_TOPRIGHT;
		}
		if (area->y > context->windowSizeBase.y - sideEdgeSize) {
			return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		}
		return SDL_HITTEST_RESIZE_RIGHT;
	}

	if (area->y < topBarSize) {
		windowDraggable = true;
		return SDL_HITTEST_DRAGGABLE;
	}

	if (area->y < sideEdgeSize) {
		return SDL_HITTEST_RESIZE_TOP;
	}

	if (area->y > context->windowSizeBase.y - sideEdgeSize) {
		return SDL_HITTEST_RESIZE_BOTTOM;
	}

	return SDL_HITTEST_NORMAL;
}
SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
	std::string fileToLoad{};
	if (argc >= 2) fileToLoad = std::string(argv[1]);

	context.appName = "Rendepth";
	context.windowSize = { 1920, 1080 };
	context.appIcons = &appIcons;
	context.menuChoices = &menuChoices;
	context.menuSelection = &menuSelection;
	context.menuRollover = &menuRollover;
	context.menuCallback = &menuCallback;
	context.offset = { 0.0, 0.0 };
	context.displayScale = 1.0;
	context.mouse = { mouseValueNull, mouseValueNull };
	context.mouseVisibility = 1.0;
	context.infoVisibility = 0.0;
	context.loadingRotation = 0.0;
	context.currentZoom = 1.0;
	context.stereoStrength = currentStereoStrength;
	context.stereoDepth = currentStereoDepth;
	context.stereoOffset = currentStereoOffset;
	context.displayMenu = false;
	context.backgroundStyle = Blur;
	context.effectRandom = 0;
	context.swapLeftRight = false;

	loadOptions();

	firstInit = false;

	if (!fileToLoad.empty())
		parseFileList({ fileToLoad });

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Failed To Initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_Log("Audio playback is unavailable: %s", SDL_GetError());
	}

	if (!fileList.empty()) {
		FileInfo emptyFile{};
		FileInfo& initialImage = isSupportedMedia(fileList[fileIndex].link)
			? emptyFile : fileList[fileIndex];
		if (Image::init(&context, initialImage) < 0) {
			SDL_Log("Could Not Initialize Image.");
			return SDL_APP_FAILURE;
		}
		if (isSupportedMedia(fileList[fileIndex].link)) {
			doneLoadingImage = false;
			loadImage(nullptr);
		} else {
			doneLoadingImage = true;
		}
	} else {
		FileInfo emptyFile{};
		if (Image::init(&context, emptyFile) < 0) {
			SDL_Log("Could Not Initialize Image.");
			return SDL_APP_FAILURE;
		}
	}

	if (Image::useBorderlessWindow) SDL_SetWindowHitTest(context.window, windowHitCallback, &context);

	// Rendepth always starts windowed. Establish that state before applying
	// the saved stereo preference so SBS cannot affect the first frame.
	isFullscreen = false;
	context.fullscreen = isFullscreen;
	context.maximized = isMaximized;
	refreshWindowSize();
	refreshWindowSizeBase();

	const bool onLenticularDisplay = Image::isNativeDisplayOnMainWindow();
	if (onLenticularDisplay) {
		preferredStereoMode = Lenticular;
		currentStereoMode = Lenticular;
		context.mode = Lenticular;
	}

	const bool savedLenticular = preferredStereoMode == Lenticular &&
		!fileList.empty();
	setDisplay3D(false);
	if (savedLenticular) Image::setNativeOutputActive(&context, true);

	if (!fileList.empty() && fileList.size() > fileIndex) {
		refreshDisplay3D(fileList[fileIndex].type);
		if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half)
			setStereoMode(fileList[fileIndex].type == Color_Only ? Native : Mono);
	} else if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half) {
		setStereoMode(Native);
	}

	hideUI();

	return SDL_APP_CONTINUE;
}

static auto visibilitySpeed = 9.0;
static auto visibilitySwitchMinimum = 0.75;

static SDL_Surface* createAudioPlaceholder() {
	constexpr int placeholderSize = 512;
	auto* surface = SDL_CreateSurface(placeholderSize, placeholderSize,
		SDL_PIXELFORMAT_ABGR8888);
	if (surface == nullptr) return nullptr;
	const auto gray = static_cast<Uint8>(std::lround(Image::clearColorDark.r * 255.0f));
	SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, gray, gray, gray, 255));
	return surface;
}

static int loadAudioDepthImage() {
	if (!activeVideo || !videoPlayer.audioOnly() || fileList.empty()) return 1;
	auto& audioFile = fileList[fileIndex];
	if (audioFile.path.empty() || audioFile.path == audioFile.link) return 1;
	auto* rgbd = Core::loadImageDirect(audioFile.path);
	if (rgbd == nullptr) return 1;
	const auto result = Image::load(&context, audioFile, rgbd, Color_Plus_Depth);
	if (result != 0) {
		SDL_DestroySurface(rgbd);
		return result;
	}
	audioFile.type = Color_Plus_Depth;
	return 0;
}

static int loadImage(void* ptr) {
	currentVisibility = 0.0;
	targetVisibility = 0.0;
	if (activeVideo && videoPlayer.audioOnly() && !fileList.empty() &&
		fileList[fileIndex].path != fileList[fileIndex].link) {
		const auto result = loadAudioDepthImage();
		if (result == 0) doneLoadingImage = true;
		return result;
	}
	if (isSupportedAudio(fileList[fileIndex].link)) {
		setShowStereoSettings(false);
		if (activeVideo) {
			stopVideoDepth(false);
			videoPlayer.close();
			clearAudioAlbumArt();
			resetVideoPlaybackBuffer(false);
			lastVideoFrame.reset();
			activeVideo = false;
			videoFrameLoaded = false;
			setVideoControlsVisible(false);
		}
		setDisplay3D(false);
		setStereoMode(Native);
		std::string error;
		videoPlayer.setVolume(currentVideoVolume);
		if (!videoPlayer.open(fileList[fileIndex].link, error)) {
			failVideoLoad(error);
			return 1;
		}
		activeVideo = true;
		fileList[fileIndex].type = Color_Only;
		SDL_Surface* artwork = videoPlayer.takeAlbumArt();
		if (artwork == nullptr) artwork = createAudioPlaceholder();
		clearAudioAlbumArt();
		if (artwork != nullptr) audioAlbumArt = SDL_DuplicateSurface(artwork);
		FileInfo artworkInfo = fileList[fileIndex];
		const int result = Image::load(&context, artworkInfo, artwork, Color_Only);
		if (result != 0) {
			SDL_DestroySurface(artwork);
			videoPlayer.close();
			activeVideo = false;
			setVideoControlsVisible(false);
			return result;
		}
		videoFrameLoaded = false;
		videoFileTransitionActive = false;
		setVideoControlsVisible(true);
		getIcon(IconType::Play).image = videoPlayer.playing() ? IconType::Pause : IconType::Play;
		doneLoadingImage = true;
		return 0;
	}
	if (isSupportedVideo(fileList[fileIndex].link)) {
		// Video controls and stereo settings occupy the same UI area.
		// Close the stereo panel before showing controls for the new video.
		setShowStereoSettings(false);
		const bool preserveMonoVideo3D = display3D &&
			fileList[fileIndex].type == Color_Only;
		// Keep the current 3D presentation visible while the replacement video
		// initializes. This applies to both source-stereo and inferred-depth video.
		const bool preserveVideo3D = activeVideo && display3D && videoFrameLoaded &&
			lastVideoFrame != nullptr;
		const bool replacementNeedsDepth = preserveVideo3D &&
			fileList[fileIndex].type == Color_Only;
		const bool preserveVideoDepth = preserveVideo3D && replacementNeedsDepth &&
			videoDepthProcessor.running();
		const bool preserveDepthTexture = preserveVideo3D && videoDepthFrameLoaded;
		const auto previousImageType = context.imageType;
		std::string error;
		if (preserveVideoDepth) {
			// The model is independent of the video decoder. Drop pending output
			// and temporal state for the new generation, but keep the worker and
			// loaded estimator alive.
			videoDepthProcessor.reset();
		} else {
			// A source-stereo replacement does not need the old depth worker, but
			// its texture must remain until the replacement frame is uploaded.
			stopVideoDepth(false, !preserveDepthTexture);
		}
		context.imageType = previousImageType;
		videoPlayer.close();
		resetVideoPlaybackBuffer(false, !preserveDepthTexture);
		if (!preserveVideo3D) lastVideoFrame.reset();
		videoPlayer.setVolume(currentVideoVolume);
		activeVideo = videoPlayer.open(fileList[fileIndex].link, error);
		if (!activeVideo) {
			failVideoLoad(error);
			return 1;
		}
		if (preserveVideo3D) {
			if (!preserveVideoDepth && fileList[fileIndex].type == Color_Only)
				startVideoDepth(preserveDepthTexture);
		} else if (preserveMonoVideo3D) {
			startVideoDepth();
		}
		if (preserveMonoVideo3D) keep3DForDepthReload = true;
		resetVideoPlaybackBuffer(false, !preserveDepthTexture);
		context.imageType = previousImageType;
		videoFrameLoaded = preserveVideo3D;
		videoFileTransitionActive = preserveVideo3D;
		videoFileTransitionNeedsDepth = replacementNeedsDepth &&
			videoDepthProcessor.running();
		videoDepthTransitionGeneration = videoFileTransitionNeedsDepth
			? videoPlayer.generation() : 0;
		setVideoControlsVisible(true);
		getIcon(IconType::Play).image = IconType::Pause;
		std::string mediaTitle = fileList[fileIndex].name;
		SDL_SetWindowTitle(context.window, mediaTitle.c_str());
		Image::displayHelp = false;
		Image::displayInfo = false;
		if (preserveVideo3D) {
			currentVisibility = 1.0;
			targetVisibility = 1.0;
		}
		doneLoadingImage = false;
		context.loading = true;
		return 0;
	}
	if (activeVideo) {
		// Preserve the user's 3D state while changing from a video to an image.
		// refreshDisplay3D() will select the correct presentation for the new
		// source after it has been loaded.
		stopVideoDepth(false);
		videoPlayer.close();
		clearAudioAlbumArt();
		resetVideoPlaybackBuffer(false);
		lastVideoFrame.reset();
		activeVideo = false;
		videoFrameLoaded = false;
		setVideoControlsVisible(false);
	}
	auto& currentFile = fileList[fileIndex];
	auto sourceType = Core::getImageType(currentFile.path);
	if (sourceType == Unknown_Format) sourceType = currentFile.type;
	if (sourceType == Unknown_Format) sourceType = Core::defaultImportFormat;
	SDL_Surface* imageData = currentFile.preload;
	if (imageData == nullptr)
		imageData = Core::loadImageDirect(currentFile.path);
	if (imageData != nullptr)
		imageData = prepareNativeColorSurface(imageData, sourceType);
	auto result = Image::load(&context, currentFile, imageData);
	currentFile.preload = nullptr;
	doneLoadingImage = true;
	if (result == 0 && display3D && sourceType == Color_Only && !isConverting) {
		// Keep 3D active while the existing image conversion path produces the
		// RGB-D replacement. The new color source is already visible, and the
		// completion handoff will refresh it with the converted source.
		keep3DForDepthReload = true;
		callDepthGen(fileIndex);
	}
	return result;
}

static void deferMediaLoad(const std::vector<std::string>& files) {
	if (files.empty()) return;
	deferredMediaFiles = files;
}

static void serviceDeferredMediaLoad() {
	if (deferredMediaFiles.empty() || isConverting || doingPreload || context.loading)
		return;
	if (isPlayingSlideshow) cancelSlideshow();
	auto files = std::move(deferredMediaFiles);
	parseFileList(files);
	updateStereoIcon();
	loadImage(nullptr);
	checkMouseState();
}

SDL_AppResult SDL_AppIterate(void* appstate) {
	if ((currentSlider != nullptr || videoSliderScrubbing) &&
		!(SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) {
		finishSliderDrag();
	}
	serviceNativeGpuUpscale();
	serviceDeferredMediaLoad();
	serviceVideo();
	Image::updateVideoBackgroundAnimation();
	serviceScreenCapture();
	// Screen capture shares the depth processor with video, but does not enter
	// serviceVideo()'s activeVideo path where depth completion is normally
	// serviced.
	if (activeScreenCapture) serviceVideoDepth();
	auto timeNow = getTimeNow();
	context.deltaTime = timeNow - lastTime;
	lastTime = timeNow;

	deltaTimes[deltaIndex] = context.deltaTime;
	deltaIndex = (deltaIndex + 1) % deltaCount;

	auto deltaAverage = 0.0;
	for (auto time : deltaTimes) {
		deltaAverage += time;
	}
	deltaAverage /= deltaCount;

	currentVisibility = Utils::tween(currentVisibility, targetVisibility, visibilitySpeed * deltaAverage);
	Image::infoCurrentVisibility = Utils::tween(Image::infoCurrentVisibility,
		Image::infoTargetVisibility, visibilitySpeed * deltaAverage);

	if (preloadNavigationReady && pendingPreloadNavigation >= 0) {
		const int loadedIndex = pendingPreloadNavigation;
		preloadNavigationReady = false;
		pendingPreloadNavigation = -1;
		lastSwitchTime = getTimeNow();
		fileIndex = loadedIndex;
		loadImage(nullptr);
		navigationLoadingIndicator = false;
		if (display3D && fileList[fileIndex].type == Color_Only)
			callDepthGen(fileIndex);
	}

	if (switchedImage) {
		if (context.loading) {
			switchedImage = false;
			context.gotoPrev = false;
			context.gotoNext = false;
			context.gotoRand = false;
		} else {
			auto switchTime = getTimeNow() - lastSwitchTime;
			if (switchTime > getMinimumSwitchTime() && currentVisibility > visibilitySwitchMinimum) {
				lastSwitchTime = getTimeNow();
				context.offset = { 0, 0 };
				if (context.gotoPrev) {
					fileIndex = previousFileIndex();
				} else if (context.gotoNext) {
					fileIndex = nextFileIndex();
				} else if (context.gotoRand) {
					fileIndex = currentRandIndex;
				}
				context.gotoPrev = false;
				context.gotoNext = false;
				context.gotoRand = false;
				switchedImage = false;
				loadImage(nullptr);
				navigationLoadingIndicator = false;
			}
		}
		checkMouseState();
	}
	// Apply worker completions after the navigation state machine has run.
	serviceDepthCompletions();

	if (justConverted) {
		setDisplay3D(true);
		if (!fileList.empty()) {
			const auto sourceType = activeVideo && videoDepthProcessor.running()
				? Color_Plus_Depth : fileList[fileIndex].type;
			refreshDisplay3D(sourceType);
		}
		justConverted = false;
	}

	if (doneLoadingImage) {
		doneLoadingImage = false;
		const bool preserve3D = keep3DForDepthReload;
		keep3DForDepthReload = false;
		if (!preserve3D || !display3D) {
			const auto sourceType = activeVideo && videoDepthProcessor.running()
				? Color_Plus_Depth : fileList[fileIndex].type;
			refreshDisplay3D(sourceType);
		}
		Image::updateSize(&context);
		context.offset = glm::vec2(0.0f);
		currentZoom = 1.0;
		targetZoom = 1.0;
		if (preserve3D || rapidBrowseMode) {
			currentVisibility = 1.0;
			targetVisibility = 1.0;
			depthEffect = rapidBrowseMode ? 0.0 : 1.0;
			context.depthEffect = depthEffect;
		} else {
			currentVisibility = 0.0;
			targetVisibility = 1.0;
			depthEffect = 0.0;
			context.depthEffect = depthEffect;
		}
		context.effectRandom = randEffect(randGen);
		switchedImage = false;
		lastSlideshowTime = timeNow;
		menuChoices[8].active = true;
		menuSelection[ChoiceTags.label] = 4;
		if (Core::defaultImportFormat == Side_By_Side_Full) menuSelection[ChoiceTags.label] = 0;
		else if (Core::defaultImportFormat == Side_By_Side_Half) menuSelection[ChoiceTags.label] = 1;
		else if (Core::defaultImportFormat == Top_And_Bottom_Full) menuSelection[ChoiceTags.label] = 2;
		else if (Core::defaultImportFormat == Top_And_Bottom_Half) menuSelection[ChoiceTags.label] = 3;
		else if (Core::defaultImportFormat == Color_Only) menuSelection[ChoiceTags.label] = 4;
		else if (Core::defaultImportFormat == Color_Anaglyph) menuSelection[ChoiceTags.label] = 5;
		if (!activeVideo && !rapidBrowseMode) {
			if (isPlayingSlideshow) preloadImage(nextRandIndex);
			else preloadImage();
		}
		if (rapidBrowseMode && display3D)
			setDisplay3D(false);
		if (display3D && !isFullscreen && showGoFullScreenOnce && (context.mode == SBS_Full ||
				context.mode == SBS_Half || context.mode == RGB_Depth)) {
			Core::drawText(&context, "Go Full-Screen to View in Stereo",
				Image::helpFont, Image::helpTexture,
				Image::helpTextSize, "Help Texture");
			Image::displayTip = true;
			displayTipTime = getTimeNow();
			showGoFullScreenOnce = false;
		}
	} else if (doingPreload && asyncData.done.load(std::memory_order_acquire)) {
		endPreload(true);
	}

	if (rapidBrowseMode && lastRapidBrowseNavigation > 0.0 &&
		timeNow - lastRapidBrowseNavigation >= rapidBrowseIdleTime) {
		const bool restore3D = rapidBrowseRestore3D;
		resetRapidBrowseState();
		if (restore3D) {
			if (!fileList.empty() && fileList[fileIndex].type == Color_Only) {
				callDepthGen(fileIndex);
			} else {
				setDisplay3D(true);
				if (!fileList.empty()) refreshDisplay3D(fileList[fileIndex].type);
			}
		}
	}

	static auto iconVisibilitySpeed = 16.0;

	for (auto& icon : appIcons) {
		auto iconTargetVisibility = 1.0;
		if (icon.type != IconType::Loading) {
			if (icon.state == IconState::Over) icon.visibility = iconTargetVisibility;
			if (icon.state == IconState::Idle) iconTargetVisibility = 0.0;
		} else {
			// Do not reveal the spinner while a preload is being promoted. That
			// handoff still performs synchronous image/GPU work; show it only once
			// the active depth request has actually started.
			iconTargetVisibility = ((isConverting || navigationLoadingIndicator) &&
				!isPlayingSlideshow) ? 1.0 : 0.0;
		}
		icon.visibility = Utils::tween(icon.visibility, iconTargetVisibility, iconVisibilitySpeed * deltaAverage);
		if (icon.type == IconType::File && fileList.empty() && !activeScreenCapture)
			icon.visibility = 1.0;
	}


	context.loadingRotation += (float)deltaAverage;
	if (context.loadingRotation > 2.0) context.loadingRotation = fmod(context.loadingRotation, 2.0f);

	static auto zoomSpeed = 16.0;

	if (wheelSpeed != 0) {
		static auto zoomFactor = 1.11;
		if (wheelSpeed > 0)
			targetZoom *= zoomFactor * wheelSpeed;
		else
			targetZoom /= zoomFactor * abs(wheelSpeed);
		targetZoom = glm::clamp(targetZoom, zoomMin, zoomMax);
		wheelSpeed = 0;
	}

	if (context.mode == Free_View_Grid || context.mode == Free_View_LRL)
		targetZoom = 1.0;

	currentZoom = Utils::tween(currentZoom, targetZoom, zoomSpeed * deltaAverage);

	auto oneHalf = glm::vec2(0.5);
	auto halfWindow = context.windowSize * oneHalf;
	auto halfImage = context.safeSize * oneHalf;
	auto zoomVec = glm::vec2((float)currentZoom, (float)currentZoom);
	auto imageWindowBounds = glm::abs(halfWindow - halfImage * zoomVec);

	context.currentZoom = currentZoom;

	auto motionScale = glm::vec2(1.0);
	if (display3D && preferredStereoMode == Free_View_Grid) {
		motionScale.x = 0.0;
		motionScale.y = 0.0;
	}
	imageWindowBounds = glm::abs(halfWindow - halfImage * zoomVec);
	context.imageBounds = imageWindowBounds;

	if (isDragging && !context.displayMenu) {
		context.offset += pixelMotion * motionScale;
	}

	context.offset = glm::clamp(context.offset,-imageWindowBounds, imageWindowBounds);

	context.gotoPrev = false;
	context.gotoNext = false;
	context.gotoRand = false;
	// Use the window's actual state for the render decision. The cached flag
	// can briefly be stale during startup before the first resize event.
	if (context.window != nullptr)
		isFullscreen = (SDL_GetWindowFlags(context.window) & SDL_WINDOW_FULLSCREEN) != 0;
	context.fullscreen = isFullscreen;
	if (preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half) {
		const auto fullscreenMode = isFullscreen ? preferredStereoMode :
			(context.imageType == Color_Only ? Native : Mono);
		if (context.mode != fullscreenMode) setStereoMode(fullscreenMode);
	}
	context.maximized = isMaximized;
	context.visibility = (float)currentVisibility;
	context.stereoStrength = currentStereoStrength;
	context.stereoDepth = currentStereoDepth;
	context.stereoOffset = currentStereoOffset;
	context.gridAngle = currentGridAngle;
	context.swapLeftRight = (int)swapLeftRight;
	context.windowSize = windowSize;
	context.safeSize = Image::updateRatio(&context, windowSize);

	auto visibleSize = windowSize;
	if (preferredStereoMode == SBS_Half || preferredStereoMode == RGB_Depth)
		visibleSize.x *= 0.5;
	if (doingFileOp) hideUI(false);

	if (Image::draw(&context) < 0) {
		SDL_Log("Image Draw Failed.");
		return SDL_APP_FAILURE;
	}

	pixelMotion = {0.0, 0.0 };

	auto mouseMoveSince = timeNow - mouseLastActive;
	if (mouseLastActive > 0.0 && ((mouseMoveSince > mouseMoveWait || mouseLeftWindow) &&
		!isIconCaptured)) {
		hideUI(true, true);
		mouseLastActive = 0.0;
	}

	if (isPlayingSlideshow) {
		depthEffect += deltaAverage / slideshowWaitTime;
		depthEffect = std::clamp(depthEffect, 0.0, 1.0);
		context.depthEffect = depthEffect;
		if (timeNow - lastSlideshowTime > slideshowWaitTime) {
			gotoRandomImage();
			lastSlideshowTime = timeNow;
		}
	}

	if (Image::displayTip) {
		if (timeNow - displayTipTime > displayTipWait) {
			Image::displayTip = false;
			doingFileOp = false;
		}
	}

	if (Image::displayInfo) {
		if (timeNow - displayInfoTime > displayTipWait) {
			Image::displayInfo = false;
			Image::infoTargetVisibility = 0.0;
		}
	}

	if (!callbackQueue.empty()) {
		auto& callback = callbackQueue.back();
		callback();
		callbackQueue.pop_back();
	}

	serviceDeferredDepthReload();

	if (doingFileOp && !openFolderResult.empty()) {
		if (openFolderResult == "ERROR") {
			doingFileOp = false;
		} else {
			auto fileListPath = std::filesystem::path(openFolderResult);
			if (is_directory(fileListPath)) {
				auto displayName = fileListPath.filename().string();
				if (displayName == exportFolderName) {
					Core::drawText(&context, "Invalid Batch Folder", Image::helpFont,
						Image::helpTexture, Image::helpTextSize, "Help Texture");
					Image::displayTip = true;
					displayTipTime = getTimeNow();
				} else {
					batchFolderPath = fileListPath;
					batchExportIndex = 0;
					batchExportActive = false;
					batchDepthGeneration = true;
					auto result = callDepthGenOnce(fileListPath.string(), BATCH_FOLDER, -1);
					if (result == 0) {
						isConverting = true;
						auto nameMaxLen = 18;
						if (displayName.length() > nameMaxLen) {
							displayName = displayName.substr(0, nameMaxLen - 3) + "...";
						}
						Core::drawText(&context, "Converting " + displayName + " to " +
							getExportDisplayName(),
							Image::helpFont, Image::helpTexture, Image::helpTextSize, "Help Texture");
						Image::displayTip = true;
						displayTipTime = getTimeNow();
					} else {
						batchDepthGeneration = false;
						isConverting = false;
					}
				}
			}
		}
		openFolderResult.clear();
	}

	if (depthGenerationError) {
		batchDepthGeneration = false;
		batchExportActive = false;
		context.loading = false;
		justConverted = false;
		isConverting = false;
		preloadDepthIndex = -1;
		isSpeculativeDepth = false;
		SDL_SetWindowTitle(context.window, context.appName);
		Core::drawText(&context, "Could Not Load Image", Image::helpFont, Image::helpTexture,
			Image::helpTextSize, "Help Texture");
		Image::displayHelp = true;
		depthGenerationError = false;
	}

	if (batchDepthGeneration && !depthGenAlive) {
		waitForDepthThread();
		batchDepthGeneration = false;
		if (!depthGenerationError) {
			batchInputPaths.clear();
			for (const auto& entry : std::filesystem::directory_iterator(batchFolderPath)) {
				if (entry.is_regular_file() && isSupportedImage(entry.path().string()))
					batchInputPaths.push_back(entry.path());
			}
			std::sort(batchInputPaths.begin(), batchInputPaths.end());
			batchExportIndex = 0;
			batchExportActive = true;
		}
	}
	processBatchExport();

	if (windowDraggable && isFullscreen) {
		auto mouseX = 0.0f, mouseY = 0.0f;
		auto mouseFlags = SDL_GetGlobalMouseState(&mouseX, &mouseY);
		if (mouseFlags & SDL_BUTTON_LMASK) {
			isFullscreen = false;
			setFullscreen(isFullscreen);
		}
	}

	if (quitAppNextFrame) return SDL_APP_SUCCESS;
	return SDL_APP_CONTINUE;
}

bool withinArea(glm::vec2 point, glm::vec2 topLeft, glm::vec2 bottomRight) {
	return point.x > topLeft.x && point.x < bottomRight.x &&
		point.y > topLeft.y && point.y < bottomRight.y;
}

glm::vec2 getCoordinates(glm::vec4 positionAlignment, glm::vec2 aspectScale = glm::vec2(1.0)) {
	auto position = glm::vec2(positionAlignment.x, positionAlignment.y);
	auto alignment = glm::vec2(positionAlignment.z, positionAlignment.w);
	return position * aspectScale * context.displayScale + alignment * windowSize;
}

static bool isInsideSliderTrack(const Icon& icon, const glm::vec2& aspectScale) {
	if (icon.mode != IconMode::Slider || icon.slider.size.x <= 0.0f) return false;
	const auto canvas = icon.canvas();
	const auto center = getCoordinates(glm::vec4(canvas.position, canvas.alignment), aspectScale);
	auto size = icon.slider.size * aspectScale * context.displayScale;
	// Keep the slider graphics unchanged while making every slider's centered
	// hit region twice as tall for easier adjustment.
	size.y *= 2.0f;
	return context.mouse.x > center.x - size.x * 0.5f &&
		context.mouse.x < center.x + size.x * 0.5f &&
		context.mouse.y > center.y - size.y * 0.5f &&
		context.mouse.y < center.y + size.y * 0.5f;
}

static double getSliderPercentAtMouse(const Icon& icon, const glm::vec2& aspectScale) {
	const auto canvas = icon.canvas();
	const auto center = getCoordinates(glm::vec4(canvas.position, canvas.alignment), aspectScale);
	const auto width = icon.slider.size.x * aspectScale.x * context.displayScale;
	return width > 0.0f ? glm::clamp(
		(context.mouse.x - (center.x - width * 0.5f)) / width, 0.0f, 1.0f) : 0.0;
}

void checkMouseState() {
	isIconCaptured = false;
	auto aspectScale = glm::vec2(1.0);
	if (preferredStereoMode == SBS_Full && isFullscreen)
		aspectScale = glm::vec2(2.0, 1.0);
	const bool currentSourceHasDepth = activeScreenCapture
		? videoDepthFrameLoaded || videoDepthProcessor.running()
		: (!fileList.empty() &&
		(fileList[fileIndex].type == Color_Plus_Depth ||
			fileList[fileIndex].type == Light_Field_LKG ||
			(activeVideo && videoDepthFrameLoaded)));
	for (auto& icon : appIcons) {
		auto displayLoading = !(icon.type == IconType::Loading && (!isConverting || isPlayingSlideshow));
		auto displaySettings = !(icon.type == IconType::Settings &&
			(!display3D || currentStereoMode == Depth_Zoom || preferredStereoMode == RGB_Depth ||
				!currentSourceHasDepth));
		auto displayStereo = !((icon.type == IconType::Glasses || icon.type == IconType::Focus ||
			icon.type == IconType::Layers) && (!showingStereoSettings || !display3D));
		auto displayParallax = !(icon.type == IconType::Focus &&
			(!fileList.empty() && fileList[fileIndex].type == Light_Field_LKG));
		auto displayCaptureFileActions = !(activeScreenCapture &&
			(icon.type == IconType::File || icon.type == IconType::Folder ||
				icon.type == IconType::Save));
		auto displayOpen = !((icon.type != IconType::File && icon.type != IconType::Close &&
			icon.type != IconType::Minimize &&
			icon.type != IconType::Crop)
			&& fileList.empty() && !activeScreenCapture);
		auto displayMenu = !(context.displayMenu && (icon.type == IconType::Forward || icon.type == IconType::Back ||
			icon.type == IconType::File || icon.type == IconType::Folder || icon.type == IconType::Save || icon.type == IconType::Window ||
			icon.type == IconType::Fullscreen || icon.type == IconType::Play || icon.type == IconType::Pause ||
			icon.type == IconType::Crop ||
			icon.type == IconType::VideoSeek || icon.type == IconType::VideoVolume ||
			icon.type == IconType::VideoAudio || icon.type == IconType::VideoCaption ||
			icon.type == IconType::Settings || icon.type == IconType::Info));
		auto displayXD = !((context.displayMenu || preferredStereoMode == Mono) && icon.type == IconType::Stereo_3D);
		auto displaySave = true;
		if (!fileList.empty()) {
			const bool videoDepthReady = activeVideo && videoDepthFrameLoaded;
			displaySave = !(icon.type == IconType::Save &&
			(((fileList[fileIndex].type == Color_Only ||
				fileList[fileIndex].type == Color_Anaglyph) && !videoDepthReady) ||
				(fileList[fileIndex].type != Color_Plus_Depth &&
					exportFormat == Color_Plus_Depth && !videoDepthReady)));
		}
		if (withinArea(context.mouse, getCoordinates(icon.canvas().topLeft, aspectScale),
			getCoordinates(icon.canvas().bottomRight, aspectScale))) {
			if (icon.type == IconType::Folder)
				icon.label = "Batch Convert to " + getExportDisplayName();
			icon.state = IconState::Near;
			const bool videoOnlyControl = icon.type == IconType::VideoSeek ||
				icon.type == IconType::VideoVolume || icon.type == IconType::VideoAudio ||
				icon.type == IconType::VideoCaption;
			const bool videoPlayControl = activeVideo &&
				(icon.type == IconType::Play || icon.type == IconType::Pause);
		const bool displayVideo = videoOnlyControl
				? activeVideo && videoControlsVisible
				: (!videoPlayControl || videoControlsVisible);
			const bool displayCaptureNavigation = !(activeScreenCapture &&
				(icon.type == IconType::Back || icon.type == IconType::Forward ||
					icon.type == IconType::Play || icon.type == IconType::Pause));
			const bool displayFileNavigation = fileList.size() > 1 ||
				(icon.type != IconType::Back && icon.type != IconType::Forward);
			icon.active = displayLoading && displaySettings && displayStereo && displayParallax &&
				displayOpen && displaySave && displayMenu && displayXD && displayVideo &&
				displayCaptureFileActions && displayFileNavigation;
			icon.active = icon.active && displayCaptureNavigation;
			if (icon.type == IconType::Loading) icon.active = true;
			auto iconPosition = getCoordinates(
				{icon.canvas().position * aspectScale
					+ icon.slider.position * aspectScale,
					icon.canvas().alignment});
			auto iconSize = glm::vec2(icon.canvas().size.x);
			iconSize *=	aspectScale * context.displayScale;
			auto iconAspect = iconSize.x / iconSize.y;
			auto iconX = (context.mouse.x - iconPosition.x);
			auto iconY = (context.mouse.y - iconPosition.y) * iconAspect;
			auto isInsideIcon = iconX * iconX + iconY * iconY <= iconSize.x * iconSize.x;
			auto isInsideSlider = isInsideSliderTrack(icon, aspectScale);
			if (((isInsideIcon || isInsideSlider) && icon.active) || &icon == currentSlider) {
				if (icon.type != IconType::Loading) {
					icon.state = IconState::Over;
					isIconCaptured = true;
					if (displayInfoEnabled && icon.active && (!icon.shown ||
						icon.mode == IconMode::Slider)) {
						auto doShowInfo = true;
						if (icon.label != currentInfoLabel) {
							currentInfoLabel = icon.label;
							if (activeVideo && icon.type == IconType::VideoAudio)
								currentInfoLabel += " : " + filterInfoFontText(videoPlayer.audioLanguage());
							else if (activeVideo && icon.type == IconType::VideoCaption)
								currentInfoLabel += " : " + filterInfoFontText(videoPlayer.subtitleLanguage());
							if ((icon.type == IconType::Back || icon.type == IconType::Forward) &&
									(!fileList.empty() && fileList.size() > fileIndex)) {
								currentInfoLabel = Core::getFileText(fileList[fileIndex], context.imageSize);
							}
							if (icon.mode == IconMode::Slider) {
								if (activeVideo && icon.type == IconType::VideoSeek)
									currentInfoLabel = formatVideoTimecode(icon);
								else
									currentInfoLabel += " : " +
										std::to_string(int(getSliderPercent(icon) * 100)) + "%";
							}
							if (doShowInfo) {
								Core::drawText(&context, currentInfoLabel, Image::infoFont, Image::infoTexture,
										Image::infoTextSize, "Info Texture");
							}
							if (showDisplayInfoOnce) icon.shown = true;
						}
						if (doShowInfo) {
							Image::displayInfo = true;
							Image::infoTargetVisibility = 1.0;
							displayInfoTime = getTimeNow();
						}
					}
				}
			}
		} else {
			if (&icon == currentSlider) {
				icon.state = IconState::Over;
				icon.active = true;
				isIconCaptured = true;
				continue;
			}
			if (icon.type != IconType::Loading &&
				!(icon.type == IconType::File && fileList.empty() && !activeScreenCapture)) {
				icon.state = IconState::Idle;
				icon.active = false;
			} else {
				icon.active = true;
			}
		}
	}

	if (!isIconCaptured) {
		Image::displayInfo = false;
		Image::infoTargetVisibility = 0.0;
	}

	if (context.displayMenu) {
		static glm::vec3 buttonBorder{ 12.0, 6.0, 0.0 };
		for (const auto& choice : *context.menuChoices) {
			auto optionIndex = 0;
			(*context.menuRollover)[choice.label] = -1;
			for (const auto& option : choice.options) {
				if (withinArea(glm::vec2(context.mouse.x, windowSize.y - context.mouse.y),
					choice.layouts[optionIndex].position * glm::vec3(1.0)
					- choice.layouts[optionIndex].size * glm::vec3(0.5) *
					glm::vec3(aspectScale, 1.0) - buttonBorder + Image::menuMargin,
					choice.layouts[optionIndex].position * glm::vec3(1.0)
					+ choice.layouts[optionIndex].size * glm::vec3(0.5) *
					glm::vec3(aspectScale, 1.0)
					+ buttonBorder + Image::menuMargin)) {
					(*context.menuRollover)[choice.label] = optionIndex;
				}
				optionIndex++;
			}
		}
	}
}

void hideUI(bool hideCustomMouse, bool hideRealMouse) {
	isIconCaptured = false;
	for (auto& icon : appIcons) {
		icon.state = IconState::Idle;
		if (icon.type != IconType::Loading) icon.visibility = 0.0;
	}
	if (hideCustomMouse) {
		context.mouseVisibility = 0.0;
		context.mouse.x = mouseValueNull;
		context.mouse.y = mouseValueNull;
		mouseMoveDelay = mouseDelayCount;
	}
	if (hideRealMouse && SDL_CursorVisible()) {
		SDL_HideCursor();
	}
}

static bool shouldShowCustomCursor() {
	if (isFullscreen) return true;
	return isFullscreen &&
		(preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half
			|| preferredStereoMode == RGB_Depth);
}

static void showCustomCursor(bool show) {
	if (shouldShowCustomCursor() && show) {
		if (mouseMoveDelay < 0)
			context.mouseVisibility = 1.0;
		if (SDL_CursorVisible()) SDL_HideCursor();
	} else {
		context.mouseVisibility = 0.0;
		if (!SDL_CursorVisible()) SDL_ShowCursor();
	}
}

struct NativeDepthRequest {
	std::filesystem::path input;
	int mode;
	int imageId;
	std::uint64_t generation;
	SDL_Surface* surface = nullptr;
};

struct NativeDepthCompletion {
	std::string path;
	int imageId;
	std::uint64_t generation;
};

static std::mutex depthCompletionMutex;
static std::deque<NativeDepthCompletion> depthCompletions;

struct NativeGpuUpscaleRequest {
	SDL_Surface* source = nullptr;
	const SDL_Surface* guide = nullptr;
	int width = 0;
	int height = 0;
	SDL_Surface* result = nullptr;
	bool complete = false;
};

static SDL_Surface* upscaleNativeSurfaceOnRenderThread(SDL_Surface* source,
		int width, int height, const SDL_Surface* guide = nullptr) {
	if (source == nullptr) return nullptr;
	if (SDL_IsMainThread()) {
		// App callbacks and video presentation run on the render thread. They
		// cannot wait for serviceNativeGpuUpscale(), which is only reached at
		// the frame boundary. Execute directly when the caller can safely own
		// the GPU work; background depth workers use the queued path below.
		return guide != nullptr
			? Image::refineDepthSurfaceGPU(&context, guide, source, width, height)
			: Image::upscaleSurfaceGPU(&context, source, width, height);
	}
	NativeGpuUpscaleRequest request{.source = source, .guide = guide,
		.width = width, .height = height};
	{
		std::lock_guard lock(nativeGpuUpscaleMutex);
		pendingNativeGpuUpscale = &request;
	}
	nativeGpuUpscaleCondition.notify_all();

	std::unique_lock lock(nativeGpuUpscaleMutex);
	nativeGpuUpscaleCondition.wait(lock, [&request] { return request.complete; });
	return request.result;
}

static void serviceNativeGpuUpscale() {
	NativeGpuUpscaleRequest* request = nullptr;
	{
		std::lock_guard lock(nativeGpuUpscaleMutex);
		request = pendingNativeGpuUpscale;
		pendingNativeGpuUpscale = nullptr;
	}
	if (request == nullptr) return;

	request->result = request->guide != nullptr
		? Image::refineDepthSurfaceGPU(&context, request->guide, request->source,
			request->width, request->height)
		: Image::upscaleSurfaceGPU(&context, request->source,
			request->width, request->height);
	{
		std::lock_guard lock(nativeGpuUpscaleMutex);
		request->complete = true;
	}
	nativeGpuUpscaleCondition.notify_all();
}

static glm::vec3 sampleNativeRgb(const SDL_Surface* surface, int x, int y) {
	if (surface == nullptr || surface->w <= 0 || surface->h <= 0)
		return glm::vec3(0.0f);
	x = std::clamp(x, 0, surface->w - 1);
	y = std::clamp(y, 0, surface->h - 1);
	const auto* format = SDL_GetPixelFormatDetails(surface->format);
	const auto pixel = reinterpret_cast<const Uint32*>(
		static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch) + x;
	Uint8 r = 0, g = 0, b = 0, a = 0;
	SDL_GetRGBA(*pixel, format, nullptr, &r, &g, &b, &a);
	return glm::vec3(r, g, b) / 255.0f;
}

// Reconstruct depth at the output resolution while using RGB edges as the
// guide. The final weighted-mode pass removes isolated depth estimates without
// averaging across strong color boundaries.
static SDL_Surface* makeNativeDepthSurfaceForOutput(const DepthEstimator::Result& depth,
		const SDL_Surface* color, int width, int height) {
	if (!depth.valid() || color == nullptr || width <= 0 || height <= 0)
		return nullptr;

	const auto* format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
	const auto minMax = std::minmax_element(depth.values.begin(), depth.values.end());
	const float minimum = *minMax.first;
	const float range = std::max(*minMax.second - minimum,
		std::numeric_limits<float>::epsilon());
	std::vector<float> normalized(depth.values.size());
	for (size_t index = 0; index < depth.values.size(); ++index) {
		const float value = std::isfinite(depth.values[index])
			? depth.values[index] : minimum;
		normalized[index] = std::clamp((value - minimum) / range, 0.0f, 1.0f);
	}

	// The expensive edge-aware reconstruction is done at model resolution.
	// The existing GPU scaler then enlarges this refined map to the requested
	// output size. Running the 5x5 pass at 4K would make conversion needlessly
	// expensive (and would block shutdown while the worker finishes).
	SDL_Surface* modelOutput = SDL_CreateSurface(depth.width, depth.height,
		SDL_PIXELFORMAT_RGBA32);
	if (modelOutput == nullptr) return nullptr;

	constexpr int radius = 2;
	constexpr float spatialSigma = 1.35f;
	constexpr float colorSigma = 0.12f;
	constexpr float depthSigma = 0.06f;
	const float spatialDenominator = 2.0f * spatialSigma * spatialSigma;
	const float colorDenominator = 2.0f * colorSigma * colorSigma;
	const float depthDenominator = 2.0f * depthSigma * depthSigma;

	for (int y = 0; y < depth.height; ++y) {
		for (int x = 0; x < depth.width; ++x) {
			const int centerX = x;
			const int centerY = y;
			const glm::vec3 guide = sampleNativeRgb(color,
				static_cast<int>((x + 0.5f) * color->w / depth.width),
				static_cast<int>((y + 0.5f) * color->h / depth.height));

			std::array<float, 25> candidates{};
			std::array<float, 25> weights{};
			int sampleCount = 0;
			for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
				for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
					const int sampleX = std::clamp(centerX + offsetX, 0, depth.width - 1);
					const int sampleY = std::clamp(centerY + offsetY, 0, depth.height - 1);
					const float candidate = normalized[static_cast<size_t>(sampleY) * depth.width + sampleX];
					const glm::vec3 candidateGuide = sampleNativeRgb(color,
						static_cast<int>((sampleX + 0.5f) * color->w / depth.width),
						static_cast<int>((sampleY + 0.5f) * color->h / depth.height));
					const float spatialDistance = static_cast<float>(offsetX * offsetX + offsetY * offsetY);
					const float colorDistance = glm::dot(guide - candidateGuide,
						guide - candidateGuide);
					weights[sampleCount] = std::exp(-spatialDistance / spatialDenominator -
						colorDistance / colorDenominator);
					candidates[sampleCount++] = candidate;
				}
			}

			// Use a continuous weighted median instead of selecting a quantized
			// mode bin. The preliminary median gives the depth-consistency gate a
			// robust reference, so samples across a large disparity are not blended
			// into the surface being reconstructed.
			auto weightedMedian = [](const std::array<float, 25>& values,
				const std::array<float, 25>& sampleWeights) {
				std::array<float, 25> sortedValues = values;
				std::array<float, 25> sortedWeights = sampleWeights;
				for (int i = 1; i < 25; ++i) {
					const float value = sortedValues[i];
					const float weight = sortedWeights[i];
					int j = i - 1;
					while (j >= 0 && sortedValues[j] > value) {
						sortedValues[j + 1] = sortedValues[j];
						sortedWeights[j + 1] = sortedWeights[j];
						--j;
					}
					sortedValues[j + 1] = value;
					sortedWeights[j + 1] = weight;
				}
				float totalWeight = 0.0f;
				for (const float weight : sortedWeights) totalWeight += weight;
				const float target = totalWeight * 0.5f;
				float accumulated = 0.0f;
				float previous = sortedValues[0];
				for (int i = 0; i < 25; ++i) {
					const float nextAccumulated = accumulated + sortedWeights[i];
					if (nextAccumulated >= target) {
						const float fraction = sortedWeights[i] > 0.0f
							? (target - accumulated) / sortedWeights[i] : 0.0f;
						return previous + (sortedValues[i] - previous) *
							std::clamp(fraction, 0.0f, 1.0f);
					}
					accumulated = nextAccumulated;
					previous = sortedValues[i];
				}
				return sortedValues[24];
			};
			const float preliminaryMedian = weightedMedian(candidates, weights);
			for (int index = 0; index < sampleCount; ++index) {
				const float depthDifference = candidates[index] - preliminaryMedian;
				weights[index] *= std::exp(-(depthDifference * depthDifference) /
					depthDenominator);
			}
			const float refined = weightedMedian(candidates, weights);
			const Uint8 value = static_cast<Uint8>(std::lround(
				std::clamp(refined, 0.0f, 1.0f) * 255.0f));
			reinterpret_cast<Uint32*>(static_cast<Uint8*>(modelOutput->pixels) +
				static_cast<size_t>(y) * modelOutput->pitch)[x] =
				SDL_MapRGBA(format, nullptr, value, value, value, 255);
		}
	}
	// Joint bilateral reconstruction and the final upscale run in one GPU pass;
	// the CPU only prepares the small model-resolution fallback map.
	SDL_Surface* result = upscaleNativeSurfaceOnRenderThread(
		modelOutput, width, height, color);
	SDL_DestroySurface(modelOutput);
	if (result == nullptr) SDL_Log("GPU depth upscale failed.");
	return result;
}

static glm::ivec2 getNativeDepthSize(int sourceWidth, int sourceHeight) {
	if (sourceWidth <= 0 || sourceHeight <= 0) return { 0, 0 };

	int maximum = 3840;
	try {
		maximum = std::clamp(std::stoi(upscaleResolution), 1920, 3840);
	} catch (...) {
	}
	if (maximum <= 0) return { sourceWidth, sourceHeight };

	const auto longestSide = std::max(sourceWidth, sourceHeight);
	// The selected preset is a minimum output long edge. Preserve larger
	// sources, but never create output above the UHD cap.
	const int targetLongestSide = std::min(
		std::max(longestSide, maximum), 3840);
	const double scale = static_cast<double>(targetLongestSide) / longestSide;
	return {
		std::max(1, static_cast<int>(std::lround(sourceWidth * scale))),
		std::max(1, static_cast<int>(std::lround(sourceHeight * scale)))
	};
}

static SDL_Surface* makeNativeRgbdSurface(const SDL_Surface* color,
		const SDL_Surface* depth, int width, int height) {
	SDL_Surface* output = SDL_CreateSurface(width * 2, height, SDL_PIXELFORMAT_RGBA32);
	if (!output) return nullptr;

	SDL_Surface* resizedColor = upscaleNativeSurfaceOnRenderThread(
		const_cast<SDL_Surface*>(color), width, height);
	if (!resizedColor) {
		SDL_DestroySurface(output);
		SDL_Log("GPU color upscale failed.");
		return nullptr;
	}

	for (int y = 0; y < height; ++y) {
		SDL_memcpy(static_cast<Uint8*>(output->pixels) + y * output->pitch,
			static_cast<const Uint8*>(resizedColor->pixels) + y * resizedColor->pitch,
			static_cast<size_t>(width) * 4);
		SDL_memcpy(static_cast<Uint8*>(output->pixels) + y * output->pitch + width * 4,
			static_cast<const Uint8*>(depth->pixels) + y * depth->pitch,
			static_cast<size_t>(width) * 4);
	}
	SDL_DestroySurface(resizedColor);
	return output;
}

static std::filesystem::path nativeDepthOutputPath(const std::filesystem::path& input) {
	return input.parent_path() / exportFolderName /
		(input.stem().string() + "_rgbd.jpg");
}

static bool saveQoiSurface(const SDL_Surface* surface,
	const std::filesystem::path& outputPath) {
	if (surface == nullptr || surface->format != SDL_PIXELFORMAT_RGBA32) return false;

	const size_t rowBytes = static_cast<size_t>(surface->w) * 4;
	const Uint8* pixels = static_cast<const Uint8*>(surface->pixels);
	std::vector<Uint8> packedPixels;
	if (surface->pitch != static_cast<int>(rowBytes)) {
		packedPixels.resize(rowBytes * static_cast<size_t>(surface->h));
		for (int y = 0; y < surface->h; ++y) {
			SDL_memcpy(packedPixels.data() + static_cast<size_t>(y) * rowBytes,
				pixels + static_cast<size_t>(y) * surface->pitch, rowBytes);
		}
		pixels = packedPixels.data();
	}

	const qoi_desc description{
		static_cast<unsigned int>(surface->w),
		static_cast<unsigned int>(surface->h),
		4,
		QOI_SRGB
	};
	int encodedSize = 0;
	void* encoded = qoi_encode(pixels, &description, &encodedSize);
	if (encoded == nullptr || encodedSize <= 0) {
		if (encoded != nullptr) SDL_free(encoded);
		return false;
	}

	SDL_IOStream* output = SDL_IOFromFile(outputPath.string().c_str(), "wb");
	const size_t written = output == nullptr ? 0 :
		SDL_WriteIO(output, encoded, static_cast<size_t>(encodedSize));
	const bool closed = output == nullptr || SDL_CloseIO(output);
	SDL_free(encoded);
	return output != nullptr && written == static_cast<size_t>(encodedSize) && closed;
}

static bool saveTemporaryRgbdSurface(SDL_Surface* surface,
	const std::filesystem::path& outputPath) {
	if (surface == nullptr) return false;
	if (losslessDepthmaps) {
		return saveQoiSurface(surface, outputPath);
	}
	return IMG_SaveJPG(surface, outputPath.string().c_str(), 90);
}

static void cleanupStaleRuntimeDepthDirectories(
	const std::filesystem::path& runtimeRoot) {
	std::error_code error;
	if (!std::filesystem::is_directory(runtimeRoot, error)) return;

	const auto now = std::filesystem::file_time_type::clock::now();
	constexpr auto maximumAge = std::chrono::hours(24);
	for (const auto& entry :
		std::filesystem::directory_iterator(runtimeRoot, error)) {
		if (error) break;

		std::error_code entryError;
		if (!entry.is_directory(entryError)) continue;
		const auto name = entry.path().filename().string();
		if (name.rfind("Rendepth-", 0) != 0) continue;

		const auto modified = entry.last_write_time(entryError);
		if (entryError || modified > now || now - modified <= maximumAge) continue;

		std::filesystem::remove_all(entry.path(), entryError);
		if (entryError) {
			SDL_Log("Could not remove stale runtime depth directory %s: %s",
				entry.path().string().c_str(), entryError.message().c_str());
		}
	}
}

static std::filesystem::path runtimeDepthDirectory() {
	static const auto directory = [] {
		std::filesystem::path baseDirectory;
		char* prefPath = SDL_GetPrefPath("Outmode", "Rendepth");
		if (prefPath != nullptr) {
			baseDirectory = prefPath;
			SDL_free(prefPath);
		}
		if (baseDirectory.empty()) {
			std::error_code tempError;
			baseDirectory = std::filesystem::temp_directory_path(tempError);
		}
		if (baseDirectory.empty()) {
			SDL_Log("Could not find a writable directory for runtime depth data.");
			return std::filesystem::path{};
		}

		const auto runtimeRoot = baseDirectory / "Runtime";
		cleanupStaleRuntimeDepthDirectories(runtimeRoot);

		const auto sessionId = std::to_string(
			std::chrono::high_resolution_clock::now().time_since_epoch().count());
		const auto directory = runtimeRoot / ("Rendepth-" + sessionId);
		std::error_code createError;
		std::filesystem::create_directories(directory, createError);
		if (createError) {
			SDL_Log("Could not create runtime depth directory: %s", createError.message().c_str());
			return std::filesystem::path{};
		}
		return directory;
	}();
	return directory;
}

static std::filesystem::path runtimeDepthOutputPath(const std::filesystem::path& input) {
	const auto directory = runtimeDepthDirectory();
	return directory.empty() ? std::filesystem::path{} :
		directory / (input.stem().string() + "_rgbd" + (losslessDepthmaps ? ".qoi" : ".jpg"));
}

static int nativeSuperResolutionScale(const SDL_Surface* color) {
	if (color == nullptr) return 0;
	const int longestSide = std::max(color->w, color->h);
	if (longestSide <= 720) return 4;
	return 0;
}

static SDL_Surface* maybeSuperResolveNativeColor(SDL_Surface* color,
	StereoFormat sourceType) {
	if (color == nullptr || sourceType == Color_Plus_Depth) return color;
	std::lock_guard lock(nativeSuperResolutionMutex);
	const int scale = nativeSuperResolutionScale(color);
	if (scale == 0) return color;

	if (!nativeSuperResolutionAttempted) {
		nativeSuperResolutionAttempted = true;
		SuperResolution::Config config;
		const auto modelDirectoryPath = modelDirectory.empty()
			? homePath / "Models" : std::filesystem::path(modelDirectory);
		config.modelPath = ModelDownloader::ensureAvailable(
			modelDirectoryPath, "RFDN_x4.onnx", nativeSuperResolutionError);
	#ifdef RENDEPTH_ENABLE_CUDA
		config.provider = DepthEstimator::Provider::CUDA;
	#elif defined(RENDEPTH_ENABLE_ROCM)
		config.provider = DepthEstimator::Provider::ROCM;
	#endif
		if (!config.modelPath.empty())
			nativeSuperResolutionLoaded = nativeSuperResolution.load(
				config, nativeSuperResolutionError);
		if (nativeSuperResolutionLoaded)
			SDL_Log("Native RFDN x%d model loaded at %s using %s provider.", scale,
				config.modelPath.string().c_str(),
				nativeSuperResolution.providerName().c_str());
		else
			SDL_Log("Native RFDN x%d model unavailable; using original color: %s", scale,
				nativeSuperResolutionError.c_str());
	}
	if (!nativeSuperResolutionLoaded) return color;

	std::string error;
	SDL_Surface* result = nativeSuperResolution.predict(color, error);
	if (result == nullptr) {
		SDL_Log("Native RFDN x%d inference failed; using original color: %s", scale,
			error.c_str());
		return color;
	}
	SDL_DestroySurface(color);
	return result;
}

static SDL_Surface* upscaleNativeColorSurface(SDL_Surface* color) {
	if (color == nullptr) return color;
	const auto outputSize = getNativeDepthSize(color->w, color->h);
	if (outputSize.x == color->w && outputSize.y == color->h) return color;
	SDL_Surface* result = upscaleNativeSurfaceOnRenderThread(
		color, outputSize.x, outputSize.y);
	if (result == nullptr) {
		SDL_Log("GPU color upscale failed; using the available color resolution.");
		return color;
	}
	SDL_DestroySurface(color);
	return result;
}

static SDL_Surface* prepareNativeColorSurface(SDL_Surface* color,
	StereoFormat sourceType) {
	if (color == nullptr || sourceType == Color_Plus_Depth) return color;
	color = maybeSuperResolveNativeColor(color, sourceType);
	return upscaleNativeColorSurface(color);
}

static std::shared_ptr<VideoFrame> prepareVideoFrameForDisplay(
	const std::shared_ptr<VideoFrame>& frame) {
	if (!activeVideo || frame == nullptr || fileList.empty() ||
		fileList[fileIndex].type == Color_Plus_Depth ||
		(fileList[fileIndex].type == Color_Only && videoDepthProcessor.running()) ||
		frame->width <= 0 || frame->height <= 0 ||
		frame->inferenceWidth != frame->width ||
		frame->inferenceHeight != frame->height ||
		frame->inferenceRGBA.size() < static_cast<size_t>(frame->width) *
			frame->height * 4) return nullptr;

	SDL_Surface* source = SDL_CreateSurfaceFrom(frame->inferenceWidth,
		frame->inferenceHeight, SDL_PIXELFORMAT_RGBA32,
		const_cast<std::uint8_t*>(frame->inferenceRGBA.data()),
		frame->inferenceWidth * 4);
	if (source == nullptr) return nullptr;
	SDL_Surface* color = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(source);
	if (color == nullptr) return nullptr;
	// Video color must use the GPU upscale/filter path, but SR is reserved for
	// still images and native image conversion.
	color = upscaleNativeColorSurface(color);

	auto result = std::make_shared<VideoFrame>();
	result->format = VideoFrame::Format::RGBA;
	result->colorSpace = frame->colorSpace;
	result->fullRange = frame->fullRange;
	result->width = frame->width;
	result->height = frame->height;
	result->outputWidth = color->w;
	result->outputHeight = color->h;
	result->presentationTime = frame->presentationTime;
	result->generation = frame->generation;
	result->planes[0].resize(static_cast<size_t>(color->w) * color->h * 4);
	for (int y = 0; y < color->h; ++y)
		SDL_memcpy(result->planes[0].data() + static_cast<size_t>(y) * color->w * 4,
			static_cast<const std::uint8_t*>(color->pixels) +
				static_cast<size_t>(y) * color->pitch,
			static_cast<size_t>(color->w) * 4);
	SDL_DestroySurface(color);
	return result;
}

static int nativeDepthRun(void* ptr) {
	auto request = static_cast<NativeDepthRequest*>(ptr);
	const auto inputPath = request->input;
	const auto mode = request->mode;
	const auto imageId = request->imageId;
	const auto generation = request->generation;
	auto* suppliedSurface = request->surface;
	delete request;

	const auto modelOption = std::clamp(
		menuSelection[ChoiceModel.label], 0, static_cast<int>(depthModelFiles.size()) - 1);
	if (!nativeDepthEstimatorLoaded) {
		DepthEstimator::Config config;
		const auto modelDirectoryPath = modelDirectory.empty()
			? homePath / "Models" : std::filesystem::path(modelDirectory);
		config.modelPath = ModelDownloader::ensureAvailable(
			modelDirectoryPath, depthModelFiles[modelOption], nativeDepthEstimatorError);
		if (config.modelPath.empty()) {
			SDL_DestroySurface(suppliedSurface);
			std::cerr << "Native depth model download failed: "
				<< nativeDepthEstimatorError << '\n';
			depthGenerationError = true;
			depthGenAlive = false;
			return 0;
		}
	#ifdef RENDEPTH_ENABLE_CUDA
		config.provider = DepthEstimator::Provider::CUDA;
	#elif defined(RENDEPTH_ENABLE_ROCM)
		config.provider = DepthEstimator::Provider::ROCM;
	#endif
		config.processSize = depthProcessSizes[modelOption];
		nativeDepthEstimatorLoaded = nativeDepthEstimator.load(
			config, nativeDepthEstimatorError);
		if (nativeDepthEstimatorLoaded) {
			SDL_Log("Native depth model loaded: %s at %dx%d using %s provider.",
				config.modelPath.string().c_str(), config.processSize, config.processSize,
				nativeDepthEstimator.providerName().c_str());
		}
	}
	if (!nativeDepthEstimatorLoaded) {
		SDL_DestroySurface(suppliedSurface);
		std::cerr << "Native depth model failed to load: "
			<< nativeDepthEstimatorError << '\n';
		depthGenerationError = true;
		depthGenAlive = false;
		return 0;
	}

	if (mode == BATCH_FOLDER) {
		for (const auto& entry : std::filesystem::directory_iterator(inputPath)) {
			if (isSupportedImage(entry.path().string())) {
				auto batchRequest = NativeDepthRequest{entry.path(), SINGLE_IMAGE, -1};
				SDL_Surface* loaded = Core::loadImageDirect(batchRequest.input.string());
				if (!loaded) continue;
				SDL_Surface* color = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
				SDL_DestroySurface(loaded);
				if (!color) continue;
				color = maybeSuperResolveNativeColor(color,
					Core::getImageType(batchRequest.input.string()));
				std::string error;
				auto depth = nativeDepthEstimator.predict(color, error);
				if (depth.valid()) {
					const auto outputSize = getNativeDepthSize(color->w, color->h);
					SDL_Surface* depthSurface = makeNativeDepthSurfaceForOutput(
						depth, color, outputSize.x, outputSize.y);
					SDL_Surface* output = depthSurface
						? makeNativeRgbdSurface(color, depthSurface, outputSize.x, outputSize.y) : nullptr;
					if (output) {
						const auto result = runtimeDepthOutputPath(batchRequest.input);
						create_directories(result.parent_path());
						saveTemporaryRgbdSurface(output, result);
					}
					SDL_DestroySurface(output);
					SDL_DestroySurface(depthSurface);
				}
				SDL_DestroySurface(color);
			}
		}
		depthGenAlive = false;
		return 0;
	}

	SDL_Surface* color = suppliedSurface != nullptr
		? SDL_ConvertSurface(suppliedSurface, SDL_PIXELFORMAT_RGBA32) : nullptr;
	if (suppliedSurface != nullptr) SDL_DestroySurface(suppliedSurface);
	if (color == nullptr && suppliedSurface == nullptr) {
		SDL_Surface* loaded = Core::loadImageDirect(inputPath.string());
		if (loaded != nullptr) {
			color = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
			SDL_DestroySurface(loaded);
		}
	}
	if (!color) {
		depthGenerationError = true;
		depthGenAlive = false;
		return 0;
	}
	color = maybeSuperResolveNativeColor(color, Core::getImageType(inputPath.string()));

	std::string error;
	auto depth = nativeDepthEstimator.predict(color, error);
	const auto outputSize = getNativeDepthSize(color->w, color->h);
	SDL_Surface* depthSurface = depth.valid()
		? makeNativeDepthSurfaceForOutput(depth, color, outputSize.x, outputSize.y) : nullptr;
	SDL_Surface* output = depthSurface
		? makeNativeRgbdSurface(color, depthSurface, outputSize.x, outputSize.y) : nullptr;
	const auto result = mode == REAL_TIME
		? runtimeDepthOutputPath(inputPath)
		: nativeDepthOutputPath(inputPath);
	bool saved = false;
	if (output && !result.empty()) {
		create_directories(result.parent_path());
		saved = mode == REAL_TIME
			? saveTemporaryRgbdSurface(output, result)
			: IMG_SaveJPG(output, result.string().c_str(), 90);
	}
	SDL_DestroySurface(output);
	SDL_DestroySurface(depthSurface);
	SDL_DestroySurface(color);

	if (saved) {
		conversionCompleted(result.string().c_str(), imageId, generation);
	} else {
		std::cerr << "Native depth inference failed: " << error << '\n';
		depthGenerationError = true;
	}
	depthGenAlive = false;
	return 0;
}

static void conversionCompleted(const char* path, int imageId, std::uint64_t generation) {
	std::lock_guard lock(depthCompletionMutex);
	depthCompletions.push_back({path, imageId, generation});
}

static void serviceDepthCompletions() {
	std::deque<NativeDepthCompletion> completions;
	{
		std::lock_guard lock(depthCompletionMutex);
		completions.swap(depthCompletions);
	}
	for (const auto& completion : completions) {
		if (completion.generation != activeDepthGeneration) continue;
		const auto& path = completion.path;
		const auto imageId = completion.imageId;
	if (isSpeculativeDepth && imageId == preloadDepthIndex &&
		imageId >= 0 && imageId < static_cast<int>(fileList.size())) {
		if (imageId != fileIndex) {
			SDL_DestroySurface(fileList[imageId].preload);
			fileList[imageId].preload = nullptr;
			fileList[imageId].path = path;
			fileList[imageId].type = Core::getImageType(fileList[imageId].path);
			if (fileList[imageId].type == Unknown_Format)
				fileList[imageId].type = Color_Plus_Depth;
		}
		isSpeculativeDepth = false;
		preloadDepthIndex = -1;
		continue;
	}
	if (imageId != fileIndex && imageId >= 0 && imageId < static_cast<int>(fileList.size())) {
		SDL_DestroySurface(fileList[imageId].preload);
		fileList[imageId].preload = nullptr;
		fileList[imageId].path = path;
		fileList[imageId].type = Core::getImageType(fileList[imageId].path);
		if (fileList[imageId].type == Unknown_Format)
			fileList[imageId].type = Color_Plus_Depth;
		isConverting = false;
		preloadDepthIndex = -1;
		isSpeculativeDepth = false;
		continue;
	}
	if (imageId < 0 || imageId >= static_cast<int>(fileList.size())) continue;
	auto colorPath = std::filesystem::path(fileList[imageId].path).filename().replace_extension();;
	auto depthPath = std::filesystem::path(path).filename().replace_extension();;
	if (imageId == fileIndex) {
		fileList[imageId].path = path;
		fileList[imageId].type = Core::getImageType(fileList[imageId].path);
		if (fileList[imageId].type == Unknown_Format)
			fileList[imageId].type = Color_Plus_Depth;
		context.loading = false;
		setDisplay3D(true);
		refreshDisplay3D(fileList[imageId].type);
		loadImage(nullptr);
		navigationLoadingIndicator = false;
		justConverted = true;
		isConverting = false;
		rapidBrowseClicks = 0;
	}
	}
}

static int callDepthGenOnce(const std::string& fileFolderPath, int genMode, int imageId,
	SDL_Surface* inputSurface) {
	if (depthGenAlive.load(std::memory_order_acquire)) {
		nativeDepthEstimator.cancel();
	}
	waitForDepthThread();
	if (!nativeDepthEstimatorLoaded) {
		Core::drawText(&context, "Loading Depth Model, Please Wait",
			Image::helpFont, Image::helpTexture,
			Image::helpTextSize, "Help Texture");
		Image::displayTip = true;
		displayTipTime = getTimeNow();
	}

	depthGenAlive = true;
	activeDepthGeneration = ++nextDepthGeneration;
	auto request = new NativeDepthRequest{
		std::filesystem::path(fileFolderPath), genMode, imageId, activeDepthGeneration,
		inputSurface};
	depthGenThread = SDL_CreateThread(nativeDepthRun, "nativeDepthRun", request);
	if (depthGenThread == nullptr) {
		SDL_DestroySurface(request->surface);
		delete request;
		depthGenAlive = false;
		isConverting = false;
		depthGenerationError = true;
		isSpeculativeDepth = false;
		return 1;
	}
	return 0;
}

static void callDepthGen(int imageIndex, bool speculative, SDL_Surface* inputSurface) {
	if (!speculative) {
		// A preloaded image can be promoted to the current image while its
		// speculative depth job is still finishing. From this point onward the
		// current-image conversion owns completion handling; do not let its
		// result be mistaken for the old speculative request.
		if (isSpeculativeDepth && preloadDepthIndex == imageIndex && depthGenAlive) {
			isConverting = true;
			isSpeculativeDepth = false;
			preloadDepthIndex = -1;
			return;
		}
		isConverting = true;
		isSpeculativeDepth = false;
		preloadDepthIndex = -1;
	}
	if (callDepthGenOnce(fileList[imageIndex].link, REAL_TIME, imageIndex,
		inputSurface) != 0 && speculative) {
		isSpeculativeDepth = false;
		preloadDepthIndex = -1;
	}
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
	if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
		if (context.window == nullptr ||
			event->window.windowID != SDL_GetWindowID(context.window))
			return SDL_APP_CONTINUE;
		refreshWindowSize();

		auto currentDisplay = static_cast<SDL_DisplayID>(event->window.data1);
		if (currentDisplay == 0) return SDL_APP_CONTINUE;
		auto displayMode = SDL_GetCurrentDisplayMode(currentDisplay);
		if (displayMode == nullptr) return SDL_APP_CONTINUE;
		Image::currentDisplay = currentDisplay;
		auto virtualSize = glm::vec2(displayMode->w, displayMode->h) * displayMode->pixel_density;
		context.virtualSize = virtualSize;

		auto displaySize = glm::vec3((float)displayMode->w, (float)displayMode->h, 0.0f);
		context.displaySize = displaySize;
		showCustomCursor(false);
		auto displayScale = SDL_GetWindowDisplayScale(context.window);
		context.displayScale = displayScale;
		auto pixelDensity = SDL_GetWindowPixelDensity(context.window);
		context.pixelDensity = pixelDensity;
#if defined(__APPLE__)
		Image::mouseScale = pixelDensity;
#elif defined(__linux__) || defined(__unix__)
		Image::mouseScale = pixelDensity;
#else
		Image::mouseScale = 1.0;
#endif
		updateDisplayScale();
		if (isFullscreen) {
			Image::configureFullscreenMode(context.window, currentDisplay);
		}
		if (preferredStereoMode == Lenticular) {
			Image::initNativeOutput(&context);
		}
	} else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
		showCustomCursor(false);
		refreshWindowSizeBase();
		Image::updateSize(&context);
		Image::saveMenuLayout(&context);
		refreshWindowSize();
		hideUI();
	} else if (event->type == SDL_EVENT_WINDOW_RESTORED) {
		isMaximized = false;
		context.maximized = isMaximized;
		context.offset = glm::vec2(0.0f);
		Image::updateSize(&context);
		Image::saveMenuLayout(&context);
	} else if (event->type == SDL_EVENT_WINDOW_MINIMIZED) {
		isMaximized = false;
		context.maximized = isMaximized;
		context.offset = glm::vec2(0.0f);
		Image::updateSize(&context);
		Image::saveMenuLayout(&context);
	} else if (event->type == SDL_EVENT_WINDOW_MAXIMIZED) {
		isMaximized = true;
		context.maximized = isMaximized;
		context.offset = glm::vec2(0.0f);
		showCustomCursor(false);
		Image::updateSize(&context);
		Image::saveMenuLayout(&context);
	} else if (event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
		isFullscreen = true;
		updateFullscreenState();
	} else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
		isFullscreen = false;
		updateFullscreenState();
	} else if (event->type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
		mouseLeftWindow = false;
	} else if (event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
		mouseLeftWindow = true;
		if (currentSlider != nullptr || videoSliderScrubbing)
			finishSliderDrag();
		hideUI(false);
	} else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
		finishSliderDrag();
	} else if (event->type == SDL_EVENT_WINDOW_HIT_TEST) {
		} else if (event->type == SDL_EVENT_KEY_DOWN) {
		if (activeScreenCapture && event->key.key == SDLK_SPACE)
			return SDL_APP_CONTINUE;
		if (event->key.key == SDLK_ESCAPE) {
			if(context.displayMenu) {
				toggleOptions();
			} else if (isFullscreen) {
				isFullscreen = false;
				setFullscreen(isFullscreen);
			} else if (isMaximized) {
				isMaximized = false;
				setMaximize(isMaximized);
			} else if (!doingFileOp) {
				return SDL_APP_SUCCESS;
				}
			}

			if (event->key.key == SDLK_HOME && !event->key.repeat) {
				if (compileShadersForReload()) Image::reloadShader(&context);
			}

			if (event->key.key == SDLK_1) {
			changeStereo(0);
			menuSelection[ChoiceStereo.label] = 0;
		} else if (event->key.key == SDLK_2) {
			changeStereo(1);
			menuSelection[ChoiceStereo.label] = 1;
		} else if (event->key.key == SDLK_3) {
			changeStereo(2);
			menuSelection[ChoiceStereo.label] = 2;
		} else if (event->key.key == SDLK_4) {
			changeStereo(3);
			menuSelection[ChoiceStereo.label] = 3;
		} else if (event->key.key == SDLK_5) {
			changeStereo(4);
			menuSelection[ChoiceStereo.label] = 4;
		} else if (event->key.key == SDLK_6) {
			changeStereo(5);
			menuSelection[ChoiceStereo.label] = 5;
		} else if (event->key.key == SDLK_7) {
			changeStereo(6);
			menuSelection[ChoiceStereo.label] = 6;
			} else if (event->key.key == SDLK_8) {
				changeStereo(7);
				menuSelection[ChoiceStereo.label] = 7;
			} else if (event->key.key == SDLK_9) {
				changeStereo(8);
				menuSelection[ChoiceStereo.label] = 8;
			} else if (event->key.key == SDLK_0) {
				changeStereo(9);
				menuSelection[ChoiceStereo.label] = 9;
		}

		if (event->key.key == SDLK_MINUS || event->key.key == SDLK_KP_MINUS) {
			changeEyes(0);
			menuSelection[ChoiceEyes.label] = 0;
		} else if (event->key.key == SDLK_EQUALS || event->key.key == SDLK_KP_PLUS) {
			changeEyes(1);
			menuSelection[ChoiceEyes.label] = 1;
		}

		if (event->key.key == SDLK_SPACE) {
			if (!activeScreenCapture && !isConverting && !doingPreload && !fileList.empty()) {
				if (activeVideo) {
					if (!event->key.repeat) toggleSlideshow();
				} else {
					toggleStereo();
				}
			}
		} else if (event->key.key == SDLK_KP_5) {
			if (!activeScreenCapture && !isConverting && !doingPreload && !fileList.empty()) toggleStereo();
		}

		if (event->key.key == SDLK_RETURN && event->key.mod == SDL_KMOD_LALT) {
			toggleFullscreen();
		}

		if (event->key.key == SDLK_F || event->key.key == SDLK_KP_0 || event->key.key == SDLK_F11) {
			toggleFullscreen();
		}

		if (event->key.key == SDLK_E && !event->key.repeat) {
			if (!activeScreenCapture && !isConverting && !fileList.empty() && fileIndex < fileList.size()) {
				const bool videoDepthReady = activeVideo && videoDepthFrameLoaded;
				const bool canExport = !(((fileList[fileIndex].type == Color_Only ||
					fileList[fileIndex].type == Color_Anaglyph) && !videoDepthReady) ||
					(fileList[fileIndex].type != Color_Plus_Depth &&
						exportFormat == Color_Plus_Depth && !videoDepthReady));
				if (canExport) {
					if (isPlayingSlideshow) {
						if (currentStereoMode == Depth_Zoom) {
							preferredStereoMode = defaultStereoMode;
						}
						setSlideshow(false);
						if (doingPreload) endPreload(false);
						callbackQueue.push_back([]() { saveFile(); });
					} else if (!doingPreload && !doingFileOp) {
						saveFile();
					}
				}
			}
		}

		if (activeVideo && videoPlayer.hasChapters()) {
			if (event->key.key == SDLK_RIGHTBRACKET || event->key.key == SDLK_PAGEDOWN) {
				if (!event->key.repeat) {
					jumpNextChapter();
				}
			} else if (event->key.key == SDLK_LEFTBRACKET || event->key.key == SDLK_PAGEUP) {
				if (!event->key.repeat) {
					jumpPreviousChapter();
				}
			}
		}

		if (!activeScreenCapture && (event->key.key == SDLK_LEFT || event->key.key == SDLK_A || event->key.key == SDLK_UP ||
			event->key.key == SDLK_KP_4 || event->key.key == SDLK_KP_8)) {
			if (!prevNextKeyDown) gotoPreviousImage();
			prevNextKeyDown = true;
		} else if (!activeScreenCapture && (event->key.key == SDLK_RIGHT || event->key.key == SDLK_D || event->key.key == SDLK_DOWN ||
			event->key.key == SDLK_KP_6 || event->key.key == SDLK_KP_2)) {
			if (!prevNextKeyDown) gotoNextImage();
			prevNextKeyDown = true;
		}
	} else if (event->type == SDL_EVENT_KEY_UP) {
		prevNextKeyDown = false;

	} else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (event->button.button == SDL_BUTTON_LEFT) {
			const bool menuWasOpen = context.displayMenu;
			context.mouse.x = event->button.x * Image::mouseScale;
			context.mouse.y = event->button.y * Image::mouseScale;
			mouseLastActive = getTimeNow();
			checkMouseState();
			leftClickConsumedByUI = isIconCaptured || context.displayMenu;
			auto sliderAspectScale = glm::vec2(1.0);
			if (preferredStereoMode == SBS_Full && isFullscreen)
				sliderAspectScale = glm::vec2(2.0, 1.0);
			mouseIsDown = true;
			bool languageButtonClicked = false;
			bool refreshMouseStateAfterClick = false;
			for (auto& icon : appIcons) {
				if (icon.state == IconState::Over && icon.active) {
					auto allowCallback = true;
					auto queueCallback = false;
					Image::displayTip = false;
					Image::displayInfo = false;
					Image::infoTargetVisibility = 0.0;
					const bool isNavigationIcon = icon.type == IconType::Back ||
						icon.type == IconType::Forward;
					if ((!isConverting && !doingPreload) || isNavigationIcon ||
						icon.type == IconType::Close) {
						if (icon.mode == IconMode::Button) {
							languageButtonClicked = icon.type == IconType::VideoAudio ||
								icon.type == IconType::VideoCaption;
							if (isPlayingSlideshow && (icon.type != IconType::Forward &&
								icon.type != IconType::Back && icon.type != IconType::Minimize)) {
								if (icon.type == IconType::Stereo_3D) {
									if (currentStereoMode == Depth_Zoom) {
										preferredStereoMode = defaultStereoMode;
										allowCallback = false;
									}

									setSlideshow(false);
								} else if (icon.type != IconType::Play) {
									if (currentStereoMode == Depth_Zoom) {
										preferredStereoMode = defaultStereoMode;
									}
									setSlideshow(false);
								}
								if (icon.type == IconType::File ||
									icon.type == IconType::Folder ||
									icon.type == IconType::Save) {
									queueCallback = true;
									if (doingPreload) endPreload(false);
								}
							}
							if (queueCallback) {
								if (icon.callback) callbackQueue.push_back(icon.callback);
							} else if (icon.callback && allowCallback) {
								icon.callback();
								if (icon.type == IconType::Settings)
									refreshMouseStateAfterClick = true;
							}
						} else if (icon.mode == IconMode::Slider) {
							currentSlider = &icon;
							if (activeVideo && icon.type == IconType::VideoSeek) {
								videoSliderScrubbing = true;
								videoSliderWasPlaying = videoPlayer.playing();
								videoScrubLastPreviewTime =
									getTimeNow() - videoScrubPreviewInterval;
								// Audio-only seeks have no preview frame to display. Keep
								// playback running while dragging instead of requiring the
								// video preview pause/resume cycle.
								if (!videoPlayer.audioOnly()) videoPlayer.setPlaying(false);
							}
							if (isInsideSliderTrack(icon, sliderAspectScale)) {
								double percent = getSliderPercentAtMouse(icon, sliderAspectScale);
								if (icon.type == IconType::VideoSeek && videoPlayer.hasChapters()) {
									percent = getSnappedSeekPercent(percent);
								}
								setSliderPercent(icon, percent);
								if (icon.callback) icon.callback();
							}
						}
					}
					}
			}
			if (refreshMouseStateAfterClick) checkMouseState();
			if (languageButtonClicked) {
				// Rebuild the hover label after the track change so the selected
				// language remains visible instead of being cleared by the click.
				currentInfoLabel.clear();
				mouseLastActive = getTimeNow();
				checkMouseState();
			}
			if (currentSlider != nullptr) SDL_CaptureMouse(true);

			if (menuWasOpen && context.displayMenu) {
				for (const auto& choice : *context.menuChoices) {
					auto optionIndex = (*context.menuRollover)[choice.label];
					if (optionIndex >= 0) {
						(*context.menuSelection)[choice.label] = optionIndex;
						(*context.menuCallback)[choice.label](optionIndex);
					}
				}
			}

			showCustomCursor(true);
			if (!isIconCaptured && !isConverting) isDragging = true;
		} else if (event->button.button == SDL_BUTTON_RIGHT) {
			context.mouse.x = event->button.x * Image::mouseScale;
			context.mouse.y = event->button.y * Image::mouseScale;
			mouseLastActive = getTimeNow();
			checkMouseState();
			if (!isConverting) {
				for (auto& icon : appIcons) {
					if (icon.mode == IconMode::Slider &&
						icon.group == IconGroup::SettingsDepth &&
						icon.state == IconState::Over && icon.active) {
						setSliderPercent(icon, sliderStart);
						currentSlider = &icon;
						if (icon.callback) icon.callback();
						currentSlider = nullptr;
						saveOptions();
						break;
					}
				}
			}
		} else if (event->button.button == SDL_BUTTON_X1) {
			gotoNextImage();
		} else if (event->button.button == SDL_BUTTON_X2) {
			gotoPreviousImage();
		}
	} else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (event->button.button == SDL_BUTTON_LEFT) {
			finishSliderDrag();
			auto timeNow = getTimeNow();
			if (leftClickConsumedByUI) {
				lastClick = 0.0;
				leftClickConsumedByUI = false;
				return SDL_APP_CONTINUE;
			}
			auto clickDiff = timeNow - lastClick;
			lastClick = timeNow;
			if (!isIconCaptured && !context.displayMenu) {
				if (clickDiff < doubleClickTime) toggleMaximized();
			}
		}
	} else if (event->type == SDL_EVENT_MOUSE_MOTION) {
		mouseMoveDelay--;
		if (mouseMoveDelay > 0) {
			return SDL_APP_CONTINUE;
		}
		if (mouseMoveDelay < -100) {
			mouseMoveDelay = 0;
		}

		showCustomCursor(true);

		context.mouse.x = event->motion.x * Image::mouseScale;
		context.mouse.y = event->motion.y * Image::mouseScale;
		pixelMotion.x += event->motion.xrel * Image::mouseScale;
		pixelMotion.y += event->motion.yrel * Image::mouseScale;

		mouseLastActive = getTimeNow();
		if (!mouseStateInitialized) {
			mouseStateInitialized = true;
			hideUI(false);
		} else {
			checkMouseState();
		}

		if (currentSlider != nullptr && mouseIsDown) {
			auto aspectScale = 1.0;
			if (preferredStereoMode == SBS_Full && isFullscreen)
				aspectScale = 2.0;
			aspectScale *= context.displayScale / Image::mouseScale;
			currentSlider->slider.position += glm::vec2(event->motion.xrel / aspectScale,
				event->motion.yrel / aspectScale);
			auto sliderExtents = currentSlider->slider.size.x * 0.5f;
			currentSlider->slider.position.x = glm::clamp(currentSlider->slider.position.x,
				-sliderExtents, sliderExtents);
			currentSlider->slider.position.y = 0.0f;
			if (currentSlider->callback) currentSlider->callback();
		} else if (currentSlider != nullptr) {
			finishSliderDrag();
		}
	} else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
		wheelSpeed += (int)event->wheel.y;
		mouseLastActive = getTimeNow();
	} else if (event->type == SDL_EVENT_DROP_FILE) {
		std::string droppedFile = event->drop.data;
		if (isSupportedImage(droppedFile) || isSupportedMedia(droppedFile)) {
			if (!isConverting && !doingPreload && !context.loading) {
				float mouseX = 0.0f;
				float mouseY = 0.0f;
				SDL_GetMouseState(&mouseX, &mouseY);
				context.mouse.x = mouseX * Image::mouseScale;
				context.mouse.y = mouseY * Image::mouseScale;
				mouseLeftWindow = false;
				mouseLastActive = getTimeNow();
				showCustomCursor(true);
				deferMediaLoad({droppedFile});
			}
		}
	}

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	stopVideoDepth();
	screenCapture.stop();
	videoPlayer.close();
	clearAudioAlbumArt();
	resetVideoPlaybackBuffer(false);
	lastVideoFrame.reset();
	activeVideo = false;
	saveOptions();
	resetDepthGeneration();
	nativeDepthEstimator.unload();
	nativeDepthEstimatorLoaded = false;
	nativeSuperResolution.unload();
	nativeSuperResolutionLoaded = false;
	const auto runtimeDirectory = runtimeDepthDirectory();
	if (!runtimeDirectory.empty()) {
		std::error_code cleanupError;
		std::filesystem::remove_all(runtimeDirectory, cleanupError);
	}
	Image::quit(&context);
	SDL_Quit();
}
