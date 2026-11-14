#include "granadad/render/scene_types.hpp"

#include <cmath>

#include "granadad/render/vertical.hpp"

namespace granadad::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;

}  // namespace

Camera Camera::fromBody(std::int32_t xQ8, std::int32_t yQ8, std::int32_t eyeZQ8,
                        std::int32_t yawBam, std::int32_t pitchBam, float hfovTan) noexcept {
    Camera camera;
    camera.x = static_cast<float>(xQ8) / 256.0F;
    camera.y = static_cast<float>(yQ8) / 256.0F;
    // THE ONE ASYMMETRY IN THIS FUNCTION, and it is not a typo. x and y arrive
    // in Q8 TILES; eyeZQ8 arrives in Q8 BANDS, because the body's vertical axis
    // counts bands (sim/player.hpp: feetZ_ is `band << 8`). Multiplying by
    // kBandHeight is what puts all three in the same space, and forgetting to
    // is what buried the eye 1.9 tiles inside the ground floor.
    camera.z = static_cast<float>(eyeZQ8) * kBandHeight / 256.0F;
    camera.yaw = static_cast<float>(yawBam) * (2.0F * kPi / 65536.0F);
    camera.pitch = static_cast<float>(pitchBam) * (2.0F * kPi / 65536.0F);
    camera.hfovTan = hfovTan;
    return camera;
}

Projection projectionFor(const Camera& camera, int width, int height) noexcept {
    const float halfWidth = static_cast<float>(width) * 0.5F;
    const float focal = halfWidth / camera.hfovTan;
    Projection projection;
    projection.focal = focal;
    projection.horizon = static_cast<float>(height) * 0.5F + std::tan(camera.pitch) * focal;
    return projection;
}

}  // namespace granadad::render
