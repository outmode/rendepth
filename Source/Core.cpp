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

#include "Core.h"
#include "SDL3_image/SDL_image.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <regex>

namespace {
	int readExifOrientation(const std::string& path) {
		FILE* file = fopen(path.c_str(), "rb");
		if (file == nullptr) return 1;
		uint8_t header[65536];
		size_t bytesRead = fread(header, 1, sizeof(header), file);
		fclose(file);
		if (bytesRead < 12) return 1;

		// Check JPEG: 0xFF, 0xD8
		if (header[0] == 0xFF && header[1] == 0xD8) {
			size_t offset = 2;
			while (offset + 4 <= bytesRead) {
				if (header[offset] != 0xFF) break;
				uint8_t marker = header[offset + 1];
				if (marker == 0xD9 || marker == 0xDA) break;
				uint16_t length = (static_cast<uint16_t>(header[offset + 2]) << 8) | header[offset + 3];
				if (length < 2 || offset + 2 + length > bytesRead) break;
				if (marker == 0xE1 && length >= 14) {
					const uint8_t* exif = header + offset + 4;
					if (std::memcmp(exif, "Exif\0\0", 6) == 0) {
						const uint8_t* tiff = exif + 6;
						size_t tiffLen = length - 8;
						if (tiffLen >= 8) {
							bool littleEndian = (tiff[0] == 'I' && tiff[1] == 'I');
							bool bigEndian = (tiff[0] == 'M' && tiff[1] == 'M');
							if (!littleEndian && !bigEndian) break;
							auto read16 = [littleEndian](const uint8_t* p) -> uint16_t {
								return littleEndian ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
							};
							auto read32 = [littleEndian](const uint8_t* p) -> uint32_t {
								return littleEndian
									? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
									   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
									: ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
									   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
							};
							if (read16(tiff + 2) == 0x002A) {
								uint32_t ifdOffset = read32(tiff + 4);
								if (ifdOffset + 2 <= tiffLen) {
									uint16_t numEntries = read16(tiff + ifdOffset);
									size_t entryOffset = ifdOffset + 2;
									for (uint16_t i = 0; i < numEntries && entryOffset + 12 <= tiffLen; ++i, entryOffset += 12) {
										uint16_t tag = read16(tiff + entryOffset);
										if (tag == 0x0112) {
											uint16_t val = read16(tiff + entryOffset + 8);
											if (val >= 1 && val <= 8) return val;
										}
									}
								}
							}
						}
					}
				}
				offset += 2 + length;
			}
		}
		return 1;
	}

	bool parseQuiltTag(const std::string& file, glm::vec3* grid) {
		static const std::regex quiltPattern(
			R"((?:_|\(|\s)qs([0-9]+)x([0-9]+)a([0-9]+(?:\.[0-9]+)?))",
			std::regex::icase);
		std::smatch match;
		if (!std::regex_search(file, match, quiltPattern)) return false;
		if (grid != nullptr) {
			try {
				*grid = {
					std::stof(match[1].str()),
					std::stof(match[2].str()),
					std::stof(match[3].str())
				};
			} catch (const std::exception&) {
				return false;
			}
		}
		return true;
	}
}

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

	#ifdef _WIN32
		if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
			entryPoint = "main";
			shaderExt = "dxil";
		} else if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
			entryPoint = "main";
			shaderExt = "spv";
		}
	#elif defined(__APPLE__)
		if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_MSL;
			entryPoint = "main0";
			shaderExt = "msl";
		} else if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
			entryPoint = "main";
			shaderExt = "spv";
		}
	#else
		if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
			entryPoint = "main";
			shaderExt = "spv";
		} else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_MSL;
			entryPoint = "main0";
			shaderExt = "msl";
		} else if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
			gpuShaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
			entryPoint = "main";
			shaderExt = "dxil";
		}
	#endif
	if (gpuShaderFormat == SDL_GPU_SHADERFORMAT_INVALID) {
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

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string& filepath) {
	if (surface == nullptr) return nullptr;

	float rotation = 0.0f;
	SDL_FlipMode flip = SDL_FLIP_NONE;

	SDL_PropertiesID props = SDL_GetSurfaceProperties(surface);
	if (props != 0) {
		rotation = SDL_GetFloatProperty(props, SDL_PROP_SURFACE_ROTATION_FLOAT, 0.0f);
	}

	if (rotation == 0.0f && flip == SDL_FLIP_NONE && !filepath.empty()) {
		int orientation = readExifOrientation(filepath);
		switch (orientation) {
		case 2: flip = SDL_FLIP_HORIZONTAL; break;
		case 3: rotation = 180.0f; break;
		case 4: flip = SDL_FLIP_VERTICAL; break;
		case 5: flip = SDL_FLIP_HORIZONTAL; rotation = 270.0f; break;
		case 6: rotation = 90.0f; break;
		case 7: flip = SDL_FLIP_HORIZONTAL; rotation = 90.0f; break;
		case 8: rotation = 270.0f; break;
		default: break;
		}
	}

	if (flip != SDL_FLIP_NONE) {
		SDL_FlipSurface(surface, flip);
	}
	if (rotation != 0.0f) {
		SDL_Surface* rotated = SDL_RotateSurface(surface, rotation);
		if (rotated != nullptr) {
			SDL_DestroySurface(surface);
			surface = rotated;
		}
		props = SDL_GetSurfaceProperties(surface);
		if (props != 0) SDL_ClearProperty(props, SDL_PROP_SURFACE_ROTATION_FLOAT);
	}

	return surface;
}

SDL_Surface* Core::loadImageDirect(const std::string& imageFilename) {
	SDL_Surface* surface = IMG_Load(imageFilename.c_str());
	if (surface == nullptr) return nullptr;

	surface = orientSurface(surface, imageFilename);
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
	SDL_DestroySurface(data->surface);
	data->surface = Core::loadImageDirect(data->path);
	data->done.store(true, std::memory_order_release);
	return data->fileIndex;
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
	auto nameMaxLen = 28;
	auto displayName = imageInfo.name;
	if (displayName.length() > nameMaxLen) {
		displayName = displayName.substr(0, nameMaxLen - 3) + "...";
	}
	return displayName + " [" + std::to_string((int)imageSize.x) + "x" +
		std::to_string((int)imageSize.y) + "] " + imageInfo.size;
}

StereoFormat Core::getImageType(const std::string& file) {
	if (parseQuiltTag(file, nullptr)) return Light_Field_LKG;
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
	parseQuiltTag(file, &result);
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
