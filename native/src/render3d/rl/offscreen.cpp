#include "internal.hpp"

// THE OFFSCREEN SCENE AND THE POST PASS.
//
// The 3D pass no longer draws into the window's back buffer: beginFrame opens
// an offscreen target (colour texture + depth renderbuffer, the window's own
// render size), the scene and the hands draw into it, and the first thing that
// needs the window itself -- the HUD overlay, or the end of the frame --
// closes it and puts it on the window through the post shader. The HUD is
// drawn after that, straight onto the window, so it is never graded, never
// blurred and never scaled: the eighteen pixel-exact HUD/page test files stay
// exact because the overlay path is byte-for-byte what it was.

namespace granadad::render3d {

using namespace rl;

void Backend::Impl::ensureTarget(int w, int h) {
    if (target.id != 0 && target.texture.width == w && target.texture.height == h) {
        return;
    }
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = RenderTexture2D{};
    }
    target = LoadRenderTexture(w, h);
    // One texel per window pixel: nearest, so the post pass is exact.
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
}

void Backend::Impl::resolveScene() {
    if (!sceneOpen) {
        return;
    }
    sceneOpen = false;
    EndTextureMode();
    const float w = static_cast<float>(target.texture.width);
    const float h = static_cast<float>(target.texture.height);
    // A GL texture is bottom row first: the source rectangle's negative height
    // is the flip.
    const Rectangle source{0.0F, 0.0F, w, -h};
    const Rectangle dest{0.0F, 0.0F, w, h};
    const bool shaded = post.id != 0;
    if (shaded) {
        const float exposure = shadingNow.exposure;
        const float vignette = shadingNow.vignette;
        SetShaderValue(post, postExposure, &exposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post, postVignette, &vignette, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(post);
    }
    // Opaque: the frame replaces what the window held. Straight blend would
    // let the scene's own blended alpha through.
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    DrawTexturePro(target.texture, source, dest, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    if (shaded) {
        EndShaderMode();
    }
    // Back to raylib's own default for the overlay drawn next.
    shadingNow = SceneShading{};
}

}  // namespace granadad::render3d
