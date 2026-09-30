// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#ifndef RENDEPTH_VIDEO_DEPTH_MOTION_H
#define RENDEPTH_VIDEO_DEPTH_MOTION_H

#include "VideoPlayer.h"
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * @brief Reprojects depth values using motion vectors.
 * 
 * For each motion vector block, we calculate the spatial displacement between the 
 * destination block center and the source block center. We then apply this 
 * displacement to every pixel within the destination block's boundary, performing 
 * bilinear interpolation from the previous frame's depth values.
 */
inline void reprojectVideoDepth(std::vector<std::uint16_t>& values, int width, int height,
		const std::vector<VideoFrame::MotionVector>& motionVectors) {
	if (width <= 0 || height <= 0 || values.size() != static_cast<size_t>(width) * height)
		return;

	const std::vector<std::uint16_t> previous = values;
	constexpr float motionFraction = 0.67f; // Weight of the warped value vs original

	for (const auto& motion : motionVectors) {
		if (motion.width <= 0.0f || motion.height <= 0.0f) continue;

		// Define the block boundaries in pixel space.
		const float left_norm = std::max(0.0f, motion.destinationX - motion.width * 0.5f);
		const float top_norm = std::max(0.0f, motion.destinationY - motion.height * 0.5f);
		const float right_norm = std::min(1.0f, motion.destinationX + motion.width * 0.5f);
		const float bottom_norm = std::min(1.0f, motion.destinationY + motion.height * 0.5f);

		const int left = static_cast<int>(std::floor(left_norm * width));
		const int top = static_cast<int>(std::floor(top_norm * height));
		const int right = static_cast<int>(std::ceil(right_norm * width));
		const int bottom = static_cast<int>(std::ceil(bottom_norm * height));

		// Displacement in pixels between the centers of the two block positions.
		const float dx = (motion.sourceX - motion.destinationX) * width;
		const float dy = (motion.sourceY - motion.destinationY) * height;

		for (int y = std::max(0, top); y < std::min(height, bottom); ++y) {
			for (int x = std::max(0, left); x < std::min(width, right); ++x) {
				// Map the destination pixel center (x + 0.5) to its source position in pixels.
				const float sx = static_cast<float>(x) + 0.5f + dx;
				const float sy = static_cast<float>(y) + 0.5f + dy;

				if (sx < 0.5f || sx >= static_cast<float>(width - 1) || 
				    sy < 0.5f || sy >= static_cast<float>(height - 1)) continue;

				// Bilinear interpolation from the previous frame's depth values.
				const float x0 = std::floor(sx);
				const float y0 = std::floor(sy);
				const float x1 = std::ceil(sx);
				const float y1 = std::ceil(sy);

				const auto sample = [&](float px, float py) {
					return static_cast<float>(previous[static_cast<size_t>(std::floor(py)) * width + static_cast<size_t>(std::floor(px))]);
				};

				const float weight_x = sx - x0;
				const float weight_y = sy - y0;

				const float v00 = sample(x0, y0);
				const float v10 = sample(x1, y0);
				const float v01 = sample(x0, y1);
				const float v11 = sample(x1, y1);

				const float warped = std::lerp(std::lerp(v00, v10, weight_x),
															 std::lerp(v01, v11, weight_x), 
															 weight_y);

				const size_t index = static_cast<size_t>(y) * width + x;
				values[index] = static_cast<std::uint16_t>(std::lround(
					std::lerp(static_cast<float>(previous[index]), warped, motionFraction)));
			}
		}
	}
}

#endif
