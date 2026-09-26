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

#include "Image.h"
#include "GettingStarted.h"
#include "Utils.h"
#include "WindowsCalibration.h"
#include "NativeDisplayIdentity.h"
#include "NativeDisplaySelection.h"
#include "LookingGlassCalibration.h"
#include "CalibrationVolumes.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
	static_assert(sizeof(Image::ImageDataFrag) % 16 == 0,
		"Image shader uniform data must remain 16-byte aligned.");
	static_assert(sizeof(Image::InterlacerDataFrag) == 128,
		"Interlacer uniform data must match the shader layout.");
	using BlurClock = std::chrono::steady_clock;
	constexpr auto videoBlurInterval = std::chrono::milliseconds(670);
	constexpr Uint32 blurSnapshotSize = 32;
	constexpr Uint32 blurSnapshotMipLevels = 6;
	BlurClock::time_point blurTransitionStart{};
	bool videoBlurActive = false;
	BlurClock::time_point videoSolidTransitionStart{};
	glm::vec4 videoSolidTransitionSource{};
	glm::vec4 videoSolidTransitionTarget{};
	bool videoSolidTransitionActive = false;
	bool videoSolidColorValid = false;
	glm::ivec2 videoYUVLumaSize{};
	glm::ivec2 videoYUVChromaSize{};
	VideoFrame::Format videoYUVFormat = VideoFrame::Format::RGBA;
	double uploadedVideoTime = -1.0;
	AudioWaveform::Bars smoothedAudioWaveform{};
	BlurClock::time_point waveformDrawTime{};

	// Opt-in diagnostics count distinct video frames after a real window draw,
	// not decoder output or repeated redraws. This is GPU completion, not a
	// measurement of the compositor's physical scanout.
	void recordBrowserDraw(Context* context, bool hasSwapchain) {
		static const bool enabled = SDL_getenv("RENDEPTH_BROWSER_STATS") != nullptr;
		if (!enabled) return;
		static auto started = BlurClock::now(), lastFrameAt = started;
		static double lastFrame = -1.0, maxGap = 0.0;
		static int frames = 0;
		const auto now = BlurClock::now();
		if (context->fileName != "Firefox Video") {
			started = lastFrameAt = now;
			lastFrame = -1.0;
			maxGap = 0.0;
			frames = 0;
			return;
		}
		if (hasSwapchain && uploadedVideoTime != lastFrame) {
			if (lastFrame >= 0.0)
				maxGap = std::max(maxGap, std::chrono::duration<double, std::milli>(now - lastFrameAt).count());
			lastFrame = uploadedVideoTime;
			lastFrameAt = now;
			++frames;
		}
		const double elapsed = std::chrono::duration<double>(now - started).count();
		if (elapsed >= 2.0) {
			SDL_Log("Firefox viewer: %.1f unique frames/s, %.1f ms max gap, %dx%d uploaded",
				frames / elapsed, maxGap, videoYUVLumaSize.x, videoYUVLumaSize.y);
			started = now;
			frames = 0;
			maxGap = 0.0;
		}
	}

	// Derive the size of one displayed view from a packed stereo or quilt image.
	glm::vec2 getStereoImageSize(glm::vec2 packedSize, StereoFormat type,
		glm::vec3 gridSize = glm::vec3(1.0f)) {
		if (type == Color_Plus_Depth || type == Side_By_Side_Full ||
			type == Side_By_Side_Swap) {
			packedSize.x *= 0.5f;
		} else if (type == Top_And_Bottom_Full) {
			packedSize.y *= 0.5f;
		} else if (type == Light_Field_LKG && gridSize.x > 0.0f && gridSize.y > 0.0f) {
			packedSize.x /= gridSize.x;
			packedSize.y /= gridSize.y;
		}
		return packedSize;
	}

	// Estimate a subdued background color from sparse samples of the frame's available pixel data.
	glm::vec4 getVideoBackgroundColor(const VideoFrame& frame) {
		constexpr int sampleSize = 4;
		glm::vec4 result{};

		if (frame.inferenceWidth > 0 && frame.inferenceHeight > 0 &&
			frame.inferenceRGBA.size() >= static_cast<size_t>(frame.inferenceWidth) * frame.inferenceHeight * 4) {
			const auto* pixels = frame.inferenceRGBA.data();
			const int width = frame.inferenceWidth;
			const int height = frame.inferenceHeight;
			for (int y = 0; y < sampleSize; ++y) {
				const int sampleY = std::min(height - 1, y * height / sampleSize);
				for (int x = 0; x < sampleSize; ++x) {
					const int sampleX = std::min(width - 1, x * width / sampleSize);
					const size_t offset = static_cast<size_t>(sampleY * width + sampleX) * 4;
					result += glm::vec4(pixels[offset], pixels[offset + 1], pixels[offset + 2], 255.0f);
				}
			}
			result /= static_cast<float>(sampleSize * sampleSize * 255);
		} else if (frame.format == VideoFrame::Format::RGBA) {
			if (frame.outputWidth <= 0 || frame.outputHeight <= 0 ||
				frame.planes[0].size() < static_cast<size_t>(frame.outputWidth) * frame.outputHeight * 4)
				return Image::clearColorSolid;
			const auto* pixels = frame.planes[0].data();
			const int width = frame.outputWidth;
			const int height = frame.outputHeight;
			for (int y = 0; y < sampleSize; ++y) {
				const int sampleY = std::min(height - 1, y * height / sampleSize);
				for (int x = 0; x < sampleSize; ++x) {
					const int sampleX = std::min(width - 1, x * width / sampleSize);
					const size_t offset = static_cast<size_t>(sampleY * width + sampleX) * 4;
					result += glm::vec4(pixels[offset], pixels[offset + 1], pixels[offset + 2], 255.0f);
				}
			}
			result /= static_cast<float>(sampleSize * sampleSize * 255);
		} else if (frame.format == VideoFrame::Format::NV12 || frame.format == VideoFrame::Format::YUV420P) {
			const int width = frame.width;
			const int height = frame.height;
			const int chromaWidth = (width + 1) / 2;
			const int chromaHeight = (height + 1) / 2;
			if (width <= 0 || height <= 0 ||
				frame.planes[0].size() < static_cast<size_t>(width) * height ||
				frame.planes[1].size() < static_cast<size_t>(chromaWidth) * chromaHeight * (frame.format == VideoFrame::Format::NV12 ? 2 : 1) ||
				(frame.format == VideoFrame::Format::YUV420P && frame.planes[2].size() < static_cast<size_t>(chromaWidth) * chromaHeight))
				return Image::clearColorSolid;

			float kr = 0.2126f, kb = 0.0722f;
			if (frame.colorSpace == VideoFrame::ColorSpace::BT601) {
				kr = 0.2990f; kb = 0.1140f;
			} else if (frame.colorSpace == VideoFrame::ColorSpace::BT2020) {
				kr = 0.2627f; kb = 0.0593f;
			}
			const float kg = 1.0f - kr - kb;

			for (int y = 0; y < sampleSize; ++y) {
				const int sampleY = std::min(height - 1, y * height / sampleSize);
				const int chromaY = sampleY / 2;
				for (int x = 0; x < sampleSize; ++x) {
					const int sampleX = std::min(width - 1, x * width / sampleSize);
					const int chromaX = sampleX / 2;

					const float yVal = static_cast<float>(frame.planes[0][sampleY * width + sampleX]);
					float uVal = 128.0f;
					float vVal = 128.0f;
					if (frame.format == VideoFrame::Format::NV12) {
						const size_t chromaOffset = static_cast<size_t>(chromaY * chromaWidth + chromaX) * 2;
						uVal = static_cast<float>(frame.planes[1][chromaOffset]);
						vVal = static_cast<float>(frame.planes[1][chromaOffset + 1]);
					} else {
						const size_t chromaOffset = static_cast<size_t>(chromaY * chromaWidth + chromaX);
						uVal = static_cast<float>(frame.planes[1][chromaOffset]);
						vVal = static_cast<float>(frame.planes[2][chromaOffset]);
					}

					float yScaled = 0.0f, cb = 0.0f, cr = 0.0f;
					if (!frame.fullRange) {
						yScaled = (yVal - 16.0f) / 219.0f;
						cb = (uVal - 128.0f) / 224.0f;
						cr = (vVal - 128.0f) / 224.0f;
					} else {
						yScaled = yVal / 255.0f;
						cb = uVal / 255.0f - 0.5f;
						cr = vVal / 255.0f - 0.5f;
					}

					const float r = glm::clamp(yScaled + 2.0f * (1.0f - kr) * cr, 0.0f, 1.0f);
					const float g = glm::clamp(yScaled - 2.0f * kb * (1.0f - kb) / kg * cb - 2.0f * kr * (1.0f - kr) / kg * cr, 0.0f, 1.0f);
					const float b = glm::clamp(yScaled + 2.0f * (1.0f - kb) * cb, 0.0f, 1.0f);

					result += glm::vec4(r, g, b, 1.0f);
				}
			}
			result /= static_cast<float>(sampleSize * sampleSize);
		} else {
			return Image::clearColorSolid;
		}

		result.a = 1.0f;
		return glm::mix(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f), result, 0.9f);
	}

// Blend the solid background toward the latest video color sample.
void updateVideoSolidColor() {
		if (!videoSolidTransitionActive) return;
		const auto elapsed = BlurClock::now() - videoSolidTransitionStart;
		const float blend = glm::clamp(
			std::chrono::duration<float>(elapsed).count() /
			std::chrono::duration<float>(videoBlurInterval).count(), 0.0f, 1.0f);
		Image::clearColorSolid = glm::mix(videoSolidTransitionSource,
			videoSolidTransitionTarget, blend);
		if (blend >= 1.0f) videoSolidTransitionActive = false;
	}

	// Read a calibration number from either a direct value or a nested value field.
	bool readCalibrationNumber(const rapidjson::Value& object, const char* name, float& result) {
		if (!object.IsObject() || !object.HasMember(name)) return false;
		const auto& entry = object[name];
		if (entry.IsNumber()) {
			result = entry.GetFloat();
			return true;
		}
		if (entry.IsObject() && entry.HasMember("value") && entry["value"].IsNumber()) {
			result = entry["value"].GetFloat();
			return true;
		}
		return false;
	}

	// Interpret a numeric calibration flag as a boolean.
	bool readCalibrationBool(const rapidjson::Value& object, const char* name, bool& result) {
		float value = 0.0f;
		if (!readCalibrationNumber(object, name, value)) return false;
		result = value != 0.0f;
		return true;
	}

	// Validate Looking Glass calibration JSON and translate its optical settings into the renderer's
	// configuration.
	bool parseLookingGlassCalibration(const std::string& text, const std::string& source,
		NativeDisplayConfig& config) {
		rapidjson::Document document;
		document.Parse(text.data(), text.size());
		if (document.HasParseError() || !document.IsObject()) return false;

		NativeDisplayConfig loaded = config;
		float pitch = loaded.pitch, slope = loaded.slope, center = loaded.center, dpi = loaded.dpi;
		float screenWidth = (float)loaded.screenSize.x, screenHeight = (float)loaded.screenSize.y, viewCone = loaded.viewCone;
		float subpixel = loaded.subpixel;

		const bool complete =
			readCalibrationNumber(document, "pitch", pitch) && pitch > 0.0f &&
			(readCalibrationNumber(document, "slope", slope) ||
				readCalibrationNumber(document, "tilt", slope)) && std::abs(slope) > 0.001f &&
			readCalibrationNumber(document, "center", center) &&
			(readCalibrationNumber(document, "DPI", dpi) ||
				readCalibrationNumber(document, "dpi", dpi)) && dpi > 0.0f &&
			(readCalibrationNumber(document, "screenW", screenWidth) ||
				readCalibrationNumber(document, "screenWidth", screenWidth) ||
				readCalibrationNumber(document, "width", screenWidth)) && screenWidth > 0.0f &&
			(readCalibrationNumber(document, "screenH", screenHeight) ||
				readCalibrationNumber(document, "screenHeight", screenHeight) ||
				readCalibrationNumber(document, "height", screenHeight)) && screenHeight > 0.0f;
		if (!complete) {
			SDL_Log("Incomplete Looking Glass calibration in %s", source.c_str());
			return false;
		}
		if (!readCalibrationNumber(document, "subpixel", subpixel)) {
			readCalibrationNumber(document, "subpixelOffset", subpixel);
		}

		if (pitch > 0.0f) loaded.pitch = pitch;
		loaded.slope = slope;
		loaded.center = center;
		if (dpi > 0.0f) loaded.dpi = dpi;
		if (screenWidth > 0.0f && screenHeight > 0.0f) {
			loaded.screenSize = {(int)std::lround(screenWidth), (int)std::lround(screenHeight)};
		}
		loaded.subpixel = subpixel;
		if (readCalibrationNumber(document, "viewCone", viewCone)) loaded.viewCone = viewCone;

		bool vendorInvertView = false;
		if (readCalibrationBool(document, "invView", vendorInvertView))
			loaded.invertView = !vendorInvertView;
		readCalibrationBool(document, "invertView", loaded.invertView);
		readCalibrationBool(document, "flipImageX", loaded.flipImageX);
		readCalibrationBool(document, "flipX", loaded.flipImageX);
		readCalibrationBool(document, "flipImageY", loaded.flipImageY);
		readCalibrationBool(document, "flipY", loaded.flipImageY);
		readCalibrationBool(document, "flipSubp", loaded.flipSubpixel);
		readCalibrationBool(document, "flipSubpixel", loaded.flipSubpixel);

		float viewCountVal = 0.0f;
		if (readCalibrationNumber(document, "viewCount", viewCountVal) ||
			readCalibrationNumber(document, "views", viewCountVal)) {
			loaded.viewCount = (int)std::lround(viewCountVal);
			if (loaded.viewCount == 2) loaded.quiltGrid = {2.0f, 1.0f};
		}
		loaded.calibrated = true;
		if (loaded.screenSize == glm::ivec2(1440, 2560) && loaded.viewCount == 2) {
			loaded.quiltGrid = {11.0f, 6.0f};
			loaded.viewCount = 66;
		}
		// The LKG-J 16-inch landscape calibration omits a view count. Use the
		// multiview layout already used by the Go for RGB-D light field output.
		if (const auto serial = document.FindMember("serial");
			serial != document.MemberEnd() && serial->value.IsString() &&
			std::string_view(serial->value.GetString()).starts_with("LKG-J") &&
			loaded.screenSize == glm::ivec2(3840, 2160) && loaded.viewCount == 2) {
			loaded.quiltGrid = {11.0f, 6.0f};
			loaded.viewCount = 66;
		}
		config = loaded;
		SDL_Log("Loaded Looking Glass calibration from %s: pitch=%.6f slope=%.6f "
			"center=%.6f DPI=%.3f screen=%dx%d viewCone=%.3f invert=%d "
			"flipX=%d flipY=%d flipSubp=%d",
			source.c_str(), config.pitch, config.slope, config.center, config.dpi,
			config.screenSize.x, config.screenSize.y, config.viewCone,
			config.invertView ? 1 : 0, config.flipImageX ? 1 : 0,
			config.flipImageY ? 1 : 0, config.flipSubpixel ? 1 : 0);

		return true;
	}

	// Load and parse a bounded-size Looking Glass calibration file.
	bool loadLookingGlassCalibration(const std::filesystem::path& path,
		NativeDisplayConfig& config) {
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);
		if (error || size == 0 || size > 64 * 1024) return false;
		std::ifstream file(path);
		std::string text((std::istreambuf_iterator<char>(file)), {});
		if (!file && text.empty()) return false;
		return parseLookingGlassCalibration(text, path.string(), config);
	}

	// Locate calibration using an explicit override, mounted device storage, or local fallback paths.
	std::filesystem::path findLookingGlassCalibration() {
		if (const char* requested = std::getenv("RENDEPTH_NATIVE_CALIBRATION"))
			return requested;

		std::error_code error;
	#if defined(_WIN32)
		// Looking Glass devices expose their calibration on the volume root.
		// Scan drive letters so USB volumes and ordinary mounted drives work
		// without requiring a Linux-style mount point. Keep the device search
		// ahead of local paths so a connected device always wins.
		for (char drive = 'A'; drive <= 'Z'; ++drive) {
			const auto volumeRoot = std::filesystem::path(std::string(1, drive) + ":\\");
			if (GetDriveTypeW(volumeRoot.c_str()) == DRIVE_CDROM) continue;
			const auto candidate = volumeRoot / "LKG_calibration" / "visual.json";
			if (std::filesystem::is_regular_file(candidate, error)) return candidate;
		}

		// Also support calibration copied from the device or installed by
		// Looking Glass Bridge. These environment variables are the native
		// Windows locations and avoid assuming a particular user name.
		for (const char* variable : {"LOCALAPPDATA", "APPDATA", "PROGRAMDATA"}) {
			if (const char* root = std::getenv(variable); root != nullptr && *root != '\0') {
				const auto rootPath = std::filesystem::path(root);
				for (const auto& relative : {
					std::filesystem::path("LKG_calibration") / "visual.json",
					std::filesystem::path("Looking Glass") / "LKG_calibration" / "visual.json",
					std::filesystem::path("Looking Glass") / "visual.json"}) {
					const auto candidate = rootPath / relative;
					if (std::filesystem::is_regular_file(candidate, error)) return candidate;
				}
			}
		}
	#else
		const auto opticalMounts = CalibrationVolumes::opticalMounts();
		for (const auto& mediaRoot : {std::filesystem::path("/run/media"), std::filesystem::path("/media")}) {
			if (!std::filesystem::is_directory(mediaRoot, error)) continue;
			for (const auto& userRoot : std::filesystem::directory_iterator(mediaRoot, error)) {
				if (error || CalibrationVolumes::contains(opticalMounts, userRoot.path()) ||
					!userRoot.is_directory(error)) continue;
				for (const auto& volume : std::filesystem::directory_iterator(userRoot.path(), error)) {
					if (error || CalibrationVolumes::contains(opticalMounts, volume.path()) ||
						!volume.is_directory(error)) continue;
					auto candidate = volume.path() / "LKG_calibration" / "visual.json";
					if (std::filesystem::is_regular_file(candidate, error)) return candidate;
				}
			}
		}
	#endif
		// Prefer the attached Looking Glass calibration over saved experiments
		// for other panels. RENDEPTH_NATIVE_CALIBRATION remains an explicit override.
		for (const auto& localCandidate : {
			std::filesystem::current_path() / "calibration.json",
			std::filesystem::current_path() / "visual.json",
			Core::getHomeDirectory() / ".config" / "rendepth" / "calibration.json",
			Core::getHomeDirectory() / ".rendepth" / "calibration.json"
		}) {
			if (std::filesystem::is_regular_file(localCandidate, error)) return localCandidate;
		}

		return {};
	}

	// Apply environment overrides for native display dimensions and optical calibration.
	void applyNativeDisplayOverrides(NativeDisplayConfig& config) {
		if (const char* value = std::getenv("RENDEPTH_NATIVE_WIDTH"))
			config.screenSize.x = (int)std::strtol(value, nullptr, 10);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_HEIGHT"))
			config.screenSize.y = (int)std::strtol(value, nullptr, 10);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_PITCH"))
			config.pitch = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_TILT"))
			config.slope = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_SLOPE"))
			config.slope = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_CENTER"))
			config.center = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_SUBPIXEL"))
			config.subpixel = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_DPI"))
			config.dpi = std::strtof(value, nullptr);
		if (const char* value = std::getenv("RENDEPTH_NATIVE_VIEWS")) {
			config.viewCount = (int)std::strtol(value, nullptr, 10);
			if (config.viewCount == 2) config.quiltGrid = {2.0f, 1.0f};
		}
		if (const char* value = std::getenv("RENDEPTH_NATIVE_FLIP_X"))
			config.flipImageX = (std::string(value) == "1" || std::string(value) == "true");
		if (const char* value = std::getenv("RENDEPTH_NATIVE_FLIP_Y"))
			config.flipImageY = (std::string(value) == "1" || std::string(value) == "true");
		if (const char* value = std::getenv("RENDEPTH_NATIVE_FLIP_SUBPIXEL"))
			config.flipSubpixel = (std::string(value) == "1" || std::string(value) == "true");
		if (const char* value = std::getenv("RENDEPTH_NATIVE_INVERT"))
			config.invertView = (std::string(value) == "1" || std::string(value) == "true");
	}

	// Reload device-specific calibration while retaining the selected display identity.
	void reloadNativeDisplayConfig(NativeDisplayConfig& config) {
		const auto displayName = config.displayName;
		NativeDisplayConfig reloaded{};
		if (config.cubeViC1) {
			reloaded.cubeViC1 = true;
			reloaded.screenSize = {1440, 2560};
			reloaded.quiltGrid = {8.0f, 5.0f};
			reloaded.viewCount = 40;
			std::string error;
			const auto path = CubeViCalibration::find();
			if (!path.empty() && CubeViCalibration::load(path, reloaded.cubeViOptics, error)) {
				reloaded.usingDefaultCalibration = false;
				SDL_Log("Loaded CubeVi C1 calibration from %s: interval=%.6f obliquity=%.6f deviation=%.6f",
					path.string().c_str(), reloaded.cubeViOptics.interval,
					reloaded.cubeViOptics.obliquity, reloaded.cubeViOptics.deviation);
			} else {
				if (!path.empty())
					SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CubeVi C1 calibration unavailable: %s (%s)",
						path.string().c_str(), error.c_str());
				reloaded.cubeViOptics = CubeViCalibration::c1Defaults;
				SDL_Log("Using built-in CubeVi C1 calibration: interval=%.6f obliquity=%.6f deviation=%.6f",
					reloaded.cubeViOptics.interval, reloaded.cubeViOptics.obliquity,
					reloaded.cubeViOptics.deviation);
			}
			reloaded.calibrated = true;
			reloaded.displayName = displayName;
			config = reloaded;
			return;
		}
	#ifdef _WIN32
		std::string portableCalibration;
		if (std::getenv("RENDEPTH_NATIVE_CALIBRATION") == nullptr &&
			WindowsCalibration::load(portableCalibration)) {
			parseLookingGlassCalibration(portableCalibration,
				"Looking Glass portable device/LKG_calibration/visual.json", reloaded);
		}
	#endif
		if (!reloaded.calibrated) {
			if (const auto path = findLookingGlassCalibration(); !path.empty()) {
				if (!loadLookingGlassCalibration(path, reloaded))
					SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
						"Could not load Looking Glass calibration from %s; using defaults.",
						path.string().c_str());
			} else if (!config.lookingGlassGo) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"No Looking Glass calibration found; using default native output calibration.");
			}
		}
		reloaded.usingDefaultCalibration = !reloaded.calibrated;
		if (!reloaded.calibrated && config.lookingGlassGo)
			parseLookingGlassCalibration(LookingGlassCalibration::goDefaults,
				"built-in LKG Go fallback", reloaded);
		reloaded.lookingGlassGo = config.lookingGlassGo;
		applyNativeDisplayOverrides(reloaded);
		reloaded.displayName = displayName;
		config = reloaded;
	}
}

// Advance the shared solid-color background animation.
void updateVideoBackgroundAnimationImpl() {
	updateVideoSolidColor();
}

// Locate an icon's normalized origin in the texture atlas.
glm::vec2 Image::getIconCoordinates(IconType iconType) {
	auto iconIndex = static_cast<int>(iconType);
	auto iconX = iconIndex % gridSize;
	auto iconY = iconIndex / gridSize;
	return glm::vec2(iconX, iconY) / (float)gridSize;
}

// Discover supported lenticular displays and create or reuse their GPU output windows.
int Image::initNativeOutput(Context* context) {
	if (context == nullptr || context->device == nullptr) return -1;
	const char* enabled = std::getenv("RENDEPTH_NATIVE_OUTPUT");
	if (enabled != nullptr && std::string(enabled) == "0") {
		setNativeOutputActive(context, false);
		return 0;
	}
	const char* requestedName = std::getenv("RENDEPTH_NATIVE_DISPLAY");

	int displayCount = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
	SDL_DisplayID appDisplay = context->window != nullptr ? SDL_GetDisplayForWindow(context->window) : 0;
	std::vector<NativeOutput> selected;
	NativeDisplaySelection selection;
	for (int i = 0; displays != nullptr && i < displayCount; ++i) {
		const char* name = SDL_GetDisplayName(displays[i]);
		std::string displayName = name != nullptr ? name : "";
		const auto* mode = SDL_GetCurrentDisplayMode(displays[i]);
		const auto hardware = NativeDisplayIdentity::hardwareId(displays[i]);
		const bool cubeVi = NativeDisplayIdentity::isCubeViC1(hardware,
			mode ? mode->w : 0, mode ? mode->h : 0, displayName);
		SDL_Log("Display %u: name='%s' hardware='%s' mode=%dx%d CubeViC1=%d",
			displays[i], displayName.c_str(), hardware.c_str(), mode ? mode->w : 0,
			mode ? mode->h : 0, cubeVi ? 1 : 0);
		const bool requested = requestedName != nullptr &&
			displayName.find(requestedName) != std::string::npos;
		const std::string lowerName = [&displayName] {
			std::string value = displayName;
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			return value;
		}();
		const bool isLookingGlass = lowerName.find("lkg") != std::string::npos ||
			lowerName.find("looking glass") != std::string::npos;
		const bool isGenericLenticular = lowerName.find("3d display") != std::string::npos ||
			lowerName.find("lenticular") != std::string::npos;
		const bool knownQuiltDisplay = cubeVi || isLookingGlass || isGenericLenticular;
		if (!requested && requestedName == nullptr && !knownQuiltDisplay) continue;
		if (!requested && requestedName != nullptr) continue;

		bool matchedCubeVi = cubeVi;
		// Explicit model override supports hardware revisions not yet observed.
		if (const char* model = std::getenv("RENDEPTH_NATIVE_MODEL"))
			matchedCubeVi = std::string(model) == "cubevi-c1";
		if (!selection.add(matchedCubeVi)) {
			SDL_Log("Skipping duplicate native display type: %s", displayName.c_str());
			continue;
		}
		NativeOutput output;
		output.display = displays[i];
		output.config.displayName = displayName;
		output.config.cubeViC1 = matchedCubeVi;
		// Go panels advertise an LKG-E serial as their display name.
		output.config.lookingGlassGo = !matchedCubeVi &&
			(lowerName.rfind("lkg-e", 0) == 0 || lowerName.find("lkg go") != std::string::npos ||
				lowerName.find("looking glass go") != std::string::npos);
		selected.push_back(output);
	}
	SDL_free(displays);
	// Keep the main-window calibration as the public/default configuration.
	const auto mainOutput = std::find_if(selected.begin(), selected.end(), [appDisplay](const NativeOutput& output) {
		return output.display == appDisplay;
	});
	if (mainOutput != selected.end()) std::rotate(selected.begin(), mainOutput, mainOutput + 1);
	const bool sameRouting = selected.size() == nativeOutputs.size() &&
		std::equal(selected.begin(), selected.end(), nativeOutputs.begin(), [context, appDisplay](const NativeOutput& a, const NativeOutput& b) {
			return a.display == b.display && a.config.cubeViC1 == b.config.cubeViC1 &&
				a.config.lookingGlassGo == b.config.lookingGlassGo &&
				(a.display == appDisplay) == (b.window == context->window);
		});
	if (sameRouting && !selected.empty()) return 0;
	setNativeOutputActive(context, false);
	for (auto& output : selected) {
		reloadNativeDisplayConfig(output.config);
		// configureFullscreenMode uses this until the output is registered.
		nativeDisplayConfig = output.config;
		if (output.display == appDisplay) {
			output.window = context->window;
		} else {
			SDL_Rect bounds{};
			if (!SDL_GetDisplayBounds(output.display, &bounds)) continue;
			output.window = SDL_CreateWindow("Rendepth Native Output", bounds.w, bounds.h,
				SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN |
				SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_UTILITY);
			if (!output.window) continue;
			SDL_SetWindowPosition(output.window, SDL_WINDOWPOS_CENTERED_DISPLAY(output.display),
				SDL_WINDOWPOS_CENTERED_DISPLAY(output.display));
			configureFullscreenMode(output.window, output.display);
			SDL_SetWindowFullscreen(output.window, true);
#ifdef _WIN32
			const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(output.window),
				SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
			if (!hwnd) {
				SDL_DestroyWindow(output.window);
				continue;
			}
			EnableWindow(hwnd, FALSE);
#endif
			if (!SDL_ClaimWindowForGPUDevice(context->device, output.window)) {
				SDL_Log("Could not claim native output for %s: %s", output.config.displayName.c_str(), SDL_GetError());
				SDL_DestroyWindow(output.window);
				continue;
			}
			SDL_ShowWindow(output.window);
		}
		SDL_Log("Native output: %s (%s)", output.config.displayName.c_str(),
			output.config.cubeViC1 ? "cubevi-c1" : "looking-glass/generic");
		if (output.config.usingDefaultCalibration && nativeCalibrationTexture == nullptr)
			Core::drawText(context, "Calibration Not Found", helpFont,
				nativeCalibrationTexture, nativeCalibrationTextSize, "Native Calibration Help Texture");
		nativeOutputs.push_back(output);
	}
	nativeOutputEnabled = !nativeOutputs.empty();
	nativeDisplayOnMainWindow = nativeOutputEnabled && nativeOutputs.front().window == context->window;
	nativeDisplay = nativeOutputEnabled ? nativeOutputs.front().display : 0;
	nativeDisplayConfig = nativeOutputEnabled ? nativeOutputs.front().config : NativeDisplayConfig{};
	// Compatibility alias for pipeline setup and main-window routing; ownership is in nativeOutputs.
	context->nativeOutputWindow = nullptr;
	for (const auto& output : nativeOutputs)
		if (output.window != context->window) { context->nativeOutputWindow = output.window; break; }
	return 0;
}

// Select a fullscreen mode suited to the target display's calibrated dimensions.
void Image::configureFullscreenMode(SDL_Window* window, SDL_DisplayID displayID) {
	if (window == nullptr || displayID == 0) return;
	const auto output = std::find_if(nativeOutputs.begin(), nativeOutputs.end(), [displayID](const NativeOutput& value) {
		return value.display == displayID;
	});
	const auto& config = output != nativeOutputs.end() ? output->config : nativeDisplayConfig;
	int modeCount = 0;
	SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayID, &modeCount);
	const SDL_DisplayMode* bestMode = nullptr;
	if (modes != nullptr && modeCount > 0) {
		if (config.screenSize.x > 0 && config.screenSize.y > 0) {
			for (int i = 0; i < modeCount; ++i) {
				if (modes[i] != nullptr &&
					modes[i]->w == config.screenSize.x &&
					modes[i]->h == config.screenSize.y) {
					bestMode = modes[i];
					break;
				}
			}
		}
		if (bestMode == nullptr) {
			for (int i = 0; i < modeCount; ++i) {
				if (modes[i] != nullptr && modes[i]->w == 3840 && modes[i]->h == 2160) {
					bestMode = modes[i];
					break;
				}
			}
		}
		if (bestMode == nullptr) {
			int maxPixels = 0;
			for (int i = 0; i < modeCount; ++i) {
				if (modes[i] != nullptr) {
					int pixels = modes[i]->w * modes[i]->h;
					if (pixels > maxPixels) {
						maxPixels = pixels;
						bestMode = modes[i];
					}
				}
			}
		}
	}
	if (bestMode != nullptr) {
		SDL_SetWindowFullscreenMode(window, bestMode);
		SDL_Log("Selected native fullscreen display mode: %dx%d (%.2f Hz) for display %u",
			bestMode->w, bestMode->h, bestMode->refresh_rate, (unsigned int)displayID);
	} else {
		SDL_SetWindowFullscreenMode(window, nullptr);
	}
	if (modes != nullptr) SDL_free(modes);
}

// Report whether native display output has been enabled.
bool Image::nativeOutputAvailable() {
	return nativeOutputEnabled;
}

// Serialize the device-specific optical calibration to the requested JSON file.
bool Image::saveNativeDisplayConfig(const std::filesystem::path& path, const NativeDisplayConfig& config) {
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	if (config.cubeViC1) {
		writer.Key("model"); writer.String("cubevi-c1");
		writer.Key("config"); writer.StartObject();
		writer.Key("lineNumber"); writer.Double(config.cubeViOptics.interval);
		writer.Key("obliquity"); writer.Double(config.cubeViOptics.obliquity);
		writer.Key("deviation"); writer.Double(config.cubeViOptics.deviation);
		writer.EndObject();
	} else {
		writer.Key("pitch");
		writer.Double(config.pitch);
		writer.Key("slope");
		writer.Double(config.slope);
		writer.Key("center");
		writer.Double(config.center);
		writer.Key("dpi");
		writer.Double(config.dpi);
		writer.Key("screenWidth");
		writer.Int(config.screenSize.x);
		writer.Key("screenHeight");
		writer.Int(config.screenSize.y);
		writer.Key("viewCount");
		writer.Int(config.viewCount);
		writer.Key("subpixel");
		writer.Double(config.subpixel);
		writer.Key("invertView");
		writer.Bool(config.invertView);
		writer.Key("flipImageX");
		writer.Bool(config.flipImageX);
		writer.Key("flipImageY");
		writer.Bool(config.flipImageY);
		writer.Key("flipSubpixel");
		writer.Bool(config.flipSubpixel);
	}
	writer.EndObject();

	const std::string text = buffer.GetString();
	SDL_IOStream* file = SDL_IOFromFile(path.string().c_str(), "w");
	if (file == nullptr) {
		SDL_Log("Failed to save calibration to %s: %s", path.string().c_str(), SDL_GetError());
		return false;
	}
	SDL_WriteIO(file, text.data(), text.size());
	SDL_CloseIO(file);
	SDL_Log("Saved lenticular calibration to %s", path.string().c_str());
	return true;
}

// Save calibration to the default file in the current working directory.
bool Image::saveNativeDisplayConfig(const NativeDisplayConfig& config) {
	std::filesystem::path savePath = std::filesystem::current_path() / "calibration.json";
	return saveNativeDisplayConfig(savePath, config);
}

// Restore native calibration from the device or default settings and refresh its shader data.
void Image::resetNativeDisplayConfig(Context* context) {
	std::string currentDisplayName = nativeDisplayConfig.displayName;
	if (nativeDisplayConfig.cubeViC1 || nativeDisplayConfig.lookingGlassGo) {
		reloadNativeDisplayConfig(nativeDisplayConfig);
		return;
	}
	nativeDisplayConfig = NativeDisplayConfig{};
	nativeDisplayConfig.displayName = currentDisplayName;
	saveNativeDisplayConfig(nativeDisplayConfig);
	if (context != nullptr) {
		updateInterlacerUniforms(context, (int)context->windowSize.x, (int)context->windowSize.y);
	}
}

// Report whether the main application window is also the native display output.
bool Image::isNativeDisplayOnMainWindow() {
	return nativeDisplayOnMainWindow;
}

// Initialize native output when enabled, or release its additional windows and display state.
void Image::setNativeOutputActive(Context* context, bool active) {
	if (context == nullptr || context->device == nullptr) return;
	if (active) {
		if (!nativeOutputEnabled || (nativeDisplay != 0 && (context->nativeOutputWindow == nullptr && !nativeDisplayOnMainWindow)))
			initNativeOutput(context);
		return;
	}
	for (auto& output : nativeOutputs) {
		if (output.window && output.window != context->window) {
			SDL_ReleaseWindowFromGPUDevice(context->device, output.window);
			SDL_DestroyWindow(output.window);
		}
	}
	nativeOutputs.clear();
	context->nativeOutputWindow = nullptr;
	nativeDisplay = 0;
	nativeOutputEnabled = false;
	nativeOutputSourceReady = false;
	nativeDisplayOnMainWindow = false;
}

// Calculate the fitted image size and aspect ratios for the current stereo presentation.
glm::vec2 Image::updateRatio(Context* context, glm::vec2 windowSize) {
	auto visualSize = imageSize;
	if (context->mode == SBS_Full && context->fullscreen)
		visualSize.x *= 2.0;
	if (context->imageType == Stereo_Free_View_LRL)
		visualSize.y *= 3.0;
	auto newImageSize = Utils::getSafeSize(visualSize, windowSize, 1.0f, true);
	context->displayAspect.x = windowSize.x / windowSize.y;
	context->displayAspect.y = newImageSize.x / newImageSize.y;
	return newImageSize;
}

// Update the safe image bounds for the current window and render mode.
void Image::updateSize(Context* context) {
	imageSize = glm::vec3(context->imageSize.x, context->imageSize.y, 0);
	auto visualSize = imageSize;
	if (context->mode == SBS_Full && context->fullscreen)
		visualSize.x *= 2.0;
	safeImageSize = Utils::getSafeSize(visualSize, context->virtualSize, (float)safePercent, true);
}

// Read a surface pixel's RGB channels, using black if the read fails.
glm::vec3 Image::getColor(SDL_Surface* surface, int x, int y) {
	Uint8 red, green, blue, alpha;
	auto success = SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha);
	if (success) return { red, green, blue };
	return { 0, 0, 0 };
}

// Sample a surface to choose a background color that complements the image.
glm::vec4 Image::getBackgroundColor(SDL_Surface* surface, int size, int width, int height) {
	std::vector<glm::vec4> backgroundColors{};
	Uint8 red, green, blue, alpha;
	int strideX = width / size;
	int strideY = height / size;
	for (auto y = 0; y < size; y++) {
		for (auto x = 0; x < size; x++) {
			auto success = SDL_ReadSurfacePixel(surface, x * strideX, y * strideY, &red, &green, &blue, &alpha);
			if (success) {
				glm::vec4 color(red, green, blue, alpha);
				backgroundColors.push_back(color);
			}
		}
	}
	glm::vec4 result{};
	for (auto color : backgroundColors) {
		result += color;
	}
	result /= backgroundColors.size();
	result /= 255.0f;
	result.a = 1.0f;
	glm::vec4 clearColor = glm::vec4(0.1, 0.1, 0.1, 1.0);
	result = mix(clearColor, result, 0.9);
	return result;
}

// Load an image surface into rendering state, resolve its stereo layout, and prepare its background.
int Image::load(Context* context, FileInfo& imageInfo, SDL_Surface* imageData,
		StereoFormat forcedType) {
	if (imageInfo.path.empty()) {
		SDL_SetWindowTitle(context->window, context->appName);
		Core::drawText(context, "Drag & Drop or Click Load Icon", helpFont,
			helpTexture, helpTextSize, "Help Texture");
		displayHelp = true;
		return 1;
	}
	displayHelp = false;
	context->currentZoom = 1.0f;
	context->loading = true;
	if (imageData == nullptr) {
		imageData = Core::loadImageDirect(imageInfo.path);
		if (imageData == nullptr) {
			imageInfo.path = imageInfo.link;
			imageData = Core::loadImageDirect(imageInfo.link);
			if (imageData == nullptr) {
				SDL_SetWindowTitle(context->window, imageInfo.name.c_str());
				Core::drawText(context, "Could Not Load Media", helpFont, helpTexture,
				    helpTextSize, "Help Texture");
				context->loading = false;
				displayHelp = true;
				return 2;
			}
		}
	}

	if (imageData->w > maxImageSize || imageData->h > maxImageSize) {
		SDL_SetWindowTitle(context->window, context->appName);
		Core::drawText(context, "Image Dimensions Exceeded", helpFont,
			helpTexture, helpTextSize, "Help Texture");
		context->loading = false;
		displayHelp = true;
		return 3;
	}

	context->loading = false;
	context->fileLink = imageInfo.link;
	context->fileName = imageInfo.base;
	if (forcedType != Unknown_Format) {
		imageInfo.type = forcedType;
	} else {
		imageInfo.type = Core::getImageType(imageInfo.path);
		if (imageInfo.type == Unknown_Format) imageInfo.type = Core::defaultImportFormat;
	}
	context->imageType = imageInfo.type;
	if (context->imageType == Light_Field_LKG) {
		auto gridSize = Core::getGridInfo(imageInfo.base);
		context->gridSize = gridSize;
	}
	context->imageSize = getStereoImageSize(
		{(float)imageData->w, (float)imageData->h}, context->imageType, context->gridSize);
	context->infoText = Core::getFileText(imageInfo, context->imageSize);
	updateSize(context);

	SDL_SetWindowTitle(context->window, imageInfo.name.c_str());

	// The image is already decoded before reaching this point. Submit the
	// upload and let the GPU queue order it before rendering instead of blocking
	// the UI thread on a fence during navigation.
	uploadTexture(context, imageData, &imageTexture, "Image Texture", false, false);
	blitBlurTexture(context, imageTexture, (Uint32)imageData->w,
		(Uint32)imageData->h, false, false);
	clearColorSolid = getBackgroundColor(imageData, 4, imageData->w, imageData->h);
	SDL_DestroySurface(imageData);

	return 0;
}

// Upload packed or planar video pixels, reusing compatible textures and refreshing the background when
// requested.
int Image::updateVideoFrame(Context* context, const VideoFrame& frame, bool firstFrame,
		int logicalWidth, int logicalHeight, bool updateBlur) {
	if (context == nullptr || context->device == nullptr || frame.width <= 0 ||
		frame.height <= 0 || frame.outputWidth <= 0 || frame.outputHeight <= 0)
		return -1;
	const glm::ivec2 frameSize{frame.outputWidth, frame.outputHeight};
	const bool reuseTexture = !firstFrame && frameSize == videoTextureSize;
	const glm::vec2 packedImageSize = {
		(float)(logicalWidth > 0 ? logicalWidth : frame.width),
		(float)(logicalHeight > 0 ? logicalHeight : frame.height)};
	context->imageSize = getStereoImageSize(packedImageSize,
		videoDepthTexture != nullptr ? Color_Only : context->imageType, context->gridSize);
	imageSize = context->imageSize;
	updateSize(context);
	if (frame.format == VideoFrame::Format::RGBA) {
		if (frame.planes[0].size() < static_cast<size_t>(frame.outputWidth) *
			frame.outputHeight * 4) return -1;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(frame.outputWidth, frame.outputHeight,
			SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(frame.planes[0].data()),
			frame.outputWidth * 4);
		if (surface == nullptr) return -1;
		const int result = uploadTexture(context, surface, &imageTexture, "Video Texture",
			reuseTexture, !reuseTexture, false);
		SDL_DestroySurface(surface);
		if (result < 0) return -1;
	} else {
		const glm::ivec2 lumaSize{frame.width, frame.height};
		const glm::ivec2 chromaSize{(frame.width + 1) / 2, (frame.height + 1) / 2};
		const bool reusePlanes = videoYTexture != nullptr && videoUTexture != nullptr &&
			videoYUVLumaSize == lumaSize && videoYUVChromaSize == chromaSize &&
			videoYUVFormat == frame.format &&
			(frame.format == VideoFrame::Format::NV12 || videoVTexture != nullptr);
		auto createTexture = [&](SDL_GPUTexture*& texture, SDL_GPUTextureFormat format,
				glm::ivec2 size, const char* name, SDL_GPUTextureUsageFlags usage) {
			if (texture != nullptr) SDL_ReleaseGPUTexture(context->device, texture);
			const SDL_GPUTextureCreateInfo info{
				.type = SDL_GPU_TEXTURETYPE_2D,
				.format = format,
				.usage = usage,
				.width = static_cast<Uint32>(size.x),
				.height = static_cast<Uint32>(size.y),
				.layer_count_or_depth = 1,
				.num_levels = 1
			};
			texture = SDL_CreateGPUTexture(context->device, &info);
			if (texture != nullptr) SDL_SetGPUTextureName(context->device, texture, name);
			return texture != nullptr;
		};
		if (!reusePlanes) {
			if (!createTexture(videoYTexture, SDL_GPU_TEXTUREFORMAT_R8_UNORM, lumaSize,
					"Video Y Plane", SDL_GPU_TEXTUREUSAGE_SAMPLER) ||
				!createTexture(videoUTexture,
					frame.format == VideoFrame::Format::NV12
						? SDL_GPU_TEXTUREFORMAT_R8G8_UNORM : SDL_GPU_TEXTUREFORMAT_R8_UNORM,
					chromaSize, "Video UV Plane", SDL_GPU_TEXTUREUSAGE_SAMPLER)) return -1;
			if (frame.format == VideoFrame::Format::YUV420P) {
				if (!createTexture(videoVTexture, SDL_GPU_TEXTUREFORMAT_R8_UNORM, chromaSize,
						"Video V Plane", SDL_GPU_TEXTUREUSAGE_SAMPLER)) return -1;
			} else if (videoVTexture != nullptr) {
				SDL_ReleaseGPUTexture(context->device, videoVTexture);
				videoVTexture = nullptr;
			}
			videoYUVLumaSize = lumaSize;
			videoYUVChromaSize = chromaSize;
			videoYUVFormat = frame.format;
		}
		if (!reuseTexture) {
			if (!createTexture(imageTexture, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
					frameSize, "Video Texture",
					SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return -1;
		}

		const size_t yBytes = frame.planes[0].size();
		const size_t uBytes = frame.planes[1].size();
		const size_t vBytes = frame.format == VideoFrame::Format::YUV420P
			? frame.planes[2].size() : 0;
		if (yBytes < static_cast<size_t>(frame.width) * frame.height ||
			uBytes < static_cast<size_t>(chromaSize.x) * chromaSize.y *
				(frame.format == VideoFrame::Format::NV12 ? 2 : 1) ||
			(frame.format == VideoFrame::Format::YUV420P &&
				vBytes < static_cast<size_t>(chromaSize.x) * chromaSize.y)) return -1;
		const size_t transferBytes = yBytes + uBytes + vBytes;
		if (transferBytes > std::numeric_limits<Uint32>::max()) return -1;
		const SDL_GPUTransferBufferCreateInfo transferInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = static_cast<Uint32>(transferBytes)
		};
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(context->device, &transferInfo);
		if (transfer == nullptr) return -1;
		auto* mapped = static_cast<std::uint8_t*>(
			SDL_MapGPUTransferBuffer(context->device, transfer, false));
		if (mapped == nullptr) {
			SDL_ReleaseGPUTransferBuffer(context->device, transfer);
			return -1;
		}
		SDL_memcpy(mapped, frame.planes[0].data(), yBytes);
		SDL_memcpy(mapped + yBytes, frame.planes[1].data(), uBytes);
		if (vBytes > 0) SDL_memcpy(mapped + yBytes + uBytes, frame.planes[2].data(), vBytes);
		SDL_UnmapGPUTransferBuffer(context->device, transfer);

		SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(context->device);
		if (commands == nullptr) {
			SDL_ReleaseGPUTransferBuffer(context->device, transfer);
			return -1;
		}
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
		auto uploadPlane = [&](SDL_GPUTexture* texture, size_t offset, glm::ivec2 size) {
			const SDL_GPUTextureTransferInfo source{
				.transfer_buffer = transfer,
				.offset = static_cast<Uint32>(offset)
			};
			const SDL_GPUTextureRegion destination{
				.texture = texture,
				.w = static_cast<Uint32>(size.x),
				.h = static_cast<Uint32>(size.y),
				.d = 1
			};
			SDL_UploadToGPUTexture(copy, &source, &destination, reusePlanes);
		};
		uploadPlane(videoYTexture, 0, lumaSize);
		uploadPlane(videoUTexture, yBytes, chromaSize);
		if (frame.format == VideoFrame::Format::YUV420P)
			uploadPlane(videoVTexture, yBytes + uBytes, chromaSize);
		SDL_EndGPUCopyPass(copy);

		const SDL_GPUColorTargetInfo target{
			.texture = imageTexture,
			.load_op = SDL_GPU_LOADOP_DONT_CARE,
			.store_op = SDL_GPU_STOREOP_STORE,
			.cycle = reuseTexture
		};
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
		if (pass == nullptr) {
			SDL_CancelGPUCommandBuffer(commands);
			SDL_ReleaseGPUTransferBuffer(context->device, transfer);
			return -1;
		}
		bindPipeline(pass, videoYUVPipeline);
		SDL_GPUTextureSamplerBinding bindings[3] = {
			{.texture = videoYTexture, .sampler = imageSampler},
			{.texture = videoUTexture, .sampler = imageSampler},
			{.texture = frame.format == VideoFrame::Format::YUV420P
				? videoVTexture : videoYTexture, .sampler = imageSampler}
		};
		SDL_BindGPUFragmentSamplers(pass, 0, bindings, 3);
		const VideoYUVDataFrag uniforms{
			.format = frame.format == VideoFrame::Format::NV12 ? 1 : 2,
			.colorSpace = static_cast<int>(frame.colorSpace),
			.fullRange = frame.fullRange ? 1 : 0
		};
		SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
		const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(frame.outputWidth),
			static_cast<float>(frame.outputHeight), 0.0f, 1.0f};
		SDL_SetGPUViewport(pass, &viewport);
		SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
		SDL_EndGPURenderPass(pass);
		SDL_SubmitGPUCommandBuffer(commands);
		SDL_ReleaseGPUTransferBuffer(context->device, transfer);
	}
	videoTextureSize = frameSize;
	bool refreshedVideoBackground = false;
	if (firstFrame) videoSolidColorValid = false;
	if (firstFrame || (updateBlur && (blurTexture == nullptr || blurTextureNext == nullptr))) {
		blitBlurTexture(context, imageTexture, static_cast<Uint32>(frame.outputWidth),
			static_cast<Uint32>(frame.outputHeight));
		videoBlurActive = true;
		blurTransitionStart = BlurClock::now();
		imageDataFrag.blurMix = 0.0f;
		refreshedVideoBackground = true;
	} else if (updateBlur && BlurClock::now() - blurTransitionStart >= videoBlurInterval) {
		std::swap(blurTexture, blurTextureNext);
		blitBlurTexture(context, imageTexture, static_cast<Uint32>(frame.outputWidth),
			static_cast<Uint32>(frame.outputHeight), true);
		blurTransitionStart = BlurClock::now();
		imageDataFrag.blurMix = 0.0f;
		refreshedVideoBackground = true;
	}
	if (refreshedVideoBackground) {
		const glm::vec4 nextColor = getVideoBackgroundColor(frame);
		if (!videoSolidColorValid) {
			clearColorSolid = nextColor;
			videoSolidColorValid = true;
			videoSolidTransitionActive = false;
		} else {
			updateVideoSolidColor();
			videoSolidTransitionSource = clearColorSolid;
			videoSolidTransitionTarget = nextColor;
			videoSolidTransitionStart = BlurClock::now();
			videoSolidTransitionActive = true;
		}
	}
	uploadedVideoTime = frame.presentationTime;
	return 0;
}

// Advance the solid and blurred background transitions between video snapshots.
void Image::updateVideoBackgroundAnimation() {
	updateVideoBackgroundAnimationImpl();
	if (!videoBlurActive) {
		imageDataFrag.blurMix = 0.0f;
		return;
	}
	imageDataFrag.blurMix = glm::clamp(
		std::chrono::duration<float>(BlurClock::now() - blurTransitionStart).count() /
		std::chrono::duration<float>(videoBlurInterval).count(), 0.0f, 1.0f);
}

// Replace or clear the disc-menu preview and animate its matching background.
void Image::updateDiscBackground(Context* context, SDL_Surface* preview) {
	const bool hadPreview = discBackgroundTexture != nullptr;
	if (preview == nullptr) {
		if (discBackgroundTexture) SDL_ReleaseGPUTexture(context->device, discBackgroundTexture);
		discBackgroundTexture = nullptr;
		videoBlurActive = false;
		videoSolidTransitionActive = false;
		videoSolidColorValid = false;
		clearColorSolid = clearColorDark;
		return;
	}
	if (uploadTexture(context, preview, &discBackgroundTexture, "Disc preview background", false, false) != 0)
		return;
	Context previewContext = *context;
	previewContext.imageType = Color_Only;
	if (hadPreview) std::swap(blurTexture, blurTextureNext);
	blitBlurTexture(&previewContext, discBackgroundTexture, preview->w, preview->h, hadPreview, false);
	videoBlurActive = true;
	blurTransitionStart = BlurClock::now();
	imageDataFrag.blurMix = 0.0f;
	updateVideoSolidColor();
	videoSolidTransitionSource = clearColorSolid;
	videoSolidTransitionTarget = getBackgroundColor(preview, 4, preview->w, preview->h);
	videoSolidTransitionStart = BlurClock::now();
	videoSolidTransitionActive = true;
}

// Upload a 16-bit video depth map and refine it with the available color guide.
int Image::updateVideoDepth(Context* context, const std::vector<std::uint16_t>& values,
		int width, int height) {
	if (context == nullptr || context->device == nullptr || width <= 0 || height <= 0 ||
		values.size() != static_cast<size_t>(width) * height) return -1;
	const glm::ivec2 size{width, height};
	const bool reuseTexture = videoDepthTexture != nullptr && videoDepthTextureSize == size;
	if (!reuseTexture) {
		if (videoDepthTexture != nullptr)
			SDL_ReleaseGPUTexture(context->device, videoDepthTexture);
		const SDL_GPUTextureCreateInfo textureInfo{
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R16_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
			.width = static_cast<Uint32>(width),
			.height = static_cast<Uint32>(height),
			.layer_count_or_depth = 1,
			.num_levels = 1
		};
		videoDepthTexture = SDL_CreateGPUTexture(context->device, &textureInfo);
		if (videoDepthTexture == nullptr) return -1;
		SDL_SetGPUTextureName(context->device, videoDepthTexture, "Video Depth Texture");
		videoDepthTextureSize = size;
	}

	const size_t uploadBytes = values.size() * sizeof(std::uint16_t);
	if (uploadBytes > std::numeric_limits<Uint32>::max()) return -1;
	const SDL_GPUTransferBufferCreateInfo transferInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = static_cast<Uint32>(uploadBytes)
	};
	SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(context->device, &transferInfo);
	if (transfer == nullptr) return -1;
	void* mapped = SDL_MapGPUTransferBuffer(context->device, transfer, false);
	if (mapped == nullptr) {
		SDL_ReleaseGPUTransferBuffer(context->device, transfer);
		return -1;
	}
	SDL_memcpy(mapped, values.data(), uploadBytes);
	SDL_UnmapGPUTransferBuffer(context->device, transfer);
	SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(context->device);
	if (commands == nullptr) {
		SDL_ReleaseGPUTransferBuffer(context->device, transfer);
		return -1;
	}
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
	const SDL_GPUTextureTransferInfo source{.transfer_buffer = transfer};
	const SDL_GPUTextureRegion destination{
		.texture = videoDepthTexture,
		.w = static_cast<Uint32>(width),
		.h = static_cast<Uint32>(height),
		.d = 1
	};
	SDL_UploadToGPUTexture(copy, &source, &destination, reuseTexture);
	SDL_EndGPUCopyPass(copy);
	SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(context->device, transfer);
	// Reconstruct between inference texels, rather than baking the RGB-guided
	// edges onto the coarse inference grid and magnifying those stair steps.
	if (imageTexture != nullptr) {
		if (SDL_GPUTexture* refined = refineDepthTextureGPU(context, imageTexture,
				videoDepthTexture, width, height, 2); refined != nullptr) {
			SDL_ReleaseGPUTexture(context->device, videoDepthTexture);
			videoDepthTexture = refined;
			videoDepthTextureSize = glm::ivec2(width * 2, height * 2);
		}
	}
	return 0;
}

// Release the current video depth texture and reset its dimensions.
void Image::clearVideoDepth(Context* context) {
	if (context != nullptr && context->device != nullptr && videoDepthTexture != nullptr)
		SDL_ReleaseGPUTexture(context->device, videoDepthTexture);
	videoDepthTexture = nullptr;
	videoDepthTextureSize = {};
}

// Release video and background textures so no output can display a stale frame.
void Image::clearVideoFrame(Context* context) {
	// Hiding the image alone leaves its last frame available to native output.
	for (auto* texture : {&imageTexture, &videoYTexture, &videoUTexture,
		&videoVTexture, &blurTexture, &blurTextureNext}) {
		if (*texture != nullptr) SDL_ReleaseGPUTexture(context->device, *texture);
		*texture = nullptr;
	}
	clearVideoDepth(context);
	videoTextureSize = videoYUVLumaSize = videoYUVChromaSize = {};
	videoBlurActive = videoSolidTransitionActive = videoSolidColorValid = false;
	imageDataFrag.blurMix = 0.0f;
	clearColorSolid = clearColorDark;
	clearColorCurrent = context->backgroundStyle == Light ? clearColorLight : clearColorDark;
	nativeOutputSourceReady = false;
}

// Build or clear the subtitle overlay from a text or bitmap cue.
void Image::updateVideoSubtitle(Context* context,
	const std::shared_ptr<const VideoSubtitle>& subtitle) {
	if (context == nullptr) return;
	auto clearSubtitleTextures = [&]() {
		if (subtitleTexture != nullptr) {
			SDL_ReleaseGPUTexture(context->device, subtitleTexture);
			subtitleTexture = nullptr;
		}
		if (subtitleShadowTexture != nullptr) {
			SDL_ReleaseGPUTexture(context->device, subtitleShadowTexture);
			subtitleShadowTexture = nullptr;
		}
		subtitleTextSize = {};
		subtitleBitmap = false;
		subtitleBitmapPosition = {};
		subtitleBitmapCanvasSize = {};
	};
	if (subtitle == nullptr || (subtitle->format == VideoSubtitle::Format::Text && subtitle->text.empty())) {
		clearSubtitleTextures();
		return;
	}
	if (subtitle->format == VideoSubtitle::Format::Bitmap) {
		if (subtitle->width <= 0 || subtitle->height <= 0 ||
			subtitle->canvasWidth <= 0 || subtitle->canvasHeight <= 0 ||
			subtitle->rgba.size() < static_cast<size_t>(subtitle->width) * subtitle->height * 4) {
			clearSubtitleTextures();
			return;
		}
		clearSubtitleTextures();
		SDL_Surface* surface = SDL_CreateSurfaceFrom(subtitle->width, subtitle->height,
			SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(subtitle->rgba.data()),
			subtitle->width * 4);
		if (surface == nullptr) return;
		const int result = uploadTexture(context, surface, &subtitleTexture,
			"PGS Subtitle Texture", false, false, false);
		SDL_DestroySurface(surface);
		if (result < 0) return;
		subtitleBitmap = true;
		subtitleBitmapPosition = { subtitle->x, subtitle->y };
		subtitleBitmapCanvasSize = { subtitle->canvasWidth, subtitle->canvasHeight };
		subtitleTextSize = { subtitle->width, subtitle->height };
		return;
	}

	const auto& text = subtitle->text;
	clearSubtitleTextures();
	if (subtitleFont == nullptr) return;
	const auto wrapWidth = std::max(256.0f, context->windowSize.x * 0.95f);
	const auto outlineSize = 0;
	const SDL_Color outlineColor = { 255, 255, 255, 255 };
	const SDL_Color textColor = { 255, 255, 255, 255 };

	TTF_SetFontOutline(subtitleFont, outlineSize);
	SDL_Surface* outlinedText = TTF_RenderText_Blended_Wrapped(subtitleFont,
		text.c_str(), text.size(), outlineColor, static_cast<int>(wrapWidth));
	TTF_SetFontOutline(subtitleFont, 0);
	SDL_Surface* plainText = TTF_RenderText_Blended_Wrapped(subtitleFont,
		text.c_str(), text.size(), textColor, static_cast<int>(wrapWidth));
	if (outlinedText == nullptr || plainText == nullptr) {
		SDL_Log("Could Not Render Subtitle Font.");
		if (outlinedText != nullptr) SDL_DestroySurface(outlinedText);
		if (plainText != nullptr) SDL_DestroySurface(plainText);
		return;
	}
	SDL_Surface* subtitleData = SDL_CreateSurface(outlinedText->w, outlinedText->h,
		SDL_PIXELFORMAT_ABGR8888);
	if (subtitleData == nullptr) {
		SDL_DestroySurface(outlinedText);
		SDL_DestroySurface(plainText);
		return;
	}
	SDL_BlitSurface(outlinedText, nullptr, subtitleData, nullptr);
	const SDL_Rect textDestination = { outlineSize, outlineSize, plainText->w, plainText->h };
	SDL_Surface* whiteText = SDL_CreateSurface(outlinedText->w, outlinedText->h,
		SDL_PIXELFORMAT_ABGR8888);
	if (whiteText == nullptr) {
		SDL_DestroySurface(outlinedText);
		SDL_DestroySurface(plainText);
		SDL_DestroySurface(subtitleData);
		return;
	}
	SDL_BlitSurface(plainText, nullptr, whiteText, &textDestination);
	SDL_Surface* rgbaShadowData = SDL_ConvertSurface(subtitleData, SDL_PIXELFORMAT_ABGR8888);
	SDL_BlitSurface(plainText, nullptr, subtitleData, &textDestination);
	SDL_DestroySurface(outlinedText);
	SDL_DestroySurface(plainText);
	subtitleTextSize = { subtitleData->w, subtitleData->h };
	if (subtitleTexture != nullptr) {
		SDL_ReleaseGPUTexture(context->device, subtitleTexture);
		subtitleTexture = nullptr;
	}
	if (subtitleShadowTexture != nullptr) {
		SDL_ReleaseGPUTexture(context->device, subtitleShadowTexture);
		subtitleShadowTexture = nullptr;
	}
	SDL_Surface* rgbaSubtitleData = SDL_ConvertSurface(subtitleData, SDL_PIXELFORMAT_ABGR8888);
	if (rgbaShadowData != nullptr && rgbaSubtitleData != nullptr) {
		uploadTexture(context, rgbaShadowData, &subtitleShadowTexture, "Subtitle Shadow Texture");
		uploadTexture(context, rgbaSubtitleData, &subtitleTexture, "Subtitle Texture");
	}
	if (rgbaShadowData != nullptr) SDL_DestroySurface(rgbaShadowData);
	if (rgbaSubtitleData != nullptr) SDL_DestroySurface(rgbaSubtitleData);
	SDL_DestroySurface(subtitleData);
	SDL_DestroySurface(whiteText);
}

// Reset the CPU surface and placement state used to build the menu text atlas.
void Image::initMenuTexture() {
	if (menuTextSurface) SDL_DestroySurface(menuTextSurface);
	optionsLabels.clear();
	optionTextures.clear();
	menuTextSurface = SDL_CreateSurface((int)menuTextureSize.x, (int)menuTextureSize.y,
		SDL_PIXELFORMAT_ABGR8888);
	menuTextureOffset = { 2, 2 };
}

// Render menu labels and choices into their shared GPU text texture.
void Image::createMenuAssets(Context* context) {
	const auto originalStyle = TTF_GetFontStyle(menuFont);
	for (const Choice& choice : *context->menuChoices) {
		if (choice.bold) TTF_SetFontStyle(menuFont, originalStyle | TTF_STYLE_BOLD);
		addToMenuText(context, choice.label);
		for (const std::string& option : choice.options) {
			addToMenuText(context, option);
			if (choice.inlineText && (choice.readOnly || choice.options.size() == 1)) {
				addToMenuText(context, choice.label + ": " + option);
				addToMenuText(context, choice.label + ": " + option + " (Unavailable)");
			}
		}
		if (choice.inlineText && !choice.options.empty()) addToMenuText(context, choice.label + ":");
		if (choice.bold) TTF_SetFontStyle(menuFont, originalStyle);
	}
	uploadTexture(context, menuTextSurface, &menuTexture, "Menu Texture");
}

// Append a rendered label to the menu atlas and record its texture coordinates.
void Image::addToMenuText(Context* context, const std::string& text) {
	static SDL_Color fgColor = { 255, 255, 255, 255 };
	optionsLabels.push_back(text);
	auto menuText = text;
	if (menuText.empty()) menuText = "   ";
	static auto padding = 2.0f;
	auto stringLength = strlen(menuText.c_str());
	auto menuData = TTF_RenderText_Blended(menuFont, menuText.c_str(),
		stringLength, fgColor);
	if (menuData == nullptr) {
		SDL_Log("Could Not Render Menu Font.");
		return;
	}
	static auto menuHeightMax = 0.0f;
	glm::vec2 menuTextSize = { menuData->w, menuData->h };
	menuHeightMax = std::max(menuHeightMax, menuTextSize.y);
	if (menuTextureOffset.x + menuTextSize.x > menuTextureSize.x) {
		menuTextureOffset.x = padding;
		menuTextureOffset.y += menuHeightMax + padding;
		menuHeightMax = 0.0f;
	}
	auto rgbaMenuData = SDL_ConvertSurface(menuData, SDL_PIXELFORMAT_ABGR8888);
	if (rgbaMenuData == nullptr) {
		SDL_Log("Could Not Create Menu Texture Surface.");
		SDL_DestroySurface(menuData);
		return;
	}
	SDL_SetSurfaceBlendMode(rgbaMenuData, SDL_BLENDMODE_NONE);

	SDL_Rect blitRect{ (int)menuTextureOffset.x, (int)menuTextureOffset.y, 0, 0 };
	SDL_BlitSurface(rgbaMenuData, nullptr, menuTextSurface, &blitRect);
	OptionsTexture option{};
	option.offset = menuTextureOffset;
	option.size = menuTextSize;
	optionTextures[text] = option;
	menuTextureOffset += glm::vec2(menuTextSize.x + padding, 0.0);

	SDL_DestroySurface(menuData);
	SDL_DestroySurface(rgbaMenuData);
}


// Calculate menu row geometry, hit areas, and scrolling limits for the current window.
void Image::saveMenuLayout(Context* context) {
	int windowWidth, windowHeight;
	SDL_GetWindowSizeInPixels(context->window, &windowWidth, &windowHeight);
	glm::vec2 windowSize = { windowWidth, windowHeight };

	auto aspectScale = glm::vec2(1.0);
	if (context->mode == SBS_Full && context->fullscreen) {
		aspectScale.x = 2.0;
	}

	auto buttonMargin = style.getButtonMargin(Style::getCurrentScale()) * context->displayScale;
	auto menuWidth = (style.getOptionsSize(Style::getCurrentScale()) + buttonMargin) * 4.0f *
		aspectScale.x * context->displayScale;
	auto choiceCentered = (windowSize.x - menuWidth) * 0.5f;
	// Initial breathing room above the choices scrolls with the content.
	menuTopInset = std::min(128.0f, windowSize.y * 0.5f);
	auto choiceStart = glm::vec3(choiceCentered, windowSize.y - menuTopInset - buttonMargin, 0.0f);
	auto choiceIndex = 0;

	for (auto& choice : *context->menuChoices) {
		if (choiceIndex > 0) choiceStart.y -= 24.0f;
		choiceIndex++;
		if (choice.inlineText) {
			const bool singleLine = choice.readOnly || choice.options.size() <= 1;
			const auto heading = choice.label + (choice.options.empty() ? "" : ":");
			const float gap = 20.0f * context->displayScale;
			float width = optionTextures[heading].size.x;
			float height = optionTextures[heading].size.y;
			if (singleLine) {
				for (const auto& option : choice.options) {
					const auto size = optionTextures[choice.label + ": " + option].size;
					width = std::max(width, size.x);
					height = std::max(height, size.y);
				}
			} else {
				for (const auto& option : choice.options) width += gap + optionTextures[option].size.x;
			}
			const float centerY = choiceStart.y - height * 0.5f;
			float left = (windowSize.x - width * aspectScale.x) * 0.5f;
			choice.layout.position = { singleLine ? windowSize.x * 0.5f :
				left + optionTextures[heading].size.x * aspectScale.x * 0.5f, centerY, 0.0f };
			choice.layout.size = { optionTextures[heading].size, 1.0f };
			choice.layouts.clear();
			choice.active = true;
			if (singleLine && !choice.readOnly) {
				choice.layouts.push_back({ choice.layout.position, { width, height, 1.0f } });
			} else if (!choice.readOnly) {
				left += optionTextures[heading].size.x * aspectScale.x;
				for (const auto& option : choice.options) {
					const auto size = optionTextures[option].size;
					left += gap * aspectScale.x;
					choice.layouts.push_back({ { left + size.x * aspectScale.x * 0.5f, centerY, 0.0f },
						{ size, 1.0f } });
					left += size.x * aspectScale.x;
				}
			}
			choiceStart.y -= height + buttonMargin * 2.0f;
			continue;
		}

		auto centerPadChoice = menuWidth - optionTextures[choice.label].size.x * context->displayScale;
		auto spritePosition = choiceStart + glm::vec3(centerPadChoice * 0.5f, 0.0f, 0.0f) +
				glm::vec3(optionTextures[choice.label].size.x * context->displayScale,
				-optionTextures[choice.label].size.y, 0.0) * glm::vec3(0.5, 0.5, 0.0);
		auto spriteSize = glm::vec3(optionTextures[choice.label].size, 1.0);

		choice.layout.position = spritePosition + glm::vec3(0.0f, -1.0f, 0.0f);
		choice.layout.size = spriteSize;
		choice.layouts.clear();
		choice.active = true;

		choiceStart.y -= optionTextures[choice.label].size.y + buttonMargin * 5.0f;

		auto optionIndex = 0;
		auto optionsNum = choice.options.size();
		auto optionsLeft = optionsNum;

		for (const auto& option : choice.options) {
			if (optionIndex % 4 == 0) {
				auto optionsWidth = (style.getOptionsSize(Style::getCurrentScale()) +
					buttonMargin) * (float)optionsLeft * aspectScale.x * context->displayScale;
				if (optionsLeft < 4) {
					choiceCentered = (windowSize.x - optionsWidth) * 0.5f;
				} else {
					choiceCentered = (windowSize.x - menuWidth) * 0.5f;
				}

				optionsLeft -= 4;
				choiceStart.x = choiceCentered;
				if (optionIndex > 0)
					choiceStart.y -= optionTextures[choice.label].size.y + buttonMargin * 5.0f;
			}
			auto centerPadOption = style.getOptionsSize(Style::getCurrentScale()) * context->displayScale -
				optionTextures[option].size.x;
			spritePosition = choiceStart +
				glm::vec3((optionTextures[option].size.x + centerPadOption) * aspectScale.x, -optionTextures[option].size.y, 0.0) * glm::vec3(0.5, 0.5, 0.0);
			spriteSize = glm::vec3(optionTextures[option].size, 1.0);

			MenuLayout layout{};
			layout.position = spritePosition;
			layout.size = spriteSize;
			choice.layouts.push_back(layout);

			choiceStart.x += (style.getOptionsSize(Style::getCurrentScale()) + buttonMargin) * aspectScale.x * context->displayScale;
			optionIndex++;
		}

		choiceCentered = (windowSize.x - menuWidth) * 0.5f;
		choiceStart.x = choiceCentered;
		choiceStart.y -= optionTextures[choice.label].size.y + buttonMargin * 2.0f;
	}
	menuMargin.y = 0.0f;
	menuScrollLimit = std::max(0.0f, 24.0f - (choiceStart.y + menuMargin.y));
	menuScroll = std::clamp(menuScroll, 0.0f, menuScrollLimit);
	menuMargin.y += menuScroll;
}

// Move the menu within its scroll limits and update its drawing offset.
void Image::scrollMenu(float pixels) {
	const float previous = menuScroll;
	menuScroll = std::clamp(menuScroll + pixels, 0.0f, menuScrollLimit);
	menuMargin.y += menuScroll - previous;
}


// Create the window, GPU resources, shaders, fonts, and initial image state used by the renderer.
int Image::init(Context* context, FileInfo& imageInfo) {
	context->nativeOutputWindow = nullptr;
	auto currentDisplay = SDL_GetPrimaryDisplay();
	auto displayMode = SDL_GetCurrentDisplayMode(currentDisplay);
	auto displaySize = glm::vec3((float)displayMode->w, (float)displayMode->h, 0.0f);
	auto virtualSize = glm::vec2(displayMode->w, displayMode->h) * displayMode->pixel_density;
	context->virtualSize = virtualSize;

	auto displayMin = std::min(displayMode->w, displayMode->h);
	auto firstImageSize = Utils::getSafeSize({ displayMin, displayMin }, displaySize,
		(float)safePercent, true);

	SDL_Surface* imageData = nullptr;

	if (!imageInfo.path.empty()) {
		imageData = Core::loadImageDirect(imageInfo.path);
		if (imageData == nullptr) {
			SDL_Log("Could Not Load Image: %s", imageInfo.path.c_str());
			return -1;
		}

		firstImageSize = glm::vec2((float)imageData->w, (float)imageData->h);
		if (imageInfo.type == Color_Plus_Depth || imageInfo.type == Side_By_Side_Full ||
			imageInfo.type == Side_By_Side_Swap) {
			firstImageSize.x /= 2.0;
		} else if (imageInfo.type == Top_And_Bottom_Full) {
			firstImageSize.y /= 2.0;
		} else if (imageInfo.type == Light_Field_LKG) {
			auto gridSize = Core::getGridInfo(imageInfo.base);
			context->gridSize = gridSize;
			firstImageSize.x /= gridSize.x;
			firstImageSize.y /= gridSize.y;
		}
	}

	imageSize = glm::vec3(firstImageSize.x, firstImageSize.y, 0);

	context->displaySize = displaySize;
	context->imageSize = firstImageSize;
	updateSize(context);

	context->currentZoom = 1.0f;

	auto fileName = imageInfo.name;
	if (fileName.empty()) fileName = "Rendepth";

	auto windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (useBorderlessWindow) windowFlags |= SDL_WINDOW_BORDERLESS;

	firstImageSize = Utils::getSafeSize(firstImageSize, displaySize,
			(float)safePercent, true);

	context->window = SDL_CreateWindow(fileName.c_str(), static_cast<int>(firstImageSize.x),
		static_cast<int>(firstImageSize.y), windowFlags);

	if (context->window == nullptr) {
		SDL_Log("Window Creation Failed.");
		return -1;
	}

	SDL_SetWindowPosition(context->window, SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED);
	context->displayScale = SDL_GetWindowDisplayScale(context->window);
	context->pixelDensity = SDL_GetWindowPixelDensity(context->window);
	Style::calculateScale(virtualSize / context->displayScale);
	SDL_SetWindowMinimumSize(context->window, 512, 768);
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
	SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0");
	SDL_SetWindowFullscreenMode(context->window, nullptr);

	auto gpuFormat = SDL_GPU_SHADERFORMAT_INVALID;
#ifdef WIN32
	gpuFormat = SDL_GPU_SHADERFORMAT_DXIL;
#elif defined(__APPLE__)
	gpuFormat = SDL_GPU_SHADERFORMAT_MSL;
#else
	gpuFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#endif
	context->device = SDL_CreateGPUDevice(
		gpuFormat,
		false,
		nullptr);
	if (context->device == nullptr) {
		SDL_Log("GPU Create Device Failed");
		return -1;
	}

	SDL_SetGPUAllowedFramesInFlight(context->device, 3);

	SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(context->device);
	Core::gpuShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;

	if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
		Core::gpuShaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
		Core::gpuShaderFormat  = SDL_GPU_SHADERFORMAT_MSL;
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
		Core::gpuShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
	} else {
		SDL_Log("Invalid GPU Shader Format.");
		return 1;
	}

	std::filesystem::path exePath = SDL_GetBasePath();
	std::filesystem::path appPath = exePath.parent_path().parent_path();
	std::filesystem::path assetPath = appPath / "Assets";
	std::filesystem::path appIconPath = assetPath / "AppIcons.png";
	std::filesystem::path appIconAbsPath = exePath / appIconPath;

	SDL_Surface* iconData = Core::loadImageDirect(appIconAbsPath.string());
	if (iconData == nullptr) {
		SDL_Log("Could Not Load Icon Image.");
		return -1;
	}
	uploadTexture(context, iconData, &iconTexture, "Icon Texture");
	SDL_DestroySurface(iconData);

	std::filesystem::path sliderIconPath = assetPath / "CircleIcon.png";
	std::filesystem::path sliderIconAbsPath = exePath / sliderIconPath;
	SDL_Surface* sliderData = Core::loadImageDirect(sliderIconAbsPath.string());
	if (sliderData == nullptr) {
		SDL_Log("Could Not Load Slider Image.");
		return -1;
	}
	uploadTexture(context, sliderData, &sliderTexture, "Slider Texture");
	SDL_DestroySurface(sliderData);

	if (!TTF_Init()) {
		SDL_Log("Could Not Load TTF Font System.");
		return -1;
	}

	initFonts(context);
	context->infoText = "";

	initMenuTexture();
	createMenuAssets(context);
	saveMenuLayout(context);

	imageDataVert.displayImageAspect = { 1.0, 1.0, 1.0 };
	imageDataFrag.visibility = context->visibility;
	imageDataFrag.mode = context->mode;
	imageDataFrag.type = context->imageType;
	imageDataFrag.stereoStrength = 1.0f;
	imageDataFrag.stereoDepth = 1.0f;
	imageDataFrag.stereoOffset = 0.005;
	imageDataFrag.depthEffect = 1.0f;
	imageDataFrag.effectRandom = 0;
	imageDataVert.transform = glm::mat4(1.0);
	imageDataVert.fillScreen = 0;
	iconDataVert.transform = glm::mat4(1.0f);
	iconDataVert.projection = glm::mat4(1.0f);
	iconDataVert.gridSize = glm::vec2(gridSize, gridSize);
	iconDataVert.gridOffset = glm::vec2(8.0, 8.0);
	iconDataFrag.color = glm::vec4(1.0);
	iconDataFrag.visibility = 1.0;
	iconDataFrag.rotation = 0.0;
	iconDataFrag.animated = 0;
	iconDataFrag.force = 0;

	if (!SDL_ClaimWindowForGPUDevice(context->device, context->window)) {
		SDL_Log("GPU Cannot Claim Window");
		return -1;
	}
	// Load the shader stages shared by image rendering, video conversion, depth refinement, and UI
	// pipelines.
	SDL_GPUShader* imageVertexShader = Core::loadShader(context->device,
		"Image.vert", 0, 1, 0, 0);
	if (imageVertexShader == nullptr) {
		SDL_Log("Failed To Create Image Vertex Shader.");
		return -1;
	}

	SDL_GPUShader* imageFragmentShader = Core::loadShader(context->device,
		"Image.frag", 4, 1, 0, 0);
	if (imageFragmentShader == nullptr) {
		SDL_Log("Failed To Create Image Fragment Shader.");
		return -1;
	}
	SDL_GPUShader* lanczosFragmentShader = Core::loadShader(context->device,
		"Lanczos.frag", 1, 1, 0, 0);
	if (lanczosFragmentShader == nullptr) {
		SDL_Log("Failed To Create Lanczos Fragment Shader.");
		return -1;
	}
	SDL_GPUShader* depthRefineFragmentShader = Core::loadShader(context->device,
		"DepthRefine.frag", 2, 1, 0, 0);
	if (depthRefineFragmentShader == nullptr) {
		SDL_Log("Failed To Create Depth Refine Shader.");
		return -1;
	}
	SDL_GPUShader* videoYUVVertexShader = Core::loadShader(context->device,
		"VideoYUV.vert", 0, 0, 0, 0);
	SDL_GPUShader* videoYUVFragmentShader = Core::loadShader(context->device,
		"VideoYUV.frag", 3, 1, 0, 0);
	if (videoYUVVertexShader == nullptr || videoYUVFragmentShader == nullptr) {
		SDL_Log("Failed To Create Video YUV Shaders.");
		if (videoYUVVertexShader != nullptr)
			SDL_ReleaseGPUShader(context->device, videoYUVVertexShader);
		if (videoYUVFragmentShader != nullptr)
			SDL_ReleaseGPUShader(context->device, videoYUVFragmentShader);
		return -1;
	}
	SDL_GPUShader* interlacerFragmentShader = Core::loadShader(context->device,
		"Interlacer.frag", 2, 1, 0, 0);
	if (interlacerFragmentShader == nullptr) {
		SDL_Log("Failed To Create Interlacer Fragment Shader.");
		return -1;
	}

	SDL_GPUShader* iconVertexShader = Core::loadShader(context->device,
		"Icon.vert", 0, 1, 0, 0);
	if (iconVertexShader == nullptr) {
		SDL_Log("Failed To Create Icon Vertex Shader.");
		return -1;
	}

	SDL_GPUShader* iconFragmentShader = Core::loadShader(context->device,
		"Icon.frag", 1, 1, 0, 0);
	if (iconFragmentShader == nullptr) {
		SDL_Log("Failed To Create Icon Fragment Shader.");
		return -1;
	}

	SDL_GPUShader* spriteVertexShader = Core::loadShader(context->device,
		"Sprite.vert", 0, 1, 0, 0);
	if (spriteVertexShader == nullptr) {
		SDL_Log("Failed To Create Sprite Vertex Shader.");
		return -1;
	}

	SDL_GPUShader* spriteFragmentShader = Core::loadShader(context->device,
		"Sprite.frag", 1, 1, 0, 0);
	if (spriteFragmentShader == nullptr) {
		SDL_Log("Failed To Create Sprite Fragment Shader.");
		return -1;
	}

	// Describe the shared textured quad so each pipeline can use the same vertex and index buffers.
	SDL_GPUVertexBufferDescription vertexBufferDescription[1] =  {{
		.slot = 0,
		.pitch = sizeof(Vertex),
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
		.instance_step_rate = 0
	}};

	SDL_GPUVertexAttribute vertexBufferAttribute[2] = {{
		.location = 0,
		.buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		.offset = 0
	}, {
		.location = 1,
		.buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		.offset = sizeof(float) * 3
	}};

	SDL_GPUColorTargetDescription colorTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.blend_state = SDL_GPUColorTargetBlendState{
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};

	SDL_GPUGraphicsPipelineCreateInfo imagePipelineCreateInfo = {
		.vertex_shader = imageVertexShader,
		.fragment_shader = imageFragmentShader,
		.vertex_input_state = SDL_GPUVertexInputState{
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = colorTargetDescription,
			.num_color_targets = 1
		}
	};

	imagePipeline = SDL_CreateGPUGraphicsPipeline(context->device, &imagePipelineCreateInfo);
	if (imagePipeline == nullptr) {
		SDL_Log("Failed To Create Image Pipeline.");
		return -1;
	}
	// Reuse the image pipeline layout for offscreen filtering, choosing render-target formats for each
	// pass.
	SDL_GPUGraphicsPipelineCreateInfo lanczosPipelineInfo = imagePipelineCreateInfo;
	lanczosPipelineInfo.fragment_shader = lanczosFragmentShader;
	SDL_GPUColorTargetDescription lanczosTargetDescription{
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
	};
	lanczosPipelineInfo.target_info.color_target_descriptions = &lanczosTargetDescription;
	lanczosPipeline = SDL_CreateGPUGraphicsPipeline(context->device,
		&lanczosPipelineInfo);
	if (lanczosPipeline == nullptr) {
		SDL_Log("Failed To Create Lanczos Pipeline.");
		return -1;
	}
	SDL_GPUGraphicsPipelineCreateInfo depthRefinePipelineInfo = imagePipelineCreateInfo;
	depthRefinePipelineInfo.fragment_shader = depthRefineFragmentShader;
	SDL_GPUColorTargetDescription depthRefineTargetDescription{
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
	};
	depthRefinePipelineInfo.target_info.color_target_descriptions = &depthRefineTargetDescription;
	depthRefinePipeline = SDL_CreateGPUGraphicsPipeline(context->device,
		&depthRefinePipelineInfo);
	if (depthRefinePipeline == nullptr) {
		SDL_Log("Failed To Create Depth Refine Pipeline.");
		return -1;
	}
	SDL_GPUColorTargetDescription depthRefineR16TargetDescription{
		.format = SDL_GPU_TEXTUREFORMAT_R16_UNORM
	};
	depthRefinePipelineInfo.target_info.color_target_descriptions =
		&depthRefineR16TargetDescription;
	depthRefineR16Pipeline = SDL_CreateGPUGraphicsPipeline(context->device,
		&depthRefinePipelineInfo);
	if (depthRefineR16Pipeline == nullptr) {
		SDL_Log("Failed To Create R16 Depth Refine Pipeline.");
		return -1;
	}
	SDL_GPUGraphicsPipelineCreateInfo videoYUVPipelineInfo = imagePipelineCreateInfo;
	videoYUVPipelineInfo.vertex_shader = videoYUVVertexShader;
	videoYUVPipelineInfo.fragment_shader = videoYUVFragmentShader;
	SDL_GPUColorTargetDescription videoYUVTargetDescription{
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
	};
	videoYUVPipelineInfo.target_info.color_target_descriptions = &videoYUVTargetDescription;
	videoYUVPipeline = SDL_CreateGPUGraphicsPipeline(context->device, &videoYUVPipelineInfo);
	if (videoYUVPipeline == nullptr) {
		SDL_Log("Failed To Create Video YUV Pipeline.");
		return -1;
	}

	SDL_GPUColorTargetDescription interlacerTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device,
			context->nativeOutputWindow != nullptr ? context->nativeOutputWindow : context->window)
	}};
	SDL_GPUGraphicsPipelineCreateInfo interlacerPipelineInfo = {
		.vertex_shader = imageVertexShader,
		.fragment_shader = interlacerFragmentShader,
		.vertex_input_state = SDL_GPUVertexInputState{
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = interlacerTargetDescription,
			.num_color_targets = 1
		}
	};
	interlacerPipeline = SDL_CreateGPUGraphicsPipeline(context->device, &interlacerPipelineInfo);
	if (interlacerPipeline == nullptr) {
		SDL_Log("Failed To Create Interlacer Pipeline.");
		return -1;
	}

	SDL_GPUColorTargetDescription iconTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.blend_state = SDL_GPUColorTargetBlendState{
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};

	SDL_GPUGraphicsPipelineCreateInfo iconPipelineCreateInfo = {
		.vertex_shader = iconVertexShader,
		.fragment_shader = iconFragmentShader,
		.vertex_input_state = SDL_GPUVertexInputState{
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = iconTargetDescription,
			.num_color_targets = 1
		}
	};

	iconPipeline = SDL_CreateGPUGraphicsPipeline(context->device, &iconPipelineCreateInfo);
	if (iconPipeline == nullptr) {
		SDL_Log("Failed To Create Icon Pipeline.");
		return -1;
	}

	SDL_GPUColorTargetDescription spriteTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.blend_state = SDL_GPUColorTargetBlendState{
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};

	SDL_GPUGraphicsPipelineCreateInfo spritePipelineCreateInfo = {
		.vertex_shader = spriteVertexShader,
		.fragment_shader = spriteFragmentShader,
		.vertex_input_state = SDL_GPUVertexInputState{
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = spriteTargetDescription,
			.num_color_targets = 1
		}
	};

	spritePipeline = SDL_CreateGPUGraphicsPipeline(context->device, &spritePipelineCreateInfo);
	if (spritePipeline == nullptr) {
		SDL_Log("Failed To Create Sprite Pipeline.");
		return -1;
	}

	SDL_ReleaseGPUShader(context->device, imageVertexShader);
	SDL_ReleaseGPUShader(context->device, imageFragmentShader);
	SDL_ReleaseGPUShader(context->device, lanczosFragmentShader);
	SDL_ReleaseGPUShader(context->device, depthRefineFragmentShader);
	SDL_ReleaseGPUShader(context->device, videoYUVVertexShader);
	SDL_ReleaseGPUShader(context->device, videoYUVFragmentShader);
	SDL_ReleaseGPUShader(context->device, interlacerFragmentShader);

	SDL_ReleaseGPUShader(context->device, iconVertexShader);
	SDL_ReleaseGPUShader(context->device, iconFragmentShader);

	SDL_ReleaseGPUShader(context->device, spriteVertexShader);
	SDL_ReleaseGPUShader(context->device, spriteFragmentShader);


	SDL_GPUSamplerCreateInfo samplerCreateInfo = {
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
		.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
		.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.mip_lod_bias = -0.5f,
		.max_anisotropy = 4,
		.min_lod = -128.0,
		.max_lod = 128.0,
		.enable_anisotropy = true
	};
	imageSampler = SDL_CreateGPUSampler(context->device, &samplerCreateInfo);

	// Upload one reusable quad and its indices instead of rebuilding geometry for each image or UI
	// draw.
	SDL_GPUBufferCreateInfo vertexBufferCreateInfo = {
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = sizeof(Vertex) * 4
	};
	sharedVertexBuffer = SDL_CreateGPUBuffer(
		context->device, &vertexBufferCreateInfo
		);
	SDL_SetGPUBufferName(
		context->device,
		sharedVertexBuffer,
		"Shared Vertex Buffer"
	);

	SDL_GPUBufferCreateInfo indexBufferCreateInfo = {
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = sizeof(Uint16) * 6
	};

	sharedIndexBuffer = SDL_CreateGPUBuffer(
		context->device, &indexBufferCreateInfo);

	SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo = {
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = (sizeof(Vertex) * 4) + (sizeof(Uint16) * 6),
	};

	SDL_GPUTransferBuffer* bufferTransferBuffer = SDL_CreateGPUTransferBuffer(
		context->device, &transferBufferCreateInfo);

	auto transferData = (Vertex*)SDL_MapGPUTransferBuffer(
		context->device,
		bufferTransferBuffer,
		false
	);

	transferData[0] = quadVertices[0];
	transferData[1] = quadVertices[1];
	transferData[2] = quadVertices[2];
	transferData[3] = quadVertices[3];

	auto indexData = (Uint16*) &transferData[4];
	indexData[0] = quadIndices[0];
	indexData[1] = quadIndices[1];
	indexData[2] = quadIndices[2];
	indexData[3] = quadIndices[3];
	indexData[4] = quadIndices[4];
	indexData[5] = quadIndices[5];

	SDL_UnmapGPUTransferBuffer(context->device, bufferTransferBuffer);

	SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(context->device);
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);

	SDL_GPUTransferBufferLocation vertexTransferBufferLocation = {
		.transfer_buffer = bufferTransferBuffer, .offset = 0 };
	SDL_GPUBufferRegion vertexBufferRegion = {
		.buffer = sharedVertexBuffer, .offset = 0, .size = sizeof(Vertex) * 4 };

	SDL_UploadToGPUBuffer(
		copyPass,
		&vertexTransferBufferLocation,
		&vertexBufferRegion,
		false
	);

	SDL_GPUTransferBufferLocation indexTransferBufferLocation = {
		.transfer_buffer = bufferTransferBuffer, .offset = sizeof(Vertex) * 4 };
	SDL_GPUBufferRegion indexBufferRegion = {
		.buffer = sharedIndexBuffer, .offset = 0, .size = sizeof(Uint16) * 6 };

	SDL_UploadToGPUBuffer(
		copyPass,
		&indexTransferBufferLocation,
		&indexBufferRegion,
		false
	);

	SDL_EndGPUCopyPass(copyPass);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);
	SDL_ReleaseGPUTransferBuffer(context->device, bufferTransferBuffer);

	load(context, imageInfo, imageData);

	saveMenuLayout(context);

	if (imageInfo.path.empty()) {
		SDL_SetWindowTitle(context->window, context->appName);
		Core::drawText(context, "Drag & Drop or Click Load Icon", helpFont,
			helpTexture, helpTextSize, "Help Texture");
		displayHelp = true;
		return 1;
	}

	Core::drawText(context, "Rendepth 2D-to-3D Conversion", infoFont, infoTexture,
					infoTextSize, "Info Texture");

	return 0;
}

// Rebuild the image pipeline from disk and replace it only after successful creation.
int Image::reloadShader(Context* context) {
	if (context == nullptr || context->device == nullptr) return -1;

	SDL_WaitForGPUIdle(context->device);
	SDL_GPUShader* vertexShader = Core::loadShader(context->device,
		"Image.vert", 0, 1, 0, 0);
	SDL_GPUShader* fragmentShader = Core::loadShader(context->device,
		"Image.frag", 4, 1, 0, 0);
	if (vertexShader == nullptr || fragmentShader == nullptr) {
		if (vertexShader != nullptr) SDL_ReleaseGPUShader(context->device, vertexShader);
		if (fragmentShader != nullptr) SDL_ReleaseGPUShader(context->device, fragmentShader);
		SDL_Log("Shader reload failed: could not load Image shaders.");
		return -1;
	}

	SDL_GPUVertexBufferDescription vertexBufferDescription[1] = {{
		.slot = 0,
		.pitch = sizeof(Vertex),
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
		.instance_step_rate = 0
	}};
	SDL_GPUVertexAttribute vertexBufferAttribute[2] = {{
		.location = 0,
		.buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		.offset = 0
	}, {
		.location = 1,
		.buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		.offset = sizeof(float) * 3
	}};
	SDL_GPUColorTargetDescription colorTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.blend_state = {
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
		.vertex_shader = vertexShader,
		.fragment_shader = fragmentShader,
		.vertex_input_state = {
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = colorTargetDescription,
			.num_color_targets = 1
		}
	};

	SDL_GPUGraphicsPipeline* replacement = SDL_CreateGPUGraphicsPipeline(
		context->device, &pipelineInfo);
	SDL_ReleaseGPUShader(context->device, vertexShader);
	SDL_ReleaseGPUShader(context->device, fragmentShader);
	if (replacement == nullptr) {
		SDL_Log("Shader reload failed: could not create Image pipeline.");
		return -1;
	}

	SDL_ReleaseGPUGraphicsPipeline(context->device, imagePipeline);
	imagePipeline = replacement;
	SDL_Log("Image shader reloaded.");
	return reloadInterlacerShader(context);
}

// Reload the lenticular interlacer pipeline while retaining the existing pipeline if creation fails.
int Image::reloadInterlacerShader(Context* context) {
	if (context == nullptr || context->device == nullptr || interlacerPipeline == nullptr)
		return -1;
	SDL_WaitForGPUIdle(context->device);
	SDL_GPUShader* vertexShader = Core::loadShader(context->device,
		"Image.vert", 0, 1, 0, 0);
	SDL_GPUShader* fragmentShader = Core::loadShader(context->device,
		"Interlacer.frag", 2, 1, 0, 0);
	if (vertexShader == nullptr || fragmentShader == nullptr) {
		if (vertexShader != nullptr) SDL_ReleaseGPUShader(context->device, vertexShader);
		if (fragmentShader != nullptr) SDL_ReleaseGPUShader(context->device, fragmentShader);
		SDL_Log("Interlacer shader reload failed: could not load shaders.");
		return -1;
	}

	SDL_GPUVertexBufferDescription vertexBufferDescription[1] = {{
		.slot = 0, .pitch = sizeof(Vertex),
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX, .instance_step_rate = 0
	}};
	SDL_GPUVertexAttribute vertexBufferAttribute[2] = {{
		.location = 0, .buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0
	}, {
		.location = 1, .buffer_slot = 0,
		.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = sizeof(float) * 3
	}};
	SDL_Window* outputWindow = context->nativeOutputWindow != nullptr ?
		context->nativeOutputWindow : context->window;
	SDL_GPUColorTargetDescription targetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, outputWindow)
	}};
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
		.vertex_shader = vertexShader,
		.fragment_shader = fragmentShader,
		.vertex_input_state = {
			.vertex_buffer_descriptions = vertexBufferDescription,
			.num_vertex_buffers = 1,
			.vertex_attributes = vertexBufferAttribute,
			.num_vertex_attributes = 2
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = targetDescription,
			.num_color_targets = 1
		}
	};
	SDL_GPUGraphicsPipeline* replacement = SDL_CreateGPUGraphicsPipeline(
		context->device, &pipelineInfo);
	SDL_ReleaseGPUShader(context->device, vertexShader);
	SDL_ReleaseGPUShader(context->device, fragmentShader);
	if (replacement == nullptr) {
		SDL_Log("Interlacer shader reload failed: could not create pipeline.");
		return -1;
	}
	SDL_ReleaseGPUGraphicsPipeline(context->device, interlacerPipeline);
	interlacerPipeline = replacement;
	SDL_Log("Interlacer shader reloaded.");
	return 0;
}

// Reload the application's fonts at sizes appropriate to the current display scale.
int Image::initFonts(Context* context) {
	std::filesystem::path exePath = SDL_GetBasePath();
	std::filesystem::path appPath = exePath.parent_path().parent_path();
	std::filesystem::path assetPath = appPath / "Assets";
	std::filesystem::path fontFilePath = assetPath / "Lato.ttf";
	std::filesystem::path fontFileAbsPath = exePath / fontFilePath;

	if (helpFont) TTF_CloseFont(helpFont);
	helpFont = TTF_OpenFont(fontFileAbsPath.string().c_str(),
		style.getHelpFontSize(Style::getCurrentScale()) * context->displayScale);
	if (helpFont == nullptr) {
		SDL_Log("Could Not Load Help Font: %s", fontFileAbsPath.string().c_str());
		return -1;
	}
	auto fontStyle = TTF_STYLE_NORMAL;
	TTF_SetFontStyle(helpFont, fontStyle);

	if (infoFont) TTF_CloseFont(infoFont);
	infoFont = TTF_OpenFont(fontFileAbsPath.string().c_str(),
		style.getInfoFontSize(Style::getCurrentScale()) * context->displayScale);
	if (infoFont == nullptr) {
		SDL_Log("Could Not Load Info Font: %s", fontFileAbsPath.string().c_str());
		return -1;
	}
	TTF_SetFontStyle(helpFont, fontStyle);

	if (subtitleFont) TTF_CloseFont(subtitleFont);
	const auto subtitleFontPath = assetPath / "NotoSans.ttf";
	const auto subtitleFontSize = style.getHelpFontSize(Style::getCurrentScale()) * 1.15f * context->displayScale;
	subtitleFont = TTF_OpenFont((exePath / subtitleFontPath).string().c_str(), subtitleFontSize);
	if (subtitleFont == nullptr) {
		SDL_Log("Could Not Load Subtitle Font: %s", (exePath / subtitleFontPath).string().c_str());
		return -1;
	}
	TTF_SetFontStyle(subtitleFont, fontStyle);

	if (menuFont) TTF_CloseFont(menuFont);
	menuFont = TTF_OpenFont(fontFileAbsPath.string().c_str(),
		style.getMenuFontSize(Style::getCurrentScale()) * context->displayScale);
	if (menuFont == nullptr) {
		SDL_Log("Could Not Load Menu Font: %s", fontFileAbsPath.string().c_str());
		return -1;
	}
	TTF_SetFontStyle(menuFont, fontStyle);

	return 0;
}

// Transfer an RGBA surface to a new or reusable GPU texture with optional mipmaps and synchronization.
int Image::uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
		const std::string& textureName, bool reuseTexture, bool waitForGpu, bool generateMipmaps) {
	auto textureMipLevels = generateMipmaps
		? (Uint32) std::floor(log2(std::max(imageData->w, imageData->h))) + 1 : 1;
	auto gpuFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	auto bytesPerPixel = 4;
	SDL_GPUTextureCreateInfo textureCreateInfo = {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = gpuFormat,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = (Uint32)imageData->w,
		.height = (Uint32)imageData->h,
		.layer_count_or_depth = 1,
		.num_levels = textureMipLevels
	};
	if (!reuseTexture || *gpuTexture == nullptr) {
		if (*gpuTexture != nullptr) SDL_ReleaseGPUTexture(context->device, *gpuTexture);
		*gpuTexture = SDL_CreateGPUTexture(context->device, &textureCreateInfo);
	}
	if (*gpuTexture == nullptr) return -1;

	SDL_SetGPUTextureName(
		context->device,
		*gpuTexture,
		textureName.c_str()
	);

	SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = (Uint32)imageData->w * (Uint32)imageData->h * bytesPerPixel
	};

	SDL_GPUTransferBuffer* textureTransferBuffer = SDL_CreateGPUTransferBuffer(
		context->device, &transferBufferInfo);
	if (textureTransferBuffer == nullptr) return -1;

	auto textureTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(
		context->device,
		textureTransferBuffer,
		false
	);
	if (textureTransferPtr == nullptr) {
		SDL_ReleaseGPUTransferBuffer(context->device, textureTransferBuffer);
		return -1;
	}
	const size_t rowBytes = static_cast<size_t>(imageData->w) * bytesPerPixel;
	for (int row = 0; row < imageData->h; ++row) {
		SDL_memcpy(textureTransferPtr + static_cast<size_t>(row) * rowBytes,
			static_cast<const Uint8*>(imageData->pixels) + static_cast<size_t>(row) * imageData->pitch,
			rowBytes);
	}
	SDL_UnmapGPUTransferBuffer(context->device, textureTransferBuffer);

	SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(context->device);
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);

	SDL_GPUTextureTransferInfo textureTransferInfo = {
		.transfer_buffer = textureTransferBuffer
	};
	SDL_GPUTextureRegion textureRegion = {
		.texture = *gpuTexture,
		.w = (Uint32)imageData->w,
		.h = (Uint32)imageData->h,
		.d = 1
	};
	SDL_UploadToGPUTexture(
		copyPass, &textureTransferInfo,
		&textureRegion, reuseTexture
	);

	SDL_EndGPUCopyPass(copyPass);
	if (generateMipmaps) SDL_GenerateMipmapsForGPUTexture(uploadCmdBuf, *gpuTexture);

	if (waitForGpu) {
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
		SDL_WaitForGPUFences(context->device, true, &fence, 1);
		SDL_ReleaseGPUFence(context->device, fence);
	} else {
		SDL_SubmitGPUCommandBuffer(uploadCmdBuf);
	}
	SDL_ReleaseGPUTransferBuffer(context->device, textureTransferBuffer);

	return 0;
}

// Resize an RGBA surface with the GPU filtering pipeline and read the result back to CPU memory.
SDL_Surface* Image::upscaleSurfaceGPU(Context* context, const SDL_Surface* source,
		int outputWidth, int outputHeight) {
	if (context == nullptr || context->device == nullptr || source == nullptr ||
		outputWidth <= 0 || outputHeight <= 0 || source->format != SDL_PIXELFORMAT_RGBA32)
		return nullptr;

	SDL_GPUTextureCreateInfo sourceInfo{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
		.width = static_cast<Uint32>(source->w),
		.height = static_cast<Uint32>(source->h),
		.layer_count_or_depth = 1,
		.num_levels = 1};
	SDL_GPUTexture* sourceTexture = SDL_CreateGPUTexture(context->device, &sourceInfo);

	sourceInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	sourceInfo.width = static_cast<Uint32>(outputWidth);
	sourceInfo.height = static_cast<Uint32>(outputHeight);
	SDL_GPUTexture* outputTexture = SDL_CreateGPUTexture(context->device, &sourceInfo);

	SDL_GPUTransferBufferCreateInfo uploadInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = static_cast<Uint32>(source->w * source->h * 4)};
	SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(context->device, &uploadInfo);
	SDL_GPUTransferBufferCreateInfo downloadInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = static_cast<Uint32>(outputWidth * outputHeight * 4)};
	SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(context->device, &downloadInfo);

	SDL_Surface* result = nullptr;
	if (sourceTexture != nullptr && outputTexture != nullptr && upload != nullptr && download != nullptr) {
		auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(context->device, upload, false));
		if (mapped != nullptr) {
			for (int y = 0; y < source->h; ++y)
				SDL_memcpy(mapped + static_cast<size_t>(y) * source->w * 4,
					static_cast<const Uint8*>(source->pixels) + static_cast<size_t>(y) * source->pitch,
					static_cast<size_t>(source->w) * 4);
			SDL_UnmapGPUTransferBuffer(context->device, upload);

			SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(context->device);
			if (commands != nullptr) {
				SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
				SDL_GPUTextureTransferInfo uploadTransfer{.transfer_buffer = upload};
				SDL_GPUTextureRegion sourceRegion{.texture = sourceTexture,
					.w = static_cast<Uint32>(source->w), .h = static_cast<Uint32>(source->h), .d = 1};
				SDL_UploadToGPUTexture(copy, &uploadTransfer, &sourceRegion, false);
				SDL_EndGPUCopyPass(copy);

				if (lanczosPipeline != nullptr && sharedVertexBuffer != nullptr && sharedIndexBuffer != nullptr) {
					const SDL_GPUColorTargetInfo target{
						.texture = outputTexture,
						.load_op = SDL_GPU_LOADOP_DONT_CARE,
						.store_op = SDL_GPU_STOREOP_STORE
					};
					SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
					if (pass != nullptr) {
						bindPipeline(pass, lanczosPipeline);
						const SDL_GPUBufferBinding vertexBinding{.buffer = sharedVertexBuffer, .offset = 0};
						const SDL_GPUBufferBinding indexBinding{.buffer = sharedIndexBuffer, .offset = 0};
						SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
						SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
						const SDL_GPUTextureSamplerBinding bindings[1] = {
							{.texture = sourceTexture, .sampler = imageSampler}
						};
						SDL_BindGPUFragmentSamplers(pass, 0, bindings, 1);
						const ImageDataVert vertexUniforms{
							.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)),
							.projection = glm::mat4(1.0f),
							.displayImageAspect = glm::vec3(1.0f),
							.fillScreen = 0
						};
						const LanczosDataFrag fragmentUniforms{
							.sourceSize = glm::vec2(source->w, source->h),
							.targetSize = glm::vec2(outputWidth, outputHeight)
						};
						SDL_PushGPUVertexUniformData(commands, 0, &vertexUniforms, sizeof(vertexUniforms));
						SDL_PushGPUFragmentUniformData(commands, 0, &fragmentUniforms,
							sizeof(fragmentUniforms));
						const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(outputWidth),
							static_cast<float>(outputHeight), 0.0f, 1.0f};
						SDL_SetGPUViewport(pass, &viewport);
						SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
						SDL_EndGPURenderPass(pass);
					}
				} else {
					SDL_GPUBlitInfo blit{};
					blit.source = {.texture = sourceTexture, .w = static_cast<Uint32>(source->w),
						.h = static_cast<Uint32>(source->h)};
					blit.destination = {.texture = outputTexture,
						.w = static_cast<Uint32>(outputWidth), .h = static_cast<Uint32>(outputHeight)};
					blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
					blit.filter = SDL_GPU_FILTER_LINEAR;
					SDL_BlitGPUTexture(commands, &blit);
				}

				copy = SDL_BeginGPUCopyPass(commands);
				SDL_GPUTextureTransferInfo downloadTransfer{.transfer_buffer = download};
				SDL_GPUTextureRegion outputRegion{.texture = outputTexture,
					.w = static_cast<Uint32>(outputWidth), .h = static_cast<Uint32>(outputHeight), .d = 1};
				SDL_DownloadFromGPUTexture(copy, &outputRegion, &downloadTransfer);
				SDL_EndGPUCopyPass(copy);

				SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
				const bool completed = fence != nullptr &&
					SDL_WaitForGPUFences(context->device, true, &fence, 1);
				if (fence != nullptr) SDL_ReleaseGPUFence(context->device, fence);
				if (completed) {
					mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(
						context->device, download, false));
					if (mapped != nullptr) {
						result = SDL_CreateSurface(outputWidth, outputHeight, SDL_PIXELFORMAT_RGBA32);
						if (result != nullptr) {
							for (int y = 0; y < outputHeight; ++y)
								SDL_memcpy(static_cast<Uint8*>(result->pixels) +
									static_cast<size_t>(y) * result->pitch,
									mapped + static_cast<size_t>(y) * outputWidth * 4,
									static_cast<size_t>(outputWidth) * 4);
						}
						SDL_UnmapGPUTransferBuffer(context->device, download);
					}
				}
			}
		}
	}

	if (upload != nullptr) SDL_ReleaseGPUTransferBuffer(context->device, upload);
	if (download != nullptr) SDL_ReleaseGPUTransferBuffer(context->device, download);
	if (sourceTexture != nullptr) SDL_ReleaseGPUTexture(context->device, sourceTexture);
	if (outputTexture != nullptr) SDL_ReleaseGPUTexture(context->device, outputTexture);
	return result;
}

// Refine and enlarge a depth surface using color as an edge guide, then return the GPU result as a
// surface.
SDL_Surface* Image::refineDepthSurfaceGPU(Context* context, const SDL_Surface* color,
		const SDL_Surface* depth, int outputWidth, int outputHeight) {
	if (context == nullptr || context->device == nullptr || color == nullptr || depth == nullptr ||
		outputWidth <= 0 || outputHeight <= 0 || depthRefinePipeline == nullptr)
		return nullptr;

	SDL_GPUTexture* colorTexture = nullptr;
	SDL_GPUTexture* depthTexture = nullptr;
	if (uploadTexture(context, const_cast<SDL_Surface*>(color), &colorTexture,
			"Depth Guide Texture", false, true, false) < 0 ||
		uploadTexture(context, const_cast<SDL_Surface*>(depth), &depthTexture,
			"Depth Input Texture", false, true, false) < 0) {
		if (colorTexture != nullptr) SDL_ReleaseGPUTexture(context->device, colorTexture);
		if (depthTexture != nullptr) SDL_ReleaseGPUTexture(context->device, depthTexture);
		return nullptr;
	}

	const SDL_GPUTextureCreateInfo outputInfo{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = static_cast<Uint32>(outputWidth),
		.height = static_cast<Uint32>(outputHeight),
		.layer_count_or_depth = 1,
		.num_levels = 1
	};
	SDL_GPUTexture* outputTexture = SDL_CreateGPUTexture(context->device, &outputInfo);
	const SDL_GPUTransferBufferCreateInfo downloadInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = static_cast<Uint32>(outputWidth * outputHeight * 4)
	};
	SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(context->device, &downloadInfo);
	SDL_Surface* result = nullptr;
	if (outputTexture != nullptr && download != nullptr) {
		SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(context->device);
		if (commands != nullptr) {
			const SDL_GPUColorTargetInfo target{
				.texture = outputTexture,
				.load_op = SDL_GPU_LOADOP_DONT_CARE,
				.store_op = SDL_GPU_STOREOP_STORE
			};
			SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
			if (pass != nullptr) {
				bindPipeline(pass, depthRefinePipeline);
				const SDL_GPUBufferBinding vertexBinding{.buffer = sharedVertexBuffer, .offset = 0};
				const SDL_GPUBufferBinding indexBinding{.buffer = sharedIndexBuffer, .offset = 0};
				SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
				SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
				const SDL_GPUTextureSamplerBinding bindings[2] = {
					{.texture = colorTexture, .sampler = imageSampler},
					{.texture = depthTexture, .sampler = imageSampler}
				};
				SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
				const ImageDataVert vertexUniforms{
					.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)),
					.projection = glm::mat4(1.0f),
					.displayImageAspect = glm::vec3(1.0f),
					.fillScreen = 0
				};
				const struct {
					glm::vec2 depthSize;
					float spatialSigma;
					float colorSigma;
				} fragmentUniforms{
					.depthSize = glm::vec2(depth->w, depth->h),
					.spatialSigma = 1.35f,
					.colorSigma = 0.12f
				};
				SDL_PushGPUVertexUniformData(commands, 0, &vertexUniforms, sizeof(vertexUniforms));
				SDL_PushGPUFragmentUniformData(commands, 0, &fragmentUniforms,
					sizeof(fragmentUniforms));
				const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(outputWidth),
					static_cast<float>(outputHeight), 0.0f, 1.0f};
				SDL_SetGPUViewport(pass, &viewport);
				SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
				SDL_EndGPURenderPass(pass);
				SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
				const SDL_GPUTextureTransferInfo transfer{.transfer_buffer = download};
				const SDL_GPUTextureRegion region{.texture = outputTexture,
					.w = static_cast<Uint32>(outputWidth), .h = static_cast<Uint32>(outputHeight), .d = 1};
				SDL_DownloadFromGPUTexture(copy, &region, &transfer);
				SDL_EndGPUCopyPass(copy);
				SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
				const bool completed = fence != nullptr &&
					SDL_WaitForGPUFences(context->device, true, &fence, 1);
				if (fence != nullptr) SDL_ReleaseGPUFence(context->device, fence);
				if (completed) {
					auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(
						context->device, download, false));
					if (mapped != nullptr) {
						result = SDL_CreateSurface(outputWidth, outputHeight, SDL_PIXELFORMAT_RGBA32);
						if (result != nullptr) {
							for (int y = 0; y < outputHeight; ++y)
								SDL_memcpy(static_cast<Uint8*>(result->pixels) +
									static_cast<size_t>(y) * result->pitch,
									mapped + static_cast<size_t>(y) * outputWidth * 4,
									static_cast<size_t>(outputWidth) * 4);
						}
						SDL_UnmapGPUTransferBuffer(context->device, download);
					}
				}
			} else {
				SDL_CancelGPUCommandBuffer(commands);
			}
		}
	}
	if (download != nullptr) SDL_ReleaseGPUTransferBuffer(context->device, download);
	if (outputTexture != nullptr) SDL_ReleaseGPUTexture(context->device, outputTexture);
	SDL_ReleaseGPUTexture(context->device, colorTexture);
	SDL_ReleaseGPUTexture(context->device, depthTexture);
	return result;
}

// Create an enlarged 16-bit depth texture using the color-guided refinement shader.
SDL_GPUTexture* Image::refineDepthTextureGPU(Context* context, SDL_GPUTexture* color,
		SDL_GPUTexture* depth, int width, int height, int scale) {
	if (context == nullptr || context->device == nullptr || color == nullptr || depth == nullptr ||
		width <= 0 || height <= 0 || depthRefineR16Pipeline == nullptr)
		return nullptr;
	const SDL_GPUTextureCreateInfo outputInfo{
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R16_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = static_cast<Uint32>(width * scale),
		.height = static_cast<Uint32>(height * scale),
		.layer_count_or_depth = 1,
		.num_levels = 1
	};
	SDL_GPUTexture* output = SDL_CreateGPUTexture(context->device, &outputInfo);
	if (output == nullptr) return nullptr;
	SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(context->device);
	if (commands == nullptr) {
		SDL_ReleaseGPUTexture(context->device, output);
		return nullptr;
	}
	const SDL_GPUColorTargetInfo target{
		.texture = output,
		.load_op = SDL_GPU_LOADOP_DONT_CARE,
		.store_op = SDL_GPU_STOREOP_STORE
	};
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
	if (pass == nullptr) {
		SDL_CancelGPUCommandBuffer(commands);
		SDL_ReleaseGPUTexture(context->device, output);
		return nullptr;
	}
	bindPipeline(pass, depthRefineR16Pipeline);
	const SDL_GPUBufferBinding vertexBinding{.buffer = sharedVertexBuffer, .offset = 0};
	const SDL_GPUBufferBinding indexBinding{.buffer = sharedIndexBuffer, .offset = 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
	SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
	const SDL_GPUTextureSamplerBinding bindings[2] = {
		{.texture = color, .sampler = imageSampler},
		{.texture = depth, .sampler = imageSampler}
	};
	SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
	const ImageDataVert vertexUniforms{
		.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)),
		.projection = glm::mat4(1.0f),
		.displayImageAspect = glm::vec3(1.0f),
		.fillScreen = 0
	};
	const struct {
		glm::vec2 depthSize;
		float spatialSigma;
		float colorSigma;
	} fragmentUniforms{
		.depthSize = glm::vec2(width, height),
		.spatialSigma = 1.35f,
		.colorSigma = 0.12f
	};
	SDL_PushGPUVertexUniformData(commands, 0, &vertexUniforms, sizeof(vertexUniforms));
	SDL_PushGPUFragmentUniformData(commands, 0, &fragmentUniforms, sizeof(fragmentUniforms));
	const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(width * scale),
		static_cast<float>(height * scale), 0.0f, 1.0f};
	SDL_SetGPUViewport(pass, &viewport);
	SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
	SDL_EndGPURenderPass(pass);
	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	const bool completed = fence != nullptr &&
		SDL_WaitForGPUFences(context->device, true, &fence, 1);
	if (fence != nullptr) SDL_ReleaseGPUFence(context->device, fence);
	if (!completed) {
		SDL_ReleaseGPUTexture(context->device, output);
		return nullptr;
	}
	return output;
}

// Capture a small background texture for blur sampling and optional crossfading.
void Image::blitBlurTexture(Context* context, SDL_GPUTexture* inputTexture,
		Uint32 imageWidth, Uint32 imageHeight, bool nextSnapshotOnly, bool waitForGpu) {
	if (context == nullptr || inputTexture == nullptr) return;
	const SDL_GPUTextureCreateInfo textureInfo = {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = blurSnapshotSize,
		.height = blurSnapshotSize,
		.layer_count_or_depth = 1,
		.num_levels = blurSnapshotMipLevels
	};
	if (blurTexture == nullptr) {
		blurTexture = SDL_CreateGPUTexture(context->device, &textureInfo);
		if (blurTexture != nullptr)
			SDL_SetGPUTextureName(context->device, blurTexture, "Blur Texture Current");
	}
	if (blurTextureNext == nullptr) {
		blurTextureNext = SDL_CreateGPUTexture(context->device, &textureInfo);
		if (blurTextureNext != nullptr)
			SDL_SetGPUTextureName(context->device, blurTextureNext, "Blur Texture Next");
	}
	if (blurTexture == nullptr || blurTextureNext == nullptr) return;

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) return;
	const auto sourceWidth = context->imageType == Color_Plus_Depth
		? imageWidth / 2 : imageWidth;
	auto captureSnapshot = [&](SDL_GPUTexture* destination) {
		const SDL_GPUBlitInfo blitInfo = {
			.source = {
				.texture = inputTexture,
				.layer_or_depth_plane = 0,
				.w = sourceWidth,
				.h = imageHeight },
			.destination = {
				.texture = destination,
				.layer_or_depth_plane = 0,
				.w = blurSnapshotSize,
				.h = blurSnapshotSize },
			.load_op = SDL_GPU_LOADOP_DONT_CARE,
			.filter = SDL_GPU_FILTER_LINEAR
		};
		SDL_BlitGPUTexture(commandBuffer, &blitInfo);
		SDL_GenerateMipmapsForGPUTexture(commandBuffer, destination);
	};

	if (!nextSnapshotOnly) captureSnapshot(blurTexture);
	captureSnapshot(blurTextureNext);
	if (nextSnapshotOnly || !waitForGpu) {
		SDL_SubmitGPUCommandBuffer(commandBuffer);
	} else {
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
		SDL_WaitForGPUFences(context->device, true, &fence, 1);
		SDL_ReleaseGPUFence(context->device, fence);
		videoBlurActive = false;
		imageDataFrag.blurMix = 0.0f;
	}
}

// Divide a render rectangle into horizontal viewports for packed stereo output.
static SDL_GPUViewport getViewport(int view, int views, glm::vec2 rect) {
	SDL_GPUViewport result{ 0, 0, rect.x, rect.y, 0, 0 };
	auto viewWidth = rect.x / (float)views;
	result.x = (float)floor(viewWidth * double(view));
	result.w = ceil(viewWidth);
	return result;
}

// Locate one tile's viewport in a regular quilt grid.
static SDL_GPUViewport getViewportGrid(glm::vec2 view, glm::vec2 rect) {
	SDL_GPUViewport result{ rect.x * view.x, rect.y * view.y, rect.x, rect.y, 0, 0 };
	return result;
}

// Render the source into the requested stereo or quilt export layout.
int Image::renderStereoImage(Context* context, StereoFormat stereoFormat,
		SDL_GPUTexture* sourceTexture) {
	if (context == nullptr || context->device == nullptr || context->window == nullptr) {
		SDL_Log("Export render requested without a valid GPU context.");
		return -1;
	}
	if (displayHelp) return -1;
	if (context->imageType == Color_Only || context->imageType == Color_Anaglyph ||
		(context->imageType != Color_Plus_Depth &&
		(stereoFormat == Color_Only || stereoFormat == Color_Plus_Depth ||
			stereoFormat == Light_Field_LKG))) return -1;

	auto renderFormat = Left;
	auto singleImageSize = context->imageSize;
	auto viewportSize = singleImageSize;
	auto viewsX = 1, viewsY = 1;
	auto quiltViewCount = viewsX * viewsY;
	auto stereoStrength = context->stereoStrength;
	auto stereoDepth = context->stereoDepth;
	auto stereoOffset = context->stereoOffset;
	auto gridBoost = 8.0;
	auto strengthStep = 0.0;
	auto offsetStep = 0.0;
	auto startX = 0;
	auto startY = 0;
	auto stepX = 1;
	auto stepY = 1;

	if (stereoFormat == Color_Only) {
		renderFormat = Mono;
	} else if (stereoFormat == Color_Anaglyph) {
		renderFormat = Anaglyph_Accurate;
	} else if (stereoFormat == Side_By_Side_Full) {
		renderFormat = Left;
		viewportSize = singleImageSize * glm::vec2(2.0, 1.0);
		viewsX = 2;
	} else if (stereoFormat == Side_By_Side_Half) {
		renderFormat = Left;
		viewportSize = singleImageSize;
		singleImageSize = singleImageSize * glm::vec2(0.5, 1.0);
		viewsX = 2;
	} else if (stereoFormat == Color_Plus_Depth) {
		renderFormat = Native;
		singleImageSize = context->imageSize * glm::vec2(2.0, 1.0);
		viewportSize = singleImageSize;
	} else if (stereoFormat == Stereo_Free_View_Grid) {
		renderFormat = Left;
		viewportSize = singleImageSize;
		singleImageSize = singleImageSize * glm::vec2(0.5, 0.5);
		viewsX = 2;
		viewsY = 2;
	} else if (stereoFormat == Stereo_Free_View_LRL) {
		renderFormat = Left;
		viewportSize = singleImageSize * glm::vec2(1.5, 0.5);
		singleImageSize = singleImageSize * glm::vec2(0.5, 0.5);
		viewsX = 3;
		viewsY = 1;
	} else if (stereoFormat == Light_Field_LKG) {
		const auto quiltGrid = exportQuiltDimLKG;
		quiltViewCount = (int)(quiltGrid.x * quiltGrid.y);
		viewsX = (int)quiltGrid.x;
		viewsY = (int)quiltGrid.y;
		startY = viewsY - 1;
		stepY = -1;
		renderFormat = Left;
		auto maxRes = exportQuiltMaxResLKG;
		auto maxSize = std::max(singleImageSize.x, singleImageSize.y);
		singleImageSize *= maxRes / maxSize;
		singleImageSize.x = roundf(singleImageSize.x);
		singleImageSize.y = roundf(singleImageSize.y);
		viewportSize = singleImageSize * quiltGrid;
		stereoStrength *= gridBoost;
		stereoOffset *= gridBoost;
		strengthStep = -stereoStrength * 2.0f / (float(viewsX * viewsY - 1));
		offsetStep = -stereoOffset * 2.0f / (float(viewsX * viewsY - 1));
	}

	const float imageHalfWidth = singleImageSize.x * 0.5f;
	const float imageHalfHeight = singleImageSize.y * 0.5f;

	if (!std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y) ||
		viewportSize.x < 1.0f || viewportSize.y < 1.0f ||
		viewportSize.x > static_cast<float>(std::numeric_limits<Uint32>::max()) ||
		viewportSize.y > static_cast<float>(std::numeric_limits<Uint32>::max())) {
		SDL_Log("Invalid export texture size: %.1fx%.1f", viewportSize.x, viewportSize.y);
		return -1;
	}

	SDL_GPUTextureCreateInfo textureCreateInfo {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = (Uint32)viewportSize.x,
		.height = (Uint32)viewportSize.y,
		.layer_count_or_depth = 1,
		.num_levels = 1
	};

	SDL_GPUTexture* renderedTexture = SDL_CreateGPUTexture(context->device, &textureCreateInfo);
	if (renderedTexture == nullptr) {
		SDL_Log("Create export texture failed (%ux%u): %s", textureCreateInfo.width,
			textureCreateInfo.height, SDL_GetError());
		return -1;
	}

	SDL_SetGPUTextureName(
		context->device,
		renderedTexture,
		"Export Texture"
	);

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) {
		SDL_Log("Acquire export command buffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTexture(context->device, renderedTexture);
		return -1;
	}

	if (context->backgroundStyle == Solid) clearColorCurrent = clearColorSolid;
	else if (context->backgroundStyle == Light) clearColorCurrent = clearColorLight;
	else if (context->backgroundStyle == Dark) clearColorCurrent = clearColorDark;

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = renderedTexture;
	colorTargetInfo.clear_color = SDL_FColor{ clearColorCurrent.r, clearColorCurrent.g, clearColorCurrent.b, clearColorCurrent.a };
	colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

	SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, nullptr);
	if (renderPass == nullptr) {
		SDL_Log("Begin export render pass failed: %s", SDL_GetError());
		SDL_CancelGPUCommandBuffer(commandBuffer);
		SDL_ReleaseGPUTexture(context->device, renderedTexture);
		return -1;
	}

	for (auto renderY = startY; renderY >= 0 && renderY < viewsY; renderY += stepY) {
		for (auto renderX = startX; renderX < viewsX; renderX += stepX) {
			const int quiltView = (viewsY - 1 - renderY) * viewsX + renderX;
			if (stereoFormat == Light_Field_LKG && quiltView >= quiltViewCount) continue;
			auto drawViewport = getViewportGrid(glm::vec2(renderX, renderY), singleImageSize);
			SDL_SetGPUViewport(renderPass, &drawViewport);

			SDL_GPUBufferBinding vertexBinding = { .buffer = sharedVertexBuffer, .offset = 0 };
			SDL_GPUBufferBinding indexBinding = { .buffer = sharedIndexBuffer, .offset = 0 };
			SDL_BindGPUGraphicsPipeline(renderPass, imagePipeline);
			SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);
			SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
			SDL_GPUTextureSamplerBinding sampleBindings[4] = {
				{ .texture = sourceTexture != nullptr ? sourceTexture : imageTexture,
					.sampler = imageSampler },
				{ .texture = blurTexture, .sampler = imageSampler },
				{ .texture = blurTextureNext != nullptr ? blurTextureNext : blurTexture,
					.sampler = imageSampler },
				{ .texture = videoDepthTexture != nullptr ? videoDepthTexture :
					(sourceTexture != nullptr ? sourceTexture : imageTexture), .sampler = imageSampler }};
			SDL_BindGPUFragmentSamplers(renderPass, 0, &sampleBindings[0], 4);

			if (stereoFormat == Side_By_Side_Full || stereoFormat == Side_By_Side_Half ||
				stereoFormat == Stereo_Free_View_Grid || stereoFormat == Stereo_Free_View_LRL) {
				renderFormat = (ViewMode)(Left + (renderX + (renderY % 2)) % 2);
			}
			imageDataFrag.mode = renderFormat;
			imageDataFrag.stereoStrength = (float)stereoStrength;
			imageDataFrag.stereoDepth = (float)stereoDepth;
			imageDataFrag.stereoOffset = (float)stereoOffset;
			imageDataFrag.windowSize = context->imageSize;
			imageDataFrag.imageSize = context->imageSize;
			imageDataFrag.type = context->imageType;
			imageDataFrag.separateDepth = videoDepthTexture != nullptr ? 1 : 0;
			imageDataFrag.packedOutput = stereoFormat == Color_Plus_Depth ? 1 : 0;
			imageDataFrag.gridSize = context->gridSize;
			auto singleImageAspect = singleImageSize.x / singleImageSize.y;
			imageDataVert.displayImageAspect = glm::vec3(singleImageAspect, singleImageAspect, 1.0);
			imageDataVert.fillScreen = 0;
			imageDataVert.projection = glm::ortho((float)-imageHalfWidth, (float)imageHalfWidth,
				-(float)imageHalfHeight, (float)imageHalfHeight);
			imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(singleImageSize.x, singleImageSize.y, 1.0));
			imageDataFrag.visibility = 1.0;
			imageDataFrag.blur = 0;

			drawImage(commandBuffer, renderPass);

			if (stereoFormat == Light_Field_LKG) {
				stereoStrength += strengthStep;
				stereoOffset += offsetStep;
			}
		}
	}

	SDL_EndGPURenderPass(renderPass);
	imageDataFrag.packedOutput = 0;

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	if (fence == nullptr) {
		SDL_Log("Submit export render failed: %s", SDL_GetError());
		SDL_ReleaseGPUTexture(context->device, renderedTexture);
		return -1;
	}
	if (!SDL_WaitForGPUFences(context->device, true, &fence, 1)) {
		SDL_Log("Wait for export render failed: %s", SDL_GetError());
		SDL_ReleaseGPUFence(context->device, fence);
		SDL_ReleaseGPUTexture(context->device, renderedTexture);
		return -1;
	}
	SDL_ReleaseGPUFence(context->device, fence);
	if (exportTexture != nullptr) SDL_ReleaseGPUTexture(context->device, exportTexture);
	exportTexture = renderedTexture;
	exportTextureSize = {textureCreateInfo.width, textureCreateInfo.height};

	return 0;
}

// Read the rendered export texture back into a CPU surface after GPU completion.
SDL_Surface* Image::getExportTexture(Context* context) {
	if (context == nullptr || context->device == nullptr || exportTexture == nullptr ||
		exportTextureSize.x == 0 || exportTextureSize.y == 0) {
		SDL_Log("Export readback requested without a valid rendered texture.");
		return nullptr;
	}
	const glm::uvec2 readbackSize = exportTextureSize;

	const Uint64 pixelBytes = static_cast<Uint64>(readbackSize.x) *
		readbackSize.y * 4u;
	if (pixelBytes > std::numeric_limits<Uint32>::max()) {
		SDL_Log("Export readback is too large: %ux%u", readbackSize.x,
			readbackSize.y);
		return nullptr;
	}

	SDL_GPUTransferBufferCreateInfo transferBufferInfo {
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = static_cast<Uint32>(pixelBytes)
	};

	SDL_GPUTransferBuffer* downloadTransferBuffer = SDL_CreateGPUTransferBuffer(
		context->device, &transferBufferInfo);
	if (downloadTransferBuffer == nullptr) {
		SDL_Log("Create export transfer buffer failed: %s", SDL_GetError());
		return nullptr;
	}

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) {
		SDL_Log("Acquire export copy command buffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);
		return nullptr;
	}
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
	if (copyPass == nullptr) {
		SDL_Log("Begin export copy pass failed: %s", SDL_GetError());
		SDL_CancelGPUCommandBuffer(commandBuffer);
		SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);
		return nullptr;
	}

	SDL_GPUTextureRegion textureRegion = {
		.texture = exportTexture,
		.mip_level = 0,
		.layer = 0,
		.x = 0,
		.y = 0,
		.z = 0,
		.w = readbackSize.x,
		.h = readbackSize.y,
		.d = 1
	};

	SDL_GPUTextureTransferInfo textureTransfer = {
		.transfer_buffer = downloadTransferBuffer,
		.offset = 0,
		.pixels_per_row = readbackSize.x,
		.rows_per_layer = readbackSize.y
	};

	SDL_DownloadFromGPUTexture(
		copyPass, &textureRegion,
		&textureTransfer
	);

	SDL_EndGPUCopyPass(copyPass);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	if (fence == nullptr) {
		SDL_Log("Submit export readback failed: %s", SDL_GetError());
		SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);
		return nullptr;
	}
	if (!SDL_WaitForGPUFences(context->device, true, &fence, 1)) {
		SDL_Log("Wait for export readback failed: %s", SDL_GetError());
		SDL_ReleaseGPUFence(context->device, fence);
		SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);
		return nullptr;
	}
	SDL_ReleaseGPUFence(context->device, fence);
	SDL_ReleaseGPUTexture(context->device, exportTexture);
	exportTexture = nullptr;
	exportTextureSize = {0, 0};

	auto downloadedData = (Uint8*)SDL_MapGPUTransferBuffer(
		context->device,
		downloadTransferBuffer,
		false
	);
	if (downloadedData == nullptr) {
		SDL_Log("Map export transfer buffer failed: %s", SDL_GetError());
		SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);
		return nullptr;
	}

	SDL_Surface* result = SDL_CreateSurface(static_cast<int>(readbackSize.x),
		static_cast<int>(readbackSize.y), SDL_PIXELFORMAT_ARGB8888);
	if (result != nullptr) {
		const size_t sourcePitch = static_cast<size_t>(readbackSize.x) * 4u;
		for (Uint32 row = 0; row < readbackSize.y; ++row) {
			SDL_memcpy(static_cast<Uint8*>(result->pixels) +
				static_cast<size_t>(row) * result->pitch,
				static_cast<const Uint8*>(downloadedData) +
				static_cast<size_t>(row) * sourcePitch, sourcePitch);
		}
	} else {
		SDL_Log("Create owned export surface failed: %s", SDL_GetError());
	}

	SDL_UnmapGPUTransferBuffer(context->device, downloadTransferBuffer);
	SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);

	return result;
}

// Bind a graphics pipeline and the shared quad geometry used by image and UI draws.
void Image::bindPipeline(SDL_GPURenderPass* renderPass, SDL_GPUGraphicsPipeline* pipeline) {
	SDL_GPUBufferBinding bindingVertex = { .buffer = sharedVertexBuffer, .offset = 0 };
	SDL_GPUBufferBinding bindingIndex = { .buffer = sharedIndexBuffer, .offset = 0 };
	SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
	SDL_BindGPUVertexBuffers(renderPass, 0, &bindingVertex, 1);
	SDL_BindGPUIndexBuffer(renderPass, &bindingIndex, SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

// Draw the image quad using the current image shader uniforms.
void Image::drawImage(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &imageDataVert, sizeof(ImageDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &imageDataFrag, sizeof(ImageDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

// Draw an icon quad using the current icon shader uniforms.
void Image::drawIcon(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &iconDataVert, sizeof(IconDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &iconDataFrag, sizeof(IconDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

// Draw a sprite quad using the current sprite shader uniforms.
void Image::drawSprite(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &spriteDataVert, sizeof(SpriteDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &spriteDataFrag, sizeof(SpriteDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

// Build the translation and scale matrix for a canvas element.
glm::mat4 Image::getTransform(glm::vec3 position, glm::vec3 size, glm::vec3 aspect) {
	auto value = glm::mat4(1.0f);
	value = translate(value, position);
	value = scale(value, size * aspect);
	return value;
}

// Prepare the transform, texture region, and appearance for the next sprite draw.
void Image::setSpriteUniforms(glm::vec3 position, glm::vec3 size, glm::vec4 color,
	float visibility, int useTexture, glm::vec2 uvOffset, glm::vec2 uvSize,
		glm::vec2 slice, glm::vec3 aspect) {
	spriteDataVert.transform = getTransform(position, size, aspect);
	spriteDataVert.uvOffset = uvOffset;
	spriteDataVert.uvSize = uvSize;
	spriteDataFrag.color = color;
	spriteDataFrag.visibility = visibility;
	spriteDataFrag.useTexture = useTexture;
	spriteDataFrag.slice = slice;
}

namespace {
SDL_GPUTexture* gettingStartedTexture = nullptr;
glm::ivec2 gettingStartedSize{};
std::uint64_t gettingStartedRevision = 0;

// Draw the regular cursor in each guide view, matching the page's packing and pointer coordinates.
void drawGettingStartedCursor(Context* context, SDL_GPUCommandBuffer* command,
	SDL_GPURenderPass* pass, int width, int height, GettingStarted::Layout layout) {
	if (!context->fullscreen || SDL_GetMouseFocus() != context->window) return;
	float mouseX = 0, mouseY = 0;
	int logicalWidth = 0, logicalHeight = 0;
	SDL_GetMouseState(&mouseX, &mouseY);
	SDL_GetWindowSize(context->window, &logicalWidth, &logicalHeight);
	mouseX *= static_cast<float>(width) / std::max(1, logicalWidth);
	mouseY *= static_cast<float>(height) / std::max(1, logicalHeight);
	const int views = layout == GettingStarted::Layout::Single ? 1 : 2;
	const int leftWidth = views == 2 ? width / 2 : width;
	const bool right = views == 2 && mouseX >= leftWidth;
	const float localX = (mouseX - (right ? leftWidth : 0)) /
		std::max(1, right ? width - leftWidth : leftWidth);
	// Keep the app's pointer position current when the guide consumes mouse events.
	context->mouse = {mouseX, mouseY};
	context->mouseVisibility = 1.0;
	Image::bindPipeline(pass, Image::iconPipeline);
	SDL_GPUTextureSamplerBinding binding{Image::iconTexture, Image::imageSampler};
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	for (int view = 0; view < views; ++view) {
		const int viewWidth = view == 0 ? leftWidth : width - leftWidth;
		const SDL_GPUViewport viewport{static_cast<float>(view * leftWidth), 0,
			static_cast<float>(viewWidth), static_cast<float>(height), 0, 1};
		SDL_SetGPUViewport(pass, &viewport);
		const SDL_Rect clip{view * leftWidth, 0, viewWidth, height};
		SDL_SetGPUScissor(pass, &clip);
		const float horizontalScale = layout == GettingStarted::Layout::SBSHalf ||
			layout == GettingStarted::Layout::RGBD ? static_cast<float>(viewWidth) / width : 1.0f;
		const glm::vec2 scale = glm::vec2(horizontalScale, 1.0f) * context->displayScale;
		const glm::vec2 position{localX * viewWidth, height - mouseY};
		Image::iconDataVert.projection = glm::ortho(0.0f, static_cast<float>(viewWidth),
			0.0f, static_cast<float>(height));
		Image::iconDataVert.transform = Image::getTransform(
			glm::vec3(position + glm::vec2(9.0f, -16.0f) * scale, 0.0f),
			glm::vec3(scale * Image::style.getIconRadius(Style::getCurrentScale()), 1.0f), glm::vec3(1.0f));
		Image::iconDataVert.gridOffset = Image::getIconCoordinates(IconType::Cursor_Black);
		Image::iconDataFrag.color = glm::vec4(1.0f);
		Image::iconDataFrag.visibility = 1.0f;
		Image::iconDataFrag.rotation = 0.0f;
		Image::iconDataFrag.animated = 0;
		Image::iconDataFrag.force = layout == GettingStarted::Layout::RGBD && view == 1 ? 1 : 0;
		Image::drawIcon(command, pass);
	}
}

// Present the guide in the same fullscreen packing layout as the media and controls.
int drawGettingStarted(Context* context) {
	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(context->window, &width, &height);
	if (width <= 0 || height <= 0) return 0;
	const auto assets = std::filesystem::path(SDL_GetBasePath()).parent_path().parent_path() / "Assets";
	auto layout = GettingStarted::Layout::Single;
	if (context->fullscreen) {
		if (context->mode == SBS_Full) layout = GettingStarted::Layout::SBSFull;
		else if (context->mode == SBS_Half) layout = GettingStarted::Layout::SBSHalf;
		else if (context->mode == RGB_Depth) layout = GettingStarted::Layout::RGBD;
	}
	auto* page = GettingStarted::render(width, height, context->displayScale, assets,
		context->backgroundStyle == Light, layout);
	if (page == nullptr) {
		SDL_Log("Could not render getting started guide: %s", SDL_GetError());
		GettingStarted::visible = false;
		return 0;
	}
	if (!gettingStartedTexture || gettingStartedRevision != GettingStarted::revision()) {
		if (Image::uploadTexture(context, page, &gettingStartedTexture, "Getting Started",
			gettingStartedTexture && gettingStartedSize == glm::ivec2(width, height), true, false) < 0)
			return -1;
		gettingStartedSize = {width, height};
		gettingStartedRevision = GettingStarted::revision();
	}
	auto* command = SDL_AcquireGPUCommandBuffer(context->device);
	if (!command) return -1;
	SDL_GPUTexture* swapchain = nullptr;
	if (!SDL_AcquireGPUSwapchainTexture(command, context->window, &swapchain, nullptr, nullptr)) {
		SDL_CancelGPUCommandBuffer(command);
		return -1;
	}
	if (!swapchain) return SDL_SubmitGPUCommandBuffer(command) ? 0 : -1;
	SDL_GPUColorTargetInfo target{};
	target.texture = swapchain;
	target.load_op = SDL_GPU_LOADOP_CLEAR;
	target.store_op = SDL_GPU_STOREOP_STORE;
	auto* pass = SDL_BeginGPURenderPass(command, &target, 1, nullptr);
	if (!pass) { SDL_CancelGPUCommandBuffer(command); return -1; }
	Image::bindPipeline(pass, Image::spritePipeline);
	SDL_GPUTextureSamplerBinding binding{gettingStartedTexture, Image::imageSampler};
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	Image::spriteDataVert.projection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
	Image::setSpriteUniforms({width * 0.5f, height * 0.5f, 0}, {width, height, 1},
		{1, 1, 1, 1}, 1, 1, {0, 0}, {1, 1}, {0.5f, 0.5f}, {1, 1, 1});
	Image::drawSprite(command, pass);
	drawGettingStartedCursor(context, command, pass, width, height, layout);
	SDL_EndGPURenderPass(pass);
	return SDL_SubmitGPUCommandBuffer(command) ? 0 : -1;
}
}

// Render media, backgrounds, subtitles, and UI overlays for the active presentation mode.
int Image::draw(Context* context) {
	if (GettingStarted::visible) {
		if (context->fullscreen && SDL_GetMouseFocus() == context->window) SDL_HideCursor();
		else SDL_ShowCursor();
		return drawGettingStarted(context);
	}
	imageDataFrag.packedOutput = 0;
	// Use faster attack than decay so waveform bars follow transients without flickering between
	// frames.
	const auto waveformNow = BlurClock::now();
	const float waveformDelta = std::clamp(
		std::chrono::duration<float>(waveformNow - waveformDrawTime).count(), 0.0f, 0.1f);
	waveformDrawTime = waveformNow;
	for (size_t i = 0; i < smoothedAudioWaveform.size(); ++i) {
		if (!displayAudioWaveform) smoothedAudioWaveform[i] = 0.0f;
		else {
			const float speed = audioWaveform[i] > smoothedAudioWaveform[i] ? 30.0f : 12.0f;
			smoothedAudioWaveform[i] += (audioWaveform[i] - smoothedAudioWaveform[i]) *
				(1.0f - std::exp(-speed * waveformDelta));
		}
	}
	updateVideoBackgroundAnimation();
	if (nativeOutputEnabled && discMenuTexture == nullptr) {
		nativeOutputSourceReady = imageTexture != nullptr;
		if (nativeOutputSourceReady) drawNativeOutput(context);
	}
	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) {
		SDL_Log("Acquire GPU Command Buffer Failed.");
		return -1;
	}

	Uint32 swapchainWidth = 0, swapchainHeight = 0;
	SDL_GPUTexture* swapchainTexture;
	if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, context->window, &swapchainTexture,
			&swapchainWidth, &swapchainHeight)) {
		SDL_Log("Acquire GPU Swap Chain Failed.");
		return -1;
	}

	if (swapchainTexture != nullptr) {
		if (context->backgroundStyle == Solid) clearColorCurrent = clearColorSolid;
		else if (context->backgroundStyle == Light) clearColorCurrent = clearColorLight;
		else if (context->backgroundStyle == Dark) clearColorCurrent = clearColorDark;
		if (context->backgroundStyle == Solid && context->mode == RGB_Depth) clearColorCurrent = clearColorDepth;
		if (discMenuTexture != nullptr && context->backgroundStyle == Blur) clearColorCurrent = clearColorDark;

		SDL_GPUColorTargetInfo colorTargetInfo{};
		colorTargetInfo.texture = swapchainTexture;
		colorTargetInfo.clear_color = SDL_FColor{ clearColorCurrent.r, clearColorCurrent.g, clearColorCurrent.b, clearColorCurrent.a };
		colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, nullptr);

		if (renderPass == nullptr) {
			SDL_Log("GPU Render Pass Failed.");
			return -1;
		}

		int windowWidth = 0, windowHeight = 0;
		if (swapchainWidth > 0 && swapchainHeight > 0) {
			windowWidth = (int)swapchainWidth;
			windowHeight = (int)swapchainHeight;
		} else {
			SDL_GetWindowSizeInPixels(context->window, &windowWidth, &windowHeight);
		}
		glm::vec2 windowSize = { windowWidth, windowHeight };
		const float audioArtworkScale = 0.5f * std::max(0.1f, (windowSize.y - 240.0f) / windowSize.y);
		auto aspectScale = glm::vec2(1.0, 1.0);

		if (context->mode == SBS_Full && context->fullscreen) {
			aspectScale.x = 2.0;
		}

		auto uiProjection = glm::ortho(0.0f, context->windowSize.x,  0.0f, context->windowSize.y);
		iconDataVert.projection = uiProjection;
		spriteDataVert.projection = uiProjection;
		spriteDataVert.uvOffset = { 0.0, 0.0 };
		spriteDataVert.uvSize = { 1.0, 1.0 };

		auto viewsX = 1;
		auto viewsY = 1;
		if ((context->mode == SBS_Full || context->mode == SBS_Half
			|| context->mode == RGB_Depth) && context->fullscreen) {
			viewsX = 2;
		} else if (context->mode == Free_View_Grid) {
			viewsX = 2;
			viewsY = 2;
		}

		// Disc selection is mono content, but still follows the output layout.
		if (discMenuTexture != nullptr) {
			viewsX = context->fullscreen && (context->mode == SBS_Full ||
				context->mode == SBS_Half || context->mode == RGB_Depth) ? 2 : 1;
			viewsY = 1;
		}

		// Choose calibrated interlacing only when the main window is presenting a usable native-display
		// source.
		const bool isMainInterlaced = (context->mode == Lenticular && context->display3D &&
			context->fullscreen && (nativeDisplayOnMainWindow || context->nativeOutputWindow == nullptr) && interlacerPipeline != nullptr &&
			imageTexture != nullptr);

		if (discMenuTexture != nullptr) {
			if (context->backgroundStyle == Blur && discBackgroundTexture && blurTexture && blurTextureNext) {
				bindPipeline(renderPass, imagePipeline);
				SDL_GPUTextureSamplerBinding bindings[4] = {
					{.texture = discBackgroundTexture, .sampler = imageSampler},
					{.texture = blurTexture, .sampler = imageSampler},
					{.texture = blurTextureNext, .sampler = imageSampler},
					{.texture = discBackgroundTexture, .sampler = imageSampler}};
				SDL_BindGPUFragmentSamplers(renderPass, 0, bindings, 4);
				auto vertices = imageDataVert;
				vertices.projection = glm::mat4(1.0f);
				vertices.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
				vertices.displayImageAspect = glm::vec3(1.0f);
				vertices.fillScreen = 1;
				auto fragments = imageDataFrag;
				fragments.windowSize = windowSize;
				fragments.mode = Native;
				fragments.type = Color_Only;
				fragments.visibility = 1.0f;
				fragments.blur = 1;
				fragments.force = 0;
				SDL_PushGPUVertexUniformData(commandBuffer, 0, &vertices, sizeof(vertices));
				SDL_PushGPUFragmentUniformData(commandBuffer, 0, &fragments, sizeof(fragments));
				for (int view = 0; view < viewsX; ++view) {
					auto viewport = getViewport(view, viewsX, windowSize);
					SDL_SetGPUViewport(renderPass, &viewport);
					SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
				}
			}
		} else if (isMainInterlaced) {
			auto drawViewport = getViewport(0, 1, windowSize);
			SDL_SetGPUViewport(renderPass, &drawViewport);
			if (context->backgroundStyle == Blur && blurTexture != nullptr &&
				blurTextureNext != nullptr) {
				bindPipeline(renderPass, imagePipeline);
				SDL_GPUTextureSamplerBinding sampleBindings[4] = {
					{ .texture = imageTexture, .sampler = imageSampler },
					{ .texture = blurTexture, .sampler = imageSampler },
					{ .texture = blurTextureNext, .sampler = imageSampler },
					{ .texture = videoDepthTexture != nullptr ? videoDepthTexture : imageTexture,
						.sampler = imageSampler }};
				SDL_BindGPUFragmentSamplers(renderPass, 0, &sampleBindings[0], 4);

				imageDataVert.fillScreen = 1;
				imageDataVert.projection = glm::mat4(1.0f);
				imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0, 2.0, 1.0));

				imageDataFrag.windowSize = windowSize;
				imageDataFrag.imageSize = context->imageSize;
				imageDataFrag.gridSize = context->gridSize;
				imageDataFrag.mode = context->mode;
				imageDataFrag.type = context->imageType;
				imageDataFrag.separateDepth = videoDepthTexture != nullptr ? 1 : 0;
				imageDataFrag.swapLeftRight = context->swapLeftRight;
				imageDataVert.displayImageAspect = glm::vec3(context->displayAspect, 1.0);
				imageDataFrag.visibility = 1.0;
				imageDataFrag.stereoStrength = (float)context->stereoStrength;
				imageDataFrag.stereoDepth = (float)context->stereoDepth;
				imageDataFrag.stereoOffset = (float)context->stereoOffset;
				imageDataFrag.gridAngle = (float)context->gridAngle;
				imageDataFrag.depthEffect = (float)context->depthEffect;
				imageDataFrag.effectRandom = context->effectRandom;
				imageDataFrag.blur = 1;

				drawImage(commandBuffer, renderPass);
			}

			updateInterlacerUniforms(context, (int)windowSize.x, (int)windowSize.y);
			bindPipeline(renderPass, interlacerPipeline);
			imageDataVert.displayImageAspect = {1.0f, 1.0f, 1.0f};
			imageDataVert.displayImageAspect.x = windowSize.x / windowSize.y;
			imageDataVert.displayImageAspect.y = context->imageSize.x / context->imageSize.y;
			imageDataVert.fillScreen = 0;
			imageDataVert.projection = glm::mat4(1.0f);
			imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
			SDL_PushGPUVertexUniformData(commandBuffer, 0, &imageDataVert, sizeof(imageDataVert));
			SDL_GPUTextureSamplerBinding bindings[2] = {
				{.texture = imageTexture, .sampler = imageSampler},
				{.texture = videoDepthTexture != nullptr ? videoDepthTexture : imageTexture,
					.sampler = imageSampler}
			};
			SDL_BindGPUFragmentSamplers(renderPass, 0, bindings, 2);
			SDL_PushGPUFragmentUniformData(commandBuffer, 0, &interlacerDataFrag,
				sizeof(interlacerDataFrag));
			SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
			if (nativeDisplayOnMainWindow)
				drawNativeCalibrationWarning(context, commandBuffer, renderPass,
					(int)windowSize.x, (int)windowSize.y);
		} else {
			// Render each ordinary stereo or grid view into its own viewport with the matching source
			// and eye settings.
			for (auto viewY = 0; viewY < viewsY; viewY++) {
				for (auto viewX = 0; viewX < viewsX; viewX++) {
					if (displayHelp || displayAudioPlaceholder) continue;
					auto viewSize = windowSize;
					auto drawViewport = getViewport(viewX, viewsX * viewsY, viewSize);
					if (viewsY > 1) {
						viewSize = windowSize / glm::vec2(viewsX, viewsY);
						drawViewport = getViewportGrid(glm::vec2(viewX, viewY), viewSize);
					}
					SDL_SetGPUViewport(renderPass, &drawViewport);

					if (imageTexture != nullptr && blurTexture != nullptr &&
						blurTextureNext != nullptr) {
						bindPipeline(renderPass, imagePipeline);
						SDL_GPUTextureSamplerBinding sampleBindings[4] = {
							{ .texture = imageTexture, .sampler = imageSampler },
							{ .texture = blurTexture, .sampler = imageSampler },
							{ .texture = blurTextureNext, .sampler = imageSampler },
							{ .texture = videoDepthTexture != nullptr ? videoDepthTexture : imageTexture,
								.sampler = imageSampler }};
						SDL_BindGPUFragmentSamplers(renderPass, 0, &sampleBindings[0], 4);

						imageDataVert.fillScreen = 1;
						imageDataVert.projection = glm::mat4(1.0f);
						imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0, 2.0, 1.0));

						imageDataFrag.windowSize = windowSize;
						imageDataFrag.imageSize = context->imageSize;
						imageDataFrag.gridSize = context->gridSize;
						// The native output uses both eyes, while this window previews one
						// eye at the aspect ratio stored in context->imageSize.
						imageDataFrag.mode = context->mode == Lenticular &&
							isNativeStereoSource(context->imageType) ? Mono : context->mode;
						imageDataFrag.type = context->imageType;
						imageDataFrag.separateDepth = videoDepthTexture != nullptr ? 1 : 0;
						imageDataFrag.swapLeftRight = context->swapLeftRight;

						imageDataVert.displayImageAspect = glm::vec3(context->displayAspect, 1.0);
						if (context->mode == SBS_Full && viewsX > 1)
							imageDataVert.displayImageAspect.x *= 2.0f;

						if (context->mode == SBS_Full|| context->mode == SBS_Half || context->mode == RGB_Depth
							|| context->mode == Free_View_Grid || context->mode == Free_View_LRL) {
							if (context->imageType == Color_Only) {
								if (context->mode == RGB_Depth) {
									if (viewX == 0) imageDataFrag.mode = Mono;
									if (viewX == 1) imageDataFrag.mode = RGB_Depth;
								} else {
									imageDataFrag.mode = Native;
								}
							} else {
								if (context->display3D) {
									imageDataFrag.mode = Left + (viewX + (viewY % 2)) % 2;
									if (context->mode == RGB_Depth) {
										if (viewX == 0) imageDataFrag.mode = Mono;
										if (viewX == 1) imageDataFrag.mode = RGB_Depth;
									}
								} else {
									if (context->mode == RGB_Depth) {
										if (viewX == 0) imageDataFrag.mode = Mono;
										if (viewX == 1) imageDataFrag.mode = RGB_Depth;
									} else {
										imageDataFrag.mode = Mono;
									}
								}
							}
						} else if (context->imageType == Color_Only) imageDataFrag.mode = Native;

						// Native is a temporary fallback during media/mode transitions.
						// Never expose the packed depth half on the display. Export has
						// its own render path, and explicit RGB_Depth preview is unchanged.
						if (imageDataFrag.mode == Native && context->imageType == Color_Plus_Depth)
							imageDataFrag.mode = Mono;

						imageDataFrag.visibility = 1.0;
						imageDataFrag.stereoStrength = (float)context->stereoStrength;
						imageDataFrag.stereoDepth = (float)context->stereoDepth;
						imageDataFrag.stereoOffset = (float)context->stereoOffset;
						imageDataFrag.gridAngle = (float)context->gridAngle;
						imageDataFrag.depthEffect = (float)context->depthEffect;
						imageDataFrag.effectRandom = context->effectRandom;
						imageDataFrag.blur = 1;

						if (context->backgroundStyle == Blur) drawImage(commandBuffer, renderPass);

						imageDataVert.displayImageAspect = glm::vec3(context->displayAspect, 1.0);
						imageDataVert.fillScreen = 0;
						imageDataVert.projection = glm::ortho(-windowSize.x * 0.5f, windowSize.x * 0.5f,
							-windowSize.y * 0.5f, windowSize.y * 0.5f);
						imageDataVert.transform = glm::mat4(1.0f);
						auto viewOffset = context->offset;
						if (context->mode == Free_View_Grid) {
							viewOffset.x = viewX == 0 ? context->imageBounds.x : -context->imageBounds.x;
							viewOffset.y = viewY == 0 ? context->imageBounds.y : -context->imageBounds.y;
						}
						imageDataVert.transform = glm::translate(imageDataVert.transform, glm::vec3(viewOffset * glm::vec2(1.0, -1.0), 0.0f));
						imageDataVert.transform = glm::scale(imageDataVert.transform, glm::vec3(context->currentZoom, context->currentZoom, 1.0f));
						if (displayAudioWaveform && !context->display3D) {
							// Keep audio artwork compact, with room for the waveform below it.
							imageDataVert.transform = glm::scale(imageDataVert.transform,
								glm::vec3(audioArtworkScale, audioArtworkScale, 1.0f));
						}
						imageDataVert.transform = glm::scale(imageDataVert.transform, glm::vec3(windowSize.x, windowSize.y, 1.0));
						imageDataFrag.visibility = context->visibility;
						if (displayTip) imageDataFrag.visibility *= 0.5;
						imageDataFrag.blur = 0;
						imageDataFrag.force = 0;
						if (context->mode == RGB_Depth && viewX > 0) {
							if (!context->display3D || (context->imageType != Color_Plus_Depth &&
								videoDepthTexture == nullptr)) {
								imageDataFrag.force = 1;
							}
						}

						drawImage(commandBuffer, renderPass);
					}
				}
			}
		}

		viewsX = 1;
		if ((context->mode == SBS_Full || context->mode == SBS_Half
			|| context->mode == RGB_Depth) && context->fullscreen) {
			viewsX = 2;
		}

		for (auto view = 0; view < viewsX; view++) {
			auto viewColorPink = style.getColor(Style::Color::Pink, Style::Alpha::Strong);
			auto viewColorPinkSolid = style.getColor(Style::Color::Pink, Style::Alpha::Solid);
			auto viewColorWhite = style.getColor(Style::Color::White, Style::Alpha::Strong);
			auto viewColorWhiteSolid = style.getColor(Style::Color::White, Style::Alpha::Solid);
			auto viewColorWhiteLight = style.getColor(Style::Color::White, Style::Alpha::Weak);
			auto viewColorGrayLight = style.getColor(Style::Color::Gray, Style::Alpha::Weak);
			auto viewColorBlack = style.getColor(Style::Color::Black, Style::Alpha::Strong);
			auto viewColorBlackSolid = style.getColor(Style::Color::Black, Style::Alpha::Solid);
			auto viewColorBlueSolid = style.getColor(Style::Color::Black, Style::Alpha::Solid);
			if (context->mode == Anaglyph_Accurate || context->mode == Anaglyph_Vivid) {
				viewColorPink = style.toAnaglyph(viewColorPink);
				viewColorPinkSolid = style.toAnaglyph(viewColorPinkSolid);
			}

			auto drawViewport = getViewport(view, viewsX, windowSize);
			SDL_SetGPUViewport(renderPass, &drawViewport);

			bindPipeline(renderPass, spritePipeline);
			SDL_GPUTextureSamplerBinding sliderSampleBindings[1] = {{ .texture = sliderTexture, .sampler = imageSampler } };
			SDL_BindGPUFragmentSamplers(renderPass, 0, &sliderSampleBindings[0], 1);

			if (discMenuTexture != nullptr && !context->displayMenu) {
				const bool depthView = context->mode == RGB_Depth && view > 0;
				SDL_GPUTextureSamplerBinding discBinding{
					.texture = depthView && discMenuDepthTexture ? discMenuDepthTexture : discMenuTexture,
					.sampler = imageSampler};
				SDL_BindGPUFragmentSamplers(renderPass, 0, &discBinding, 1);
				setSpriteUniforms(glm::vec3(context->windowSize * 0.5f, 0.0f),
					glm::vec3(context->windowSize, 1.0f), glm::vec4(1.0f), 1.0f, 2,
					glm::vec2(0.0f), glm::vec2(1.0f), glm::vec2(0.5f), glm::vec3(1.0f));
				drawSprite(commandBuffer, renderPass);
				SDL_BindGPUFragmentSamplers(renderPass, 0, &sliderSampleBindings[0], 1);
			}

			if (displayAudioPlaceholder && !displayHelp) {
				// Use native texture pixels, independent of artwork zoom and UI scale.
				const glm::vec2 pixelScale = context->windowSize /
					glm::vec2(drawViewport.w, drawViewport.h);
				const float diameter = std::min(256.0f, std::min(drawViewport.w, drawViewport.h));
				setSpriteUniforms(glm::vec3(context->windowSize * 0.5f, 1.0f),
					glm::vec3(pixelScale * diameter, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.12f),
					1.0f, 1, { 0, 0 }, { 1, 1 }, { 0.5f, 0.5f }, glm::vec3(1.0f));
				drawSprite(commandBuffer, renderPass);

				bindPipeline(renderPass, iconPipeline);
				SDL_GPUTextureSamplerBinding logoBinding{ .texture = iconTexture, .sampler = imageSampler };
				SDL_BindGPUFragmentSamplers(renderPass, 0, &logoBinding, 1);
				iconDataVert.transform = getTransform(glm::vec3(context->windowSize * 0.5f, 1.0f),
					glm::vec3(pixelScale * diameter * 0.5f, 1.0f), glm::vec3(1.0f));
				iconDataVert.gridOffset = getIconCoordinates(IconType::Logo_White);
				iconDataFrag.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.33f);
				iconDataFrag.visibility = 1.0f;
				iconDataFrag.rotation = 0.0f;
				iconDataFrag.animated = 0;
				iconDataFrag.force = 0;
				drawIcon(commandBuffer, renderPass);
				bindPipeline(renderPass, spritePipeline);
				SDL_BindGPUFragmentSamplers(renderPass, 0, &sliderSampleBindings[0], 1);
			}

			if (displayAudioWaveform && !displayHelp) {
				const glm::vec2 pixelScale = context->windowSize / glm::vec2(drawViewport.w, drawViewport.h);
				const float width = std::min(256.0f, drawViewport.w * 0.7f);
				const float spacing = width / static_cast<float>(smoothedAudioWaveform.size());
				float baseline = displayAudioPlaceholder ? drawViewport.h * 0.5f - 176.0f : 88.0f;
				if (!displayAudioPlaceholder && !context->display3D) {
					// Match the fitted artwork height, including zoom and vertical pan.
					const float artworkHeight = drawViewport.h *
						std::min(1.0f, context->displayAspect.x / context->displayAspect.y) *
						context->currentZoom * audioArtworkScale;
					baseline = drawViewport.h * 0.5f - context->offset.y / pixelScale.y -
						artworkHeight * 0.5f - 48.0f;
				}
				baseline = std::min(drawViewport.h * 0.5f, std::max(88.0f, baseline));
				for (size_t i = 0; i < smoothedAudioWaveform.size(); ++i) {
					const float height = 2.0f + 34.0f * smoothedAudioWaveform[i];
					const glm::vec2 position(drawViewport.w * 0.5f - width * 0.5f +
						(static_cast<float>(i) + 0.5f) * spacing, baseline);
					setSpriteUniforms(glm::vec3(position * pixelScale, 1.0f),
						glm::vec3(glm::vec2(std::min(2.0f, spacing * 0.5f), height) * pixelScale, 1.0f),
						glm::vec4(1.0f, 1.0f, 1.0f, 0.55f), 1.0f, 0,
						{ 0, 0 }, { 1, 1 }, { 0.5f, 0.5f }, glm::vec3(1.0f));
					drawSprite(commandBuffer, renderPass);
				}
			}

			for (const auto& icon : *context->appIcons) {
				if (!icon.active) continue;
				if (icon.mode == IconMode::Slider) {
					auto iconCanvas = icon.canvas();
					auto sliderPosition = Utils::getCanvasPosition(context, &iconCanvas, nullptr, aspectScale * context->displayScale);
					auto slideEnd = icon.slider.size.y / icon.slider.size.x * 0.5;
					auto spriteColor = context->backgroundStyle == Light
					? glm::vec4(0.18f, 0.18f, 0.18f, 0.33f) : viewColorGrayLight;
					if (context->mode == RGB_Depth && view > 0) spriteColor = uiColorDepth;
					setSpriteUniforms(glm::vec3(sliderPosition, 1.0), glm::vec3(icon.slider.size * context->displayScale, 1.0),
						spriteColor, (float)icon.visibility, 1, { 0, 0 },
						{ 1, 1 }, glm::vec2(slideEnd, 1.0 - slideEnd),
						glm::vec3(aspectScale, 1.0));
					drawSprite(commandBuffer, renderPass);

					if (icon.type == IconType::VideoSeek && !context->chapterMarkers.empty()) {
						const float trackWidth = icon.slider.size.x * context->displayScale;
						const float trackHeight = icon.slider.size.y * context->displayScale;
						const float trackLeft = sliderPosition.x - trackWidth * 0.5f;
						const float tickWidth = std::max(2.0f * context->displayScale, 1.5f);
						const float tickHeight = trackHeight + 2.0f * context->displayScale;
						auto tickColor = context->backgroundStyle == Light
							? glm::vec4(0.0f, 0.0f, 0.0f, 0.75f)
							: glm::vec4(1.0f, 1.0f, 1.0f, 0.75f);
						if (context->mode == RGB_Depth && view > 0) tickColor = uiColorDepth;

						for (double markerPercent : context->chapterMarkers) {
							if (markerPercent <= 0.001 || markerPercent >= 0.999) continue;
							const float tickX = trackLeft + static_cast<float>(markerPercent) * trackWidth;
							setSpriteUniforms(glm::vec3(tickX, sliderPosition.y, 1.0f),
								glm::vec3(tickWidth, tickHeight, 1.0f),
								tickColor, (float)icon.visibility, 0, { 0, 0 },
								{ 1, 1 }, glm::vec2(0.5f, 0.5f),
								glm::vec3(aspectScale, 1.0f));
							drawSprite(commandBuffer, renderPass);
						}
					}

				}
			}

			if ((displayHelp || displayTip) && !context->displayMenu) {
				bindPipeline(renderPass, iconPipeline);
				SDL_GPUTextureSamplerBinding iconSampleBindings[1] = {{ .texture = iconTexture, .sampler = imageSampler } };
				SDL_BindGPUFragmentSamplers(renderPass, 0, &iconSampleBindings[0], 1);
				auto iconPosition = context->windowSize * glm::vec2(0.5);
				iconDataVert.transform = glm::translate(glm::mat4(1.0f),glm::vec3(iconPosition, 1.0f));
				iconDataVert.transform = glm::scale(iconDataVert.transform,
					glm::vec3(glm::vec2(2.0) * style.getIconRadius(Style::getCurrentScale()) * aspectScale * context->displayScale, 1.0f));
				iconDataVert.gridOffset = getIconCoordinates(
					context->backgroundStyle == Light ? IconType::Logo_Light : IconType::Logo_Dark);
				auto spriteColor = viewColorWhiteSolid;
				iconDataFrag.visibility = 1.0;
				iconDataFrag.animated = 0;
				iconDataFrag.force = 0;
				if (context->mode == RGB_Depth && view > 0) {
					spriteColor = uiColorDepth;
					iconDataFrag.force = 1;
				}
				iconDataFrag.color = spriteColor;

				drawIcon(commandBuffer, renderPass);
			}

			if ((displayHelp || displayTip) && helpTexture != nullptr && !context->displayMenu) {
				bindPipeline(renderPass, spritePipeline);
				SDL_GPUTextureSamplerBinding helpSampleBindings[1] = {{ .texture = helpTexture, .sampler = imageSampler } };
				SDL_BindGPUFragmentSamplers(renderPass, 0, &helpSampleBindings[0], 1);

				auto helpCenter = glm::vec3(windowSize.x * 0.5f,
					windowSize.y * 0.5f, 0.0f) - glm::vec3(0.0, (style.getIconRadius(Style::getCurrentScale()) + 32.0f) *
						context->displayScale, 0.0f);
				auto helpSize = glm::vec3(helpTextSize, 1.0);
				auto helpUvOffset = glm::vec2(0.0, 0.0);
				auto helpUvSize = glm::vec2(1.0, 1.0);
				auto helpTextColor = context->backgroundStyle == Light ?
					viewColorBlueSolid : viewColorWhiteSolid;
				if (context->mode == RGB_Depth && view > 0) helpTextColor = uiColorDepth;
				setSpriteUniforms(helpCenter, helpSize, helpTextColor,
					1.0f, 1, helpUvOffset, helpUvSize,
					glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
				drawSprite(commandBuffer, renderPass);
			}

			if (context->displayMenu && menuTexture != nullptr) {
				bindPipeline(renderPass, spritePipeline);
				SDL_GPUTextureSamplerBinding menuSampleBindings[1] = {{ .texture = menuTexture, .sampler = imageSampler }};
				SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);

				auto bgSize = glm::vec3(windowSize.x + 2.0f, windowSize.y + 2.0f, 1.0);
				setSpriteUniforms(glm::vec3(windowSize * 0.5f, 0.0), bgSize,
					viewColorBlack, 1.0f, 0,
					glm::vec2(0.0), glm::vec2(1.0),
					glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
				drawSprite(commandBuffer, renderPass);

				auto buttonMargin = style.getButtonMargin(Style::getCurrentScale());
				auto bgMargin = 32.0f;

				// Keep the logo in the menu's top inset and scroll it with the choices.
				bindPipeline(renderPass, iconPipeline);
				SDL_GPUTextureSamplerBinding logoBinding{ .texture = iconTexture, .sampler = imageSampler };
				SDL_BindGPUFragmentSamplers(renderPass, 0, &logoBinding, 1);
				const float logoSize = std::min(
					2.0f * style.getIconRadius(Style::getCurrentScale()) * context->displayScale,
					menuTopInset * 0.75f);
				const auto logoPosition = glm::vec3(windowSize.x * 0.5f,
					windowSize.y - menuTopInset * 0.5f, 0.0f) + menuMargin;
				iconDataVert.transform = glm::translate(glm::mat4(1.0f), logoPosition);
				iconDataVert.transform = glm::scale(iconDataVert.transform,
					glm::vec3(glm::vec2(logoSize) * aspectScale, 1.0f));
				iconDataVert.gridOffset = getIconCoordinates(IconType::Logo_White);
				iconDataFrag.color = viewColorWhiteSolid;
				iconDataFrag.visibility = context->mode == RGB_Depth && view > 0 ? 0.0f : 1.0f;
				iconDataFrag.animated = 0;
				iconDataFrag.force = 0;
				drawIcon(commandBuffer, renderPass);
				bindPipeline(renderPass, spritePipeline);
				SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);

				for (const auto& choice : *context->menuChoices) {
					if (!choice.active) continue;
					if (choice.inlineText) {
						const bool singleLine = choice.readOnly || choice.options.size() <= 1;
						menuSampleBindings[0] = { .texture = menuTexture, .sampler = imageSampler };
						SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);
						std::string heading = choice.label + (choice.options.empty() ? "" : ":");
						if (singleLine) {
							const int selected = choice.readOnly ? (*context->menuSelection)[choice.label] : 0;
							if (selected >= 0 && selected < static_cast<int>(choice.options.size()))
								heading += " " + choice.options[selected];
							if (choice.unavailable) heading += " (Unavailable)";
						}
						const auto& text = optionTextures[heading];
						const float visibility = context->mode == RGB_Depth && view > 0 ? 0.0f : 1.0f;
						const bool linkHover = !choice.readOnly && singleLine && (*context->menuRollover)[choice.label] == 0;
						setSpriteUniforms(choice.layout.position + menuMargin, glm::vec3(text.size, 1.0f),
							linkHover ? viewColorPinkSolid : viewColorWhiteSolid, visibility, 1, text.offset / menuTextureSize,
							text.size / menuTextureSize, glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
						drawSprite(commandBuffer, renderPass);
						if (!singleLine) {
							for (size_t i = 0; i < choice.options.size(); ++i) {
								const auto& option = optionTextures[choice.options[i]];
								const bool hover = (*context->menuRollover)[choice.label] == static_cast<int>(i);
								setSpriteUniforms(choice.layouts[i].position + menuMargin, glm::vec3(option.size, 1.0f),
									hover ? viewColorPinkSolid : viewColorWhiteSolid, visibility, 1,
									option.offset / menuTextureSize, option.size / menuTextureSize,
									glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
								drawSprite(commandBuffer, renderPass);
							}
						}
						continue;
					}
					menuSampleBindings[0] = { .texture = sliderTexture, .sampler = imageSampler };
					SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);

					auto uvOffset = optionTextures[choice.label].offset / menuTextureSize;
					auto uvSize = optionTextures[choice.label].size / menuTextureSize;

					auto choiceBgSize = glm::vec3(optionTextures[choice.label].size +
						glm::vec2(buttonMargin * 2.0f + 2.0f) + glm::vec2(bgMargin, 0.0), 1.0);
					auto bgSliceEnd = choiceBgSize.y / choiceBgSize.x * 0.5;
					auto bgSlice = glm::vec2(bgSliceEnd, 1.0 - bgSliceEnd);

					auto spriteColor = viewColorBlackSolid;
					if (context->mode == RGB_Depth && view > 0) spriteColor = uiColorBGDepth;
					setSpriteUniforms(choice.layout.position + menuMargin, choiceBgSize, spriteColor,
						1.0f, 1, {0.0, 0.0 }, { 1.0, 1.0 }, bgSlice,
						glm::vec3(aspectScale, 1.0));
					drawSprite(commandBuffer, renderPass);

					menuSampleBindings[0] = { .texture = menuTexture, .sampler = imageSampler };
					SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);

					spriteColor = viewColorWhiteSolid;
					auto menuTextVis = 1.0f;
					if (context->mode == RGB_Depth && view > 0) menuTextVis = 0.0f;
					setSpriteUniforms(choice.layout.position + menuMargin, choice.layout.size, spriteColor,
					menuTextVis, 1, uvOffset, uvSize,glm::vec2(0.5),
					glm::vec3(aspectScale, 1.0));
					drawSprite(commandBuffer, renderPass);

					auto optionIndex = 0;
					for (const auto& option : choice.options) {
						uvOffset = optionTextures[option].offset / menuTextureSize;
						uvSize = optionTextures[option].size / menuTextureSize;
						auto selection = (*context->menuSelection)[choice.label] == optionIndex;
						auto rollover = (*context->menuRollover)[choice.label] == optionIndex;

						if (selection || (context->mode == RGB_Depth && view > 0)) {
							menuSampleBindings[0] = { .texture = sliderTexture, .sampler = imageSampler };
							SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);
							auto buttonSize = glm::vec3(style.getOptionsSize(Style::getCurrentScale()) * context->displayScale,
								optionTextures[choice.label].size.y + buttonMargin * 2.0f + 2.0f, 1.0);
							bgSliceEnd = buttonSize.y / buttonSize.x * 0.5;
							bgSlice = glm::vec2(bgSliceEnd, 1.0 - bgSliceEnd);
							spriteColor = viewColorPink;
							if (context->mode == RGB_Depth && view > 0) spriteColor = uiColorBGDepth;
							setSpriteUniforms(choice.layouts[optionIndex].position + menuMargin, buttonSize, spriteColor,
								1.0f, 1, {0.0, 0.0 }, { 1.0, 1.0 }, bgSlice, glm::vec3(aspectScale, 1.0));
							drawSprite(commandBuffer, renderPass);
						}

						menuSampleBindings[0] = { .texture = menuTexture, .sampler = imageSampler };
						SDL_BindGPUFragmentSamplers(renderPass, 0, &menuSampleBindings[0], 1);

						spriteColor = rollover && !selection ? viewColorPinkSolid : viewColorWhiteSolid;
						auto menuTextVis = 1.0f;
						if (context->mode == RGB_Depth && view > 0) menuTextVis = 0.0f;
						setSpriteUniforms(choice.layouts[optionIndex].position + menuMargin,
							choice.layouts[optionIndex].size, spriteColor,
							menuTextVis, 1, uvOffset, uvSize, glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
						drawSprite(commandBuffer, renderPass);

						optionIndex++;
					}
				}
			}

			if (!context->displayMenu && infoTexture != nullptr) {
				bindPipeline(renderPass, spritePipeline);
				SDL_GPUTextureSamplerBinding infoSampleBindings[1] = {{ .texture = infoTexture, .sampler = imageSampler } };
				SDL_BindGPUFragmentSamplers(renderPass, 0, &infoSampleBindings[0], 1);

				auto infoMargin = 12.0f * context->displayScale;
				auto infoCenter = glm::vec3(windowSize.x * 0.5f,
					windowSize.y, 0.0f) - glm::vec3(0.0,  infoMargin, 0.0f);
				auto infoSize = glm::vec3(windowSize.x, infoTextSize.y + infoMargin, 1.0);
				auto infoUvOffset = glm::vec2(0.0, 0.0);
				auto infoUvSize = glm::vec2(1.0, 1.0);
				auto infoColor = viewColorBlack;
				if (context->mode == RGB_Depth && view > 0) infoColor = uiColorDepth;

				setSpriteUniforms(infoCenter, infoSize, infoColor,
					infoCurrentVisibility, 0, infoUvOffset, infoUvSize,
					glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
				drawSprite(commandBuffer, renderPass);

				if (displayInfo) {
					infoCenter = glm::vec3(windowSize.x * 0.5f,
						windowSize.y, 0.0f) - glm::vec3(0.0,  (infoMargin + 2.0f * context->displayScale), 0.0f);
					infoSize = glm::vec3(infoTextSize, 1.0);
					infoUvOffset = glm::vec2(0.0, 0.0);
					infoUvSize = glm::vec2(1.0, 1.0);
					infoColor = viewColorWhiteSolid;
					if (context->mode == RGB_Depth && view > 0) infoColor = uiColorDepth;
					setSpriteUniforms(infoCenter, infoSize, infoColor,
						1.0f, 1, infoUvOffset, infoUvSize,
						glm::vec2(0.5), glm::vec3(aspectScale, 1.0));
					drawSprite(commandBuffer, renderPass);
				}
			}

			// Place bitmap captions in source-image coordinates; text captions use a screen-aligned
			// baseline and shadow.
			if (!context->displayMenu && subtitleTexture != nullptr) {
				bindPipeline(renderPass, spritePipeline);
				SDL_GPUTextureSamplerBinding subtitleBindings[1] = {{ .texture = subtitleTexture, .sampler = imageSampler }};
				SDL_BindGPUFragmentSamplers(renderPass, 0, &subtitleBindings[0], 1);
				if (subtitleBitmap) {
					const auto canvasSize = glm::vec2(subtitleBitmapCanvasSize);
					const auto bitmapSize = subtitleTextSize;
					const auto subtitleScale = context->safeSize / canvasSize *
						static_cast<float>(context->currentZoom);
					const auto bitmapCenter = glm::vec2(subtitleBitmapPosition) + bitmapSize * 0.5f;
					const auto sourceCenter = bitmapCenter - canvasSize * 0.5f;
					const auto screenCenter = windowSize * 0.5f +
						glm::vec2(context->offset.x, -context->offset.y) + sourceCenter * subtitleScale;
					setSpriteUniforms(glm::vec3(screenCenter, 0.0f),
						glm::vec3(bitmapSize * subtitleScale, 1.0f), viewColorWhiteSolid,
						1.0f, 1, {0.0f, 0.0f}, {1.0f, 1.0f},
						glm::vec2(0.5f), glm::vec3(aspectScale, 1.0f));
					drawSprite(commandBuffer, renderPass);
				} else if (subtitleShadowTexture != nullptr) {
					const auto subtitleMargin = 24.0f * context->displayScale;
					const auto subtitleCenter = glm::vec3(windowSize.x * 0.5f, windowSize.y, 0.0f) -
						glm::vec3(0.0f, subtitleTextSize.y + subtitleMargin, 0.0f);
					const auto subtitleShadowOffset = 1.5f * context->displayScale / aspectScale;
					const auto subtitleSize = glm::vec3(subtitleTextSize, 1.0f);
					for (const auto& offset : std::array<glm::vec2, 4>{
						glm::vec2(-subtitleShadowOffset.x, 0.0f),
						glm::vec2(subtitleShadowOffset.x, 0.0f),
						glm::vec2(0.0f, -subtitleShadowOffset.y),
						glm::vec2(0.0f, subtitleShadowOffset.y)}) {
						SDL_GPUTextureSamplerBinding shadowBindings[1] = {{ .texture = subtitleShadowTexture, .sampler = imageSampler }};
						SDL_BindGPUFragmentSamplers(renderPass, 0, &shadowBindings[0], 1);
						setSpriteUniforms(subtitleCenter + glm::vec3(offset, 0.0f), subtitleSize,
							glm::vec4(0.0f, 0.0f, 0.0f, 0.33f), 1.0f, 1, {0.0f, 0.0f}, {1.0f, 1.0f},
							glm::vec2(0.5f), glm::vec3(aspectScale, 1.0f));
						drawSprite(commandBuffer, renderPass);
					}
					SDL_BindGPUFragmentSamplers(renderPass, 0, &subtitleBindings[0], 1);
					// SDL alpha-blits this text onto a transparent surface, producing
					// premultiplied RGB. Atlas and directly uploaded text stay straight.
					setSpriteUniforms(subtitleCenter, subtitleSize, viewColorWhiteSolid, 1.0f, 2,
						{0.0f, 0.0f}, {1.0f, 1.0f}, glm::vec2(0.5f), glm::vec3(aspectScale, 1.0f));
					drawSprite(commandBuffer, renderPass);
				}
			}

			bindPipeline(renderPass, iconPipeline);
			SDL_GPUTextureSamplerBinding iconSampleBindings[1] = {{ .texture = iconTexture, .sampler = imageSampler } };
			SDL_BindGPUFragmentSamplers(renderPass, 0, &iconSampleBindings[0], 1);

			for (const auto& icon : *context->appIcons) {
				if (!icon.active || icon.type == IconType::None) continue;
				auto iconCanvas = icon.canvas();
				auto iconPosition = Utils::getCanvasPosition(context, &iconCanvas, &icon.slider, aspectScale * context->displayScale);
				iconDataVert.transform = glm::translate(glm::mat4(1.0f),glm::vec3(iconPosition, 1.0f));
				auto iconRadius = style.getIconRadius(Style::getCurrentScale());
				if (icon.mode == IconMode::Slider || icon.type == IconType::VideoAudio ||
					icon.type == IconType::VideoCaption)
					iconRadius = style.getIconSlider(Style::getCurrentScale());
				iconDataVert.transform = glm::scale(iconDataVert.transform,
					glm::vec3(glm::vec2(2.0) * iconRadius * aspectScale * context->displayScale, 1.0f));
				iconDataVert.gridOffset = getIconCoordinates(IconType::Background);
				iconDataFrag.color = icon.state == IconState::Over ? viewColorPink : viewColorBlack;
				if (context->mode == RGB_Depth && view > 0) iconDataFrag.color = uiColorDepth;
				iconDataFrag.visibility = (float)icon.visibility;
				iconDataFrag.rotation = 0.0f;
				iconDataFrag.animated = 0;

				if (icon.type != IconType::Loading) {
					drawIcon(commandBuffer, renderPass);
				}

				iconPosition = icon.canvas().position + icon.canvas().alignment * context->windowSize;
				iconPosition.y = context->windowSize.y - iconPosition.y;
				iconDataVert.gridOffset = getIconCoordinates(icon.image);
				iconDataFrag.color = icon.color;
				if (context->mode == RGB_Depth && view > 0) iconDataFrag.color = uiColorDepth;
				iconDataFrag.visibility = (float)icon.visibility;

				if (icon.type == IconType::Loading) {
					const auto pi = 3.14159265359f;
					iconDataVert.transform = glm::rotate(iconDataVert.transform,
						-context->loadingRotation * pi, glm::vec3(0.0f, 0.0f, 1.0f));
					iconDataFrag.rotation = context->loadingRotation;
					iconDataFrag.animated = 1;
					if (displayTip) iconDataFrag.visibility = 0.0;
				}

				drawIcon(commandBuffer, renderPass);
			}

			auto cursorPosition = context->mouse;
			cursorPosition.y = context->windowSize.y - cursorPosition.y;
			static glm::vec2 cursorOffset{9.0, -16.0 };
			iconDataVert.transform = glm::translate(glm::mat4(1.0f),glm::vec3(cursorPosition +
				cursorOffset * aspectScale * context->displayScale, 0.0f));
			iconDataVert.transform = glm::scale(iconDataVert.transform,
				glm::vec3(glm::vec2(style.getIconRadius(Style::getCurrentScale())) * aspectScale * context->displayScale, 1.0f));
			iconDataVert.gridOffset = getIconCoordinates(IconType::Cursor_Black);
			iconDataFrag.color = viewColorWhiteSolid;
			iconDataFrag.visibility = context->mouseVisibility;
			iconDataFrag.animated = 0;
			iconDataFrag.force = 0;
			if (context->mode == RGB_Depth && view > 0) {
				iconDataFrag.color = uiColorDepth;
				iconDataFrag.force = 1;
			}

			drawIcon(commandBuffer, renderPass);
		}

		SDL_EndGPURenderPass(renderPass);
	}

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);

	recordBrowserDraw(context, swapchainTexture != nullptr);
	return 0;
}

// Translate source layout and display calibration into lenticular interlacer uniforms.
void Image::updateInterlacerUniforms(Context* context, int width, int height, NativeDisplayConfig& config) {
	if (!config.cubeViC1 && context->imageType == Light_Field_LKG && context->gridSize.x > 0.0f &&
		context->gridSize.y > 0.0f) {
		config.quiltGrid = {context->gridSize.x, context->gridSize.y};
		config.viewCount = (int)std::lround(
			context->gridSize.x * context->gridSize.y);
	}
	interlacerDataFrag.outputSize = {(float)width, (float)height};
	interlacerDataFrag.imageSize = context->imageSize;
	interlacerDataFrag.quiltSize = config.quiltGrid;
	interlacerDataFrag.tileSize = 1.0f / config.quiltGrid;
	const bool hasSlope = std::abs(config.slope) > 0.001f;
	float phaseX = config.pitch / std::max(config.dpi, 1.0f);
	if (hasSlope)
		phaseX *= std::cos(std::atan(1.0f / config.slope));
	const float phaseY = hasSlope ? phaseX / config.slope : 0.0f;
	interlacerDataFrag.phaseScale = {phaseX, phaseY};
	interlacerDataFrag.center = config.center;
	interlacerDataFrag.subpixelPhase = phaseX / 3.0f + config.subpixel;
	if (config.flipSubpixel)
		interlacerDataFrag.subpixelPhase = -interlacerDataFrag.subpixelPhase;
	interlacerDataFrag.cubeViC1 = config.cubeViC1 ? 1 : 0;
	if (config.cubeViC1) {
		const auto& optics = config.cubeViOptics;
		// Pass raw vendor parameters; preserve the vendor's operation order in
		// the shader to avoid phase drift at view boundaries.
		interlacerDataFrag.phaseScale = {optics.interval, optics.obliquity};
		interlacerDataFrag.center = optics.deviation;
		interlacerDataFrag.subpixelPhase = 0.0f;
		if (context->imageType == Light_Field_LKG && context->gridSize.x > 0 && context->gridSize.y > 0) {
			interlacerDataFrag.quiltSize = {context->gridSize.x, context->gridSize.y};
			interlacerDataFrag.tileSize = 1.0f / interlacerDataFrag.quiltSize;
		}
	}
	interlacerDataFrag.viewCount = std::min(config.viewCount,
		(int)(config.quiltGrid.x * config.quiltGrid.y));
	interlacerDataFrag.gridColumns = (int)interlacerDataFrag.quiltSize.x;
	interlacerDataFrag.gridRows = (int)interlacerDataFrag.quiltSize.y;
	// Stereo sources contain two actual eye images, so use two views on every
	// native display regardless of its RGB-D or quilt view count.
	const bool nativeStereoSource = isNativeStereoSource(context->imageType);
	if (nativeStereoSource) {
		interlacerDataFrag.quiltSize = {2.0f, 1.0f};
		interlacerDataFrag.tileSize = {0.5f, 1.0f};
		interlacerDataFrag.viewCount = 2;
		interlacerDataFrag.gridColumns = 2;
		interlacerDataFrag.gridRows = 1;
	}
	interlacerDataFrag.output2D = (!config.calibrated || context->mode != Lenticular || !context->display3D) ? 1 : 0;
	const bool isLenticular2View = config.viewCount == 2 || nativeStereoSource;
	if (isLenticular2View) {
		interlacerDataFrag.sourceFlat = (context->imageType == Color_Only) ? 1 : 0;
	} else {
		interlacerDataFrag.sourceFlat = (context->imageType != Color_Plus_Depth &&
			context->imageType != Light_Field_LKG) ? 1 : 0;
	}
	// invView orders supplied views. RGB-D views are generated from depth in
	// screen-phase order, so reversing their indices flips the parallax.
	const bool generatedLightField = !config.cubeViC1 && config.viewCount > 2 &&
		context->imageType == Color_Plus_Depth;
	interlacerDataFrag.invertView =
		generatedLightField ? 0 : (config.invertView ? 1 : 0);
	interlacerDataFrag.flipImageX = config.flipImageX ? 1 : 0;
	interlacerDataFrag.flipImageY = config.flipImageY ? 1 : 0;
	interlacerDataFrag.separateDepth = videoDepthTexture != nullptr ? 1 : 0;
	interlacerDataFrag.swapLeftRight = context->swapLeftRight;
	interlacerDataFrag.sourceRgbd = context->imageType == Color_Plus_Depth ? 1 : 0;
	interlacerDataFrag.stereoStrength = (float)context->stereoStrength;
	interlacerDataFrag.stereoDepth = (float)context->stereoDepth;
	interlacerDataFrag.stereoOffset = (float)context->stereoOffset;
	interlacerDataFrag.sourceType = context->imageType;
	interlacerDataFrag.testPattern = testPatternMode;
}

// Show a temporary native-output notice when the display is using default calibration.
void Image::drawNativeCalibrationWarning(Context* context, SDL_GPUCommandBuffer* commandBuffer,
	SDL_GPURenderPass* renderPass, int width, int height, const NativeDisplayConfig& config) {
	if (!nativeOutputEnabled || !config.usingDefaultCalibration ||
		context->mode != Lenticular || !context->display3D || nativeCalibrationTexture == nullptr) return;

	// Session-only state: display reconnects and 3D toggles must not restart the notice.
	static bool shown = false;
	static Uint64 startedAt = 0;
	const Uint64 now = SDL_GetTicks();
	if (!shown) {
		shown = true;
		startedAt = now;
	}
	if (now - startedAt >= 3000) return;

	const auto nativeProjection = glm::ortho(0.0f, (float)width, 0.0f, (float)height);
	const auto closeCenter = glm::vec2(width, height) * 0.5f;
	const auto closeRadius = style.getIconRadius(Style::getCurrentScale()) * context->displayScale;

	bindPipeline(renderPass, iconPipeline);
	SDL_GPUTextureSamplerBinding iconBinding{
		.texture = iconTexture, .sampler = imageSampler};
	SDL_BindGPUFragmentSamplers(renderPass, 0, &iconBinding, 1);
	iconDataVert.projection = nativeProjection;
	iconDataVert.transform = glm::translate(glm::mat4(1.0f),
		glm::vec3(closeCenter, 1.0f));
	iconDataVert.transform = glm::scale(iconDataVert.transform,
		glm::vec3(glm::vec2(2.0f * closeRadius), 1.0f));
	iconDataVert.gridOffset = getIconCoordinates(IconType::Close);
	iconDataFrag.color = context->backgroundStyle == Light
		? style.getColor(Style::Color::Black, Style::Alpha::Solid)
		: style.getColor(Style::Color::White, Style::Alpha::Solid);
	iconDataFrag.visibility = 1.0f;
	iconDataFrag.rotation = 0.0f;
	iconDataFrag.animated = 0;
	iconDataFrag.force = 0;
	drawIcon(commandBuffer, renderPass);

	bindPipeline(renderPass, spritePipeline);
	SDL_GPUTextureSamplerBinding infoBinding{
		.texture = nativeCalibrationTexture, .sampler = imageSampler};
	SDL_BindGPUFragmentSamplers(renderPass, 0, &infoBinding, 1);
	spriteDataVert.projection = nativeProjection;
	const auto textColor = context->backgroundStyle == Light
		? style.getColor(Style::Color::Black, Style::Alpha::Solid)
		: style.getColor(Style::Color::White, Style::Alpha::Solid);
	setSpriteUniforms(
		glm::vec3(closeCenter, 0.0f) -
			glm::vec3(0.0f, closeRadius + 32.0f * context->displayScale, 0.0f),
		glm::vec3(nativeCalibrationTextSize, 1.0f), textColor, 1.0f, 1,
		{0.0f, 0.0f}, {1.0f, 1.0f}, glm::vec2(0.5f), glm::vec3(1.0f));
	drawSprite(commandBuffer, renderPass);
}

// Render each additional native output window using its own calibration.
int Image::drawNativeOutput(Context* context) {
	if (!context) return 0;
	int result = 0;
	for (size_t i = 0; i < nativeOutputs.size(); ++i) {
		auto& output = nativeOutputs[i];
		if (output.window == context->window) continue;
		if (drawNativeOutput(context, output, i == 0 ? nativeDisplayConfig : output.config) != 0)
			result = -1;
	}
	return result;
}

// Present the current source on one native display with its calibrated interlacing or mono fallback.
int Image::drawNativeOutput(Context* context, NativeOutput& output, NativeDisplayConfig& config) {
	if (!nativeOutputEnabled || !nativeOutputSourceReady || context == nullptr ||
		output.window == nullptr ||
		interlacerPipeline == nullptr) return 0;
	SDL_GPUTexture* quiltTexture = imageTexture;
	if (quiltTexture == nullptr) return 0;

	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(output.window, &width, &height);
	if (width <= 0 || height <= 0) return 0;
	const bool outputSizeChanged = output.lastSize != glm::ivec2(width, height);
	if (outputSizeChanged) {
		output.lastSize = {width, height};
		SDL_Log("Native output swapchain: %dx%d physical pixels", width, height);
		if (config.calibrated &&
			(width != config.screenSize.x || height != config.screenSize.y)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Native output is %dx%d but calibration requires %dx%d. "
				"Compositor scaling will prevent correct lenticular output.",
				width, height, config.screenSize.x,
				config.screenSize.y);
		}
	}
	updateInterlacerUniforms(context, width, height, config);
	static int lastCubeViOutput2D = -1;
	if (config.cubeViC1 &&
		(outputSizeChanged || lastCubeViOutput2D != interlacerDataFrag.output2D)) {
		SDL_Log("CubeVi output: views=%d quilt=%dx%d sourceType=%d output2D=%d",
			interlacerDataFrag.viewCount, interlacerDataFrag.gridColumns,
			interlacerDataFrag.gridRows, interlacerDataFrag.sourceType, interlacerDataFrag.output2D);
		lastCubeViOutput2D = interlacerDataFrag.output2D;
	} else if (outputSizeChanged && !config.cubeViC1) {
		SDL_Log("Native phase: X=%.8f Y=%.8f center=%.8f subpixel=%.8f "
			"origin=%d view=%s",
			interlacerDataFrag.phaseScale.x, interlacerDataFrag.phaseScale.y,
			interlacerDataFrag.center,
			interlacerDataFrag.subpixelPhase, 1,
			interlacerDataFrag.invertView != 0 ? "inverted" : "forward");
	}

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) return -1;
	SDL_GPUTexture* outputTexture = nullptr;
	if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, output.window,
		&outputTexture, nullptr, nullptr)) {
		SDL_CancelGPUCommandBuffer(commandBuffer);
		return -1;
	}
	if (outputTexture == nullptr) {
		SDL_SubmitGPUCommandBuffer(commandBuffer);
		return 0;
	}
	SDL_GPUColorTargetInfo targetInfo{};
	targetInfo.texture = outputTexture;
	targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	targetInfo.store_op = SDL_GPU_STOREOP_STORE;
	if (context->backgroundStyle == Solid) clearColorCurrent = clearColorSolid;
	else if (context->backgroundStyle == Light) clearColorCurrent = clearColorLight;
	else if (context->backgroundStyle == Dark) clearColorCurrent = clearColorDark;
	targetInfo.clear_color = {clearColorCurrent.r, clearColorCurrent.g,
		clearColorCurrent.b, clearColorCurrent.a};
	SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, nullptr);
	if (renderPass == nullptr) {
		SDL_SubmitGPUCommandBuffer(commandBuffer);
		return -1;
	}

	SDL_GPUViewport viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
	SDL_SetGPUViewport(renderPass, &viewport);
	if (context->backgroundStyle == Blur && blurTexture != nullptr &&
		blurTextureNext != nullptr) {
		bindPipeline(renderPass, imagePipeline);
		SDL_GPUTextureSamplerBinding backgroundBindings[4] = {
			{.texture = imageTexture, .sampler = imageSampler},
			{.texture = blurTexture, .sampler = imageSampler},
			{.texture = blurTextureNext, .sampler = imageSampler},
			{.texture = videoDepthTexture != nullptr ? videoDepthTexture : imageTexture,
				.sampler = imageSampler}
		};
		SDL_BindGPUFragmentSamplers(renderPass, 0, &backgroundBindings[0], 4);
		imageDataVert.displayImageAspect = {1.0f, 1.0f, 1.0f};
		imageDataVert.fillScreen = 1;
		imageDataVert.projection = glm::mat4(1.0f);
		imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
		imageDataFrag.windowSize = {(float)width, (float)height};
		imageDataFrag.imageSize = context->imageSize;
		imageDataFrag.gridSize = context->gridSize;
		imageDataFrag.visibility = 1.0f;
		imageDataFrag.mode = context->mode;
		imageDataFrag.type = context->imageType;
		imageDataFrag.separateDepth = videoDepthTexture != nullptr ? 1 : 0;
		imageDataFrag.blur = 1;
		drawImage(commandBuffer, renderPass);
	}

	bindPipeline(renderPass, interlacerPipeline);
	imageDataVert.displayImageAspect = {1.0f, 1.0f, 1.0f};
	imageDataVert.displayImageAspect.x = (float)width / (float)height;
	imageDataVert.displayImageAspect.y = context->imageSize.x / context->imageSize.y;
	imageDataVert.fillScreen = 0;
	imageDataVert.projection = glm::mat4(1.0f);
	imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &imageDataVert, sizeof(imageDataVert));
	SDL_GPUTextureSamplerBinding bindings[2] = {
		{.texture = quiltTexture, .sampler = imageSampler},
		{.texture = videoDepthTexture != nullptr ? videoDepthTexture : quiltTexture,
			.sampler = imageSampler}
	};
	SDL_BindGPUFragmentSamplers(renderPass, 0, bindings, 2);
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &interlacerDataFrag,
		sizeof(interlacerDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);

	drawNativeCalibrationWarning(context, commandBuffer, renderPass, width, height, config);
	SDL_EndGPURenderPass(renderPass);
	SDL_SubmitGPUCommandBuffer(commandBuffer);
	return 0;
}

// Wait for outstanding GPU work and release rendering resources, fonts, and windows.
void Image::quit(Context* context){
	if (context->device != nullptr) SDL_WaitForGPUIdle(context->device);
	if (gettingStartedTexture) SDL_ReleaseGPUTexture(context->device, gettingStartedTexture);
	gettingStartedTexture = nullptr;
	GettingStarted::release();
	setNativeOutputActive(context, false);
	SDL_ReleaseGPUGraphicsPipeline(context->device, interlacerPipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, imagePipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, lanczosPipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, depthRefinePipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, depthRefineR16Pipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, videoYUVPipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, iconPipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, spritePipeline);
	SDL_ReleaseGPUBuffer(context->device, sharedVertexBuffer);
	SDL_ReleaseGPUBuffer(context->device, sharedIndexBuffer);
	SDL_ReleaseGPUTexture(context->device, imageTexture);
	if (videoYTexture != nullptr) SDL_ReleaseGPUTexture(context->device, videoYTexture);
	if (videoUTexture != nullptr) SDL_ReleaseGPUTexture(context->device, videoUTexture);
	if (videoVTexture != nullptr) SDL_ReleaseGPUTexture(context->device, videoVTexture);
	if (videoDepthTexture != nullptr) SDL_ReleaseGPUTexture(context->device, videoDepthTexture);
	SDL_ReleaseGPUTexture(context->device, blurTexture);
	SDL_ReleaseGPUTexture(context->device, blurTextureNext);
	if (discBackgroundTexture) SDL_ReleaseGPUTexture(context->device, discBackgroundTexture);
	discBackgroundTexture = nullptr;
	SDL_ReleaseGPUTexture(context->device, iconTexture);
	SDL_ReleaseGPUTexture(context->device, helpTexture);
	if (subtitleTexture != nullptr) SDL_ReleaseGPUTexture(context->device, subtitleTexture);
	if (subtitleShadowTexture != nullptr) SDL_ReleaseGPUTexture(context->device, subtitleShadowTexture);
	SDL_ReleaseGPUTexture(context->device, menuTexture);
	SDL_ReleaseGPUTexture(context->device, sliderTexture);
	if (nativeCalibrationTexture != nullptr)
		SDL_ReleaseGPUTexture(context->device, nativeCalibrationTexture);
	if (exportTexture != nullptr) SDL_ReleaseGPUTexture(context->device, exportTexture);
	exportTexture = nullptr;
	exportTextureSize = {0, 0};
	SDL_ReleaseGPUSampler(context->device, imageSampler);
	SDL_DestroySurface(menuTextSurface);
	SDL_DestroySurface(ssimSurface);
	TTF_CloseFont(menuFont);
	TTF_CloseFont(subtitleFont);
	TTF_CloseFont(helpFont);
	TTF_Quit();
	Core::quit(context);
}
