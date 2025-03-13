#version 450

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

layout (location = 0) in vec2 fragUV;
layout (location = 0) out vec4 outColor;
layout (set = 2, binding = 0) uniform sampler2D spriteTexture;
layout (set = 3, binding = 0) uniform SpriteDataFrag {
	vec4 color;
	float visibility;
	int useTexture;
	vec2 slice;
};

void main() {
	float alpha = 1.0;
	vec4 spriteColor = vec4(1.0);
	if (useTexture > 0) {
		vec2 sliceUV = fragUV;
		if (fragUV.x < slice.x){
			sliceUV.x = sliceUV.x / slice.x * 0.5;
		} else if (fragUV.x > slice.y) {
			sliceUV.x = (sliceUV.x - slice.y) / slice.x * 0.5 + 0.5;
		} else {
			sliceUV.x = 0.5;
		}
		spriteColor *= texture(spriteTexture, sliceUV);
	}
	if (spriteColor.a > 0) spriteColor.rgb /= spriteColor.a;
	alpha = clamp(color.a * visibility * alpha, 0.0, 1.0);
	spriteColor.rgb *= color.rgb;
	spriteColor.a *= alpha;
	outColor = spriteColor;
}
