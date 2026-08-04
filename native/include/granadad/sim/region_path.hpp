#pragma once

// Getting from one tile to another inside a bounded piece of the world.
//
// A greedy step-toward-the-target walk is enough in a street and useless in a
// room: the Gilded Gull's taproom has a seven-tile bar counter across the
// middle of it, four table blocks and a partition wall, and an actor walking
// straight at its post sticks to the first of them and stays there. Being stuck
// is worse than being slow, because a stuck actor is a bug the player watches.
//
// So it is a breadth-first search over standable tiles inside a BOX, which is
// the right shape of answer for interiors: the box is small (the Gull and the
// street outside it is under a thousand cells), the search is exact, and it
// cannot wander off across the district when a post becomes unreachable.
//
// DETERMINISM. The frontier is a plain FIFO and the eight neighbours are
// visited in a FIXED order, so the path between two tiles is a pure function of
// the box and the world. Nothing here draws from an RNG and nothing here is a
// float: an actor's route is simulation state and it has to replay.
//
// Diagonals are allowed, and a diagonal step is only allowed when BOTH of its
// orthogonal neighbours can also be entered -- otherwise actors slip through
// the corner where two walls meet, which reads as walking through the join.

#include <cstdint>
#include <vector>

#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

/// An inclusive tile box. Everything a search may touch.
struct TileBox {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t z0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t z1 = 0;

    [[nodiscard]] constexpr bool contains(std::int32_t x, std::int32_t y,
                                          std::int32_t z) const noexcept {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    }
    [[nodiscard]] constexpr std::int32_t width() const noexcept { return x1 - x0 + 1; }
    [[nodiscard]] constexpr std::int32_t height() const noexcept { return y1 - y0 + 1; }
    [[nodiscard]] constexpr std::int32_t levels() const noexcept { return z1 - z0 + 1; }
    [[nodiscard]] constexpr std::int32_t cellCount() const noexcept {
        return width() * height() * levels();
    }
};

/// One tile on a route.
struct PathStep {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
};

/// A search over one box. Holds its own scratch so repeated searches over the
/// same room do not reallocate; not thread-safe, and not meant to be.
class RegionPath {
public:
    RegionPath(const TileQuery& tiles, const TileBox& box);

    [[nodiscard]] const TileBox& box() const noexcept { return box_; }

    /// Fills `out` with the tiles from `from` (exclusive) to `to` (inclusive).
    /// Returns false and leaves `out` empty when there is no route inside the
    /// box, when either end is outside it, or when the destination cannot be
    /// stood on.
    ///
    /// `from` and `to` already standing on the same tile is a success with an
    /// empty route.
    bool find(const PathStep& from, const PathStep& to, std::vector<PathStep>& out);

    /// The first step of the route, or `from` itself when there is none. The
    /// shape actors actually want: they re-ask every time they arrive.
    [[nodiscard]] PathStep firstStepToward(const PathStep& from, const PathStep& to);

private:
    [[nodiscard]] std::int32_t indexOf(std::int32_t x, std::int32_t y,
                                       std::int32_t z) const noexcept;

    const TileQuery* tiles_;
    TileBox box_;
    /// Predecessor cell index per cell, -1 unvisited, -2 for the root.
    std::vector<std::int32_t> came_;
    std::vector<std::int32_t> frontier_;
    std::vector<PathStep> scratch_;
    /// firstStepToward's own route buffer, so the call an actor makes every
    /// time it arrives somewhere does not allocate.
    std::vector<PathStep> route_;
};

}  // namespace granadad::sim
