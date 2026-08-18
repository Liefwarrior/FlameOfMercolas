#pragma once

// THE WARD MAP -- a top-down district map the player presses M to see.
//
// THE OWNER'S ASK, VERBATIM: "It's too difficult to locate places like the
// mission, let's give the player a map that they can press M to see." The
// whole point is NAMES ON GROUND: the district's authored sign table
// (docks_signs_generated.hpp -- 40 doors and 43 way signs, every one a real
// name a mapper wrote) drawn over a software-rendered plan of the tiles the
// first-person renderer already reads. NO discovery gating and NO fog of war,
// deliberately: the ward is the player's home turf and the locals know it --
// everything signed is shown.
//
// THIS IS NOT THE TILED MENU'S CHART TILE. Session::mapRows() (the Menu's
// top-centre tile) is the INVESTIGATION's map -- known ground, open leads,
// who will talk -- and it stays exactly as #82 built it. This page is
// NAVIGATION: where the streets run, where the doors are, where you stand and
// which way you face. The old "the text map is settled" note is superseded
// FOR NAVIGATION ONLY by the owner's direct ask above; the Chart tile's own
// job is untouched.
//
// PURE RENDER-LAYER READS. Everything here is a const walk over TileQuery,
// the atlas and the body's already-public position -- no sim state moves, no
// hash can move, and the whole page is drawable headless, which is how its
// declutter and palette rules get unit tests instead of adjectives.
//
// Floats are legal in this file. Nothing in it may be read by the simulation.

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// what one tile of the plan is
// ---------------------------------------------------------------------------

/// How one (x, y) column reads on the plan at the player's own band. The
/// z-band emphasis rule the design settled for v1: the player's OWN band is
/// what the plan is a plan OF, one band down still draws (dimmer -- the quay
/// seen from the mid-slope), and the harbour's water is always shown, because
/// a dock district's map with no waterline is not a map of a dock district.
enum class MapCell : std::uint8_t {
    /// Nothing authored anywhere useful -- the true-black field.
    Void = 0,
    /// Solid at the player's band: masonry, drawn dark in the material's own
    /// tone so warehouse brick and granite frontage read apart.
    Wall,
    /// Walkable at the player's band -- the ground underfoot, drawn light.
    Floor,
    /// Walkable one band DOWN from the player -- lower ground, the same
    /// material tone dimmed, so a mid-slope reader still sees the quay.
    LowerFloor,
    /// The harbour (or any water column): FLUID-lane depth at the surface
    /// band, always shown whatever band the player stands on.
    Water,
};

/// Classifies one column for a plan drawn at `band`. Pure; the whole page's
/// geometry honesty rests on this being a plain read of the same TileQuery
/// answers movement already lives by, so it is public and tested against the
/// real baked Docks.
[[nodiscard]] MapCell mapCellAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                std::int32_t band) noexcept;

// ---------------------------------------------------------------------------
// the palette -- derived from the atlas, never invented beside it
// ---------------------------------------------------------------------------

/// Per-material plan tones, DERIVED from the shipped atlas's own tile
/// averages -- walls from the material's side face darkened, floors from its
/// floor face lifted -- so the plan reads as THIS game's own district and a
/// future art-pack swap recolours the map by the act of swapping. Water and
/// void come from the atlas's own water tile and voidColour().
struct MapPalette {
    /// Indexed by MATERIAL-lane id, same as the atlas.
    std::vector<Rgb> wall;
    std::vector<Rgb> floor;
    Rgb water{0.16F, 0.28F, 0.38F};
    Rgb voidTone{0.02F, 0.02F, 0.03F};

    [[nodiscard]] static MapPalette fromAtlas(const TileAtlas& atlas);

    [[nodiscard]] Rgb wallTone(std::uint16_t material) const noexcept;
    [[nodiscard]] Rgb floorTone(std::uint16_t material) const noexcept;
};

// ---------------------------------------------------------------------------
// frame geometry, and the labels
// ---------------------------------------------------------------------------

/// The authored extent of a world, in world tiles -- the VOID border ring
/// cropped away so the plan spends its pixels on the district. Scanned once
/// off the column contents; Session caches it at boot.
struct MapBounds {
    std::int32_t minX = 0;
    std::int32_t minY = 0;
    std::int32_t maxX = 0;  // inclusive
    std::int32_t maxY = 0;  // inclusive
};

[[nodiscard]] MapBounds mapContentBounds(const sim::TileQuery& tiles);

/// World tiles -> map pixels. Integer pixels-per-tile (the chunkiness is the
/// art direction), the plan centred in the frame.
struct MapFrame {
    int scale = 2;
    int originX = 0;  ///< screen px of bounds.minX's left edge
    int originY = 0;
    MapBounds bounds{};

    [[nodiscard]] int pxOfX(float worldX) const noexcept {
        return originX + static_cast<int>((worldX - static_cast<float>(bounds.minX)) *
                                          static_cast<float>(scale));
    }
    [[nodiscard]] int pxOfY(float worldY) const noexcept {
        return originY + static_cast<int>((worldY - static_cast<float>(bounds.minY)) *
                                          static_cast<float>(scale));
    }
};

[[nodiscard]] MapFrame mapFrameFor(int width, int height, const MapBounds& bounds) noexcept;

/// One label the declutter pass actually placed: text, its top-left pixel,
/// and whether it names a way (a street) or a door.
struct PlacedMapLabel {
    std::string text;
    int x = 0;
    int y = 0;
    bool way = false;
    /// The sign's own anchor, in map pixels -- where the door dot goes.
    int anchorPx = 0;
    int anchorPy = 0;
};

/// THE DECLUTTER RULE, deterministic and testable on its own:
///
///   * way names first -- one label per distinct name (a street signed six
///     times along its reach gets ONE label, at the sign nearest the
///     cluster's own centre so it does not wander as the player walks),
///     sorted by name so the placement order cannot drift between runs;
///   * door names second, NEAREST TO THE PLAYER FIRST (ties by sign id) --
///     the door you are hunting is almost always the one near you;
///   * each label tries four positions around its anchor (above, below,
///     right, left) and takes the first that fits inside the map rect
///     without overlapping anything already placed; a label with no clean
///     position is DROPPED, never overprinted -- 83 signs into 640x360 all
///     at once is sludge, and sludge is worse than fewer names.
///
/// `textScale` is the 4x6 font scale labels will be drawn at (the caller
/// passes hudMinorScale-derived 1 at 640x360).
[[nodiscard]] std::vector<PlacedMapLabel> placeMapLabels(const MapFrame& frame, float playerX,
                                                         float playerY, int textScale);

// ---------------------------------------------------------------------------
// the page
// ---------------------------------------------------------------------------

/// Everything drawDistrictMap needs, gathered by Session::drawFrame.
struct DistrictMapState {
    const sim::TileQuery* tiles = nullptr;
    const MapPalette* palette = nullptr;
    MapBounds bounds{};
    /// Player position in world-tile units (Q8 / 256), and facing.
    float playerX = 0.0F;
    float playerY = 0.0F;
    std::int32_t band = 0;
    std::int32_t yawBam = 0;
    /// The title row -- Session::placeLabel(), the same words the HUD uses.
    std::string title;
    /// A bouncer's warning routed onto this page while it is up, the same
    /// contract the tiled Menu's journal tile carries. Empty draws nothing.
    std::string alert;
    /// 0 (closed) .. 1 (open) -- the page's own EasedToggle, per the settled
    /// UI convention (DECISIONS.md rule 1).
    float openAmount = 1.0F;
};

/// Draws the whole page over a rendered frame: the plan, the water, the
/// labels (drawTextPlate-backed, per the settled convention -- rule 2), the
/// door dots, the player's own facing wedge, title and key hint.
void drawDistrictMap(Framebuffer& target, const DistrictMapState& state);

}  // namespace granadad::render
