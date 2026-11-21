#pragma once

// SHAPES -- the small procedural solids the people, the hands and what they
// hold are built from (OBLIVION-PLAN section 7, items 3 and 4: "a procedural
// low-poly humanoid ... bevelled boxes ... equipment meshes built the same
// way", no model files, no licence question).
//
// Two primitives, both pure over their arguments:
//
//   loft   a stack of rings, each its own height, radii, centre and colour,
//          closed with a cap at either end. A limb, a torso, a head, a blade
//          and a skirt are all this. Six or eight sides, so a figure costs
//          hundreds of triangles and not thousands. The sin and cos of a ring
//          are literals (no libm call): the vertices that land in the hashed
//          SceneDescription are the same bytes on every machine.
//   block  a box about a centre, lightly shaded per face, optionally turned.
//
// Both write into a MeshData and, when the builder is rigged, one bone index
// per vertex (MeshData::bones), which is what lets one rest mesh be posed by
// figure.hpp's skeleton. Normals are NOT written here: the description's own
// fillNormals() takes them from the triangles, so shared ring vertices shade
// smooth and a block's faces stay hard.
//
// Frame: scene space, feet on y = 0, +Y up, a figure faces -Z, +X is its
// right hand. Ring angle 0 is the FRONT (-Z) and the angle grows towards +X.

#include <cstdint>
#include <vector>

#include "granadad/render3d/scene.hpp"

namespace granadad::render3d::shapes {

/// A 3x3 rotation, row major. Enough matrix for a pose; nothing else.
struct Mat3 {
    float m[9] = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
};

[[nodiscard]] Mat3 mul(const Mat3& a, const Mat3& b) noexcept;
[[nodiscard]] Vec3 apply(const Mat3& a, const Vec3& v) noexcept;
/// Rotation about X by `a` radians (right handed, Y up): +a takes +Y towards
/// +Z, so a limb hanging down swings FORWARD (-Z) for a positive angle.
[[nodiscard]] Mat3 rotX(float a) noexcept;
[[nodiscard]] Mat3 rotY(float a) noexcept;
[[nodiscard]] Mat3 rotZ(float a) noexcept;
/// Rz(roll) * Ry(yaw) * Rx(pitch): pitch first, then yaw, then roll.
[[nodiscard]] Mat3 euler(float pitch, float yaw, float roll) noexcept;

/// A rotation then a translation, for placing a solid off the vertical.
struct Xf {
    Mat3 r;
    Vec3 t;
    [[nodiscard]] Vec3 point(const Vec3& p) const noexcept;
};

/// A placement that stands a loft (authored up +Y from the origin) along the
/// line from `origin` to `end`: local Y runs along it, and `ref` picks which
/// way local X lies (across `ref` and the axis). Built from normalised
/// vectors only -- a square root and cross products, no trig -- so the
/// vertices it makes are the same bytes on every machine. `length` is the
/// distance, for the rings' heights.
[[nodiscard]] Xf alongAxis(const Vec3& origin, const Vec3& end, const Vec3& ref, float& length) noexcept;

/// One ring of a loft: a flat ellipse at height `y`, radii `rx`/`rz`,
/// centred at (cx, cz), with its own colour. Two rings at the same height
/// with different colours make a hard band (a cuff, a belt, a boot top).
struct Ring {
    float y = 0.0F;
    float rx = 0.0F;
    float rz = 0.0F;
    float cx = 0.0F;
    float cz = 0.0F;
    Rgba8 colour;
};

/// Where a solid is written: the mesh, the bone every vertex it adds is
/// weighted to (ignored unless `rigged`), and an optional placement.
struct Builder {
    MeshData* mesh = nullptr;
    std::uint8_t bone = 0;
    bool rigged = false;
    Xf xf;
};

/// Sides a loft may have: 4, 6 or 8.
void loft(Builder& b, const std::vector<Ring>& rings, int sides, bool capBottom = true,
          bool capTop = true);

/// A box [centre - half, centre + half], six hard faces, shaded top 1.0,
/// north-south 0.9, east-west 0.8, bottom 0.7 of `colour`.
void block(Builder& b, const Vec3& centre, const Vec3& half, const Rgba8& colour);

/// A tapered box: the bottom face (y = centre.y - half.y) is `half` wide and
/// deep, the top face `topHalfX` by `topHalfZ`; the top is also shifted by
/// `topShiftZ` (a blade's point, a boot's toe).
void wedge(Builder& b, const Vec3& centre, const Vec3& half, float topHalfX, float topHalfZ,
           float topShiftZ, const Rgba8& colour);

[[nodiscard]] Rgba8 shaded(const Rgba8& c, float factor) noexcept;
[[nodiscard]] Rgba8 mixed(const Rgba8& a, const Rgba8& b, float t) noexcept;

}  // namespace granadad::render3d::shapes
