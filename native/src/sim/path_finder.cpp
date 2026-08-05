#include "granadad/sim/path_finder.hpp"

#include <algorithm>
#include <utility>

namespace granadad::sim {

namespace {

/// The eight neighbours, IN THIS ORDER, and the order is load-bearing.
///
/// Four orthogonals first, then four diagonals. Two routes of equal cost are
/// separated by which neighbour was pushed first, so changing this list changes
/// which way every actor in the ward walks round every corner in the district.
constexpr std::int32_t kNeighbourDx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
constexpr std::int32_t kNeighbourDy[8] = {0, 0, -1, 1, -1, -1, 1, 1};

/// The octile distance between two cells, in the same 10/14 units the steps
/// cost. Admissible: it never over-estimates, because a diagonal is the
/// cheapest way to close both axes at once and 14 <= 2 * 10.
///
/// The band difference is charged at the orthogonal rate. A band is a storey
/// and reaching it costs at least one authored ramp step, so charging it
/// something is closer to the truth than charging it nothing -- and one step
/// per band is the floor, which keeps the estimate admissible.
[[nodiscard]] std::int32_t octile(std::int32_t ax, std::int32_t ay, std::int32_t az,
                                  std::int32_t bx, std::int32_t by, std::int32_t bz) noexcept {
    const std::int32_t dx = ax > bx ? ax - bx : bx - ax;
    const std::int32_t dy = ay > by ? ay - by : by - ay;
    const std::int32_t dz = az > bz ? az - bz : bz - az;
    const std::int32_t lo = std::min(dx, dy);
    const std::int32_t hi = std::max(dx, dy);
    return kStepCostDiagonal * lo + kStepCostOrthogonal * (hi - lo) +
           kStepCostOrthogonal * dz;
}

}  // namespace

PathFinder::Box PathFinder::boxFor(const PathStep& from, const PathStep& to) const noexcept {
    Box box;
    box.x0 = std::min(from.x, to.x) - kSearchPadding;
    box.x1 = std::max(from.x, to.x) + kSearchPadding;
    box.y0 = std::min(from.y, to.y) - kSearchPadding;
    box.y1 = std::max(from.y, to.y) + kSearchPadding;
    // One band of slack each way: a route between two bands can legitimately
    // touch a third when the only authored ramp climbs past its own landing.
    box.z0 = std::min(from.band, to.band) - 1;
    box.z1 = std::max(from.band, to.band) + 1;

    box.x0 = std::max(box.x0, 0);
    box.y0 = std::max(box.y0, 0);
    box.z0 = std::max(box.z0, 0);
    box.x1 = std::min(box.x1, tiles_->sizeX() - 1);
    box.y1 = std::min(box.y1, tiles_->sizeY() - 1);
    box.z1 = std::min(box.z1, tiles_->sizeZ() - 1);
    return box;
}

std::int32_t PathFinder::indexOf(const Box& box, std::int32_t x, std::int32_t y,
                                 std::int32_t z) const noexcept {
    return ((z - box.z0) * box.height() + (y - box.y0)) * box.width() + (x - box.x0);
}

void PathFinder::heapPush(std::int32_t f, std::int32_t node) {
    heapF_.push_back(f);
    heapSeq_.push_back(sequence_++);
    heapNode_.push_back(node);
    std::size_t i = heapF_.size() - 1;
    while (i > 0) {
        const std::size_t parent = (i - 1) / 2;
        const bool higher = heapF_[i] < heapF_[parent] ||
                            (heapF_[i] == heapF_[parent] && heapSeq_[i] < heapSeq_[parent]);
        if (!higher) {
            break;
        }
        std::swap(heapF_[i], heapF_[parent]);
        std::swap(heapSeq_[i], heapSeq_[parent]);
        std::swap(heapNode_[i], heapNode_[parent]);
        i = parent;
    }
}

std::int32_t PathFinder::heapPop() {
    const std::int32_t top = heapNode_.front();
    const std::size_t last = heapF_.size() - 1;
    heapF_[0] = heapF_[last];
    heapSeq_[0] = heapSeq_[last];
    heapNode_[0] = heapNode_[last];
    heapF_.pop_back();
    heapSeq_.pop_back();
    heapNode_.pop_back();
    std::size_t i = 0;
    const std::size_t size = heapF_.size();
    while (true) {
        const std::size_t left = 2 * i + 1;
        const std::size_t right = left + 1;
        std::size_t best = i;
        if (left < size && (heapF_[left] < heapF_[best] ||
                            (heapF_[left] == heapF_[best] && heapSeq_[left] < heapSeq_[best]))) {
            best = left;
        }
        if (right < size && (heapF_[right] < heapF_[best] ||
                             (heapF_[right] == heapF_[best] && heapSeq_[right] < heapSeq_[best]))) {
            best = right;
        }
        if (best == i) {
            break;
        }
        std::swap(heapF_[i], heapF_[best]);
        std::swap(heapSeq_[i], heapSeq_[best]);
        std::swap(heapNode_[i], heapNode_[best]);
        i = best;
    }
    return top;
}

bool PathFinder::find(const PathStep& from, const PathStep& to, std::uint32_t salt,
                      std::vector<PathStep>& out) {
    out.clear();
    expansions_ = 0;
    if (from.x == to.x && from.y == to.y && from.band == to.band) {
        return true;
    }
    if (!tiles_->standable(to.x, to.y, to.band) || !tiles_->inBounds(from.x, from.y, from.band)) {
        return false;
    }

    const Box box = boxFor(from, to);
    if (box.width() > kSearchMaxSpan || box.height() > kSearchMaxSpan) {
        return false;
    }
    if (!box.contains(from.x, from.y, from.band) || !box.contains(to.x, to.y, to.band)) {
        return false;
    }

    const std::size_t cells = static_cast<std::size_t>(box.width()) *
                              static_cast<std::size_t>(box.height()) *
                              static_cast<std::size_t>(box.levels());
    if (stamp_.size() < cells) {
        stamp_.assign(cells, 0);
        came_.assign(cells, -1);
        gScore_.assign(cells, 0);
        closed_.assign(cells, 0);
        generation_ = 0;
    }
    ++generation_;
    if (generation_ == 0) {
        // Wrapped: the only moment the scratch has to be cleared, and it
        // happens once every four billion searches.
        std::fill(stamp_.begin(), stamp_.end(), 0);
        generation_ = 1;
    }

    heapF_.clear();
    heapSeq_.clear();
    heapNode_.clear();
    sequence_ = 0;

    const std::int32_t start = indexOf(box, from.x, from.y, from.band);
    const std::int32_t goal = indexOf(box, to.x, to.y, to.band);
    stamp_[static_cast<std::size_t>(start)] = generation_;
    came_[static_cast<std::size_t>(start)] = -2;
    gScore_[static_cast<std::size_t>(start)] = 0;
    closed_[static_cast<std::size_t>(start)] = 0;
    heapPush(octile(from.x, from.y, from.band, to.x, to.y, to.band), start);

    const std::int32_t width = box.width();
    const std::int32_t height = box.height();
    bool found = false;

    while (!heapF_.empty()) {
        const std::int32_t node = heapPop();
        const std::size_t at = static_cast<std::size_t>(node);
        if (closed_[at] != 0) {
            continue;  // lazy deletion: a stale copy of a cell already expanded
        }
        closed_[at] = 1;
        if (node == goal) {
            found = true;
            break;
        }
        if (++expansions_ > kSearchMaxNodes) {
            break;
        }

        const std::int32_t cz = box.z0 + node / (width * height);
        const std::int32_t rest = node % (width * height);
        const std::int32_t cy = box.y0 + rest / width;
        const std::int32_t cx = box.x0 + rest % width;
        const std::int32_t g = gScore_[at];

        for (int n = 0; n < 8; ++n) {
            const std::int32_t nx = cx + kNeighbourDx[n];
            const std::int32_t ny = cy + kNeighbourDy[n];
            if (!box.contains(nx, ny, cz)) {
                continue;
            }
            const std::int32_t nz = tiles_->stepBand(cx, cy, cz, nx, ny);
            if (nz == TileQuery::kNoBand || !box.contains(nx, ny, nz)) {
                continue;
            }
            const bool diagonal = n >= 4;
            if (diagonal) {
                // NEVER CUT A SOLID CORNER. A diagonal is legal only when both
                // of its orthogonal flanks can also be entered. Without this an
                // actor slips between two wall corners into a pocket whose
                // every exit needs the same cut -- and the route out is a cut
                // this search refuses to plan, so the actor is sealed in for
                // good. The same rule lives in Actor::tryStep and in
                // RegionPath; all three must agree.
                if (tiles_->stepBand(cx, cy, cz, nx, cy) == TileQuery::kNoBand ||
                    tiles_->stepBand(cx, cy, cz, cx, ny) == TileQuery::kNoBand) {
                    continue;
                }
            }
            const std::int32_t neighbour = indexOf(box, nx, ny, nz);
            const std::size_t nAt = static_cast<std::size_t>(neighbour);
            if (stamp_[nAt] == generation_ && closed_[nAt] != 0) {
                continue;
            }
            // The jitter. Non-negative and small, so the heuristic stays
            // admissible and routes stay near-optimal -- but two actors asking
            // the same question break their ties differently.
            const std::int32_t jitter =
                salt == 0 ? 0
                          : static_cast<std::int32_t>(
                                routeJitterHash(salt, static_cast<std::uint32_t>(neighbour)) & 3u);
            const std::int32_t step =
                (diagonal ? kStepCostDiagonal : kStepCostOrthogonal) + jitter;
            const std::int32_t tentative = g + step;
            if (stamp_[nAt] == generation_ && gScore_[nAt] <= tentative) {
                continue;
            }
            stamp_[nAt] = generation_;
            closed_[nAt] = 0;
            gScore_[nAt] = tentative;
            came_[nAt] = node;
            heapPush(tentative + octile(nx, ny, nz, to.x, to.y, to.band), neighbour);
        }
    }

    if (!found) {
        return false;
    }

    // Walk the predecessors back and reverse. `from` itself is excluded, which
    // is what makes out.front() the first step an actor takes.
    std::int32_t node = goal;
    while (node != start) {
        const std::int32_t z = box.z0 + node / (width * height);
        const std::int32_t rest = node % (width * height);
        out.push_back(PathStep{box.x0 + rest % width, box.y0 + rest / width, z});
        node = came_[static_cast<std::size_t>(node)];
    }
    std::reverse(out.begin(), out.end());
    return true;
}

}  // namespace granadad::sim
