#version 450

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D quiltTexture;
layout (set = 2, binding = 1) uniform sampler2D depthTexture;

layout (set = 3, binding = 0) uniform InterlacerData {
	vec2 outputSize;
	vec2 imageSize;
	vec2 quiltSize;
	vec2 tileSize;
	vec2 phaseScale;
	float center;
	float subpixelPhase;
	int viewCount;
	int gridColumns;
	int gridRows;
	int output2D;
	int sourceFlat;
	int invertView;
	int flipImageX;
	int flipImageY;
	int separateDepth;
	int swapLeftRight;
	int sourceRgbd;
	float stereoStrength;
	float stereoDepth;
	float stereoOffset;
	int sourceType;
	int testPattern;
};

const float stereoScaleLKG = 4000.0;
const float offsetBoostLKG = -8.0;
const float stereoScaleLenticular = 25000.0;
const float zNear = 0.1;
const float zFar = 100.0;
const float depthSamples[5] = { 0.125, 0.250, 0.375, 0.500, 0.625 };
const int sampleCount = 5;
const int Color_Anaglyph = 1;
const int Side_By_Side_Full = 3;
const int Side_By_Side_Swap = 4;
const int Side_By_Side_Half = 5;
const int Stereo_Free_View_Grid = 6;
const int Stereo_Free_View_LRL = 7;
const int Light_Field_LKG = 8;
const int Top_And_Bottom_Full = 9;
const int Top_And_Bottom_Half = 10;

float luminance(vec3 color) {
	return dot(vec3(0.30, 0.59, 0.11), color);
}

vec3 sampleMono(vec2 uv) {
	vec2 sourceUV = uv;
	if (sourceType == Color_Anaglyph) {
		vec3 color = textureLod(quiltTexture, sourceUV, 0.0).rgb;
		color = vec3(0.25, color.g * 1.5, color.b * 1.5);
		return vec3(luminance(color));
	}
	if (sourceType == Side_By_Side_Full || sourceType == Side_By_Side_Half)
		sourceUV.x *= 0.5;
	else if (sourceType == Side_By_Side_Swap)
		sourceUV.x = sourceUV.x * 0.5 + 0.5;
	else if (sourceType == Stereo_Free_View_Grid)
		sourceUV.x *= 0.5;
	else if (sourceType == Stereo_Free_View_LRL)
		sourceUV.x *= 0.333;
	else if (sourceType == Top_And_Bottom_Full || sourceType == Top_And_Bottom_Half) {
		sourceUV.y *= 0.5;
	}
	return textureLod(quiltTexture, sourceUV, 0.0).rgb;
}

vec3 sampleStereo(vec2 uv, int view) {
	if (flipImageX != 0) uv.x = 1.0 - uv.x;
	if (flipImageY != 0) uv.y = 1.0 - uv.y;
	vec2 sourceUV = clamp(uv, vec2(0.0), vec2(1.0));
	bool isRight = (view > 0);
	if (sourceType == Side_By_Side_Full || sourceType == Side_By_Side_Half) {
		sourceUV.x = isRight ? sourceUV.x * 0.5 + 0.5 : sourceUV.x * 0.5;
		return textureLod(quiltTexture, sourceUV, 0.0).rgb;
	} else if (sourceType == Side_By_Side_Swap) {
		sourceUV.x = isRight ? sourceUV.x * 0.5 : sourceUV.x * 0.5 + 0.5;
		return textureLod(quiltTexture, sourceUV, 0.0).rgb;
	} else if (sourceType == Top_And_Bottom_Full || sourceType == Top_And_Bottom_Half) {
		sourceUV.y = isRight ? sourceUV.y * 0.5 + 0.5 : sourceUV.y * 0.5;
		return textureLod(quiltTexture, sourceUV, 0.0).rgb;
	} else if (sourceType == Stereo_Free_View_Grid) {
		sourceUV.x = isRight ? sourceUV.x * 0.5 + 0.5 : sourceUV.x * 0.5;
		sourceUV.y = sourceUV.y * 0.5;
		return textureLod(quiltTexture, sourceUV, 0.0).rgb;
	} else if (sourceType == Stereo_Free_View_LRL) {
		sourceUV.x = isRight ? sourceUV.x * 0.33333333 + 0.33333333 : sourceUV.x * 0.33333333;
		return textureLod(quiltTexture, sourceUV, 0.0).rgb;
	} else if (sourceType == Color_Anaglyph) {
		vec3 color = textureLod(quiltTexture, sourceUV, 0.0).rgb;
		if (!isRight) {
			return vec3(color.r);
		} else {
			return vec3((color.g + color.b) * 0.5);
		}
	}
	return sampleMono(sourceUV);
}

float getDepth(sampler2D tex, vec2 uv) {
	float depthSample = 1.0 - (separateDepth != 0
		? texture(depthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r
		: texture(tex, clamp(uv, vec2(0.0), vec2(1.0))).r);
	float ndc = depthSample * 2.0 - 1.0;
	float linearDepth = (2.0 * zNear * zFar) /
		(zFar + zNear - ndc * (zFar - zNear));
	linearDepth /= zFar - zNear;
	return linearDepth;
}

float getParallax(float depth) {
	depth = -stereoDepth / depth;
	return depth;
}

vec2 clampEdge(vec2 inUV, vec2 minUV, vec2 maxUV) {
	const float edgeStretch = 0.333;
	if (inUV.x < minUV.x) inUV.x = (minUV.x - inUV.x) * edgeStretch;
	if (inUV.x > maxUV.x) inUV.x = maxUV.x + (maxUV.x - inUV.x) * edgeStretch;
	return clamp(inUV, minUV, maxUV);
}

// Preserve the Looking Glass view direction and convergence used before the
// two-view lenticular renderer was introduced.
vec3 sampleRgbdLightField(vec2 uv, int view) {
	vec2 colorUV = separateDepth != 0 ? uv : vec2(uv.x * 0.5, uv.y);
	vec2 depthUV = separateDepth != 0 ? uv : vec2(uv.x * 0.5 + 0.5, uv.y);
	float normalizedView = viewCount > 1 ? float(view) / float(viewCount - 1) - 0.5 : 0.0;
	float viewAmount = abs(normalizedView) * 2.0;
	float aspect = imageSize.x / imageSize.y;
	vec2 minUVDepth = separateDepth != 0 ? vec2(0.001, 0.0) : vec2(0.501, 0.0);
	vec2 maxUVDepth = separateDepth != 0 ? vec2(0.999, 1.0) : vec2(0.999, 1.0);
	float centerDepth = getDepth(quiltTexture, clampEdge(depthUV, minUVDepth, maxUVDepth));
	float minDepth = centerDepth;
	float offsetSign = normalizedView < 0.0 ? 1.0 : -1.0;
	for (int i = 0; i < sampleCount; ++i) {
		float depthOffset = (depthSamples[i] * stereoStrength * viewAmount / aspect) /
			stereoScaleLKG + stereoOffset * offsetBoostLKG * viewAmount / aspect;
		minDepth = min(minDepth, getDepth(quiltTexture, clampEdge(
			depthUV + vec2(offsetSign * depthOffset, 0.0), minUVDepth, maxUVDepth)));
	}
	float parallax = (stereoStrength * viewAmount / aspect * (stereoDepth / minDepth)) /
		stereoScaleLKG + stereoOffset * offsetBoostLKG * viewAmount / aspect;
	colorUV = clampEdge(colorUV + vec2(offsetSign * parallax, 0.0),
		vec2(0.001, 0.0), separateDepth != 0 ? vec2(0.999, 1.0) : vec2(0.499, 1.0));
	return textureLod(quiltTexture, colorUV, 0.0).rgb;
}

vec3 sampleRgbd(vec2 uv, int view) {
	if (viewCount > 2) return sampleRgbdLightField(uv, view);
	vec2 colorUV = separateDepth != 0 ? uv : vec2(uv.x * 0.5, uv.y);
	vec2 depthUV = separateDepth != 0 ? uv : vec2(uv.x * 0.5 + 0.5, uv.y);
	float normalizedView = viewCount > 1 ? (float(view) / float(viewCount - 1) - 0.5) * 2.0 : 0.0;
	float aspect = imageSize.x / imageSize.y;
	vec2 minUVDepth = separateDepth != 0 ? vec2(0.001, 0.0) : vec2(0.501, 0.0);
	vec2 maxUVDepth = separateDepth != 0 ? vec2(0.999, 1.0) : vec2(0.999, 1.0);
	float centerDepth = getDepth(quiltTexture, clampEdge(depthUV, minUVDepth, maxUVDepth));
	float minDepth = centerDepth;
	float stereoScale = stereoScaleLenticular;
	for (int i = 0; i < sampleCount; ++i) {
		float depthOffset = (depthSamples[i] * stereoStrength / aspect) /
			stereoScale + stereoOffset / aspect;
		minDepth = min(minDepth, getDepth(quiltTexture, clampEdge(
			depthUV - vec2(normalizedView * depthOffset, 0.0), minUVDepth, maxUVDepth)));
	}
	float parallax = (stereoStrength / aspect * getParallax(minDepth)) /
		stereoScale + stereoOffset / aspect;
	colorUV = clampEdge(colorUV - vec2(normalizedView * parallax, 0.0),
		vec2(0.001, 0.0), separateDepth != 0 ? vec2(0.999, 1.0) : vec2(0.499, 1.0));
	return textureLod(quiltTexture, colorUV, 0.0).rgb;
}

vec3 sampleTestPattern(vec2 uv, int view) {
	if (testPattern == 1) {
		return (view == 0) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 1.0);
	} else if (testPattern == 2) {
		return (view == 0) ? vec3(1.0, 1.0, 1.0) : vec3(0.0, 0.0, 0.0);
	} else if (testPattern == 3) {
		return (view == 0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);
	} else if (testPattern == 4) {
		vec2 grid = fract(uv * 20.0);
		float line = (grid.x < 0.04 || grid.y < 0.04) ? 1.0 : 0.0;
		vec3 baseColor = (view == 0) ? vec3(0.85, 0.15, 0.15) : vec3(0.15, 0.55, 0.85);
		return mix(baseColor, vec3(1.0), line);
	}
	return vec3(0.0);
}

vec3 sampleView(vec2 uv, float phase) {
	int view = int(floor(fract(phase) * float(viewCount)));
	view = clamp(view, 0, viewCount - 1);
	if (invertView != 0) view = viewCount - 1 - view;
	if (swapLeftRight != 0) view = viewCount - 1 - view;
	if (testPattern != 0) {
		return sampleTestPattern(uv, view);
	}
	if (output2D != 0) {
		if (sourceRgbd != 0)
			return textureLod(quiltTexture, separateDepth != 0 ? uv :
				vec2(uv.x * 0.5, uv.y), 0.0).rgb;
		if (sourceFlat != 0)
			return sampleMono(uv);
	}
	if (sourceRgbd != 0) return sampleRgbd(uv, view);
	if (sourceType == Light_Field_LKG) {
		int targetView = view;
		if (output2D != 0) {
			targetView = viewCount / 2;
		} else {
			targetView = viewCount - 1 - view;
		}
		int column = targetView % gridColumns;
		int row = gridRows - 1 - targetView / gridColumns;
		if (flipImageX != 0) uv.x = 1.0 - uv.x;
		if (flipImageY != 0) uv.y = 1.0 - uv.y;
		vec2 quiltUV = (vec2(column, row) + clamp(uv, vec2(0.0), vec2(1.0))) * tileSize;
		return textureLod(quiltTexture, quiltUV, 0.0).rgb;
	}
	if (output2D != 0 || sourceFlat != 0) {
		return sampleMono(uv);
	}
	return sampleStereo(uv, view);
}

void main() {
	vec2 pixel = gl_FragCoord.xy;
	pixel.y = outputSize.y - pixel.y;
	float basePhase = dot(pixel, phaseScale);
	basePhase -= center;
	vec3 color;
	color.r = sampleView(fragUV, basePhase - subpixelPhase).r;
	color.g = sampleView(fragUV, basePhase).g;
	color.b = sampleView(fragUV, basePhase + subpixelPhase).b;
	outColor = vec4(color, 1.0);
}
