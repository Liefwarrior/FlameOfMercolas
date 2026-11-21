#pragma once

// The types the 3D scene builders and the signage speak: where the eye is and the projection
// numbers derived from it. (They were the software world pass's too, until phase 7 of
// docs/design/OBLIVION-PLAN.md retired that pass.) LampGlow already lives in lighting.hpp.
//
// Floats are legal in this file. Nothing in it may be read by the simulation.

#include <cstdint>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/lighting.hpp"

namespace granadad::render {

/// Where the eye is and where it points. Derived from the body's integers at
/// draw time; never the other way round.
struct Camera {
    /// Eye position in world tile units.
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    /// Radians. 0 faces north, which is -Y; increases clockwise.
    float yaw = 0.0F;
    /// Radians. Positive looks up.
    float pitch = 0.0F;
    /// tan(horizontal field of view / 2). 1.0 is a 90-degree view.
    float hfovTan = 1.0F;

    /// Builds a camera from a body's Q8 position and BAM facing.
    [[nodiscard]] static Camera fromBody(std::int32_t xQ8, std::int32_t yQ8, std::int32_t eyeZQ8,
                                         std::int32_t yawBam, std::int32_t pitchBam,
                                         float hfovTan) noexcept;
};

/// The two numbers every screen-space projection in this renderer is built
/// from -- `focal` turns a world offset into pixels, `horizon` is the row a
/// point at eye height projects to. Derived from the framebuffer size and the
/// camera alone, in one place: a second, drifting copy of "half the frame
/// width over hfovTan" is how a world-space overlay (a sign, say) ends up
/// misaligned with the picture under it.
struct Projection {
    float focal = 0.0F;
    float horizon = 0.0F;
};

[[nodiscard]] Projection projectionFor(const Camera& camera, int width, int height) noexcept;

}  // namespace granadad::render
