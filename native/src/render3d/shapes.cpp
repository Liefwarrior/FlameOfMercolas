#include "granadad/render3d/shapes.hpp"

#include <algorithm>
#include <cmath>

namespace granadad::render3d::shapes {

Mat3 mul(const Mat3& a, const Mat3& b) noexcept {
    Mat3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r * 3 + c] = a.m[r * 3 + 0] * b.m[0 * 3 + c] + a.m[r * 3 + 1] * b.m[1 * 3 + c] +
                               a.m[r * 3 + 2] * b.m[2 * 3 + c];
        }
    }
    return out;
}

Vec3 apply(const Mat3& a, const Vec3& v) noexcept {
    return Vec3{a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
                a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z};
}

Mat3 rotX(float a) noexcept {
    const float c = std::cos(a);
    const float s = std::sin(a);
    Mat3 out;
    out.m[4] = c;
    out.m[5] = -s;
    out.m[7] = s;
    out.m[8] = c;
    return out;
}

Mat3 rotY(float a) noexcept {
    const float c = std::cos(a);
    const float s = std::sin(a);
    Mat3 out;
    out.m[0] = c;
    out.m[2] = s;
    out.m[6] = -s;
    out.m[8] = c;
    return out;
}

Mat3 rotZ(float a) noexcept {
    const float c = std::cos(a);
    const float s = std::sin(a);
    Mat3 out;
    out.m[0] = c;
    out.m[1] = -s;
    out.m[3] = s;
    out.m[4] = c;
    return out;
}

Mat3 euler(float pitch, float yaw, float roll) noexcept {
    return mul(rotZ(roll), mul(rotY(yaw), rotX(pitch)));
}

Vec3 Xf::point(const Vec3& p) const noexcept {
    const Vec3 q = apply(r, p);
    return Vec3{q.x + t.x, q.y + t.y, q.z + t.z};
}

namespace {

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] Vec3 unit(const Vec3& v, const Vec3& fallback) noexcept {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-6F) {
        return fallback;
    }
    return Vec3{v.x / l, v.y / l, v.z / l};
}

}  // namespace

Xf alongAxis(const Vec3& origin, const Vec3& end, const Vec3& ref, float& length) noexcept {
    const Vec3 d{end.x - origin.x, end.y - origin.y, end.z - origin.z};
    length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const Vec3 axis = unit(d, Vec3{0.0F, 1.0F, 0.0F});
    Vec3 x = unit(cross(ref, axis), Vec3{1.0F, 0.0F, 0.0F});
    if (std::abs(x.x) + std::abs(x.y) + std::abs(x.z) < 1e-6F) {
        x = Vec3{1.0F, 0.0F, 0.0F};
    }
    const Vec3 z = cross(x, axis);
    Xf xf;
    xf.r.m[0] = x.x;
    xf.r.m[1] = axis.x;
    xf.r.m[2] = z.x;
    xf.r.m[3] = x.y;
    xf.r.m[4] = axis.y;
    xf.r.m[5] = z.y;
    xf.r.m[6] = x.z;
    xf.r.m[7] = axis.z;
    xf.r.m[8] = z.z;
    xf.t = origin;
    return xf;
}

Rgba8 shaded(const Rgba8& c, float factor) noexcept {
    const auto ch = [factor](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(v) * factor + 0.5F, 0.0F, 255.0F));
    };
    return Rgba8{ch(c.r), ch(c.g), ch(c.b), c.a};
}

Rgba8 mixed(const Rgba8& a, const Rgba8& b, float t) noexcept {
    const auto ch = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t + 0.5F,
                       0.0F, 255.0F));
    };
    return Rgba8{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 255};
}

namespace {

/// (sin, cos) of the angle of side j, as literals: no libm in the hashed
/// geometry. Angle 0 is the front.
struct Unit {
    float s;
    float c;
};

constexpr Unit kUnit4[4] = {{0.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {-1.0F, 0.0F}};
constexpr Unit kUnit6[6] = {{0.0F, 1.0F},        {0.8660254F, 0.5F},   {0.8660254F, -0.5F},
                            {0.0F, -1.0F},       {-0.8660254F, -0.5F}, {-0.8660254F, 0.5F}};
constexpr Unit kUnit8[8] = {{0.0F, 1.0F},         {0.70710678F, 0.70710678F},   {1.0F, 0.0F},
                            {0.70710678F, -0.70710678F}, {0.0F, -1.0F},         {-0.70710678F, -0.70710678F},
                            {-1.0F, 0.0F},        {-0.70710678F, 0.70710678F}};

[[nodiscard]] Unit unitOf(int sides, int j) noexcept {
    switch (sides) {
        case 4: return kUnit4[j];
        case 6: return kUnit6[j];
        default: return kUnit8[j];
    }
}

void pushVertex(Builder& b, const Vec3& local, const Rgba8& c) {
    MeshData& mesh = *b.mesh;
    const Vec3 p = b.xf.point(local);
    mesh.positions.push_back(p.x);
    mesh.positions.push_back(p.y);
    mesh.positions.push_back(p.z);
    mesh.texcoords.push_back(0.0F);
    mesh.texcoords.push_back(0.0F);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
    if (b.rigged) {
        mesh.bones.push_back(b.bone);
    }
}

void pushTriangle(MeshData& mesh, std::size_t a, std::size_t b, std::size_t c) {
    mesh.indices.push_back(static_cast<std::uint16_t>(a));
    mesh.indices.push_back(static_cast<std::uint16_t>(b));
    mesh.indices.push_back(static_cast<std::uint16_t>(c));
}

void pushQuad(Builder& b, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
              const Rgba8& c) {
    const std::size_t base = b.mesh->vertexCount();
    pushVertex(b, p0, c);
    pushVertex(b, p1, c);
    pushVertex(b, p2, c);
    pushVertex(b, p3, c);
    pushTriangle(*b.mesh, base, base + 1, base + 2);
    pushTriangle(*b.mesh, base, base + 2, base + 3);
}

}  // namespace

void loft(Builder& b, const std::vector<Ring>& rings, int sides, bool capBottom, bool capTop) {
    if (rings.size() < 2) {
        return;
    }
    if (sides != 4 && sides != 6) {
        sides = 8;
    }
    MeshData& mesh = *b.mesh;
    const std::size_t first = mesh.vertexCount();
    const auto s = static_cast<std::size_t>(sides);
    for (const Ring& ring : rings) {
        for (int j = 0; j < sides; ++j) {
            const Unit u = unitOf(sides, j);
            pushVertex(b, Vec3{ring.cx + ring.rx * u.s, ring.y, ring.cz - ring.rz * u.c}, ring.colour);
        }
    }
    for (std::size_t i = 0; i + 1 < rings.size(); ++i) {
        for (std::size_t j = 0; j < s; ++j) {
            const std::size_t jn = (j + 1) % s;
            const std::size_t a = first + i * s + j;
            const std::size_t bb = first + i * s + jn;
            const std::size_t c = first + (i + 1) * s + jn;
            const std::size_t d = first + (i + 1) * s + j;
            pushTriangle(mesh, a, d, c);
            pushTriangle(mesh, a, c, bb);
        }
    }
    const auto cap = [&](const Ring& ring, bool top) {
        const std::size_t centre = mesh.vertexCount();
        pushVertex(b, Vec3{ring.cx, ring.y, ring.cz}, ring.colour);
        const std::size_t base = mesh.vertexCount();
        for (int j = 0; j < sides; ++j) {
            const Unit u = unitOf(sides, j);
            pushVertex(b, Vec3{ring.cx + ring.rx * u.s, ring.y, ring.cz - ring.rz * u.c}, ring.colour);
        }
        for (std::size_t j = 0; j < s; ++j) {
            const std::size_t jn = (j + 1) % s;
            if (top) {
                pushTriangle(mesh, centre, base + jn, base + j);
            } else {
                pushTriangle(mesh, centre, base + j, base + jn);
            }
        }
    };
    if (capBottom) {
        cap(rings.front(), false);
    }
    if (capTop) {
        cap(rings.back(), true);
    }
}

namespace {

/// A hexahedron from its four bottom corners (x0,z0)(x1,z0)(x1,z1)(x0,z1)
/// at y0 and the same four at y1, shaded per face.
void hexahedron(Builder& b, const Vec3 p[8], const Rgba8& base) {
    pushQuad(b, p[7], p[6], p[5], p[4], shaded(base, 1.00F));  // top
    pushQuad(b, p[0], p[1], p[2], p[3], shaded(base, 0.70F));  // bottom
    pushQuad(b, p[3], p[2], p[6], p[7], shaded(base, 0.90F));  // +Z
    pushQuad(b, p[1], p[0], p[4], p[5], shaded(base, 0.90F));  // -Z
    pushQuad(b, p[2], p[1], p[5], p[6], shaded(base, 0.80F));  // +X
    pushQuad(b, p[0], p[3], p[7], p[4], shaded(base, 0.80F));  // -X
}

}  // namespace

void wedge(Builder& b, const Vec3& centre, const Vec3& half, float topHalfX, float topHalfZ,
           float topShiftZ, const Rgba8& colour) {
    const float y0 = centre.y - half.y;
    const float y1 = centre.y + half.y;
    const float x0 = centre.x - half.x;
    const float x1 = centre.x + half.x;
    const float z0 = centre.z - half.z;
    const float z1 = centre.z + half.z;
    const float tx0 = centre.x - topHalfX;
    const float tx1 = centre.x + topHalfX;
    const float tz0 = centre.z + topShiftZ - topHalfZ;
    const float tz1 = centre.z + topShiftZ + topHalfZ;
    const Vec3 p[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                       {tx0, y1, tz0}, {tx1, y1, tz0}, {tx1, y1, tz1}, {tx0, y1, tz1}};
    hexahedron(b, p, colour);
}

void block(Builder& b, const Vec3& centre, const Vec3& half, const Rgba8& colour) {
    wedge(b, centre, half, half.x, half.z, 0.0F, colour);
}

}  // namespace granadad::render3d::shapes
