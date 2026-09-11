#pragma once

// THE CHUNK MESHER -- world geometry, as the W lane will fill it in.
//
// The district is 256x192 columns of ~29 levels read through sim::TileQuery.
// The mesher cuts it into 16x16-column chunks (12x8 authored, the VOID border
// chunks skipped), turns each into ONE MeshData -- greedy-meshed faces off the
// same voxel classification the software pass uses, UVs into the tile atlas,
// per-vertex colours from SkyState + LampGlow -- and hands it back under a
// stable mesh id. A chunk rebuilds locally (a door, a fluid change) and
// recolours when the time-of-day bucket moves; the version field is what
// tells the adapter which.
//
// THIS LANE SHIPS THE CONTRACT AND A STUB. meshChunk() below returns an empty
// mesh with the right id and version; the W lane replaces the body and adds
// tests/test_chunk_mesher.cpp. The ids, the chunk size and the coordinate
// mapping are fixed HERE so the actor and viewmodel lanes can build against
// them in parallel.

#include <cstdint>

#include "granadad/render3d/scene.hpp"

namespace granadad::sim {
class TileQuery;
}

namespace granadad::render3d {

/// Columns per chunk side. 16 gives 12x8 = 96 authored chunks of the Docks.
inline constexpr std::int32_t kChunkTiles = 16;
/// Chunk columns across the baked district (256 / 16). Fixes the id layout.
inline constexpr std::int32_t kChunksAcross = 16;
inline constexpr std::int32_t kChunksDown = 12;

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

/// What the mesher needs besides the tiles: the lighting bucket it colours
/// with. The W lane widens this (SkyState, LampGlow) as it lands.
struct ChunkLighting {
    /// Seconds since midnight, bucketed by the caller to the minute.
    int timeOfDaySeconds = 12 * 3600;
};

/// The version a chunk mesh carries for a rebuild count and a lighting bucket
/// -- so a recolour and a rebuild both move it and nothing else does.
[[nodiscard]] std::uint32_t chunkVersion(std::uint32_t rebuildCount,
                                         const ChunkLighting& lighting) noexcept;

/// Meshes one chunk across every level. STUB: the id and version are real,
/// the arrays are empty. The W lane fills the body.
[[nodiscard]] MeshData meshChunk(const sim::TileQuery& tiles, ChunkKey key,
                                 std::uint32_t rebuildCount, const ChunkLighting& lighting);

}  // namespace granadad::render3d
