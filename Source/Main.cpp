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

#define SDL_HINT_RENDER_VSYNC "SDL_RENDER_VSYNC"
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_image/SDL_image.h>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <zmq.hpp>
#include <filesystem>
#include <format>
#include <algorithm>
#include <functional>
#include <vector>
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <regex>
#include <thread>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Core.h"
#include "Utils.h"
#include "Image.h"

Context context{};
Image imageView{};

glm::vec2 windowSize{};
auto switchedImage = false;
bool isFullscreen = false;
bool isMaximized = false;
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
auto lastSwitchTime = 0.0;
auto mouseLastActive = 0.0;
auto mouseMoveWait = 3.0;
auto currentStereoMode = Native;
auto preferredStereoMode = Anaglyph;
auto defaultStereoMode = Anaglyph;
glm::vec2 pixelMotion = {0.0, 0.0 };
bool isDragging = false;
bool isIconCaptured = false;
const auto mouseDelayCount = 1;
auto mouseMoveDelay = mouseDelayCount;
auto mouseValueNull = -128.0f;
static std::string depthCommand;
bool isConverting = false;
bool justConverted = false;
SDL_Thread* depthGenThread = nullptr;
SDL_Thread* depthPipeThread = nullptr;
std::atomic<bool> depthGenAlive (false);
std::atomic<bool> depthPipeAlive (false);
std::atomic<bool> doneLoadingImage (false);
std::atomic<bool> depthPipeError (false);
std::atomic<bool> doingFileOp (false);
std::vector<std::function<void()>> callbackQueue{};
std::string qualityMode = "0";
bool display3D = false;
bool prevNextKeyDown = false;
Icon* currentSlider = nullptr;
auto currentSliderValue = 0.0;
auto showingStereoSettings = false;
auto isPlayingSlideshow = false;
auto slideshowWaitTime = 8.0;
auto lastSlideshowTime = 0.0;
auto displayTipTime = 0.0;
auto displayTipWait = 2.0;
auto similarityThreshold = 0.67;
auto swapInterlace = false;
auto mouseLeftWindow = false;
auto mouseIsDown = false;
std::string openFolderResult;
std::string upscaleResolution = "1920";
std::string depthSize = "540";
StereoFormat exportFormat = Color_Anaglyph;
std::string exportTag = "anaglyph";
SortOrder sortOrder = Alpha_Ascending;
std::string exportFolderName = "3D Export";
Style style;

std::vector<FileInfo> fileList{};
auto fileIndex = 0;
auto preloadDir = 1;
auto doingPreload = false;
static SDL_Thread* preloadThread = nullptr;
static AsyncData asyncData{};

zmq::context_t signalContext{1};
zmq::socket_t signalSend{};
std::string signalEndpoint;

static std::string packageFolder = "Package";
static std::string envFolder = "Environment";
static std::string depthGenExe = "DepthGenerate.py";

std::filesystem::path exePath = std::filesystem::path(
	SDL_GetBasePath()).parent_path().parent_path();
std::filesystem::path homeDir = Core::getHomeDirectory();
std::filesystem::path homePath = homeDir / ".Rendepth";
std::filesystem::path tempPath = "Temp/";
static std::filesystem::path tempFolder = homePath / tempPath;
static std::string nextFileToConvert;
static int nextIndexToConvert = -1;

static std::random_device randDevice;
static std::mt19937 randGen(randDevice());
static std::uniform_int_distribution randEffect(0, 1);
static std::uniform_int_distribution randImage(0);
static int nextRandIndex = -1;
static int currentRandIndex = -1;

static glm::vec2 refreshWindowSize();
static void setMaximize(bool maximize = true);
static void toggleMaximized();
static void checkMouseState();
static void hideUI(bool hideCustomMouse = true, bool hideRealMouse = false);
static void showCustomCursor(bool show);
static double getTimeNow() {
	return (double)SDL_GetTicks() / 1000.0;
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
static void callDepthGen(int imageIndex);

int previousFileIndex() {
	int index = fileIndex - 1;;
	if (!fileList.empty()) {
		if (index < 0) index = (int)fileList.size() - 1;
	}
	return index;
}

int nextFileIndex() {
	int index = fileIndex + 1;
	if (!fileList.empty()) {
		index = index % (int)fileList.size();
	}
	return index;
}

static void setSlideshow(bool slide);
static void cancelSlideshow();

void gotoPreviousImage() {
	if (isConverting) return;
	if (justConverted) return;
	if (doingPreload) return;
	if (doingFileOp) return;
	if (context.loading) return;
	if (fileList.empty()) return;
	if (isPlayingSlideshow) cancelSlideshow();
	preloadDir = -1;
	auto previousIndex = previousFileIndex();
	if (display3D) {
		if (fileList[previousIndex].type == Color_Only &&
			fileList[previousIndex].similarity <= similarityThreshold) {
			fileIndex = previousIndex;
			SDL_DestroySurface(fileList[fileIndex].preload);
			fileList[fileIndex].preload = nullptr;
			callDepthGen(previousIndex);
			return;
		}
	}
	context.gotoPrev = true;
	switchedImage = true;
}

void gotoNextImage() {
	if (isConverting) return;
	if (justConverted) return;
	if (doingPreload) return;
	if (doingFileOp) return;
	if (context.loading) return;
	if (fileList.empty()) return;
	if (isPlayingSlideshow) cancelSlideshow();
	preloadDir = 1;
	auto nextIndex = nextFileIndex();
	if (display3D) {
		if (fileList[nextIndex].type == Color_Only &&
			fileList[nextIndex].similarity <= similarityThreshold) {
			fileIndex = nextIndex;
			SDL_DestroySurface(fileList[fileIndex].preload);
			fileList[fileIndex].preload = nullptr;
			callDepthGen(nextIndex);
			return;
		}
	}
	context.gotoNext = true;
	switchedImage = true;
}

static std::string stringLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c){ return std::tolower(c); });
	return s;
}

static const std::vector<std::string> supportedExts { ".jpg", ".jpeg", ".jps",
	".png", ".tga", ".bmp" };
bool isSupportedImage(const std::string& path) {
	auto filePath = std::filesystem::path(path);
	auto fileExt = stringLower(filePath.extension().string());
	for (const auto& ext : supportedExts) {
		if (fileExt == ext) return true;
	}
	return false;
}

bool isStereoImage(StereoFormat format) {
	return !(format == Color_Only || format == Color_Plus_Depth || format == Unknown_Format);
}

int getRandImageIndex() {
	auto randIndex = randImage(randGen);
	auto randAttempts = 64;
	while ((randIndex == fileIndex || randIndex == nextRandIndex ||
		isStereoImage(fileList[randIndex].type)) && --randAttempts > 0) {
		randIndex = randImage(randGen);
	}
	return randIndex;
}

void gotoRandomImage() {
	if (isConverting) return;
	if (doingFileOp) return;
	if (context.loading) return;
	if (fileList.empty()) return;
	auto randIndex = nextRandIndex;
	if (randIndex < 0) {
		randIndex = getRandImageIndex();
	}
	nextRandIndex = getRandImageIndex();
	preloadDir = 0;
	if (display3D || preferredStereoMode == Depth_Zoom) {
		if (fileList[randIndex].type == Color_Only &&
			fileList[randIndex].similarity <= similarityThreshold) {
			fileIndex = randIndex;
			SDL_DestroySurface(fileList[randIndex].preload);
			fileList[randIndex].preload = nullptr;
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
static void setStereoMode(Mode mode);
static void refreshDisplay3D(StereoFormat type);
static void toggleFullscreen();
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
static int callDepthGenOnce(const std::string& fileFolderPath, bool realTime, int imageId = -1);
static int loadImage(void* ptr);
static void conversionCompleted(const char* path, int imageId = -1);

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
	true
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
	[]() { gotoPreviousImage(); },
	1.0,
	true
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
	[]() { gotoNextImage(); },
	1.0,
	true
};

Icon IconStereo3D = {
	IconType::Stereo_3D,
	IconType::Stereo_3D,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Display 3D",
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
	false
};

Icon IconFullscreen = {
	IconType::Fullscreen,
	IconType::Fullscreen,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Toggle Fullscreen",
	style.getColor(Style::Color::White, Style::Alpha::Solid),
	[]()->Canvas {
		return {
			sizeStandard,
		{ -positionEdge, style.getIconRadius(Style::getCurrentScale()) + style.getIconSpacer(Style::getCurrentScale()) + style.getInfo(Style::getCurrentScale()) },
			alignRightTop,
			{-positionEdge * 3.0, style.getInfo(Style::getCurrentScale()), 1.0, 0.0},
			{0.0, positionEdge * 2.0 + style.getInfo(Style::getCurrentScale()),
				1.0, 0.0}};
	},
	[]() {
		if (isConverting) return;
		toggleFullscreen();
	},
	1.0,
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
	1.0,
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
	"Open File",
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
	false
};

Icon IconSave = {
	IconType::Save,
	IconType::Save,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Save File",
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
	false
};

Icon IconBatch = {
	IconType::Folder,
	IconType::Folder,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Save Folder",
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
	false
};

Icon IconOptions = {
	IconType::Options,
	IconType::Options,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Open Options",
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
	false
};

Icon IconSettings = {
	IconType::Settings,
	IconType::Settings,
	IconGroup::None,
	IconMode::Button,
	IconState::Idle,
	"Open Settings",
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
	false
};

Choice ChoiceStereo {
	"3D Mode",
	{ "Anaglyph", "SBS Full", "SBS Half", "Free View",
		"Horizontal", "Vertical", "Checkerboard", "Disabled" },
};

Choice ChoiceExport {
	"Export Format",
	{ "Anaglyph", "Color + Depth", "SBS Full", "SBS Half",
		"Free View", "Light Field", "Color Only" },
};

Choice ChoiceModel {
	"Depth Model",
	{ "Small", "Base", "Large" },
};

Choice ChoiceResolution {
	"Minimum Resolution",
	{ "1920", "2560", "3840" },
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
	"Add 3D Tag",
	{ "Original", "Side by Side", "Color Only" },
};

static std::vector menuChoices = { ChoiceStereo, ChoiceExport, ChoiceModel, ChoiceResolution,
	ChoiceBackground, ChoiceSorting, ChoiceSlideshow, ChoiceTags };

static std::unordered_map<std::string, int> menuSelection = {
	{ ChoiceStereo.label, 0 },
	{ ChoiceExport.label, 0 },
	{ ChoiceModel.label, 0 },
	{ ChoiceResolution.label, 0 },
	{ ChoiceBackground.label, 0 },
	{ ChoiceSorting.label, 0 },
	{ ChoiceSlideshow.label, 0 },
	{ ChoiceTags.label, 0 }
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
};

static void setPreferredStereo(Mode mode, bool saveMode = true);
static std::array stereoModes = {
	Anaglyph, SBS_Full, SBS_Half, Free_View, Horizontal, Vertical, Checkerboard, Mono };
static void changeStereo(int option) {
	setPreferredStereo(stereoModes[option], true);
	Image::saveMenuLayout(&context);
}

static std::array exportFormats = {
	Color_Anaglyph, Color_Plus_Depth, Side_By_Side_Full, Side_By_Side_Half,
	Stereo_Free_View, Grid_Array, Color_Only };
static std::array exportTags = { "anaglyph",  "rgbd", "sbs", "sbs_half_width",
	"free_view", "qs", "rgb" };
static void changeExport(int option) {
	exportFormat = exportFormats[option];
	exportTag = exportTags[option];
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

static std::array importTags = { "", "sbs", "rgb" };
static void changeImport(int option, bool init) {
	std::string tag = importTags[option];
	if (!init && !fileList.empty())
		addStereoTag(fileList[fileIndex].link, tag);
}

static auto depthCloseWait = 1200;
static void endPreload(bool success);
static void sendDepthQuit();
static void deleteTempFiles(const std::filesystem::path& folder);
static auto depthRegenerated = false;
static void resetDepthGeneration() {
	depthPipeAlive = false;
	depthGenAlive = false;
	sendDepthQuit();
	endPreload(false);
	deleteTempFiles(tempFolder);
	depthRegenerated = true;
}

static void closeDepthGeneration() {
	if (signalContext.handle()) {
		signalContext.close();
		signalContext.shutdown();
	}
}

static std::array<std::string, 3> depthQuality = { "0", "1", "2" };
static std::array<std::string, 3> depthSizes = { "560", "640", "720" };

static void changeModel(int option, bool init) {
	qualityMode = depthQuality[option];
	depthSize = depthSizes[option];
	if (!init) resetDepthGeneration();
}

static std::array<std::string, 3> upscaleResolutions = { "1920", "2560", "3840" };
static void changeResolution(int option, bool init) {
	upscaleResolution = upscaleResolutions[option];
	if (!init) resetDepthGeneration();
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
    { ChoiceTags.label, [](int option) { changeImport(option, firstInit); } }
};

double sliderStart = 0.5;
double currentStereoStrength = sliderStart * 0.8 + 0.1;
double currentStereoDepth = sliderStart * 0.5 + 0.25;
double currentStereoOffset = (1.0 - sliderStart) / 100.0;
double currentGridAngle = sliderStart;

static double getSliderPercent(const Icon& icon) {
	return icon.slider.position.x / icon.slider.size.x + 0.5;
}

static void setSliderPercent(Icon& icon, double percent) {
	icon.slider.position.x = icon.slider.size.x * (float)(percent - 0.5);
}

static void updateStrengthSlider() {
	currentStereoStrength = getSliderPercent(*currentSlider) * 0.8 + 0.1;
}

static void updateDepthSlider() {
	currentStereoDepth = getSliderPercent(*currentSlider) * 0.5 + 0.25;
}

static void updateOffsetSlider() {
	currentStereoOffset = (1.0 - getSliderPercent(*currentSlider)) / 100.0;
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
	"Stereo Depth",
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
		1.0,
		true,
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
	"Stereo Offset",
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
		{
			{ style.getIconDragger(Style::getCurrentScale()), style.getIconBar(Style::getCurrentScale()) },
			{ 0.0, 0.0 },
			alignCenter,
			areaTopLeft, areaBottomRightFull },
		};

static std::vector appIcons = { IconLoading, IconFullscreen, IconOpen, IconBatch, IconSave,
	IconOptions, IconSettings, IconBack, IconForward, IconStereo3D,
	IconStrength, IconDepth, IconOffset, IconPlay };

static const SDL_DialogFileFilter filters[] = {
	{ "All Images",  "jpg;jpeg;jps;png;tga;bmp" },
	{ "JPEG Images", "jpg;jpeg" },
	{ "JPS Images", "jps" },
	{ "PNG Images",  "png" },
	{ "TGA Images",  "tga" },
	{ "BMP Images",  "bmp" }
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
		if (isSupportedImage(*filelist)) fileNames.push_back(*filelist);
		filelist++;
	}

	if (!fileNames.empty()) {
		parseFileList(fileNames);
		loadImage(nullptr);
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

static void saveFile() {
	doingFileOp = true;
	auto renderResult = Image::renderStereoImage(&context, exportFormat);
	if (renderResult != 0) {
		doingFileOp = false;
		return;
	}

	auto data = Image::getExportTexture(&context, exportFormat);
	auto exportDir = std::filesystem::path(context.fileLink).parent_path();
	if (exportDir.filename() != exportFolderName) exportDir = exportDir / exportFolderName;
	const std::string exportType = "jpg";

	std::string gridInfo;
	if (exportFormat == Grid_Array) {
		std::string aspect = std::format("{:.3f}", context.imageSize.x / context.imageSize.y);
		gridInfo = "9x8a" + aspect;
	}

	if (!exists(exportDir)) create_directories(exportDir);
	std::string outFileName = removeFileTags(context.fileName);
	std::filesystem::path outFilePath = outFileName + "_" + exportTag + gridInfo + "." + exportType;
	auto outputPath = exportDir / outFilePath;
	IMG_SaveJPG(data, outputPath.string().c_str(), 65);
	SDL_DestroySurface(data);

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
		dataSize = SDL_ReadIO(optionsFile, dataBuffer, 65536);
		SDL_CloseIO(optionsFile);
	}
	if (dataSize == 0) return;
	if (document.Parse(dataBuffer).HasParseError()) return;
	if (!document.IsObject()) return;

	for (auto& setting : menuSelection) {
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
}

static void toggleStereoSettings() {
	setShowStereoSettings(!showingStereoSettings);
}

static void toggleOptions() {
	context.displayMenu = !context.displayMenu;
	auto& icon = getIcon(IconType::Options);
	icon.image = context.displayMenu ? IconType::Close : IconType::Options;
	setShowStereoSettings(showingStereoSettings && !context.displayMenu);
	if (!context.displayMenu) {
		if (depthRegenerated) {
			depthRegenerated = false;
			if (!isConverting && !context.loading && !doingPreload && !fileList.empty()) {
				parseFileList({ fileList[fileIndex].link });
				loadImage(nullptr);
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
	if (preferredStereoMode == SBS_Full|| preferredStereoMode == SBS_Half) {
		context.mode = preferredStereoMode;
		if (type == Color_Only) setDisplay3D(false);
		return;
	}
	if (type == Color_Only) {
		setStereoMode(Native);
		setDisplay3D(false);
	} else {
		if (display3D) {
			setStereoMode(preferredStereoMode);
		} else {
			setStereoMode(Mono);
		}
	}
}

static void setStereoMode(Mode mode) {
	currentStereoMode = mode;
	context.mode = currentStereoMode;
	Image::updateSize(&context);
}

static void setPreferredStereo(Mode mode, bool saveMode) {
	if (saveMode) defaultStereoMode = mode;
	preferredStereoMode = mode;
	updateStereoIcon();
	if (!fileList.empty()) {
		refreshDisplay3D(fileList[fileIndex].type);
		Image::updateSize(&context);
		Image::updateRatio(&context, windowSize);
	}
}

bool naturalLess(const std::string& a, const std::string& b) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (std::isdigit(a[i]) && std::isdigit(b[j])) {
			size_t iEnd = i, jEnd = j;
			while (iEnd < a.size() && std::isdigit(a[iEnd])) ++iEnd;
			while (jEnd < b.size() && std::isdigit(b[jEnd])) ++jEnd;

			std::string numA = a.substr(i, iEnd - i);
			std::string numB = b.substr(j, jEnd - j);

			if (std::stoll(numA) != std::stoll(numB))
				return std::stoll(numA) < std::stoll(numB);

			i = iEnd;
			j = jEnd;
		} else {
			if (a[i] != b[j]) return a[i] < b[j];
			++i;
			++j;
		}
	}

	return i == a.size() && j < b.size();
}

bool nameAscending(const FileInfo& f1, const FileInfo& f2) {
	return naturalLess(f1.base, f2.base);
}

bool nameDescending(const FileInfo& f1, const FileInfo& f2) {
	return naturalLess(f2.base, f1.base);
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

static void updateFullscreenState() {
	auto& icon = getIcon(IconType::Fullscreen);
	icon.image = isFullscreen ? IconType::Window : IconType::Fullscreen;
	context.offset = glm::vec2(0.0f);
	targetZoom = 1.0f;
	context.fullscreen = isFullscreen;
	context.maximized = isMaximized;
	Image::updateSize(&context);
}

static void setFullscreen(bool fullscreen = true) {
	SDL_SetWindowFullscreen(context.window, fullscreen);
	SDL_SyncWindow(context.window);
	updateFullscreenState();
}

static void toggleFullscreen() {
	isFullscreen = !isFullscreen;
	setFullscreen(isFullscreen);
}

void toggleStereo() {
	if (isConverting) return;
	if (fileList.empty()) return;
	if (!display3D && fileList[fileIndex].type == Color_Only)
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
	auto extension = filePath.extension();
	std::string fileName = filePath.string();
	if (isSupportedImage(fileName)) {
		if (fileName.empty()) return;
		if (filePath.filename().string().empty()) return;
		std::filesystem::file_time_type modifiedTime = last_write_time(filePath);
		std::string clockString = std::format("{:%Y-%m-%d}", modifiedTime);
		std::uintmax_t fileSize = file_size(filePath);
		auto sizeText = formatFileSize(fileSize);
		FileInfo info;
		info.link = fileName;
		info.path = fileName;
		info.name = filePath.filename().string();
		info.base = filePath.filename().replace_extension().string();
		info.size = sizeText;
		info.date = std::format("{:%Y-%m-%d}", modifiedTime);
		info.modified = modifiedTime;
		info.preload = nullptr;
		info.similarity = -1.0;
		info.type = Core::getImageType(info.name);
		if (info.type == Unknown_Format) info.type = Color_Only;
		fileList.push_back(info);
	}
}

static void parseFileList(const std::vector<std::string>& filesToLoad) {
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
			for (const auto& entry : std::filesystem::directory_iterator(parentPath)) {
				pushFileInfo(entry.path());
			}
		}
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
	auto imageType = Core::getImageType(preloadFile.path);
	auto untagged = imageType == Unknown_Format;
	if (untagged && preloadFile.similarity < 0.0) {
		auto ssimSize = Image::blitSSIMTexture(preloadData,
			preloadData->w, preloadData->h);
		auto ssim = Image::getSimilarity(Image::ssimSurface, ssimSize.x, ssimSize.y);
		preloadFile.similarity = ssim;
		if (ssim > similarityThreshold) {
			preloadFile.preloadType = Side_By_Side_Full;
		}
	}
}

void preloadImage(int overrideId = -1) {
	auto nextId = nextFileIndex();
	auto prevId = previousFileIndex();
	if (preloadDir == 0 && overrideId == -1) return;
	auto preloadId = preloadDir > 0 ? nextId : prevId;
	if (overrideId >= 0) preloadId = overrideId;
	if (doingPreload) endPreload(true);
	asyncData.path = fileList[preloadId].path;
	asyncData.surface = nullptr;
	asyncData.fileIndex = preloadId;
	doingPreload = true;
	preloadThread = Core::loadImageAsync(asyncData);
	if (preloadThread == nullptr) endPreload(false);
}

static void endPreload(bool success) {
	if (success) {
		int asyncId;
		SDL_WaitThread(preloadThread, &asyncId);
		if (asyncData.surface != nullptr && asyncData.fileIndex >= 0 && !fileList.empty()) {
			preloadComplete(asyncData.surface, fileList[asyncData.fileIndex]);
		}
	} else {
		SDL_DetachThread(preloadThread);
	}
	asyncData.surface = nullptr;
	asyncData.fileIndex = -1;
	asyncData.path = "";
	preloadThread = nullptr;
	doingPreload = false;
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
	context.swapInterlace = false;

	loadOptions();

	firstInit = false;

	if (!fileToLoad.empty())
		parseFileList({ fileToLoad });

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Failed To Initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if (!fileList.empty()) {
		if (Image::init(&context, fileList[fileIndex]) < 0) {
			SDL_Log("Could Not Initialize Image.");
			return SDL_APP_FAILURE;
		}
		doneLoadingImage = true;
	} else {
		FileInfo emptyFile{};
		if (Image::init(&context, emptyFile) < 0) {
			SDL_Log("Could Not Initialize Image.");
			return SDL_APP_FAILURE;
		}
	}

	setDisplay3D(false);

	if (!fileList.empty() && fileList.size() > fileIndex) refreshDisplay3D(fileList[fileIndex].type);

	hideUI();
	refreshWindowSize();

	return SDL_APP_CONTINUE;
}

static auto visibilitySpeed = 9.0;
static auto visibilitySwitchMinimum = 0.75;
static auto minimumSwitchTime = 0.125;

static int loadImage(void* ptr) {
	currentVisibility = 0.0;
	targetVisibility = 0.0;
	auto result = Image::load(&context, fileList[fileIndex], fileList[fileIndex].preload);
	fileList[fileIndex].preload = nullptr;
	doneLoadingImage = true;
	return result;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
	auto timeNow = getTimeNow();
	context.deltaTime = timeNow - lastTime;
	lastTime = timeNow;

	currentVisibility = Utils::tween(currentVisibility, targetVisibility, visibilitySpeed / context.refreshRate);

	if (switchedImage) {
		if (context.loading) {
			switchedImage = false;
		} else {
			auto switchTime = getTimeNow() - lastSwitchTime;
			if (switchTime > minimumSwitchTime && currentVisibility > visibilitySwitchMinimum) {
				lastSwitchTime = getTimeNow();
				context.offset = { 0, 0 };
				if (context.gotoPrev) {
					fileIndex = previousFileIndex();
				} else if (context.gotoNext) {
					fileIndex = nextFileIndex();
				} else if (context.gotoRand) {
					fileIndex = currentRandIndex;
				}
				loadImage(nullptr);
			} else {
				switchedImage = false;
			}
		}
	}

	if (justConverted) setDisplay3D(true);
	justConverted = false;

	if (doneLoadingImage) {
		doneLoadingImage = false;
		refreshDisplay3D(fileList[fileIndex].type);
		Image::updateSize(&context);
		currentZoom = 1.0;
		targetZoom = 1.0;
		currentVisibility = 0.0;
		targetVisibility = 1.0;
		depthEffect = 0.0;
		context.depthEffect = depthEffect;
		context.effectRandom = randEffect(randGen);
		switchedImage = false;
		lastSlideshowTime = timeNow;
		menuChoices[7].active = true;
		menuSelection[ChoiceTags.label] = 0;
		auto fileType = Core::getImageType(
			std::filesystem::path(fileList[fileIndex].path).filename().string());
		if (fileType == Unknown_Format) menuSelection[ChoiceTags.label] = 0;
		else if (fileType == Side_By_Side_Full) menuSelection[ChoiceTags.label] = 1;
		else if (fileType == Color_Only) menuSelection[ChoiceTags.label] = 2;
		else menuChoices[7].active = false;
		if (isPlayingSlideshow) preloadImage(nextRandIndex);
		else preloadImage();
	} else if (doingPreload) {
		endPreload(true);
	}

	static auto iconVisibilitySpeed = 16.0;

	for (auto& icon : appIcons) {
		auto iconTargetVisibility = 1.0;
		if (icon.type != IconType::Loading) {
			if (icon.state == IconState::Over) icon.visibility = iconTargetVisibility;
			if (icon.state == IconState::Idle) iconTargetVisibility = 0.0;
		} else {
			iconTargetVisibility = (isConverting && !isPlayingSlideshow) ? 1.0 : 0.0;
		}
		icon.visibility = Utils::tween(icon.visibility, iconTargetVisibility, iconVisibilitySpeed / context.refreshRate);
	}

	context.loadingRotation += (float)context.deltaTime;
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

	currentZoom = Utils::tween(currentZoom, targetZoom, zoomSpeed / context.refreshRate);

	auto oneHalf = glm::vec2(0.5);
	auto halfWindow = context.windowSize * oneHalf;
	auto halfImage = context.safeSize * oneHalf;
	auto zoomVec = glm::vec2((float)currentZoom, (float)currentZoom);
	auto imageWindowBounds = glm::abs(halfWindow - halfImage * zoomVec);

	context.currentZoom = currentZoom;

	auto motionScale = glm::vec2(1.0);
	if (display3D && preferredStereoMode == Free_View) {
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
	context.fullscreen = isFullscreen;
	context.maximized = isMaximized;
	context.visibility = (float)currentVisibility;
	context.stereoStrength = currentStereoStrength;
	context.stereoDepth = currentStereoDepth;
	context.stereoOffset = currentStereoOffset;
	context.gridAngle = currentGridAngle;
	context.swapInterlace = (int)swapInterlace;
	context.windowSize = windowSize;
	context.safeSize = Image::updateRatio(&context, windowSize);

	auto visibleSize = windowSize;
	if (preferredStereoMode == SBS_Half) visibleSize.x *= 0.5;
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
		depthEffect += context.deltaTime / slideshowWaitTime;
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

	if (!callbackQueue.empty()) {
		auto& callback = callbackQueue.back();
		callback();
		callbackQueue.pop_back();
	}

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
					auto result = callDepthGenOnce(fileListPath.string(), false, -1);
					if (result == 0) {
						auto nameMaxLen = 18;
						if (displayName.length() > nameMaxLen) {
							displayName = displayName.substr(0, nameMaxLen - 3) + "...";
						}
						Core::drawText(&context, "Converting " + displayName + " to Depth",
							Image::helpFont, Image::helpTexture, Image::helpTextSize, "Help Texture");
						Image::displayTip = true;
						displayTipTime = getTimeNow();
					}
				}
			}
		}
		openFolderResult.clear();
		isConverting = false;
	}

	if (depthPipeError) {
		nextFileToConvert.clear();
		nextIndexToConvert = -1;
		context.loading = false;
		justConverted = false;
		isConverting = false;
		SDL_SetWindowTitle(context.window, context.appName);
		Core::drawText(&context, "Could Not Load Image", Image::helpFont, Image::helpTexture,
			Image::helpTextSize, "Help Texture");
		Image::displayHelp = true;
		depthPipeError = false;
	}

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

void checkMouseState() {
	isIconCaptured = false;
	auto aspectScale = glm::vec2(1.0);
	if (preferredStereoMode == SBS_Full && (isMaximized || isFullscreen))
		aspectScale = glm::vec2(2.0, 1.0);
	for (auto& icon : appIcons) {
		auto displayLoading = !(icon.type == IconType::Loading && (!isConverting || isPlayingSlideshow));
		auto displaySettings = !(icon.type == IconType::Settings &&
			(!display3D || currentStereoMode == Depth_Zoom || (!fileList.empty() && fileList[fileIndex].type != Color_Plus_Depth &&
				fileList[fileIndex].type != Grid_Array)));
		auto displayStereo = !((icon.type == IconType::Glasses || icon.type == IconType::Focus ||
			icon.type == IconType::Layers) && (!showingStereoSettings || !display3D));
		auto displayParallax = !(icon.type == IconType::Focus &&
			(!fileList.empty() && fileList[fileIndex].type == Grid_Array));
		auto displayOpen = !((icon.type != IconType::File)
			&& fileList.empty());
		auto displayMenu = !(context.displayMenu && (icon.type == IconType::Forward || icon.type == IconType::Back ||
			icon.type == IconType::File || icon.type == IconType::Folder || icon.type == IconType::Save || icon.type == IconType::Window ||
			icon.type == IconType::Fullscreen || icon.type == IconType::Play || icon.type == IconType::Pause ||
			icon.type == IconType::Settings));
		auto displayXD = !(context.displayMenu && icon.type == IconType::Stereo_3D);
		auto displaySave = true;
		if (!fileList.empty())
			displaySave = !(icon.type == IconType::Save && ((fileList[fileIndex].type == Color_Only ||
				fileList[fileIndex].type == Color_Anaglyph) || (fileList[fileIndex].type != Color_Plus_Depth &&
					exportFormat == Color_Plus_Depth)));
		if (withinArea(context.mouse, getCoordinates(icon.canvas().topLeft, aspectScale),
			getCoordinates(icon.canvas().bottomRight, aspectScale))) {
			icon.state = IconState::Near;
			icon.active = displayLoading && displaySettings && displayStereo && displayParallax &&
				displayOpen && displaySave && displayMenu && displayXD;
			if (icon.type == IconType::Loading) icon.active = true;
			auto iconPosition = getCoordinates(
				{icon.canvas().position * aspectScale
					+ icon.slider.position * aspectScale,
					icon.canvas().alignment});
			auto iconSize = glm::vec2(icon.canvas().size.x);
			iconSize *=	aspectScale * context.displayScale;
			auto iconAspect = iconSize.x / iconSize.y;
			auto iconX = (context.mouse.x - iconPosition.x - 1.0);
			auto iconY = (context.mouse.y - iconPosition.y - 1.0) * iconAspect;
			auto isInsideIcon = iconX * iconX + iconY * iconY <= iconSize.x * iconSize.x;
			if (isInsideIcon || &icon == currentSlider) {
				if (icon.type != IconType::Loading) {
					icon.state = IconState::Over;
					isIconCaptured = true;
				}
			}
		} else {
			if (&icon == currentSlider) {
				currentSlider = nullptr;
			}
			if (icon.type != IconType::Loading) {
				icon.state = IconState::Idle;
				icon.active = false;
			} else {
				icon.active = true;
			}
		}
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
	return (isFullscreen || isMaximized) &&
		(preferredStereoMode == SBS_Full || preferredStereoMode == SBS_Half);
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

static void createTempFolder(const std::filesystem::path& folder) {
	if (exists(folder)) return;
	create_directories(folder);
}

static void deleteTempFiles(const std::filesystem::path& folder) {
	if (!exists(folder)) return;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		const auto& currentFile = entry.path();
		if (currentFile.extension() == ".jpg") {
			std::filesystem::remove(currentFile);
		}
	}
}

static void conversionCompleted(const char* path, int imageId) {
	auto colorPath = std::filesystem::path(fileList[imageId].path).filename().replace_extension();;
	auto depthPath = std::filesystem::path(path).filename().replace_extension();;
	if (imageId == fileIndex) fileList[imageId].path = path;
	nextFileToConvert.clear();
	nextIndexToConvert = -1;
	context.loading = false;
	switchedImage = true;
	justConverted = true;
	isConverting = false;
}

static int depthGenRun(void* ptr) {
	if (depthGenAlive) {
		auto packagePath = homePath / packageFolder;
		auto depthPath = packagePath / depthGenExe;
#ifdef WIN32
		auto pythonPath = homePath / envFolder / "Scripts" / "python.exe";
#else
		auto pythonPath = homePath / envFolder / "bin" / "python3";
#endif

		auto command = static_cast<std::string*>(ptr);
#ifdef WIN32
		ShellExecute(nullptr, "open", pythonPath.string().c_str(),
			(depthPath.string() + " " + (*command)).c_str(), nullptr, SW_HIDE);
#else
		auto success = system((pythonPath.string() + " " +  depthPath.string() + " " + (*command)).c_str());
#endif
	}
	return 0;
}

static void sendDepthQuit() {
	if (signalSend.handle()) {
		const std::string_view quitMessage = std::string_view("quit");
		signalSend.send(zmq::buffer(quitMessage), zmq::send_flags::none);
		signalSend.close();
	}
}

static int depthPipeRun(void* ptr) {
	while (depthPipeAlive) {
		if (!nextFileToConvert.empty()) {
			const std::string_view fileMessage = std::string_view(nextFileToConvert);
			signalSend.send(zmq::buffer(fileMessage), zmq::send_flags::none);
			zmq::message_t message;
			auto result = signalSend.recv(message, zmq::recv_flags::none);
			auto reply = message.to_string();
			if (reply != "ERROR") {
				conversionCompleted(reply.c_str(), nextIndexToConvert);
			} else {
				depthPipeError = true;
			}
		}
	}
	return 0;
}

static void	setupDepthSignal() {
	signalSend = zmq::socket_t(signalContext, zmq::socket_type::req);
	signalSend.bind("tcp://127.0.0.1:*");
	signalEndpoint = signalSend.get(zmq::sockopt::last_endpoint);
}

static int callDepthGenOnce(const std::string& fileFolderPath, bool realTime, int imageId) {
	auto packagePath = homePath / packageFolder;
	auto depthPath = packagePath / depthGenExe;

	if (!exists(depthPath)) {
		Core::drawText(&context, "\"DepthGenerate\" Not Found, Check Install Instructions", Image::helpFont,
			Image::helpTexture, Image::helpTextSize, "Help Texture");
		Image::displayTip = true;
		displayTipTime = getTimeNow();
		isConverting = false;
		return 1;
	}

	std::string service = realTime ? "2" : "0";
	depthCommand = "--model " + qualityMode + " --depth " + depthSize +
		" --upscale " + upscaleResolution + " --maxsize " + std::to_string(Image::maxImageSize) +
		" --mode " + service + " --base " + exePath.string() +
		" --home " + homePath.string() + " --input \"";
	depthCommand += fileFolderPath + "\"";
	if (realTime) {
		setupDepthSignal();
		depthCommand += " --endpoint " + signalEndpoint;
	}

	createTempFolder(tempFolder);

	depthGenAlive = true;
	depthGenThread = SDL_CreateThread(depthGenRun, "depthGenRun", &depthCommand);
	SDL_DetachThread(depthGenThread);

	if (realTime) {
		nextFileToConvert = fileFolderPath;
		nextIndexToConvert = imageId;
		depthPipeAlive = true;
		depthPipeThread = SDL_CreateThread(depthPipeRun, "depthPipeRun", nullptr);
		SDL_DetachThread(depthPipeThread);
	}
	return 0;
}

static void callDepthGen(int imageIndex) {
	isConverting = true;
	if (!depthPipeAlive) {
		callDepthGenOnce(fileList[imageIndex].link, true, imageIndex);
	} else {
		nextFileToConvert = fileList[imageIndex].link;
		nextIndexToConvert = imageIndex;
	}
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

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
	if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
		auto currentDisplay = event->display.data1;
		auto displayMode = SDL_GetCurrentDisplayMode(currentDisplay);
		Image::currentDisplay = currentDisplay;
		auto virtualSize = glm::vec2(displayMode->w, displayMode->h) * displayMode->pixel_density;
		context.virtualSize = virtualSize;

		auto displaySize = glm::vec3((float)displayMode->w, (float)displayMode->h, 0.0f);
		context.displaySize = displaySize;
		auto refreshRate = displayMode->refresh_rate;
		if (refreshRate < 1.0f) refreshRate = 60.0f;
		context.refreshRate = refreshRate;
		showCustomCursor(false);
		auto displayScale = SDL_GetWindowDisplayScale(context.window);
		context.displayScale = displayScale;
#if defined(__APPLE__)
		Image::mouseScale = displayScale;
#else
		Image::mouseScale = 1.0;
#endif
		updateDisplayScale();
	} else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
		showCustomCursor(false);
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
		hideUI(false);
	} else if (event->type == SDL_EVENT_KEY_DOWN) {
		if (event->key.key == SDLK_ESCAPE) {
			if(context.displayMenu) {
				context.displayMenu = false;
				auto& icon = getIcon(IconType::Options);
				icon.image = IconType::Options;
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
		}

		if (event->key.key == SDLK_MINUS || event->key.key == SDLK_KP_MINUS) {
			swapInterlace = false;
		} else if (event->key.key == SDLK_EQUALS || event->key.key == SDLK_KP_PLUS) {
			swapInterlace = true;
		}

		if (event->key.key == SDLK_SPACE || event->key.key == SDLK_KP_5) {
			if (!isConverting && !doingPreload && !fileList.empty()) {
				if (fileList[fileIndex].type == Color_Only) callDepthGen(fileIndex);
				setDisplay3D(!display3D);
				refreshDisplay3D(fileList[fileIndex].type);
			}
		}

		if (event->key.key == SDLK_RETURN && event->key.mod == SDL_KMOD_LALT) {
			toggleFullscreen();
		}

		if (event->key.key == SDLK_F || event->key.key == SDLK_F11 || event->key.key == SDLK_KP_0) {
			toggleFullscreen();
		}

		if (event->key.key == SDLK_LEFT || event->key.key == SDLK_A || event->key.key == SDLK_UP ||
			event->key.key == SDLK_KP_4 || event->key.key == SDLK_KP_8) {
			if (!prevNextKeyDown) gotoPreviousImage();
			prevNextKeyDown = true;
		} else if (event->key.key == SDLK_RIGHT || event->key.key == SDLK_D || event->key.key == SDLK_DOWN ||
			event->key.key == SDLK_KP_6 || event->key.key == SDLK_KP_2) {
			if (!prevNextKeyDown) gotoNextImage();
			prevNextKeyDown = true;
		}
	} else if (event->type == SDL_EVENT_KEY_UP) {
		prevNextKeyDown = false;

	} else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (event->button.button == SDL_BUTTON_LEFT) {
			mouseIsDown = true;
			for (auto& icon : appIcons) {
				if (icon.state == IconState::Over && icon.active) {
					Image::displayTip = false;
					auto allowCallback = true;
					auto queueCallback = false;
					if ((!isConverting && !doingPreload) || icon.type == IconType::Close) {
						if (icon.mode == IconMode::Button) {
							if (isPlayingSlideshow && (icon.type != IconType::Forward &&
								icon.type != IconType::Back)) {
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
							}
						} else if (icon.mode == IconMode::Slider) {
							currentSlider = &icon;
						}
					}
				}
			}

			if (context.displayMenu) {
				for (const auto& choice : *context.menuChoices) {
					auto optionIndex = (*context.menuRollover)[choice.label];
					if (optionIndex >= 0) {
						(*context.menuSelection)[choice.label] = optionIndex;
						(*context.menuCallback)[choice.label](optionIndex);
					}
				}
			}

			showCustomCursor(true);
			context.mouse.x = event->button.x * Image::mouseScale;
			context.mouse.y = event->button.y * Image::mouseScale;
			mouseLastActive = getTimeNow();
			checkMouseState();
			if (!isIconCaptured && !isConverting) isDragging = true;
		} else if (event->button.button == SDL_BUTTON_X1) {
			gotoNextImage();
		} else if (event->button.button == SDL_BUTTON_X2) {
			gotoPreviousImage();
		}
	} else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (event->button.button == SDL_BUTTON_LEFT) {
			auto timeNow = getTimeNow();
			auto clickDiff = timeNow - lastClick;
			lastClick = timeNow;
			if (!isIconCaptured && !context.displayMenu) {
				if (clickDiff < doubleClickTime) toggleMaximized();
			}
			isDragging = false;
			mouseIsDown = false;
			currentSlider = nullptr;
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
		checkMouseState();

		if (currentSlider != nullptr) {
			auto aspectScale = 1.0;
			if (preferredStereoMode == SBS_Full && (isMaximized || isFullscreen))
				aspectScale = 2.0;
			aspectScale *= context.displayScale / Image::mouseScale;
			currentSlider->slider.position += glm::vec2(event->motion.xrel / aspectScale,
				event->motion.yrel / aspectScale);
			auto sliderExtents = currentSlider->slider.size.x * 0.5f;
			currentSlider->slider.position.x = glm::clamp(currentSlider->slider.position.x,
				std::ceil(-sliderExtents), std::floor(sliderExtents));
			currentSlider->slider.position.y = 0.0f;
			if (currentSlider->callback) currentSlider->callback();
		}
	} else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
		wheelSpeed += (int)event->wheel.y;
		mouseLastActive = getTimeNow();
	} else if (event->type == SDL_EVENT_DROP_FILE) {
		std::string droppedFile = event->drop.data;
		if (isSupportedImage(droppedFile)) {
			if (!isConverting && !doingPreload && !context.loading) {
				if (isPlayingSlideshow) cancelSlideshow();
				parseFileList({ droppedFile });
				setDisplay3D(false);
				updateStereoIcon();
				loadImage(nullptr);
			}
		}
	}

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	saveOptions();
	if (signalSend.handle()) {
		resetDepthGeneration();
		closeDepthGeneration();
	}
	Image::quit(&context);
	SDL_Delay(depthCloseWait);
	SDL_Quit();
}