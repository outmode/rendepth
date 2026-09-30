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

#ifndef RENDEPTH_IMAGE_H
#define RENDEPTH_IMAGE_H

#include "Core.h"
#include "VideoPlayer.h"
#include "Utils.h"
#include "Style.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtc/matrix_transform.hpp"
#include "SDL3_ttf/SDL_ttf.h"
#include "SDL3_shadercross/SDL_shadercross.h"
#include <filesystem>

class Image {
public:
	Image() = default;
	~Image() = default;

	inline static SDL_GPUGraphicsPipeline* imagePipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* lanczosPipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* depthRefinePipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* depthRefineR16Pipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* videoYUVPipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* iconPipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* spritePipeline = nullptr;
	inline static SDL_GPUBuffer* sharedVertexBuffer = nullptr;
	inline static SDL_GPUBuffer* sharedIndexBuffer = nullptr;
	inline static SDL_GPUTexture* imageTexture = nullptr;
	inline static SDL_GPUTexture* videoYTexture = nullptr;
	inline static SDL_GPUTexture* videoUTexture = nullptr;
	inline static SDL_GPUTexture* videoVTexture = nullptr;
	inline static SDL_GPUTexture* videoDepthTexture = nullptr;
	inline static SDL_GPUTexture* blurTexture = nullptr;
	inline static SDL_GPUTexture* blurTextureNext = nullptr;
	inline static SDL_GPUTexture* iconTexture = nullptr;
	inline static SDL_GPUTexture* helpTexture = nullptr;
	inline static SDL_GPUTexture* infoTexture = nullptr;
	inline static SDL_GPUTexture* subtitleTexture = nullptr;
	inline static SDL_GPUTexture* subtitleShadowTexture = nullptr;
	inline static bool subtitleBitmap = false;
	inline static bool subtitleBitmapStereoPacked = false;
	inline static glm::ivec2 subtitleBitmapPosition{};
	inline static glm::ivec2 subtitleBitmapCanvasSize{};
	inline static SDL_GPUTexture* menuTexture = nullptr;
	inline static SDL_GPUTexture* discMenuTexture = nullptr;
	inline static SDL_GPUTexture* discMenuDepthTexture = nullptr;
	inline static SDL_GPUTexture* discBackgroundTexture = nullptr;
	inline static SDL_GPUTexture* sliderTexture = nullptr;
	inline static SDL_GPUTexture* nativeCalibrationTexture = nullptr;
	inline static SDL_GPUTexture* exportTexture = nullptr;
	inline static glm::uvec2 exportTextureSize{0, 0};
	inline static SDL_GPUGraphicsPipeline* interlacerPipeline = nullptr;
	inline static SDL_GPUSampler* imageSampler = nullptr;
	inline static SDL_Surface* menuTextSurface = nullptr;
	inline static SDL_Surface* ssimSurface = nullptr;
	inline static TTF_Font* helpFont = nullptr;
	inline static TTF_Font* infoFont = nullptr;
	inline static TTF_Font* subtitleFont = nullptr;
	inline static TTF_Font* menuFont = nullptr;

	struct Vertex {
		glm::vec3 position;
		glm::vec2 uv;
	};

	inline static Vertex quadVertices[] = {
		{{ -0.5, 0.5, 0.0 }, {0.0, 0.0 }},
		{{ 0.5, 0.5, 0.0 }, {1.0, 0.0 }},
		{{ 0.5, -0.5, 0.0 }, {1.0, 1.0 }},
		{{ -0.5, -0.5, 0.0 }, {0.0, 1.0 }} };

	inline static uint16_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };

	struct ImageDataVert {
		glm::mat4 transform;
		glm::mat4 projection;
		glm::vec3 displayImageAspect;
		int fillScreen;
	};

	struct ImageDataFrag {
		glm::vec2 windowSize;
		glm::vec2 imageSize;
		glm::vec3 gridSize;
		float visibility;
		int blur;
		int mode;
		int type;
		float stereoStrength;
		float stereoDepth;
		float stereoOffset;
		float gridAngle;
		float depthEffect;
		int effectRandom;
		int swapLeftRight;
		int force;
		float blurMix;
		int separateDepth;
		int packedOutput;
		int depthPadding1;
		int depthPadding2;
	};

	struct VideoYUVDataFrag {
		int format;
		int colorSpace;
		int fullRange;
		int padding;
	};

	struct LanczosDataFrag {
		glm::vec2 sourceSize;
		glm::vec2 targetSize;
	};

	struct InterlacerDataFrag {
		glm::vec2 outputSize;
		glm::vec2 imageSize;
		glm::vec2 quiltSize;
		glm::vec2 tileSize;
		glm::vec2 phaseScale;
		float center;
		float subpixelPhase;
		int viewCount;
		int gridColumns;
		int gridRows;
		int output2D;
		int sourceFlat;
		int invertView;
		int flipImageX;
		int flipImageY;
		int separateDepth;
		int swapLeftRight;
		int sourceRgbd;
		float stereoStrength;
		float stereoDepth;
		float stereoOffset;
		int sourceType;
		int testPattern;
		int cubeViC1;
		int padding[3];
	};

	struct IconDataVert {
		glm::mat4 transform;
		glm::mat4 projection;
		glm::vec2 gridSize;
		glm::vec2 gridOffset;
	};

	struct IconDataFrag {
		glm::vec4 color;
		float visibility;
		float rotation;
		int animated;
		int force;
	};

	inline static ImageDataVert imageDataVert{};
	inline static ImageDataFrag imageDataFrag{};
	inline static InterlacerDataFrag interlacerDataFrag{};
	inline static IconDataVert iconDataVert{};
	inline static IconDataFrag iconDataFrag{};
	inline static SpriteDataVert spriteDataVert{};
	inline static SpriteDataFrag spriteDataFrag{};
	inline static glm::vec2 imageSize;
	inline static glm::ivec2 videoTextureSize{};
	inline static glm::ivec2 videoDepthTextureSize{};
	inline static glm::vec2 safeImageSize;
	inline static double safePercent = 0.8f;
	inline static glm::vec4 clearColorLight{ 0.99, 0.99, 0.99, 1.0f };
	inline static glm::vec4 clearColorDark{ 0.11, 0.11, 0.11, 1.0f };
	inline static glm::vec4 clearColorDepth{ 0.5, 0.5, 0.5, 1.0f };
	inline static glm::vec4 uiColorDepth{ 1.0, 1.0, 1.0, 1.0f };
	inline static glm::vec4 uiColorBGDepth{ 0.75, 0.75, 0.75, 1.0f };
	inline static glm::vec4 clearColorSolid = clearColorDark;
	inline static glm::vec4 clearColorCurrent = clearColorDark;
	inline static bool useBackgroundBlur = true;
	inline static bool displayAudioPlaceholder = false;
	inline static bool displayAudioWaveform = false;
	inline static AudioWaveform::Bars audioWaveform{};
	inline static bool useBackgroundSolid = false;
	inline static glm::vec2 helpTextSize;
	inline static glm::vec2 infoTextSize;
	inline static glm::vec2 nativeCalibrationTextSize;
	inline static glm::vec2 subtitleTextSize;
	inline static glm::vec2 menuTextureSize { 1024.0, 1024.0 };
	inline static glm::vec2 menuTextureOffset { 2.0, 2.0 };
	inline static float menuRowHeight = 0.0f;
	inline static bool displayHelp = false;
	inline static bool displayTip = false;
	inline static bool displayInfo = false;
	inline static float infoCurrentVisibility = 0.0;
	inline static float infoTargetVisibility = 0.0;
	inline static std::unordered_map<std::string, OptionsTexture> optionTextures;
	inline static std::vector<std::string> optionsLabels{};
	inline static const int maxImageSize = 16384;
	inline static const int maxConversionSize = 7680;
	inline static auto gridSize = 8;

	static int init(Context* context, FileInfo& imageInfo);
	static int reloadShader(Context* context);
	static int load(Context* context, FileInfo& imageInfo, SDL_Surface* imageData,
		StereoFormat forcedType = Unknown_Format);
	static int updateVideoFrame(Context* context, const VideoFrame& frame, bool firstFrame,
		int logicalWidth, int logicalHeight, bool updateBlur = true);
	static void updateVideoBackgroundAnimation();
	static void updateDiscBackground(Context* context, SDL_Surface* preview);
	static int updateVideoDepth(Context* context, const std::vector<std::uint16_t>& values,
		int width, int height);
	static void clearVideoDepth(Context* context);
	static void clearVideoFrame(Context* context);
	static void updateVideoSubtitle(Context* context,
		const std::shared_ptr<const VideoSubtitle>& subtitle);
	static int draw(Context* context);
	static int initNativeOutput(Context* context);
	static bool nativeOutputAvailable();
	static bool isNativeDisplayOnMainWindow();
	static void setNativeOutputActive(Context* context, bool active);
	static void configureFullscreenMode(SDL_Window* window, SDL_DisplayID displayID);
	static bool saveNativeDisplayConfig(const std::filesystem::path& path, const NativeDisplayConfig& config);
	static bool saveNativeDisplayConfig(const NativeDisplayConfig& config);
	static void resetNativeDisplayConfig(Context* context = nullptr);
	static void updateInterlacerUniforms(Context* context, int width, int height,
		NativeDisplayConfig& config = nativeDisplayConfig);
	static int drawNativeOutput(Context* context);
	static void drawNativeCalibrationWarning(Context* context, SDL_GPUCommandBuffer* commandBuffer,
		SDL_GPURenderPass* renderPass, int width, int height,
		const NativeDisplayConfig& config = nativeDisplayConfig);
	static int reloadInterlacerShader(Context* context);
	static void quit(Context* context);
	static void bindPipeline(SDL_GPURenderPass* renderPass, SDL_GPUGraphicsPipeline* pipeline);
	static void drawImage(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static void drawIcon(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static void drawSprite(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static int uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
		const std::string& textureName, bool reuseTexture = false,
		bool waitForGpu = true, bool generateMipmaps = true);
	static SDL_Surface* upscaleSurfaceGPU(Context* context, const SDL_Surface* source,
		int outputWidth, int outputHeight);
	static SDL_Surface* refineDepthSurfaceGPU(Context* context, const SDL_Surface* color,
		const SDL_Surface* depth, int outputWidth, int outputHeight);
	static SDL_GPUTexture* refineDepthTextureGPU(Context* context, SDL_GPUTexture* color,
		SDL_GPUTexture* depth, int width, int height, int scale = 1);
	static void blitBlurTexture(Context* context, SDL_GPUTexture* inputTexture,
		Uint32 imageWidth, Uint32 imageHeight, bool nextSnapshotOnly = false,
		bool waitForGpu = true);
	static int renderStereoImage(Context* context, StereoFormat stereoFormat,
		SDL_GPUTexture* sourceTexture = nullptr);
	static SDL_Surface* getExportTexture(Context* context);
	static glm::vec2 getIconCoordinates(IconType iconType);
	static glm::vec2 updateRatio(Context* context, glm::vec2 windowSize);
	static void updateSize(Context* context);
	static glm::vec4 getBackgroundColor(SDL_Surface* surface, int size, int width, int height);
	static glm::vec3 getColor(SDL_Surface* surface, int x, int y);
	static int initFonts(Context* context);
	static void initMenuTexture();
	static void createMenuAssets(Context* context);
	static void addToMenuText(Context* context, const std::string& text);
	static void saveMenuLayout(Context* context);
	static void scrollMenu(float pixels);
	static glm::mat4 getTransform(glm::vec3 position, glm::vec3 size,
		glm::vec3 aspect = glm::vec3(1.0));
	static void setSpriteUniforms(glm::vec3 position, glm::vec3 size,
		glm::vec4 color = { 1.0, 1.0, 1.0, 1.0 }, float visibility = 1.0, int useTexture = 1,
		glm::vec2 uvOffset = { 0.0, 0.0 }, glm::vec2 uvSize = { 1.0, 1.0 },
		glm::vec2 slice = { 0.5, 0.5 }, glm::vec3 aspect = { 1.0, 1.0, 1.0 });
	inline static glm::vec3 menuMargin{ 0 };
	inline static float menuTopInset = 128.0f;
	inline static float menuScroll = 0.0f;
	inline static float menuScrollLimit = 0.0f;
	inline static SDL_DisplayID currentDisplay = 0;
	inline static float mouseScale = 1.0;
	inline static bool useBorderlessWindow = true;
	inline static bool nativeOutputEnabled = false;
	inline static bool nativeOutputSourceReady = false;
	inline static bool nativeDisplayOnMainWindow = false;
	struct NativeOutput {
		SDL_DisplayID display = 0;
		SDL_Window* window = nullptr; // Owned unless this is the main application window.
		NativeDisplayConfig config;
		glm::ivec2 lastSize{0, 0};
	};
	inline static std::vector<NativeOutput> nativeOutputs;
	static int drawNativeOutput(Context* context, NativeOutput& output, NativeDisplayConfig& config);
	inline static SDL_DisplayID nativeDisplay = 0;
	inline static NativeDisplayConfig nativeDisplayConfig{};
	inline static int testPatternMode = 0;
	inline static Style style;
};

#endif
