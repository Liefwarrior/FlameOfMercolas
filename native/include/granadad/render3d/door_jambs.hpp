#pragma once

// THE JAMBS -- which wall cells flank a door opening, and how high the head
// of that opening is.
//
// ONE TEST, TWO READERS. The placer (static_pieces.cpp) stands a slim timber
// jamb on every cell this says yes to; the chunk mesher (chunk_mesher.cpp)
// leaves that same cell's metre-square box out from the ground to the head,
// so the beam IS the jamb instead of a decal stuck on a block. If the two
// ever disagreed the frontage would show a hole or swallow the beam, so
// neither owns the rule: it lives here, pure over the tile bytes, with no
// catalogue in it at all (the mesher has none).
//
// It is findDoors()' own door rule read from the outside in: a WALL cell
// whose neighbour along a wall line starts a two- or three-cell walkable
// opening, with a second cell of wall behind this one and two more beyond
// the far end, exactly one side of the opening out of doors, and ground
// under the cell to stand the beam on. A GATE (four cells and wider) is not
// a door and keeps its own posts and beam.

#include <cstdint>

namespace granadad::sim {
class TileQuery;
}

namespace granadad::render3d {

/// The head of a door opening over its threshold -- the underside of the
/// lintel. The jamb beam runs the ground to here and the lintel lies
/// across here.
inline constexpr float kDoorHeadHeight = 2.03F;

/// The soffit over the opening, between the two lintels: AT the head, the
/// level of the beams' undersides, ending at their back faces (adjacent,
/// never overlapping, so nothing fights). At the beams' top level the
/// whole inner face of each lintel hung below the soffit, hewn facets and
/// all, and read as a dark tent in the head from the street.
inline constexpr float kDoorSoffitHeight = kDoorHeadHeight;

/// Where the mesher cuts the jamb cell's box off: not at the head but three
/// centimetres OVER the soffit. The reveal quad stands a centimetre inside
/// the jamb cell (a rebate the post stands proud of), so any stone course
/// starting under the soffit showed through that centimetre as a black
/// band over every open leaf; and the box's underside three centimetres
/// over the soffit's buried edge is out of the depth buffer's reach of it.
/// What is left of the cell over the cut is inside the head box the
/// headers, the soffit and the reveals close, and shows nowhere.
inline constexpr float kDoorHeadCut = kDoorSoffitHeight + 0.03F;

/// The widest opening still a door and not a gate, in cells (findDoors()
/// calls anything wider a gate: DoorGap::gate()).
inline constexpr std::int32_t kMaxDoorCells = 3;

/// Whether the WALL cell at (x, y, z) flanks a door opening: the cell hard
/// against the left or the right of the gap, along the gap's own line.
[[nodiscard]] bool doorPostAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                              std::int32_t z) noexcept;

}  // namespace granadad::render3d
