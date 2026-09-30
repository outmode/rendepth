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

#include "Style.h"

// Return the UI size preset selected for the current display.
Style::Scale Style::getCurrentScale() {
	return currentScale;
}

// Choose a UI size preset from the display's shorter dimension.
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

// Return the icon radius for the selected UI size preset.
float Style::getIconRadius(Scale scale) {
	return iconRadiusMap[scale];
}

// Return the icon gutter for the selected UI size preset.
float Style::getIconGutter(Scale scale) {
	return iconGutterMap[scale];
}

// Return the spacing between icons for the selected UI size preset.
float Style::getIconSpacer(Scale scale) {
	return iconSpacerMap[scale];
}

// Return the slider thumb size for the selected UI size preset.
float Style::getIconSlider(Scale scale) {
	return iconSliderMap[scale];
}

// Return the slider dragger size for the selected UI size preset.
float Style::getIconDragger(Scale scale) {
	return iconDraggerMap[scale];
}

// Return the slider bar thickness for the selected UI size preset.
float Style::getIconBar(Scale scale) {
	return iconBarMap[scale];
}

// Return the slider spacing for the selected UI size preset.
float Style::getIconSliderSpace(Scale scale) {
	return iconSliderSpaceMap[scale];
}

// Return the information-area margin for the selected UI size preset.
float Style::getInfo(Scale scale) {
	return infoMarginMap[scale];
}

// Return the help-text font size for the selected UI size preset.
float Style::getHelpFontSize(Scale scale) {
	return helpFontMap[scale];
}

// Return the information-label font size for the selected UI size preset.
float Style::getInfoFontSize(Scale scale) {
	return infoFontMap[scale];
}

// Return the menu font size for the selected UI size preset.
float Style::getMenuFontSize(Scale scale) {
	return menuFontMap[scale];
}

// Return the choice-row size for the selected UI size preset.
float Style::getChoiceSize(Scale scale) {
	return choiceMap[scale];
}

// Return the options-layout size for the selected UI size preset.
float Style::getOptionsSize(Scale scale) {
	return optionsMap[scale];
}

// Return the button margin for the selected UI size preset.
float Style::getButtonMargin(Scale scale) {
	return buttonMarginMap[scale];
}

// Combine a named palette color and opacity into an RGBA value.
glm::vec4 Style::getColor(Color color, Alpha alpha) {
	return { colorMap[color], alphaMap[alpha] };
}

// Apply the left and right anaglyph filters and gamma correction while retaining opacity.
glm::vec4 Style::toAnaglyph(glm::vec4 color) const {
	glm::vec3 filtered{ 0.0 };
	filtered += clamp(glm::vec3(color) * leftFilter,
		glm::vec3(0.0), glm::vec3(1.0));
	filtered += clamp(glm::vec3(color) * rightFilter,
		glm::vec3(0.0), glm::vec3(1.0));
	filtered = correctColor(filtered);
	return { filtered, color.a };
}

// Apply the configured per-channel gamma correction to an RGB color.
glm::vec3 Style::correctColor(glm::vec3 original) const {
	glm::vec3 corrected { 0.0 };
	corrected.r = glm::pow(original.r, 1.0f / gammaMap.r);
	corrected.g = glm::pow(original.g, 1.0f / gammaMap.g);
	corrected.b = glm::pow(original.b, 1.0f / gammaMap.b);
	return corrected;
}
