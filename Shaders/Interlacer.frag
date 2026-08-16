#version 450

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D quiltTexture;

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
	int reserved2;
	int swapLeftRight;
	int sourceRgbd;
	float stereoStrength;
	float stereoDepth;
	float stereoOffset;
	int reserved3;
};

const float stereoScale = 4000.0;
const float offsetBoost = -8.0;
const float zNear = 0.1;
const float zFar = 100.0;
const float depthSamples[5] = { 0.125, 0.250, 0.375, 0.500, 0.625 };
const int sampleCount = 5;

float getDepth(sampler2D tex, vec2 uv) {
	float depthSample = 1.0 - texture(tex, clamp(uv, vec2(0.0), vec2(1.0))).r;
	float ndc = depthSample * 2.0 - 1.0;
	float linearDepth = (2.0 * zNear * zFar) /
		(zFar + zNear - ndc * (zFar - zNear));
	linearDepth /= zFar - zNear;
	return linearDepth;
}

float getParallax(float depth) {
	depth = stereoDepth / depth;
	return depth;
}

vec2 clampEdge(vec2 inUV, vec2 minUV, vec2 maxUV) {
	const float edgeStretch = 0.333;
	if (inUV.x < minUV.x) inUV.x = (minUV.x - inUV.x) * edgeStretch;
	if (inUV.x > maxUV.x) inUV.x = maxUV.x + (maxUV.x - inUV.x) * edgeStretch;
	return clamp(inUV, minUV, maxUV);
}

vec3 sampleRgbd(vec2 uv, int view) {
	vec2 colorUV = vec2(uv.x * 0.5, uv.y);
	vec2 depthUV = vec2(uv.x * 0.5 + 0.5, uv.y);
	float normalizedView = viewCount > 1 ? float(view) / float(viewCount - 1) - 0.5 : 0.0;
	float viewAmount = abs(normalizedView) * 2.0;
	float aspect = imageSize.x / imageSize.y;
	vec2 minUVDepth = vec2(0.501, 0.0);
	vec2 maxUVDepth = vec2(0.999, 1.0);
	float centerDepth = getDepth(quiltTexture, clampEdge(depthUV, minUVDepth, maxUVDepth));
	float minDepth = centerDepth;
	float offsetSign = normalizedView < 0.0 ? 1.0 : -1.0;
	for (int i = 0; i < sampleCount; ++i) {
		float depthOffset = (depthSamples[i] * stereoStrength * viewAmount / aspect) /
			stereoScale + stereoOffset * offsetBoost * viewAmount / aspect;
		minDepth = min(minDepth, getDepth(quiltTexture, clampEdge(
			depthUV + vec2(offsetSign * depthOffset, 0.0), minUVDepth, maxUVDepth)));
	}
	float parallax = (stereoStrength * viewAmount / aspect * getParallax(minDepth)) /
		stereoScale + stereoOffset * offsetBoost * viewAmount / aspect;
	colorUV = clampEdge(colorUV + vec2(offsetSign * parallax, 0.0),
		vec2(0.001, 0.0), vec2(0.499, 1.0));
	return textureLod(quiltTexture, colorUV, 0.0).rgb;
}

vec3 sampleView(vec2 uv, float phase) {
	if (output2D != 0) {
		if (sourceRgbd != 0)
			return textureLod(quiltTexture, vec2(uv.x * 0.5, uv.y), 0.0).rgb;
		if (sourceFlat != 0)
			return textureLod(quiltTexture, uv, 0.0).rgb;
	}
	int view = int(floor(fract(phase) * float(viewCount)));
	view = clamp(view, 0, viewCount - 1);
	if (invertView != 0) view = viewCount - 1 - view;
	if (swapLeftRight != 0) view = viewCount - 1 - view;
	if (sourceRgbd != 0) return sampleRgbd(uv, view);
	if (output2D != 0) {
		view = viewCount / 2;
	} else {
		view = viewCount - 1 - view;
	}
	int column = view % gridColumns;
	int row = gridRows - 1 - view / gridColumns;
	if (flipImageX != 0) uv.x = 1.0 - uv.x;
	if (flipImageY != 0) uv.y = 1.0 - uv.y;
	vec2 quiltUV = (vec2(column, row) + clamp(uv, vec2(0.0), vec2(1.0))) * tileSize;
	return textureLod(quiltTexture, quiltUV, 0.0).rgb;
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
