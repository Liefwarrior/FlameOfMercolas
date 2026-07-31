#include "granadad/content/world.hpp"

#include <string>

namespace granadad::content {

World::World(Coords coords, LaneLayout lanes)
    : coords_(coords),
      lanes_(std::move(lanes)),
      tileCount_(static_cast<std::size_t>(coords.chunkCount()) *
                 static_cast<std::size_t>(kTilesPerChunk)) {
    const std::size_t laneCount = lanes_.count();
    byteLanes_.resize(laneCount);
    shortLanes_.resize(laneCount);
    for (std::size_t i = 0; i < laneCount; ++i) {
        // Exactly one backing vector is allocated per lane; the other stays
        // empty, so asking a byte lane for its short storage is a hard error
        // rather than a silently-wrong read.
        if (lanes_.byIndex(i).bytesPerTile == 2) {
            shortLanes_[i].assign(tileCount_, 0);
        } else {
            byteLanes_[i].assign(tileCount_, 0);
        }
    }
}

std::span<std::uint8_t> World::byteLane(std::size_t laneIndex) {
    const LaneDef& def = lanes_.byIndex(laneIndex);
    if (def.bytesPerTile != 1) {
        throw FormatError("lane '" + def.name + "' is a short lane, not a byte lane");
    }
    return byteLanes_[laneIndex];
}

std::span<const std::uint8_t> World::byteLane(std::size_t laneIndex) const {
    const LaneDef& def = lanes_.byIndex(laneIndex);
    if (def.bytesPerTile != 1) {
        throw FormatError("lane '" + def.name + "' is a short lane, not a byte lane");
    }
    return byteLanes_[laneIndex];
}

std::span<std::uint16_t> World::shortLane(std::size_t laneIndex) {
    const LaneDef& def = lanes_.byIndex(laneIndex);
    if (def.bytesPerTile != 2) {
        throw FormatError("lane '" + def.name + "' is a byte lane, not a short lane");
    }
    return shortLanes_[laneIndex];
}

std::span<const std::uint16_t> World::shortLane(std::size_t laneIndex) const {
    const LaneDef& def = lanes_.byIndex(laneIndex);
    if (def.bytesPerTile != 2) {
        throw FormatError("lane '" + def.name + "' is a byte lane, not a short lane");
    }
    return shortLanes_[laneIndex];
}

ChunkView World::chunk(std::size_t chunkIndex) {
    if (chunkIndex >= chunkCount()) {
        throw FormatError("chunkIndex " + std::to_string(chunkIndex) + " out of range (" +
                          std::to_string(chunkCount()) + " chunks)");
    }
    return ChunkView(*this, chunkIndex);
}

void World::putOverlay(OverlayId overlay, std::uint32_t tileIndex, std::uint16_t value) {
    const std::size_t ordinal = static_cast<std::size_t>(overlay);
    if (ordinal >= kOverlayCount) {
        throw FormatError("unknown overlay ordinal " + std::to_string(ordinal));
    }
    if (static_cast<std::size_t>(tileIndex) >= tileCount_) {
        throw FormatError("overlay tile index " + std::to_string(tileIndex) + " out of range (" +
                          std::to_string(tileCount_) + " tiles)");
    }
    std::vector<OverlayEntry>& cells = overlays_[ordinal];
    if (!cells.empty() && cells.back().tileIndex >= tileIndex) {
        throw FormatError("overlay cells must arrive in strictly ascending tile order: " +
                          std::to_string(tileIndex) + " after " +
                          std::to_string(cells.back().tileIndex));
    }
    cells.push_back(OverlayEntry{tileIndex, value});
}

std::span<const OverlayEntry> World::overlay(OverlayId overlay) const {
    const std::size_t ordinal = static_cast<std::size_t>(overlay);
    if (ordinal >= kOverlayCount) {
        throw FormatError("unknown overlay ordinal " + std::to_string(ordinal));
    }
    return overlays_[ordinal];
}

std::span<std::uint8_t> ChunkView::byteLane(std::size_t laneIndex) const {
    const std::size_t offset = World::tileIndex(chunkIndex_, 0);
    return world_->byteLane(laneIndex).subspan(offset, static_cast<std::size_t>(kTilesPerChunk));
}

std::span<std::uint16_t> ChunkView::shortLane(std::size_t laneIndex) const {
    const std::size_t offset = World::tileIndex(chunkIndex_, 0);
    return world_->shortLane(laneIndex).subspan(offset, static_cast<std::size_t>(kTilesPerChunk));
}

}  // namespace granadad::content
