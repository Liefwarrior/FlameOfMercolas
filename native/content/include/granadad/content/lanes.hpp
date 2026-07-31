#pragma once

// The dense lane set and the small enums stored inside it.
//
// Seven core lanes are registered by every world, in this exact order, before
// any extension lane. Registry order IS lane order for chunk layout, hashing
// and saves — so the layout is a dense vector indexed by ordinal and never a
// map keyed by name. std::unordered_map has exactly the iteration-order hazard
// java.util.HashMap does, and neither belongs anywhere near this path.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/format_error.hpp"

namespace granadad::content {

// ---------------------------------------------------------------------------
// FORM lane
// ---------------------------------------------------------------------------

/// The structural form of a tile's material. Ordinals are save-format-stable:
/// append only, never reorder.
enum class TileForm : std::uint8_t {
    /// Immutable nothing: the world border ring. Never paintable.
    Void = 0,
    /// Air. The MATERIAL lane is ignored here.
    Open = 1,
    /// Material occupies only the walkable floor slab.
    Floor = 2,
    /// Material fills the tile solid.
    Wall = 3,
    /// Walkable slope to the z-level above.
    Ramp = 4,
    /// Walkable stair to the z-level above.
    Stair = 5,
};

inline constexpr std::size_t kTileFormCount = 6;

[[nodiscard]] std::string_view tileFormName(TileForm form) noexcept;

// ---------------------------------------------------------------------------
// FLAGS lane
// ---------------------------------------------------------------------------

namespace flag_bits {
/// Derived from material + form; maintained by the writer.
inline constexpr std::uint8_t kBlocksMove = 0x01;
/// Derived from material opacity + form; maintained by the writer.
inline constexpr std::uint8_t kBlocksLight = 0x02;
/// Owned by the fire system.
inline constexpr std::uint8_t kOnFire = 0x04;
/// Every bit currently defined; bits 3..7 are reserved.
inline constexpr std::uint8_t kDefinedMask = 0x07;
}  // namespace flag_bits

// ---------------------------------------------------------------------------
// FLUID lane (2 bytes, bit-packed)
// ---------------------------------------------------------------------------

namespace fluid_bits {
/// Fluid depth 0..7; 0 means no fluid.
[[nodiscard]] constexpr int depth(std::uint16_t packed) noexcept {
    return packed & 0x7;
}
/// Fluid registry index 0..7 (fluids are sorted by string id).
[[nodiscard]] constexpr int fluidId(std::uint16_t packed) noexcept {
    return (packed >> 3) & 0x7;
}
/// Owned by the fluid system; the Tiled importer leaves it clear.
[[nodiscard]] constexpr bool settled(std::uint16_t packed) noexcept {
    return (packed & 0x40) != 0;
}
}  // namespace fluid_bits

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

/// Sparse 16-bit overlays. Ordinals are save-format-stable: append only.
enum class OverlayId : std::uint8_t {
    /// Chromatis/lightstone stored charge, in charge units (cu).
    Charge = 0,
};

inline constexpr std::size_t kOverlayCount = 1;

[[nodiscard]] std::string_view overlayName(OverlayId overlay) noexcept;

// ---------------------------------------------------------------------------
// Lane registry
// ---------------------------------------------------------------------------

namespace lane_names {
inline constexpr std::string_view kMaterial = "material";
inline constexpr std::string_view kForm = "form";
inline constexpr std::string_view kFlags = "flags";
inline constexpr std::string_view kTemperature = "temperature";
inline constexpr std::string_view kFluid = "fluid";
inline constexpr std::string_view kLight = "light";
inline constexpr std::string_view kOpacity = "opacity";
}  // namespace lane_names

/// Stable registry indices of the core lanes.
inline constexpr std::size_t kMaterialLane = 0;
inline constexpr std::size_t kFormLane = 1;
inline constexpr std::size_t kFlagsLane = 2;
inline constexpr std::size_t kTemperatureLane = 3;
inline constexpr std::size_t kFluidLane = 4;
inline constexpr std::size_t kLightLane = 5;
inline constexpr std::size_t kOpacityLane = 6;

/// Lanes every world registers before any extension lane.
inline constexpr std::size_t kCoreLaneCount = 7;

/// Maximum lanes a chunk frame can carry — the count is a u8 in the codec.
inline constexpr std::size_t kMaxLaneCount = 255;

/// One registered dense lane.
struct LaneDef {
    /// Registration ordinal, dense from 0.
    std::size_t index = 0;
    /// Canonical lower-case lane name.
    std::string name;
    /// Cell width: 1 (byte lane) or 2 (short lane).
    int bytesPerTile = 1;
};

/// A world's lane set in registry order. Dense vector, never a map.
class LaneLayout {
public:
    LaneLayout() = default;

    /// The seven core lanes in canonical order.
    [[nodiscard]] static LaneLayout core();

    /// Appends a lane. Throws FormatError on an illegal width or an empty name.
    void registerLane(std::string name, int bytesPerTile);

    [[nodiscard]] std::size_t count() const noexcept { return lanes_.size(); }
    [[nodiscard]] const LaneDef& byIndex(std::size_t index) const;

    /// Linear scan — deterministic and, at seven entries, faster than hashing.
    /// Returns nullptr when absent.
    [[nodiscard]] const LaneDef* find(std::string_view name) const noexcept;

    [[nodiscard]] const std::vector<LaneDef>& all() const noexcept { return lanes_; }

private:
    std::vector<LaneDef> lanes_;
};

}  // namespace granadad::content
