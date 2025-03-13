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

#ifndef RENDEPTH_UTILS_H
#define RENDEPTH_UTILS_H

#include "Core.h"
#include "glm/glm.hpp"
#include <cmath>
#include <complex>

class Utils {
public:
	static Utils& get();
	static glm::vec2 getSafeSize(glm::vec2 imageSize, glm::vec2 displaySize, float safeArea = 1.0f, bool expand = false);
	static double lerp(double a, double b, double f);
	static glm::vec2 lerp(glm::vec2 a, glm::vec2 b, double f);
	static double tween(double value, double target, double speed);
	static glm::vec2 getCanvasPosition(const Context* context, const Canvas* canvas,
		const Canvas* slider = nullptr, const glm::vec2& aspectScale = glm::vec2(1.0f));
	static float getLuminance(glm::vec3 color);
	static float getDistance(glm::vec3 a, glm::vec3 b);
	static float getMean(const std::vector<float>& values);
	static float getVariance(const std::vector<float>& values, float mean);
	static float getCovariance(const std::vector<float>& valuesA, const std::vector<float>& valuesB,
		float meanA, float meanB);
	static float getSumAbs(const std::vector<float>& values);
	static std::complex<float> getSumComplexAbs(const std::vector<std::complex<float>>& valuesA, std::vector<std::complex<float>>& valuesB);
	static std::complex<float> getSumComplexAbsSq(const std::vector<std::complex<float>>& valuesA);
	static std::complex<float> getSumComplexConj(const std::vector<std::complex<float>>& valuesA, const std::vector<std::complex<float>>& valuesB);
	static std::complex<float> getSumComplexConjAbs(const std::vector<std::complex<float>>& valuesA, std::vector<std::complex<float>>& valuesB);
	static float complexSimilarity(std::vector<float> valuesA, std::vector<float>& valuesB);
};

#endif