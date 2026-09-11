#pragma once

// THE STARTER SCENE -- a lit ground plane and a cube.
//
// The first thing the 3D path ever drew, and the reference every later lane
// builds against: it is what the toolchain gate renders headless through
// rlsw, what the client shows under the terminal HUD with --3d until the
// chunk mesher lands, and the worked example of how a lane fills a
// SceneDescription -- build a MeshData with per-vertex colours for the
// lighting, put it in the scene under a stable id, add an Instance.
//
// "Lit" means what it will keep meaning on this renderer: VERTEX COLOURS.
// There are no shaders on the software path, so every face carries its own
// shade -- ambient from the day curve plus a Lambert term against one sun
// direction -- and the GPU path draws the identical colours because it is
// handed the identical bytes.

#include "granadad/render3d/scene.hpp"

namespace granadad::render3d {

struct StarterSceneParams {
    /// Height of the ground, in scene units. render::bandSurface(band) for
    /// the band the body stands on, so the plane is underfoot.
    float groundY = 0.0F;
    /// Where the plane is centred, scene space (y is ignored).
    Vec3 centre;
    /// Where the cube stands, scene space (y is ignored: it sits on the
    /// ground, one tile on a side).
    Vec3 cube;
    /// Half the plane's side, in tiles.
    float halfExtent = 48.0F;
    /// Seconds since midnight. Drives the sky, the ambient and the sun.
    int timeOfDaySeconds = 12 * 3600;
};

/// The version both meshes carry for a set of params: the time-of-day
/// bucket (one per simulated minute), so a scene that is refreshed every
/// frame re-uploads nothing until the light actually moves.
[[nodiscard]] std::uint32_t starterSceneVersion(const StarterSceneParams& params) noexcept;

/// Puts (or refreshes) the two meshes and their instances into `scene` and
/// sets the clear colour to the sky. Leaves the camera alone -- the caller
/// owns that. Meshes are rebuilt only when their version would change.
void buildStarterScene(SceneDescription& scene, const StarterSceneParams& params);

}  // namespace granadad::render3d
