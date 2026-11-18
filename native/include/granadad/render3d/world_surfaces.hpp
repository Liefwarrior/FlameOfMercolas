#pragma once

// THE WORLD'S SURFACES -- the procedural material set of OBLIVION-PLAN section 7,
// item 1, and the atlas the chunk mesher textures the district with.
//
// WHAT THIS REPLACES. The district used to wear the owner's 16x16 pixel tiles,
// ONE TILE STRETCHED OVER A WHOLE FACE: a three-metre storey was a single
// sixteen-pixel image, which is the "pixel soup" the plan names. A surface here
// is a tileable image of TWO METRES square (kSurfacePx texels), and the mesher
// gives every vertex WORLD-METRE UVs, so a texture repeats every two metres on
// a wall, a floor and a roof alike, and a wall is a run of courses and not a
// smear. The shader tiles inside the atlas cell (rl/shaders.cpp); the cell is
// ringed with a wrapped gutter (kSurfaceGutter) so filtering never reads the
// neighbour cell.
//
// WHERE THE COLOURS COME FROM. The authored material raws carry no colour, so
// the tone of each surface is the material's own row of the renderer's tone
// table (render::materialTone, atlas.cpp's kFallbackTones): granite is the cool
// grey, brick the red, oak the brown, thatch the straw. A surface is a PATTERN
// (ashlar, brick, cobble, plank, thatch, shingle...) in a material's tone, so
// the look follows the owner's canon of what each surface is.
//
// HOW THEY ARE MADE. Every texel is a pure function of its coordinates through
// an INTEGER value noise and an integer Voronoi, so the same bytes come out of
// GCC, mingw and every optimisation level: tests/test_world_surfaces.cpp pins
// that the PNGs committed under content/art/oblivion/materials are exactly what
// this file generates, and the generator tool (src/tools/gen_world_materials.cpp)
// is how they are regenerated. Nothing at run time reads those PNGs: the game
// generates the same texels in a few milliseconds, so a checkout with no
// content tree draws the same district.
//
// Floats are legal in this lane but this file uses none: the generator is
// integers all the way down.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render3d/scene.hpp"

namespace granadad::render3d {

/// Texels along one side of a surface image, which covers kSurfaceMetres.
inline constexpr int kSurfacePx = 128;
/// A surface repeats every this many metres (tiles) in both directions.
inline constexpr int kSurfaceMetres = 2;
/// Wrapped texels around a surface image in the atlas. Eight is enough for
/// the shader's mip clamp (lod 3).
inline constexpr int kSurfaceGutter = 8;
inline constexpr int kSurfaceStride = kSurfacePx + 2 * kSurfaceGutter;
/// Atlas cells across and down.
inline constexpr int kSurfaceColumns = 5;

/// One surface. Order is the atlas order and is part of the scene hash; never
/// reorder, append.
enum class Surface : std::uint8_t {
    White = 0,       // flat: the far LOD and anything with no surface of its own
    GraniteAshlar,   // coursed granite blocks
    GraniteFlag,     // irregular granite flagstones (interior floors)
    Cobble,          // harbour setts, outdoors
    BrickWall,       // running-bond brick
    BrickFacade,     // brick, a shade richer
    PlankDeck,       // floor boards (a deck, a taproom floor)
    PlankWall,       // vertical boards (a timber wall)
    WetWood,         // tarred, weathered timber: hulls, piles, the quay's woodwork
    Plaster,         // lime plaster: the inside of a masonry room
    Concrete,        // reman concrete, panelled
    RemanFacade,     // reman concrete, lighter
    Thatch,          // straw
    SlateRoof,       // slate shingles
    ClayRoof,        // clay tiles
    Earth,           // packed earth
    Ash,             // ash and cinders
    Iron,            // riveted iron plate
    Cloth,           // woven cloth
    Beam,            // dark oak timber: ceiling beams, jambs, trim
    Count
};

inline constexpr std::size_t kSurfaceCount = static_cast<std::size_t>(Surface::Count);
static_assert(kSurfaceCount <= static_cast<std::size_t>(kSurfaceColumns) * 4U,
              "the atlas is five columns by four rows");
inline constexpr int kSurfaceRows =
    static_cast<int>((kSurfaceCount + kSurfaceColumns - 1) / kSurfaceColumns);

[[nodiscard]] std::string_view surfaceName(Surface surface) noexcept;

/// One surface as kSurfacePx x kSurfacePx RGBA8, top row first, alpha 255,
/// tileable on both axes. Pure.
[[nodiscard]] std::vector<std::uint8_t> generateSurface(Surface surface);

/// The mean colour of a surface, 0..1 per channel. The far LOD paints a flat
/// face in this (times the light), so a distant street holds its colour
/// instead of shimmering into noise.
struct SurfaceTone {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

/// THE SURFACE ATLAS: every surface laid out on one texture with its gutter,
/// plus the lookups the mesher needs.
class SurfaceAtlas {
public:
    [[nodiscard]] static SurfaceAtlas build();

    /// The texture, ready for SceneDescription::putTexture. Marked as a tiling
    /// atlas (TextureData::tileU/tileV) and mipmapped.
    [[nodiscard]] const TextureData& texture() const noexcept { return texture_; }

    /// Normalised origin of a surface's image inside the atlas (the first
    /// texel of the cell proper, not of its gutter).
    [[nodiscard]] std::array<float, 2> origin(Surface surface) const noexcept;
    [[nodiscard]] SurfaceTone tone(Surface surface) const noexcept;

private:
    TextureData texture_;
    std::array<SurfaceTone, kSurfaceCount> tones_{};
};

/// What a face of a tile is dressed in.
enum class Role : std::uint8_t {
    Wall,    // the vertical side of a wall cell (outdoors)
    Inside,  // the same, seen from a roofed room
    Floor,   // the top of a floor, ramp or stair, outdoors
    Indoor,  // the top of a floor under a roof
    Cap,     // the top of a wall (a parapet, a roof-less wall head)
    Slab,    // the edge and underside of a floor slab
};

/// The surface a material wears in a role, or Surface::White when the material
/// has no surface of its own (the mesher then paints the face flat in the
/// material's tone, so nothing is ever drawn in a stand-in pattern).
[[nodiscard]] Surface surfaceFor(std::uint16_t material, Role role) noexcept;

/// The flat tone of a material (the renderer's tone table), 0..1.
[[nodiscard]] SurfaceTone materialToneOf(std::uint16_t material) noexcept;

}  // namespace granadad::render3d
