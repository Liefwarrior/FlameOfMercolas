#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

using content::TileForm;

TileQuery::TileQuery(const content::World& world) noexcept : world_(&world) {
    const content::Coords& coords = world.coords();
    sizeX_ = coords.chunksX() * content::kChunkSizeX;
    sizeY_ = coords.chunksY() * content::kChunkSizeY;
    sizeZ_ = coords.chunksZ() * content::kChunkSizeZ;
}

std::size_t TileQuery::index(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    const content::Coords& coords = world_->coords();
    const std::int32_t chunkIndex =
        coords.chunkIndexOf(x >> 5, y >> 5, z >> 3);
    const std::int32_t localIdx = content::Coords::localIdxOf(x & 31, y & 31, z & 7);
    return content::World::tileIndex(static_cast<std::size_t>(chunkIndex),
                                     static_cast<std::size_t>(localIdx));
}

TileForm TileQuery::form(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    if (!inBounds(x, y, z)) {
        return TileForm::Void;
    }
    return world_->form(index(x, y, z));
}

std::uint16_t TileQuery::material(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    if (!inBounds(x, y, z)) {
        return 0;
    }
    return world_->material(index(x, y, z));
}

int TileQuery::fluidDepth(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    if (!inBounds(x, y, z)) {
        return 0;
    }
    return content::fluid_bits::depth(world_->fluid(index(x, y, z)));
}

bool TileQuery::solid(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    const TileForm f = form(x, y, z);
    return f == TileForm::Wall || f == TileForm::Void;
}

bool TileQuery::walkable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    const TileForm f = form(x, y, z);
    if (f != TileForm::Floor && f != TileForm::Ramp && f != TileForm::Stair) {
        return false;
    }
    return fluidDepth(x, y, z) < kBlockingFluidDepth;
}

bool TileQuery::climbable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    const TileForm f = form(x, y, z);
    return f == TileForm::Ramp || f == TileForm::Stair;
}

bool TileQuery::headroom(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    // See the header: WALL at z+1 starts a whole tile above the feet and is
    // more clearance than a FLOOR slab, not less. VOID is the world border.
    return form(x, y, z + 1) != TileForm::Void;
}

bool TileQuery::standable(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    return walkable(x, y, z) && headroom(x, y, z);
}

bool TileQuery::lineOfSight(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                            std::int32_t z) const noexcept {
    if (x0 == x1 && y0 == y1) {
        return true;
    }
    // Supercover: the walk visits every cell the segment passes through, so a
    // ray cannot slip between two diagonally touching wall corners. All integer
    // -- the loop counts crossings of vertical and horizontal grid lines with
    // cross-multiplied comparisons and never divides.
    const std::int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const std::int32_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const std::int32_t stepX = x1 > x0 ? 1 : -1;
    const std::int32_t stepY = y1 > y0 ? 1 : -1;
    std::int32_t x = x0;
    std::int32_t y = y0;
    // Distance travelled along each axis, counted in whole cells and compared
    // as `nextX * dy` against `nextY * dx` -- the same test as comparing the
    // two fractions, with no division and no float anywhere near it.
    std::int32_t nextX = 1;
    std::int32_t nextY = 1;
    // Bounded so a coordinate pair nothing sane produced cannot spin forever.
    // The walk reaches the far endpoint in at most dx + dy steps.
    for (std::int32_t guard = dx + dy + 2; guard > 0; --guard) {
        if (nextX * dy < nextY * dx) {
            x += stepX;
            ++nextX;
        } else if (nextX * dy > nextY * dx) {
            y += stepY;
            ++nextY;
        } else {
            // Exactly through a lattice corner. You cannot see through a shut
            // corner: if BOTH cells the line grazes are solid, this is two
            // walls meeting and the ray stops there.
            if (solid(x + stepX, y, z) && solid(x, y + stepY, z)) {
                return false;
            }
            x += stepX;
            y += stepY;
            ++nextX;
            ++nextY;
        }
        if (x == x1 && y == y1) {
            // The far endpoint is the person being looked at, not an obstacle.
            return true;
        }
        if (solid(x, y, z)) {
            return false;
        }
    }
    return true;
}

std::int32_t TileQuery::stepBand(std::int32_t fromX, std::int32_t fromY, std::int32_t fromZ,
                                 std::int32_t x, std::int32_t y) const noexcept {
    if (standable(x, y, fromZ)) {
        return fromZ;
    }
    if (standable(x, y, fromZ - 1)) {
        return fromZ - 1;
    }
    if (standable(x, y, fromZ + 1) &&
        (climbable(fromX, fromY, fromZ) || climbable(x, y, fromZ + 1))) {
        return fromZ + 1;
    }
    return kNoBand;
}

}  // namespace granadad::sim
