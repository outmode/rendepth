#version 450

// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D sourceTexture;
layout (set = 3, binding = 0) uniform LanczosDataFrag {
	vec2 sourceSize;
	vec2 targetSize;
};

const float PI = 3.14159265358979323846;

float sinc(float x) {
	if (abs(x) < 1e-5) return 1.0;
	float px = PI * x;
	return sin(px) / px;
}

float lanczos3(float x) {
	if (abs(x) >= 3.0) return 0.0;
	return sinc(x) * sinc(x / 3.0);
}

void main() {
	vec2 pos = fragUV * sourceSize - vec2(0.5);
	vec2 base = floor(pos);
	vec2 fraction = pos - base;

	vec4 colorSum = vec4(0.0);
	float totalWeight = 0.0;
	vec4 minColor = vec4(1.0);
	vec4 maxColor = vec4(0.0);

	for (int y = -2; y <= 3; ++y) {
		float wy = lanczos3(float(y) - fraction.y);
		for (int x = -2; x <= 3; ++x) {
			float wx = lanczos3(float(x) - fraction.x);
			float weight = wx * wy;

			ivec2 sampleCoord = clamp(ivec2(base) + ivec2(x, y), ivec2(0), ivec2(sourceSize) - ivec2(1));
			vec2 sampleUV = (vec2(sampleCoord) + vec2(0.5)) / sourceSize;
			vec4 sampleColor = texture(sourceTexture, sampleUV);

			if (x >= 0 && x <= 1 && y >= 0 && y <= 1) {
				minColor = min(minColor, sampleColor);
				maxColor = max(maxColor, sampleColor);
			}

			colorSum += sampleColor * weight;
			totalWeight += weight;
		}
	}

	vec4 filtered = colorSum / max(totalWeight, 1e-5);
	filtered = clamp(filtered, minColor, maxColor);
	outColor = clamp(filtered, vec4(0.0), vec4(1.0));
}
