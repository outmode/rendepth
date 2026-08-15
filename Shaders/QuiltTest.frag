#version 450

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;

layout (set = 3, binding = 0) uniform QuiltTestData {
	vec2 outputSize;
	vec2 phaseScale;
	float center;
	float subpixelPhase;
	int viewCount;
	int pattern;
	int phaseOrigin;
	int invertView;
	int expectedWidth;
	int expectedHeight;
};

vec2 phasePixel() {
	vec2 pixel = gl_FragCoord.xy;
	if ((phaseOrigin & 1) != 0) pixel.y = outputSize.y - pixel.y;
	if ((phaseOrigin & 2) != 0) pixel.x = outputSize.x - pixel.x;
	return pixel;
}

float phaseAt(float subpixelOffset) {
	return dot(phasePixel(), phaseScale) - center + subpixelOffset;
}

int viewAt(float phase, int count) {
	int view = clamp(int(floor(fract(phase) * float(count))), 0, count - 1);
	return invertView != 0 ? count - 1 - view : view;
}

vec3 lowViewBits(int view) {
	return vec3(
		float((view >> 0) & 1),
		float((view >> 1) & 1),
		float((view >> 2) & 1));
}

vec3 highViewBits(int view) {
	return vec3(
		float((view >> 3) & 1),
		float((view >> 4) & 1),
		float((view >> 5) & 1));
}

float proceduralScene(vec2 uv, int view, float parallaxScale) {
	float viewPosition = float(view) / float(max(viewCount - 1, 1)) - 0.5;

	float frame = 0.0;
	frame = max(frame, 1.0 - smoothstep(0.004, 0.007, abs(uv.x - 0.08)));
	frame = max(frame, 1.0 - smoothstep(0.004, 0.007, abs(uv.x - 0.92)));
	frame = max(frame, 1.0 - smoothstep(0.004, 0.007, abs(uv.y - 0.10)));
	frame = max(frame, 1.0 - smoothstep(0.004, 0.007, abs(uv.y - 0.90)));

	float rearX = 0.50 - viewPosition * 0.055 * parallaxScale;
	float rearBar = 1.0 - smoothstep(0.055, 0.060, abs(uv.x - rearX));
	rearBar *= 1.0 - smoothstep(0.31, 0.32, abs(uv.y - 0.50));

	float frontX = 0.50 + viewPosition * 0.20 * parallaxScale;
	vec2 circleDelta = (uv - vec2(frontX, 0.50)) / vec2(1.0, 0.72);
	float frontCircle = 1.0 - smoothstep(0.145, 0.152, length(circleDelta));

	float centerDot = 1.0 - smoothstep(0.012, 0.017,
		length((uv - vec2(frontX, 0.50)) / vec2(1.0, 0.72)));
	return clamp(frame * 0.20 + rearBar * 0.48 + frontCircle * 0.88 +
		centerDot * 0.12, 0.0, 1.0);
}

vec3 pixelIntegrityPattern(ivec2 pixel) {
	if (outputSize.x != float(expectedWidth) || outputSize.y != float(expectedHeight)) {
		bool grid = pixel.x % 32 == 0 || pixel.y % 32 == 0;
		return grid ? vec3(1.0) : vec3(0.65, 0.0, 0.0);
	}
	bool border = pixel.x < 4 || pixel.y < 4 ||
		pixel.x >= expectedWidth - 4 || pixel.y >= expectedHeight - 4;
	if (border) return vec3(1.0);
	bool checker = ((pixel.x / 32) + (pixel.y / 32)) % 2 == 0;
	return checker ? vec3(0.12) : vec3(0.88);
}

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	if (pattern == 0) {
		outColor = vec4(pixelIntegrityPattern(pixel), 1.0);
		return;
	}
	if (pattern == 1) {
		int channel = pixel.x % 3;
		outColor = vec4(
			channel == 0 ? 1.0 : 0.0,
			channel == 1 ? 1.0 : 0.0,
			channel == 2 ? 1.0 : 0.0,
			1.0);
		return;
	}
	if (pattern == 2) {
		int eye = viewAt(phaseAt(0.0), 2);
		outColor = eye == 0
			? vec4(1.0, 0.0, 0.0, 1.0)
			: vec4(0.0, 1.0, 0.0, 1.0);
		return;
	}
	if (pattern == 3) {
		outColor = vec4(lowViewBits(viewAt(phaseAt(0.0), viewCount)), 1.0);
		return;
	}
	if (pattern == 4) {
		outColor = vec4(highViewBits(viewAt(phaseAt(0.0), viewCount)), 1.0);
		return;
	}
	if (pattern == 5) {
		vec3 redView = lowViewBits(viewAt(phaseAt(-subpixelPhase), viewCount));
		vec3 greenView = lowViewBits(viewAt(phaseAt(0.0), viewCount));
		vec3 blueView = lowViewBits(viewAt(phaseAt(subpixelPhase), viewCount));
		outColor = vec4(redView.r, greenView.g, blueView.b, 1.0);
		return;
	}

	if (pattern == 6) {
		bool right = fragUV.x >= 0.5;
		bool bottom = fragUV.y >= 0.5;
		vec3 color = !right && !bottom ? vec3(1.0, 0.0, 0.0) :
			right && !bottom ? vec3(0.0, 1.0, 0.0) :
			!right && bottom ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 1.0, 0.0);
		outColor = vec4(color, 1.0);
		return;
	}

	float parallaxScale = pattern == 7 ? 1.0 : pattern == 8 ? 0.30 : 0.0;

	int redView = viewAt(phaseAt(-subpixelPhase), viewCount);
	int greenView = viewAt(phaseAt(0.0), viewCount);
	int blueView = viewAt(phaseAt(subpixelPhase), viewCount);
	outColor = vec4(
		proceduralScene(fragUV, redView, parallaxScale),
		proceduralScene(fragUV, greenView, parallaxScale),
		proceduralScene(fragUV, blueView, parallaxScale),
		1.0);
}
