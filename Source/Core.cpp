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

#include "Core.h"
#include "Image.h"
#include "SDL3_image/SDL_image.h"
#include <thread>
#include <iostream>

void Core::quit(Context* context) {
	SDL_ReleaseWindowFromGPUDevice(context->device, context->window);
	SDL_DestroyWindow(context->window);
	SDL_DestroyGPUDevice(context->device);
}

SDL_GPUShader* Core::loadShader(SDL_GPUDevice* device, const std::string& shaderFilename, Uint32 samplerCount,
	Uint32 uniformBufferCount, Uint32 storageBufferCount, Uint32 storageTextureCount) {
	SDL_GPUShaderStage stage;
	if (shaderFilename.find(".vert") != std::string::npos) {
		stage = SDL_GPU_SHADERSTAGE_VERTEX;
	} else if (shaderFilename.find(".frag") != std::string::npos) {
		stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	} else {
		SDL_Log("Invalid Graphics Shader.");
		return nullptr;
	}

	SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
	gpuShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
	std::string entryPoint;
	std::string shaderExt;

	if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
		gpuShaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
		entryPoint = "main";
		shaderExt = "dxil";
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
		gpuShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
		entryPoint = "main";
		shaderExt = "spv";
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
		gpuShaderFormat = SDL_GPU_SHADERFORMAT_MSL;
		entryPoint = "main0";
		shaderExt = "msl";
	} else {
		SDL_Log("Invalid Graphics Shader.");
		return nullptr;
	}

	std::filesystem::path exePath = SDL_GetBasePath();
	std::filesystem::path appPath = exePath.parent_path().parent_path();
	std::filesystem::path shaderPath = appPath / "Shaders" / "Compiled";
	auto shaderFilePath = shaderPath / (shaderFilename + "." + shaderExt);
	auto shaderFileString = shaderFilePath.string();

	size_t codeSize;
	void* code = SDL_LoadFile(shaderFileString.c_str(), &codeSize);
	if (code == nullptr) {
		SDL_Log("Failed To Load Shader: %s", shaderFileString.c_str());
		return nullptr;
	}

	SDL_GPUShaderCreateInfo shaderInfo = {
		.code_size = codeSize,
		.code = (Uint8*)code,
		.entrypoint = entryPoint.c_str(),
		.format = gpuShaderFormat,
		.stage = stage,
		.num_samplers = samplerCount,
		.num_storage_textures = storageTextureCount,
		.num_storage_buffers = storageBufferCount,
		.num_uniform_buffers = uniformBufferCount
	};

	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
	if (shader == nullptr) {
		SDL_Log("Failed To Create Shader.");
		SDL_free(code);
		return nullptr;
	}

	SDL_free(code);
	return shader;
}

int Core::uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
		const std::string& textureName) {
	auto mipLevels = (Uint32)std::floor(log2(std::max(imageData->w, imageData->h))) + 1;
	auto gpuFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	auto bytesPerPixel = 4;
	SDL_GPUTextureCreateInfo textureCreateInfo = {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = gpuFormat,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = (Uint32)imageData->w,
		.height = (Uint32)imageData->h,
		.layer_count_or_depth = 1,
		.num_levels = mipLevels
	};
	if (*gpuTexture != nullptr) SDL_ReleaseGPUTexture(context->device, *gpuTexture);
	*gpuTexture = SDL_CreateGPUTexture(context->device, &textureCreateInfo);

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

	auto textureTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(
		context->device,
		textureTransferBuffer,
		false
	);
	SDL_memcpy(textureTransferPtr, imageData->pixels, imageData->w * imageData->h * bytesPerPixel);
	SDL_UnmapGPUTransferBuffer(context->device, textureTransferBuffer);

	SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(context->device);
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);

	SDL_GPUTextureTransferInfo textureTransferInfo = {
		.transfer_buffer = textureTransferBuffer,
		.offset = 0,
	};
	SDL_GPUTextureRegion textureRegion = {
		.texture = *gpuTexture,
		.w = (Uint32)imageData->w,
		.h = (Uint32)imageData->h,
		.d = 1
	};
	SDL_UploadToGPUTexture(
		copyPass, &textureTransferInfo,
		&textureRegion, false
	);

	SDL_EndGPUCopyPass(copyPass);
	SDL_GenerateMipmapsForGPUTexture(uploadCmdBuf, *gpuTexture);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);
	SDL_ReleaseGPUTransferBuffer(context->device, textureTransferBuffer);

	return 0;
}

SDL_Surface* Core::loadImageDirect(const std::string& imageFilename) {
	SDL_Surface* surface = IMG_Load(imageFilename.c_str());
	if (surface == nullptr) return nullptr;

	SDL_PixelFormat format= SDL_PIXELFORMAT_ABGR8888;
	if (surface->format != format) {
		SDL_Surface* next = SDL_ConvertSurface(surface, format);
		SDL_DestroySurface(surface);
		surface = next;
	}

	return surface;
}

int Core::loadImageThread(void* ptr) {
	auto data = static_cast<AsyncData*>(ptr);
	SDL_DestroySurface((*data).surface);
	(*data).surface = Core::loadImageDirect((*data).path);
	return (*data).fileIndex;
}

SDL_Thread* Core::loadImageAsync(AsyncData& asyncData) {
	SDL_Thread* thread = SDL_CreateThread(Core::loadImageThread, "LoadImageThread", &asyncData);
	return thread;
}

glm::vec2 Core::getTextSize(TTF_Font* font, const std::string& text) {
	int width = 0, height = 0;
	TTF_GetStringSize(font, text.c_str(), strlen(text.c_str()), &width, &height);
	return {width, width };
}

std::string Core::getFileText(const FileInfo& imageInfo, glm::vec2 imageSize) {
	auto nameMaxLen = 22;
	auto displayName = imageInfo.name;
	if (displayName.length() > nameMaxLen) {
		displayName = displayName.substr(0, nameMaxLen - 3) + "...";
	}
	return displayName + " [" + std::to_string((int)imageSize.x) + "x" +
		std::to_string((int)imageSize.y) + "] " + imageInfo.size;
}

StereoFormat Core::getImageType(const std::string& file) {
	StereoFormat result = Unknown_Format;
	for (const auto& tag : tagType) {
		if (file.find(tag.first) != std::string::npos) {
			result = tag.second;
			break;
		}
	}
	return result;
}

glm::vec3 Core::getGridInfo(const std::string& file) {
	auto result = glm::vec3(1, 1, 1);
	std::size_t gridTag = file.find("_qs");
	if (gridTag != std::string::npos) {
		std::size_t fileLen = file.length();
		size_t gridStart = gridTag + 3;
		std::string gridInfo = file.substr(gridStart, fileLen);
		std::size_t gridLen = gridInfo.length();
		std::size_t gridX = gridInfo.find('x');
		std::size_t gridA = gridInfo.find('a');
		auto gridCol = gridInfo.substr(0, gridX);
		auto gridRow = gridInfo.substr(gridX + 1, gridA - gridX - 1);
		auto gridAspect = gridInfo.substr(gridA + 1, gridLen - gridA);
		result = { stof(gridCol), stof(gridRow), stof(gridAspect) };
	}
	return result;
}

void Core::drawText(Context* context, const std::string& text, TTF_Font* font,
		SDL_GPUTexture*& texture, glm::vec2& size, const std::string& name) {
	auto shownText = text;
	lastDrawnText = text;
	if (shownText.empty()) shownText = "   ";
	static auto fontWidth = 17.77f;
	auto winSize = context->windowSize;
	winSize = glm::max(winSize, glm::vec2(512.0));
	auto textSize = getTextSize(font, shownText);
	auto stringLength = strlen(shownText.c_str());
	if ((int)textSize.x > (int)winSize.x) {
		auto maxChars = (int)(winSize.x / fontWidth);
		stringLength = maxChars;
	}
	SDL_Color textColor = { 255, 255, 255, 255 };
	auto helpData = TTF_RenderText_Blended(font, shownText.c_str(),
		stringLength, textColor);
	if (helpData == nullptr) {
		SDL_Log("Could Not Render Font: %s", name.c_str());
		return;
	}
	size = glm::vec2(helpData->w, helpData->h);
	auto rgbaHelpData = SDL_ConvertSurface(helpData, SDL_PIXELFORMAT_ABGR8888);
	if (rgbaHelpData == nullptr) {
		SDL_Log("Could Not Create Texture Surface: %s", name.c_str());
		SDL_DestroySurface(helpData);
		return;
	}
	uploadTexture(context, rgbaHelpData, &texture, name);
	SDL_DestroySurface(helpData);
	SDL_DestroySurface(rgbaHelpData);
}

std::filesystem::path Core::getHomeDirectory() {
#ifdef _WIN32
	const char* homeDir = std::getenv("USERPROFILE");
#else
	const char* homeDir = std::getenv("HOME");
#endif
	if (homeDir) {
		return { homeDir };
	}
	return {};
}