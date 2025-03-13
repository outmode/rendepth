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

#include "Style.h"
#include <iostream>
#include <glm/ext/matrix_transform.hpp>

Style::Scale Style::getCurrentScale() {
	return currentScale;
}

void Style::calculateScale(glm::vec2 screen) {
	auto minDim = std::min(screen.x, screen.y);
	if (minDim <= 1200.0) {
		currentScale = Scale::Small;
	} else if (minDim <= 1600.0) {
		currentScale = Scale::Medium;
	} else {
		currentScale = Scale::Large;
	}
}

float Style::getIconRadius(Scale scale) {
	return iconRadiusMap[scale];
}

float Style::getIconGutter(Scale scale) {
	return iconGutterMap[scale];
}

float Style::getIconSpacer(Scale scale) {
	return iconSpacerMap[scale];
}

float Style::getIconSlider(Scale scale) {
	return iconSliderMap[scale];
}

float Style::getIconDragger(Scale scale) {
	return iconDraggerMap[scale];
}

float Style::getIconBar(Scale scale) {
	return iconBarMap[scale];
}

float Style::getIconSliderSpace(Scale scale) {
	return iconSliderSpaceMap[scale];
}

float Style::getInfo(Scale scale) {
	return infoMap[scale];
}

float Style::getInfoFontSize(Scale scale) {
	return infoFontMap[scale];
}

float Style::getMenuFontSize(Scale scale) {
	return menuFontMap[scale];
}

float Style::getChoiceSize(Scale scale) {
	return choiceMap[scale];
}

float Style::getOptionsSize(Scale scale) {
	return optionsMap[scale];
}

float Style::getButtonMargin(Scale scale) {
	return buttonMarginMap[scale];
}

glm::vec4 Style::getColor(Color color, Alpha alpha) {
	return { colorMap[color], alphaMap[alpha] };
}

glm::vec4 Style::toAnaglyph(glm::vec4 color) const {
	glm::vec3 filtered{ 0.0 };
	filtered += clamp(glm::vec3(color) * leftFilter,
		glm::vec3(0.0), glm::vec3(1.0));
	filtered += clamp(glm::vec3(color) * rightFilter,
		glm::vec3(0.0), glm::vec3(1.0));
	filtered = correctColor(filtered);
	return { filtered, color.a };
}

glm::vec3 Style::correctColor(glm::vec3 original) const {
	glm::vec3 corrected { 0.0 };
	corrected.r = glm::pow(original.r, 1.0f / gammaMap.r);
	corrected.g = glm::pow(original.g, 1.0f / gammaMap.g);
	corrected.b = glm::pow(original.b, 1.0f / gammaMap.b);
	return corrected;
}