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

#include "Image.h"
#include "Utils.h"
#include <iostream>

glm::vec2 Image::getIconCoordinates(IconType iconType) {
	auto iconIndex = static_cast<int>(iconType);
	auto iconX = iconIndex % gridSize;
	auto iconY = iconIndex / gridSize;
	return glm::vec2(iconX, iconY) / (float)gridSize;
}

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

void Image::updateSize(Context* context) {
	imageSize = glm::vec3(context->imageSize.x, context->imageSize.y, 0);
	auto visualSize = imageSize;
	if (context->mode == SBS_Full && context->fullscreen)
		visualSize.x *= 2.0;
	safeImageSize = Utils::getSafeSize(visualSize, context->virtualSize, (float)safePercent, true);
}

glm::vec3 Image::getColor(SDL_Surface* surface, int x, int y) {
	Uint8 red, green, blue, alpha;
	auto success = SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha);
	if (success) return { red, green, blue };
	return { 0, 0, 0 };
}

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

int Image::load(Context* context, FileInfo& imageInfo, SDL_Surface* imageData) {
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
				Core::drawText(context, "Could Not Load Image", helpFont, helpTexture,
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
	imageInfo.type = Core::getImageType(imageInfo.path);
	if (imageInfo.type == Unknown_Format) imageInfo.type = Core::defaultImportFormat;
	context->imageType = imageInfo.type;
	context->imageSize = glm::vec2((float)imageData->w, (float)imageData->h);

	if (context->imageType == Color_Plus_Depth || context->imageType == Side_By_Side_Full ||
		context->imageType == Side_By_Side_Swap) {
		context->imageSize.x /= 2;
	} else if (context->imageType == Light_Field_LKG) {
		auto gridSize = Core::getGridInfo(imageInfo.base);
		context->gridSize = gridSize;
		context->imageSize.x /= gridSize.x;
		context->imageSize.y /= gridSize.y;
	} else if (context->imageType == Light_Field_CV) {
		glm::vec2 imageRes = { imageData->w, imageData->h };
		imageRes /= glm::vec2(8.0, 5.0);
		auto gridSize = glm::vec3(8.0f, 5.0f, imageRes.x / imageRes.y);
		context->gridSize = gridSize;
		context->imageSize.x /= gridSize.x;
		context->imageSize.y /= gridSize.y;
	}
	context->infoText = Core::getFileText(imageInfo, context->imageSize);
	updateSize(context);

	SDL_SetWindowTitle(context->window, imageInfo.name.c_str());

	uploadTexture(context, imageData, &imageTexture, "Image Texture");
	blitBlurTexture(context, imageTexture, (Uint32)imageData->w, (Uint32)imageData->h);
	clearColorSolid = getBackgroundColor(imageData, 4, imageData->w, imageData->h);
	SDL_DestroySurface(imageData);

	return 0;
}

void Image::initMenuTexture() {
	if (menuTextSurface) SDL_DestroySurface(menuTextSurface);
	menuTextSurface = SDL_CreateSurface((int)menuTextureSize.x, (int)menuTextureSize.y,
		SDL_PIXELFORMAT_ABGR8888);
	menuTextureOffset = { 2, 2 };
}

void Image::createMenuAssets(Context* context) {
	for (const Choice& choice : *context->menuChoices) {
		addToMenuText(context, choice.label);
		for (const std::string& option : choice.options) {
			addToMenuText(context, option);
		}
	}
}

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

	uploadTexture(context, menuTextSurface, &menuTexture, "Menu Texture");
	SDL_DestroySurface(menuData);
	SDL_DestroySurface(rgbaMenuData);
}


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
	auto choiceStart = glm::vec3(choiceCentered, windowSize.y + 8.0f, 0.0f);
	auto choiceIndex = 0;

	auto firstChoice = 0.0f;
	for (auto& choice : *context->menuChoices) {
		if (choiceIndex > 0) choiceStart.y -= 24.0f;
		choiceIndex++;

		auto centerPadChoice = menuWidth - optionTextures[choice.label].size.x * context->displayScale;
		auto spritePosition = choiceStart + glm::vec3(centerPadChoice * 0.5f, 0.0f, 0.0f) +
				glm::vec3(optionTextures[choice.label].size.x * context->displayScale,
				-optionTextures[choice.label].size.y, 0.0) * glm::vec3(0.5, 0.5, 0.0);
		auto spriteSize = glm::vec3(optionTextures[choice.label].size, 1.0);
		if (choiceIndex > 0) firstChoice = optionTextures[choice.label].size.y;

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
	menuMargin.y = (firstChoice + buttonMargin * 2.0f + 2.0f) - (style.getIconRadius(Style::getCurrentScale()) * context->displayScale
		+ style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale()));
}


int Image::init(Context* context, FileInfo& imageInfo) {
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
		} else if (imageInfo.type == Light_Field_LKG) {
			auto gridSize = Core::getGridInfo(imageInfo.base);
			context->gridSize = gridSize;
			firstImageSize.x /= gridSize.x;
			firstImageSize.y /= gridSize.y;
		} else if (imageInfo.type == Light_Field_CV) {
			glm::vec2 imageRes = { imageData->w, imageData->h };
			imageRes /= glm::vec2(8.0, 5.0);
			auto gridSize = glm::vec3(8.0f, 5.0f, imageRes.x / imageRes.y);
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

	SDL_GPUShader* imageVertexShader = Core::loadShader(context->device,
		"Image.vert", 0, 1, 0, 0);
	if (imageVertexShader == nullptr) {
		SDL_Log("Failed To Create Image Vertex Shader.");
		return -1;
	}

	SDL_GPUShader* imageFragmentShader = Core::loadShader(context->device,
		"Image.frag", 2, 1, 0, 0);
	if (imageFragmentShader == nullptr) {
		SDL_Log("Failed To Create Image Fragment Shader.");
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
		.blend_state = (SDL_GPUColorTargetBlendState) {
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
		.vertex_input_state = (SDL_GPUVertexInputState){
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

	SDL_GPUColorTargetDescription iconTargetDescription[1] = {{
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.blend_state = (SDL_GPUColorTargetBlendState) {
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};

	SDL_GPUGraphicsPipelineCreateInfo iconPipelineCreateInfo = {
		.vertex_shader = iconVertexShader,
		.fragment_shader = iconFragmentShader,
		.vertex_input_state = (SDL_GPUVertexInputState){
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
		.blend_state = (SDL_GPUColorTargetBlendState) {
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.enable_blend = true
		}
	}};

	SDL_GPUGraphicsPipelineCreateInfo spritePipelineCreateInfo = {
		.vertex_shader = spriteVertexShader,
		.fragment_shader = spriteFragmentShader,
		.vertex_input_state = (SDL_GPUVertexInputState){
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
		.max_anisotropy = 4,
		.min_lod = -128.0,
		.max_lod = 128.0,
		.enable_anisotropy = true
	};
	imageSampler = SDL_CreateGPUSampler(context->device, &samplerCreateInfo);

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

	context->displayScale = SDL_GetWindowDisplayScale(context->window);
	auto pixelDensity = SDL_GetWindowPixelDensity(context->window);
	context->pixelDensity = pixelDensity;
#if defined(__APPLE__)
	mouseScale = pixelDensity;
#elif defined(__linux__) || defined(__unix__)
	mouseScale = pixelDensity;
#else
	mouseScale = 1.0;
#endif

	Style::calculateScale(virtualSize / context->displayScale);
	initFonts(context);
	initMenuTexture();
	createMenuAssets(context);
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

int Image::uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
		const std::string& textureName) {
	auto textureMipLevels = (Uint32) std::floor(log2(std::max(imageData->w, imageData->h))) + 1;
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

void Image::blitBlurTexture(Context* context, SDL_GPUTexture *inputTexture, Uint32 imageWidth, Uint32 imageHeight) {
	static Uint32 blitSize = 32;
	static Uint32 blurSize = 4;

	if (blitTexture == nullptr) {
		SDL_GPUTextureCreateInfo blitTextureCreateInfo = {
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
			.width = blitSize,
			.height = blitSize,
			.layer_count_or_depth = 1,
			.num_levels = 1
		};

		blitTexture = SDL_CreateGPUTexture(context->device, &blitTextureCreateInfo);

		SDL_SetGPUTextureName(
			context->device,
			blitTexture,
			"Blit Texture"
		);
	}

	if (blurTexture == nullptr) {
		SDL_GPUTextureCreateInfo blurTextureCreateInfo = {
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
			.width = blurSize,
			.height = blurSize,
			.layer_count_or_depth = 1,
			.num_levels = 1
		};

		blurTexture = SDL_CreateGPUTexture(context->device, &blurTextureCreateInfo);

		SDL_SetGPUTextureName(
			context->device,
			blurTexture,
			"BlurTexture"
		);
	}

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);

	SDL_GPUBlitInfo blitImageInfo = {
		.source = {
			.texture = inputTexture,
			.layer_or_depth_plane = 0,
			.w = context->imageType == Color_Plus_Depth ? imageWidth / 2 : imageWidth,
			.h = imageHeight },
		.destination = {
			.texture = blitTexture,
			.layer_or_depth_plane = 0,
			.w = blitSize,
			.h = blitSize },
		.load_op = SDL_GPU_LOADOP_LOAD,
		.filter = SDL_GPU_FILTER_LINEAR
	};
	SDL_BlitGPUTexture(commandBuffer, &blitImageInfo);

	SDL_GPUBlitInfo blitBlurInfo = {
		.source = {
			.texture = blitTexture,
			.layer_or_depth_plane = 0,
			.w = blitSize,
			.h = blitSize },
		.destination = {
			.texture = blurTexture,
			.layer_or_depth_plane = 0,
			.w = blurSize,
			.h = blurSize },
		.load_op = SDL_GPU_LOADOP_LOAD,
		.filter = SDL_GPU_FILTER_LINEAR
	};
	SDL_BlitGPUTexture(commandBuffer, &blitBlurInfo);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);
}

static SDL_GPUViewport getViewport(int view, int views, glm::vec2 rect) {
	SDL_GPUViewport result{ 0, 0, rect.x, rect.y, 0, 0 };
	auto viewWidth = rect.x / (float)views;
	result.x = (float)floor(viewWidth * double(view));
	result.w = ceil(viewWidth);
	return result;
}

static SDL_GPUViewport getViewportGrid(glm::vec2 view, glm::vec2 rect) {
	SDL_GPUViewport result{ rect.x * view.x, rect.y * view.y, rect.x, rect.y, 0, 0 };
	return result;
}

int Image::renderStereoImage(Context* context, StereoFormat stereoFormat) {
	if (displayHelp) return -1;
	if (context->imageType == Color_Only || context->imageType == Color_Anaglyph ||
		(context->imageType != Color_Plus_Depth &&
		(stereoFormat == Color_Only || stereoFormat == Color_Plus_Depth ||
			stereoFormat == Light_Field_LKG || stereoFormat == Light_Field_CV))) return -1;

	auto renderFormat = Left;
	auto singleImageSize = context->imageSize;
	auto viewportSize = singleImageSize;
	auto viewsX = 1, viewsY = 1;
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
		renderFormat = Anaglyph;
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
		viewsX = (int)exportQuiltDimLKG.x;
		viewsY = (int)exportQuiltDimLKG.y;
		startY = viewsY - 1;
		stepY = -1;
		renderFormat = Left;
		auto maxRes = exportQuiltMaxResLKG;
		auto maxSize = std::max(singleImageSize.x, singleImageSize.y);
		singleImageSize *= maxRes / maxSize;
		singleImageSize.x = roundf(singleImageSize.x);
		singleImageSize.y = roundf(singleImageSize.y);
		viewportSize = singleImageSize * exportQuiltDimLKG;
		stereoStrength *= gridBoost;
		stereoOffset *= gridBoost;
		strengthStep = -stereoStrength * 2.0f / (float(viewsX * viewsY - 1));
		offsetStep = -stereoOffset * 2.0f / (float(viewsX * viewsY - 1));
	} else if (stereoFormat == Light_Field_CV) {
		viewsX = (int)exportQuiltDimCV.x;
		viewsY = (int)exportQuiltDimCV.y;
		renderFormat = Left;
		auto maxRes = exportQuiltMaxResCV;
		auto maxSize = std::max(singleImageSize.x, singleImageSize.y);
		singleImageSize *= maxRes / maxSize;
		singleImageSize.x = roundf(singleImageSize.x);
		singleImageSize.y = roundf(singleImageSize.y);
		viewportSize = singleImageSize * exportQuiltDimCV;
		stereoStrength *= gridBoost;
		stereoOffset *= gridBoost;
		strengthStep = -stereoStrength * 2.0f / (float(viewsX * viewsY - 1));
		offsetStep = -stereoOffset * 2.0f / (float(viewsX * viewsY - 1));
	}

	int imageHalfWidth = (int)singleImageSize.x / 2;
	int imageHalfHeight = (int)singleImageSize.y / 2;

	SDL_GPUTextureCreateInfo textureCreateInfo {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GetGPUSwapchainTextureFormat(context->device, context->window),
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
		.width = (Uint32)viewportSize.x,
		.height = (Uint32)viewportSize.y,
		.layer_count_or_depth = 1,
		.num_levels = 1
	};

	if (exportTexture != nullptr) SDL_ReleaseGPUTexture(context->device, exportTexture);
	exportTexture = SDL_CreateGPUTexture(context->device, &textureCreateInfo);

	SDL_SetGPUTextureName(
		context->device,
		exportTexture,
		"Export Texture"
	);

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) {
		SDL_Log("Acquire GPU Command Buffer Failed.");
		return -1;
	}

	if (context->backgroundStyle == Solid) clearColorCurrent = clearColorSolid;
	else if (context->backgroundStyle == Light) clearColorCurrent = clearColorLight;
	else if (context->backgroundStyle == Dark) clearColorCurrent = clearColorDark;

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = exportTexture;
	colorTargetInfo.clear_color = (SDL_FColor){ clearColorCurrent.r, clearColorCurrent.g, clearColorCurrent.b, clearColorCurrent.a };
	colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

	SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, nullptr);

	for (auto renderY = startY; renderY >= 0 && renderY < viewsY; renderY += stepY) {
		for (auto renderX = startX; renderX < viewsX; renderX += stepX) {
			auto drawViewport = getViewportGrid(glm::vec2(renderX, renderY), singleImageSize);
			SDL_SetGPUViewport(renderPass, &drawViewport);

			SDL_GPUBufferBinding vertexBinding = { .buffer = sharedVertexBuffer, .offset = 0 };
			SDL_GPUBufferBinding indexBinding = { .buffer = sharedIndexBuffer, .offset = 0 };
			SDL_BindGPUGraphicsPipeline(renderPass, imagePipeline);
			SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);
			SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
			SDL_GPUTextureSamplerBinding sampleBindings[2] = {{ .texture = imageTexture, .sampler = imageSampler },
				{ .texture = blurTexture, .sampler = imageSampler }};
			SDL_BindGPUFragmentSamplers(renderPass, 0, &sampleBindings[0], 2);

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
			auto singleImageAspect = singleImageSize.x / singleImageSize.y;
			imageDataVert.displayImageAspect = glm::vec3(singleImageAspect, singleImageAspect, 1.0);
			imageDataVert.fillScreen = 0;
			imageDataVert.projection = glm::ortho((float)-imageHalfWidth, (float)imageHalfWidth,
				-(float)imageHalfHeight, (float)imageHalfHeight);
			imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(singleImageSize.x, singleImageSize.y, 1.0));
			imageDataFrag.visibility = 1.0;
			imageDataFrag.blur = 0;

			drawImage(commandBuffer, renderPass);

			if (stereoFormat == Light_Field_LKG || stereoFormat == Light_Field_CV) {
				stereoStrength += strengthStep;
				stereoOffset += offsetStep;
			}
		}
	}

	SDL_EndGPURenderPass(renderPass);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);

	return 0;
}

SDL_Surface* Image::getExportTexture(Context* context, StereoFormat stereoFormat) {
	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

	auto stereoImageSize = context->imageSize;

	if (stereoFormat == Color_Anaglyph) {
		stereoImageSize = context->imageSize;
	} else if (stereoFormat == Side_By_Side_Full) {
		stereoImageSize = context->imageSize * glm::vec2(2.0, 1.0);
	} else if (stereoFormat == Side_By_Side_Half) {
		stereoImageSize = context->imageSize;
	} else if (stereoFormat == Color_Plus_Depth) {
		stereoImageSize = context->imageSize * glm::vec2(2.0, 1.0);
	} else if (stereoFormat == Stereo_Free_View_Grid) {
		stereoImageSize = context->imageSize;
	} else if (stereoFormat == Stereo_Free_View_LRL) {
		stereoImageSize = context->imageSize * glm::vec2(1.5, 0.5);
	} else if (stereoFormat == Light_Field_LKG) {
		auto maxRes = exportQuiltMaxResLKG;
		auto maxSize = std::max(stereoImageSize.x, stereoImageSize.y);
		stereoImageSize *= maxRes / maxSize;
		stereoImageSize.x = roundf(stereoImageSize.x);
		stereoImageSize.y = roundf(stereoImageSize.y);
		stereoImageSize = stereoImageSize * exportQuiltDimLKG;
	} else if (stereoFormat == Light_Field_CV) {
		auto maxRes = exportQuiltMaxResCV;
		auto maxSize = std::max(stereoImageSize.x, stereoImageSize.y);
		stereoImageSize *= maxRes / maxSize;
		stereoImageSize.x = roundf(stereoImageSize.x);
		stereoImageSize.y = roundf(stereoImageSize.y);
		stereoImageSize = stereoImageSize * exportQuiltDimCV;
	}

	SDL_GPUTransferBufferCreateInfo transferBufferInfo {
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = 4 * (Uint32)stereoImageSize.x * (Uint32)stereoImageSize.y
	};

	SDL_GPUTransferBuffer* downloadTransferBuffer = SDL_CreateGPUTransferBuffer(
		context->device, &transferBufferInfo);

	SDL_GPUTextureRegion textureRegion = {
		.texture = exportTexture,
		.mip_level = 0,
		.layer = 0,
		.x = 0,
		.y = 0,
		.z = 0,
		.w = (Uint32)stereoImageSize.x,
		.h = (Uint32)stereoImageSize.y,
		.d = 1
	};

	SDL_GPUTextureTransferInfo textureTransfer = {
		.transfer_buffer = downloadTransferBuffer,
		.offset = 0,
		.pixels_per_row = (Uint32)stereoImageSize.x,
		.rows_per_layer = (Uint32)stereoImageSize.y
	};

	SDL_DownloadFromGPUTexture(
		copyPass, &textureRegion,
		&textureTransfer
	);

	SDL_EndGPUCopyPass(copyPass);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
	SDL_WaitForGPUFences(context->device, true, &fence, 1);
	SDL_ReleaseGPUFence(context->device, fence);

	auto downloadedData = (Uint8*)SDL_MapGPUTransferBuffer(
		context->device,
		downloadTransferBuffer,
		false
	);

	auto result = SDL_CreateSurfaceFrom((int)stereoImageSize.x, (int)stereoImageSize.y,
		SDL_PIXELFORMAT_ARGB8888, downloadedData, (int)stereoImageSize.x * 4);

	SDL_UnmapGPUTransferBuffer(context->device, downloadTransferBuffer);
	SDL_ReleaseGPUTransferBuffer(context->device, downloadTransferBuffer);

	return result;
}

void Image::bindPipeline(SDL_GPURenderPass* renderPass, SDL_GPUGraphicsPipeline* pipeline) {
	SDL_GPUBufferBinding bindingVertex = { .buffer = sharedVertexBuffer, .offset = 0 };
	SDL_GPUBufferBinding bindingIndex = { .buffer = sharedIndexBuffer, .offset = 0 };
	SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
	SDL_BindGPUVertexBuffers(renderPass, 0, &bindingVertex, 1);
	SDL_BindGPUIndexBuffer(renderPass, &bindingIndex, SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

void Image::drawImage(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &imageDataVert, sizeof(ImageDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &imageDataFrag, sizeof(ImageDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

void Image::drawIcon(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &iconDataVert, sizeof(IconDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &iconDataFrag, sizeof(IconDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

void Image::drawSprite(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* renderPass) {
	SDL_PushGPUVertexUniformData(commandBuffer, 0, &spriteDataVert, sizeof(SpriteDataVert));
	SDL_PushGPUFragmentUniformData(commandBuffer, 0, &spriteDataFrag, sizeof(SpriteDataFrag));
	SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);
}

glm::mat4 Image::getTransform(glm::vec3 position, glm::vec3 size, glm::vec3 aspect) {
	auto value = glm::mat4(1.0f);
	value = translate(value, position);
	value = scale(value, size * aspect);
	return value;
}

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

int Image::draw(Context* context) {
	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(context->device);
	if (commandBuffer == nullptr) {
		SDL_Log("Acquire GPU Command Buffer Failed.");
		return -1;
	}

	SDL_GPUTexture* swapchainTexture;
	if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, context->window, &swapchainTexture,
			nullptr, nullptr)) {
		SDL_Log("Acquire GPU Swap Chain Failed.");
		return -1;
	}

	if (swapchainTexture != nullptr) {
		if (context->backgroundStyle == Solid) clearColorCurrent = clearColorSolid;
		else if (context->backgroundStyle == Light) clearColorCurrent = clearColorLight;
		else if (context->backgroundStyle == Dark) clearColorCurrent = clearColorDark;
		if (context->backgroundStyle == Solid && context->mode == RGB_Depth) clearColorCurrent = clearColorDepth;

		SDL_GPUColorTargetInfo colorTargetInfo{};
		colorTargetInfo.texture = swapchainTexture;
		colorTargetInfo.clear_color = (SDL_FColor){ clearColorCurrent.r, clearColorCurrent.g, clearColorCurrent.b, clearColorCurrent.a };
		colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, nullptr);

		if (renderPass == nullptr) {
			SDL_Log("GPU Render Pass Failed.");
			return -1;
		}

		int windowWidth, windowHeight;
		SDL_GetWindowSizeInPixels(context->window, &windowWidth, &windowHeight);
		glm::vec2 windowSize = { windowWidth, windowHeight };
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

		for (auto viewY = 0; viewY < viewsY; viewY++) {
			for (auto viewX = 0; viewX < viewsX; viewX++) {
				if (displayHelp) continue;
				auto viewSize = windowSize;
				auto drawViewport = getViewport(viewX, viewsX * viewsY, viewSize);
				if (viewsY > 1) {
					viewSize = windowSize / glm::vec2(viewsX, viewsY);
					drawViewport = getViewportGrid(glm::vec2(viewX, viewY), viewSize);
				}
				SDL_SetGPUViewport(renderPass, &drawViewport);

				if (imageTexture != nullptr && blurTexture != nullptr) {
					bindPipeline(renderPass, imagePipeline);
					SDL_GPUTextureSamplerBinding sampleBindings[2] = {{ .texture = imageTexture, .sampler = imageSampler },
						{ .texture = blurTexture, .sampler = imageSampler }};
					SDL_BindGPUFragmentSamplers(renderPass, 0, &sampleBindings[0], 2);

					imageDataVert.fillScreen = 1;
					imageDataVert.projection = glm::mat4(1.0f);
					imageDataVert.transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0, 2.0, 1.0));

					imageDataFrag.windowSize = windowSize;
					imageDataFrag.imageSize = context->imageSize;
					imageDataFrag.gridSize = context->gridSize;
					imageDataFrag.mode = context->mode;
					imageDataFrag.type = context->imageType;
					imageDataFrag.swapLeftRight = context->swapLeftRight;

					imageDataVert.displayImageAspect = glm::vec3(context->displayAspect, 1.0);
					if (context->mode == SBS_Full && viewsX > 1)
						imageDataVert.displayImageAspect.x *= 2.0f;

					if (context->mode == SBS_Full|| context->mode == SBS_Half || context->mode == RGB_Depth
						|| context->mode == Free_View_Grid || context->mode == Free_View_LRL) {
						if (context->imageType == Color_Only) {
							imageDataFrag.mode = Native;
						} else {
							if (context->display3D) {
								imageDataFrag.mode = Left + (viewX + (viewY % 2)) % 2;
								if (context->mode == RGB_Depth) {
									if (viewX == 0) imageDataFrag.mode = Mono;
									if (viewX == 1) imageDataFrag.mode = RGB_Depth;
								}
							} else {
								imageDataFrag.mode = Mono;
							}
						}
					} else if (context->imageType == Color_Only) imageDataFrag.mode = Native;

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
					imageDataVert.transform = glm::scale(imageDataVert.transform, glm::vec3(windowSize.x, windowSize.y, 1.0));
					imageDataFrag.visibility = context->visibility;
					if (displayTip) imageDataFrag.visibility *= 0.5;
					imageDataFrag.blur = 0;
					imageDataFrag.force = 0;
					if (context->mode == RGB_Depth && context->imageType != Color_Plus_Depth && viewX > 0) {
						imageDataFrag.force = 1;
					}

					drawImage(commandBuffer, renderPass);
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
			auto viewColorBlackLight = style.getColor(Style::Color::Black, Style::Alpha::Weak);
			auto viewColorBlueSolid = style.getColor(Style::Color::Black, Style::Alpha::Solid);
			if (context->mode == Anaglyph) {
				viewColorPink = style.toAnaglyph(viewColorPink);
				viewColorPinkSolid = style.toAnaglyph(viewColorPinkSolid);
			}

			auto drawViewport = getViewport(view, viewsX, windowSize);
			SDL_SetGPUViewport(renderPass, &drawViewport);

			bindPipeline(renderPass, spritePipeline);
			SDL_GPUTextureSamplerBinding sliderSampleBindings[1] = {{ .texture = sliderTexture, .sampler = imageSampler } };
			SDL_BindGPUFragmentSamplers(renderPass, 0, &sliderSampleBindings[0], 1);

			for (const auto& icon : *context->appIcons) {
				if (!icon.active) continue;
				if (icon.mode == IconMode::Slider) {
					auto iconCanvas = icon.canvas();
					auto sliderPosition = Utils::getCanvasPosition(context, &iconCanvas, nullptr, aspectScale * context->displayScale);
					auto slideEnd = icon.slider.size.y / icon.slider.size.x * 0.5;
					auto spriteColor = viewColorGrayLight;
					if (context->mode == RGB_Depth && view > 0) spriteColor = uiColorDepth;
					setSpriteUniforms(glm::vec3(sliderPosition, 1.0), glm::vec3(icon.slider.size * context->displayScale, 1.0),
						spriteColor, (float)icon.visibility, 1, { 0, 0 },
						{ 1, 1 }, glm::vec2(slideEnd, 1.0 - slideEnd),
						glm::vec3(aspectScale, 1.0));
					drawSprite(commandBuffer, renderPass);
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

				for (const auto& choice : *context->menuChoices) {
					if (!choice.active) continue;
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

			bindPipeline(renderPass, iconPipeline);
			SDL_GPUTextureSamplerBinding iconSampleBindings[1] = {{ .texture = iconTexture, .sampler = imageSampler } };
			SDL_BindGPUFragmentSamplers(renderPass, 0, &iconSampleBindings[0], 1);

			for (const auto& icon : *context->appIcons) {
				if (!icon.active) continue;
				auto iconCanvas = icon.canvas();
				auto iconPosition = Utils::getCanvasPosition(context, &iconCanvas, &icon.slider, aspectScale * context->displayScale);
				iconDataVert.transform = glm::translate(glm::mat4(1.0f),glm::vec3(iconPosition, 1.0f));
				auto iconRadius = style.getIconRadius(Style::getCurrentScale());
				if (icon.mode == IconMode::Slider) iconRadius = style.getIconSlider(Style::getCurrentScale());
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

	return 0;
}

void Image::quit(Context* context){
	SDL_ReleaseGPUGraphicsPipeline(context->device, imagePipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, iconPipeline);
	SDL_ReleaseGPUGraphicsPipeline(context->device, spritePipeline);
	SDL_ReleaseGPUBuffer(context->device, sharedVertexBuffer);
	SDL_ReleaseGPUBuffer(context->device, sharedIndexBuffer);
	SDL_ReleaseGPUTexture(context->device, imageTexture);
	SDL_ReleaseGPUTexture(context->device, blurTexture);
	SDL_ReleaseGPUTexture(context->device, blitTexture);
	SDL_ReleaseGPUTexture(context->device, iconTexture);
	SDL_ReleaseGPUTexture(context->device, helpTexture);
	SDL_ReleaseGPUTexture(context->device, menuTexture);
	SDL_ReleaseGPUTexture(context->device, sliderTexture);
	SDL_ReleaseGPUTexture(context->device, exportTexture);
	SDL_ReleaseGPUSampler(context->device, imageSampler);
	SDL_DestroySurface(menuTextSurface);
	SDL_DestroySurface(ssimSurface);
	TTF_CloseFont(menuFont);
	TTF_CloseFont(helpFont);
	TTF_Quit();
	Core::quit(context);
}
