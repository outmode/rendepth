#include "VideoDepthMotion.h"
#include <vector>
#include <cassert>

// Smoke-test depth reprojection with a synthetic block motion vector.
int main() {
    int width = 10;
    int height = 10;
    std::vector<std::uint16_t> values(width * height, 100);
    std::vector<VideoFrame::MotionVector> motionVectors;
    
    // A simple vector that moves everything by 1 pixel in X
    VideoFrame::MotionVector mv;
    mv.destinationX = 0.5f; // Center of block at x=5 (normalized)
    mv.destinationY = 0.5f;
    mv.sourceX = 0.6f;      // Moves to x=6 (normalized)
    mv.sourceY = 0.5f;
    mv.width = 0.1f;
    mv.height = 0.1f;
    motionVectors.push_back(mv);

    reprojectVideoDepth(values, width, height, motionVectors);
    
    // If it works, the values should have changed
    return 0;
}
