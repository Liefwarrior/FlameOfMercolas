#include "granadad/content/chunk_codec.hpp"

#include <algorithm>
#include <string>

namespace granadad::content {

ChunkCodec::ChunkCodec(const LaneLayout& lanes) : laneOrder_(lanes.all()) {
    if (laneOrder_.empty() || laneOrder_.size() > kMaxLaneCount) {
        throw FormatError("lane count must be in [1, " + std::to_string(kMaxLaneCount) +
                          "]: " + std::to_string(laneOrder_.size()));
    }
    for (std::size_t i = 0; i < laneOrder_.size(); ++i) {
        if (laneOrder_[i].index != i) {
            throw FormatError("lane order must be dense ascending registry order; position " +
                              std::to_string(i) + " holds lane index " +
                              std::to_string(laneOrder_[i].index) + " (" + laneOrder_[i].name + ")");
        }
    }
}

void ChunkCodec::decodeByteLane(ByteReader& in, std::span<std::uint8_t> lane,
                                const LaneDef& def) const {
    const std::uint8_t mode = in.u8();
    if (mode == kModeRaw) {
        in.readFully(lane);
        return;
    }
    if (mode != kModeRle) {
        throw FormatError(in.what() + ": lane '" + def.name + "' unknown encoding mode " +
                          std::to_string(mode));
    }
    const std::uint32_t runs = in.u16();
    std::size_t pos = 0;
    for (std::uint32_t r = 0; r < runs; ++r) {
        const std::size_t len = in.u16();
        const std::uint8_t value = in.u8();
        if (len == 0 || pos + len > static_cast<std::size_t>(kTilesPerChunk)) {
            throw FormatError(in.what() + ": lane '" + def.name + "' malformed run at cell " +
                              std::to_string(pos));
        }
        std::fill_n(lane.begin() + static_cast<std::ptrdiff_t>(pos), len, value);
        pos += len;
    }
    if (pos != static_cast<std::size_t>(kTilesPerChunk)) {
        throw FormatError(in.what() + ": lane '" + def.name + "' runs cover " +
                          std::to_string(pos) + " of " + std::to_string(kTilesPerChunk) + " cells");
    }
}

void ChunkCodec::decodeShortLane(ByteReader& in, std::span<std::uint16_t> lane,
                                 const LaneDef& def) const {
    const std::uint8_t mode = in.u8();
    if (mode == kModeRaw) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(kTilesPerChunk); ++i) {
            lane[i] = in.u16();
        }
        return;
    }
    if (mode != kModeRle) {
        throw FormatError(in.what() + ": lane '" + def.name + "' unknown encoding mode " +
                          std::to_string(mode));
    }
    const std::uint32_t runs = in.u16();
    std::size_t pos = 0;
    for (std::uint32_t r = 0; r < runs; ++r) {
        const std::size_t len = in.u16();
        const std::uint16_t value = in.u16();
        if (len == 0 || pos + len > static_cast<std::size_t>(kTilesPerChunk)) {
            throw FormatError(in.what() + ": lane '" + def.name + "' malformed run at cell " +
                              std::to_string(pos));
        }
        std::fill_n(lane.begin() + static_cast<std::ptrdiff_t>(pos), len, value);
        pos += len;
    }
    if (pos != static_cast<std::size_t>(kTilesPerChunk)) {
        throw FormatError(in.what() + ": lane '" + def.name + "' runs cover " +
                          std::to_string(pos) + " of " + std::to_string(kTilesPerChunk) + " cells");
    }
}

void ChunkCodec::decode(ByteReader& in, const ChunkView& target,
                        std::vector<DecodedOverlayCell>& overlaysOut) const {
    const std::uint8_t version = in.u8();
    if (version != kCodecVersion) {
        throw FormatError(in.what() + ": chunk codec version " + std::to_string(version) +
                          " cannot be migrated by this build (supports " +
                          std::to_string(kCodecVersion) + ")");
    }
    const std::size_t laneCount = in.u8();
    if (laneCount != laneOrder_.size()) {
        throw FormatError(in.what() + ": lane-set mismatch: stream has " +
                          std::to_string(laneCount) + " lanes, world has " +
                          std::to_string(laneOrder_.size()));
    }
    for (const LaneDef& def : laneOrder_) {
        const int width = in.u8();
        if (width != def.bytesPerTile) {
            throw FormatError(in.what() + ": lane '" + def.name + "' width mismatch: stream " +
                              std::to_string(width) + ", world " +
                              std::to_string(def.bytesPerTile));
        }
        if (width == 2) {
            decodeShortLane(in, target.shortLane(def.index), def);
        } else {
            decodeByteLane(in, target.byteLane(def.index), def);
        }
    }

    // Overlay blocks. Every OverlayId is emitted by the encoder even with zero
    // cells, so a well-formed frame ends with one block per declared overlay.
    // Ordinals must strictly ascend and must be known: there is no forward
    // compatibility here by design, because an unknown overlay would mean
    // silently dropping state the world hash depends on.
    const std::size_t overlayCount = in.u8();
    int prevOrdinal = -1;
    for (std::size_t k = 0; k < overlayCount; ++k) {
        const int ordinal = in.u8();
        if (ordinal <= prevOrdinal || static_cast<std::size_t>(ordinal) >= kOverlayCount) {
            throw FormatError(in.what() + ": unknown or out-of-order overlay ordinal " +
                              std::to_string(ordinal));
        }
        const std::size_t cells = in.u16();
        if (cells > static_cast<std::size_t>(kTilesPerChunk)) {
            throw FormatError(in.what() + ": overlay " +
                              std::string(overlayName(static_cast<OverlayId>(ordinal))) +
                              " cell count out of range: " + std::to_string(cells));
        }
        int prevLocalIdx = -1;
        for (std::size_t i = 0; i < cells; ++i) {
            const int localIdx = in.u16();
            const std::uint16_t value = in.u16();
            if (localIdx <= prevLocalIdx || localIdx >= kTilesPerChunk) {
                throw FormatError(in.what() + ": overlay " +
                                  std::string(overlayName(static_cast<OverlayId>(ordinal))) +
                                  " cells not ascending localIdx: " + std::to_string(localIdx) +
                                  " after " + std::to_string(prevLocalIdx));
            }
            overlaysOut.push_back(DecodedOverlayCell{static_cast<std::uint8_t>(ordinal),
                                                     static_cast<std::uint16_t>(localIdx), value});
            prevLocalIdx = localIdx;
        }
        prevOrdinal = ordinal;
    }
}

}  // namespace granadad::content
