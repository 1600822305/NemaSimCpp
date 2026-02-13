#pragma once

// ================================================================
// Step 130: Real-time worm body renderer using ImGui DrawList
// Driven by the REAL 302-neuron simulation, NOT pre-recorded data
// Two views: top-down (XY) + side (XZ) with muscle activation colors
// ================================================================

#include "body/body_model.h"
#include <array>

namespace celegans {

class WormRenderer3D {
public:
    bool initialize(int width, int height);

    // Draw the worm body into the current ImGui window
    // Uses ImGui DrawList — no OpenGL extensions needed
    void draw(const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_ = 400, height_ = 400;
};

} // namespace celegans
