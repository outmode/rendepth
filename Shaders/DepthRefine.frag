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

// Positive cubic B-spline weights provide bicubic smoothing without the
// negative lobes that can produce ringing around fine features.
float bicubicWeight(float value) {
	float absoluteValue = abs(value);
	if (absoluteValue <= 1.0)
		return 2.0 / 3.0 - absoluteValue * absoluteValue +
			0.5 * absoluteValue * absoluteValue * absoluteValue;
	if (absoluteValue < 2.0) {
		float distanceToEdge = 2.0 - absoluteValue;
		return distanceToEdge * distanceToEdge * distanceToEdge / 6.0;
	}
	return 0.0;
}

float bicubicDepth(vec2 uv, float referenceDepth) {
	vec2 pixel = uv * depthSize - vec2(0.5);
	vec2 base = floor(pixel);
	vec2 fraction = pixel - base;
	const float depthSigma = 0.06;
	float depthDenominator = 2.0 * depthSigma * depthSigma;
	float result = 0.0;
	float totalWeight = 0.0;
	for (int y = -1; y <= 2; ++y) {
		for (int x = -1; x <= 2; ++x) {
			ivec2 samplePixel = clamp(ivec2(base) + ivec2(x, y),
				ivec2(0), ivec2(depthSize) - ivec2(1));
			vec2 offset = vec2(x, y) - fraction;
			float weight = bicubicWeight(offset.x) * bicubicWeight(offset.y);
			float candidate = texture(depthTexture,
				(vec2(samplePixel) + vec2(0.5)) / depthSize).r;
			float depthDifference = candidate - referenceDepth;
			weight *= exp(-(depthDifference * depthDifference) / depthDenominator);
			result += candidate * weight;
			totalWeight += weight;
		}
	}
	return result / max(totalWeight, 0.0001);
}

void main() {
	vec3 guide = texture(colorTexture, fragUV).rgb;
	vec2 depthPixel = fragUV * depthSize - vec2(0.5);
	ivec2 center = ivec2(floor(depthPixel + vec2(0.5)));
	float spatialDenominator = 2.0 * spatialSigma * spatialSigma;
	float colorDenominator = 2.0 * colorSigma * colorSigma;
	const float depthSigma = 0.06;
	float depthDenominator = 2.0 * depthSigma * depthSigma;
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

	// Sort the small neighborhood by depth and select a continuous weighted
	// median. Interpolating at the 50% crossing avoids quantized plateaus.
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
	float localMaximum = candidates[24];
	for (int i = 0; i < 25; ++i) {
		float nextAccumulated = accumulatedWeight + weights[i];
		if (nextAccumulated >= totalWeight * 0.5) {
			float previous = i > 0 ? candidates[i - 1] : candidates[i];
			float fraction = weights[i] > 0.0
				? (totalWeight * 0.5 - accumulatedWeight) / weights[i] : 0.0;
			median = mix(previous, candidates[i], clamp(fraction, 0.0, 1.0));
			break;
		}
		accumulatedWeight = nextAccumulated;
	}

	// Reject samples from the opposite side of a large depth discontinuity.
	// This permits smoothing within one surface without allowing the smoothing
	// pass to pull foreground and background depths together.
	for (int i = 0; i < 25; ++i) {
		float depthDifference = candidates[i] - median;
		weights[i] *= exp(-(depthDifference * depthDifference) / depthDenominator);
	}

	// Re-sort is unnecessary: depth values are unchanged, only their weights
	// were gated. Recompute the continuous weighted median with the new weights.
	totalWeight = 0.0;
	for (int i = 0; i < 25; ++i) totalWeight += weights[i];
	accumulatedWeight = 0.0;
	median = candidates[24];
	for (int i = 0; i < 25; ++i) {
		float nextAccumulated = accumulatedWeight + weights[i];
		if (nextAccumulated >= totalWeight * 0.5) {
			float previous = i > 0 ? candidates[i - 1] : candidates[i];
			float fraction = weights[i] > 0.0
				? (totalWeight * 0.5 - accumulatedWeight) / weights[i] : 0.0;
			median = mix(previous, candidates[i], clamp(fraction, 0.0, 1.0));
			break;
		}
		accumulatedWeight = nextAccumulated;
	}

	// Retain the median's edge selection, then add a restrained bicubic
	// reconstruction to soften haloing and recover sub-texel smoothness.
	float smoothDepth = clamp(bicubicDepth(fragUV, median), localMinimum, localMaximum);
	float softened = mix(median, smoothDepth, 0.30);
	outColor = vec4(vec3(softened), 1.0);
}
