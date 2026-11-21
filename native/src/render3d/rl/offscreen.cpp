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

namespace {

/// A render target the scene is drawn into: a half-float colour texture (a
/// lamp's core is over one, the bloom takes what is over its threshold and
/// the filmic shoulder rolls the rest off) and, when asked, a 24-bit depth
/// renderbuffer. Falls back to raylib's own 8-bit target on a driver that
/// cannot attach one (the picture is then clipped at one, as it always was).
[[nodiscard]] RenderTexture2D loadTarget(int w, int h, bool withDepth) {
    RenderTexture2D rt{};
    rt.id = rlLoadFramebuffer();
    if (rt.id != 0) {
        const unsigned int colour = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
        rt.texture = Texture2D{colour, w, h, 1, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16};
        rlFramebufferAttach(rt.id, colour, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        if (withDepth) {
            const unsigned int depthBuffer = rlLoadTextureDepth(w, h, true);
            rt.depth = Texture2D{depthBuffer, w, h, 1, 19};
            rlFramebufferAttach(rt.id, depthBuffer, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        }
        if (rlFramebufferComplete(rt.id)) {
            return rt;
        }
        UnloadRenderTexture(rt);
        rt = RenderTexture2D{};
    }
    return withDepth ? LoadRenderTexture(w, h) : RenderTexture2D{};
}

}  // namespace

void Backend::Impl::ensureTarget(int w, int h) {
    if (target.id != 0 && target.texture.width == w && target.texture.height == h) {
        return;
    }
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = RenderTexture2D{};
    }
    if (bloomA.id != 0) {
        UnloadRenderTexture(bloomA);
        bloomA = RenderTexture2D{};
    }
    if (bloomB.id != 0) {
        UnloadRenderTexture(bloomB);
        bloomB = RenderTexture2D{};
    }
    target = loadTarget(w, h, true);
    // Bilinear, clamped: the post pass draws it one texel to a pixel, so it is
    // exact there, and the bloom's down-sample reads it between texels.
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
    // The bloom works at a quarter of the resolution.
    const int qw = std::max(1, w / 4);
    const int qh = std::max(1, h / 4);
    bloomA = loadTarget(qw, qh, false);
    bloomB = loadTarget(qw, qh, false);
    for (RenderTexture2D* rt : {&bloomA, &bloomB}) {
        if (rt->id != 0) {
            SetTextureFilter(rt->texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(rt->texture, TEXTURE_WRAP_CLAMP);
        }
    }
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
    // THE BLOOM: what is over the threshold, taken at a quarter of the size,
    // blurred once each way (a nine-tap gaussian), added back in the post pass.
    // No readback: the blur is a texture the next pass samples.
    bool bloomed = false;
    if (shaded && shadingNow.bloom > 0.0F && bright.id != 0 && blur.id != 0 && bloomA.id != 0 &&
        bloomB.id != 0) {
        const float qw = static_cast<float>(bloomA.texture.width);
        const float qh = static_cast<float>(bloomA.texture.height);
        const Rectangle quarter{0.0F, 0.0F, qw, qh};
        const Rectangle quarterFlip{0.0F, 0.0F, qw, -qh};
        rlDrawRenderBatchActive();
        rlDisableColorBlend();
        {
            BeginTextureMode(bloomA);
            ClearBackground(BLACK);
            const float texel[2] = {1.0F / w, 1.0F / h};
            SetShaderValue(bright, brightTexel, texel, SHADER_UNIFORM_VEC2);
            SetShaderValue(bright, brightThreshold, &shadingNow.bloomThreshold, SHADER_UNIFORM_FLOAT);
            SetShaderValue(bright, brightExposure, &shadingNow.exposure, SHADER_UNIFORM_FLOAT);
            BeginShaderMode(bright);
            DrawTexturePro(target.texture, source, quarter, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
        {
            BeginTextureMode(bloomB);
            const float dir[2] = {1.0F / qw, 0.0F};
            SetShaderValue(blur, blurDirection, dir, SHADER_UNIFORM_VEC2);
            BeginShaderMode(blur);
            DrawTexturePro(bloomA.texture, quarterFlip, quarter, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
        {
            BeginTextureMode(bloomA);
            const float dir[2] = {0.0F, 1.0F / qh};
            SetShaderValue(blur, blurDirection, dir, SHADER_UNIFORM_VEC2);
            BeginShaderMode(blur);
            DrawTexturePro(bloomB.texture, quarterFlip, quarter, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
        rlEnableColorBlend();
        bloomed = true;
    }
    if (shaded) {
        SetShaderValue(post, postExposure, &shadingNow.exposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post, postVignette, &shadingNow.vignette, SHADER_UNIFORM_FLOAT);
        const float bloomStrength = bloomed ? shadingNow.bloom : 0.0F;
        SetShaderValue(post, postBloom, &bloomStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post, postTone, &shadingNow.toneCurve, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post, postSaturation, &shadingNow.saturation, SHADER_UNIFORM_FLOAT);
        SetShaderValue(post, postContrast, &shadingNow.contrast, SHADER_UNIFORM_FLOAT);
        const float tint[3] = {shadingNow.gradeTint.x, shadingNow.gradeTint.y, shadingNow.gradeTint.z};
        SetShaderValue(post, postTint, tint, SHADER_UNIFORM_VEC3);
        if (bloomed) {
            SetShaderValueTexture(post, postBloomTexture, bloomA.texture);
        }
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
