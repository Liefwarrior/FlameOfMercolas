#pragma once

// World-space name signs: the first text this renderer draws that lives IN
// the world rather than pinned to a screen edge.
//
// Every Docks building's door and every named street gets a legible label
// when the player is close enough to read it -- docks_signs_generated.hpp
// carries the 83 markers a mapper already authored in docks_surface.tmx,
// docks_signs.hpp resolves what each one actually SAYS (see its own header
// for the priority order), and this file projects that label into screen
// space and draws it.
//
// REUSES THE CAMERA MATH, DELIBERATELY. The projection here is the exact
// shape WorldRenderer::drawSprite already uses for a billboard: a world
// offset dotted against the camera's forward/right axes gives distance and
// lateral offset, and `projectionFor`'s focal/horizon turn those into a
// screen point. A second, independently-derived formula is exactly how a
// sign would end up standing in a different place than the wall behind it.
//
// OCCLUSION REUSES THE DEPTH BUFFER THE WORLD PASS ALREADY WROTE, rather than
// a second visibility system: a sign draws only when the pixel it projects
// to is either unwritten-behind-it (world geometry the same distance or
// farther) -- never when the world pass painted raw sky there (the building
// is not in view at all from here) or something nearer (a wall in front of
// it).

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/world_renderer.hpp"

namespace granadad::render {

struct SignageSettings {
    /// Closer than this and the label is behind the eye or pressed against
    /// the camera plane -- the same near-cull drawSprite uses.
    float minDistance = 0.5F;
    /// Past this, a label is unreadable noise rather than information, so it
    /// is not drawn at all. Well inside RenderSettings::maxDistance: fog
    /// closes the WORLD in by then, but a name tag needs to stop being
    /// legible well before the wall it is nailed to disappears into it.
    float maxDistance = 16.0F;
    /// Alpha eases from 1 to 0 across this last stretch before maxDistance,
    /// so a sign disappears as you back away rather than popping off.
    float fadeSpan = 5.0F;
};

/// Draws every Docks sign that is in range, facing the right way to be seen,
/// and not occluded, as a floating nameplate near its door or street post.
///
/// Call AFTER WorldRenderer::renderFrame and BEFORE the HUD: it reads the
/// depth buffer renderFrame just wrote, and it is world content, not
/// interface, so the HUD's edge-hugging rule (hud.hpp's own header) does not
/// apply to it and it must not be drawn after drawHud paints over it.
void drawSignage(Framebuffer& target, const Camera& camera,
                 const SignageSettings& settings = SignageSettings{});

}  // namespace granadad::render
