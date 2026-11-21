#pragma once

// THE DOCKS, PLACED FOR A SESSION: the one place a render::Session becomes a
// render3d::SceneDescription. The client's window and its capture shutter, and
// the workbench's game pane, all draw through this, so there is exactly one
// answer to "what does the game look like" and it is not copied anywhere.
//
// (It lived in client/main.cpp until phase 7 of docs/design/OBLIVION-PLAN.md
// moved the workbench's game pane onto the 3D backend and it had a second
// caller.)

#include <memory>
#include <string>

#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/static_pieces.hpp"
#include "granadad/render3d/world_scene.hpp"

namespace granadad::render {
class Session;
}

namespace granadad::render3d {

/// A LANE. Where the skinned rigs live when the licensed export has been run:
/// content/art/lot-3d/characters (gitignored; absent on a fresh checkout and
/// in the docker gate, and the adapter falls back to the placeholder figures
/// per rig without a word).
[[nodiscard]] std::string rigModelDir();
/// V LANE. Where the static weapon exports live (the mace and the dagger hung
/// on the viewmodel's Hand_R when they exist): content/art/lot-3d/static,
/// same licence, same gitignore, same silent fallback.
[[nodiscard]] std::string weaponModelDir();
/// S LANE. Where the static building pieces live -- the same export tree the
/// weapons come from; the catalogue names "<pack>/<prefab>.gltf" under it.
[[nodiscard]] std::string staticModelDir();

/// The session's own tiles meshed by chunk (built once, on the first frame),
/// lit from its own clock, its baked lamps and the Gull's own hearth and
/// candles, the sky dome on the eye, and the camera from the session's own
/// Camera -- Session::camera(), the body's Q8 position and BAM facing through
/// Camera::fromBody, combat impulses included. The rig borrows the session's
/// tiles, atlas and lamp field, so it must not outlive the session it was
/// first refreshed with.
struct SceneRig {
    /// PHASE 5: lit per pixel (--flat-light turns it off).
    bool perPixel = true;
    /// PHASE 6, VERIFICATION ONLY: --hands= (-1 = what the sim holds) and
    /// --sheet= (0 off, 1 one figure per clip, 2 one per kind of body).
    int handsKind = -1;
    int sheet = 0;
    SceneDescription scene;
    std::unique_ptr<WorldScene> world;
    /// S LANE. The piece catalogue (content/raws/world3d/docks-pieces.json)
    /// loaded once per rig; the world scene borrows it. Absent or malformed
    /// -> empty -> no pieces, the district exactly as before the lane.
    StaticCatalogue catalogue;

    /// Rewrites `scene` for this frame off the session's state.
    void refresh(const render::Session& session, float aspect);
};

}  // namespace granadad::render3d
