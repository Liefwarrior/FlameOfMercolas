#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace granadad::render3d {

/// SQUARING A SHAFT. The kit's strapped beam (SM_Prop_Beam_01) is a flat
/// 0.209 m timber inside 0.261 m iron collars -- and hewn at two corners:
/// on two diagonally opposite arrises a diagonal cut a hand long, each
/// made of one vertex pulled in from each of the two faces that meet
/// there (off its vertices: the -x/+z arris from 0.75 to 1.04 m along, the
/// +x/-z arris from 1.07 to 1.85). Laid as a door's lintel a cut sits on a
/// bottom arris whichever way the beam is turned (the lintel needs 2.42 of
/// its 2.49 m), and its facet, looking down, drew as a dark tent on the
/// show face of every door -- in the head one way, on the front arris the
/// other. A catalogue row that asks to be SQUARE has its shaft squared
/// here, at load: every vertex inside the shaft's section that lies on one
/// face but short of the other is pushed out to the arris, its normal set
/// to the face it was on, and the cut's own triangles collapse to nothing.
/// The collars are outside the section and untouched; so is every corner
/// (on both faces) and anything on neither (an end cap's middle). `half`
/// is half the catalogue's core; the faces' own planes are read off the
/// mesh (its 0.1042 is not the catalogue's 0.1045). Returns how many
/// vertices moved.
inline std::size_t squareShaft(float* vertices, float* normals, std::size_t count,
                               float half) noexcept {
    constexpr float kOn = 0.002F;
    if (vertices == nullptr || count == 0 || half <= 0.0F) {
        return 0;
    }
    float halfX = 0.0F;
    float halfZ = 0.0F;
    for (std::size_t v = 0; v < count; ++v) {
        const float ax = std::fabs(vertices[v * 3]);
        const float az = std::fabs(vertices[v * 3 + 2]);
        if (ax <= half + kOn && az <= half + kOn) {
            halfX = std::max(halfX, ax);
            halfZ = std::max(halfZ, az);
        }
    }
    if (halfX <= 0.0F || halfZ <= 0.0F) {
        return 0;
    }
    std::size_t moved = 0;
    for (std::size_t v = 0; v < count; ++v) {
        float& x = vertices[v * 3];
        float& z = vertices[v * 3 + 2];
        if (std::fabs(x) > half + kOn || std::fabs(z) > half + kOn) {
            continue;
        }
        const bool onX = halfX - std::fabs(x) <= kOn;
        const bool onZ = halfZ - std::fabs(z) <= kOn;
        if (onX == onZ) {
            continue;
        }
        if (onX) {
            z = z < 0.0F ? -halfZ : halfZ;
            if (normals != nullptr) {
                normals[v * 3] = x < 0.0F ? -1.0F : 1.0F;
                normals[v * 3 + 1] = 0.0F;
                normals[v * 3 + 2] = 0.0F;
            }
        } else {
            x = x < 0.0F ? -halfX : halfX;
            if (normals != nullptr) {
                normals[v * 3] = 0.0F;
                normals[v * 3 + 1] = 0.0F;
                normals[v * 3 + 2] = z < 0.0F ? -1.0F : 1.0F;
            }
        }
        ++moved;
    }
    return moved;
}

}  // namespace granadad::render3d
