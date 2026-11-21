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
// THE SURFACES (world_surfaces.hpp). A face wears a procedural two-metre
// surface -- ashlar, brick, cobble, planks, thatch -- tiled over WORLD-METRE
// UVs (the vertex's own position projected on its plane), so a wall is a run
// of courses and a floor a field of setts, not one 16-pixel tile stretched
// over a storey. Per vertex: `texcoords` count repeats of the surface and
// `texcoords2` name its cell in the atlas; the shader folds one into the other
// (rl/shaders.cpp). A material with no surface of its own is drawn flat in its
// tone (the per-vertex `tones`).
//
// PHASE 4, WHAT STANDS ON TOP OF THE TILE BOXES. The tile boxes above are the
// BODY of a chunk, and every claim about faces below is about the body: a quad
// per face, four vertices each. What phase 4 adds is DETAIL, kept apart so the
// body stays the building the sim collides with, drawn exactly:
//
//   extra   trim and shapes in the same mesh as the body: a plinth under every
//           street-level wall, a cornice or string course at every wall head,
//           the posts of a door, ramps as wedges and stairs as steps, ceiling
//           beams, quay coping, bollards and pilings;
//   roofs   hipped roofs with eaves, soffits and chimneys over the district's
//           building tops, one mesh per level, drawn only while the eye is
//           below the deck (the Skyrunners run those decks, which stay flat
//           and are the sim's truth: a pitched roof over a walkable plane is a
//           picture of a roof, so it steps aside when you stand on it);
//   far     Detail::Far, a second geometry of the same chunk for the far ring
//           of the district: the body and the roofs, no trim, every face in its
//           surface's mean colour, so a distant street holds its colour
//           instead of shimmering into noise.
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
#include <map>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/world_surfaces.hpp"

namespace granadad::sim {
class TileQuery;
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

/// Levels a district may have in the id scheme below.
inline constexpr std::int32_t kRoofLevels = 32;
/// The id of one level's roof over a chunk: 2000 + (chunk * 32 + level).
/// Inside the chunk range (1000..99999) and clear of the far meshes.
inline constexpr std::uint32_t kRoofMeshIdBase = 2000;
[[nodiscard]] constexpr std::uint32_t roofMeshId(ChunkKey key, std::int32_t level) noexcept {
    return kRoofMeshIdBase +
           static_cast<std::uint32_t>((key.cy * kChunksAcross + key.cx) * kRoofLevels + level);
}
/// The id of a chunk's FAR geometry (Detail::Far), beside the near one.
inline constexpr std::uint32_t kFarMeshIdBase = 20000;
[[nodiscard]] constexpr std::uint32_t farChunkMeshId(ChunkKey key) noexcept {
    return kFarMeshIdBase + static_cast<std::uint32_t>(key.cy * kChunksAcross + key.cx);
}

/// The id of a chunk's WATER (the harbour's top faces, cut out of the body when
/// the colour stage is lit per pixel so the water shader can draw them), near
/// and far, beside the others.
inline constexpr std::uint32_t kWaterMeshIdBase = 30000;
inline constexpr std::uint32_t kFarWaterMeshIdBase = 30200;
[[nodiscard]] constexpr std::uint32_t waterMeshId(ChunkKey key, bool far) noexcept {
    return (far ? kFarWaterMeshIdBase : kWaterMeshIdBase) +
           static_cast<std::uint32_t>(key.cy * kChunksAcross + key.cx);
}

/// The bit a far geometry's mesh version is flipped by, so a chunk that
/// changes level of detail is re-uploaded even where its id is the same (a
/// roof's is).
inline constexpr std::uint32_t kFarVersionBit = 0x80000000U;

/// How much of a chunk is meshed. Near is everything; Far is the body and the
/// roofs only, every face flat in its surface's mean colour.
enum class Detail : std::uint8_t { Near = 0, Far = 1 };

/// What the colour stage needs besides the geometry.
struct ChunkLighting {
    /// PHASE 5. The light is the scene shader's, per pixel, from the point
    /// lights and the sun the description carries: the vertex colour is what
    /// the surface itself is (its tone, the wet darkening, the hand-painted
    /// facing is gone -- the normals do that now) and nothing of the clock, the
    /// lamps or the weather, so a chunk is coloured once and never again, and
    /// its water is cut into a mesh of its own. False (the default) is the
    /// vertex-colour light every frame had before the phase.
    bool perPixel = false;
    /// Seconds since midnight, bucketed by the caller to the minute.
    int timeOfDaySeconds = 12 * 3600;
    /// WEATHER. The sky the faces are lit under is skyAt(timeOfDaySeconds,
    /// weather); the default (clear) is the sky every chunk was ever
    /// coloured under. Folded into chunkVersion so a fog that rolls in
    /// recolours the district the way a minute does.
    render::Weather weather;
    /// The baked lamp field (27 Docks lamps). Null = no lamps, ambient only.
    const render::LampGlow* glow = nullptr;
    /// The handful of lights that come and go (a tavern hearth, its candles).
    /// Null or empty = none.
    const std::vector<render::Lamp>* dynamicLamps = nullptr;
    /// A key for the dynamic lamp SET, so the version moves when it does.
    /// dynamicLampKey() computes it; zero means "no dynamic lamps".
    std::uint32_t lampKey = 0;
    /// THE SURFACE POOL (render::LampGlow::buildPool, lighting.hpp): what
    /// the lamps put on a surface, as against `glow`, which is what the
    /// light law reads. Null means no pool was built (a scene with no lamp
    /// list): the colour stage then shapes `glow` with pooledGlow() as it
    /// always did, so a hand-built lighting reads exactly as before.
    const render::LampGlow* pool = nullptr;
    /// THE SPILL (world_scene.cpp, spillMap): the candle a lit pane lays on
    /// the wall cells round it, as a glow ADDED to the pool at those cells
    /// -- keyed by cellKey(x, y, z). Null or empty means no pane is up.
    const std::map<std::uint64_t, render::Rgb>* spill = nullptr;
};

/// The key the spill map is kept under: a cell's three coordinates packed,
/// the same for the chunk colour stage and the piece relight.
[[nodiscard]] constexpr std::uint64_t cellKey(std::int32_t x, std::int32_t y,
                                              std::int32_t z) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 42) ^
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 21) ^
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(z));
}

/// The key a set of dynamic lamps hashes to. Zero for an empty set.
[[nodiscard]] std::uint32_t dynamicLampKey(const std::vector<render::Lamp>& lamps) noexcept;

/// The version a chunk mesh carries for a rebuild count and a lighting bucket
/// -- so a recolour (the minute, the lamp set) and a rebuild both move it and
/// nothing else does. Never zero (the adapter's "never uploaded" sentinel).
[[nodiscard]] std::uint32_t chunkVersion(std::uint32_t rebuildCount,
                                         const ChunkLighting& lighting) noexcept;

/// WEATHER. The key a Weather folds into a mesh version: zero for clear, so
/// a clear version is what it always was; else the kind above the intensity
/// to a hundredth. chunkVersion() spreads it over every chunk's version and
/// the world scene spreads the same key over the sky dome's and the veil's,
/// so a sky that moved with the weather and not with the minute -- a day
/// skipped under live weather -- is rebuilt rather than kept.
[[nodiscard]] std::uint32_t weatherVersionKey(const render::Weather& weather) noexcept;

// ---------------------------------------------------------------------------
// materials
// ---------------------------------------------------------------------------

/// THE MATERIAL TABLE of the district: the procedural surface atlas
/// (world_surfaces.hpp) and what a material wears in each role. Nothing in it
/// reads the owner's 16-pixel tile pack any more; that pack stays the 2D
/// sprites' and the old software pass's.
///
/// HOW TO ADD A MATERIAL. A material is a raw under content/raws/materials; its
/// registry id is its position in render::materialIds() (atlas.cpp). Give it a
/// tone in the renderer's tone table (atlas.cpp's kFallbackTones) and, if it
/// should wear a pattern, a row in surfaceFor() (world_surfaces.cpp); with
/// neither it is drawn flat in its tone.
class ChunkMaterials {
public:
    /// Builds the surface atlas. The tile atlas is no longer read (kept in the
    /// signature so every caller stands).
    [[nodiscard]] static ChunkMaterials fromAtlas(const render::TileAtlas& atlas);

    /// The texture, ready for SceneDescription::putTexture.
    [[nodiscard]] const TextureData& texture() const noexcept { return surfaces_.texture(); }
    [[nodiscard]] const SurfaceAtlas& surfaces() const noexcept { return surfaces_; }

private:
    SurfaceAtlas surfaces_;
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

/// Vertex arrays with their per-vertex light inputs: what the colour stage
/// reads. The body, the detail and every roof level are one of these each.
struct SurfaceArrays {
    std::vector<float> positions;
    /// Repeats of the face's surface: world metres over kSurfaceMetres.
    std::vector<float> texcoords;
    /// The normalised origin of the surface's cell in the atlas.
    std::vector<float> texcoords2;
    /// Per vertex: the face's unit normal, cut from its corners (the quad is
    /// counter-clockwise seen from the side the face looks to).
    std::vector<float> normals;
    std::vector<std::uint16_t> indices;
    /// Per vertex: the cell the face belongs to (light is read there).
    std::vector<std::int32_t> cells;
    /// Per vertex: the directional factor (kFacingX / kFacingY / 1 / lift).
    std::vector<float> facing;
    /// Per vertex: the pack's water alpha for the face's wetness, 0 = dry.
    std::vector<float> wetAlpha;
    /// Per vertex: the colour multiplier of the face's surface (1 for a
    /// textured surface, the material's own tone for a flat one).
    std::vector<float> tones;
    /// Per vertex: negative for everything but standing water; on a water
    /// face, how far the vertex is from the nearest bank, in tiles, held at
    /// three. The water shader's foam and its shallows read it.
    std::vector<float> shore;
    /// Per vertex: 1 where the sky's light reaches the face, 0 for a face that
    /// looks into a room or up at a ceiling.
    std::vector<float> skyOpen;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size() / 3; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

/// One level's roof over a chunk. `deckY` is the height of the deck it covers:
/// the world scene draws it only while the eye is lower.
struct RoofLayer : SurfaceArrays {
    std::int32_t level = 0;
    float deckY = 0.0F;
};

/// A chunk's geometry with its per-vertex light inputs. See the header.
struct ChunkGeometry : SurfaceArrays {
    ChunkKey key;
    std::uint32_t rebuildCount = 0;
    Detail detail = Detail::Near;
    std::vector<ChunkFace> faces;
    /// Trim and shapes drawn with the body (the header says which).
    SurfaceArrays extra;
    /// Roofs, one per level that has one, lowest first.
    std::vector<RoofLayer> roofs;
    /// True if the 65535-vertex guard stopped emission early. Never in the
    /// Docks; a test says so.
    bool truncated = false;

    [[nodiscard]] std::size_t totalTriangles() const noexcept {
        std::size_t n = triangleCount() + extra.triangleCount();
        for (const RoofLayer& roof : roofs) {
            n += roof.triangleCount();
        }
        return n;
    }
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
                                               std::uint32_t rebuildCount,
                                               Detail detail = Detail::Near);

/// The colour stage: the body and its detail as a MeshData lit for
/// `lighting`, under the chunk's id (farChunkMeshId for a Far geometry) and
/// the version chunkVersion() gives it.
[[nodiscard]] MeshData colourChunk(const ChunkGeometry& geometry, const ChunkLighting& lighting);

/// The water of a chunk (the faces `shore` marks), as a mesh under waterMeshId:
/// only for a perPixel lighting, whose colourChunk leaves them out. The vertex
/// colour carries what the water shader reads: red the shore distance over three
/// tiles, green the pack's depth tint, blue the tide's own height over the
/// cell, alpha 255. Empty (no vertices) for a chunk with no water.
[[nodiscard]] MeshData colourWater(const ChunkGeometry& geometry);

/// The colour stage for one roof level of a chunk, under roofMeshId().
[[nodiscard]] MeshData colourRoof(const ChunkGeometry& geometry, const RoofLayer& roof,
                                  const ChunkLighting& lighting);

/// Both stages in one call. Empty arrays (with the right id and version)
/// for a chunk with nothing in it -- the VOID border.
[[nodiscard]] MeshData meshChunk(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                                 const ChunkMaterials& materials, ChunkKey key,
                                 std::uint32_t rebuildCount, const ChunkLighting& lighting);

}  // namespace granadad::render3d
