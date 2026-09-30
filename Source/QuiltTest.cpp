// Copyright (c) 2026 Outmode

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "rapidjson/document.h"
#include "CalibrationVolumes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
	constexpr int PatternCount = 10;
	constexpr const char* PatternNames[PatternCount] = {
		"physical-pixel checkerboard",
		"one-pixel RGB stripes",
		"two-view red/green",
		"66-view low bits",
		"66-view high bits",
		"66-view RGB subpixels",
		"orientation quadrants",
		"smooth 66-view scene, strong disparity",
		"smooth 66-view scene, moderate disparity",
		"smooth 66-view scene, zero disparity"
	};

	struct Calibration {
		float pitch = 80.0f;
		float slope = -6.0f;
		float center = 0.5f;
		float dpi = 491.0f;
		int screenWidth = 1440;
		int screenHeight = 2560;
		bool invertView = false;
		bool flipSubpixel = false;
		std::string source = "built-in fallback";
	};

	struct alignas(16) QuiltTestData {
		float outputSize[2]{};
		float phaseScale[2]{};
		float center = 0.0f;
		float subpixelPhase = 0.0f;
		int viewCount = 66;
		int pattern = 0;
		int phaseOrigin = 1;
		int invertView = 0;
		int expectedWidth = 0;
		int expectedHeight = 0;
	};

	static_assert(sizeof(QuiltTestData) == 48);

	struct App {
		SDL_Window* window = nullptr;
		SDL_GPUDevice* device = nullptr;
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		Calibration calibration{};
		QuiltTestData uniforms{};
		SDL_DisplayID display = 0;
		int lastPixelWidth = 0;
		int lastPixelHeight = 0;
		Uint32 lastSwapchainWidth = 0;
		Uint32 lastSwapchainHeight = 0;
		bool dimensionsMatch = false;
		bool slantPitchCorrection = true;
	};

	// Read a calibration number from a direct or nested value field.
	bool readNumber(const rapidjson::Value& object, const char* name, float& result) {
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

	// Interpret a numeric calibration field as a boolean flag.
	bool readBool(const rapidjson::Value& object, const char* name, bool& result) {
		float value = 0.0f;
		if (!readNumber(object, name, value)) return false;
		result = value != 0.0f;
		return true;
	}

	// Find an explicit calibration override or the calibration stored on a mounted Looking Glass
	// device.
	std::filesystem::path findCalibration() {
		if (const char* requested = std::getenv("RENDEPTH_NATIVE_CALIBRATION"))
			return requested;

	#ifdef __APPLE__
		return CalibrationVolumes::lookingGlassCalibration("/Volumes");
	#else
		std::error_code error;
		for (const auto& root : {std::filesystem::path("/run/media"),
				 std::filesystem::path("/media")}) {
			if (!std::filesystem::is_directory(root, error)) continue;
			for (const auto& user : std::filesystem::directory_iterator(root, error)) {
				if (error || !user.is_directory(error)) continue;
				for (const auto& volume : std::filesystem::directory_iterator(user.path(), error)) {
					if (error || !volume.is_directory(error)) continue;
					const auto candidate = volume.path() / "LKG_calibration" / "visual.json";
					if (std::filesystem::is_regular_file(candidate, error)) return candidate;
				}
			}
		}
		return {};
	#endif
	}

	// Load calibration for the visual test while retaining fallback settings if it is unavailable.
	bool loadCalibration(Calibration& calibration) {
		const auto path = findCalibration();
		if (path.empty()) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"No visual.json found; using fallback calibration.");
			return false;
		}

		std::ifstream file(path);
		const std::string text((std::istreambuf_iterator<char>(file)), {});
		rapidjson::Document document;
		document.Parse(text.data(), text.size());
		float width = 0.0f;
		float height = 0.0f;
		Calibration loaded = calibration;
		if (document.HasParseError() || !document.IsObject() ||
			!readNumber(document, "pitch", loaded.pitch) || loaded.pitch <= 0.0f ||
			!readNumber(document, "slope", loaded.slope) || std::abs(loaded.slope) < 0.001f ||
			!readNumber(document, "center", loaded.center) ||
			!readNumber(document, "DPI", loaded.dpi) || loaded.dpi <= 0.0f ||
			!readNumber(document, "screenW", width) || width <= 0.0f ||
			!readNumber(document, "screenH", height) || height <= 0.0f) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid Looking Glass calibration: %s", path.string().c_str());
			return false;
		}
		loaded.screenWidth = static_cast<int>(std::lround(width));
		loaded.screenHeight = static_cast<int>(std::lround(height));
		bool vendorInvertView = false;
		if (readBool(document, "invView", vendorInvertView))
			loaded.invertView = !vendorInvertView;
		readBool(document, "flipSubp", loaded.flipSubpixel);
		loaded.source = path.string();
		calibration = loaded;
		return true;
	}

	// Normalize display names for case-insensitive matching.
	std::string lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	// Find the requested display or an automatically recognized Looking Glass device.
	SDL_DisplayID findDisplay() {
		const char* requested = std::getenv("RENDEPTH_NATIVE_DISPLAY");
		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&count);
		SDL_DisplayID result = 0;
		for (int i = 0; displays != nullptr && i < count; ++i) {
			const char* displayName = SDL_GetDisplayName(displays[i]);
			const std::string name = displayName != nullptr ? displayName : "";
			const std::string normalized = lower(name);
			const bool explicitlyRequested = requested != nullptr &&
				name.find(requested) != std::string::npos;
			const bool lookingGlass = normalized.find("lkg") != std::string::npos ||
				normalized.find("looking glass") != std::string::npos;
			if (explicitlyRequested || (requested == nullptr && lookingGlass)) {
				result = displays[i];
				break;
			}
		}
		SDL_free(displays);
		return result;
	}

	// Load the test shader format supported by the active GPU backend.
	SDL_GPUShader* loadShader(SDL_GPUDevice* device, const char* filename,
		SDL_GPUShaderStage stage, Uint32 uniformBuffers) {
		const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
		SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
		const char* extension = nullptr;
		const char* entrypoint = nullptr;
		if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
			format = SDL_GPU_SHADERFORMAT_DXIL;
			extension = "dxil";
			entrypoint = "main";
		} else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
			format = SDL_GPU_SHADERFORMAT_SPIRV;
			extension = "spv";
			entrypoint = "main";
		} else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
			format = SDL_GPU_SHADERFORMAT_MSL;
			extension = "msl";
			entrypoint = "main0";
		} else {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No supported GPU shader format.");
			return nullptr;
		}

		const std::filesystem::path basePath = SDL_GetBasePath();
		const auto projectPath = basePath.parent_path().parent_path();
		const auto path = projectPath / "Shaders" / "Compiled" /
			(std::string(filename) + "." + extension);
		size_t codeSize = 0;
		void* code = SDL_LoadFile(path.string().c_str(), &codeSize);
		if (code == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s: %s",
				path.string().c_str(), SDL_GetError());
			return nullptr;
		}

		const SDL_GPUShaderCreateInfo createInfo{
			.code_size = codeSize,
			.code = static_cast<const Uint8*>(code),
			.entrypoint = entrypoint,
			.format = format,
			.stage = stage,
			.num_samplers = 0,
			.num_storage_textures = 0,
			.num_storage_buffers = 0,
			.num_uniform_buffers = uniformBuffers
		};
		SDL_GPUShader* shader = SDL_CreateGPUShader(device, &createInfo);
		SDL_free(code);
		if (shader == nullptr)
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create %s: %s",
				filename, SDL_GetError());
		return shader;
	}

	// Log the selected test pattern and phase origin for calibration comparisons.
	void logPattern(const App& app) {
		SDL_Log("QuiltTest pattern %d/%d: %s; phase origin %d",
			app.uniforms.pattern + 1, PatternCount,
			PatternNames[app.uniforms.pattern], app.uniforms.phaseOrigin);
	}

	// Convert optical calibration into shader phase increments and orientation flags.
	void updateCalibrationUniforms(App& app) {
		float phaseX = app.calibration.pitch / app.calibration.dpi;
		if (app.slantPitchCorrection)
			phaseX *= std::cos(std::atan(1.0f / app.calibration.slope));
		const float phaseY = phaseX / app.calibration.slope;
		app.uniforms.phaseScale[0] = phaseX;
		app.uniforms.phaseScale[1] = phaseY;
		app.uniforms.center = app.calibration.center;
		app.uniforms.subpixelPhase = phaseX / 3.0f;
		if (app.calibration.flipSubpixel)
			app.uniforms.subpixelPhase = -app.uniforms.subpixelPhase;
		app.uniforms.invertView = app.calibration.invertView ? 1 : 0;
		app.uniforms.expectedWidth = app.calibration.screenWidth;
		app.uniforms.expectedHeight = app.calibration.screenHeight;
	}

	// Create the graphics pipeline used to draw calibration patterns.
	bool createPipeline(App& app) {
		SDL_GPUShader* vertexShader = loadShader(app.device, "QuiltTest.vert",
			SDL_GPU_SHADERSTAGE_VERTEX, 0);
		SDL_GPUShader* fragmentShader = loadShader(app.device, "QuiltTest.frag",
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
		if (vertexShader == nullptr || fragmentShader == nullptr) {
			if (vertexShader != nullptr) SDL_ReleaseGPUShader(app.device, vertexShader);
			if (fragmentShader != nullptr) SDL_ReleaseGPUShader(app.device, fragmentShader);
			return false;
		}

		const SDL_GPUColorTargetDescription target{
			.format = SDL_GetGPUSwapchainTextureFormat(app.device, app.window)
		};
		const SDL_GPUGraphicsPipelineCreateInfo createInfo{
			.vertex_shader = vertexShader,
			.fragment_shader = fragmentShader,
			.vertex_input_state = {},
			.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
			.target_info = {
				.color_target_descriptions = &target,
				.num_color_targets = 1
			}
		};
		app.pipeline = SDL_CreateGPUGraphicsPipeline(app.device, &createInfo);
		SDL_ReleaseGPUShader(app.device, vertexShader);
		SDL_ReleaseGPUShader(app.device, fragmentShader);
		if (app.pipeline == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create pipeline: %s",
				SDL_GetError());
			return false;
		}
		return true;
	}

	// Draw the selected calibration pattern into the display swapchain.
	bool render(App& app) {
		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(app.device);
		if (commandBuffer == nullptr) return false;
		SDL_GPUTexture* swapchainTexture = nullptr;
		Uint32 swapchainWidth = 0;
		Uint32 swapchainHeight = 0;
		if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, app.window,
				&swapchainTexture, &swapchainWidth, &swapchainHeight)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Swapchain acquire failed: %s",
				SDL_GetError());
			SDL_CancelGPUCommandBuffer(commandBuffer);
			return false;
		}
		if (swapchainTexture == nullptr) {
			return SDL_SubmitGPUCommandBuffer(commandBuffer);
		}

		int pixelWidth = 0;
		int pixelHeight = 0;
		SDL_GetWindowSizeInPixels(app.window, &pixelWidth, &pixelHeight);
		app.uniforms.outputSize[0] = static_cast<float>(swapchainWidth);
		app.uniforms.outputSize[1] = static_cast<float>(swapchainHeight);
		app.dimensionsMatch = swapchainWidth == static_cast<Uint32>(app.calibration.screenWidth) &&
			swapchainHeight == static_cast<Uint32>(app.calibration.screenHeight) &&
			pixelWidth == app.calibration.screenWidth && pixelHeight == app.calibration.screenHeight;
		if (pixelWidth != app.lastPixelWidth || pixelHeight != app.lastPixelHeight ||
			swapchainWidth != app.lastSwapchainWidth || swapchainHeight != app.lastSwapchainHeight) {
			app.lastPixelWidth = pixelWidth;
			app.lastPixelHeight = pixelHeight;
			app.lastSwapchainWidth = swapchainWidth;
			app.lastSwapchainHeight = swapchainHeight;
			int logicalWidth = 0;
			int logicalHeight = 0;
			SDL_GetWindowSize(app.window, &logicalWidth, &logicalHeight);
			SDL_Log("Display sizes: logical=%dx%d pixels=%dx%d swapchain=%ux%u "
				"density=%.4f scale=%.4f expected=%dx%d [%s]",
				logicalWidth, logicalHeight, pixelWidth, pixelHeight,
				swapchainWidth, swapchainHeight,
				SDL_GetWindowPixelDensity(app.window),
				SDL_GetWindowDisplayScale(app.window),
				app.calibration.screenWidth, app.calibration.screenHeight,
				app.dimensionsMatch ? "PASS" : "FAIL");
		}

		const SDL_GPUColorTargetInfo target{
			.texture = swapchainTexture,
			.clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
			.load_op = SDL_GPU_LOADOP_CLEAR,
			.store_op = SDL_GPU_STOREOP_STORE
		};
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);
		if (pass == nullptr) return false;
		SDL_BindGPUGraphicsPipeline(pass, app.pipeline);
		SDL_PushGPUFragmentUniformData(commandBuffer, 0, &app.uniforms,
			sizeof(app.uniforms));
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		SDL_EndGPURenderPass(pass);
		return SDL_SubmitGPUCommandBuffer(commandBuffer);
	}
}

// Initialize the test display, calibration, GPU pipeline, and interactive controls.
SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
	SDL_SetAppMetadata("Rendepth QuiltTest", "1.0", "com.outmode.rendepth.quilttest");
	SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0");
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL initialization failed: %s",
			SDL_GetError());
		return SDL_APP_FAILURE;
	}

	auto* app = new App();
	*appstate = app;
	loadCalibration(app->calibration);
	app->display = findDisplay();
	if (app->display == 0) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"No Looking Glass display found; skipping QuiltTest. Set RENDEPTH_NATIVE_DISPLAY to override detection.");
		return SDL_APP_SUCCESS;
	}

	const SDL_Rect bounds = [&] {
		SDL_Rect value{};
		SDL_GetDisplayBounds(app->display, &value);
		return value;
	}();
	app->window = SDL_CreateWindow("QuiltTest", bounds.w, bounds.h,
		SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (app->window == nullptr) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_SetWindowPosition(app->window, SDL_WINDOWPOS_CENTERED_DISPLAY(app->display),
		SDL_WINDOWPOS_CENTERED_DISPLAY(app->display));
	SDL_SetWindowFullscreenMode(app->window, nullptr);
	if (!SDL_SetWindowFullscreen(app->window, true))
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen request failed: %s", SDL_GetError());

	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_SPIRV;
#if defined(_WIN32)
	format = SDL_GPU_SHADERFORMAT_DXIL;
#elif defined(__APPLE__)
	format = SDL_GPU_SHADERFORMAT_MSL;
#endif
	app->device = SDL_CreateGPUDevice(format, true, nullptr);
	if (app->device == nullptr || !SDL_ClaimWindowForGPUDevice(app->device, app->window)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPU/window setup failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_SetGPUAllowedFramesInFlight(app->device, 1);
	if (!createPipeline(*app)) return SDL_APP_FAILURE;

	updateCalibrationUniforms(*app);
	const char* displayName = SDL_GetDisplayName(app->display);
	SDL_Log("QuiltTest display: %s; calibration: %s",
		displayName != nullptr ? displayName : "unknown", app->calibration.source.c_str());
	SDL_Log("Reference phase: X=%.8f Y=%.8f center=%.8f subpixel=%.8f "
		"(pitch=%.8f slope=%.8f DPI=%.2f)",
		app->uniforms.phaseScale[0], app->uniforms.phaseScale[1],
		app->uniforms.center, app->uniforms.subpixelPhase,
		app->calibration.pitch, app->calibration.slope, app->calibration.dpi);
	SDL_Log("Controls: 1-9/0 select patterns 1-10, Left/Right or Space cycle, "
		"O cycles phase origin, I reverses views, P toggles slant pitch correction, Escape exits.");
	logPattern(*app);
	return SDL_APP_CONTINUE;
}

// Handle keyboard changes to patterns, phase origin, view ordering, and pitch correction.
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	auto& app = *static_cast<App*>(appstate);
	if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
	if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) return SDL_APP_CONTINUE;
	if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
	if (event->key.key >= SDLK_1 && event->key.key <= SDLK_9) {
		app.uniforms.pattern = static_cast<int>(event->key.key - SDLK_1);
		logPattern(app);
	} else if (event->key.key == SDLK_0) {
		app.uniforms.pattern = 9;
		logPattern(app);
	} else if (event->key.key == SDLK_RIGHT || event->key.key == SDLK_SPACE) {
		app.uniforms.pattern = (app.uniforms.pattern + 1) % PatternCount;
		logPattern(app);
	} else if (event->key.key == SDLK_LEFT) {
		app.uniforms.pattern = (app.uniforms.pattern + PatternCount - 1) % PatternCount;
		logPattern(app);
	} else if (event->key.key == SDLK_O) {
		app.uniforms.phaseOrigin = (app.uniforms.phaseOrigin + 1) % 4;
		logPattern(app);
	} else if (event->key.key == SDLK_I) {
		app.uniforms.invertView = app.uniforms.invertView == 0 ? 1 : 0;
		SDL_Log("QuiltTest view order: %s",
			app.uniforms.invertView != 0 ? "inverted" : "forward");
	} else if (event->key.key == SDLK_P) {
		app.slantPitchCorrection = !app.slantPitchCorrection;
		updateCalibrationUniforms(app);
		SDL_Log("QuiltTest pitch model: %s; phase X=%.8f Y=%.8f",
			app.slantPitchCorrection ? "slant-corrected" : "raw pitch / DPI",
			app.uniforms.phaseScale[0], app.uniforms.phaseScale[1]);
	}
	return SDL_APP_CONTINUE;
}

// Render the next calibration-test frame and propagate rendering failures.
SDL_AppResult SDL_AppIterate(void* appstate) {
	auto& app = *static_cast<App*>(appstate);
	return render(app) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

// Wait for GPU work and release the test pipeline, display window, and application state.
void SDL_AppQuit(void* appstate, SDL_AppResult) {
	auto* app = static_cast<App*>(appstate);
	if (app == nullptr) return;
	if (app->device != nullptr) SDL_WaitForGPUIdle(app->device);
	if (app->pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(app->device, app->pipeline);
	if (app->window != nullptr && app->device != nullptr)
		SDL_ReleaseWindowFromGPUDevice(app->device, app->window);
	if (app->device != nullptr) SDL_DestroyGPUDevice(app->device);
	if (app->window != nullptr) SDL_DestroyWindow(app->window);
	delete app;
}
