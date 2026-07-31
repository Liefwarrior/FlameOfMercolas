#pragma once

// META + WRLD -> a populated World.
//
// META layout v1, little-endian, tightly packed:
//   u8   metaVersion            (1; anything else is a hard fail)
//   i32  chunksX
//   i32  chunksY
//   i32  chunksZ
//   u8   laneCount
//   laneCount x { u8 nameLen, nameLen bytes US-ASCII name, u8 bytesPerTile }
//   i32  siteCount              (always 0 in v1; non-zero is a hard fail)
//
// WRLD layout v1:
//   i32  chunkCount             (must equal chunksX*chunksY*chunksZ — the saver
//                                always writes every chunk, border included)
//   chunkCount x { i32 chunkIndex, i32 byteLen, byteLen bytes of codec frame }
//   no trailer.
//
// chunkIndex must be strictly ascending and in range.

#include <cstdint>
#include <span>

#include "granadad/content/coords.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/trojsav.hpp"
#include "granadad/content/world.hpp"

namespace granadad::content {

/// The META layout version this build reads.
inline constexpr std::uint8_t kMetaVersion = 1;

/// Everything META describes.
struct WorldMeta {
    Coords coords{kMinChunksPerAxis, kMinChunksPerAxis, kMinChunksPerAxis};
    LaneLayout lanes;
    /// Always 0 in v1. Site loading is not implemented in the Java either.
    std::int32_t siteCount = 0;
};

/// Decodes a META section. Replays the lane list against the core lane set: the
/// first seven entries must match by name AND width, positionally, and anything
/// beyond is registered as an extension lane with whatever the file declares.
[[nodiscard]] WorldMeta readMetaSection(std::span<const std::uint8_t> meta);

/// Decodes a WRLD section into a fresh World.
[[nodiscard]] World readWorldSection(const WorldMeta& meta, std::span<const std::uint8_t> wrld);

/// Reads META + WRLD from an open container.
[[nodiscard]] World loadWorld(TrojSav& save);

/// Opens a .trojsav and loads its world.
[[nodiscard]] World loadWorldFile(const std::filesystem::path& file);

}  // namespace granadad::content
