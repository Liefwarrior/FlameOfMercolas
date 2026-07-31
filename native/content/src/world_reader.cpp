#include "granadad/content/world_reader.hpp"

#include <string>
#include <vector>

#include "granadad/content/byte_reader.hpp"
#include "granadad/content/chunk_codec.hpp"

namespace granadad::content {
namespace {

/// Replays META's lane list against the core lane set.
///
/// The first kCoreLaneCount entries must match the canonical lanes by name AND
/// width, positionally — a world whose lane 0 is not a 2-byte "material" is not
/// a world this build can read, and pretending otherwise would misinterpret
/// every tile. Entries past the core set are extension lanes and are taken at
/// whatever name and width the file declares.
LaneLayout replayLanes(ByteReader& in) {
    const std::size_t laneCount = in.u8();
    if (laneCount < kCoreLaneCount) {
        throw FormatError(in.what() + ": lane-set mismatch: save has " +
                          std::to_string(laneCount) + " lanes, world pre-registers " +
                          std::to_string(kCoreLaneCount));
    }
    const LaneLayout coreLanes = LaneLayout::core();
    LaneLayout lanes;
    for (std::size_t i = 0; i < laneCount; ++i) {
        const std::size_t nameLen = in.u8();
        const std::span<const std::uint8_t> nameBytes = in.take(nameLen);
        std::string name(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
        const int bytesPerTile = in.u8();
        if (i < kCoreLaneCount) {
            const LaneDef& expected = coreLanes.byIndex(i);
            if (expected.name != name || expected.bytesPerTile != bytesPerTile) {
                throw FormatError(in.what() + ": lane-set mismatch at index " +
                                  std::to_string(i) + ": save has '" + name + "'/" +
                                  std::to_string(bytesPerTile) + ", world has '" + expected.name +
                                  "'/" + std::to_string(expected.bytesPerTile));
            }
        }
        lanes.registerLane(std::move(name), bytesPerTile);
    }
    return lanes;
}

}  // namespace

WorldMeta readMetaSection(std::span<const std::uint8_t> meta) {
    ByteReader in(meta, "TROJSAV META");
    const std::uint8_t metaVersion = in.u8();
    if (metaVersion != kMetaVersion) {
        throw FormatError("unsupported META layout version " + std::to_string(metaVersion) +
                          " (this build reads " + std::to_string(kMetaVersion) + ")");
    }
    const std::int32_t chunksX = in.i32();
    const std::int32_t chunksY = in.i32();
    const std::int32_t chunksZ = in.i32();

    WorldMeta result{Coords::checked(chunksX, chunksY, chunksZ), replayLanes(in), 0};
    result.siteCount = in.i32();
    if (result.siteCount != 0) {
        throw FormatError("META carries " + std::to_string(result.siteCount) +
                          " site defs; site loading is not implemented in this build");
    }
    // The Java's DataInputStream simply stops here, but a META section with
    // anything after siteCount means the writer and this reader disagree about
    // the layout.
    in.requireExhausted();
    return result;
}

World readWorldSection(const WorldMeta& meta, std::span<const std::uint8_t> wrld) {
    World world(meta.coords, meta.lanes);
    ChunkCodec codec(world.lanes());

    ByteReader in(wrld, "TROJSAV WRLD");
    const std::int32_t chunkCount = in.i32();
    const std::int32_t worldChunks = meta.coords.chunkCount();
    if (chunkCount != worldChunks) {
        throw FormatError("WRLD section carries " + std::to_string(chunkCount) +
                          " chunk frames; the world has " + std::to_string(worldChunks) +
                          " chunks (the saver always writes all of them)");
    }

    std::vector<DecodedOverlayCell> cells;
    std::int32_t prevIndex = -1;
    for (std::int32_t k = 0; k < chunkCount; ++k) {
        const std::int32_t chunkIndex = in.i32();
        const std::int32_t byteLen = in.i32();
        if (chunkIndex <= prevIndex || chunkIndex >= worldChunks) {
            throw FormatError("WRLD chunk frames not ascending in-range chunkIndex: " +
                              std::to_string(chunkIndex) + " after " + std::to_string(prevIndex));
        }
        if (byteLen < 0) {
            throw FormatError("WRLD chunk " + std::to_string(chunkIndex) +
                              " has negative frame length");
        }
        const std::span<const std::uint8_t> frame = in.take(static_cast<std::size_t>(byteLen));
        ByteReader frameIn(frame, "TROJSAV WRLD chunk " + std::to_string(chunkIndex));

        cells.clear();
        codec.decode(frameIn, world.chunk(static_cast<std::size_t>(chunkIndex)), cells);
        // The declared frame length and the codec's own framing must agree.
        frameIn.requireExhausted();

        for (const DecodedOverlayCell& cell : cells) {
            world.putOverlay(static_cast<OverlayId>(cell.ordinal),
                             static_cast<std::uint32_t>(World::tileIndex(
                                 static_cast<std::size_t>(chunkIndex), cell.localIdx)),
                             cell.value);
        }
        prevIndex = chunkIndex;
    }
    // WRLD has no trailer; the stream must land exactly on the end.
    in.requireExhausted();
    return world;
}

World loadWorld(TrojSav& save) {
    const WorldMeta meta = readMetaSection(save.section(sections::kMeta));
    return readWorldSection(meta, save.section(sections::kWrld));
}

World loadWorldFile(const std::filesystem::path& file) {
    TrojSav save = TrojSav::readFile(file);
    return loadWorld(save);
}

}  // namespace granadad::content
