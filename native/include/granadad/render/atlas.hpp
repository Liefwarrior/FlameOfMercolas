#pragma once

// The tile art, read from the owner's own pack.
//
// content/art/custom is 16x16 ORIGINAL pixel art in the MERCOLAS-24 palette,
// with per-material `.face` (the side of a block), `.floor`, `.ramp`, `.stair`
// and `.top` regions, plus a `water` region and a `missing` region — exactly
// the vocabulary a first-person voxel renderer needs, already authored. The
// index is content/art/custom/art-mapping.json; the sheet is tiles.png.
//
// Nothing here writes to content/. The pack is read once at boot and copied
// into a flat texel store.
//
// WHEN THE PACK IS NOT THERE. A checkout without content/art still has to run —
// the docker gate compiles without the art tree and a stripped build should
// boot rather than die over a texture. So the atlas falls back to PROCEDURAL
// tiles generated from a per-material base colour with a deterministic integer
// hash for grain. They are ugly and they are legible, which is the correct
// order of priorities for a fallback.
//
// MATERIAL IDS. The MATERIAL lane stores the registry id, which sim-core
// assigns by sorting the raws' string ids and numbering from zero. That mapping
// is a fact about content/raws, so the ordered name table lives here as a
// literal and test_atlas.cpp pins it against the ids the TROJSAV tests already
// read out of the shipped bytes.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

/// Which face of a tile is being drawn.
enum class FaceKind : std::uint8_t {
    /// The vertical side of a block — `<material>.face`.
    Side = 0,
    /// The walkable top of a FLOOR — `<material>.floor`.
    FloorTop = 1,
    /// The top of a RAMP — `<material>.ramp`.
    RampTop = 2,
    /// The top of a STAIR — `<material>.stair`.
    StairTop = 3,
    /// The top of a WALL — `<material>.top`.
    BlockTop = 4,
    /// The surface of standing water — the `water` region.
    Water = 5,
};

inline constexpr std::size_t kFaceKindCount = 6;

/// The material registry's ordered string ids, index == MATERIAL-lane value.
/// 21 authored raws under content/raws/materials plus the one material the
/// getilia-soak treatment mints, sorted as strings.
[[nodiscard]] std::span<const std::string_view> materialIds() noexcept;

/// The atlas. Immutable once built.
class TileAtlas {
public:
    /// Side length of one tile in texels.
    static constexpr int kTilePx = 16;
    static constexpr std::size_t kTileTexels =
        static_cast<std::size_t>(kTilePx) * static_cast<std::size_t>(kTilePx);

    /// Loads content/art/custom. Falls back to procedural tiles for anything
    /// the pack does not supply, and entirely if the pack is missing.
    [[nodiscard]] static TileAtlas load(const std::filesystem::path& contentDir);

    /// The procedural pack, with no files involved. Used by the fallback and
    /// by tests that must not depend on the art tree.
    [[nodiscard]] static TileAtlas procedural();

    /// Whether the authored sheet was actually found and decoded.
    [[nodiscard]] bool fromAuthoredArt() const noexcept { return fromAuthoredArt_; }

    /// How many distinct tile images are in the store.
    [[nodiscard]] std::size_t tileCount() const noexcept {
        return texels_.size() / kTileTexels;
    }

    /// Picks the tile image for a material/face at a world cell. `variantKey`
    /// selects between the pack's authored appearance variants and must be a
    /// pure function of the cell so a wall does not shimmer as you walk past.
    [[nodiscard]] std::size_t tileFor(std::uint16_t material, FaceKind face,
                                      std::uint32_t variantKey) const noexcept;

    /// One texel of a tile image, wrapping u and v into 0..15.
    [[nodiscard]] Rgb texel(std::size_t tile, int u, int v) const noexcept;

    /// Straight RGBA access, for tests and for sprite masks.
    [[nodiscard]] std::uint32_t texelRaw(std::size_t tile, int u, int v) const noexcept;

    /// The pack's authored light ramp: index 0..31, Q8 multiplier.
    [[nodiscard]] const std::vector<std::int32_t>& lightTintQ8() const noexcept {
        return lightTintQ8_;
    }

    /// The pack's authored colour for VOID.
    [[nodiscard]] Rgb voidColour() const noexcept { return voidColour_; }

    /// Per-depth alpha for water, 8 entries indexed by FLUID-lane depth.
    [[nodiscard]] const std::vector<std::int32_t>& waterDepthAlphaQ8() const noexcept {
        return waterDepthAlphaQ8_;
    }

    /// A flat average of a tile image — used to keep distant geometry from
    /// aliasing into noise, and by tests as a cheap fingerprint.
    [[nodiscard]] Rgb averageOf(std::size_t tile) const noexcept;

private:
    TileAtlas() = default;

    std::size_t appendProceduralTile(Rgb base, std::uint32_t seed, bool speckled);

    /// Fills tileAverages_ from texels_. Must run after every texel append is
    /// final for the atlas being built — averageOf() is O(1) off this cache,
    /// not a live per-call texel scan, so it has to be complete before the
    /// atlas is handed out.
    void buildAverageCache();

    std::vector<std::uint32_t> texels_;
    /// [material][face] -> the tile indices that may be used, in pack order.
    std::vector<std::vector<std::vector<std::size_t>>> byMaterialFace_;
    std::size_t missingTile_ = 0;
    std::vector<std::int32_t> lightTintQ8_;
    std::vector<std::int32_t> waterDepthAlphaQ8_;
    Rgb voidColour_{0.05F, 0.043F, 0.063F};
    bool fromAuthoredArt_ = false;
    /// Per-tile flat average, index == tile index. Precomputed by
    /// buildAverageCache() so averageOf() is a lookup, not a 256-texel loop,
    /// since the renderer now calls it live per minified horizontal face.
    std::vector<Rgb> tileAverages_;
};

}  // namespace granadad::render
