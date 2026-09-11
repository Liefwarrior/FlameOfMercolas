#pragma once

// THE CHUNK MESHER -- the tile map as geometry.
//
// The district is 256x192 columns of ~29 levels read through sim::TileQuery.
// The mesher cuts it into 16x16-column chunks (12x8 authored, the VOID border
// chunks come out empty), classifies every cell with the SAME classifier the
// software raycaster uses (render/voxel_classify.hpp -- one opinion of what
// a cell is), and emits its faces: a top for every drawable cell, a bottom
// for every slab with air under it, four sides for every cell that has them
// -- each face dropped when the neighbouring cell already covers it, so two
// walls in a row share no z-fighting boundary and the solid substrate under
// the Docks costs nothing. Floors are quads at the walking surface, walls
// are the storey (kBandHeight tall), ramps and stairs are the thin slabs the
// software pass draws (its own stated gap; the wedge is a change to the
// shared classifier), water is a top face at the tide's height. Doors are
// where the map has them: a gap in the WALL run is a gap in the mesh, so
// the building the sim collides against is the building you see -- the
// renderer plan's ruling that the tile mesh IS the building.
//
// UVs point into ONE atlas texture built from the tile pack (ChunkMaterials
// below): the same 16x16 tile the software pass would have sampled for the
// same cell and face, picked with the same variant key. A side face wears
// its tile stretched over the whole face height, exactly as the software
// pass stretches it.
//
// THE LIGHT IS VERTEX COLOUR, in two stages that the version rules keep
// apart:
//
//   geometry   buildChunkGeometry(): positions, uvs, indices, and PER VERTEX
//              the cell it came from and its facing factor. Built once per
//              rebuild count (a door, a fluid change -- nothing moves it
//              yet), never per frame.
//   colour     colourChunk(): ambient from the day curve plus the baked
//              lamp field plus the handful of dynamic lamps, times the
//              facing factor, times the wetness tint -- the software pass's
//              own surface light, per vertex. Re-run when the minute bucket
//              or the dynamic lamp set moves, ~100k vertices for the whole
//              district, a couple of milliseconds. The rlsw frame and the
//              GL 3.3 frame get the identical bytes, which is the point.
//
// meshChunk() below does both in one call: the one-shot form the tests and
// any simple caller use. WorldScene (world_scene.hpp) keeps the geometry and
// re-runs only the colour.
//
// Everything is a pure function of the tile bytes, the lamps and the clock.
// Two builds of the same chunk are the same bytes, and tests/test_chunk_mesher
// says so on every build.

#include <cstdint>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render3d/scene.hpp"

namespace granadad::sim {
class TileQuery;
}

namespace granadad::render {
class LampGlow;
}

namespace granadad::render3d {

/// Columns per chunk side. 16 gives 12x8 = 96 authored chunks of the Docks.
inline constexpr std::int32_t kChunkTiles = 16;
/// Chunk columns across the baked district (256 / 16). Fixes the id layout.
inline constexpr std::int32_t kChunksAcross = 16;
inline constexpr std::int32_t kChunksDown = 12;

/// The one texture every chunk instance binds: the tile pack laid out as a
/// grid. Texture ids are their own space (the adapter keys textures apart
/// from meshes); this one is numbered with the chunks for legibility.
inline constexpr std::uint32_t kChunkAtlasTextureId = kChunkMeshIdBase;

struct ChunkKey {
    std::int32_t cx = 0;
    std::int32_t cy = 0;
};

/// Which chunk a world tile falls in.
[[nodiscard]] constexpr ChunkKey chunkOf(std::int32_t tileX, std::int32_t tileY) noexcept {
    // Tiles are never negative in a baked world (the VOID border starts at 0),
    // so plain division is floor division here.
    return ChunkKey{tileX / kChunkTiles, tileY / kChunkTiles};
}

/// The mesh id a chunk's geometry lives under. Row-major over the district.
[[nodiscard]] constexpr std::uint32_t chunkMeshId(ChunkKey key) noexcept {
    return kChunkMeshIdBase + static_cast<std::uint32_t>(key.cy * kChunksAcross + key.cx);
}

/// What the colour stage needs besides the geometry.
struct ChunkLighting {
    /// Seconds since midnight, bucketed by the caller to the minute.
    int timeOfDaySeconds = 12 * 3600;
    /// The baked lamp field (27 Docks lamps). Null = no lamps, ambient only.
    const render::LampGlow* glow = nullptr;
    /// The handful of lights that come and go (a tavern hearth, its candles).
    /// Null or empty = none.
    const std::vector<render::Lamp>* dynamicLamps = nullptr;
    /// A key for the dynamic lamp SET, so the version moves when it does.
    /// dynamicLampKey() computes it; zero means "no dynamic lamps".
    std::uint32_t lampKey = 0;
};

/// The key a set of dynamic lamps hashes to. Zero for an empty set.
[[nodiscard]] std::uint32_t dynamicLampKey(const std::vector<render::Lamp>& lamps) noexcept;

/// The version a chunk mesh carries for a rebuild count and a lighting bucket
/// -- so a recolour (the minute, the lamp set) and a rebuild both move it and
/// nothing else does. Never zero (the adapter's "never uploaded" sentinel).
[[nodiscard]] std::uint32_t chunkVersion(std::uint32_t rebuildCount,
                                         const ChunkLighting& lighting) noexcept;

// ---------------------------------------------------------------------------
// materials
// ---------------------------------------------------------------------------

/// A rectangle of the atlas texture, in 0..1 texture space.
struct UvRect {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
};

/// THE MATERIAL TABLE. Every tile image the pack holds, laid out on one
/// RGBA texture (kAtlasColumns across), plus the lookup from (material,
/// face, variant) to a rectangle of it -- the same lookup TileAtlas::tileFor
/// answers for the software pass, so a cell wears the same tile in 3D.
///
/// HOW TO ADD A MATERIAL. A material is a raw under content/raws/materials;
/// its registry id is its position in render::materialIds() (atlas.cpp's
/// kMaterialIds, sorted), and the art for its faces is authored in
/// content/art/custom/art-mapping.json under materials.<id>.forms
/// (wall/floor/ramp/stair/block). Add the raw, add the id to kMaterialIds
/// AND kFallbackTones in the same row, add the art (or let the procedural
/// fallback tone stand in), and every cell of that material meshes with it
/// -- nothing in this file changes. The colour of a face is the tile's own
/// texels times the light; there is no second colour table to keep in step.
class ChunkMaterials {
public:
    static constexpr int kAtlasColumns = 16;

    /// Lays the atlas's tiles out on one texture. The procedural pack works
    /// too (that is what the tests use: no art tree required).
    [[nodiscard]] static ChunkMaterials fromAtlas(const render::TileAtlas& atlas);

    /// The texture, ready for SceneDescription::putTexture.
    [[nodiscard]] const TextureData& texture() const noexcept { return texture_; }

    /// The rectangle of the tile a cell's face wears. `variantKey` is
    /// sideVariantKey() / flatVariantKey() of the cell.
    [[nodiscard]] UvRect uv(std::uint16_t material, render::FaceKind face,
                            std::uint32_t variantKey) const noexcept;

    [[nodiscard]] std::size_t tileCount() const noexcept { return tileCount_; }

private:
    const render::TileAtlas* atlas_ = nullptr;
    TextureData texture_;
    std::size_t tileCount_ = 0;
    int rows_ = 0;
};

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------

/// Which way a face looks, in scene space.
enum class FaceDir : std::uint8_t {
    Top = 0,     // +Y
    Bottom = 1,  // -Y
    North = 2,   // -Z
    South = 3,   // +Z
    East = 4,    // +X
    West = 5,    // -X
};

/// Bits of the six faces, for cellFaceMask().
inline constexpr std::uint8_t kFaceTopBit = 1U << 0;
inline constexpr std::uint8_t kFaceBottomBit = 1U << 1;
inline constexpr std::uint8_t kFaceNorthBit = 1U << 2;
inline constexpr std::uint8_t kFaceSouthBit = 1U << 3;
inline constexpr std::uint8_t kFaceEastBit = 1U << 4;
inline constexpr std::uint8_t kFaceWestBit = 1U << 5;
inline constexpr std::uint8_t kFaceSideBits =
    kFaceNorthBit | kFaceSouthBit | kFaceEastBit | kFaceWestBit;

/// One emitted quad, for tests and debugging: which cell, which way, which
/// art. Four vertices at `firstVertex`.
struct ChunkFace {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    FaceDir dir = FaceDir::Top;
    render::FaceKind kind = render::FaceKind::Side;
    std::uint32_t firstVertex = 0;
};

/// A chunk's geometry with its per-vertex light inputs. See the header.
struct ChunkGeometry {
    ChunkKey key;
    std::uint32_t rebuildCount = 0;
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<std::uint16_t> indices;
    /// Per vertex: the cell the face belongs to (light is read there).
    std::vector<std::int32_t> cells;
    /// Per vertex: the directional factor (kFacingX / kFacingY / 1 / lift).
    std::vector<float> facing;
    /// Per vertex: the pack's water alpha for the face's wetness, 0 = dry.
    std::vector<float> wetAlpha;
    std::vector<ChunkFace> faces;
    /// True if the 65535-vertex guard stopped emission early. Never in the
    /// Docks; a test says so.
    bool truncated = false;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size() / 3; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

/// The faces a single cell emits, given its neighbours. The wall-vs-floor
/// fact the tests pin: a WALL in the open has all four sides, a FLOOR in a
/// street has none.
[[nodiscard]] std::uint8_t cellFaceMask(const sim::TileQuery& tiles, std::int32_t x,
                                        std::int32_t y, std::int32_t z) noexcept;

/// Meshes one chunk across every level: the geometry stage.
[[nodiscard]] ChunkGeometry buildChunkGeometry(const sim::TileQuery& tiles,
                                               const render::TileAtlas& atlas,
                                               const ChunkMaterials& materials, ChunkKey key,
                                               std::uint32_t rebuildCount);

/// The colour stage: the geometry as a MeshData lit for `lighting`, under
/// the chunk's id and the version chunkVersion() gives it.
[[nodiscard]] MeshData colourChunk(const ChunkGeometry& geometry, const ChunkLighting& lighting);

/// Both stages in one call. Empty arrays (with the right id and version)
/// for a chunk with nothing in it -- the VOID border.
[[nodiscard]] MeshData meshChunk(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                                 const ChunkMaterials& materials, ChunkKey key,
                                 std::uint32_t rebuildCount, const ChunkLighting& lighting);

}  // namespace granadad::render3d
