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

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec2 inUV;
layout (location = 0) out vec2 fragUV;
layout (set = 1, binding = 0) uniform ImageDataVertex {
	mat4 transform;
	mat4 projection;
	vec3 displayImageAspect;
	int fillScreen;
};

void main() {
	fragUV = inUV;
	vec3 aspectRatio = vec3(1.0, 1.0, 1.0);
	float displayAspect = displayImageAspect.x;
	float imageAspect = displayImageAspect.y;
	if (displayAspect > imageAspect) {
		aspectRatio.x = imageAspect / displayAspect;
	} else {
		aspectRatio.y = displayAspect / imageAspect;
	}
	if (fillScreen == 1) {
		float minDim = min(aspectRatio.x, aspectRatio.y);
		aspectRatio /= minDim;
	}
	vec3 adjustedPosition = inPosition * aspectRatio;
	gl_Position = projection * transform * vec4(adjustedPosition, 1.0);
}
