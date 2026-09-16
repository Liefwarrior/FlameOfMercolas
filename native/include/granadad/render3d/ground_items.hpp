#pragma once

// GROUND ITEMS -- the things lying on tiles, drawn.
//
// KIT BUILD. The room owns a hashed list of what is on the floor
// (sim::Tavern::groundItems(): the raws' authored stands less what was taken,
// plus what was dropped), and this is the one place it reaches the 3D world:
// every entry near the eye becomes a StaticInstance of the Synty static the
// piece catalogue's `items` table names for its item id (PieceRole::Item),
// stood on the tile's centre at the band's surface, lit by the light where
// it lies (the actor rule: ambient + baked + the Gull's flames). The room's
// two fixtures ride the same path off their own hashed state -- the four
// strongboxes at the bed feet while their room has a box (the `strongbox`
// row), the bale in the snug while balesInSnug_ > 0 (the `bale` row) -- so
// the chest and the sack the crosshair already names are things the eye can
// see.
//
// THE PLACEHOLDER RULE HOLDS. No catalogue, or no `items` table, or an id
// the table does not dress: no instance. A file the adapter cannot find: the
// instance is described (and hashed) and drawn as nothing -- the frame is
// what it was before the Kit. The renderer never removes an item from the
// sim; TAKE does, and the next frame has one fewer.
//
// Floats are legal here (render-side); nothing in this file is read by the
// simulation.

#include <vector>

#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/static_pieces.hpp"

namespace granadad::render {
class Session;
struct Camera;
}  // namespace granadad::render

namespace granadad::render3d {

/// Instances this frame: every ground item (and the two fixtures) inside
/// `maxDistance` of the eye, in the room's own list order, then the boxes,
/// then the bale. Pure over the session and the catalogue; appended to
/// SceneDescription::statics by the caller (after WorldScene::refresh wrote
/// the placed pieces, whose piece table the instances index).
[[nodiscard]] std::vector<StaticInstance> groundItemInstances(const render::Session& session,
                                                              const StaticCatalogue& catalogue,
                                                              const render::Camera& view,
                                                              float maxDistance = 40.0F);

}  // namespace granadad::render3d
