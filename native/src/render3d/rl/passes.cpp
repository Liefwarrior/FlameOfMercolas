#include "internal.hpp"

// THE 3D PASS: INSTANCES AND BODIES.

namespace granadad::render3d {

using namespace rl;

bool Backend::Impl::drawMeshWith(std::uint32_t meshId, std::uint32_t textureId, const Rgba8& tint,
                  const Matrix& transform, SceneStats& stats, std::uint8_t surface, bool plain) {
    const auto mesh = meshes.find(meshId);
    if (mesh == meshes.end()) {
        return false;
    }
    const CachedMesh& cached = mesh->second;
    // THE SHADOW PASS draws what the colour pass draws, through the depth
    // shader: nothing is counted, nothing is textured, and the sky, the water
    // and the far ring cast none.
    if (depthOverride != nullptr) {
        if (surface != kSurfaceLit) {
            return false;
        }
        const Shader keep = material.shader;
        material.shader = *depthOverride;
        DrawMesh(cached.mesh, material, transform);
        material.shader = keep;
        return true;
    }
    const bool litFrame = shadingNow.lightMix > 0.0F;
    material.maps[MATERIAL_MAP_DIFFUSE].color = colourOf(tint);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
    float cell[2] = {0.0F, 0.0F};
    if (textureId != 0) {
        const auto texture = textures.find(textureId);
        if (texture != textures.end()) {
            material.maps[MATERIAL_MAP_DIFFUSE].texture = texture->second.texture;
            cell[0] = texture->second.tileU;
            cell[1] = texture->second.tileV;
        }
    }
    // THE SKY: the dome through the sky shader, from the description's colours.
    if (surface == kSurfaceSky && sky.id != 0 && shadingNow.skyShader) {
        material.shader = sky;
        DrawMesh(cached.mesh, material, transform);
        material.shader = scene;
        ++stats.instancesDrawn;
        stats.trianglesDrawn += static_cast<std::size_t>(cached.mesh.triangleCount);
        return true;
    }
    // THE HARBOUR: through the water shader, lit by the lights that reach it.
    if (surface == kSurfaceWater && water.id != 0 && litFrame) {
        setDrawLights(water, waterLit, cached.lo, cached.hi);
        material.shader = water;
        DrawMesh(cached.mesh, material, transform);
        material.shader = scene;
        ++stats.instancesDrawn;
        stats.trianglesDrawn += static_cast<std::size_t>(cached.mesh.triangleCount);
        return true;
    }
    // A tiling atlas tells the scene shader the size of one cell; every
    // other texture leaves it at zero and is sampled as it always was.
    if (scene.id != 0 && sceneAtlasCell >= 0) {
        SetShaderValue(scene, sceneAtlasCell, cell, SHADER_UNIFORM_VEC2);
    }
    // The skyline ring and the hands wear their own colours: no light, no fog.
    const bool unlit = litFrame && (surface == kSurfaceFar || plain);
    if (scene.id != 0 && litFrame) {
        if (unlit) {
            const float on = 1.0F;
            SetShaderValue(scene, sceneLit.unlit, &on, SHADER_UNIFORM_FLOAT);
        } else {
            // Bounds in scene space: the mesh's own, moved by the instance's
            // transform (the identity for a chunk; a placeholder body is
            // turned and scaled, so its box is the sphere round its middle).
            const Vector3 middle = Vector3Transform(
                Vector3{0.5F * (cached.lo.x + cached.hi.x), 0.5F * (cached.lo.y + cached.hi.y),
                        0.5F * (cached.lo.z + cached.hi.z)},
                transform);
            const float spanX = cached.hi.x - cached.lo.x;
            const float spanY = cached.hi.y - cached.lo.y;
            const float spanZ = cached.hi.z - cached.lo.z;
            const float scaleOf = std::sqrt(transform.m0 * transform.m0 + transform.m1 * transform.m1 +
                                            transform.m2 * transform.m2);
            const float radius = 0.5F * std::sqrt(spanX * spanX + spanY * spanY + spanZ * spanZ) *
                                 std::max(scaleOf, 0.0001F);
            const bool identity = std::abs(scaleOf - 1.0F) < 1e-4F && std::abs(transform.m12) < 1e-4F &&
                                  std::abs(transform.m13) < 1e-4F && std::abs(transform.m14) < 1e-4F;
            if (identity) {
                setDrawLights(scene, sceneLit, cached.lo, cached.hi);
            } else {
                setDrawLights(scene, sceneLit, Vec3{middle.x - radius, middle.y - radius, middle.z - radius},
                              Vec3{middle.x + radius, middle.y + radius, middle.z + radius});
            }
        }
    }
    DrawMesh(cached.mesh, material, transform);
    if (unlit && scene.id != 0) {
        const float off = 0.0F;
        SetShaderValue(scene, sceneLit.unlit, &off, SHADER_UNIFORM_FLOAT);
    }
    // The cell goes back to zero: a glTF piece drawn after a chunk through the
    // same shader must not be sampled as a tiling atlas.
    if (scene.id != 0 && sceneAtlasCell >= 0 && cell[0] > 0.0F) {
        const float none[2] = {0.0F, 0.0F};
        SetShaderValue(scene, sceneAtlasCell, none, SHADER_UNIFORM_VEC2);
    }
    ++stats.instancesDrawn;
    stats.trianglesDrawn += static_cast<std::size_t>(cached.mesh.triangleCount);
    return true;
}

bool Backend::Impl::drawInstance(const Instance& instance, SceneStats& stats) {
    // Scale, then yaw, then place. Yaw is clockwise-from-above in the
    // description and raylib's Y rotation is counter-clockwise, hence
    // the sign -- see scene.hpp on the frame.
    const Matrix transform = MatrixMultiply(
        MatrixMultiply(MatrixScale(instance.scale, instance.scale, instance.scale),
                       MatrixRotateY(-instance.yaw)),
        MatrixTranslate(instance.position.x, instance.position.y, instance.position.z));
    return drawMeshWith(instance.meshId, instance.textureId, instance.tint, transform, stats,
                        instance.surface);
}

void Backend::Impl::drawActor(const ActorInstance& actor, SceneStats& stats) {
    RigModel* rig = rigFor(actor.rig);
    if (rig == nullptr || !rig->loaded) {
        if (drawInstance(actor.instance, stats)) {
            ++stats.actorsDrawn;
        }
        return;
    }
    const int clipIndex = static_cast<int>(actor.clip);
    if (actor.skinned && clipIndex < rig->clipCount) {
        const ModelAnimation& clip = rig->clips[clipIndex];
        if (clip.keyframeCount > 0) {
            // A looping clip wraps (raylib does the modulo); a one-shot
            // holds its last keyframe -- a corpse stays down.
            const auto length = static_cast<std::uint32_t>(clip.keyframeCount);
            const std::uint32_t frame = actorClipOneShot(actor.clip)
                                            ? std::min(actor.clipFrame, length - 1U)
                                            : actor.clipFrame % length;
            UpdateModelAnimation(rig->model, clip, static_cast<float>(frame));
            rig->posed = true;
            ++stats.actorsSkinned;
        }
    } else if (rig->posed && rig->clipCount > 0) {
        // The far draw: the rest pose, put back once and shared by every
        // unskinned body of this kind after it (they are drawn after the
        // skinned ones -- see drawScene).
        UpdateModelAnimation(rig->model, rig->clips[0], 0.0F);
        rig->posed = false;
    }
    const Instance& instance = actor.instance;
    // A glb rig draws through raylib's own default shader unless it is given
    // ours for the draw: the depth shader in the shadow pass, the scene shader
    // (lit by the lights round the body, fogged) in the colour pass. Put back
    // after, so UnloadModel never frees a shader that is shared.
    std::vector<Shader> kept;
    const Shader* wear = depthOverride != nullptr ? depthOverride : (scene.id != 0 ? &scene : nullptr);
    if (wear != nullptr) {
        for (int i = 0; i < rig->model.materialCount; ++i) {
            kept.push_back(rig->model.materials[i].shader);
            rig->model.materials[i].shader = *wear;
        }
        if (depthOverride == nullptr) {
            const float reach = 1.6F * instance.scale + 0.5F;
            setDrawLights(scene, sceneLit,
                          Vec3{instance.position.x - reach, instance.position.y - 0.5F, instance.position.z - reach},
                          Vec3{instance.position.x + reach, instance.position.y + 2.5F * instance.scale + 0.5F,
                               instance.position.z + reach});
        }
    }
    DrawModelEx(rig->model, vec(instance.position), Vector3{0.0F, 1.0F, 0.0F},
                -(instance.yaw + kGltfForwardYaw) * kRadToDeg,
                Vector3{instance.scale, instance.scale, instance.scale},
                colourOf(instance.tint));
    for (std::size_t i = 0; i < kept.size(); ++i) {
        rig->model.materials[i].shader = kept[i];
    }
    if (depthOverride != nullptr) {
        return;
    }
    ++stats.actorsDrawn;
    ++stats.instancesDrawn;
    for (int i = 0; i < rig->model.meshCount; ++i) {
        stats.trianglesDrawn += static_cast<std::size_t>(rig->model.meshes[i].triangleCount);
    }
}

SceneStats Backend::drawScene(const SceneDescription& scene) {
    SceneStats stats;
    Impl& impl = *impl_;
    impl.ensureMaterial();
    impl.applyShading(scene.shading);

    // Uploads first, outside the 3D pass: a mesh whose version moved is
    // replaced whole (a recolour is a re-upload; cheap at chunk size).
    for (const MeshData& data : scene.meshes) {
        if (data.vertexCount() == 0 || data.triangleCount() == 0) {
            continue;
        }
        auto found = impl.meshes.find(data.id);
        if (found != impl.meshes.end() && found->second.version == data.version) {
            continue;
        }
        if (found != impl.meshes.end()) {
            UnloadMesh(found->second.mesh);
            impl.meshes.erase(found);
        }
        CachedMesh cached;
        cached.version = data.version;
        cached.mesh = uploadMesh(data);
        {
            Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()};
            Vec3 hi{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()};
            for (std::size_t v = 0; v + 2 < data.positions.size(); v += 3) {
                lo.x = std::min(lo.x, data.positions[v]);
                lo.y = std::min(lo.y, data.positions[v + 1]);
                lo.z = std::min(lo.z, data.positions[v + 2]);
                hi.x = std::max(hi.x, data.positions[v]);
                hi.y = std::max(hi.y, data.positions[v + 1]);
                hi.z = std::max(hi.z, data.positions[v + 2]);
            }
            cached.lo = lo;
            cached.hi = hi;
        }
        impl.meshes.emplace(data.id, cached);
        ++stats.meshesUploaded;
    }
    for (const TextureData& data : scene.textures) {
        if (data.width <= 0 || data.height <= 0 ||
            data.pixels.size() != static_cast<std::size_t>(data.width) *
                                      static_cast<std::size_t>(data.height) * 4U) {
            continue;
        }
        auto found = impl.textures.find(data.id);
        if (found != impl.textures.end() && found->second.version == data.version) {
            continue;
        }
        if (found != impl.textures.end()) {
            UnloadTexture(found->second.texture);
            impl.textures.erase(found);
        }
        CachedTexture cached;
        cached.version = data.version;
        cached.texture = uploadTexture(data);
        cached.tileU = data.tileU;
        cached.tileV = data.tileV;
        impl.textures.emplace(data.id, cached);
        ++stats.texturesUploaded;
    }

    // PHASE 5: the lights the draws pick from, the shadow map the sun or the
    // moon draws first, and the uniforms every lit shader shares.
    impl.sceneNow = &scene;
    impl.eyeNow = scene.camera.position;
    impl.ensureBlendShader();
    impl.renderShadows(scene);
    impl.applyShadows();

    Camera3D camera{};
    camera.position = vec(scene.camera.position);
    camera.target = vec(scene.camera.target);
    camera.up = vec(scene.camera.up);
    camera.fovy = scene.camera.fovyDegrees;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginMode3D(camera);
    for (const Instance& instance : scene.instances) {
        impl.drawInstance(instance, stats);
    }
    // The building pieces over the chunks, opaque now, glass and water held
    // back until the people are in.
    impl.ensureBlendShader();
    impl.deferred.clear();
    impl.halos.clear();
    impl.lateGlass.clear();
    for (const StaticInstance& piece : scene.statics) {
        impl.drawStatic(piece, scene, stats);
    }
    // The people, after the world: the skinned (near) bodies first so a
    // rig's one shared model is posed per body and then put back to rest
    // ONCE for every far body of its kind. Draw order within each set is
    // the description's own -- the depth buffer sorts the picture.
    for (const ActorInstance& actor : scene.actors) {
        if (actor.skinned) {
            impl.drawActor(actor, stats);
        }
    }
    for (const ActorInstance& actor : scene.actors) {
        if (!actor.skinned) {
            impl.drawActor(actor, stats);
        }
    }
    for (const DeferredMesh& late : impl.deferred) {
        impl.drawStaticMesh(*late.model, late.mesh, late.transform, late.tints, late.mirrored, stats);
    }
    impl.deferred.clear();
    // WEATHER: the veils, last of all inside the pass -- translucent shells
    // round the eye through the same mesh path as any instance (vertex
    // alpha, straight over, the depth test still on), so what stands inside
    // a shell is clear of it and what stands beyond is seen through it.
    // After the people and the glass so a body in the fog is in the fog.
    // The veils write no depth: the halos and the bright glass below are
    // depth-tested against the WORLD, not against a shell of fog round the
    // eye (a shell that wrote depth culled every halo past it, and in a fog
    // the lamps went out before the walls they lit -- the round-two
    // player's fourth).
    rlDisableDepthMask();
    for (const Instance& veil : scene.veils) {
        impl.drawInstance(veil, stats);
    }
    // The bright glass once more at the fog's share of it
    // (StaticInstance::pane.a), so a lamp's light is seen further into a
    // fog than the wall round it -- and THEN the halos, over the glass as
    // over everything (the round-three auditor's first: glass drawn after
    // the halos cut a pane-shaped hole in every glow).
    for (const DeferredMesh& late : impl.lateGlass) {
        impl.drawLateGlass(late, stats);
    }
    impl.lateGlass.clear();
    for (const DeferredMesh& glow : impl.halos) {
        impl.drawStaticMesh(*glow.model, glow.mesh, glow.transform, glow.tints, glow.mirrored, stats);
    }
    impl.halos.clear();
    rlEnableDepthMask();
    EndMode3D();
    // The hands, last, in their own pass over everything.
    impl.drawViewmodel(scene.viewmodel, stats);
    impl.sceneNow = nullptr;
    stats.rigModelsLoaded = impl.rigsLoaded();
    stats.staticModelsLoaded = impl.staticsLoaded();
    // The material never keeps hold of a cached texture between passes:
    // UnloadMaterial would otherwise free it a second time at teardown.
    impl.material.maps[MATERIAL_MAP_DIFFUSE].texture = impl.defaultTexture;
    impl.material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    return stats;
}

void Backend::drawOverlay(const render::Framebuffer& overlay) {
    Impl& impl = *impl_;
    impl.resolveScene();
    const int width = overlay.width();
    const int height = overlay.height();
    if (impl.overlay.id == 0 || impl.overlayWidth != width || impl.overlayHeight != height) {
        if (impl.overlay.id != 0) {
            UnloadTexture(impl.overlay);
        }
        Image image{};
        image.data = const_cast<std::uint32_t*>(overlay.pixels().data());
        image.width = width;
        image.height = height;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        impl.overlay = LoadTextureFromImage(image);
        // Nearest neighbour, always. The chunkiness is the art direction.
        SetTextureFilter(impl.overlay, TEXTURE_FILTER_POINT);
        impl.overlayWidth = width;
        impl.overlayHeight = height;
    } else {
        UpdateTexture(impl.overlay, overlay.pixels().data());
    }

    const OverlayPlacement placement = overlayPlacement(width, height);
    const Rectangle source{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
    const Rectangle dest{static_cast<float>(placement.offsetX),
                         static_cast<float>(placement.offsetY),
                         static_cast<float>(width * placement.scale),
                         static_cast<float>(height * placement.scale)};
    // Straight alpha over the scene: raylib's default blend mode.
    DrawTexturePro(impl.overlay, source, dest, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
}

}  // namespace granadad::render3d
