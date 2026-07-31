#pragma once

// Lane-wise RLE codec for one chunk's dense lanes plus its sparse overlays.
// Versioned independently of the container so lane layout can migrate in place.
//
// Frame layout v1, little-endian:
//
//   u8   codecVersion
//   u8   laneCount                       (must equal the world's lane count)
//   per lane, IN REGISTRY ORDER:
//       u8  bytesPerTile                 (must equal the world's lane width)
//       u8  mode
//       mode 0 (RLE): u16 runCount, then runCount x { u16 runLength, u8|u16 value }
//       mode 1 (RAW): 8192 x u8|u16 value, in localIdx order
//   u8   overlayCount
//   per overlay, ordinals STRICTLY ASCENDING:
//       u8  ordinal
//       u16 cellCount
//       cellCount x { u16 localIdx, u16 value }   localIdx STRICTLY ASCENDING
//
// RLE runs must be maximal and cover exactly 8192 cells; the decoder rejects a
// zero-length run, an overrun, and a final position short of 8192.
//
// Mode is a pure function of content on the encode side, which is what makes
// encoding deterministic:
//
//   short lane: rleBytes = 2 + runs*4, RLE iff < 16384  ->  RAW at runs >= 4096
//   byte  lane: rleBytes = 2 + runs*3, RLE iff <  8192  ->  RAW at runs >= 2730
//
// VERIFICATION GAP (M0): RAW mode is unexercised by the shipped corpus — 0 of
// 336 chunk frames across the three baked worlds use it, because the densest
// observed lane has 741 runs against a 2730 threshold. The decoder's RAW path
// here is therefore validated only against synthetic frames built in the tests,
// not against authored content.

#include <cstdint>
#include <vector>

#include "granadad/content/byte_reader.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"

namespace granadad::content {

inline constexpr std::uint8_t kCodecVersion = 1;
inline constexpr std::uint8_t kModeRle = 0;
inline constexpr std::uint8_t kModeRaw = 1;

/// One overlay cell as it comes off the wire, still chunk-local.
struct DecodedOverlayCell {
    std::uint8_t ordinal = 0;
    std::uint16_t localIdx = 0;
    std::uint16_t value = 0;
};

/// A codec bound to one world's lane layout. The layout must be dense ascending
/// registry order; anything else is a programming error and throws on
/// construction.
class ChunkCodec {
public:
    explicit ChunkCodec(const LaneLayout& lanes);

    /// Decodes one frame into `target`'s backing arrays and appends any overlay
    /// cells to `overlaysOut` in ascending localIdx order. `overlaysOut` is NOT
    /// cleared — the caller owns it, so a per-chunk loop can reuse one buffer.
    void decode(ByteReader& in, const ChunkView& target,
                std::vector<DecodedOverlayCell>& overlaysOut) const;

    [[nodiscard]] std::size_t laneCount() const noexcept { return laneOrder_.size(); }

private:
    void decodeByteLane(ByteReader& in, std::span<std::uint8_t> lane, const LaneDef& def) const;
    void decodeShortLane(ByteReader& in, std::span<std::uint16_t> lane, const LaneDef& def) const;

    std::vector<LaneDef> laneOrder_;
};

}  // namespace granadad::content
