#include "granadad/render3d/chunk_mesher.hpp"

#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

std::uint32_t chunkVersion(std::uint32_t rebuildCount, const ChunkLighting& lighting) noexcept {
    const int minute = ((lighting.timeOfDaySeconds % 86400) + 86400) % 86400 / 60;
    // Rebuilds in the high bits, the lighting bucket in the low twelve (1440
    // minutes fit), plus one so a fresh mesh never reads as version 0.
    return (rebuildCount << 12) + static_cast<std::uint32_t>(minute) + 1U;
}

MeshData meshChunk(const sim::TileQuery& tiles, ChunkKey key, std::uint32_t rebuildCount,
                   const ChunkLighting& lighting) {
    // STUB -- see the header. The W lane replaces this body with the greedy
    // mesher over the shared voxel classifier; the id/version contract is
    // what the other lanes already build against.
    (void)tiles;
    MeshData mesh;
    mesh.id = chunkMeshId(key);
    mesh.version = chunkVersion(rebuildCount, lighting);
    return mesh;
}

}  // namespace granadad::render3d
