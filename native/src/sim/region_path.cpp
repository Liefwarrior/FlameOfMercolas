#include "granadad/sim/region_path.hpp"

#include <algorithm>
#include <array>

namespace granadad::sim {

namespace {

// The eight neighbours, in a FIXED order that is part of the answer. Orthogonals
// first so a route prefers a straight line over a diagonal when both are the
// same length, which is what makes actors walk down the middle of a room rather
// than zig-zagging across it.
constexpr std::array<std::int32_t, 8> kStepX = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr std::array<std::int32_t, 8> kStepY = {-1, 0, 1, 0, -1, 1, 1, -1};
constexpr std::size_t kOrthogonalCount = 4;

constexpr std::int32_t kUnvisited = -1;
constexpr std::int32_t kRoot = -2;

}  // namespace

RegionPath::RegionPath(const TileQuery& tiles, const TileBox& box) : tiles_(&tiles), box_(box) {
    came_.assign(static_cast<std::size_t>(std::max(0, box_.cellCount())), kUnvisited);
    frontier_.reserve(came_.size());
}

std::int32_t RegionPath::indexOf(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    return ((z - box_.z0) * box_.height() + (y - box_.y0)) * box_.width() + (x - box_.x0);
}

bool RegionPath::find(const PathStep& from, const PathStep& to, std::vector<PathStep>& out) {
    out.clear();
    if (!box_.contains(from.x, from.y, from.band) || !box_.contains(to.x, to.y, to.band)) {
        return false;
    }
    if (from.x == to.x && from.y == to.y && from.band == to.band) {
        return true;
    }
    if (!tiles_->standable(to.x, to.y, to.band)) {
        return false;
    }

    std::fill(came_.begin(), came_.end(), kUnvisited);
    frontier_.clear();

    const std::int32_t start = indexOf(from.x, from.y, from.band);
    came_[static_cast<std::size_t>(start)] = kRoot;
    frontier_.push_back(start);

    // A plain FIFO over an index vector: no allocation per search and the
    // visit order is exactly insertion order, which is what makes the route
    // reproducible.
    std::size_t head = 0;
    std::int32_t goal = -1;
    while (head < frontier_.size() && goal < 0) {
        const std::int32_t cell = frontier_[head++];
        const std::int32_t local = cell % (box_.width() * box_.height());
        const std::int32_t x = box_.x0 + (local % box_.width());
        const std::int32_t y = box_.y0 + (local / box_.width());
        const std::int32_t z = box_.z0 + (cell / (box_.width() * box_.height()));

        for (std::size_t dir = 0; dir < kStepX.size() && goal < 0; ++dir) {
            const std::int32_t nx = x + kStepX[dir];
            const std::int32_t ny = y + kStepY[dir];
            const std::int32_t band = tiles_->stepBand(x, y, z, nx, ny);
            if (band == TileQuery::kNoBand || !box_.contains(nx, ny, band)) {
                continue;
            }
            if (dir >= kOrthogonalCount) {
                // A diagonal needs both of its orthogonal halves to be enterable
                // too, or actors cut the corner where two walls meet.
                const std::int32_t sideA = tiles_->stepBand(x, y, z, nx, y);
                const std::int32_t sideB = tiles_->stepBand(x, y, z, x, ny);
                if (sideA == TileQuery::kNoBand || sideB == TileQuery::kNoBand) {
                    continue;
                }
            }
            const std::int32_t next = indexOf(nx, ny, band);
            if (came_[static_cast<std::size_t>(next)] != kUnvisited) {
                continue;
            }
            came_[static_cast<std::size_t>(next)] = cell;
            if (nx == to.x && ny == to.y && band == to.band) {
                goal = next;
                break;
            }
            frontier_.push_back(next);
        }
    }

    if (goal < 0) {
        return false;
    }

    // Walk back to `start` and stop THERE, not at the root marker: the route is
    // where the actor is going, and the tile it is standing on is not part of
    // that.
    scratch_.clear();
    for (std::int32_t cell = goal; cell != start;
         cell = came_[static_cast<std::size_t>(cell)]) {
        const std::int32_t local = cell % (box_.width() * box_.height());
        scratch_.push_back(PathStep{box_.x0 + (local % box_.width()),
                                    box_.y0 + (local / box_.width()),
                                    box_.z0 + (cell / (box_.width() * box_.height()))});
    }
    out.assign(scratch_.rbegin(), scratch_.rend());
    return true;
}

PathStep RegionPath::firstStepToward(const PathStep& from, const PathStep& to) {
    if (!find(from, to, route_) || route_.empty()) {
        return from;
    }
    return route_.front();
}

}  // namespace granadad::sim
