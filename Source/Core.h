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

#ifndef RENDEPTH_CORE_H
#define RENDEPTH_CORE_H

#define SDL_GPU_SHADERCROSS_IMPLEMENTATION
#include "SDL3_shadercross/SDL_shadercross.h"
#include "SDL3_ttf/SDL_ttf.h"
#include "glm/glm.hpp"
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <filesystem>

enum ViewMode {
	Native,
	Mono,
	Left,
	Right,
	Anaglyph,
	RGB_Depth,
	SBS_Full,
	SBS_Half,
	Free_View_Grid,
	Free_View_LRL,
	Horizontal,
	Vertical,
	Checkerboard,
	Depth_Zoom
};

enum StereoFormat {
	Color_Only,
	Color_Anaglyph,
	Color_Plus_Depth,
	Side_By_Side_Full,
	Side_By_Side_Swap,
	Side_By_Side_Half,
	Stereo_Free_View_Grid,
	Stereo_Free_View_LRL,
	Light_Field_LKG,
	Light_Field_CV,
	Unknown_Format
};

enum EyesFormat {
	Left_Right,
	Right_Left
};

enum class IconType {
	None, Background, Back, Forward, Rotate_CW, Rotate_CCW, Zoom_In, Zoom_Out,
	Fullscreen, Window, Sort, Sort_Reverse, Info, Minimize, Maximize, Close,
	Loading, Loading_Fade, Options, Settings, Folder, Save, Delete, Crop,
	Glasses, Focus, Layers, Stereo_2D, Stereo_3D, Cursor_Empty, Cursor_Black, Cursor_Pink,
	Logo_White, Logo_Dark, Logo_Light, Play, Pause, File, Mono_SD, Mono_SR
};

enum class IconGroup {
	None, SettingsDepth, SettingsGrid
};

enum class IconMode {
	Button, Slider, Choice
};

enum class IconState {
	Idle, Near, Over, Click
};

static inline std::vector<std::pair<std::string, StereoFormat>> tagType = {
	{ "_anaglyph", Color_Anaglyph },
	{ "_rgbd", Color_Plus_Depth },
	{ "_rgb", Color_Only },
	{ "_sbs_half_width", Side_By_Side_Half },
	{ "_sbs", Side_By_Side_Full },
	{ "_free_view_lrl", Stereo_Free_View_LRL },
	{ "_free_view", Stereo_Free_View_Grid },
	{ "_qs", Light_Field_LKG },
	{ "_cv", Light_Field_CV },
	{ "_half_2x1", Side_By_Side_Half },
	{ "_2x1", Side_By_Side_Full },
	{ ".jps", Side_By_Side_Swap },
	{ ".pns", Side_By_Side_Swap } };

static inline glm::vec2 exportQuiltDimLKG = glm::ivec2(9, 8);
static inline float exportQuiltMaxResLKG = 864.0;

static inline glm::vec2 exportQuiltDimCV = glm::ivec2(8, 5);
static inline float exportQuiltMaxResCV = 960.0;

struct SpriteDataVert {
	glm::mat4 transform;
	glm::mat4 projection;
	glm::vec2 uvOffset;
	glm::vec2 uvSize;
};

struct SpriteDataFrag {
	glm::vec4 color;
	float visibility;
	int useTexture;
	glm::vec2 slice;
};

struct Canvas {
	glm::vec2 size;
	glm::vec2 position;
	glm::vec2 alignment;
	glm::vec4 topLeft;
	glm::vec4 bottomRight;
};

struct Icon {
	IconType type;
	IconType image;
	IconGroup group;
	IconMode mode;
	IconState state;
	std::string label;
	glm::vec4 color;
	std::function<Canvas()> canvas;
	std::function<void()> callback;
	double visibility;
	bool active;
	bool shown;
	Canvas slider;
};

struct MenuLayout {
	glm::vec3 position;
	glm::vec3 size;
};

struct Choice {
	std::string label;
	std::vector<std::string> options;
	MenuLayout layout;
	std::vector<MenuLayout> layouts;
	bool active;
};

struct OptionsTexture {
	glm::vec2 offset;
	glm::vec2 size;
};

enum BackgroundStyle {
	Blur,
	Solid,
	Light,
	Dark
};

enum SortOrder {
	Alpha_Ascending,
	Alpha_Descending,
	Date_Ascending,
	Date_Descending
};

struct Context {
	const char* appName;
	std::string fileName;
	std::string fileLink;
	SDL_Window* window;
	SDL_GPUDevice* device;
	bool gotoPrev;
	bool gotoNext;
	bool gotoRand;
	double deltaTime;
	float displayScale;
	float pixelDensity;
	glm::vec2 windowSize;
	glm::vec2 windowSizeBase;
	glm::vec2 displaySize;
	glm::vec2 virtualSize;
	glm::vec2 imageSize;
	glm::vec3 gridSize;
	glm::vec2 safeSize;
	glm::vec2 displayAspect;
	bool fullscreen;
	bool maximized;
	float visibility;
	bool loading;
	double currentZoom;
	ViewMode mode;
	int view;
	bool display3D;
	std::vector<Icon>* appIcons;
	std::vector<Choice>* menuChoices;
	std::unordered_map<std::string, int>* menuSelection;
	std::unordered_map<std::string, int>* menuRollover;
	std::unordered_map<std::string, std::function<void(int)>>* menuCallback;
	glm::vec2 offset;
	glm::vec2 mouse;
	glm::vec2 pivot;
	glm::vec2 infoSize;
	std::string infoText;
	float infoVisibility;
	float mouseVisibility;
	float loadingRotation;
	StereoFormat imageType;
	double stereoStrength;
	double stereoDepth;
	double stereoOffset;
	double gridAngle;
	double depthEffect;
	bool displayMenu;
	BackgroundStyle backgroundStyle;
	int effectRandom;
	int swapLeftRight;
	glm::vec2 imageBounds;
};

struct FileInfo {
	std::string link;
	std::string path;
	std::string name;
	std::string base;
	std::string size;
	std::string date;
	std::filesystem::file_time_type modified;
	StereoFormat type;
	StereoFormat preloadType;
	SDL_Surface* preload;
};

struct AsyncData {
	std::string path;
	SDL_Surface* surface;
	int fileIndex;
	bool done;
};

class Core {
public:
	static void quit(Context* context);
	static SDL_GPUShader* loadShader(SDL_GPUDevice* device, const std::string& shaderFilename, Uint32 samplerCount,
		Uint32 uniformBufferCount, Uint32 storageBufferCount, Uint32 storageTextureCount);
	static SDL_Surface* loadImageDirect(const std::string& imageFilename);
	static int loadImageThread(void* ptr);
	static SDL_Thread* loadImageAsync(AsyncData& asyncData);
	static glm::vec2 getTextSize(TTF_Font* font, const std::string& text);
	static std::string getFileText(const FileInfo& imageInfo, glm::vec2 imageSize);
	static StereoFormat getImageType(const std::string& file);
	static glm::vec3 getGridInfo(const std::string& file);
	static void drawText(Context* context, const std::string& text, TTF_Font* font,
		SDL_GPUTexture*& texture, glm::vec2& size, const std::string& name);
	static int uploadTexture(Context* context, SDL_Surface* imageData, SDL_GPUTexture** gpuTexture,
		const std::string& textureName);
	static std::filesystem::path getHomeDirectory();
	inline static SDL_GPUShaderFormat gpuShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
	inline static std::string lastDrawnText;
	inline static StereoFormat defaultImportFormat = Color_Only;
};

#endif