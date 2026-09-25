#include "granadad/render3d/door_jambs.hpp"

#include "granadad/render3d/static_pieces.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

namespace {

[[nodiscard]] bool isWall(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                          std::int32_t z) noexcept {
    return tiles.form(x, y, z) == content::TileForm::Wall;
}

/// Not a wall and not the void: a cell a face can be seen from. The door
/// rule's own word for the air either side of an opening.
[[nodiscard]] bool isOpenish(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                             std::int32_t z) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    return form != content::TileForm::Wall && form != content::TileForm::Void;
}

[[nodiscard]] bool isWalkableForm(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                  std::int32_t z) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    return form == content::TileForm::Floor || form == content::TileForm::Ramp ||
           form == content::TileForm::Stair;
}

/// A cell of an opening in a wall line: walkable, and open on both of the
/// two sides the line does not run along. `alongX` means the line is a row
/// (the opening is a hole in an X-line, open north and south).
[[nodiscard]] bool gapCell(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                           std::int32_t z, bool alongX) noexcept {
    if (!isWalkableForm(tiles, x, y, z)) {
        return false;
    }
    return alongX ? (isOpenish(tiles, x, y - 1, z) && isOpenish(tiles, x, y + 1, z))
                  : (isOpenish(tiles, x - 1, y, z) && isOpenish(tiles, x + 1, y, z));
}

/// Ground under a cell. Once the jamb cell's own box is gone the cell below
/// shows its top face, and that top face is the threshold the beam stands
/// on -- without one the doorway would open onto a hole.
[[nodiscard]] bool standsOn(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                            std::int32_t z) noexcept {
    const content::TileForm below = tiles.form(x, y, z - 1);
    return below != content::TileForm::Open && below != content::TileForm::Void;
}

}  // namespace

bool doorPostAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                std::int32_t z) noexcept {
    if (!isWall(tiles, x, y, z) || !standsOn(tiles, x, y, z)) {
        return false;
    }
    for (int axis = 0; axis < 2; ++axis) {
        const bool alongX = axis == 0;
        const std::int32_t ax = alongX ? 1 : 0;
        const std::int32_t ay = alongX ? 0 : 1;
        for (std::int32_t dir = -1; dir <= 1; dir += 2) {
            // The opening would start at our neighbour on this side and run
            // on away from us.
            const std::int32_t dx = dir * ax;
            const std::int32_t dy = dir * ay;
            const std::int32_t sx = x + dx;
            const std::int32_t sy = y + dy;
            if (!gapCell(tiles, sx, sy, z, alongX)) {
                continue;
            }
            std::int32_t w = 1;
            while (w <= kMaxDoorCells && gapCell(tiles, sx + w * dx, sy + w * dy, z, alongX)) {
                ++w;
            }
            if (w < 2 || w > kMaxDoorCells) {
                // One cell is a notch, four and over is a gate: neither
                // wears jambs.
                continue;
            }
            // A second cell of wall behind us, the far jamb, and a second
            // cell of wall behind that: the door rule's own test, read from
            // this end.
            const std::int32_t fx = sx + w * dx;
            const std::int32_t fy = sy + w * dy;
            if (!isWall(tiles, x - dx, y - dy, z) || !isWall(tiles, fx, fy, z) ||
                !isWall(tiles, fx + dx, fy + dy, z)) {
                continue;
            }
            // Exactly one side out of doors, asked at the opening's own
            // first cell in world order so both jambs read the same cell:
            // a doorway, not a passage between two rooms or two streets.
            const std::int32_t g0x = dir > 0 ? sx : sx - (w - 1) * ax;
            const std::int32_t g0y = dir > 0 ? sy : sy - (w - 1) * ay;
            const std::int32_t ox = alongX ? 0 : 1;
            const std::int32_t oy = alongX ? 1 : 0;
            if (cellRoofed(tiles, g0x - ox, g0y - oy, z) !=
                cellRoofed(tiles, g0x + ox, g0y + oy, z)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace granadad::render3d
