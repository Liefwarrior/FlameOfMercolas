#pragma once

// The types the software world pass (world_renderer.hpp) and the 3D scene builders both speak:
// where the eye is, the projection numbers derived from it, and one billboard. Moved out of
// world_renderer.hpp unchanged so the software world pass can be retired without taking them
// with it. LampGlow, the third of the shared types, already lives in lighting.hpp.
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
/// point at eye height projects to. renderFrame's own horizontal-face loop
/// and drawSprite both derive these from the framebuffer size and the camera
/// alone, so it is pulled out here rather than duplicated: a second, drifting
/// copy of "half the frame width over hfovTan" is exactly how a world-space
/// overlay (a sign, say) ends up misaligned with what the ray march drew.
struct Projection {
    float focal = 0.0F;
    float horizon = 0.0F;
};

[[nodiscard]] Projection projectionFor(const Camera& camera, int width, int height) noexcept;

/// Anything alive, or anything that should face the camera. Drawn after the
/// world against the depth buffer the world pass wrote.
struct SpriteInstance {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float halfWidth = 0.3F;
    float halfHeight = 0.4F;
    Rgb colour{1.0F, 1.0F, 1.0F};
    /// 0 draws as a lit solid; 1 draws as pure additive glow.
    float glow = 0.0F;
    /// How soft the billboard's edge is. 1 is the flame's smooth falloff; 0 is
    /// a hard-edged ellipse, which is what a body has to be to read as chunky
    /// at 320x180 instead of as a smudge.
    float softness = 1.0F;
    /// True when this billboard is part of a PERSON.
    ///
    /// It exists because of the S2 review. "The room is lit and full at nine"
    /// asserted `spritePixels > 0`, and a lit Gull carries nine flame sprites --
    /// so the check passed with every human being in the room invisible. This
    /// flag is what lets a frame say how much of it is people, separately from
    /// how much of it is candles.
    bool person = false;

    /// DISTRICT PHASE A: true for the two authored harbour lights that behave
    /// as BEACONS after dark -- the Weighhouse signal-mast lamp and the
    /// Mission's doctrinal night lamp. A beacon's additive glow takes a reduced
    /// fog wash at night, which is the honest way a light "carries": fog eats
    /// the wall it hangs on long before it eats the flame itself. Tagged by
    /// name in lampSprites(); nothing else may set it.
    bool beacon = false;

    // --- #78: a figure somebody drew, instead of an ellipse ------------------
    //
    // A 16x16 RGBA cutout, or null for the ellipse path. Both paths stay,
    // deliberately: the lamps are ellipses and always will be, the tavern's
    // fourteen are ellipses until somebody moves them, and a renderer that
    // could only draw textured quads would have to fake a flame with one.
    //
    // Alpha is a CUTOUT and not a blend. A texel is either the figure or it is
    // whatever is behind it, which is what keeps a body hard-edged at 320x180
    // instead of fringed -- the same argument `softness = 0` makes for the
    // ellipse, one level further down.
    /// True for a body belonging to the WARD's population rather than to the
    /// taproom. Both are people and both count toward actorPixels; this is what
    /// lets a capture report how much of the district's own roll a frame is
    /// looking at, separately from how many of the Gull's fourteen are in it.
    bool ward = false;

    const std::uint32_t* art = nullptr;
    /// Side of the art cell in texels. 16, always, but stated so the sampler
    /// does not carry the constant.
    int artSize = 0;
    /// The sub-rectangle of the cell that actually carries ink, inclusive. A
    /// figure drawn fourteen rows tall inside a sixteen-row cell floats two
    /// rows above the pavement if the quad is sized to the cell.
    int artU0 = 0;
    int artU1 = 0;
    int artV0 = 0;
    int artV1 = 0;
};

}  // namespace granadad::render
