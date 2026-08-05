#pragma once

// Getting one actor from one tile to another across a whole district.
//
// WHY THIS EXISTS BESIDE RegionPath, WHICH ALREADY WALKS TILES
//
// RegionPath is a breadth-first search over a BOX and it is the right answer
// for a room: the Gilded Gull and the street outside it is under a thousand
// cells, so an exhaustive frontier is cheap and exact. The ward is not a room.
// A night-roster guard's bunk is two hundred tiles and two bands from the
// corner it beats, and a breadth-first frontier over that box visits every
// walkable cell in the Docks before it finds the door -- 17,054 of them, per
// actor, per replan. At 692 actors that is not a slow path, it is a stopped
// simulation.
//
// So this is A*: the same fixed neighbour order and the same corner rule, with
// an octile heuristic in front of it and two independent bounds behind it. On a
// clear route it expands a few hundred cells instead of seventeen thousand.
//
// EVERY STRUCTURE IS A PLAIN ARRAY. No map, no set, no float, no RNG stream. A
// route is simulation state -- an actor walks it and the world hash folds where
// the actor ended up -- so it has to replay bit for bit on every toolchain.
//
// THE OPEN SET IS A BINARY MIN-HEAP OVER PARALLEL ARRAYS with a monotonic
// insertion-sequence tie-break. Ties on f-score are broken by WHICH WAS PUSHED
// FIRST and never by object identity or by address, because two cells with the
// same f are the common case in open ground and the order they come off the
// heap decides which of two equally good routes an actor walks.
//
// THE TWO BOUNDS, and they are independent on purpose:
//
//   the box      a bounding rectangle of the two endpoints padded by kPadding,
//                clamped to the world and capped at kMaxSpan on each axis. This
//                is what stops a search for a door on the far side of a wall
//                from walking the whole district looking for a way round.
//   maxNodes     a hard cap on expansions. This is what stops a search inside a
//                large open box from being expensive even when the box is legal.
//
// Whichever hits first, the answer is NO ROUTE and never a partial one. A
// partial route is worse than no route: the actor walks confidently to a place
// that is not where it wanted to go, arrives, and re-asks -- which reads to a
// player as an actor with somewhere else to be, forever.
//
// PER-ACTOR ROUTE JITTER. Entering a cell costs a few extra units drawn from a
// pure avalanche hash of (actor salt, cell). Small against the 10/14 base, so
// routes stay near-optimal -- but the tie-breaking landscape differs per actor,
// which is what ends the single-file convoy of a dozen dockhands walking the
// same tile sequence to the same warehouse. It is a HASH and not a draw: no RNG
// stream is touched, nothing is consumed, and the same actor asked the same
// question twice gets the same answer.

#include <cstdint>
#include <vector>

#include "granadad/sim/region_path.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

/// Cost of a straight step, and of a diagonal one. 10 and 14 rather than 1 and
/// sqrt(2), because the whole search is integer.
inline constexpr std::int32_t kStepCostOrthogonal = 10;
inline constexpr std::int32_t kStepCostDiagonal = 14;

/// Tiles of slack around the bounding rectangle of the two endpoints. A route
/// almost never needs to leave that rectangle; when it does -- rounding a
/// warehouse -- this is how far out of the way it is allowed to go.
inline constexpr std::int32_t kSearchPadding = 28;

/// The widest box a search may open, per axis. The Docks is 256 tiles across,
/// so this admits a route from one end of the district to the other and refuses
/// anything that would be a bug.
inline constexpr std::int32_t kSearchMaxSpan = 256;

/// Cell expansions before a search gives up. The Java's number, and it is a
/// budget rather than a limit: a clear route across the ward expands a few
/// hundred, and the searches that hit this are the ones asking for somewhere
/// there is no way to.
inline constexpr std::int32_t kSearchMaxNodes = 4000;

/// A route search. Holds its own scratch so a district-sized search does not
/// reallocate on every replan; not thread-safe and not meant to be.
class PathFinder {
public:
    explicit PathFinder(const TileQuery& tiles) noexcept : tiles_(&tiles) {}

    /// Fills `out` with the tiles from `from` (exclusive) to `to` (inclusive).
    ///
    /// Returns false and leaves `out` empty when there is no route inside the
    /// bounds, when the destination cannot be stood on, or when either bound is
    /// reached first. `from == to` is a success with an empty route.
    ///
    /// `salt` is the per-actor jitter key. ZERO MEANS NO JITTER, so an actor
    /// whose id is genuinely 0 must pass id + 1 -- see WardActor::replan.
    bool find(const PathStep& from, const PathStep& to, std::uint32_t salt,
              std::vector<PathStep>& out);

    /// How many cells the last search expanded. For tests and for the report:
    /// a pathing change that quietly triples the work is a change worth seeing.
    [[nodiscard]] std::int32_t lastExpansions() const noexcept { return expansions_; }

private:
    struct Box {
        std::int32_t x0 = 0, y0 = 0, z0 = 0;
        std::int32_t x1 = 0, y1 = 0, z1 = 0;
        [[nodiscard]] std::int32_t width() const noexcept { return x1 - x0 + 1; }
        [[nodiscard]] std::int32_t height() const noexcept { return y1 - y0 + 1; }
        [[nodiscard]] std::int32_t levels() const noexcept { return z1 - z0 + 1; }
        [[nodiscard]] bool contains(std::int32_t x, std::int32_t y,
                                    std::int32_t z) const noexcept {
            return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
        }
    };

    [[nodiscard]] Box boxFor(const PathStep& from, const PathStep& to) const noexcept;
    [[nodiscard]] std::int32_t indexOf(const Box& box, std::int32_t x, std::int32_t y,
                                       std::int32_t z) const noexcept;

    void heapPush(std::int32_t f, std::int32_t node);
    [[nodiscard]] std::int32_t heapPop();

    const TileQuery* tiles_;

    // Per-cell scratch, generation-stamped so a search never clears it.
    std::vector<std::uint32_t> stamp_;
    std::vector<std::int32_t> came_;
    std::vector<std::int32_t> gScore_;
    std::vector<std::uint8_t> closed_;
    std::uint32_t generation_ = 0;

    // The open heap, as three parallel arrays.
    std::vector<std::int32_t> heapF_;
    std::vector<std::int32_t> heapSeq_;
    std::vector<std::int32_t> heapNode_;
    std::int32_t sequence_ = 0;

    std::int32_t expansions_ = 0;
};

/// The avalanche the route jitter is drawn from: murmur3's fmix32 over the
/// actor salt and the cell key. Pure, and exposed because a test that cannot
/// name the function cannot prove the jitter is stable.
[[nodiscard]] constexpr std::uint32_t routeJitterHash(std::uint32_t salt,
                                                      std::uint32_t cellKey) noexcept {
    std::uint32_t h = salt * 0x9E3779B1u + cellKey;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

}  // namespace granadad::sim
