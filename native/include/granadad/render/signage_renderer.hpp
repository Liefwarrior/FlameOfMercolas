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

#include <string_view>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
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
    /// A SCREEN RECTANGLE THE INTERFACE HAS ALREADY CLAIMED, which a label
    /// must not land on. Empty by default, so nothing changes for a caller
    /// that does not pass one.
    ///
    /// This file has always had an anti-overlap rule -- nearest first, and a
    /// label whose box collides with one already placed is skipped rather than
    /// shoved. It just could not see the HUD, which is drawn AFTER it and
    /// therefore wins every argument by painting over: on the street the ward's
    /// own "TARWALK" and the crosshair's "E - TALK" landed on the same pixels
    /// and the result was a sign cut through mid-word by a prompt. Seeding the
    /// same rule with the prompt's own rectangle (see hudAimPromptRect) makes
    /// the interface just another box the signs place around, which is the
    /// answer this file already knew for two signs.
    ///
    /// THE PROMPT'S BOX, NOT THE WHOLE FENCE. hudAimRect spans the entire
    /// right half of the centre band; excluding all of it would drop signs that
    /// were never in the prompt's way.
    CentreRect exclusion{0, 0, 0, 0};
    /// THE BUILDING THE CROSSHAIR IS ALREADY NAMING, so the ward does not name
    /// it twice in the same breath.
    ///
    /// Two frames of the demo caught this exactly -- `night-beacon` and
    /// `quay-turret` both read `MERLE'S BOATS` as a world sign and `MERLE'S
    /// BOATS / E - LOOK` as the prompt two rows under it. The anti-overlap rule
    /// above cannot help: the two do not collide, they are neighbours, and
    /// neighbours saying the same word is worse than an overlap because it
    /// reads as a bug rather than as clutter.
    ///
    /// THE SIGN IS THE ONE THAT YIELDS, and the prompt stays. The sign exists
    /// to answer "what is that over there" at a distance; the prompt answers
    /// "what is this and what key opens it", carries a VERB and a KEY the sign
    /// does not have, and is the one the player summoned by aiming. When both
    /// are on screen the prompt is strictly the better of the two, so the sign
    /// steps back for exactly as long as the prompt is naming its building --
    /// and is back the moment the crosshair moves off, which is the one frame
    /// the player needs it again.
    ///
    /// Empty suppresses nothing, so a caller that does not pass one is
    /// pixel-identical to before this existed.
    std::string_view namedByPrompt;
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
