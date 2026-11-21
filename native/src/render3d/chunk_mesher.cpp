#include "granadad/render3d/chunk_mesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

#include "granadad/render/lighting.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render3d/door_jambs.hpp"
#include "granadad/render3d/static_pieces.hpp"
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

/// THE DOORWAY IS CUT OUT OF THE BOX. A wall cell that flanks a door
/// opening (door_jambs.hpp -- the one test the placer stands its jamb beam
/// by) keeps only its course over the soffit (kDoorHeadCut): the
/// metre-square storey under it goes. That is what lets a beam a
/// quarter-metre thick READ as the jamb instead of hiding inside a block,
/// and it is done HERE, in the cache, so
/// every question asked afterwards is asked of the cut cell: the cell's own
/// faces, the neighbouring wall's cover test (it shows its reveal down the
/// whole opening now), the cell below (it shows its top, the threshold the
/// beam stands on). Nothing is left see-through and nothing is drawn twice.
/// Above the head the wall meshes as it always did, so the frontage has no
/// hole in it. Render only: sim::TileQuery::solid() is what a body walks
/// into, and the cell is still a wall.
void openJamb(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y, std::int32_t z,
              render::Voxel& v) noexcept {
    if (!v.hasSides || v.water) {
        return;
    }
    const float cut = v.bottom + kDoorHeadCut;
    if (cut >= v.top || !doorPostAt(tiles, x, y, z)) {
        return;
    }
    v.bottom = cut;
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
                    if (drawn) {
                        openJamb(tiles, x0_ + lx, y0_ + ly, lz - 1, voxels_[at]);
                    }
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

// ---------------------------------------------------------------------------
// the emitter: faces with world-metre UVs, a surface cell and a tone
// ---------------------------------------------------------------------------

struct V3 {
    float x;
    float y;
    float z;
};

struct CellId {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
};

/// What a face wears: a surface of the atlas, and the colour multiplier that
/// goes with it (white for a textured surface, the flat tone otherwise).
struct Dress {
    Surface surface = Surface::White;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    /// A face that looks into a room (or up at a ceiling): the sky's own light
    /// does not reach it, only the lamps in the room. The per-pixel look carries
    /// it as the vertex colour's alpha (colourLit).
    bool indoor = false;
};

/// The standing water's tone (it is multiplied by the pack's per-depth
/// tint at the colour stage, as the water tile always was).
constexpr SurfaceTone kWaterTone{0.46F, 0.62F, 0.72F};

[[nodiscard]] V3 cross(const V3& a, const V3& b) noexcept {
    return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] V3 sub(const V3& a, const V3& b) noexcept {
    return V3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] float dot(const V3& a, const V3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] V3 normalised(const V3& v) noexcept {
    const float length = std::sqrt(dot(v, v));
    if (length > 1.0e-12F) {
        return V3{v.x / length, v.y / length, v.z / length};
    }
    return V3{0.0F, 1.0F, 0.0F};
}

/// The directional factor of a normal: the body's own constants on an axis,
/// blended across a slope (a roof plane is lit between a top and a side).
[[nodiscard]] float facingOf(const V3& n) noexcept {
    const float horizontal = std::abs(n.x) + std::abs(n.z);
    const float side = horizontal > 1.0e-6F
                           ? (std::abs(n.x) * render::kFacingX + std::abs(n.z) * render::kFacingY) /
                                 horizontal
                           : 1.0F;
    if (n.y >= 0.0F) {
        return side + (1.0F - side) * n.y;
    }
    return side + (render::kUndersideLift - side) * -n.y;
}

class Emitter {
public:
    Emitter(SurfaceArrays& out, const SurfaceAtlas& atlas, std::vector<ChunkFace>* faces,
            const SurfaceArrays* sibling)
        : out_(out), atlas_(atlas), faces_(faces), sibling_(sibling) {}

    [[nodiscard]] bool full(std::size_t more = 4) const noexcept {
        const std::size_t used =
            out_.vertexCount() + (sibling_ != nullptr ? sibling_->vertexCount() : 0U);
        return used + more > kMaxVertices;
    }

    /// Four corners counter-clockwise seen from the side the face looks to.
    void quad(const V3 (&c)[4], CellId cell, float wet, FaceDir dir, render::FaceKind kind,
              const Dress& dress, const float* shore = nullptr) {
        const auto base = static_cast<std::uint16_t>(out_.vertexCount());
        if (faces_ != nullptr) {
            ChunkFace face;
            face.x = cell.x;
            face.y = cell.y;
            face.z = cell.z;
            face.dir = dir;
            face.kind = kind;
            face.firstVertex = base;
            faces_->push_back(face);
        }
        const V3 n = normalised(cross(sub(c[1], c[0]), sub(c[2], c[0])));
        for (int k = 0; k < 4; ++k) {
            vertex(c[k], n, cell, wet, dress, shore != nullptr ? shore[k] : -1.0F);
        }
        out_.indices.push_back(base);
        out_.indices.push_back(static_cast<std::uint16_t>(base + 1));
        out_.indices.push_back(static_cast<std::uint16_t>(base + 2));
        out_.indices.push_back(base);
        out_.indices.push_back(static_cast<std::uint16_t>(base + 2));
        out_.indices.push_back(static_cast<std::uint16_t>(base + 3));
    }

    /// A quad whose winding is fixed up so it looks along `outward`.
    void quadOut(V3 (&c)[4], const V3& outward, CellId cell, const Dress& dress) {
        const V3 n = cross(sub(c[1], c[0]), sub(c[2], c[0]));
        if (dot(n, outward) < 0.0F) {
            std::swap(c[1], c[3]);
        }
        quad(c, cell, 0.0F, FaceDir::Top, render::FaceKind::Side, dress);
    }

    void tri(V3 (&c)[3], const V3& outward, CellId cell, const Dress& dress) {
        const V3 n0 = cross(sub(c[1], c[0]), sub(c[2], c[0]));
        if (dot(n0, outward) < 0.0F) {
            std::swap(c[1], c[2]);
        }
        const V3 n = normalised(cross(sub(c[1], c[0]), sub(c[2], c[0])));
        const auto base = static_cast<std::uint16_t>(out_.vertexCount());
        for (const V3& corner : c) {
            vertex(corner, n, cell, 0.0F, dress);
        }
        out_.indices.push_back(base);
        out_.indices.push_back(static_cast<std::uint16_t>(base + 1));
        out_.indices.push_back(static_cast<std::uint16_t>(base + 2));
    }

    /// The faces of an axis-aligned box that are in `mask` (FaceDir bits),
    /// each dressed by `dress`.
    void box(float x0, float x1, float y0, float y1, float z0, float z1, std::uint8_t mask,
             CellId cell, const Dress& dress) {
        const auto want = [mask](std::uint8_t bit) { return (mask & bit) != 0U; };
        if (want(kFaceTopBit)) {
            const V3 c[4] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}};
            quad(c, cell, 0.0F, FaceDir::Top, render::FaceKind::Side, dress);
        }
        if (want(kFaceBottomBit)) {
            const V3 c[4] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}};
            quad(c, cell, 0.0F, FaceDir::Bottom, render::FaceKind::Side, dress);
        }
        if (want(kFaceSouthBit)) {
            const V3 c[4] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
            quad(c, cell, 0.0F, FaceDir::South, render::FaceKind::Side, dress);
        }
        if (want(kFaceNorthBit)) {
            const V3 c[4] = {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}};
            quad(c, cell, 0.0F, FaceDir::North, render::FaceKind::Side, dress);
        }
        if (want(kFaceEastBit)) {
            const V3 c[4] = {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}};
            quad(c, cell, 0.0F, FaceDir::East, render::FaceKind::Side, dress);
        }
        if (want(kFaceWestBit)) {
            const V3 c[4] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}};
            quad(c, cell, 0.0F, FaceDir::West, render::FaceKind::Side, dress);
        }
    }

private:
    /// A vertex: its position projected on its own plane for the UV, so the
    /// surface runs on across every face of a wall, a floor or a roof plane
    /// with no seam where two cells meet. `t` runs along the face
    /// horizontally, `b` up it.
    void vertex(const V3& p, const V3& n, CellId cell, float wet, const Dress& dress,
                float shore = -1.0F) {
        V3 t{1.0F, 0.0F, 0.0F};
        if (std::abs(n.y) < 0.999F) {
            t = normalised(cross(V3{0.0F, 1.0F, 0.0F}, n));
        }
        const V3 b = cross(n, t);
        constexpr float kInv = 1.0F / static_cast<float>(kSurfaceMetres);
        out_.positions.push_back(p.x);
        out_.positions.push_back(p.y);
        out_.positions.push_back(p.z);
        out_.normals.push_back(n.x);
        out_.normals.push_back(n.y);
        out_.normals.push_back(n.z);
        out_.texcoords.push_back(dot(p, t) * kInv);
        out_.texcoords.push_back(-dot(p, b) * kInv);
        const std::array<float, 2> origin = atlas_.origin(dress.surface);
        out_.texcoords2.push_back(origin[0]);
        out_.texcoords2.push_back(origin[1]);
        out_.cells.push_back(cell.x);
        out_.cells.push_back(cell.y);
        out_.cells.push_back(cell.z);
        out_.facing.push_back(facingOf(n));
        out_.wetAlpha.push_back(wet);
        out_.tones.push_back(dress.r);
        out_.tones.push_back(dress.g);
        out_.tones.push_back(dress.b);
        out_.shore.push_back(shore);
        out_.skyOpen.push_back(dress.indoor ? 0.0F : 1.0F);
    }

    SurfaceArrays& out_;
    const SurfaceAtlas& atlas_;
    std::vector<ChunkFace>* faces_;
    const SurfaceArrays* sibling_;
};

// ---------------------------------------------------------------------------
// what a face wears
// ---------------------------------------------------------------------------

/// The dress of a face of `material` in `role`. Near: the surface itself, or
/// the material's tone when it has none. Far: always flat, in the mean colour
/// of what the near face would wear.
[[nodiscard]] Dress dressFor(const SurfaceAtlas& atlas, bool far, std::uint16_t material,
                             Role role) noexcept {
    const Surface surface = surfaceFor(material, role);
    Dress d;
    d.indoor = role == Role::Indoor || role == Role::Inside;
    if (surface == Surface::White) {
        const SurfaceTone t = materialToneOf(material);
        d.r = t.r;
        d.g = t.g;
        d.b = t.b;
        return d;
    }
    if (far) {
        const SurfaceTone t = atlas.tone(surface);
        d.r = t.r;
        d.g = t.g;
        d.b = t.b;
        return d;
    }
    d.surface = surface;
    return d;
}

/// The dress of a specific surface, `shade` darkening or lifting it.
[[nodiscard]] Dress dressOf(const SurfaceAtlas& atlas, bool far, Surface surface,
                            float shade = 1.0F) noexcept {
    Dress d;
    if (far) {
        const SurfaceTone t = atlas.tone(surface);
        d.r = t.r * shade;
        d.g = t.g * shade;
        d.b = t.b * shade;
        return d;
    }
    d.surface = surface;
    d.r = d.g = d.b = shade;
    return d;
}

/// Whether a material is something a person builds a wall from: it gets a
/// plinth and a cornice. Earth, ash, cloth and straw do not.
[[nodiscard]] bool isMasonryMaterial(std::uint16_t material) noexcept {
    const Surface s = surfaceFor(material, Role::Wall);
    return s == Surface::GraniteAshlar || s == Surface::BrickWall || s == Surface::BrickFacade ||
           s == Surface::Concrete || s == Surface::RemanFacade;
}
[[nodiscard]] bool isTimberMaterial(std::uint16_t material) noexcept {
    const Surface s = surfaceFor(material, Role::Wall);
    return s == Surface::PlankWall || s == Surface::WetWood;
}

constexpr int kDx[4] = {0, 1, 0, -1};
constexpr int kDy[4] = {-1, 0, 1, 0};
constexpr std::uint8_t kSideBitOf[4] = {kFaceNorthBit, kFaceEastBit, kFaceSouthBit, kFaceWestBit};

[[nodiscard]] bool walkableForm(content::TileForm f) noexcept {
    return f == content::TileForm::Floor || f == content::TileForm::Ramp ||
           f == content::TileForm::Stair;
}

// ---------------------------------------------------------------------------
// the builder
// ---------------------------------------------------------------------------

constexpr float kRampRise = 1.2F;
constexpr int kStairSteps = 6;

class Builder {
public:
    Builder(const sim::TileQuery& tiles, const render::TileAtlas& tileAtlas,
            const ChunkMaterials& materials, ChunkKey key, Detail detail, ChunkGeometry& out)
        : tiles_(tiles),
          waterAlpha_(tileAtlas.waterDepthAlphaQ8()),
          atlas_(materials.surfaces()),
          key_(key),
          far_(detail == Detail::Far),
          out_(out),
          cache_(tiles, key),
          body_(out, materials.surfaces(), &out.faces, nullptr),
          extra_(out.extra, materials.surfaces(), nullptr, &out) {}

    void run() {
        const std::int32_t xStart = key_.cx * kChunkTiles;
        const std::int32_t yStart = key_.cy * kChunkTiles;
        for (std::int32_t z = 0; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = yStart; y < yStart + kChunkTiles; ++y) {
                for (std::int32_t x = xStart; x < xStart + kChunkTiles; ++x) {
                    const render::Voxel* vp = cache_.at(x, y, z);
                    if (vp == nullptr) {
                        continue;
                    }
                    const std::uint8_t mask = faceMaskOf(cache_, x, y, z, *vp);
                    if (mask == 0U) {
                        continue;
                    }
                    if (!body(x, y, z, *vp, mask)) {
                        return;
                    }
                }
            }
        }
        if (!far_) {
            for (std::int32_t z = 0; z < tiles_.sizeZ(); ++z) {
                for (std::int32_t y = yStart; y < yStart + kChunkTiles; ++y) {
                    for (std::int32_t x = xStart; x < xStart + kChunkTiles; ++x) {
                        const render::Voxel* vp = cache_.at(x, y, z);
                        if (vp == nullptr || vp->water) {
                            continue;
                        }
                        trimAndShapes(x, y, z, *vp, faceMaskOf(cache_, x, y, z, *vp));
                    }
                }
            }
        }
        roofs();
    }

private:
    /// How far the point (px, pz) of a water surface in band `z` is from the
    /// nearest bank, in tiles, held at three: a cell is a bank when something
    /// that is not open air stands in it at the water's band or the quay's one
    /// above. Beyond the district's edge is more water.
    [[nodiscard]] float shoreDistance(float px, float pz, std::int32_t z) const noexcept {
        const std::int32_t cx = static_cast<std::int32_t>(std::floor(px));
        const std::int32_t cz = static_cast<std::int32_t>(std::floor(pz));
        float best = 3.0F;
        for (std::int32_t gy = cz - 3; gy <= cz + 2; ++gy) {
            for (std::int32_t gx = cx - 3; gx <= cx + 2; ++gx) {
                if (!tiles_.inBounds(gx, gy, z)) {
                    continue;
                }
                const auto solid = [&](std::int32_t level) {
                    if (!tiles_.inBounds(gx, gy, level)) {
                        return false;
                    }
                    const content::TileForm f = tiles_.form(gx, gy, level);
                    return f != content::TileForm::Open && f != content::TileForm::Void;
                };
                if (!solid(z) && !solid(z + 1)) {
                    continue;
                }
                const float dx = std::max({static_cast<float>(gx) - px, px - static_cast<float>(gx + 1), 0.0F});
                const float dz = std::max({static_cast<float>(gy) - pz, pz - static_cast<float>(gy + 1), 0.0F});
                best = std::min(best, std::sqrt(dx * dx + dz * dz));
            }
        }
        return best;
    }

    // ---- the body: the tile boxes, as they always were ---------------------

    [[nodiscard]] bool body(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v,
                            std::uint8_t mask) {
        const CellId cell{x, y, z};
        const float x0 = static_cast<float>(x);
        const float x1 = x0 + 1.0F;
        const float z0 = static_cast<float>(y);
        const float z1 = z0 + 1.0F;
        const float b = v.bottom;
        const float t = v.top;

        float wet = 0.0F;
        if (v.wetness > 0 && !waterAlpha_.empty()) {
            const std::size_t slot = static_cast<std::size_t>(
                std::clamp(v.wetness, 0, static_cast<int>(waterAlpha_.size()) - 1));
            wet = static_cast<float>(waterAlpha_[slot]) / 256.0F;
        }
        const auto emit = [&](const V3 (&c)[4], float wetness, FaceDir dir, render::FaceKind kind,
                              const Dress& dress) -> bool {
            if (body_.full()) {
                out_.truncated = true;
                return false;
            }
            body_.quad(c, cell, wetness, dir, kind, dress);
            return true;
        };

        if ((mask & kFaceTopBit) != 0U) {
            Dress dress;
            if (v.topFace == render::FaceKind::Water) {
                dress.r = kWaterTone.r;
                dress.g = kWaterTone.g;
                dress.b = kWaterTone.b;
            } else {
                Role role = Role::Floor;
                if (v.topFace == render::FaceKind::BlockTop) {
                    role = Role::Cap;
                } else if (cellRoofed(tiles_, x, y, z)) {
                    role = Role::Indoor;
                }
                dress = dressFor(atlas_, far_, v.material, role);
            }
            const V3 c[4] = {{x0, t, z1}, {x1, t, z1}, {x1, t, z0}, {x0, t, z0}};
            if (v.topFace == render::FaceKind::Water) {
                // The harbour's own faces carry how far each corner is from
                // a bank, for the water shader's foam and shallows.
                float shore[4];
                for (int k = 0; k < 4; ++k) {
                    shore[k] = shoreDistance(c[k].x, c[k].z, z);
                }
                if (body_.full()) {
                    out_.truncated = true;
                    return false;
                }
                body_.quad(c, cell, wet, FaceDir::Top, v.topFace, dress, shore);
            } else if (!emit(c, wet, FaceDir::Top, v.topFace, dress)) {
                return false;
            }
        }
        if (!v.hasSides) {
            return true;
        }
        if ((mask & kFaceBottomBit) != 0U) {
            const V3 c[4] = {{x0, b, z0}, {x1, b, z0}, {x1, b, z1}, {x0, b, z1}};
            if (!emit(c, 0.0F, FaceDir::Bottom, render::FaceKind::Side,
                      dressFor(atlas_, far_, v.material, Role::Inside))) {
                return false;
            }
        }
        const auto side = [&](std::uint8_t bit, int dir, FaceDir faceDir, const V3 (&c)[4]) -> bool {
            if ((mask & bit) == 0U) {
                return true;
            }
            const bool inside = cellRoofed(tiles_, x + kDx[dir], y + kDy[dir], z);
            return emit(c, 0.0F, faceDir, render::FaceKind::Side,
                        dressFor(atlas_, far_, v.material, inside ? Role::Inside : Role::Wall));
        };
        {
            const V3 c[4] = {{x0, b, z1}, {x1, b, z1}, {x1, t, z1}, {x0, t, z1}};
            if (!side(kFaceSouthBit, 2, FaceDir::South, c)) {
                return false;
            }
        }
        {
            const V3 c[4] = {{x1, b, z0}, {x0, b, z0}, {x0, t, z0}, {x1, t, z0}};
            if (!side(kFaceNorthBit, 0, FaceDir::North, c)) {
                return false;
            }
        }
        {
            const V3 c[4] = {{x1, b, z1}, {x1, b, z0}, {x1, t, z0}, {x1, t, z1}};
            if (!side(kFaceEastBit, 1, FaceDir::East, c)) {
                return false;
            }
        }
        {
            const V3 c[4] = {{x0, b, z0}, {x0, b, z1}, {x0, t, z1}, {x0, t, z0}};
            if (!side(kFaceWestBit, 3, FaceDir::West, c)) {
                return false;
            }
        }
        return true;
    }

    // ---- the detail: trim and shapes ---------------------------------------

    [[nodiscard]] bool extraFull() {
        if (extra_.full(24)) {
            out_.truncated = true;
            return true;
        }
        return false;
    }

    void trimAndShapes(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v,
                       std::uint8_t mask) {
        if (extraFull()) {
            return;
        }
        const content::TileForm form = tiles_.form(x, y, z);
        switch (form) {
            case content::TileForm::Wall:
                if (!v.water) {
                    trimWall(x, y, z, v, mask);
                    doorPost(x, y, z);
                }
                break;
            case content::TileForm::Ramp:
                wedge(x, y, z, v);
                break;
            case content::TileForm::Stair:
                steps(x, y, z, v);
                break;
            case content::TileForm::Floor:
                floorDetail(x, y, z, v, mask);
                break;
            default:
                break;
        }
    }

    /// A plinth at the foot of a wall that stands on a street, a cornice or a
    /// string course at its head: the two lines that make a facade read as
    /// built.
    void trimWall(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v,
                  std::uint8_t mask) {
        const bool masonry = isMasonryMaterial(v.material);
        const bool timber = !masonry && isTimberMaterial(v.material);
        if (!masonry && !timber) {
            return;
        }
        const CellId cell{x, y, z};
        const float foot = render::bandSurface(z);
        const float head = v.top;
        const bool storeyAbove = [&] {
            const render::Voxel* above = cache_.at(x, y, z + 1);
            return above != nullptr && above->hasSides && above->bottom <= head + kEps;
        }();
        const Dress plinth = timber ? dressOf(atlas_, false, Surface::Beam, 0.9F)
                                    : dressOf(atlas_, false, Surface::GraniteAshlar, 0.78F);
        const Dress cornice = timber ? dressOf(atlas_, false, Surface::Beam, 1.0F)
                                     : dressOf(atlas_, false, Surface::GraniteAshlar, 1.08F);
        for (int d = 0; d < 4; ++d) {
            if ((mask & kSideBitOf[d]) == 0U) {
                continue;
            }
            const std::int32_t nx = x + kDx[d];
            const std::int32_t ny = y + kDy[d];
            if (cellRoofed(tiles_, nx, ny, z)) {
                continue;  // an indoor face: no trim on the plaster
            }
            // The wall continues along this face on either side? Then the
            // trim runs on and shows no end.
            const bool alongX = d == 0 || d == 2;
            const bool endA = !wallContinues(x - (alongX ? 1 : 0), y - (alongX ? 0 : 1), z);
            const bool endB = !wallContinues(x + (alongX ? 1 : 0), y + (alongX ? 0 : 1), z);
            const render::Voxel* foot_ = cache_.at(nx, ny, z);
            if (foot_ != nullptr && foot_->hasSides && std::abs(foot_->top - v.bottom) < 0.02F &&
                foot > 0.0F) {
                trimBox(x, y, d, foot, foot + 0.5F, 0.08F, endA, endB, cell, plinth);
            }
            const float depth = storeyAbove ? 0.07F : 0.16F;
            const float height = storeyAbove ? 0.16F : 0.28F;
            trimBox(x, y, d, head - height, head, depth, endA, endB, cell, cornice);
        }
    }

    [[nodiscard]] bool wallContinues(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return tiles_.form(x, y, z) == content::TileForm::Wall;
    }

    /// A slab proud of face `d` of cell (x, y) between heights y0 and y1:
    /// its front, its top, its underside, and an end where the run stops.
    void trimBox(std::int32_t x, std::int32_t y, int d, float y0, float y1, float proud, bool endA,
                 bool endB, CellId cell, const Dress& dress) {
        float x0 = static_cast<float>(x);
        float x1 = x0 + 1.0F;
        float z0 = static_cast<float>(y);
        float z1 = z0 + 1.0F;
        std::uint8_t mask = kFaceTopBit | kFaceBottomBit;
        switch (d) {
            case 0:  // north face: the slab stands out toward -Z
                z1 = z0;
                z0 -= proud;
                mask = static_cast<std::uint8_t>(mask | kFaceNorthBit);
                break;
            case 2:  // south
                z0 = z1;
                z1 += proud;
                mask = static_cast<std::uint8_t>(mask | kFaceSouthBit);
                break;
            case 1:  // east
                x0 = x1;
                x1 += proud;
                mask = static_cast<std::uint8_t>(mask | kFaceEastBit);
                break;
            default:  // west
                x1 = x0;
                x0 -= proud;
                mask = static_cast<std::uint8_t>(mask | kFaceWestBit);
                break;
        }
        const bool alongX = d == 0 || d == 2;
        // The ends: the low side and the high side of the run along the face.
        if (alongX) {
            if (endA) {
                mask = static_cast<std::uint8_t>(mask | kFaceWestBit);
            }
            if (endB) {
                mask = static_cast<std::uint8_t>(mask | kFaceEastBit);
            }
        } else {
            if (endA) {
                mask = static_cast<std::uint8_t>(mask | kFaceNorthBit);
            }
            if (endB) {
                mask = static_cast<std::uint8_t>(mask | kFaceSouthBit);
            }
        }
        extra_.box(x0, x1, y0, y1, z0, z1, mask, cell, dress);
    }

    /// The timber post of a door: where the door rule says the jamb is, the
    /// mesher has cut the box away from the ground to the head, and a slim
    /// post stands in its place on the side of the opening.
    void doorPost(std::int32_t x, std::int32_t y, std::int32_t z) {
        if (!doorPostAt(tiles_, x, y, z)) {
            return;
        }
        for (int d = 0; d < 4; ++d) {
            if (!walkableForm(tiles_.form(x + kDx[d], y + kDy[d], z))) {
                continue;
            }
            constexpr float kPost = 0.3F;
            const float foot = render::bandSurface(z);
            const float fx = static_cast<float>(x);
            const float fz = static_cast<float>(y);
            // Thick across the wall, slim along the opening, on the opening's side.
            float x0 = fx + 0.1F;
            float x1 = fx + 0.9F;
            float z0 = fz + 0.1F;
            float z1 = fz + 0.9F;
            if (d == 1) {
                x0 = fx + 1.0F - kPost;
                x1 = fx + 1.0F;
            } else if (d == 3) {
                x0 = fx;
                x1 = fx + kPost;
            } else if (d == 2) {
                z0 = fz + 1.0F - kPost;
                z1 = fz + 1.0F;
            } else {
                z0 = fz;
                z1 = fz + kPost;
            }
            extra_.box(x0, x1, foot, foot + kDoorHeadHeight, z0, z1,
                       static_cast<std::uint8_t>(kFaceTopBit | kFaceSideBits), CellId{x, y, z},
                       dressOf(atlas_, false, Surface::Beam, 1.0F));
            return;
        }
    }

    /// A ramp as a wedge: the slab's flat top stays (it is the sim's walking
    /// surface), and a sloped surface rises from it toward the terrace the
    /// ramp climbs to.
    void wedge(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v) {
        const int d = climbDir(x, y, z, true);
        if (d < 0) {
            return;
        }
        const CellId cell{x, y, z};
        const float deck = render::bandSurface(z);
        const float x0 = static_cast<float>(x);
        const float x1 = x0 + 1.0F;
        const float z0 = static_cast<float>(y);
        const float z1 = z0 + 1.0F;
        const float up = deck + kRampRise;
        const Dress dress = dressFor(atlas_, false, v.material, Role::Floor);
        // The sloped top: low edge at the deck, high edge at the rise.
        V3 top[4];
        switch (d) {
            case 0: top[0] = {x0, up, z0}; top[1] = {x1, up, z0}; top[2] = {x1, deck, z1}; top[3] = {x0, deck, z1}; break;
            case 2: top[0] = {x0, deck, z0}; top[1] = {x1, deck, z0}; top[2] = {x1, up, z1}; top[3] = {x0, up, z1}; break;
            case 1: top[0] = {x0, deck, z0}; top[1] = {x1, up, z0}; top[2] = {x1, up, z1}; top[3] = {x0, deck, z1}; break;
            default: top[0] = {x0, up, z0}; top[1] = {x1, deck, z0}; top[2] = {x1, deck, z1}; top[3] = {x0, up, z1}; break;
        }
        extra_.quadOut(top, V3{0.0F, 1.0F, 0.0F}, cell, dress);
        // The two flanks, unless the next ramp of the row carries them on.
        const bool alongX = d == 0 || d == 2;
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            const int nx = x + (alongX ? (sideIdx == 0 ? -1 : 1) : 0);
            const int ny = y + (alongX ? 0 : (sideIdx == 0 ? -1 : 1));
            if (tiles_.form(nx, ny, z) == content::TileForm::Ramp) {
                continue;
            }
            V3 flank[3];
            V3 outward{0.0F, 0.0F, 0.0F};
            if (alongX) {
                const float fx = sideIdx == 0 ? x0 : x1;
                outward.x = sideIdx == 0 ? -1.0F : 1.0F;
                const float lowZ = d == 0 ? z1 : z0;
                const float highZ = d == 0 ? z0 : z1;
                flank[0] = {fx, deck, lowZ};
                flank[1] = {fx, deck, highZ};
                flank[2] = {fx, up, highZ};
            } else {
                const float fz = sideIdx == 0 ? z0 : z1;
                outward.z = sideIdx == 0 ? -1.0F : 1.0F;
                const float lowX = d == 1 ? x0 : x1;
                const float highX = d == 1 ? x1 : x0;
                flank[0] = {lowX, deck, fz};
                flank[1] = {highX, deck, fz};
                flank[2] = {highX, up, fz};
            }
            extra_.tri(flank, outward, cell, dress);
        }
    }

    /// A stair as steps: boxes climbing toward the landing.
    void steps(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v) {
        const int d = climbDir(x, y, z, false);
        if (d < 0) {
            return;
        }
        const CellId cell{x, y, z};
        const float deck = render::bandSurface(z);
        const Dress dress = dressFor(atlas_, false, v.material, Role::Floor);
        const float fx = static_cast<float>(x);
        const float fz = static_cast<float>(y);
        for (int i = 0; i < kStairSteps; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(kStairSteps);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(kStairSteps);
            const float top = deck + kRampRise * t1;
            float x0 = fx;
            float x1 = fx + 1.0F;
            float z0 = fz;
            float z1 = fz + 1.0F;
            std::uint8_t mask = kFaceTopBit | kFaceSideBits;
            // The run is measured from the low edge toward the landing.
            switch (d) {
                case 0: z1 = fz + 1.0F - t0; z0 = fz + 1.0F - t1; mask = static_cast<std::uint8_t>(mask & ~kFaceNorthBit); break;
                case 2: z0 = fz + t0; z1 = fz + t1; mask = static_cast<std::uint8_t>(mask & ~kFaceSouthBit); break;
                case 1: x0 = fx + t0; x1 = fx + t1; mask = static_cast<std::uint8_t>(mask & ~kFaceEastBit); break;
                default: x1 = fx + 1.0F - t0; x0 = fx + 1.0F - t1; mask = static_cast<std::uint8_t>(mask & ~kFaceWestBit); break;
            }
            extra_.box(x0, x1, deck, top, z0, z1, mask, cell, dress);
        }
    }

    /// The side a ramp or a stair climbs toward: a neighbour with a floor one
    /// level up. A ramp wants that neighbour's own level to be a wall (the
    /// terrace it rises against); a stair wants it to be open ground.
    [[nodiscard]] int climbDir(std::int32_t x, std::int32_t y, std::int32_t z, bool ramp) const noexcept {
        for (int d = 0; d < 4; ++d) {
            const std::int32_t nx = x + kDx[d];
            const std::int32_t ny = y + kDy[d];
            if (!walkableForm(tiles_.form(nx, ny, z + 1))) {
                continue;
            }
            const bool wall = tiles_.form(nx, ny, z) == content::TileForm::Wall;
            if (ramp == wall) {
                return d;
            }
        }
        return -1;
    }

    /// Ceiling beams under a slab, quay coping, bollards and pilings.
    void floorDetail(std::int32_t x, std::int32_t y, std::int32_t z, const render::Voxel& v,
                     std::uint8_t mask) {
        const CellId cell{x, y, z};
        const float fx = static_cast<float>(x);
        const float fz = static_cast<float>(y);
        // A beam every second line along the underside of a slab with a room
        // (or the street) under it.
        if ((mask & kFaceBottomBit) != 0U && (x & 1) == 0) {
            extra_.box(fx + 0.38F, fx + 0.62F, v.bottom - 0.26F, v.bottom, fz, fz + 1.0F,
                       static_cast<std::uint8_t>(kFaceBottomBit | kFaceEastBit | kFaceWestBit), cell,
                       dressOf(atlas_, false, Surface::Beam, 1.0F));
        }
        // The water's edge: a quay cell with the harbour in the next cell down.
        int waterSide = -1;
        for (int d = 0; d < 4; ++d) {
            const std::int32_t nx = x + kDx[d];
            const std::int32_t ny = y + kDy[d];
            if (tiles_.form(nx, ny, z) == content::TileForm::Open &&
                (tiles_.fluidDepth(nx, ny, z - 1) > 0 || tiles_.fluidDepth(nx, ny, z) > 0)) {
                waterSide = d;
                break;
            }
        }
        if (waterSide < 0) {
            return;
        }
        const float deck = v.top;
        const std::uint32_t h = render::variantMix(static_cast<std::uint32_t>(x) * 92821U ^
                                           static_cast<std::uint32_t>(y) * 68917U);
        const bool timber = isTimberMaterial(v.material);
        // Coping: a lip along the edge, a hand wide, a few centimetres proud.
        {
            float x0 = fx;
            float x1 = fx + 1.0F;
            float z0 = fz;
            float z1 = fz + 1.0F;
            constexpr float kLip = 0.14F;
            switch (waterSide) {
                case 0: z1 = fz + kLip; break;
                case 2: z0 = fz + 1.0F - kLip; break;
                case 1: x0 = fx + 1.0F - kLip; break;
                default: x1 = fx + kLip; break;
            }
            const std::uint8_t lipMask = static_cast<std::uint8_t>(
                kFaceTopBit | kSideBitOf[waterSide] |
                (waterSide == 0 || waterSide == 2 ? (kFaceEastBit | kFaceWestBit)
                                               : (kFaceNorthBit | kFaceSouthBit)));
            extra_.box(x0, x1, deck, deck + 0.06F, z0, z1, lipMask, cell,
                       timber ? dressOf(atlas_, false, Surface::Beam, 0.95F)
                              : dressOf(atlas_, false, Surface::GraniteAshlar, 1.12F));
        }
        // A bollard on one edge cell in four.
        if ((h & 3U) == 0U) {
            const float cx = fx + 0.5F + (waterSide == 1 ? 0.3F : waterSide == 3 ? -0.3F : 0.0F);
            const float cz = fz + 0.5F + (waterSide == 2 ? 0.3F : waterSide == 0 ? -0.3F : 0.0F);
            extra_.box(cx - 0.11F, cx + 0.11F, deck, deck + 0.5F, cz - 0.11F, cz + 0.11F,
                       static_cast<std::uint8_t>(kFaceSideBits), cell,
                       dressOf(atlas_, false, Surface::Iron, 0.8F));
            extra_.box(cx - 0.16F, cx + 0.16F, deck + 0.5F, deck + 0.62F, cz - 0.16F, cz + 0.16F,
                       static_cast<std::uint8_t>(kFaceTopBit | kFaceSideBits), cell,
                       dressOf(atlas_, false, Surface::Iron, 0.95F));
        }
        // A piling under a timber deck where the harbour is beneath it.
        if (timber && (x & 1) == 0 && (y & 1) == 0 && tiles_.fluidDepth(x, y, z - 1) > 0) {
            render::Voxel water;
            float bottom = deck - 2.0F;
            if (render::classifyVoxel(tiles_, x, y, z - 1, water) && water.water) {
                bottom = water.top - 0.25F;
            }
            const float top = v.bottom;
            if (top > bottom) {
                extra_.box(fx + 0.32F, fx + 0.68F, bottom, top, fz + 0.32F, fz + 0.68F,
                           static_cast<std::uint8_t>(kFaceSideBits), cell,
                           dressOf(atlas_, false, Surface::WetWood, 0.8F));
            }
        }
    }

    // ---- roofs ---------------------------------------------------------------

    struct Deck {
        std::int32_t z = 0;
        std::vector<std::pair<std::int32_t, std::int32_t>> cells;
    };

    [[nodiscard]] bool isDeck(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (tiles_.form(x, y, z) != content::TileForm::Floor || tiles_.fluidDepth(x, y, z) > 0) {
            return false;
        }
        if (z + 1 < tiles_.sizeZ() && tiles_.form(x, y, z + 1) != content::TileForm::Open) {
            return false;
        }
        return true;
    }

    void roofs() {
        if (tiles_.sizeZ() > kRoofLevels) {
            return;
        }
        const std::int32_t xStart = key_.cx * kChunkTiles;
        const std::int32_t yStart = key_.cy * kChunkTiles;
        std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> seen;
        for (std::int32_t z = 1; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = yStart; y < yStart + kChunkTiles; ++y) {
                for (std::int32_t x = xStart; x < xStart + kChunkTiles; ++x) {
                    if (!isDeck(x, y, z) || seen.count({x, y, z}) != 0U) {
                        continue;
                    }
                    Deck deck;
                    if (!flood(x, y, z, deck, seen)) {
                        continue;
                    }
                    // The roof belongs to the chunk of its first cell, so a
                    // building across a chunk seam is meshed once.
                    const auto first = *std::min_element(
                        deck.cells.begin(), deck.cells.end(),
                        [](const auto& a, const auto& b) {
                            return a.second != b.second ? a.second < b.second : a.first < b.first;
                        });
                    if (chunkOf(first.first, first.second).cx != key_.cx ||
                        chunkOf(first.first, first.second).cy != key_.cy) {
                        continue;
                    }
                    roof(deck);
                }
            }
        }
    }

    /// Flood the deck's component and decide whether it is a roof: a floor
    /// with sky over it, held up by walls at its edge, over at least one room.
    /// A pier deck stands over water and a street over earth; neither has
    /// both. Cells seen while flooding are marked so each is met once per chunk.
    [[nodiscard]] bool flood(std::int32_t sx, std::int32_t sy, std::int32_t z, Deck& deck,
                             std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>>& seen) const {
        constexpr std::size_t kMaxCells = 3000;
        deck.z = z;
        std::vector<std::pair<std::int32_t, std::int32_t>> stack{{sx, sy}};
        seen.insert({sx, sy, z});
        bool tooBig = false;
        while (!stack.empty()) {
            const auto [x, y] = stack.back();
            stack.pop_back();
            deck.cells.emplace_back(x, y);
            if (deck.cells.size() > kMaxCells) {
                tooBig = true;
                break;
            }
            for (int d = 0; d < 4; ++d) {
                const std::int32_t nx = x + kDx[d];
                const std::int32_t ny = y + kDy[d];
                if (isDeck(nx, ny, z) && seen.insert({nx, ny, z}).second) {
                    stack.emplace_back(nx, ny);
                }
            }
        }
        if (tooBig || deck.cells.size() < 4) {
            return false;
        }
        std::set<std::pair<std::int32_t, std::int32_t>> in(deck.cells.begin(), deck.cells.end());
        std::size_t rooms = 0;
        std::size_t edges = 0;
        std::size_t walled = 0;
        for (const auto& [x, y] : deck.cells) {
            const content::TileForm below = tiles_.form(x, y, z - 1);
            if (walkableForm(below) && tiles_.fluidDepth(x, y, z - 1) == 0) {
                ++rooms;
            }
            for (int d = 0; d < 4; ++d) {
                if (in.count({x + kDx[d], y + kDy[d]}) == 0U) {
                    ++edges;
                    if (below == content::TileForm::Wall) {
                        ++walled;
                    }
                }
            }
        }
        return rooms >= 1 && walled * 10 >= edges * 7;
    }

    void roof(const Deck& deck) {
        const std::int32_t z = deck.z;
        RoofLayer* layer = nullptr;
        for (RoofLayer& existing : out_.roofs) {
            if (existing.level == z) {
                layer = &existing;
            }
        }
        if (layer == nullptr) {
            out_.roofs.emplace_back();
            layer = &out_.roofs.back();
            layer->level = z;
            layer->deckY = render::bandSurface(z);
        }
        Emitter emit(*layer, atlas_, nullptr, nullptr);
        if (emit.full(1024)) {
            out_.truncated = true;
            return;
        }
        std::int32_t minX = INT32_MAX;
        std::int32_t minY = INT32_MAX;
        std::int32_t maxX = INT32_MIN;
        std::int32_t maxY = INT32_MIN;
        for (const auto& [x, y] : deck.cells) {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        const int w = maxX - minX + 1;
        const int h = maxY - minY + 1;
        // Vertex grid (w + 1) x (h + 1); a cell is in or out.
        std::vector<std::uint8_t> inside(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        std::map<std::uint16_t, int> materialVotes;
        for (const auto& [x, y] : deck.cells) {
            inside[static_cast<std::size_t>(y - minY) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(x - minX)] = 1;
            ++materialVotes[tiles_.material(x, y, z)];
        }
        const auto cellIn = [&](int cx, int cy) {
            return cx >= 0 && cy >= 0 && cx < w && cy < h &&
                   inside[static_cast<std::size_t>(cy) * static_cast<std::size_t>(w) +
                          static_cast<std::size_t>(cx)] != 0U;
        };
        // The covering follows the deck's own material.
        std::uint16_t majority = 0;
        int best = -1;
        for (const auto& [material, votes] : materialVotes) {
            if (votes > best) {
                best = votes;
                majority = material;
            }
        }
        Surface covering = Surface::SlateRoof;
        {
            const auto ids = render::materialIds();
            const std::string_view name = majority < ids.size() ? ids[majority] : std::string_view{};
            if (name == "thatch" || name == "oak" || name == "trudgeon_wood" ||
                name == "trudgeon_wood@getilia_soak") {
                covering = Surface::Thatch;
            } else if (name == "brick" || name == "brick_facade" || name == "dirt") {
                covering = Surface::ClayRoof;
            }
        }
        // Distance of every vertex to the roof's edge, in the max norm: the
        // hipped roof's own height field.
        const int vw = w + 1;
        const int vh = h + 1;
        std::vector<int> dist(static_cast<std::size_t>(vw) * static_cast<std::size_t>(vh), -1);
        std::vector<std::pair<int, int>> frontier;
        const auto vi = [&](int vx, int vy) {
            return static_cast<std::size_t>(vy) * static_cast<std::size_t>(vw) + static_cast<std::size_t>(vx);
        };
        for (int vy = 0; vy < vh; ++vy) {
            for (int vx = 0; vx < vw; ++vx) {
                const bool allIn = cellIn(vx - 1, vy - 1) && cellIn(vx, vy - 1) &&
                                   cellIn(vx - 1, vy) && cellIn(vx, vy);
                if (!allIn) {
                    dist[vi(vx, vy)] = 0;
                    frontier.emplace_back(vx, vy);
                }
            }
        }
        int maxD = 0;
        for (std::size_t head = 0; head < frontier.size(); ++head) {
            const auto [vx, vy] = frontier[head];
            const int d0 = dist[vi(vx, vy)];
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = vx + dx;
                    const int ny = vy + dy;
                    if (nx < 0 || ny < 0 || nx >= vw || ny >= vh || dist[vi(nx, ny)] >= 0) {
                        continue;
                    }
                    dist[vi(nx, ny)] = d0 + 1;
                    maxD = std::max(maxD, d0 + 1);
                    frontier.emplace_back(nx, ny);
                }
            }
        }
        if (maxD < 1) {
            return;
        }
        const float deckY = render::bandSurface(z);
        const float pitch = std::clamp(2.4F / static_cast<float>(maxD), 0.3F, 0.7F);
        constexpr float kOverhang = 0.34F;
        const float eaveDrop = pitch * kOverhang;
        // Where an edge vertex is pushed out to: along the open sides only (a
        // roof ends flush against a taller wall).
        const auto rim = [&](int vx, int vy) -> V3 {
            float sx = 0.0F;
            float sy = 0.0F;
            for (int dy = -1; dy <= 0; ++dy) {
                for (int dx = -1; dx <= 0; ++dx) {
                    const int cx = vx + dx;
                    const int cy = vy + dy;
                    if (cellIn(cx, cy)) {
                        continue;
                    }
                    const std::int32_t wx = minX + cx;
                    const std::int32_t wy = minY + cy;
                    if (tiles_.form(wx, wy, z) != content::TileForm::Open) {
                        continue;
                    }
                    sx += dx == 0 ? 1.0F : -1.0F;
                    sy += dy == 0 ? 1.0F : -1.0F;
                }
            }
            const float ox = sx > 0.0F ? 1.0F : sx < 0.0F ? -1.0F : 0.0F;
            const float oy = sy > 0.0F ? 1.0F : sy < 0.0F ? -1.0F : 0.0F;
            return V3{static_cast<float>(minX + vx) + ox * kOverhang, deckY - eaveDrop,
                      static_cast<float>(minY + vy) + oy * kOverhang};
        };
        const auto vertexAt = [&](int vx, int vy) -> V3 {
            const int d = dist[vi(vx, vy)];
            if (d == 0) {
                return rim(vx, vy);
            }
            return V3{static_cast<float>(minX + vx), deckY + pitch * static_cast<float>(d),
                      static_cast<float>(minY + vy)};
        };
        const Dress shingles = dressOf(atlas_, far_, covering, 1.0F);
        const Dress soffit = dressOf(atlas_, far_, Surface::Beam, 0.9F);
        for (const auto& [x, y] : deck.cells) {
            const int cx = x - minX;
            const int cy = y - minY;
            const CellId cell{x, y, z};
            const V3 p00 = vertexAt(cx, cy);
            const V3 p10 = vertexAt(cx + 1, cy);
            const V3 p01 = vertexAt(cx, cy + 1);
            const V3 p11 = vertexAt(cx + 1, cy + 1);
            // The diagonal that follows the crease: the one that joins the
            // vertices furthest apart in height.
            const bool diagA = std::abs(p00.y - p11.y) >= std::abs(p10.y - p01.y);
            V3 t0[3];
            V3 t1[3];
            if (diagA) {
                t0[0] = p00; t0[1] = p10; t0[2] = p11;
                t1[0] = p00; t1[1] = p11; t1[2] = p01;
            } else {
                t0[0] = p00; t0[1] = p10; t0[2] = p01;
                t1[0] = p10; t1[1] = p11; t1[2] = p01;
            }
            if (emit.full(8)) {
                out_.truncated = true;
                return;
            }
            emit.tri(t0, V3{0.0F, 1.0F, 0.0F}, cell, shingles);
            emit.tri(t1, V3{0.0F, 1.0F, 0.0F}, cell, shingles);
            if (far_) {
                continue;
            }
            // Fascia and soffit along each open edge of the roof.
            const int nx[4] = {cx, cx + 1, cx, cx - 1};
            const int ny[4] = {cy - 1, cy, cy + 1, cy};
            for (int d = 0; d < 4; ++d) {
                if (cellIn(nx[d], ny[d])) {
                    continue;
                }
                if (tiles_.form(minX + nx[d], minY + ny[d], z) != content::TileForm::Open) {
                    continue;
                }
                V3 a;
                V3 b;
                // The edge's two vertices, and the wall line they sit above.
                switch (d) {
                    case 0: a = vertexAt(cx, cy); b = vertexAt(cx + 1, cy); break;
                    case 1: a = vertexAt(cx + 1, cy); b = vertexAt(cx + 1, cy + 1); break;
                    case 2: a = vertexAt(cx + 1, cy + 1); b = vertexAt(cx, cy + 1); break;
                    default: a = vertexAt(cx, cy + 1); b = vertexAt(cx, cy); break;
                }
                const V3 outward{static_cast<float>(kDx[d]), 0.0F, static_cast<float>(kDy[d])};
                constexpr float kFascia = 0.13F;
                V3 fascia[4] = {{a.x, a.y - kFascia, a.z}, {b.x, b.y - kFascia, b.z}, {b.x, b.y, b.z}, {a.x, a.y, a.z}};
                emit.quadOut(fascia, outward, cell, soffit);
                // The soffit: back from the fascia's foot to the wall face.
                const V3 wa{a.x - outward.x * kOverhang, a.y - kFascia, a.z - outward.z * kOverhang};
                const V3 wb{b.x - outward.x * kOverhang, b.y - kFascia, b.z - outward.z * kOverhang};
                V3 under[4] = {{a.x, a.y - kFascia, a.z}, {b.x, b.y - kFascia, b.z}, wb, wa};
                emit.quadOut(under, V3{0.0F, -1.0F, 0.0F}, cell, soffit);
            }
        }
        // A chimney on a ridge cell, by a hash of the roof's own corner.
        if (!far_ && deck.cells.size() >= 12) {
            const std::uint32_t pick =
                render::variantMix(static_cast<std::uint32_t>(minX) * 7919U ^ static_cast<std::uint32_t>(minY) * 104729U ^
                           static_cast<std::uint32_t>(z));
            for (std::size_t attempt = 0; attempt < deck.cells.size(); ++attempt) {
                const auto& [x, y] = deck.cells[(pick + attempt) % deck.cells.size()];
                const int cx = x - minX;
                const int cy = y - minY;
                const int d = std::min({dist[vi(cx, cy)], dist[vi(cx + 1, cy)], dist[vi(cx, cy + 1)],
                                        dist[vi(cx + 1, cy + 1)]});
                if (d < std::max(1, maxD - 1)) {
                    continue;
                }
                const float base = deckY + pitch * static_cast<float>(d) - 0.1F;
                const float top = deckY + pitch * static_cast<float>(maxD) + 0.8F;
                const float fx = static_cast<float>(x) + 0.28F;
                const float fz = static_cast<float>(y) + 0.28F;
                const CellId cell{x, y, z};
                emit.box(fx, fx + 0.44F, base, top, fz, fz + 0.44F,
                         static_cast<std::uint8_t>(kFaceTopBit | kFaceSideBits), cell,
                         dressOf(atlas_, false, Surface::BrickWall, 0.9F));
                emit.box(fx - 0.05F, fx + 0.49F, top, top + 0.1F, fz - 0.05F, fz + 0.49F,
                         static_cast<std::uint8_t>(kFaceTopBit | kFaceSideBits), cell,
                         dressOf(atlas_, false, Surface::GraniteAshlar, 0.9F));
                break;
            }
        }
    }

    const sim::TileQuery& tiles_;
    const std::vector<std::int32_t>& waterAlpha_;
    const SurfaceAtlas& atlas_;
    ChunkKey key_;
    bool far_;
    ChunkGeometry& out_;
    CellCache cache_;
    Emitter body_;
    Emitter extra_;
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
    if (lighting.perPixel) {
        // The light is the shader's: nothing of the clock, the lamps or the
        // weather is in a vertex colour, so only a rebuild moves the version.
        // Bit 11 keeps it apart from the minute bucket of the vertex-colour
        // look (1440 minutes never reach it), so a switch re-uploads.
        return (rebuildCount << 12) + 1U + 0x800U;
    }
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

ChunkMaterials ChunkMaterials::fromAtlas(const render::TileAtlas& /*atlas*/) {
    ChunkMaterials materials;
    materials.surfaces_ = SurfaceAtlas::build();
    return materials;
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
                                 std::uint32_t rebuildCount, Detail detail) {
    ChunkGeometry out;
    out.key = key;
    out.rebuildCount = rebuildCount;
    out.detail = detail;
    if (key.cx < 0 || key.cy < 0) {
        return out;
    }
    Builder builder(tiles, atlas, materials, key, detail, out);
    builder.run();
    std::sort(out.roofs.begin(), out.roofs.end(),
              [](const RoofLayer& a, const RoofLayer& b) { return a.level < b.level; });
    return out;
}

// ---------------------------------------------------------------------------
// colour
// ---------------------------------------------------------------------------

namespace {

/// THE PER-PIXEL LOOK (ChunkLighting::perPixel): the vertices of `arrays` that
/// are (or are not) standing water, appended to `mesh` with the colour of what
/// the surface IS and none of the light on it. Land wears its tone and the pack's
/// wet darkening; water wears what the water shader reads (see colourWater).
void colourLit(MeshData& mesh, const SurfaceArrays& arrays, bool water) {
    const std::size_t vertices = arrays.vertexCount();
    const auto isWater = [&](std::size_t i) {
        return i < arrays.shore.size() && arrays.shore[i] >= 0.0F;
    };
    std::vector<std::int32_t> remap(vertices, -1);
    const std::size_t first = mesh.vertexCount();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < vertices; ++i) {
        if (isWater(i) != water) {
            continue;
        }
        remap[i] = static_cast<std::int32_t>(first + kept);
        ++kept;
        mesh.positions.insert(mesh.positions.end(), arrays.positions.begin() + static_cast<std::ptrdiff_t>(i * 3),
                              arrays.positions.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3));
        mesh.texcoords.insert(mesh.texcoords.end(), arrays.texcoords.begin() + static_cast<std::ptrdiff_t>(i * 2),
                              arrays.texcoords.begin() + static_cast<std::ptrdiff_t>(i * 2 + 2));
        mesh.texcoords2.insert(mesh.texcoords2.end(), arrays.texcoords2.begin() + static_cast<std::ptrdiff_t>(i * 2),
                               arrays.texcoords2.begin() + static_cast<std::ptrdiff_t>(i * 2 + 2));
        mesh.normals.insert(mesh.normals.end(), arrays.normals.begin() + static_cast<std::ptrdiff_t>(i * 3),
                            arrays.normals.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3));
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;
        if (water) {
            r = std::clamp(arrays.shore[i] / 3.0F, 0.0F, 1.0F);
            g = arrays.wetAlpha[i];
            b = 0.0F;
        } else {
            const float wet = arrays.wetAlpha[i];
            r = (1.0F + (kWaterTintFull.r - 1.0F) * wet) * arrays.tones[i * 3];
            g = (1.0F + (kWaterTintFull.g - 1.0F) * wet) * arrays.tones[i * 3 + 1];
            b = (1.0F + (kWaterTintFull.b - 1.0F) * wet) * arrays.tones[i * 3 + 2];
        }
        mesh.colours.push_back(channel8(r));
        mesh.colours.push_back(channel8(g));
        mesh.colours.push_back(channel8(b));
        // The alpha is what the sky reaches: 255 open to it, 0 in a room. The
        // scene shader dims the ambient by it (and never reads it as a blend).
        mesh.colours.push_back(water || i >= arrays.skyOpen.size() ? 255U : channel8(arrays.skyOpen[i]));
    }
    for (std::size_t t = 0; t + 2 < arrays.indices.size(); t += 3) {
        const std::int32_t a = remap[arrays.indices[t]];
        const std::int32_t b = remap[arrays.indices[t + 1]];
        const std::int32_t c = remap[arrays.indices[t + 2]];
        if (a < 0 || b < 0 || c < 0) {
            continue;
        }
        mesh.indices.push_back(static_cast<std::uint16_t>(a));
        mesh.indices.push_back(static_cast<std::uint16_t>(b));
        mesh.indices.push_back(static_cast<std::uint16_t>(c));
    }
}

/// Colours `arrays` and appends it to `mesh`, its indices lifted by the
/// vertices already there.
void colourInto(MeshData& mesh, const SurfaceArrays& arrays, const ChunkLighting& lighting,
                const render::SkyState& sky, bool hasDynamic) {
    const std::size_t first = mesh.vertexCount();
    const std::size_t vertices = arrays.vertexCount();
    mesh.positions.insert(mesh.positions.end(), arrays.positions.begin(), arrays.positions.end());
    mesh.texcoords.insert(mesh.texcoords.end(), arrays.texcoords.begin(), arrays.texcoords.end());
    mesh.texcoords2.insert(mesh.texcoords2.end(), arrays.texcoords2.begin(), arrays.texcoords2.end());
    mesh.normals.insert(mesh.normals.end(), arrays.normals.begin(), arrays.normals.end());
    for (const std::uint16_t index : arrays.indices) {
        mesh.indices.push_back(static_cast<std::uint16_t>(index + first));
    }
    mesh.colours.resize((first + vertices) * 4U);

    // Light is a fact about the CELL, so it is computed once per face and
    // written to its corners.
    std::int32_t lastX = INT32_MIN;
    std::int32_t lastY = INT32_MIN;
    std::int32_t lastZ = INT32_MIN;
    render::Rgb surface{};
    for (std::size_t i = 0; i < vertices; ++i) {
        const std::int32_t x = arrays.cells[i * 3];
        const std::int32_t y = arrays.cells[i * 3 + 1];
        const std::int32_t z = arrays.cells[i * 3 + 2];
        if (x != lastX || y != lastY || z != lastZ) {
            // The lamp's share on a SURFACE: the pool field (lighting.hpp,
            // THE SURFACE POOL) where the scene built one -- a hot core and
            // a skirt that carries to eight tiles -- and, without one, the
            // law's own glow shaped by pooledGlow() as it always was. The
            // glow field itself is untouched either way, and so is the
            // light law that reads it.
            render::Rgb pool{};
            if (lighting.pool != nullptr) {
                const render::Rgb baked = lighting.pool->at(x, y, z);
                const render::Rgb live = hasDynamic
                                             ? render::dynamicPoolAt(*lighting.dynamicLamps, x, y, z)
                                             : render::Rgb{};
                pool = render::Rgb{std::max(baked.r, live.r), std::max(baked.g, live.g),
                                   std::max(baked.b, live.b)};
            } else {
                const render::Rgb baked =
                    lighting.glow != nullptr ? lighting.glow->at(x, y, z) : render::Rgb{};
                const render::Rgb live = hasDynamic
                                             ? render::dynamicGlowAt(*lighting.dynamicLamps, x, y, z)
                                             : render::Rgb{};
                pool = render::pooledGlow(render::Rgb{std::max(baked.r, live.r),
                                                      std::max(baked.g, live.g),
                                                      std::max(baked.b, live.b)});
            }
            // The pool gutters with the wind as the halos do; a candle
            // behind glass (the spill, below) does not.
            pool = render::Rgb{pool.r * sky.haloScale, pool.g * sky.haloScale, pool.b * sky.haloScale};
            surface = render::Rgb{sky.ambient.r + pool.r, sky.ambient.g + pool.g,
                                  sky.ambient.b + pool.b};
            lastX = x;
            lastY = y;
            lastZ = z;
        }
        // THE SPILL: a lit pane's candle on the cells round it, read AT THE
        // VERTEX as the mean of the four cells that meet at its corner, so
        // the map does not step at a cell seam (the round-two player's
        // first). A face's corners differ; the pool above is the cell's.
        render::Rgb candle{};
        if (lighting.spill != nullptr && !lighting.spill->empty()) {
            const std::int32_t px = static_cast<std::int32_t>(std::floor(arrays.positions[i * 3] + 0.001F));
            const std::int32_t pz =
                static_cast<std::int32_t>(std::floor(arrays.positions[i * 3 + 2] + 0.001F));
            for (std::int32_t dy = -1; dy <= 0; ++dy) {
                for (std::int32_t dx = -1; dx <= 0; ++dx) {
                    const auto found = lighting.spill->find(cellKey(px + dx, pz + dy, z));
                    if (found != lighting.spill->end()) {
                        candle.r += found->second.r * 0.25F;
                        candle.g += found->second.g * 0.25F;
                        candle.b += found->second.b * 0.25F;
                    }
                }
            }
        }
        const float facing = arrays.facing[i];
        // A vertex colour is clipped at one per channel by the GPU: the
        // clamp is at one here, as one colour, so the hue holds in the
        // brightest band (the round-three auditor's third).
        const render::Rgb clamped = render::clampLight(
            render::Rgb{surface.r + candle.r, surface.g + candle.g, surface.b + candle.b}, 1.0F);
        render::Rgb lit{clamped.r * facing, clamped.g * facing, clamped.b * facing};
        const float wet = arrays.wetAlpha[i];
        if (wet > 0.0F) {
            lit = render::Rgb{lit.r * (1.0F + (kWaterTintFull.r - 1.0F) * wet),
                              lit.g * (1.0F + (kWaterTintFull.g - 1.0F) * wet),
                              lit.b * (1.0F + (kWaterTintFull.b - 1.0F) * wet)};
        }
        // The surface's own tone: white for a textured face, the flat colour
        // of a material (or a far face) otherwise.
        lit = render::Rgb{lit.r * arrays.tones[i * 3], lit.g * arrays.tones[i * 3 + 1],
                          lit.b * arrays.tones[i * 3 + 2]};
        const std::size_t at = (first + i) * 4U;
        mesh.colours[at] = channel8(lit.r);
        mesh.colours[at + 1] = channel8(lit.g);
        mesh.colours[at + 2] = channel8(lit.b);
        mesh.colours[at + 3] = 255U;
    }
}

}  // namespace

MeshData colourChunk(const ChunkGeometry& geometry, const ChunkLighting& lighting) {
    MeshData mesh;
    mesh.id = geometry.detail == Detail::Far ? farChunkMeshId(geometry.key) : chunkMeshId(geometry.key);
    mesh.version = chunkVersion(geometry.rebuildCount, lighting) ^
                   (geometry.detail == Detail::Far ? kFarVersionBit : 0U);
    if (lighting.perPixel) {
        colourLit(mesh, geometry, false);
        colourLit(mesh, geometry.extra, false);
        return mesh;
    }
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds, lighting.weather);
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();
    colourInto(mesh, geometry, lighting, sky, hasDynamic);
    colourInto(mesh, geometry.extra, lighting, sky, hasDynamic);
    return mesh;
}

MeshData colourWater(const ChunkGeometry& geometry) {
    MeshData mesh;
    const bool far = geometry.detail == Detail::Far;
    mesh.id = waterMeshId(geometry.key, far);
    ChunkLighting lit;
    lit.perPixel = true;
    mesh.version = chunkVersion(geometry.rebuildCount, lit) ^ (far ? kFarVersionBit : 0U);
    colourLit(mesh, geometry, true);
    return mesh;
}

MeshData colourRoof(const ChunkGeometry& geometry, const RoofLayer& roof,
                    const ChunkLighting& lighting) {
    MeshData mesh;
    mesh.id = roofMeshId(geometry.key, roof.level);
    mesh.version = chunkVersion(geometry.rebuildCount, lighting) ^
                   (geometry.detail == Detail::Far ? kFarVersionBit : 0U);
    if (lighting.perPixel) {
        colourLit(mesh, roof, false);
        return mesh;
    }
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds, lighting.weather);
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();
    colourInto(mesh, roof, lighting, sky, hasDynamic);
    return mesh;
}

MeshData meshChunk(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                   const ChunkMaterials& materials, ChunkKey key, std::uint32_t rebuildCount,
                   const ChunkLighting& lighting) {
    return colourChunk(buildChunkGeometry(tiles, atlas, materials, key, rebuildCount), lighting);
}

}  // namespace granadad::render3d
