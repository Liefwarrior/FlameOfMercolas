#include "internal.hpp"

// S LANE: DRAWING A STATIC BUILDING PIECE.

namespace granadad::render3d {

using namespace rl;

void Backend::Impl::drawStatic(const StaticInstance& piece, const SceneDescription& scene,
                SceneStats& stats) {
    if (piece.piece >= scene.pieces.size()) {
        ++stats.staticsMissing;
        return;
    }
    StaticModel* model = staticFor(scene.pieces[piece.piece]);
    if (model == nullptr || !model->loaded) {
        ++stats.staticsMissing;
        return;
    }
    // Scale, roll about the piece's own Z, pitch about its X, yaw, place.
    const Matrix transform = MatrixMultiply(
        MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(piece.scale.x, piece.scale.y, piece.scale.z),
                                                     MatrixRotateZ(piece.roll)),
                                      MatrixRotateX(piece.pitch)),
                       MatrixRotateY(-piece.yaw)),
        MatrixTranslate(piece.position.x, piece.position.y, piece.position.z));
    const Matrix full = MatrixMultiply(model->model.transform, transform);
    // A mirrored piece (one negative scale axis: a ceiling quad laid as
    // a floor, a flat quad drawn both sides) has its winding reversed,
    // so it is drawn both-sided.
    const bool mirrored = piece.scale.x * piece.scale.y * piece.scale.z < 0.0F;
    PieceTints tints;
    tints.a = colourOf(piece.tint);
    tints.b = colourOf(piece.tint2);
    tints.c = colourOf(piece.tint3);
    tints.d = colourOf(piece.tint4);
    tints.from = piece.gradientFrom;
    tints.to = piece.gradientTo;
    tints.fromZ = piece.gradientFromZ;
    tints.toZ = piece.gradientToZ;
    tints.pane = colourOf(piece.pane);
    tints.mode = piece.mode;
    // A halo is ONE fan standing in for whatever the flame model holds,
    // held back with the glass so it draws over the lantern's own pane
    // and over the people: one deferred entry, never one per sub-mesh.
    if (tints.mode == kDrawHalo) {
        halos.push_back(DeferredMesh{model, 0, full, tints, mirrored});
        ++stats.staticsDrawn;
        ++stats.instancesDrawn;
        return;
    }
    // A timber pane is its ramp quad and nothing else: opaque, drawn
    // now, depth written, so the halo's depth test and the frame's own
    // panels meet it where a pane stands.
    if (tints.mode == kDrawPaneRamp) {
        drawPaneRamp(full, tints, stats);
        if (tints.pane.a < kLateGlassBelow) {
            lateGlass.push_back(DeferredMesh{model, -1, full, tints, mirrored});
        }
        ++stats.staticsDrawn;
        ++stats.instancesDrawn;
        return;
    }
    // A translucent instance (a lit pane, the water) is held back whole,
    // like a pane of glass.
    const bool seeThrough = piece.tint.a < 255;
    for (int i = 0; i < model->model.meshCount; ++i) {
        if (model->translucent[static_cast<std::size_t>(i)] || seeThrough) {
            deferred.push_back(DeferredMesh{model, i, full, tints, mirrored});
            continue;
        }
        drawStaticMesh(*model, i, full, tints, mirrored, stats);
    }
    ++stats.staticsDrawn;
    ++stats.instancesDrawn;
}

void Backend::Impl::drawHaloFan(const Matrix& transform, const PieceTints& tints, SceneStats& stats) {
    const float halfW = 0.5F * (tints.to - tints.from);
    const float halfH = 0.5F * (tints.toZ - tints.fromZ);
    if (halfW <= 0.0F || halfH <= 0.0F) {
        return;
    }
    ensureMaterial();
    ensureHaloFan();
    const Matrix fit = MatrixMultiply(MatrixScale(halfW, halfH, 1.0F),
                                      MatrixTranslate(0.5F * (tints.from + tints.to),
                                                      0.5F * (tints.fromZ + tints.toZ), 0.0F));
    const Color glow = averageColour(tints);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
    material.maps[MATERIAL_MAP_DIFFUSE].color = glow;
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(haloFan, material, MatrixMultiply(fit, transform));
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    stats.trianglesDrawn += static_cast<std::size_t>(haloFan.triangleCount);
}

void Backend::Impl::drawPaneRamp(const Matrix& transform, const PieceTints& tints, SceneStats& stats) {
    const float w = tints.to - tints.from;
    const float h = tints.toZ - tints.fromZ;
    if (w <= 0.0F || h <= 0.0F) {
        return;
    }
    ensureMaterial();
    ensurePaneRamp();
    const Matrix fit =
        MatrixMultiply(MatrixScale(w, h, 1.0F), MatrixTranslate(tints.from, tints.fromZ, 0.0F));
    const Rgba8 tint{tints.pane.r, tints.pane.g, tints.pane.b, 255};
    material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
    material.maps[MATERIAL_MAP_DIFFUSE].color = Color{tint.r, tint.g, tint.b, 255};
    rlDisableBackfaceCulling();
    DrawMesh(paneRamp, material, MatrixMultiply(fit, transform));
    rlEnableBackfaceCulling();
    material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    stats.trianglesDrawn += static_cast<std::size_t>(paneRamp.triangleCount);
}

void Backend::Impl::drawLateGlass(const DeferredMesh& late, SceneStats& stats) {
    ensureMaterial();
    const PieceTints& tints = late.tints;
    const Rgba8 pane{tints.pane.r, tints.pane.g, tints.pane.b, tints.pane.a};
    material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
    material.maps[MATERIAL_MAP_DIFFUSE].color = Color{pane.r, pane.g, pane.b, pane.a};
    // Never written: its own depth is already there from the opaque
    // pass, and drawHaloFan puts the mask back on after itself.
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    if (late.mesh < 0) {
        const float w = tints.to - tints.from;
        const float h = tints.toZ - tints.fromZ;
        if (w > 0.0F && h > 0.0F) {
            ensurePaneRamp();
            const Matrix fit = MatrixMultiply(MatrixScale(w, h, 1.0F),
                                              MatrixTranslate(tints.from, tints.fromZ, 0.0F));
            DrawMesh(paneRamp, material, MatrixMultiply(fit, late.transform));
            stats.trianglesDrawn += static_cast<std::size_t>(paneRamp.triangleCount);
        }
    } else if (late.model != nullptr && late.mesh < late.model->model.meshCount) {
        const Mesh& mesh = late.model->model.meshes[late.mesh];
        DrawMesh(mesh, material, late.transform);
        stats.trianglesDrawn += static_cast<std::size_t>(mesh.triangleCount);
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
}

void Backend::Impl::drawStaticMesh(StaticModel& model, int i, const Matrix& transform, const PieceTints& tints,
                    bool mirrored, SceneStats& stats) {
    // A halo does not draw the piece's mesh at all: the fan stands in
    // for it, once, whatever the flame model's own sub-meshes are.
    if (tints.mode == kDrawHalo) {
        drawHaloFan(transform, tints, stats);
        return;
    }
    const int m = model.model.meshMaterial[i];
    if (m < 0 || m >= model.model.materialCount) {
        return;
    }
    // The blend: the four tints over the piece when the shader is there
    // and they differ -- or the piece asks for shading, which only the
    // shader does -- their average when the blend shader did not load
    // or they do not. A halo never gets here.
    const bool spanned = tints.to > tints.from || tints.toZ > tints.fromZ;
    const bool blended =
        blend.id != 0 && ((spanned && !tints.uniform()) || tints.mode >= kDrawShaded);
    const Color tint = blended ? Color{255, 255, 255, tints.a.a} : averageColour(tints);
    const Color base = model.baseColour[static_cast<std::size_t>(m)];
    const auto ch = [](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>((static_cast<unsigned>(a) * static_cast<unsigned>(b) + 127U) /
                                          255U);
    };
    // Glass wears the pane tint the world scene decided -- dark by day
    // and over an unlit room, warm over a lit room at night -- flat,
    // never blended, and VERBATIM: not through the glass material's
    // own base colour, which on the camp lantern is a grey-green that
    // turned the night glass olive and dimmer than the cap over it
    // (the night-light critic's first). Opaque, too: a lit pane is its
    // own light, and a dark one is a dark pane, not a hole onto the
    // chunk wall behind the frame.
    const bool glass = model.glass[static_cast<std::size_t>(i)];
    model.model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color =
        glass ? Color{tints.pane.r, tints.pane.g, tints.pane.b, 255}
              : Color{ch(base.r, tint.r), ch(base.g, tint.g), ch(base.b, tint.b), ch(base.a, tint.a)};
    if (mirrored) {
        rlDisableBackfaceCulling();
    }
    // (The halo's own depth rule -- tested, never written, so the soft
    // rim of one glow cannot cut a hole through the next halo or a lit
    // pane behind it -- lives in drawHaloFan now, with the fan.)
    Material& material = model.model.materials[m];
    const Shader keep = material.shader;
    if (glass) {
        // Glass is a colour, not a texel: drawn on the backend's own
        // white texture so the pane tint is what shows, the ramp (if
        // the mesh carries one) multiplied in by the shader. Opaque
        // here; a pane alpha under 255 is the fog's share, for the
        // late pass (lateGlass) and not for this one.
        ensureMaterial();
        const Texture2D keepTexture = material.maps[MATERIAL_MAP_DIFFUSE].texture;
        material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
        if (mirrored) {
            rlDisableBackfaceCulling();
        }
        material.maps[MATERIAL_MAP_DIFFUSE].color = Color{tints.pane.r, tints.pane.g, tints.pane.b, 255};
        if (tints.pane.a < kLateGlassBelow) {
            lateGlass.push_back(DeferredMesh{&model, i, transform, tints, mirrored});
        }
        DrawMesh(model.model.meshes[i], material, transform);
        if (mirrored) {
            rlEnableBackfaceCulling();
        }
        material.maps[MATERIAL_MAP_DIFFUSE].texture = keepTexture;
        stats.trianglesDrawn += static_cast<std::size_t>(model.model.meshes[i].triangleCount);
        return;
    }
    if (blended && !glass) {
        // The material's own colour factor stays in colDiffuse; the
        // four lit tints go through the shader's own slots. An empty
        // span gets a zero factor: its blend stays at its first tint.
        material.shader = blend;
        const float a[4] = {channelOf(tints.a.r), channelOf(tints.a.g), channelOf(tints.a.b), 1.0F};
        const float b[4] = {channelOf(tints.b.r), channelOf(tints.b.g), channelOf(tints.b.b), 1.0F};
        const float c[4] = {channelOf(tints.c.r), channelOf(tints.c.g), channelOf(tints.c.b), 1.0F};
        const float d[4] = {channelOf(tints.d.r), channelOf(tints.d.g), channelOf(tints.d.b), 1.0F};
        const float span[4] = {tints.from, tints.to > tints.from ? 1.0F / (tints.to - tints.from) : 0.0F,
                               tints.fromZ,
                               tints.toZ > tints.fromZ ? 1.0F / (tints.toZ - tints.fromZ) : 0.0F};
        SetShaderValue(blend, blendTintA, a, SHADER_UNIFORM_VEC4);
        SetShaderValue(blend, blendTintB, b, SHADER_UNIFORM_VEC4);
        SetShaderValue(blend, blendTintC, c, SHADER_UNIFORM_VEC4);
        SetShaderValue(blend, blendTintD, d, SHADER_UNIFORM_VEC4);
        SetShaderValue(blend, blendSpan, span, SHADER_UNIFORM_VEC4);
        const int mode = static_cast<int>(tints.mode);
        SetShaderValue(blend, blendMode, &mode, SHADER_UNIFORM_INT);
    }
    DrawMesh(model.model.meshes[i], material, transform);
    material.shader = keep;
    if (mirrored) {
        rlEnableBackfaceCulling();
    }
    stats.trianglesDrawn += static_cast<std::size_t>(model.model.meshes[i].triangleCount);
}

}  // namespace granadad::render3d
