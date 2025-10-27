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

#include "Utils.h"
#include "glm/glm.hpp"
#include <cmath>
#include <algorithm>
#include <ranges>

Utils& Utils::get() {
	static Utils instance;
	return instance;
}

glm::vec2 Utils::getSafeSize(glm::vec2 imageSize, glm::vec2 displaySize, float safeArea, bool expand) {
	auto defaultHeight = 576.0;
	auto smallerDimension = std::min(displaySize.x, displaySize.y);
	auto unsafePixels = smallerDimension * (1.0f - safeArea);
	auto safeSize = glm::vec3(displaySize.x - unsafePixels, displaySize.y - unsafePixels, 0);
	auto imageAspect = imageSize.x / imageSize.y;
	auto safeAspect = safeSize.x / safeSize.y;
	if (!expand) {
		if (imageSize.y < defaultHeight || displaySize.y < defaultHeight)
			return {std::round(defaultHeight * imageAspect), defaultHeight };
		if (imageSize.x < safeSize.x && imageSize.y < safeSize.y) return imageSize;
	}
	if (imageAspect >= safeAspect)
		return { std::round(safeSize.x), std::round(safeSize.x / imageAspect) };
	return {std::round(safeSize.y * imageAspect), std::round(safeSize.y) };
}

bool Utils::isFullWidth(glm::vec2 imageSize) {
	float aspect = imageSize.x / imageSize.y;
	return  aspect > 2.0 && !(aspect < 1.0);
}

double Utils::lerp(double a, double b, double f){
	return a * (1.0 - f) + (b * f);
}

glm::vec2 Utils::lerp(glm::vec2 a, glm::vec2 b, double f){
	return {lerp(a.x, b.x, f), lerp(a.y, b.y, f) };
}

double Utils::tween(double value, double target, double speed) {
	static auto snapThreshold = 0.03;
	static auto snapStep = 0.003;
	static auto clampThreshold = 0.003;
	auto result = lerp(value, target, speed);
	if (fabs(result - target) < snapThreshold) {
		result += glm::sign(target - result) * snapStep;
		if (fabs(result - target) < clampThreshold) {
			result = target;
		}
	}
	return result;
}

glm::vec2 Utils::getCanvasPosition(const Context* context, const Canvas* canvas,
	const Canvas* slider, const glm::vec2& aspectScale) {
	auto position = canvas->position * aspectScale + canvas->alignment * context->windowSize;
	if (slider) position += slider->position * aspectScale;
	position.y = context->windowSize.y - position.y;
	return position;
}
