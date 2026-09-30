#version 450

// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D yTexture;
layout (set = 2, binding = 1) uniform sampler2D uTexture;
layout (set = 2, binding = 2) uniform sampler2D vTexture;
layout (set = 3, binding = 0) uniform VideoYUVDataFrag {
	int format;
	int colorSpace;
	int fullRange;
	int padding;
};

const int NV12 = 1;
const int BT601 = 0;
const int BT709 = 1;

void main() {
	float y = texture(yTexture, fragUV).r;
	vec2 chroma = format == NV12
		? texture(uTexture, fragUV).rg
		: vec2(texture(uTexture, fragUV).r, texture(vTexture, fragUV).r);
	if (fullRange == 0) {
		y = (y * 255.0 - 16.0) / 219.0;
		chroma = (chroma * 255.0 - 128.0) / 224.0;
	} else {
		chroma -= vec2(0.5);
	}

	vec2 krKb = colorSpace == BT601 ? vec2(0.2990, 0.1140)
		: colorSpace == BT709 ? vec2(0.2126, 0.0722)
		: vec2(0.2627, 0.0593);
	float kr = krKb.x;
	float kb = krKb.y;
	float kg = 1.0 - kr - kb;
	float cb = chroma.x;
	float cr = chroma.y;
	vec3 rgb = vec3(
		y + 2.0 * (1.0 - kr) * cr,
		y - 2.0 * kb * (1.0 - kb) / kg * cb - 2.0 * kr * (1.0 - kr) / kg * cr,
		y + 2.0 * (1.0 - kb) * cb);
	outColor = vec4(clamp(rgb, vec3(0.0), vec3(1.0)), 1.0);
}
