#version 450

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

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D imageTexture;
layout (set = 2, binding = 1) uniform sampler2D blurTexture;
layout (set = 2, binding = 2) uniform sampler2D blurTextureNext;
layout (set = 2, binding = 3) uniform sampler2D depthTexture;
layout (set = 3, binding = 0) uniform ImageDataFrag {
	vec2 windowSize;
	vec2 imageSize;
	vec3 gridSize;
	float visibility;
	int blur;
	int mode;
	int type;
	float stereoStrength;
	float stereoDepth;
	float stereoOffset;
	float gridAngle;
	float depthEffect;
	int effectRandom;
	int swapLeftRight;
	int force;
	float blurMix;
	int separateDepth;
	int packedOutput;
	int depthPadding1;
	int depthPadding2;
};

#define Disabled 0
#define Enabled 1

#define Native 0
#define Mono 1
#define Left 2
#define Right 3
#define Anaglyph_Accurate 4
#define Anaglyph_Vivid 5
#define RGB_Depth 6
#define SBS_Full 7
#define SBS_Half 8
#define Free_View_Grid 9
#define Free_View_LRL 10
#define Horizontal 11
#define Vertical 12
#define Checkerboard 13
#define Depth_Zoom 14
#define Light_Field 15

#define Color_Only 0
#define Color_Anaglyph 1
#define Color_Plus_Depth 2
#define Side_By_Side_Full 3
#define Side_By_Side_Swap 4
#define Side_By_Side_Half 5
#define Stereo_Free_View_Grid 6
#define Stereo_Free_View_LRL 7
#define Light_Field_LKG 8
#define Top_And_Bottom_Full 9
#define Top_And_Bottom_Half 10

const float stereoScale = 25000.0;
const float zNear = 0.1;
const float zFar = 100.0;
const float depthSamples[5] = { 0.125, 0.250, 0.375, 0.500, 0.625 };
const int sampleCount = 5;
const mat3 accurateLeftFilter = mat3(
	vec3(0.439, 0.447, 0.148),
	vec3(0.0, 0.0, 0.0),
	vec3(0.0, 0.0, 0.0));
const mat3 accurateRightFilter = mat3(
	vec3(0.0, 0.0, 0.0),
	vec3(0.095, 0.934, -0.005),
	vec3(-0.018, -0.028, 1.057));
const mat3 vividLeftFilter = mat3(
    vec3(0.4561, 0.500484, 0.176381),
    vec3(-0.400822, -0.0378246, -0.0157589),
    vec3(-0.0152161, -0.0205971, -0.00546856));
const mat3 vividRightFilter = mat3(
    vec3(-0.0434706, -0.0879388, -0.00155529),
    vec3(0.378476, 0.73364, -0.0184503),
    vec3(-0.0721527, -0.112961, 1.2264));
const vec3 gammaMap = vec3(1.10, 1.05, 1.05);
const int blurRange = 2;
const float blurOffsets[5] = float[5](0.0, 1.0, 3.0, 7.0, 16.0);
const float uvGutter = 0.001;
const vec2 minGutterUV = vec2(uvGutter, 0.0);
const vec2 maxGutterUV = vec2(1.0 - uvGutter, 1.0);
const vec2 minUVColor = minGutterUV;
const vec2 maxUVColor = vec2(maxGutterUV.x - 0.5, maxGutterUV.y);
const vec2 minUVDepth = vec2(minGutterUV.x + 0.5, minGutterUV.y);
const vec2 maxUVDepth = maxGutterUV;

vec3 fromLinear(vec3 linearRGB) {
	bvec3 cutoff = lessThan(linearRGB.rgb, vec3(0.0031308));
	vec3 higher = vec3(1.055) * pow(linearRGB.rgb, vec3(1.0 / 2.4)) - vec3(0.055);
	vec3 lower = linearRGB.rgb * vec3(12.92);

	return mix(higher, lower, cutoff);
}

vec3 toLinear(vec3 sRGB) {
	bvec3 cutoff = lessThan(sRGB.rgb, vec3(0.04045));
	vec3 higher = pow(sRGB.rgb + vec3(0.055) / vec3(1.055), vec3(2.4));
	vec3 lower = sRGB.rgb/vec3(12.92);

	return mix(higher, lower, cutoff);
}

vec4 getColor(sampler2D tex, vec2 uv) {
	vec4 color = texture(tex, clamp(uv, vec2(0.0), vec2(1.0))).rgba;
	return color;
}

float getRawDepth(sampler2D tex, vec2 uv) {
	if (separateDepth == 1)
		return texture(depthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r;
	return texture(tex, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float getDepth(sampler2D tex, vec2 uv) {
	float depthSample = 1.0 - getRawDepth(tex, uv);
	float ndc = depthSample * 2.0 - 1.0;
	float linearDepth = (2.0 * zNear * zFar) / (zFar + zNear - ndc * (zFar - zNear));
	linearDepth /= zFar - zNear;
	return linearDepth;
}

float getParallax(float depth) {
	depth = -stereoDepth / depth;
	return depth;
}

vec3 correctColor(vec3 original) {
	vec3 corrected;
	corrected.r = pow(original.r, 1.0 / gammaMap.r);
	corrected.g = pow(original.g, 1.0 / gammaMap.g);
	corrected.b = pow(original.b, 1.0 / gammaMap.b);
	return corrected;
}

vec3 uncorrectColor(vec3 original) {
	vec3 corrected;
	corrected.r = pow(original.r, gammaMap.r);
	corrected.g = pow(original.g, gammaMap.g);
	corrected.b = pow(original.b, gammaMap.b);
	return corrected;
}

vec3 blurImage(vec2 uv) {
	vec3 result = vec3(0.0);
	float totalWeight = 0.0;
	float snapshotMix = smoothstep(0.0, 1.0, blurMix);
	for (int y = -blurRange; y <= blurRange; y++) {
		for (int x = -blurRange; x <= blurRange; x++) {
			vec2 offsetUV = vec2(blurOffsets[abs(x)], blurOffsets[abs(y)]) / vec2(200.0);
			totalWeight += 1.0;
			vec3 currentSample = textureLod(blurTexture, uv + offsetUV, 3.0).rgb;
			vec3 nextSample = textureLod(blurTextureNext, uv + offsetUV, 3.0).rgb;
			result += mix(currentSample, nextSample, snapshotMix);
		}
	}
	result /= totalWeight;
	return result;
}

vec3 combineStereoViews(vec3 leftColor, vec3 rightColor) {
	vec3 result = vec3(1.0);
	ivec2 currentPixel = ivec2(gl_FragCoord);
	if (swapLeftRight == 1) {
		vec3 tempColor = leftColor;
		leftColor = rightColor;
		rightColor = tempColor;
	}
	if (mode == Anaglyph_Accurate || mode == Anaglyph_Vivid) {
		mat3 leftFilter = mode == Anaglyph_Vivid ? vividLeftFilter : accurateLeftFilter;
		mat3 rightFilter = mode == Anaglyph_Vivid ? vividRightFilter : accurateRightFilter;
		result = clamp(leftColor * leftFilter, vec3(0.0), vec3(1.0)) + clamp(rightColor * rightFilter, vec3(0.0), vec3(1.0));
		result = correctColor(result);
	} else if (mode == Left) {
		result = leftColor;
	} else if (mode == Right) {
		result = rightColor;
	} else if (mode == RGB_Depth) {
		result = leftColor;
	} else if (mode == Horizontal) {
		if (currentPixel.y % 2 == 0) result = leftColor;
		else result = rightColor;
	} else if (mode == Vertical) {
		if (currentPixel.x % 2 == 0) result = leftColor;
		else result = rightColor;
	} else if (mode == Checkerboard) {
		if (currentPixel.x % 2 == 0 && currentPixel.y % 2 == 0) result = leftColor;
		else if (currentPixel.x % 2 == 1 && currentPixel.y % 2 == 1) result = leftColor;
		else result = rightColor;
	}
	return result;
}

vec2 clampEdge(vec2 inUV, vec2 minUV, vec2 maxUV) {
	const float edgeStretch = 0.333;
	if (inUV.x < minUV.x) inUV.x = (minUV.x - inUV.x) * edgeStretch;
	if (inUV.x > maxUV.x) inUV.x = maxUV.x + (maxUV.x - inUV.x) * edgeStretch;
	return clamp(inUV, minUV, maxUV);
}

vec3 generateStereoImage(vec2 inUV) {
	vec2 uv = vec2(0.0);
	vec2 screenUV = inUV;
	vec2 colorUV = separateDepth == 1 ? screenUV : vec2(screenUV.x * 0.5, screenUV.y);
	vec2 depthUV = separateDepth == 1 ? screenUV : vec2(screenUV.x * 0.5 + 0.5, screenUV.y);
	vec2 colorMin = separateDepth == 1 ? minGutterUV : minUVColor;
	vec2 colorMax = separateDepth == 1 ? maxGutterUV : maxUVColor;
	vec2 depthMin = separateDepth == 1 ? minGutterUV : minUVDepth;
	vec2 depthMax = separateDepth == 1 ? maxGutterUV : maxUVDepth;

	float centerDepth = getDepth(imageTexture, clampEdge(depthUV, depthMin, depthMax));
	float minDepthLeft = centerDepth;
	float minDepthRight = centerDepth;

	float aspect = imageSize.x / imageSize.y;

	for (int i = 0; i < sampleCount; ++i) {
		uv.x = (depthSamples[i] * stereoStrength / aspect) / stereoScale + stereoOffset / aspect;
		minDepthLeft = min(minDepthLeft, getDepth(imageTexture, clampEdge(depthUV + uv, depthMin, depthMax)));
		minDepthRight = min(minDepthRight, getDepth(imageTexture, clampEdge(depthUV - uv, depthMin, depthMax)));
	}

	float parallaxLeft = (stereoStrength / aspect * getParallax(minDepthLeft)) / stereoScale + stereoOffset / aspect;
	float parallaxRight = (stereoStrength / aspect * getParallax(minDepthRight)) / stereoScale + stereoOffset / aspect;

	vec3 colorLeft = getColor(imageTexture,
		clampEdge(colorUV + vec2(parallaxLeft, 0.0), colorMin, colorMax)).rgb;
	vec3 colorRight = getColor(imageTexture,
		clampEdge(colorUV - vec2(parallaxRight, 0.0), colorMin, colorMax)).rgb;

	return combineStereoViews(colorLeft, colorRight);
}

vec2 effectZoom(vec2 uv, vec2 depthUV) {
	float parallax = (depthEffect * 1.2 - 0.8) * 0.1;
	float depth = getRawDepth(imageTexture, depthUV);
	vec2 dir = uv - 0.5;
	dir.x *= 0.5;
	int samples = 3;
	vec2 offset = (dir * parallax) / samples;
	vec2 result = depthUV;
	while (samples-- > 0) {
		depth = min(depth, getRawDepth(imageTexture, result));
		result -= depth * offset;
	}
	return separateDepth == 1 ? result : result * vec2(2.0, 1.0) - vec2(1.0, 0.0);
}

vec2 effectDolly(vec2 uv, vec2 depthUV) {
	float parallax = (depthEffect * 1.2 - 0.8) * 0.1;
	float depth = 1.0 - getRawDepth(imageTexture, depthUV);
	vec2 dir = uv - 0.5;
	dir.x *= 0.5;
	int samples = 3;
	vec2 offset = (dir * parallax) / samples;
	vec2 result = depthUV;
	while (samples-- > 0) {
		depth = min(depth, 1.0 - getRawDepth(imageTexture, result));
		result += depth * offset;
	}
	return separateDepth == 1 ? result : result * vec2(2.0, 1.0) - vec2(1.0, 0.0);
}

float luminance(vec3 color) {
	return dot(vec3(0.30, 0.59, 0.11), color);
}

vec3 getAnaglyphGrayscale(vec3 color) {
	color.rgb = vec3(0.25, color.g * 1.5, color.b * 1.5);
	return vec3(luminance(color.rgb));
}

vec3 getAnaglyphLeft(vec3 color) {
	return vec3(color.r, 0.0, 0.0);
}

vec3 getAnaglyphRight(vec3 color) {
	return vec3(0.0, color.g, color.b);
}

int getQuiltRow(int row, int column, int view) {
	if (type == Light_Field_LKG) {
		return row - 1 - view / column;
	}
	return 0;
}

void main() {
	vec2 monoUV = vec2(fragUV.x * 0.5, fragUV.y);
	vec2 depthUV = vec2(monoUV.x + 0.5, monoUV.y);
	vec4 imageColor = vec4(vec3(0.0), 1.0);
	vec3 clearColor = vec3(0.1, 0.1, 0.1);
	vec2 gridLeftUV = vec2(0.0);
	vec2 gridRightUV = vec2(0.0);
	if (type == Color_Anaglyph) {
		monoUV = fragUV;
	} else if (type == Side_By_Side_Swap) {
		vec2 tempUV = monoUV;
		monoUV = depthUV;
		depthUV = tempUV;
	} else if (type == Top_And_Bottom_Full || type == Top_And_Bottom_Half) {
		monoUV = vec2(fragUV.x, fragUV.y * 0.5);
		depthUV = vec2(monoUV.x, monoUV.y + 0.5);
	} else if (type == Stereo_Free_View_Grid) {
		monoUV = fragUV * 0.5;
		depthUV = vec2(monoUV.x + 0.5, monoUV.y);
	} else if (type == Stereo_Free_View_LRL) {
		monoUV = vec2(fragUV.x * 0.333, fragUV.y);
		depthUV = vec2(monoUV.x + 0.333, monoUV.y);
	} else if (type == Light_Field_LKG) {
		gridLeftUV = vec2(fragUV.x / gridSize.x, fragUV.y / gridSize.y);
		gridRightUV = gridLeftUV;
		vec2 gridCenterUV = gridLeftUV;
		float gridMax = 5.0;
		int gridStereo = int(stereoStrength * gridMax);
		int gridCol = int(gridSize.x);
		int gridRow = int(gridSize.y);
		int gridCenter = (gridCol * gridRow) / 2;
		int gridSlide = int((gridAngle - 0.5) * float(gridCenter - gridStereo));
		vec2 gridScale = vec2(1.0 / gridSize.x, 1.0 / gridSize.y);
		int gridLeft = gridCenter - gridStereo + gridSlide;
		vec2 gridOffset = vec2(gridLeft % gridCol, getQuiltRow(gridRow, gridCol, gridLeft));
		gridLeftUV += gridScale * gridOffset;
		int gridRight = gridCenter + gridStereo + gridSlide;
		gridOffset = vec2(gridRight % gridCol, getQuiltRow(gridRow, gridCol, gridRight));
		gridRightUV += gridScale * gridOffset;
		gridOffset = vec2(gridCenter % gridCol, getQuiltRow(gridRow, gridCol, gridCenter));
		gridCenterUV += gridScale * gridOffset;
		monoUV = gridCenterUV;
	}
	if (separateDepth == 1) {
		monoUV = fragUV;
		depthUV = fragUV;
	}
	if (blur == Enabled) {
		imageColor.rgb = blurImage(monoUV);
		imageColor.rgb = mix(clearColor, imageColor.rgb, 0.9);
		if (mode == Anaglyph_Accurate || mode == Anaglyph_Vivid) {
			mat3 leftFilter = mode == Anaglyph_Vivid ? vividLeftFilter : accurateLeftFilter;
			mat3 rightFilter = mode == Anaglyph_Vivid ? vividRightFilter : accurateRightFilter;
			imageColor.rgb = clamp(imageColor.rgb * leftFilter, vec3(0.0), vec3(1.0)) +
				clamp(imageColor.rgb * rightFilter, vec3(0.0), vec3(1.0));
		} else if (mode == Mono && type == Color_Anaglyph) {
			imageColor.rgb = getAnaglyphGrayscale(imageColor.rgb);
		} else if (mode == RGB_Depth) {
			imageColor.rgb = vec3(0.5);
		}
		imageColor.a = 1.0;
	} else if (mode == Native) {
		if (packedOutput == 1 && type == Color_Plus_Depth && separateDepth == 1) {
			// Inferred video depth is stored separately from the RGB texture. The
			// exported RGB-D image still needs the packed layout used by embedded
			// RGB-D images: full-resolution color on the left and depth on the
			// right, rather than stretching the color texture across both halves.
			if (fragUV.x < 0.5) {
				imageColor = getColor(imageTexture,
					vec2(fragUV.x * 2.0, fragUV.y));
			} else {
				const vec2 packedDepthUV = vec2((fragUV.x - 0.5) * 2.0, fragUV.y);
				const float depthValue = getRawDepth(depthTexture, packedDepthUV);
				imageColor = vec4(vec3(depthValue), 1.0);
			}
		} else {
			imageColor = getColor(imageTexture, fragUV);
		}
	} else if (mode == Light_Field) {
		if (type == Color_Plus_Depth) {
			imageColor = getColor(imageTexture, separateDepth == 1 ? fragUV :
				vec2(fragUV.x * 0.5, fragUV.y));
		} else if (type == Light_Field_LKG && gridSize.x > 0.0 && gridSize.y > 0.0) {
			int columns = int(gridSize.x);
			int rows = int(gridSize.y);
			int centerView = (columns * rows) / 2;
			vec2 tile = vec2(centerView % columns, rows - 1 - centerView / columns);
			imageColor = getColor(imageTexture, (tile + fragUV) / gridSize.xy);
		} else {
			imageColor = getColor(imageTexture, fragUV);
		}
	} else if (mode == Mono) {
		imageColor = getColor(imageTexture, monoUV);
		if (type == Color_Anaglyph) {
			imageColor.rgb = getAnaglyphGrayscale(imageColor.rgb);
		}
	} else if (mode == RGB_Depth) {
		if (separateDepth == 1) {
			// Video depth is uploaded as a single-channel R16 texture. Replicate
			// it across RGB so the depth preview remains grayscale like embedded
			// image depth.
			float depthValue = getRawDepth(depthTexture, depthUV);
			imageColor = vec4(vec3(depthValue), 1.0);
		} else {
			imageColor = getColor(imageTexture, depthUV);
		}
		if (force == 1) imageColor.rgb = vec3(1.0);
	} else if (mode == Depth_Zoom) {
		vec2 zoomFragUV = fragUV * 0.95 + 0.025;
		vec2 zoomMonoUV = separateDepth == 1 ? zoomFragUV : vec2(zoomFragUV.x * 0.5, zoomFragUV.y);
		vec2 zoomDepthUV = separateDepth == 1 ? zoomFragUV : vec2(zoomMonoUV.x + 0.5, zoomMonoUV.y);
		vec2 zoomUV = vec2(0.0);
		if (effectRandom == 0) zoomUV = effectZoom(zoomFragUV, zoomDepthUV);
		else zoomUV = effectDolly(zoomFragUV, zoomDepthUV);
		if (separateDepth != 1) zoomUV.x *= 0.5;
		zoomUV = clamp(zoomUV, vec2(0.0, 0.0),
			separateDepth == 1 ? vec2(1.0) : vec2(0.495, 1.0));
		imageColor.rgb = getColor(imageTexture, zoomUV).rgb;
	} else {
		if (type == Color_Plus_Depth) {
			imageColor.rgb = generateStereoImage(fragUV);
		} else if (type == Color_Anaglyph) {
			imageColor.rgb = getColor(imageTexture, fragUV).rgb;
			if (mode != Anaglyph_Accurate && mode != Anaglyph_Vivid) {
				vec3 leftColor = getAnaglyphLeft(imageColor.rgb);
				vec3 rightColor = getAnaglyphRight(imageColor.rgb);
				imageColor.rgb = combineStereoViews(leftColor, rightColor);
			}
		} else if (type == Side_By_Side_Full || type == Side_By_Side_Half ||
				type == Side_By_Side_Swap || type == Stereo_Free_View_Grid ||
				type == Stereo_Free_View_LRL ||
				type == Top_And_Bottom_Full || type == Top_And_Bottom_Half) {
			vec3 leftColor = getColor(imageTexture, monoUV).rgb;
			vec3 rightColor = getColor(imageTexture, depthUV).rgb;
			imageColor.rgb = combineStereoViews(leftColor, rightColor);
		} else if (type == Light_Field_LKG) {
			vec3 leftColor = getColor(imageTexture, gridLeftUV).rgb;
			vec3 rightColor = getColor(imageTexture, gridRightUV).rgb;
			imageColor.rgb = combineStereoViews(leftColor, rightColor);
		} else {
			imageColor.rgb = vec3(1.0, 0.2, 0.2);
		}
	}
	imageColor.a = clamp(imageColor.a * visibility, 0.0, 1.0);
	outColor = imageColor;
}
