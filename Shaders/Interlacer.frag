#version 450

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D quiltTexture;

layout (set = 3, binding = 0) uniform InterlacerData {
	vec2 outputSize;
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
	int sourceRgbd;
	float stereoStrength;
	float stereoDepth;
	float stereoOffset;
	int reserved3;
};

vec3 sampleRgbd(vec2 uv, int view) {
	vec2 colorUV = vec2(uv.x * 0.5, uv.y);
	vec2 depthUV = vec2(uv.x * 0.5 + 0.5, uv.y);
	float depth = 1.0 - textureLod(quiltTexture, depthUV, 0.0).r;
	depth = clamp(depth, 0.05, 1.0);
	float normalizedView = viewCount > 1 ? float(view) / float(viewCount - 1) - 0.5 : 0.0;
	float parallax = normalizedView * stereoStrength * stereoDepth * 0.01 / depth + stereoOffset;
	colorUV.x = clamp(colorUV.x + parallax, 0.0, 0.5);
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
