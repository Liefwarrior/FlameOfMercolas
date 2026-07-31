#pragma once

// The in-memory world: dense lanes plus sparse overlays.
//
// Storage is one flat array per lane, addressed by GLOBAL TILE INDEX
//
//     tileIndex = chunkIndex * 8192 + localIdx
//
// so ascending tileIndex is ascending chunkIndex then ascending localIdx — the
// canonical order, by construction rather than by convention. Lanes are indexed
// by registry ordinal; a lane is either a byte lane or a short lane and exactly
// one of the two backing vectors is populated for it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "granadad/content/coords.hpp"
#include "granadad/content/lanes.hpp"

namespace granadad::content {

/// One overlay cell: a global tile index and its unsigned 16-bit value.
struct OverlayEntry {
    std::uint32_t tileIndex = 0;
    std::uint16_t value = 0;
};

class ChunkView;

/// A loaded world. Integer state only — no floats anywhere in here.
class World {
public:
    World(Coords coords, LaneLayout lanes);

    [[nodiscard]] const Coords& coords() const noexcept { return coords_; }
    [[nodiscard]] const LaneLayout& lanes() const noexcept { return lanes_; }

    [[nodiscard]] std::size_t chunkCount() const noexcept {
        return static_cast<std::size_t>(coords_.chunkCount());
    }

    /// chunkCount * 8192.
    [[nodiscard]] std::size_t tileCount() const noexcept { return tileCount_; }

    /// The global tile index of a chunk-local cell.
    [[nodiscard]] static constexpr std::size_t tileIndex(std::size_t chunkIndex,
                                                         std::size_t localIdx) noexcept {
        return chunkIndex * static_cast<std::size_t>(kTilesPerChunk) + localIdx;
    }

    // --- whole-world lane access ------------------------------------------

    [[nodiscard]] std::span<std::uint8_t> byteLane(std::size_t laneIndex);
    [[nodiscard]] std::span<const std::uint8_t> byteLane(std::size_t laneIndex) const;
    [[nodiscard]] std::span<std::uint16_t> shortLane(std::size_t laneIndex);
    [[nodiscard]] std::span<const std::uint16_t> shortLane(std::size_t laneIndex) const;

    /// A writable view of one chunk's slice of every lane.
    [[nodiscard]] ChunkView chunk(std::size_t chunkIndex);

    // --- overlays ---------------------------------------------------------

    /// Appends an overlay cell. Cells must arrive in ascending tileIndex order,
    /// which the load path guarantees (ascending chunk, ascending localIdx);
    /// a violation is a FormatError because it means the save was malformed.
    void putOverlay(OverlayId overlay, std::uint32_t tileIndex, std::uint16_t value);

    [[nodiscard]] std::span<const OverlayEntry> overlay(OverlayId overlay) const;

    // --- typed accessors by global tile index ------------------------------

    [[nodiscard]] std::uint16_t material(std::size_t tile) const {
        return shortLane(kMaterialLane)[tile];
    }
    [[nodiscard]] TileForm form(std::size_t tile) const {
        return static_cast<TileForm>(byteLane(kFormLane)[tile]);
    }
    [[nodiscard]] std::uint8_t flags(std::size_t tile) const { return byteLane(kFlagsLane)[tile]; }
    /// Unsigned deci-Kelvin (K x 10).
    [[nodiscard]] std::uint16_t temperature(std::size_t tile) const {
        return shortLane(kTemperatureLane)[tile];
    }
    [[nodiscard]] std::uint16_t fluid(std::size_t tile) const { return shortLane(kFluidLane)[tile]; }
    [[nodiscard]] std::uint16_t light(std::size_t tile) const { return shortLane(kLightLane)[tile]; }
    [[nodiscard]] std::uint8_t opacity(std::size_t tile) const {
        return byteLane(kOpacityLane)[tile];
    }

private:
    Coords coords_;
    LaneLayout lanes_;
    std::size_t tileCount_ = 0;
    // Indexed by lane ordinal. For a given lane exactly one is non-empty.
    std::vector<std::vector<std::uint8_t>> byteLanes_;
    std::vector<std::vector<std::uint16_t>> shortLanes_;
    std::array<std::vector<OverlayEntry>, kOverlayCount> overlays_;
};

/// A writable window onto one chunk's 8192 cells of each lane. Cheap to copy;
/// borrows the world and must not outlive it.
class ChunkView {
public:
    ChunkView(World& world, std::size_t chunkIndex) noexcept
        : world_(&world), chunkIndex_(chunkIndex) {}

    [[nodiscard]] std::size_t chunkIndex() const noexcept { return chunkIndex_; }

    [[nodiscard]] std::span<std::uint8_t> byteLane(std::size_t laneIndex) const;
    [[nodiscard]] std::span<std::uint16_t> shortLane(std::size_t laneIndex) const;

    [[nodiscard]] World& world() const noexcept { return *world_; }

private:
    World* world_;
    std::size_t chunkIndex_;
};

}  // namespace granadad::content
