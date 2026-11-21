#include "internal.hpp"

// PHASE 5: THE SHADOW MAP.
//
// The sun (or the moon, when the sun is down) draws the district into a depth
// texture fitted to the tiles round the eye, snapped to the texel so a shadow
// does not shimmer as you walk; the scene shaders then look a pixel up in it
// (shaders.cpp, shadowTerm). The pass reuses the colour pass's own draw calls
// through `depthOverride`, so what casts a shadow is exactly what is drawn: the
// chunk meshes and roofs, the opaque pieces, the people. Glass, water, the
// sky, the halos and the weather cast none.
//
// SHADOWS ARE COSMETIC (OBLIVION-PLAN 6.3; ruling 9). The sim's integer light
// field decides who sees whom; nothing here is read back, and a dark patch
// under an eave is not cover.

namespace granadad::render3d {

using namespace rl;

namespace {

[[nodiscard]] Vector3 cross3(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] float dot3(const Vector3& a, const Vector3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] Vector3 norm3(const Vector3& a) noexcept {
    const float l = std::sqrt(dot3(a, a));
    return l > 1e-6F ? Vector3{a.x / l, a.y / l, a.z / l} : Vector3{0.0F, 1.0F, 0.0F};
}

}  // namespace

void Backend::Impl::ensureShadowMap() {
    if (shadowReady) {
        return;
    }
    shadowReady = true;
    const int size = shadowSize;
    const unsigned int fbo = rlLoadFramebuffer();
    if (fbo == 0) {
        return;
    }
    const unsigned int colour = rlLoadTexture(nullptr, size, size, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    const unsigned int depthTex = rlLoadTextureDepth(size, size, false);
    rlFramebufferAttach(fbo, colour, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(fbo, depthTex, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(fbo)) {
        std::printf("granadad: render3d: the shadow map's framebuffer is not complete; no shadows\n");
        rlUnloadFramebuffer(fbo);
        rlUnloadTexture(colour);
        rlUnloadTexture(depthTex);
        return;
    }
    shadowTarget.id = fbo;
    shadowTarget.texture = Texture2D{colour, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    shadowTarget.depth = Texture2D{depthTex, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};  // only ever freed by id
    // The depth texture is sampled with a manual compare: nearest, clamped.
    rlTextureParameters(depthTex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(depthTex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(depthTex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(depthTex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
}

void Backend::Impl::drawStaticCaster(const StaticInstance& piece, const SceneDescription& sc) {
    if (piece.piece >= sc.pieces.size() || piece.mode == kDrawHalo || piece.mode == kDrawPaneRamp ||
        piece.tint.a < 255) {
        return;
    }
    StaticModel* model = staticFor(sc.pieces[piece.piece]);
    if (model == nullptr || !model->loaded) {
        return;
    }
    const Matrix full = MatrixMultiply(model->model.transform, staticTransformOf(piece));
    const bool mirrored = piece.scale.x * piece.scale.y * piece.scale.z < 0.0F;
    for (int i = 0; i < model->model.meshCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const int m = model->model.meshMaterial[i];
        if (m < 0 || m >= model->model.materialCount || model->translucent[index] || model->glass[index]) {
            continue;
        }
        Material& material = model->model.materials[m];
        const Shader keep = material.shader;
        material.shader = *depthOverride;
        if (mirrored) {
            rlDisableBackfaceCulling();
        }
        DrawMesh(model->model.meshes[i], material, full);
        if (mirrored) {
            rlEnableBackfaceCulling();
        }
        material.shader = keep;
    }
}

void Backend::Impl::renderShadows(const SceneDescription& sc) {
    shadowsOn = false;
    const SceneShading& shading = sc.shading;
    const float strength = shading.sunColour.x + shading.sunColour.y + shading.sunColour.z;
    if (!(shading.shadowStrength > 0.0F) || !(shading.lightMix > 0.0F) || !(strength > 0.02F) ||
        depth.id == 0 || scene.id == 0) {
        return;
    }
    ensureShadowMap();
    if (shadowTarget.id == 0) {
        return;
    }
    const float reach = std::max(8.0F, shading.shadowReach);
    // The light's own frame: it looks along -L at the eye, up is the world's
    // (or +Z when the light stands straight over).
    const Vector3 toLight = norm3(Vector3{shading.sunDirection.x, shading.sunDirection.y, shading.sunDirection.z});
    if (toLight.y < 0.04F) {
        return;  // a light on the horizon casts a shadow of nothing useful
    }
    const Vector3 forward = Vector3{-toLight.x, -toLight.y, -toLight.z};
    Vector3 up{0.0F, 1.0F, 0.0F};
    if (std::abs(dot3(forward, up)) > 0.98F) {
        up = Vector3{0.0F, 0.0F, 1.0F};
    }
    const Vector3 right = norm3(cross3(forward, up));
    const Vector3 trueUp = cross3(right, forward);
    // The eye, snapped to the texel along the light's own right and up.
    Vector3 centre = vec(sc.camera.position);
    const float texel = 2.0F * reach / static_cast<float>(shadowSize);
    const float a = dot3(centre, right);
    const float b = dot3(centre, trueUp);
    const float da = std::floor(a / texel) * texel - a;
    const float db = std::floor(b / texel) * texel - b;
    centre = Vector3{centre.x + right.x * da + trueUp.x * db, centre.y + right.y * da + trueUp.y * db,
                     centre.z + right.z * da + trueUp.z * db};
    // Back along the light far enough to stand over the tallest roof round the
    // eye, and a depth range that holds all of it.
    const float back = reach + 60.0F;
    Camera3D light{};
    light.position = Vector3{centre.x + toLight.x * back, centre.y + toLight.y * back, centre.z + toLight.z * back};
    light.target = centre;
    light.up = up;
    light.fovy = 2.0F * reach;  // an orthographic camera's fovy is its height in world units
    light.projection = CAMERA_ORTHOGRAPHIC;

    // Out of the colour pass's own target and into the shadow map, and back.
    if (sceneOpen) {
        EndTextureMode();
    }
    BeginTextureMode(shadowTarget);
    rlClearColor(255, 255, 255, 255);
    rlClearScreenBuffers();
    rlSetClipPlanes(1.0, static_cast<double>(back * 2.0F + 40.0F));
    BeginMode3D(light);
    lightViewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    depthOverride = &depth;
    SceneStats scratch;
    const Vec3 eye = sc.camera.position;
    const float reachSq = (reach + 30.0F) * (reach + 30.0F);
    for (const Instance& instance : sc.instances) {
        if (instance.surface != kSurfaceLit || instance.meshId < kChunkMeshIdBase ||
            instance.meshId >= kActorMeshIdBase) {
            continue;
        }
        const auto found = meshes.find(instance.meshId);
        if (found == meshes.end()) {
            continue;
        }
        // The chunk's own bounds against the eye: a chunk past the map's reach
        // casts into nothing the map covers.
        const CachedMesh& cached = found->second;
        const float dx = std::max({cached.lo.x - eye.x, eye.x - cached.hi.x, 0.0F});
        const float dz = std::max({cached.lo.z - eye.z, eye.z - cached.hi.z, 0.0F});
        if (dx * dx + dz * dz > reachSq) {
            continue;
        }
        drawInstance(instance, scratch);
    }
    for (const StaticInstance& piece : sc.statics) {
        const float dx = piece.position.x - eye.x;
        const float dz = piece.position.z - eye.z;
        if (dx * dx + dz * dz > reachSq) {
            continue;
        }
        drawStaticCaster(piece, sc);
    }
    for (const ActorInstance& actor : sc.actors) {
        const float dx = actor.instance.position.x - eye.x;
        const float dz = actor.instance.position.z - eye.z;
        if (dx * dx + dz * dz > reachSq) {
            continue;
        }
        drawActor(actor, scratch);
    }
    depthOverride = nullptr;
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(0.1, 512.0);
    if (sceneOpen) {
        // Back into the colour pass, whose contents are kept.
        BeginTextureMode(target);
    }
    // The map to its slot, for the shaders that sample it.
    rlActiveTextureSlot(kShadowSlot);
    rlEnableTexture(shadowTarget.depth.id);
    rlActiveTextureSlot(0);
    shadowsOn = true;
}

}  // namespace granadad::render3d
