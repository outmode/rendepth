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
#include "fcwt/fcwt.h"
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

float Utils::getLuminance(glm::vec3 color) {
	return dot(glm::vec3(0.299f, 0.587f, 0.114f), color);
}

float Utils::getDistance(glm::vec3 a, glm::vec3 b) {
	return distance(a, b);
}

float Utils::getMean(const std::vector<float>& values) {
	float mean = 0.0;
	for (auto value : values) mean += value;
	return mean / (float)values.size();
}

float Utils::getVariance(const std::vector<float>& values, float mean) {
	float variance = 0.0;
	for (auto value : values) variance += glm::pow(value - mean, 2.0f);
	return variance / (float)values.size();
}

float Utils::getCovariance(const std::vector<float>& valuesA, const std::vector<float>& valuesB,
	float meanA, float meanB) {
	float covariance = 0.0;
	for (auto i = 0; i < valuesA.size(); i++)
		covariance += (valuesA[i] - meanA) * (valuesB[i] - meanB);
	return covariance / (float)(valuesA.size() - 1);
}

float Utils::getSumAbs(const std::vector<float>& values) {
	float sum = 0.0;
	for (auto value : values) sum += std::abs(value);
	return sum;
}

std::complex<float> Utils::getSumComplexAbs(const std::vector<std::complex<float>>& valuesA, std::vector<std::complex<float>>& valuesB) {
	std::complex<float> sum {};
	for (auto i = 0; i < valuesA.size(); i++)
		sum += std::abs(valuesA[i]) * std::abs(valuesB[i]);
	return sum;
}

std::complex<float> Utils::getSumComplexAbsSq(const std::vector<std::complex<float>>& valuesA) {
	std::complex<float> sum{};
	for (auto i : valuesA)
		sum += std::abs(i) * std::abs(i);
	return sum;
}

std::complex<float> Utils::getSumComplexConj(const std::vector<std::complex<float>>& valuesA, const std::vector<std::complex<float>>& valuesB) {
	std::complex<float> sum{};
	for (auto i = 0; i < valuesA.size(); i++)
		sum += valuesA[i] * std::conj(valuesB[i]);
	return sum;
}

std::complex<float> Utils::getSumComplexConjAbs(const std::vector<std::complex<float>>& valuesA, std::vector<std::complex<float>>& valuesB) {
	std::complex<float> sum{};
	for (auto i = 0; i < valuesA.size(); i++)
		sum += std::abs(valuesA[i] * std::conj(valuesB[i]));
	return sum;
}

float Utils::complexSimilarity(std::vector<float> valuesA, std::vector<float>& valuesB) {
	int n = (int)valuesA.size();
	const int fs = 1000;
	const float f0 = 1.0;
	const float f1 = 32.0;
	const int fn = 8;
	Wavelet* wavelet;
	Morlet morl(2.0f);
	wavelet = &morl;
	FCWT fcwt(wavelet, 8, false, false);
	Scales scs(wavelet, FCWT_LINFREQS, fs, f0, f1, fn);
	std::vector<complex<float>> cwtA(n * fn);
	std::vector<complex<float>> cwtB(n * fn);
	fcwt.cwt(&valuesA[0], n, &cwtA[0], &scs);
	fcwt.cwt(&valuesB[0], n, &cwtB[0], &scs);
	const float K = 0.01f;
	auto cx2 = getSumComplexAbsSq(cwtA);
	auto cy2 = getSumComplexAbsSq(cwtB);
	auto cxya = getSumComplexAbs(cwtA, cwtB);
	auto cxyc = getSumComplexConj(cwtA, cwtB);
	auto cxyca = getSumComplexConjAbs(cwtA, cwtB);
	auto cwssim =
		((2.0f * cxya + K) /
		(cx2 + cy2 + K)) *
		((2.0f * std::abs(cxyc) + K) /
		(2.0f * cxyca + K));
	return cwssim.real();
}