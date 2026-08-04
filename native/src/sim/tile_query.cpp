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
