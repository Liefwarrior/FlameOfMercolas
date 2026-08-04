#pragma once

// Read-only questions about the tile grid, asked the way movement and sight
// need to ask them.
//
// This is the seam between the format reader (which knows how to decode lanes)
// and everything that has to WALK on the result. content::World hands out lane
// spans by global tile index; nothing there answers "can a body stand here",
// "does this block a ray", "is there headroom". Those three questions are asked
// from the movement code, the collision code, the renderer and — from S2 on —
// actor pathing, and they must be answered in exactly one place or they will
// drift apart.
//
// INTEGER ONLY. This is simulation state math: it decides where a body is, and
// the answer is folded into the world hash the moment the player is hashed. No
// float, no double, not even in a helper.
//
// The walkability rule is ported verbatim from the Java's
// sim/world/Walkability.java, including the part that surprises people: OPEN
// does NOT mean walkable. OPEN means "nothing was authored in this cell at all"
// — no floor, no wall, air. The docks' harbour is OPEN above the water line and
// the Drowned Hold is OPEN inside; treating it as ground would drop the player
// through the map in both. FLOOR, RAMP and STAIR are the walkable forms, and a
// walkable form still blocks when the FLUID lane says the water is at least
// knee-deep (depth >= 4 of 7).

#include <cstdint>

#include "granadad/content/coords.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// Q8 sub-tile positions
// ---------------------------------------------------------------------------
//
// A position is `(tile << 8) | sub`: 256 steps across a tile, integral and
// reproducible, per the 2026-07-31 ruling in
// docs/design/COMBAT-FEEL-REFERENCE.md section 2. The whole point is that the
// player stays inside the simulation, so this type is the simulation's, not the
// renderer's.

inline constexpr int kSubBits = 8;
inline constexpr std::int32_t kSubOne = 1 << kSubBits;
inline constexpr std::int32_t kSubMask = kSubOne - 1;
inline constexpr std::int32_t kSubHalf = kSubOne / 2;

/// The tile a Q8 coordinate lies in. Arithmetic shift, so this floors for
/// negatives rather than truncating toward zero — tile -1 is the tile at
/// -256..-1, never tile 0.
[[nodiscard]] constexpr std::int32_t q8_tile(std::int32_t q8) noexcept {
    return q8 >> kSubBits;
}

/// The offset within the tile, always in [0, 256).
[[nodiscard]] constexpr std::int32_t q8_sub(std::int32_t q8) noexcept {
    return q8 & kSubMask;
}

[[nodiscard]] constexpr std::int32_t q8_of_tile(std::int32_t tile) noexcept {
    return tile << kSubBits;
}

/// The exact centre of a tile — the only sane place to put a body that was
/// given tile coordinates.
[[nodiscard]] constexpr std::int32_t q8_tile_centre(std::int32_t tile) noexcept {
    return (tile << kSubBits) | kSubHalf;
}

// ---------------------------------------------------------------------------
// the queries
// ---------------------------------------------------------------------------

/// FLUID-lane depth at or above which a walkable tile stops being walkable.
/// Java: Walkability.BLOCKING_FLUID_DEPTH.
inline constexpr int kBlockingFluidDepth = 4;

/// Read-only movement/sight view of a loaded world. Borrows; must not outlive
/// the world it was built from.
class TileQuery {
public:
    explicit TileQuery(const content::World& world) noexcept;

    [[nodiscard]] std::int32_t sizeX() const noexcept { return sizeX_; }
    [[nodiscard]] std::int32_t sizeY() const noexcept { return sizeY_; }
    [[nodiscard]] std::int32_t sizeZ() const noexcept { return sizeZ_; }

    [[nodiscard]] const content::World& world() const noexcept { return *world_; }

    [[nodiscard]] constexpr bool inBounds(std::int32_t x, std::int32_t y,
                                          std::int32_t z) const noexcept {
        return x >= 0 && y >= 0 && z >= 0 && x < sizeX_ && y < sizeY_ && z < sizeZ_;
    }

    /// The global tile index of a cell. Callers must have checked inBounds.
    [[nodiscard]] std::size_t index(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// Out of bounds reads as VOID, which blocks everything. The world already
    /// carries a one-chunk VOID border, so this only fires for a genuinely
    /// out-of-world query and answers it the same way the border would.
    [[nodiscard]] content::TileForm form(std::int32_t x, std::int32_t y,
                                         std::int32_t z) const noexcept;

    [[nodiscard]] std::uint16_t material(std::int32_t x, std::int32_t y,
                                         std::int32_t z) const noexcept;

    /// FLUID-lane depth, 0..7. Out of bounds is 0.
    [[nodiscard]] int fluidDepth(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// Fills the cell solid: WALL or VOID. Blocks bodies and blocks sight.
    [[nodiscard]] bool solid(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// A walkable FORM whose water is not too deep. Says nothing about headroom.
    [[nodiscard]] bool walkable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// RAMP or STAIR — the two forms that authorise a one-level step up.
    [[nodiscard]] bool climbable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// Whether a body standing on (x,y,z) has somewhere to put its head.
    ///
    /// CORRECTED IN S2, and the correction is why the district's buildings can
    /// be walked into at all.
    ///
    /// S1 refused any FILLED cell at z+1 while allowing a FLOOR there. Read
    /// against the geometry the renderer draws from the same lanes, that is
    /// backwards:
    ///
    ///     FLOOR at z+1 is the slab hanging from the TOP of that cell --
    ///                  [z+1-slab, z+1]. A low ceiling, and the tightest case
    ///                  there is. S1 allowed it.
    ///     WALL  at z+1 fills the cell from its own floor upward -- [z+1, z+2].
    ///                  That is MORE clearance than the floor slab, not less.
    ///                  S1 refused it.
    ///
    /// A WALL one level up is what sits over every doorway in a two-storey
    /// building, because the storey above has an exterior wall and the door is
    /// a gap in the storey below. Under the S1 rule the door tiles of the
    /// Gilded Gull, the Bilge, the Lantern Room and every other multi-storey
    /// site in the Docks were not standable, so nothing -- player or actor --
    /// could get through them. The interiors were authored, baked, rendered,
    /// and sealed.
    ///
    /// So the only form that refuses is VOID: the world's own border ring,
    /// where there is no cell to have headroom in. The pinned reachability
    /// counts in docks.hpp moved as a result and were all re-derived from the
    /// baked bytes; test_tile_query.cpp re-derives them on every build.
    ///
    /// S3, AND THIS IS THE HONEST PART. The S2 review mutated this function to
    /// `return true` and the whole 228-test gate stayed green. It was right to:
    /// the shipped Docks contains NO cell that is walkable with VOID directly
    /// above it, so over that one map this rule and `true` are the same
    /// function, and no count re-derived from those bytes can tell them apart.
    /// That is a fact about the map, not a licence to delete the rule -- the
    /// next baked world (an interior, a sewer, anything authored right up to
    /// the border) will have such cells, and a body standing in the world's
    /// own border ring is exactly the bug this refuses.
    ///
    /// So it is proved on a world built to contain the case:
    /// test_tile_query.cpp's "headroom refuses the world's own ceiling" stands
    /// a FLOOR under a VOID cell in a purpose-built World and requires
    /// standable() to say no. That case goes red on `return true`. The same
    /// case ALSO asserts, against the real Docks, that the district has no such
    /// cell -- so the day the map gains one, the comment above stops being true
    /// out loud rather than quietly.
    [[nodiscard]] bool headroom(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// walkable() and headroom() together — "a body can be here".
    [[nodiscard]] bool standable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// The band a body already at `fromZ` would end up in after moving onto
    /// column (x,y), or `kNoBand` if that column cannot be entered.
    ///
    /// The rule, and it is the whole of the vertical movement model:
    ///   * same level, if standable                       — flat ground
    ///   * one level DOWN, if standable                   — a step off a kerb
    ///   * one level UP, if standable AND either the tile
    ///     being left or the tile being entered is a RAMP
    ///     or a STAIR                                     — the way up
    ///
    /// The last clause is what stops a body walking up the side of a warehouse
    /// and what makes the docks' three walk bands (quayside z=19, mid-slope
    /// z=20, upper z=21) reachable only by the ramps and stairs the map author
    /// actually placed.
    ///
    /// Preference order is same level, then down, then up: an ordinary step
    /// never becomes a climb because a ramp happened to be beside it.
    [[nodiscard]] std::int32_t stepBand(std::int32_t fromX, std::int32_t fromY,
                                        std::int32_t fromZ, std::int32_t x,
                                        std::int32_t y) const noexcept;

    /// stepBand's "no such band" answer. Deliberately not -1: -1 is a legal
    /// int32 z that inBounds already rejects, and a caller comparing against
    /// the wrong sentinel should not compile into a subtle out-of-world walk.
    static constexpr std::int32_t kNoBand = INT32_MIN;

private:
    const content::World* world_;
    std::int32_t sizeX_ = 0;
    std::int32_t sizeY_ = 0;
    std::int32_t sizeZ_ = 0;
};

}  // namespace granadad::sim
