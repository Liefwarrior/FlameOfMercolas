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
// #32. NEITHER BOUND MEANS "UNREACHABLE", and a caller that treats a NO ROUTE
// answer that way needs its own reason to believe it -- WardPopulation gets
// that reason from componentAt(), asked BEFORE this is ever called, over the
// unbounded flood fill kSearchPadding and kSearchMaxNodes cannot see past. A
// search that hits either bound has only proven that ITS box or ITS budget
// was too small, and roughly a sixteenth of the shipped ward's own home-to-
// work legs used to hit one of the two despite a real route existing. See the
// two constants' own comments for the measurement.
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

/// WHAT A BODY IS ABLE TO DO TO GET SOMEWHERE.
///
/// #80. The player has had the three roof moves since S5 (player.hpp: mantle,
/// leap, drop) and no ward actor had any of them, which is why the roof slum
/// was empty and why the criminal faction NAMED FOR RUNNING THE ROOFS kept
/// ground-level condos. This is the sim-side half, and it deliberately reuses
/// TileQuery's own mantleBand/landingBand rather than inventing a second
/// opinion about what a wall is -- the same reason walking, sight and the
/// player's climb are all answered in that one file.
enum class Gait : std::uint8_t {
    /// stepBand and nothing else: the district's ordinary walking rule, up only
    /// where a ramp or a stair was authored. Everybody in a coat of plates.
    Walk = 0,
    /// And a mantle up one level, and a drop of up to kMaxPathDrop. What the
    /// ward's poor and its beasts can do, and what the Watch deliberately
    /// cannot -- see wardTypeClimbs.
    Climb = 1,
};

/// CLIMBING IS NOT FREE, and the number is the whole of that claim.
///
/// A mantle costs seven ordinary steps. A route that can go round a wall in
/// under seven tiles will go round it; a burglar crossing the Gullet's roof
/// decks -- where going round is forty tiles of street with a watchman on it --
/// goes over. Priced too low and every actor in the district hauls itself over
/// every kerb; priced at nothing and the roofs become the shortest path between
/// any two points in the ward, which is a district where nobody uses the roads.
inline constexpr std::int32_t kMantleCost = 70;
/// A drop is cheaper than a climb -- gravity does the work -- and it is not
/// free either, because a body that treated every ledge as a shortcut would
/// throw itself off the Long Quay to save two tiles.
inline constexpr std::int32_t kDropCostBase = 30;
inline constexpr std::int32_t kDropCostPerLevel = 20;
/// How far a PLANNED drop may fall. Deliberately short: the player's own drop
/// is a thing a person chooses to do and can look before doing, and a route
/// planner has no eyes. Three levels is a two-storey compound to its courtyard.
inline constexpr std::int32_t kMaxPathDrop = 3;

/// Tiles of slack around the bounding rectangle of the two endpoints. A route
/// almost never needs to leave that rectangle; when it does -- rounding a
/// warehouse -- this is how far out of the way it is allowed to go.
///
/// #32. WAS 28, AND 28 WAS NOT ENOUGH. bakeRoster() only ever homes or posts
/// a body on ground the unbounded flood fill (walkComponent_) calls
/// reachable, so every home-to-work pair in the roster genuinely has a route
/// -- but the Docks' own compounds force some of those routes into a detour
/// wider than a straight-line rectangle plus 28 tiles, and a search that
/// cannot see outside its box reports NO ROUTE for a body that has one. The
/// case in test_ward_actors.cpp that names #32 walks the router over every
/// authored home/anchor pair in the shipped roster and found the tightest
/// one needed 54; this carries a real margin over that measurement rather
/// than the bare minimum, because the next map edit should not have to
/// re-derive the number.
inline constexpr std::int32_t kSearchPadding = 64;

/// The widest box a search may open, per axis. The Docks is 256 tiles across,
/// so this admits a route from one end of the district to the other and refuses
/// anything that would be a bug.
inline constexpr std::int32_t kSearchMaxSpan = 256;

/// Cell expansions before a search gives up. Was 4000, the Java's number, and
/// it is a budget rather than a limit: a clear LOCAL route across the ward
/// still expands only a few hundred.
///
/// #32. BUT NOT EVERY ROUTE IS LOCAL, and 4000 quietly assumed one thing the
/// comment above used to claim outright: that a search hitting the cap was
/// always asking for somewhere there was no way to. It was not. bakeRoster()
/// never homes or posts a body anywhere the flood fill cannot reach, so the
/// only searches that ever run here (stepToward gates on componentAt() first)
/// are already known-connected -- and roughly a sixteenth of the shipped
/// roster's home-to-work legs are long enough, through the compounds' own
/// turns, to need more than 4000 expansions before the goal comes off the
/// heap. Measured (the #32 case in test_ward_actors.cpp, over the whole
/// roster): the worst of those legs needed just under 7,000. This is set with
/// real headroom above that measurement, not at it.
inline constexpr std::int32_t kSearchMaxNodes = 10000;

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
    ///
    /// `gait` says what the body may do. WALKING IS THE DEFAULT and every
    /// existing caller keeps exactly the route it had: a climb move is only
    /// ever offered for a neighbour the walking rule already REFUSED, so a
    /// Climb search over ground with no walls in it expands the same cells in
    /// the same order at the same costs as a Walk one.
    bool find(const PathStep& from, const PathStep& to, std::uint32_t salt,
              std::vector<PathStep>& out, Gait gait = Gait::Walk);

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
