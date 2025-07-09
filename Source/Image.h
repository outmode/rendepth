// Copyright (c) 2025 Outmode
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
	inline static SDL_GPUGraphicsPipeline* iconPipeline = nullptr;
	inline static SDL_GPUGraphicsPipeline* spritePipeline = nullptr;
	inline static SDL_GPUBuffer* sharedVertexBuffer = nullptr;
	inline static SDL_GPUBuffer* sharedIndexBuffer = nullptr;
	inline static SDL_GPUTexture* imageTexture = nullptr;
	inline static SDL_GPUTexture* blurTexture = nullptr;
	inline static SDL_GPUTexture* blitTexture = nullptr;
	inline static SDL_GPUTexture* iconTexture = nullptr;
	inline static SDL_GPUTexture* helpTexture = nullptr;
	inline static SDL_GPUTexture* infoTexture = nullptr;
	inline static SDL_GPUTexture* menuTexture = nullptr;
	inline static SDL_GPUTexture* sliderTexture = nullptr;
	inline static SDL_GPUTexture* exportTexture = nullptr;
	inline static SDL_GPUSampler* imageSampler = nullptr;
	inline static SDL_Surface* menuTextSurface = nullptr;
	inline static SDL_Surface* ssimSurface = nullptr;
	inline static TTF_Font* helpFont = nullptr;
	inline static TTF_Font* infoFont = nullptr;
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
		int padding;
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
	inline static IconDataVert iconDataVert{};
	inline static IconDataFrag iconDataFrag{};
	inline static SpriteDataVert spriteDataVert{};
	inline static SpriteDataFrag spriteDataFrag{};
	inline static glm::vec2 imageSize;
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
	inline static bool useBackgroundSolid = false;
	inline static glm::vec2 helpTextSize;
	inline static glm::vec2 infoTextSize;
	inline static glm::vec2 menuTextureSize { 1024.0, 1024.0 };
	inline static glm::vec2 menuTextureOffset { 2.0, 2.0 };
	inline static bool displayHelp = false;
	inline static bool displayTip = false;
	inline static bool displayInfo = false;
	inline static float infoCurrentVisibility = 0.0;
	inline static float infoTargetVisibility = 0.0;
	inline static std::unordered_map<std::string, OptionsTexture> optionTextures;
	inline static std::vector<std::string> optionsLabels{};
	inline static const int maxImageSize = 16384;
	inline static auto gridSize = 8;
	inline static auto similarityThreshold = 0.67;

	static int init(Context* context, FileInfo& imageInfo);
	static int load(Context* context, FileInfo& imageInfo, SDL_Surface* imageData);
	static int draw(Context* context);
	static void quit(Context* context);
	static void bindPipeline(SDL_GPURenderPass* renderPass, SDL_GPUGraphicsPipeline* pipeline);
	static void drawImage(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static void drawIcon(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static void drawSprite(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass);
	static int uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
			const std::string& textureName);
	static void blitBlurTexture(Context* context, SDL_GPUTexture *inputTexture, Uint32 imageWidth, Uint32 imageHeight);
	static glm::ivec2 blitSSIMTexture(SDL_Surface* inputSurface, int imageWidth, int imageHeight);
	static int renderStereoImage(Context* context, StereoFormat stereoFormat);
	static SDL_Surface* getExportTexture(Context* context, StereoFormat stereoFormat);
	static glm::vec2 getIconCoordinates(IconType iconType);
	static glm::vec2 updateRatio(Context* context, glm::vec2 windowSize);
	static void updateSize(Context* context);
	static glm::vec4 getBackgroundColor(SDL_Surface* surface, int size, int width, int height);
	static double getSimilarity(SDL_Surface* surface, int width, int height);
	static glm::vec3 getColor(SDL_Surface* surface, int x, int y);
	static int initFonts(Context* context);
	static void initMenuTexture();
	static void createMenuAssets(Context* context);
	static void addToMenuText(Context* context, const std::string& text);
	static void saveMenuLayout(Context* context);
	static glm::mat4 getTransform(glm::vec3 position, glm::vec3 size,
		glm::vec3 aspect = glm::vec3(1.0));
	static void setSpriteUniforms(glm::vec3 position, glm::vec3 size,
		glm::vec4 color = { 1.0, 1.0, 1.0, 1.0 }, float visibility = 1.0, int useTexture = 1,
		glm::vec2 uvOffset = { 0.0, 0.0 }, glm::vec2 uvSize = { 1.0, 1.0 },
		glm::vec2 slice = { 0.5, 0.5 }, glm::vec3 aspect = { 1.0, 1.0, 1.0 });
	inline static glm::vec3 menuMargin{ 0 };
	inline static SDL_DisplayID currentDisplay = 0;
	inline static float mouseScale = 1.0;
	inline static bool useBorderlessWindow = true;
	inline static Style style;
};

#endif