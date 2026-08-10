#include "granadad/render/world_renderer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

#include "granadad/sim/vertical_scale.hpp"

namespace granadad::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;

// kBandHeight and bandSurface() are in world_renderer.hpp: session.cpp places
// billboards against the same scale and there must be exactly one of it.

/// How far a FLOOR / RAMP / STAIR slab hangs below the level it is the surface
/// of. The walking surface is exactly at the level's own height, so the slab
/// has to be underneath it — which is also why a pier deck is visibly a plank
/// with air under it when you stand at the water's edge.
///
/// ABSOLUTE TILE THICKNESSES, not fractions of a storey, and that distinction
/// is the reason they did not change when the storey tripled. A deck plank is
/// 0.22 of a tile — call it 0.20 m — of timber whether the room under it is one
/// tile high or three. Scaling these with the band would have given the Long
/// Quay a 0.60 m slab of decking, which is a bridge, not a pier.
constexpr float kFloorSlab = 0.22F;
constexpr float kRampSlab = 0.30F;
constexpr float kStairSlab = 0.36F;

/// Water surface height above its level's own floor, in tiles, from FLUID-lane
/// depth 1..7. A FRACTION OF THE BAND: a cell filled to the brim holds a band's
/// worth of water, so this one scales where the slabs above do not.
///
/// Depth 7 comes out at 0.70 of a band rather than 1.0, deliberately. The
/// harbour fills z=17 and z=18 at full depth and the quay deck's slab hangs
/// from 57.0 down to 56.78; a surface at the full 57.0 would z-fight the deck
/// along the entire waterfront. At 0.70 the waterline stands at 56.10, which is
/// 0.90 of a tile — about 0.8 m — below the deck: artifact-free, and now an
/// actual tidal drop you can see from the quay edge rather than the 0.3 of a
/// tile it used to be.
[[nodiscard]] float waterSurface(int depth) noexcept {
    return static_cast<float>(depth) * 0.10F * kBandHeight;
}

/// Screen-row conversion that cannot hand an out-of-range or NaN float to a
/// cast. A voxel directly under the eye projects to a row at plus infinity, and
/// static_cast<int> of that is undefined behaviour, not a big number.
[[nodiscard]] int rowAtLeast(float y) noexcept {
    if (!(y > -1.0e6F)) {
        return -1000000;
    }
    if (!(y < 1.0e6F)) {
        return 1000000;
    }
    return static_cast<int>(std::ceil(y));
}

[[nodiscard]] int rowAtMost(float y) noexcept {
    if (!(y > -1.0e6F)) {
        return -1000000;
    }
    if (!(y < 1.0e6F)) {
        return 1000000;
    }
    return static_cast<int>(std::floor(y));
}

[[nodiscard]] std::uint32_t hash32(std::uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

[[nodiscard]] float fractional(float value) noexcept {
    return value - std::floor(value);
}

[[nodiscard]] float luma(const Rgb& colour) noexcept {
    return 0.2126F * colour.r + 0.7152F * colour.g + 0.0722F * colour.b;
}

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

WorldRenderer::WorldRenderer(const sim::TileQuery& tiles, const TileAtlas& atlas,
                             std::vector<Lamp> lamps)
    : tiles_(&tiles), atlas_(&atlas), lamps_(std::move(lamps)) {
    glow_ = LampGlow::build(tiles, lamps_);

    columnMask_.assign(static_cast<std::size_t>(tiles.sizeX()) *
                           static_cast<std::size_t>(tiles.sizeY()),
                       0U);
    for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
        for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
            std::uint32_t mask = 0U;
            for (std::int32_t z = 0; z < tiles.sizeZ() && z < 32; ++z) {
                Voxel unused;
                if (voxelAt(x, y, z, unused)) {
                    mask |= 1U << static_cast<std::uint32_t>(z);
                }
            }
            columnMask_[static_cast<std::size_t>(y) * static_cast<std::size_t>(tiles.sizeX()) +
                        static_cast<std::size_t>(x)] = mask;
        }
    }
}

std::uint32_t WorldRenderer::columnMask(std::int32_t x, std::int32_t y) const noexcept {
    if (x < 0 || y < 0 || x >= tiles_->sizeX() || y >= tiles_->sizeY()) {
        return 0U;
    }
    return columnMask_[static_cast<std::size_t>(y) * static_cast<std::size_t>(tiles_->sizeX()) +
                       static_cast<std::size_t>(x)];
}

bool WorldRenderer::voxelAt(std::int32_t x, std::int32_t y, std::int32_t z,
                            Voxel& out) const noexcept {
    const content::TileForm form = tiles_->form(x, y, z);
    const int depth = tiles_->fluidDepth(x, y, z);

    // OPEN air holding water is the harbour: draw a surface on the TOPMOST wet
    // cell of the column and nothing below it, because seven-deep water is
    // opaque and there is nothing down there to see.
    //
    // Water sitting on AUTHORED GROUND is a puddle or a flooded cellar. That
    // cell keeps its own geometry and the wetness only tints its top face —
    // replacing a floor with a water plane would delete the floor the player is
    // standing on and leave them apparently walking on the sea.
    if (form == content::TileForm::Open || form == content::TileForm::Void) {
        if (depth > 0 && tiles_->fluidDepth(x, y, z + 1) == 0) {
            out.bottom = bandSurface(z);
            out.top = bandSurface(z) + waterSurface(depth);
            out.topFace = FaceKind::Water;
            out.material = 0;
            out.wetness = depth;
            out.water = true;
            out.hasSides = false;
            return true;
        }
        return false;
    }

    switch (form) {
        // THE WALL IS THE STOREY. It rises from its own level's walking surface
        // to the next one's, so its side face is kBandHeight tiles tall — three
        // now, one before, and that single line is the whole of what Eli was
        // looking at when he said every building appears to be one tile high.
        case content::TileForm::Wall:
            out.bottom = bandSurface(z);
            out.top = bandSurface(z) + kBandHeight;
            out.topFace = FaceKind::BlockTop;
            break;
        case content::TileForm::Floor:
            out.bottom = bandSurface(z) - kFloorSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::FloorTop;
            break;
        // VERIFICATION GAP (S2, and WORSE after polish-1): a RAMP is drawn as a
        // thinner flat slab, not as a slope. The movement rule is right
        // (stepBand authorises the climb); it is the picture that is wrong, and
        // nothing tests the picture.
        //
        // Tripling the storey made this gap bigger rather than smaller. Walking
        // a ramp used to look like stepping onto a low kerb; it now lifts the
        // body a full 2.7 m up a slab drawn 0.30 of a tile thick, so the eye
        // rises past a face that is not there. kEyeEaseRate was halved to make
        // the rise read as a climb (sim/player.hpp), which buys time but does
        // not draw the slope. Drawing the actual wedge is the fix and it is not
        // in this change.
        case content::TileForm::Ramp:
            out.bottom = bandSurface(z) - kRampSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::RampTop;
            break;
        case content::TileForm::Stair:
            out.bottom = bandSurface(z) - kStairSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::StairTop;
            break;
        case content::TileForm::Void:
        case content::TileForm::Open:
        default:
            return false;
    }
    out.material = tiles_->material(x, y, z);
    out.wetness = depth;
    out.water = false;
    out.hasSides = true;
    return true;
}

std::vector<SpriteInstance> WorldRenderer::lampSprites(float phase) const {
    std::vector<SpriteInstance> sprites;
    sprites.reserve(lamps_.size());
    for (std::size_t i = 0; i < lamps_.size(); ++i) {
        const Lamp& lamp = lamps_[i];
        // A slow, per-lamp-offset flicker. Deterministic in `phase`, so a
        // captured frame is reproducible.
        const float offset = static_cast<float>(hash32(static_cast<std::uint32_t>(i)) & 0xFFFU) /
                             4096.0F * 6.2831853F;
        const float flicker = 0.86F + 0.14F * std::sin(phase * 3.1F + offset);
        const float scale = 0.16F + 0.010F * static_cast<float>(lamp.luminance);
        SpriteInstance sprite;
        sprite.x = static_cast<float>(lamp.x) + 0.5F;
        sprite.y = static_cast<float>(lamp.y) + 0.5F;
        // A BRACKET LAMP, hung above head height on the wall of the cell it was
        // authored in. 1.90 tiles is about 1.7 m — a hand over the eye line,
        // which is where a lamp you walk under belongs and is the first thing
        // that reads wrong if you leave a light at 0.62 of a tile and call it
        // street lighting. An ABSOLUTE height in tiles, not a fraction of the
        // storey: raising the ceiling does not raise the lamp bracket.
        constexpr float kLampBracketHeight = 1.90F;
        sprite.z = bandSurface(lamp.z) + kLampBracketHeight;
        sprite.halfWidth = scale * flicker;
        sprite.halfHeight = scale * flicker * 1.25F;
        sprite.colour = lamp.warmth == LampWarmth::Fire ? Rgb{1.0F, 0.62F, 0.26F}
                                                        : Rgb{1.0F, 0.88F, 0.66F};
        sprite.glow = 1.0F;
        sprites.push_back(sprite);
    }
    return sprites;
}

FrameStats WorldRenderer::renderFrame(Framebuffer& target, const Camera& camera,
                                      const RenderSettings& settings,
                                      const std::vector<SpriteInstance>& sprites) const {
    const int width = target.width();
    const int height = target.height();
    const SkyState sky = skyAt(settings.timeOfDay);

    target.clear(sky.skyTop);

    const float halfWidth = static_cast<float>(width) * 0.5F;
    const float focal = halfWidth / camera.hfovTan;
    const float horizon =
        static_cast<float>(height) * 0.5F + std::tan(camera.pitch) * focal;

    const float forwardX = std::sin(camera.yaw);
    const float forwardY = -std::cos(camera.yaw);
    const float rightX = std::cos(camera.yaw);
    const float rightY = std::sin(camera.yaw);

    // WHICH LEVEL THE EYE IS ON, and it is a DIVISION now, not a floor.
    //
    // camera.z is in tiles and a level is kBandHeight of them, so flooring
    // camera.z straight to an int used to be right only because the two units
    // happened to be the same size. Standing on the quay at z19 that read the
    // eye's level as 58 the moment the storey grew, the z-window came out as
    // levels 54..64, the mask matched nothing that exists, and the whole
    // district rendered as empty sky. Same trap as everywhere else in this
    // change: a unit that was invisible while the conversion factor was one.
    const std::int32_t eyeLevel =
        static_cast<std::int32_t>(std::floor(camera.z / kBandHeight));
    const std::int32_t zLo = std::max(0, eyeLevel - settings.levelsBelow);
    const std::int32_t zHi = std::min(tiles_->sizeZ() - 1, eyeLevel + settings.levelsAbove);
    std::uint32_t windowMask = 0U;
    for (std::int32_t z = zLo; z <= zHi && z < 32; ++z) {
        windowMask |= 1U << static_cast<std::uint32_t>(z);
    }

    // --- sky ---------------------------------------------------------------
    // Painted first so anything unwritten by the world pass already looks like
    // sky. A vertical gradient with the horizon band where the fog lives.
    const float skySpan = std::max(1.0F, std::abs(horizon));
    for (int sy = 0; sy < height; ++sy) {
        const float t = std::clamp(static_cast<float>(sy) / skySpan, 0.0F, 1.0F);
        const Rgb band = lerp(sky.skyTop, sky.skyHorizon, t * t);
        for (int sx = 0; sx < width; ++sx) {
            target.pixels()[target.index(sx, sy)] = packRgb(band);
        }
    }

    std::vector<char> written(static_cast<std::size_t>(height), 0);

    float nearest = std::numeric_limits<float>::infinity();
    float furthest = 0.0F;
    std::size_t worldPixels = 0;

    // Unwritten rows, split at the horizon. Everything that sits entirely
    // BELOW the eye projects entirely below the horizon, and vice versa — so
    // once the ground half of a column is full, the rest of that column's ray
    // can skip every buried voxel it walks past. The district is built on a
    // solid WALL substrate eleven levels deep, so without this the ray tests
    // four or five invisible voxels in every cell for forty-four tiles.
    const int horizonRow =
        std::clamp(static_cast<int>(std::lround(std::clamp(horizon, -1.0e6F, 1.0e6F))), 0, height);

    for (int sx = 0; sx < width; ++sx) {
        std::fill(written.begin(), written.end(), static_cast<char>(0));
        int remaining = height;
        int remainingBelow = height - horizonRow;
        int remainingAbove = horizonRow;
        const auto markWritten = [&](int row) {
            written[static_cast<std::size_t>(row)] = 1;
            --remaining;
            if (row >= horizonRow) {
                --remainingBelow;
            } else {
                --remainingAbove;
            }
        };

        const float cameraX = (2.0F * static_cast<float>(sx) + 1.0F) / static_cast<float>(width) -
                              1.0F;
        const float rayX = forwardX + rightX * cameraX * camera.hfovTan;
        const float rayY = forwardY + rightY * cameraX * camera.hfovTan;

        std::int32_t mapX = static_cast<std::int32_t>(std::floor(camera.x));
        std::int32_t mapY = static_cast<std::int32_t>(std::floor(camera.y));

        const float deltaX = rayX == 0.0F ? std::numeric_limits<float>::infinity()
                                          : std::abs(1.0F / rayX);
        const float deltaY = rayY == 0.0F ? std::numeric_limits<float>::infinity()
                                          : std::abs(1.0F / rayY);
        const std::int32_t stepX = rayX < 0.0F ? -1 : 1;
        const std::int32_t stepY = rayY < 0.0F ? -1 : 1;
        float sideDistX = (rayX < 0.0F ? (camera.x - static_cast<float>(mapX))
                                       : (static_cast<float>(mapX) + 1.0F - camera.x)) *
                          deltaX;
        float sideDistY = (rayY < 0.0F ? (camera.y - static_cast<float>(mapY))
                                       : (static_cast<float>(mapY) + 1.0F - camera.y)) *
                          deltaY;

        float enter = 0.0F;
        int enterAxis = -1;  // -1 = we are inside this cell, 0 = X face, 1 = Y face

        while (remaining > 0 && enter < settings.maxDistance) {
            const float exit = std::min(sideDistX, sideDistY);
            const std::uint32_t mask = columnMask(mapX, mapY) & windowMask;
            if (mask != 0U) {
                const float safeEnter = std::max(enter, 0.0009F);
                // World point where the ray entered this cell, for the side
                // face's horizontal texture coordinate.
                const float entryX = camera.x + rayX * safeEnter;
                const float entryY = camera.y + rayY * safeEnter;

                std::uint32_t bits = mask;
                while (bits != 0U) {
                    const std::uint32_t bit = bits & (~bits + 1U);
                    const std::int32_t z =
                        static_cast<std::int32_t>(std::countr_zero(bit));
                    bits &= bits - 1U;

                    Voxel voxel;
                    if (!voxelAt(mapX, mapY, z, voxel)) {
                        continue;
                    }
                    if (voxel.top <= camera.z && remainingBelow == 0) {
                        continue;  // entirely below the eye, and the ground is full
                    }
                    if (voxel.bottom >= camera.z && remainingAbove == 0) {
                        continue;  // entirely above the eye, and the sky is full
                    }
                    const Rgb baked = glow_.at(mapX, mapY, z);
                    const Rgb live = dynamicGlowAt(settings.dynamicLamps, mapX, mapY, z);
                    const Rgb lampLight{std::max(baked.r, live.r), std::max(baked.g, live.g),
                                        std::max(baked.b, live.b)};
                    const Rgb surfaceLight{sky.ambient.r + lampLight.r, sky.ambient.g + lampLight.g,
                                           sky.ambient.b + lampLight.b};

                    // ---- the side face --------------------------------------
                    if (voxel.hasSides && enterAxis >= 0) {
                        const float yTop =
                            horizon - (voxel.top - camera.z) * focal / safeEnter;
                        const float yBottom =
                            horizon - (voxel.bottom - camera.z) * focal / safeEnter;
                        const int rowFrom = std::max(0, rowAtLeast(yTop));
                        const int rowTo = std::min(height - 1, rowAtMost(yBottom));
                        if (rowTo >= rowFrom && yBottom > yTop) {
                            const std::size_t tile = atlas_->tileFor(
                                voxel.material, FaceKind::Side,
                                hash32(static_cast<std::uint32_t>(mapX) * 73856093U ^
                                       static_cast<std::uint32_t>(mapY) * 19349663U ^
                                       static_cast<std::uint32_t>(z) * 83492791U));
                            const float along = enterAxis == 0 ? fractional(entryY)
                                                               : fractional(entryX);
                            const int u = std::clamp(
                                static_cast<int>(along * static_cast<float>(TileAtlas::kTilePx)),
                                0, TileAtlas::kTilePx - 1);
                            // Sides that face away from the light read darker.
                            // Two constants, one per axis: the cheapest form of
                            // directional shading and the one Barony uses.
                            const float facing = enterAxis == 0 ? 0.78F : 0.94F;
                            const float span = yBottom - yTop;
                            const float fog =
                                1.0F - std::exp(-safeEnter / sky.fogDistance);
                            for (int sy = rowFrom; sy <= rowTo; ++sy) {
                                if (written[static_cast<std::size_t>(sy)] != 0) {
                                    continue;
                                }
                                const float v =
                                    (static_cast<float>(sy) + 0.5F - yTop) / span;
                                const int tv = std::clamp(
                                    static_cast<int>(v * static_cast<float>(TileAtlas::kTilePx)),
                                    0, TileAtlas::kTilePx - 1);
                                Rgb colour = atlas_->texel(tile, u, tv);
                                colour = Rgb{colour.r * surfaceLight.r * facing,
                                             colour.g * surfaceLight.g * facing,
                                             colour.b * surfaceLight.b * facing};
                                colour = lerp(colour, sky.fog, fog);
                                target.set(sx, sy, colour, safeEnter);
                                markWritten(sy);
                                ++worldPixels;
                            }
                            nearest = std::min(nearest, safeEnter);
                            furthest = std::max(furthest, safeEnter);
                        }
                    }

                    // ---- the horizontal faces --------------------------------
                    for (int which = 0; which < 2; ++which) {
                        const bool isTop = which == 0;
                        const float height3d = isTop ? voxel.top : voxel.bottom;
                        if (isTop && camera.z <= height3d) {
                            continue;  // looking at it edge-on or from below
                        }
                        if (!isTop && camera.z >= height3d) {
                            continue;
                        }
                        if (!isTop && !voxel.hasSides) {
                            continue;  // water has no underside worth drawing
                        }
                        const float yA = horizon - (height3d - camera.z) * focal / safeEnter;
                        const float yB = horizon - (height3d - camera.z) * focal / exit;
                        // rowAtLeast/rowAtMost take ceil/floor directly, which is a
                        // top-left-of-pixel convention — but the sample loop below
                        // tests row centres (sy + 0.5F). Subtracting 0.5F here before
                        // ceil/floor makes "row r qualifies" mean exactly "r+0.5 lies
                        // inside [yA,yB]", matching the sampler, so a footprint that
                        // legitimately covers a pixel centre without straddling an
                        // integer boundary (e.g. [10.3,10.6]) no longer computes
                        // rowFrom > rowTo and drops the row entirely.
                        const int rowFrom = std::max(0, rowAtLeast(std::min(yA, yB) - 0.5F));
                        const int rowTo = std::min(height - 1, rowAtMost(std::max(yA, yB) - 0.5F));
                        if (rowTo < rowFrom) {
                            continue;
                        }
                        const FaceKind face = isTop ? voxel.topFace : FaceKind::Side;
                        const std::size_t tile = atlas_->tileFor(
                            voxel.material, face,
                            hash32(static_cast<std::uint32_t>(mapX) * 6971U ^
                                   static_cast<std::uint32_t>(mapY) * 40483U ^
                                   static_cast<std::uint32_t>(z) * 1572869U));
                        const float rise = height3d - camera.z;
                        // Bounded minification blend: when this face's screen footprint
                        // covers meaningfully more than one atlas texel per row (a
                        // distant or grazing-angle flat surface — water most of all),
                        // point-sampling scatters isolated bright texels instead of
                        // reading as a coherent surface. Detect it with two cheap
                        // distance evaluations (no texture reads) and, only then, blend
                        // each sampled texel toward the tile's precomputed flat average.
                        // O(1) per face, one cached lookup + one lerp per pixel — not
                        // literal supersampling, and inert when a row is already
                        // sub-texel (typical close-up wall/floor case).
                        float minifyMix = 0.0F;
                        Rgb tileAverage{};
                        if (rowTo > rowFrom) {
                            const auto worldAt = [&](int sy) {
                                const float d = horizon - (static_cast<float>(sy) + 0.5F);
                                const float dist = std::abs(d) < 0.25F
                                                       ? safeEnter
                                                       : std::clamp(rise * focal / d, safeEnter, exit);
                                return std::pair{camera.x + rayX * dist, camera.y + rayY * dist};
                            };
                            const auto [nx, ny] = worldAt(rowFrom);
                            const auto [fx, fy] = worldAt(rowTo);
                            const float worldStep = std::max(std::abs(fx - nx), std::abs(fy - ny));
                            const float texelsPerRow =
                                worldStep * static_cast<float>(TileAtlas::kTilePx) /
                                static_cast<float>(rowTo - rowFrom);
                            minifyMix = std::clamp((texelsPerRow - 1.0F) / 3.0F, 0.0F, 0.85F);
                            if (minifyMix > 0.0F) {
                                tileAverage = atlas_->averageOf(tile);
                            }
                        }
                        for (int sy = rowFrom; sy <= rowTo; ++sy) {
                            if (written[static_cast<std::size_t>(sy)] != 0) {
                                continue;
                            }
                            const float denominator = horizon - (static_cast<float>(sy) + 0.5F);
                            if (std::abs(denominator) < 0.25F) {
                                continue;
                            }
                            const float distance = rise * focal / denominator;
                            if (distance < safeEnter || distance > exit ||
                                distance > settings.maxDistance) {
                                continue;
                            }
                            const float worldX = camera.x + rayX * distance;
                            const float worldY = camera.y + rayY * distance;
                            const int u = std::clamp(
                                static_cast<int>(fractional(worldX) *
                                                 static_cast<float>(TileAtlas::kTilePx)),
                                0, TileAtlas::kTilePx - 1);
                            const int v = std::clamp(
                                static_cast<int>(fractional(worldY) *
                                                 static_cast<float>(TileAtlas::kTilePx)),
                                0, TileAtlas::kTilePx - 1);
                            Rgb colour = atlas_->texel(tile, u, v);
                            if (minifyMix > 0.0F) {
                                colour = lerp(colour, tileAverage, minifyMix);
                            }
                            if (isTop && voxel.wetness > 0) {
                                // Water darkens what it covers, by the pack's
                                // own per-depth alpha: a puddle glosses a
                                // flagstone, seven-deep harbour swallows it.
                                const std::size_t slot =
                                    static_cast<std::size_t>(std::clamp(voxel.wetness, 0, 7));
                                const float alpha =
                                    static_cast<float>(atlas_->waterDepthAlphaQ8()[slot]) / 256.0F;
                                colour = lerp(colour, Rgb{0.020F, 0.043F, 0.055F}, alpha);
                            }
                            const float lift = isTop ? 1.0F : 0.62F;
                            colour = Rgb{colour.r * surfaceLight.r * lift,
                                         colour.g * surfaceLight.g * lift,
                                         colour.b * surfaceLight.b * lift};
                            const float fog = 1.0F - std::exp(-distance / sky.fogDistance);
                            colour = lerp(colour, sky.fog, fog);
                            target.set(sx, sy, colour, distance);
                            markWritten(sy);
                            ++worldPixels;
                            nearest = std::min(nearest, distance);
                            furthest = std::max(furthest, distance);
                        }
                    }
                }
            }

            if (sideDistX < sideDistY) {
                enter = sideDistX;
                sideDistX += deltaX;
                mapX += stepX;
                enterAxis = 0;
            } else {
                enter = sideDistY;
                sideDistY += deltaY;
                mapY += stepY;
                enterAxis = 1;
            }
            if (mapX < -1 || mapY < -1 || mapX > tiles_->sizeX() || mapY > tiles_->sizeY()) {
                break;
            }
        }
    }

    // --- sprites -----------------------------------------------------------
    std::size_t spritePixels = 0;
    std::size_t actorPixels = 0;
    std::size_t wardDrawn = 0;
    if (settings.drawSprites) {
        // Far to near, so a nearer glow lands on top of a further one.
        std::vector<std::pair<float, const SpriteInstance*>> ordered;
        ordered.reserve(sprites.size());
        for (const SpriteInstance& sprite : sprites) {
            const float relX = sprite.x - camera.x;
            const float relY = sprite.y - camera.y;
            ordered.emplace_back(relX * forwardX + relY * forwardY, &sprite);
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& entry : ordered) {
            const std::size_t before = spritePixels;
            drawSprite(target, camera, settings, sky, *entry.second, focal, horizon, spritePixels);
            if (entry.second->person) {
                actorPixels += spritePixels - before;
            }
            // A body counts as SEEN when it put at least one pixel on screen --
            // not when a billboard was submitted for it. A figure standing
            // behind a warehouse is submitted, sorted, projected and drawn
            // nowhere, and counting those would make "the street is busy" a
            // claim about the roster instead of about the frame.
            if (entry.second->ward && spritePixels > before) {
                ++wardDrawn;
            }
        }
    }

    // --- what came out -----------------------------------------------------
    FrameStats stats;
    stats.worldPixels = worldPixels;
    stats.spritePixels = spritePixels;
    stats.actorPixels = actorPixels;
    stats.wardActorsDrawn = wardDrawn;
    stats.skyPixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) -
                      worldPixels;
    stats.nearestDepth = std::isfinite(nearest) ? nearest : 0.0F;
    stats.furthestDepth = furthest;

    double sum = 0.0;
    std::map<std::uint32_t, int> palette;
    for (const std::uint32_t pixel : target.pixels()) {
        sum += static_cast<double>(luma(unpackRgb(pixel)));
        if (palette.size() < 4096) {
            ++palette[pixel];
        }
    }
    stats.meanLuma = static_cast<float>(
        sum / static_cast<double>(target.pixels().size()));
    stats.distinctColours = palette.size();
    return stats;
}

void WorldRenderer::drawSprite(Framebuffer& target, const Camera& camera,
                               const RenderSettings& settings, const SkyState& sky,
                               const SpriteInstance& sprite, float focal, float horizon,
                               std::size_t& spritePixels) const {
    const float forwardX = std::sin(camera.yaw);
    const float forwardY = -std::cos(camera.yaw);
    const float rightX = std::cos(camera.yaw);
    const float rightY = std::sin(camera.yaw);

    const float relX = sprite.x - camera.x;
    const float relY = sprite.y - camera.y;
    const float distance = relX * forwardX + relY * forwardY;
    if (distance < 0.25F || distance > settings.maxDistance) {
        return;
    }
    const float lateral = relX * rightX + relY * rightY;
    const float halfWidth = static_cast<float>(target.width()) * 0.5F;
    const float centreX = halfWidth + lateral * focal / distance;
    const float centreY = horizon - (sprite.z - camera.z) * focal / distance;
    const float pixelHalfW = sprite.halfWidth * focal / distance;
    const float pixelHalfH = sprite.halfHeight * focal / distance;
    if (pixelHalfW < 0.4F || pixelHalfH < 0.4F) {
        return;
    }

    const int x0 = std::max(0, static_cast<int>(std::floor(centreX - pixelHalfW)));
    const int x1 = std::min(target.width() - 1, static_cast<int>(std::ceil(centreX + pixelHalfW)));
    const int y0 = std::max(0, static_cast<int>(std::floor(centreY - pixelHalfH)));
    const int y1 = std::min(target.height() - 1, static_cast<int>(std::ceil(centreY + pixelHalfH)));

    const float fog = 1.0F - std::exp(-distance / sky.fogDistance);

    // A flame you are standing next to must not white out the frame. An
    // additive billboard grows on screen as you approach it, so its total
    // contribution grows with the SQUARE of how close you are, and two tiles
    // from the Gilded Gull's door lamp at five in the morning that was a
    // featureless white disc across half the view. The door of the tavern is
    // the one place in the district a player is guaranteed to stand.
    //
    // A real flame is a small hot thing: walking up to it makes it bigger, not
    // brighter. So the additive term is rolled off inside two and a half tiles.
    // Only glow sprites are affected; a lit solid already shades correctly at
    // any range.
    constexpr float kGlowNearField = 2.5F;
    const float glowNear =
        sprite.glow > 0.0F ? std::clamp(distance / kGlowNearField, 0.22F, 1.0F) : 1.0F;

    // --- #78: the textured path ---------------------------------------------
    //
    // A figure somebody drew, sampled nearest-neighbour with a hard alpha
    // cutout. Nearest and not filtered on purpose: the whole look is 16x16
    // pixel art at 640x360 and a bilinear tap would turn a guard's helmet into
    // a grey smear at exactly the range the silhouette has to read at.
    if (sprite.art != nullptr && sprite.artSize > 0) {
        const float spanU = static_cast<float>(sprite.artU1 - sprite.artU0 + 1);
        const float spanV = static_cast<float>(sprite.artV1 - sprite.artV0 + 1);
        const float left = centreX - pixelHalfW;
        const float top = centreY - pixelHalfH;
        const float wide = pixelHalfW * 2.0F;
        const float tall = pixelHalfH * 2.0F;
        for (int sy = y0; sy <= y1; ++sy) {
            for (int sx = x0; sx <= x1; ++sx) {
                const std::size_t at = target.index(sx, sy);
                if (target.depth()[at] < distance) {
                    continue;  // behind world geometry
                }
                const int u = sprite.artU0 +
                              static_cast<int>((static_cast<float>(sx) + 0.5F - left) / wide * spanU);
                const int v = sprite.artV0 +
                              static_cast<int>((static_cast<float>(sy) + 0.5F - top) / tall * spanV);
                if (u < sprite.artU0 || u > sprite.artU1 || v < sprite.artV0 || v > sprite.artV1) {
                    continue;
                }
                const std::uint32_t texel =
                    sprite.art[static_cast<std::size_t>(v * sprite.artSize + u)];
                if ((texel >> 24) < 128u) {
                    continue;  // the street behind them
                }
                const float tr = static_cast<float>(texel & 0xFFu) / 255.0F;
                const float tg = static_cast<float>((texel >> 8) & 0xFFu) / 255.0F;
                const float tb = static_cast<float>((texel >> 16) & 0xFFu) / 255.0F;
                // sprite.colour is the LIGHT where they stand, not a tint of
                // their own -- see Session::wardSprites. A figure in an unlit
                // corner has to be dark or the committed-dark look dies.
                Rgb lit{tr * sprite.colour.r, tg * sprite.colour.g, tb * sprite.colour.b};
                lit = lerp(lit, sky.fog, fog);
                target.pixels()[at] = packRgb(lit);
                target.depth()[at] = distance;
                ++spritePixels;
            }
        }
        return;
    }

    for (int sy = y0; sy <= y1; ++sy) {
        for (int sx = x0; sx <= x1; ++sx) {
            const std::size_t at = target.index(sx, sy);
            if (target.depth()[at] < distance) {
                continue;  // behind world geometry
            }
            const float dx = (static_cast<float>(sx) + 0.5F - centreX) / pixelHalfW;
            const float dy = (static_cast<float>(sy) + 0.5F - centreY) / pixelHalfH;
            const float r2 = dx * dx + dy * dy;
            if (r2 > 1.0F) {
                continue;
            }
            // softness 1 is the flame's smooth falloff; softness 0 fills the
            // ellipse flat, which is what makes a body read as a body and not
            // as a smudge at this resolution.
            const float smooth = (1.0F - r2) * (1.0F - r2);
            const float falloff = 1.0F + (smooth - 1.0F) * sprite.softness;
            const float alpha = std::clamp(falloff * (1.0F - fog * 0.8F), 0.0F, 1.0F);
            if (alpha <= 0.004F) {
                continue;
            }
            if (sprite.glow > 0.0F) {
                // Additive: a flame brightens what is behind it rather than
                // replacing it, which is what makes a lamp read as a light.
                const Rgb behind = unpackRgb(target.pixels()[at]);
                const float add = alpha * sprite.glow * glowNear;
                const Rgb lit{behind.r + sprite.colour.r * add,
                              behind.g + sprite.colour.g * add,
                              behind.b + sprite.colour.b * add};
                target.pixels()[at] = packRgb(lit);
            } else {
                target.blend(sx, sy, sprite.colour, alpha);
                target.depth()[at] = distance;
            }
            ++spritePixels;
        }
    }
}

}  // namespace granadad::render
