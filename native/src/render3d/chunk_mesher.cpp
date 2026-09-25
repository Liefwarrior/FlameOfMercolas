#include "granadad/render3d/chunk_mesher.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "granadad/render/lighting.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

namespace {

/// raylib indexes with unsigned short, so a mesh stops here. A 16x16 chunk of
/// the Docks emits a few thousand faces; this is a guard, not a budget.
constexpr std::size_t kMaxVertices = 65535;

/// "Touching" for the occlusion rules: two faces on the same plane within
/// this are the same plane, which is what a covered face is.
constexpr float kEps = 1.0e-4F;

/// What standing water multiplies the lit texel by, at full alpha. The
/// software pass lerps the texel toward kDeepWaterTone instead; a multiply
/// is what a textured mesh can do, and this is the tone that makes the
/// harbour read as the same cold dark water from the quay.
constexpr render::Rgb kWaterTintFull{0.10F, 0.18F, 0.22F};

[[nodiscard]] std::uint8_t channel8(float value) noexcept {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

/// A chunk's cells plus a one-cell apron, classified once. 18 x 18 x (levels
/// + 2) entries; every neighbour question below is answered from here.
class CellCache {
public:
    CellCache(const sim::TileQuery& tiles, ChunkKey key)
        : x0_(key.cx * kChunkTiles - 1),
          y0_(key.cy * kChunkTiles - 1),
          levels_(tiles.sizeZ() + 2) {
        const std::size_t count = static_cast<std::size_t>(kSide) *
                                  static_cast<std::size_t>(kSide) *
                                  static_cast<std::size_t>(levels_);
        voxels_.resize(count);
        present_.assign(count, 0);
        for (std::int32_t lz = 0; lz < levels_; ++lz) {
            for (std::int32_t ly = 0; ly < kSide; ++ly) {
                for (std::int32_t lx = 0; lx < kSide; ++lx) {
                    const std::size_t at = index(lx, ly, lz);
                    // z = -1 and z = levels are out of the world: VOID, no
                    // water, nothing drawn -- classifyVoxel says so itself.
                    const bool drawn =
                        render::classifyVoxel(tiles, x0_ + lx, y0_ + ly, lz - 1, voxels_[at]);
                    present_[at] = static_cast<std::uint8_t>(drawn ? 1 : 0);
                }
            }
        }
    }

    /// The voxel at WORLD (x, y, z), or null when the cell draws nothing.
    [[nodiscard]] const render::Voxel* at(std::int32_t x, std::int32_t y,
                                          std::int32_t z) const noexcept {
        const std::int32_t lx = x - x0_;
        const std::int32_t ly = y - y0_;
        const std::int32_t lz = z + 1;
        if (lx < 0 || ly < 0 || lz < 0 || lx >= kSide || ly >= kSide || lz >= levels_) {
            return nullptr;
        }
        const std::size_t i = index(lx, ly, lz);
        return present_[i] != 0U ? &voxels_[i] : nullptr;
    }

private:
    static constexpr std::int32_t kSide = kChunkTiles + 2;

    [[nodiscard]] std::size_t index(std::int32_t lx, std::int32_t ly,
                                    std::int32_t lz) const noexcept {
        return (static_cast<std::size_t>(lz) * static_cast<std::size_t>(kSide) +
                static_cast<std::size_t>(ly)) *
                   static_cast<std::size_t>(kSide) +
               static_cast<std::size_t>(lx);
    }

    std::int32_t x0_;
    std::int32_t y0_;
    std::int32_t levels_;
    std::vector<render::Voxel> voxels_;
    std::vector<std::uint8_t> present_;
};

/// Whether the neighbouring column (nx, ny) covers the vertical span
/// [bottom, top] with drawable, sided cells. Its cells at z-1, z and z+1 are
/// the only ones that can reach the span (a cell is at most a storey tall
/// and a slab hangs at most a fraction below its level).
[[nodiscard]] bool sideCovered(const CellCache& cache, std::int32_t nx, std::int32_t ny,
                               std::int32_t z, float bottom, float top) noexcept {
    float cursor = bottom;
    for (std::int32_t zz = z - 1; zz <= z + 1; ++zz) {
        const render::Voxel* v = cache.at(nx, ny, zz);
        if (v == nullptr || !v->hasSides) {
            continue;
        }
        if (v->bottom <= cursor + kEps && v->top > cursor) {
            cursor = v->top;
        }
    }
    return cursor >= top - kEps;
}

[[nodiscard]] bool topCovered(const CellCache& cache, std::int32_t x, std::int32_t y,
                              std::int32_t z, float top) noexcept {
    const render::Voxel* above = cache.at(x, y, z + 1);
    return above != nullptr && above->hasSides && above->bottom <= top + kEps;
}

[[nodiscard]] bool bottomCovered(const CellCache& cache, std::int32_t x, std::int32_t y,
                                 std::int32_t z, float bottom) noexcept {
    if (z <= 0) {
        // The world's floor: nothing is ever under it to see it from.
        return true;
    }
    const render::Voxel* below = cache.at(x, y, z - 1);
    return below != nullptr && below->hasSides && below->top >= bottom - kEps;
}

/// The six faces of one cell, from its cache. Bits per FaceDir.
[[nodiscard]] std::uint8_t faceMaskOf(const CellCache& cache, std::int32_t x, std::int32_t y,
                                      std::int32_t z, const render::Voxel& v) noexcept {
    std::uint8_t mask = 0;
    if (!topCovered(cache, x, y, z, v.top)) {
        mask = static_cast<std::uint8_t>(mask | kFaceTopBit);
    }
    if (v.hasSides) {
        if (!bottomCovered(cache, x, y, z, v.bottom)) {
            mask = static_cast<std::uint8_t>(mask | kFaceBottomBit);
        }
        if (!sideCovered(cache, x, y - 1, z, v.bottom, v.top)) {
            mask = static_cast<std::uint8_t>(mask | kFaceNorthBit);
        }
        if (!sideCovered(cache, x, y + 1, z, v.bottom, v.top)) {
            mask = static_cast<std::uint8_t>(mask | kFaceSouthBit);
        }
        if (!sideCovered(cache, x + 1, y, z, v.bottom, v.top)) {
            mask = static_cast<std::uint8_t>(mask | kFaceEastBit);
        }
        if (!sideCovered(cache, x - 1, y, z, v.bottom, v.top)) {
            mask = static_cast<std::uint8_t>(mask | kFaceWestBit);
        }
    }
    return mask;
}

struct Corner {
    float x;
    float y;
    float z;
    float u;
    float v;
};

class Emitter {
public:
    explicit Emitter(ChunkGeometry& out) : out_(out) {}

    [[nodiscard]] bool full() const noexcept { return out_.vertexCount() + 4 > kMaxVertices; }

    /// Four corners COUNTER-CLOCKWISE seen from the side the face looks to.
    void quad(const Corner (&c)[4], std::int32_t x, std::int32_t y, std::int32_t z,
              float facing, float wetAlpha, FaceDir dir, render::FaceKind kind) {
        const auto base = static_cast<std::uint16_t>(out_.vertexCount());
        ChunkFace face;
        face.x = x;
        face.y = y;
        face.z = z;
        face.dir = dir;
        face.kind = kind;
        face.firstVertex = base;
        out_.faces.push_back(face);
        for (const Corner& corner : c) {
            out_.positions.push_back(corner.x);
            out_.positions.push_back(corner.y);
            out_.positions.push_back(corner.z);
            out_.texcoords.push_back(corner.u);
            out_.texcoords.push_back(corner.v);
            out_.cells.push_back(x);
            out_.cells.push_back(y);
            out_.cells.push_back(z);
            out_.facing.push_back(facing);
            out_.wetAlpha.push_back(wetAlpha);
        }
        out_.indices.push_back(base);
        out_.indices.push_back(static_cast<std::uint16_t>(base + 1));
        out_.indices.push_back(static_cast<std::uint16_t>(base + 2));
        out_.indices.push_back(base);
        out_.indices.push_back(static_cast<std::uint16_t>(base + 2));
        out_.indices.push_back(static_cast<std::uint16_t>(base + 3));
    }

private:
    ChunkGeometry& out_;
};

}  // namespace

// ---------------------------------------------------------------------------
// versions
// ---------------------------------------------------------------------------

std::uint32_t dynamicLampKey(const std::vector<render::Lamp>& lamps) noexcept {
    if (lamps.empty()) {
        return 0U;
    }
    Fnv1a64 hash;
    hash.mixU32(static_cast<std::uint32_t>(lamps.size()));
    for (const render::Lamp& lamp : lamps) {
        hash.mixU32(static_cast<std::uint32_t>(lamp.name.size()));
        hash.mix(lamp.name.data(), lamp.name.size());
        hash.mixI32(lamp.x);
        hash.mixI32(lamp.y);
        hash.mixI32(lamp.z);
        hash.mixI32(lamp.luminance);
        hash.mixU8(static_cast<std::uint8_t>(lamp.warmth));
    }
    const std::uint64_t value = hash.value();
    const auto folded = static_cast<std::uint32_t>(value ^ (value >> 32));
    return folded == 0U ? 1U : folded;
}

std::uint32_t weatherVersionKey(const render::Weather& weather) noexcept {
    if (weather.clear()) {
        return 0U;
    }
    return (static_cast<std::uint32_t>(weather.kind) << 8) +
           static_cast<std::uint32_t>(std::clamp(weather.intensity, 0.0F, 1.0F) * 100.0F + 0.5F);
}

std::uint32_t chunkVersion(std::uint32_t rebuildCount, const ChunkLighting& lighting) noexcept {
    const int minute = ((lighting.timeOfDaySeconds % 86400) + 86400) % 86400 / 60;
    // Rebuilds in the high bits, the lighting bucket in the low twelve (1440
    // minutes fit), plus one so a fresh mesh never reads as version 0. The
    // dynamic lamp key is folded in above the minute -- it never touches the
    // low twelve bits, so the result stays non-zero whatever the key.
    const std::uint32_t base = (rebuildCount << 12) + static_cast<std::uint32_t>(minute) + 1U;
    // WEATHER: the kind and the intensity to a hundredth, spread above the
    // minute the same way (zero for clear, so a clear version is what it
    // always was). Weather is a function of the minute in a live session,
    // but a version that SAYS so cannot go stale if that ever changes.
    return base ^ (lighting.lampKey << 20) ^ ((weatherVersionKey(lighting.weather) * 0x9E37U) << 12);
}

// ---------------------------------------------------------------------------
// materials
// ---------------------------------------------------------------------------

ChunkMaterials ChunkMaterials::fromAtlas(const render::TileAtlas& atlas) {
    ChunkMaterials materials;
    materials.atlas_ = &atlas;
    materials.tileCount_ = std::max<std::size_t>(1, atlas.tileCount());
    materials.rows_ = static_cast<int>((materials.tileCount_ + kAtlasColumns - 1) / kAtlasColumns);

    TextureData& texture = materials.texture_;
    texture.id = kChunkAtlasTextureId;
    texture.version = 1;
    texture.width = kAtlasColumns * render::TileAtlas::kTilePx;
    texture.height = materials.rows_ * render::TileAtlas::kTilePx;
    texture.pixels.assign(static_cast<std::size_t>(texture.width) *
                              static_cast<std::size_t>(texture.height) * 4U,
                          0U);
    for (std::size_t tile = 0; tile < materials.tileCount_; ++tile) {
        const int col = static_cast<int>(tile % kAtlasColumns);
        const int row = static_cast<int>(tile / kAtlasColumns);
        for (int v = 0; v < render::TileAtlas::kTilePx; ++v) {
            for (int u = 0; u < render::TileAtlas::kTilePx; ++u) {
                const std::uint32_t texel = atlas.texelRaw(tile, u, v);
                const int px = col * render::TileAtlas::kTilePx + u;
                const int py = row * render::TileAtlas::kTilePx + v;
                const std::size_t at =
                    (static_cast<std::size_t>(py) * static_cast<std::size_t>(texture.width) +
                     static_cast<std::size_t>(px)) *
                    4U;
                texture.pixels[at] = static_cast<std::uint8_t>(texel & 0xFFU);
                texture.pixels[at + 1] = static_cast<std::uint8_t>((texel >> 8) & 0xFFU);
                texture.pixels[at + 2] = static_cast<std::uint8_t>((texel >> 16) & 0xFFU);
                // Opaque always: the pack's alpha is a mask for sprites, and
                // a world face is a world face.
                texture.pixels[at + 3] = 255U;
            }
        }
    }
    return materials;
}

UvRect ChunkMaterials::uv(std::uint16_t material, render::FaceKind face,
                          std::uint32_t variantKey) const noexcept {
    UvRect rect;
    if (atlas_ == nullptr || rows_ <= 0) {
        return rect;
    }
    std::size_t tile = atlas_->tileFor(material, face, variantKey);
    if (tile >= tileCount_) {
        tile = 0;
    }
    const float col = static_cast<float>(tile % kAtlasColumns);
    const float row = static_cast<float>(tile / kAtlasColumns);
    const float px = static_cast<float>(render::TileAtlas::kTilePx);
    const float width = static_cast<float>(kAtlasColumns) * px;
    const float height = static_cast<float>(rows_) * px;
    // Half a texel in from the tile's edges: with point sampling that is
    // exactly the centres of the first and last texel, so a stretched face
    // never bleeds a neighbour tile's border in.
    rect.u0 = (col * px + 0.5F) / width;
    rect.u1 = (col * px + px - 0.5F) / width;
    rect.v0 = (row * px + 0.5F) / height;
    rect.v1 = (row * px + px - 0.5F) / height;
    return rect;
}

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------

std::uint8_t cellFaceMask(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                          std::int32_t z) noexcept {
    if (x < 0 || y < 0) {
        return 0;
    }
    const CellCache cache(tiles, chunkOf(x, y));
    const render::Voxel* v = cache.at(x, y, z);
    if (v == nullptr) {
        return 0;
    }
    return faceMaskOf(cache, x, y, z, *v);
}

ChunkGeometry buildChunkGeometry(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                                 const ChunkMaterials& materials, ChunkKey key,
                                 std::uint32_t rebuildCount) {
    ChunkGeometry out;
    out.key = key;
    out.rebuildCount = rebuildCount;
    if (key.cx < 0 || key.cy < 0) {
        return out;
    }
    const CellCache cache(tiles, key);
    Emitter emit(out);
    const std::vector<std::int32_t>& waterAlpha = atlas.waterDepthAlphaQ8();

    const std::int32_t xStart = key.cx * kChunkTiles;
    const std::int32_t yStart = key.cy * kChunkTiles;
    for (std::int32_t z = 0; z < tiles.sizeZ(); ++z) {
        for (std::int32_t y = yStart; y < yStart + kChunkTiles; ++y) {
            for (std::int32_t x = xStart; x < xStart + kChunkTiles; ++x) {
                const render::Voxel* vp = cache.at(x, y, z);
                if (vp == nullptr) {
                    continue;
                }
                const render::Voxel& v = *vp;
                const std::uint8_t mask = faceMaskOf(cache, x, y, z, v);
                if (mask == 0U) {
                    continue;
                }
                // Scene space: X = east (world x), Z = south (world y).
                const float x0 = static_cast<float>(x);
                const float x1 = x0 + 1.0F;
                const float z0 = static_cast<float>(y);
                const float z1 = z0 + 1.0F;
                const float b = v.bottom;
                const float t = v.top;

                float wet = 0.0F;
                if (v.wetness > 0 && !waterAlpha.empty()) {
                    const std::size_t slot = static_cast<std::size_t>(
                        std::clamp(v.wetness, 0, static_cast<int>(waterAlpha.size()) - 1));
                    wet = static_cast<float>(waterAlpha[slot]) / 256.0F;
                }

                if ((mask & kFaceTopBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const UvRect r = materials.uv(v.material, v.topFace, render::flatVariantKey(x, y, z));
                    // Seen from above (+Y), CCW: (x0,z1) (x1,z1) (x1,z0) (x0,z0);
                    // u along east, v along south -- the software pass's own
                    // fractional(worldX), fractional(worldY).
                    const Corner c[4] = {{x0, t, z1, r.u0, r.v1},
                                         {x1, t, z1, r.u1, r.v1},
                                         {x1, t, z0, r.u1, r.v0},
                                         {x0, t, z0, r.u0, r.v0}};
                    emit.quad(c, x, y, z, 1.0F, wet, FaceDir::Top, v.topFace);
                }
                if (!v.hasSides) {
                    continue;
                }
                const UvRect s = materials.uv(v.material, render::FaceKind::Side,
                                              render::sideVariantKey(x, y, z));
                if ((mask & kFaceBottomBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const Corner c[4] = {{x0, b, z0, s.u0, s.v0},
                                         {x1, b, z0, s.u1, s.v0},
                                         {x1, b, z1, s.u1, s.v1},
                                         {x0, b, z1, s.u0, s.v1}};
                    emit.quad(c, x, y, z, render::kUndersideLift, 0.0F, FaceDir::Bottom,
                              render::FaceKind::Side);
                }
                // Sides: v runs 0 at the top of the face to 1 at its foot, the
                // one tile stretched over the whole face height exactly as the
                // software pass stretches it; u runs along the face.
                if ((mask & kFaceSouthBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const Corner c[4] = {{x0, b, z1, s.u0, s.v1},
                                         {x1, b, z1, s.u1, s.v1},
                                         {x1, t, z1, s.u1, s.v0},
                                         {x0, t, z1, s.u0, s.v0}};
                    emit.quad(c, x, y, z, render::kFacingY, 0.0F, FaceDir::South,
                              render::FaceKind::Side);
                }
                if ((mask & kFaceNorthBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const Corner c[4] = {{x1, b, z0, s.u1, s.v1},
                                         {x0, b, z0, s.u0, s.v1},
                                         {x0, t, z0, s.u0, s.v0},
                                         {x1, t, z0, s.u1, s.v0}};
                    emit.quad(c, x, y, z, render::kFacingY, 0.0F, FaceDir::North,
                              render::FaceKind::Side);
                }
                if ((mask & kFaceEastBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const Corner c[4] = {{x1, b, z1, s.u1, s.v1},
                                         {x1, b, z0, s.u0, s.v1},
                                         {x1, t, z0, s.u0, s.v0},
                                         {x1, t, z1, s.u1, s.v0}};
                    emit.quad(c, x, y, z, render::kFacingX, 0.0F, FaceDir::East,
                              render::FaceKind::Side);
                }
                if ((mask & kFaceWestBit) != 0U) {
                    if (emit.full()) {
                        out.truncated = true;
                        return out;
                    }
                    const Corner c[4] = {{x0, b, z0, s.u0, s.v1},
                                         {x0, b, z1, s.u1, s.v1},
                                         {x0, t, z1, s.u1, s.v0},
                                         {x0, t, z0, s.u0, s.v0}};
                    emit.quad(c, x, y, z, render::kFacingX, 0.0F, FaceDir::West,
                              render::FaceKind::Side);
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// colour
// ---------------------------------------------------------------------------

MeshData colourChunk(const ChunkGeometry& geometry, const ChunkLighting& lighting) {
    MeshData mesh;
    mesh.id = chunkMeshId(geometry.key);
    mesh.version = chunkVersion(geometry.rebuildCount, lighting);
    mesh.positions = geometry.positions;
    mesh.texcoords = geometry.texcoords;
    mesh.indices = geometry.indices;

    const std::size_t vertices = geometry.vertexCount();
    mesh.colours.resize(vertices * 4U);
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds, lighting.weather);
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();

    // Light is a fact about the CELL, so it is computed once per face and
    // written to its four corners.
    std::int32_t lastX = INT32_MIN;
    std::int32_t lastY = INT32_MIN;
    std::int32_t lastZ = INT32_MIN;
    render::Rgb surface{};
    for (std::size_t i = 0; i < vertices; ++i) {
        const std::int32_t x = geometry.cells[i * 3];
        const std::int32_t y = geometry.cells[i * 3 + 1];
        const std::int32_t z = geometry.cells[i * 3 + 2];
        if (x != lastX || y != lastY || z != lastZ) {
            const render::Rgb baked =
                lighting.glow != nullptr ? lighting.glow->at(x, y, z) : render::Rgb{};
            const render::Rgb live = hasDynamic
                                         ? render::dynamicGlowAt(*lighting.dynamicLamps, x, y, z)
                                         : render::Rgb{};
            surface = render::Rgb{sky.ambient.r + std::max(baked.r, live.r),
                                  sky.ambient.g + std::max(baked.g, live.g),
                                  sky.ambient.b + std::max(baked.b, live.b)};
            lastX = x;
            lastY = y;
            lastZ = z;
        }
        const float facing = geometry.facing[i];
        render::Rgb lit{surface.r * facing, surface.g * facing, surface.b * facing};
        const float wet = geometry.wetAlpha[i];
        if (wet > 0.0F) {
            lit = render::Rgb{lit.r * (1.0F + (kWaterTintFull.r - 1.0F) * wet),
                              lit.g * (1.0F + (kWaterTintFull.g - 1.0F) * wet),
                              lit.b * (1.0F + (kWaterTintFull.b - 1.0F) * wet)};
        }
        mesh.colours[i * 4] = channel8(lit.r);
        mesh.colours[i * 4 + 1] = channel8(lit.g);
        mesh.colours[i * 4 + 2] = channel8(lit.b);
        mesh.colours[i * 4 + 3] = 255U;
    }
    return mesh;
}

MeshData meshChunk(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                   const ChunkMaterials& materials, ChunkKey key, std::uint32_t rebuildCount,
                   const ChunkLighting& lighting) {
    return colourChunk(buildChunkGeometry(tiles, atlas, materials, key, rebuildCount), lighting);
}

}  // namespace granadad::render3d
