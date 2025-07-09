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

#ifndef RENDEPTH_STYLE_H
#define RENDEPTH_STYLE_H

#include "glm/glm.hpp"
#include <map>

class Style {
public:
	Style() = default;
	~Style() = default;

	enum class Color {
		Black,
		White,
		Gray,
		Pink,
		Blue
	};

	enum class Alpha {
		Solid,
		Strong,
		Medium,
		Weak
	};

	enum class Scale {
		Small,
		Medium,
		Large
	};

	static Scale getCurrentScale();
	static void calculateScale(glm::vec2 screen);
	float getIconRadius(Scale scale);
	float getIconGutter(Scale scale);
	float getIconSpacer(Scale scale);
	float getIconSlider(Scale scale);
	float getIconSliderSpace(Scale scale);
	float getIconDragger(Scale scale);
	float getIconBar(Scale scale);
	float getInfo(Scale scale);
	float getHelpFontSize(Scale scale);
	float getInfoFontSize(Scale scale);
	float getMenuFontSize(Scale scale);
	float getChoiceSize(Scale scale);
	float getOptionsSize(Scale scale);
	float getButtonMargin(Scale scale);
	glm::vec4 getColor(Color color, Alpha alpha);
	[[nodiscard]] glm::vec3 correctColor(glm::vec3 original) const;
	[[nodiscard]] glm::vec4 toAnaglyph(glm::vec4 color) const;

private:
	static inline Scale currentScale = Scale::Medium;

	std::map<Color, glm::vec3> colorMap {
		{ Color::Black, {0.0, 0.0, 0.0 } },
		{ Color::White, {1.0, 1.0, 1.0 } },
		{ Color::Gray, {0.84, 0.84, 0.84 } },
		{ Color::Pink, {0.996, 0.110, 0.408 } },
		{ Color::Blue, {0.09, 0.15, 0.23 } },
	};

	std::map<Alpha, double> alphaMap {
		{ Alpha::Solid, 1.0 },
		{ Alpha::Strong, 0.67 },
		{ Alpha::Medium, 0.5 },
		{ Alpha::Weak, 0.33 },
	};

	std::map<Scale, float> helpFontMap {
		{ Scale::Small, 28.0f },
		{ Scale::Medium, 30.0f },
		{ Scale::Large, 34.0f },
	};

	std::map<Scale, float> infoFontMap {
			{ Scale::Small, 20.0f },
			{ Scale::Medium, 22.0f },
			{ Scale::Large, 26.0f },
		};

	std::map<Scale, float> menuFontMap {
		{ Scale::Small, 14.0f },
		{ Scale::Medium, 20.0f },
		{ Scale::Large, 26.0f },
	};

	std::map<Scale, float> choiceMap {
			{ Scale::Small, 280.0 },
			{ Scale::Medium, 280.0 },
			{ Scale::Large, 280.0 }
	};

	std::map<Scale, float> optionsMap {
		{ Scale::Small, 128.0 },
		{ Scale::Medium, 150.0 },
		{ Scale::Large, 228.0 }
	};

	std::map<Scale, float> iconRadiusMap {
			{ Scale::Small, 40.0f },
			{ Scale::Medium, 48.0f },
			{ Scale::Large, 58.0f },
	};

	std::map<Scale, float> iconGutterMap {
		{ Scale::Small, 28.0f },
		{ Scale::Medium, 32.0f },
		{ Scale::Large, 40.0f }
	};

	std::map<Scale, float> iconSpacerMap {
			{ Scale::Small, 8.0f },
			{ Scale::Medium, 16.0f },
			{ Scale::Large, 22.0f },
	};

	std::map<Scale, float> iconSliderMap {
		{ Scale::Small, 22.0f },
		{ Scale::Medium, 24.0f },
		{ Scale::Large, 26.0f },
	};

	std::map<Scale, float> iconSliderSpaceMap {
			{ Scale::Small, 192.0 },
			{ Scale::Medium, 192.0 },
			{ Scale::Large, 232.0 },
		};

	std::map<Scale, float> iconDraggerMap {
		{ Scale::Small, 130.0f },
		{ Scale::Medium, 140.0f },
		{ Scale::Large, 170.0f }
	};

	std::map<Scale, float> iconBarMap {
		{ Scale::Small, 8.0f },
		{ Scale::Medium, 8.0f },
		{ Scale::Large, 12.0f }
	};

	std::map<Scale, float> infoMarginMap {
		{ Scale::Small, 24.0 },
		{ Scale::Medium, 24.0 },
		{ Scale::Large, 24.0 }
	};

	std::map<Scale, float> buttonMarginMap {
			{ Scale::Small, 2.5 },
			{ Scale::Medium, 2.5 },
			{ Scale::Large, 2.5 }
	};

	const glm::mat3 leftFilter = glm::mat3(
		glm::vec3(0.4561, 0.500484, 0.176381),
		glm::vec3(-0.400822, -0.0378246, -0.0157589),
		glm::vec3(-0.0152161, -0.0205971, -0.00546856));

	const glm::mat3 rightFilter = glm::mat3(
		glm::vec3(-0.0434706, -0.0879388, -0.00155529),
		glm::vec3(0.378476, 0.73364, -0.0184503),
		glm::vec3(-0.0721527, -0.112961, 1.2264));

	const glm::vec3 gammaMap = glm::vec3(1.6, 0.8, 1.0);
};

#endif
