#include "granadad/render3d/starter_scene.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/render/lighting.hpp"

namespace granadad::render3d {

namespace {

/// One sun for the whole starter scene: high, from the south-east, so the
/// cube's three visible faces from the authored spawn read three shades.
constexpr Vec3 kSunDir{0.35F, 0.80F, -0.45F};

[[nodiscard]] Vec3 normalised(const Vec3& v) noexcept {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0F) {
        return Vec3{0.0F, 1.0F, 0.0F};
    }
    return Vec3{v.x / len, v.y / len, v.z / len};
}

[[nodiscard]] std::uint8_t channel8(float value) noexcept {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

struct Light {
    render::Rgb ambient;
    render::Rgb sun;
    Vec3 dir;
};

[[nodiscard]] Light lightAt(int timeOfDaySeconds) noexcept {
    const render::SkyState sky = render::skyAt(timeOfDaySeconds);
    Light light;
    light.ambient = sky.ambient;
    // A warm sun that goes out with the daylight -- the district is
    // "committed dark" at night, and the lamps (a later lane) carry it.
    light.sun = render::Rgb{1.0F, 0.95F, 0.85F} * sky.daylight;
    light.dir = normalised(kSunDir);
    return light;
}

/// Lambert against the one sun, plus ambient. The whole lighting model of the
/// starter scene, in one place.
[[nodiscard]] Rgba8 shade(const render::Rgb& base, const Vec3& normal,
                          const Light& light) noexcept {
    const float lambert =
        std::max(0.0F, normal.x * light.dir.x + normal.y * light.dir.y + normal.z * light.dir.z);
    const render::Rgb lit = light.ambient + light.sun * lambert;
    return Rgba8{channel8(base.r * lit.r), channel8(base.g * lit.g), channel8(base.b * lit.b),
                 255};
}

void pushVertex(MeshData& mesh, const Vec3& p, float u, float v, const Rgba8& c) {
    mesh.positions.push_back(p.x);
    mesh.positions.push_back(p.y);
    mesh.positions.push_back(p.z);
    mesh.texcoords.push_back(u);
    mesh.texcoords.push_back(v);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
}

/// A quad from four corners given COUNTER-CLOCKWISE as seen from the side the
/// normal points to. Two triangles, flat colour.
void pushQuad(MeshData& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
              const Rgba8& colour) {
    const auto base = static_cast<std::uint16_t>(mesh.vertexCount());
    pushVertex(mesh, a, 0.0F, 0.0F, colour);
    pushVertex(mesh, b, 1.0F, 0.0F, colour);
    pushVertex(mesh, c, 1.0F, 1.0F, colour);
    pushVertex(mesh, d, 0.0F, 1.0F, colour);
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 1));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 3));
}

/// The ground: a checker of two-tile squares, so distance reads as
/// perspective rather than as one flat colour. Built in the plane's own
/// local frame (centred on the origin at y = 0); the instance places it.
MeshData buildGround(const StarterSceneParams& params, const Light& light) {
    MeshData mesh;
    mesh.id = kStarterGroundMeshId;
    mesh.version = starterSceneVersion(params);
    constexpr float kSquare = 2.0F;
    const Vec3 up{0.0F, 1.0F, 0.0F};
    const Rgba8 light1 = shade(render::Rgb{0.32F, 0.36F, 0.27F}, up, light);
    const Rgba8 dark = shade(render::Rgb{0.24F, 0.27F, 0.21F}, up, light);
    // Whole squares only, and a hard cap that keeps the index type honest.
    int squares = static_cast<int>(params.halfExtent / kSquare);
    squares = std::clamp(squares, 1, 60);
    for (int sz = -squares; sz < squares; ++sz) {
        for (int sx = -squares; sx < squares; ++sx) {
            const float x0 = static_cast<float>(sx) * kSquare;
            const float z0 = static_cast<float>(sz) * kSquare;
            const float x1 = x0 + kSquare;
            const float z1 = z0 + kSquare;
            const Rgba8& colour = ((sx + sz) & 1) == 0 ? light1 : dark;
            // Seen from above (+Y), CCW: (x0,z1) -> (x1,z1) -> (x1,z0) -> (x0,z0).
            pushQuad(mesh, Vec3{x0, 0.0F, z1}, Vec3{x1, 0.0F, z1}, Vec3{x1, 0.0F, z0},
                     Vec3{x0, 0.0F, z0}, colour);
        }
    }
    return mesh;
}

/// A unit cube, its base on y = 0, centred on the origin in x and z. Six
/// faces, each flat-shaded against the sun.
MeshData buildCube(const StarterSceneParams& params, const Light& light) {
    MeshData mesh;
    mesh.id = kStarterCubeMeshId;
    mesh.version = starterSceneVersion(params);
    const render::Rgb base{0.66F, 0.50F, 0.30F};
    const float h = 0.5F;
    const Vec3 p000{-h, 0.0F, -h}, p100{h, 0.0F, -h}, p010{-h, 1.0F, -h}, p110{h, 1.0F, -h};
    const Vec3 p001{-h, 0.0F, h}, p101{h, 0.0F, h}, p011{-h, 1.0F, h}, p111{h, 1.0F, h};
    // +Y top
    pushQuad(mesh, p011, p111, p110, p010, shade(base, Vec3{0.0F, 1.0F, 0.0F}, light));
    // -Y bottom (never seen from above ground; kept so the mesh is closed)
    pushQuad(mesh, p000, p100, p101, p001, shade(base, Vec3{0.0F, -1.0F, 0.0F}, light));
    // +Z south face
    pushQuad(mesh, p001, p101, p111, p011, shade(base, Vec3{0.0F, 0.0F, 1.0F}, light));
    // -Z north face
    pushQuad(mesh, p100, p000, p010, p110, shade(base, Vec3{0.0F, 0.0F, -1.0F}, light));
    // +X east face
    pushQuad(mesh, p101, p100, p110, p111, shade(base, Vec3{1.0F, 0.0F, 0.0F}, light));
    // -X west face
    pushQuad(mesh, p000, p001, p011, p010, shade(base, Vec3{-1.0F, 0.0F, 0.0F}, light));
    return mesh;
}

}  // namespace

std::uint32_t starterSceneVersion(const StarterSceneParams& params) noexcept {
    // One bucket per simulated minute, plus one so a fresh mesh never carries
    // version 0 (the adapter's "never uploaded" sentinel).
    const int minute = ((params.timeOfDaySeconds % 86400) + 86400) % 86400 / 60;
    return static_cast<std::uint32_t>(minute) + 1U;
}

void buildStarterScene(SceneDescription& scene, const StarterSceneParams& params) {
    const std::uint32_t version = starterSceneVersion(params);
    const Light light = lightAt(params.timeOfDaySeconds);

    const MeshData* ground = scene.findMesh(kStarterGroundMeshId);
    if (ground == nullptr || ground->version != version) {
        scene.putMesh(buildGround(params, light));
    }
    const MeshData* cube = scene.findMesh(kStarterCubeMeshId);
    if (cube == nullptr || cube->version != version) {
        scene.putMesh(buildCube(params, light));
    }

    // The instances are rewritten every call: they are two entries and the
    // placement is the params', not the mesh's.
    scene.instances.clear();
    Instance groundAt;
    groundAt.meshId = kStarterGroundMeshId;
    groundAt.position = Vec3{params.centre.x, params.groundY, params.centre.z};
    scene.instances.push_back(groundAt);
    Instance cubeAt;
    cubeAt.meshId = kStarterCubeMeshId;
    cubeAt.position = Vec3{params.cube.x, params.groundY, params.cube.z};
    scene.instances.push_back(cubeAt);

    const render::SkyState sky = render::skyAt(params.timeOfDaySeconds);
    scene.clearColour = Rgba8{channel8(sky.skyHorizon.r), channel8(sky.skyHorizon.g),
                              channel8(sky.skyHorizon.b), 255};
}

}  // namespace granadad::render3d
