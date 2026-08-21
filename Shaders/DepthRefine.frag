#version 450

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D colorTexture;
layout (set = 2, binding = 1) uniform sampler2D depthTexture;
layout (set = 3, binding = 0) uniform DepthRefineData {
	vec2 depthSize;
	float spatialSigma;
	float colorSigma;
};

float catmullRomWeight(float value) {
	float absoluteValue = abs(value);
	if (absoluteValue <= 1.0)
		return 1.5 * absoluteValue * absoluteValue * absoluteValue -
			2.5 * absoluteValue * absoluteValue + 1.0;
	if (absoluteValue < 2.0)
		return -0.5 * absoluteValue * absoluteValue * absoluteValue +
			2.5 * absoluteValue * absoluteValue - 4.0 * absoluteValue + 2.0;
	return 0.0;
}

float catmullRomDepth(vec2 uv) {
	vec2 pixel = uv * depthSize - vec2(0.5);
	vec2 base = floor(pixel);
	vec2 fraction = pixel - base;
	float result = 0.0;
	float totalWeight = 0.0;
	for (int y = -1; y <= 2; ++y) {
		for (int x = -1; x <= 2; ++x) {
			ivec2 samplePixel = clamp(ivec2(base) + ivec2(x, y),
				ivec2(0), ivec2(depthSize) - ivec2(1));
			vec2 offset = vec2(x, y) - fraction;
			float weight = catmullRomWeight(offset.x) * catmullRomWeight(offset.y);
			result += texture(depthTexture,
				(vec2(samplePixel) + vec2(0.5)) / depthSize).r * weight;
			totalWeight += weight;
		}
	}
	return result / max(totalWeight, 0.0001);
}

void main() {
	vec3 guide = texture(colorTexture, fragUV).rgb;
	vec2 depthPixel = fragUV * depthSize - vec2(0.5);
	ivec2 center = ivec2(floor(depthPixel));
	float spatialDenominator = 2.0 * spatialSigma * spatialSigma;
	float colorDenominator = 2.0 * colorSigma * colorSigma;
	float candidates[25];
	float weights[25];
	int sampleCount = 0;

	for (int y = -2; y <= 2; ++y) {
		for (int x = -2; x <= 2; ++x) {
			ivec2 samplePixel = clamp(center + ivec2(x, y), ivec2(0), ivec2(depthSize) - ivec2(1));
			vec2 sampleUV = (vec2(samplePixel) + vec2(0.5)) / depthSize;
			float candidate = texture(depthTexture, sampleUV).r;
			vec3 candidateGuide = texture(colorTexture, sampleUV).rgb;
			// Include the fractional position inside the low-resolution depth
			// texel. Without this, all output pixels sharing one source texel
			// receive the same weights and the result appears blocky.
			vec2 spatialOffset = vec2(samplePixel) - depthPixel;
			float spatialDistance = dot(spatialOffset, spatialOffset);
			float colorDistance = dot(guide - candidateGuide, guide - candidateGuide);
			float weight = exp(-spatialDistance / spatialDenominator -
				colorDistance / colorDenominator);
			candidates[sampleCount] = candidate;
			weights[sampleCount] = weight;
			sampleCount++;
		}
	}

	// Sort the small neighborhood by depth and select the weighted median.
	// Unlike a weighted mode, this does not quantize the result into bins.
	for (int i = 1; i < 25; ++i) {
		float candidate = candidates[i];
		float weight = weights[i];
		int j = i - 1;
		while (j >= 0 && candidates[j] > candidate) {
			candidates[j + 1] = candidates[j];
			weights[j + 1] = weights[j];
			j--;
		}
		candidates[j + 1] = candidate;
		weights[j + 1] = weight;
	}
	float totalWeight = 0.0;
	for (int i = 0; i < 25; ++i) totalWeight += weights[i];
	float accumulatedWeight = 0.0;
	float median = candidates[24];
	float localMinimum = candidates[0];
	float localMaximum = candidates[0];
	for (int i = 0; i < 25; ++i) {
		localMinimum = min(localMinimum, candidates[i]);
		localMaximum = max(localMaximum, candidates[i]);
		accumulatedWeight += weights[i];
		if (accumulatedWeight >= totalWeight * 0.5) {
			median = candidates[i];
			break;
		}
	}

	// Retain the median's edge selection, then add a restrained bicubic
	// reconstruction to soften haloing and recover sub-texel smoothness.
	float smoothDepth = clamp(catmullRomDepth(fragUV), localMinimum, localMaximum);
	float softened = mix(median, smoothDepth, 0.50);
	outColor = vec4(vec3(softened), 1.0);
}
