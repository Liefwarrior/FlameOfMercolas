#include "internal.hpp"

// THE 3D PASS: INSTANCES AND BODIES.

namespace granadad::render3d {

using namespace rl;

bool Backend::Impl::drawMeshWith(std::uint32_t meshId, std::uint32_t textureId, const Rgba8& tint,
                  const Matrix& transform, SceneStats& stats) {
    const auto mesh = meshes.find(meshId);
    if (mesh == meshes.end()) {
        return false;
    }
    material.maps[MATERIAL_MAP_DIFFUSE].color = colourOf(tint);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
    if (textureId != 0) {
        const auto texture = textures.find(textureId);
        if (texture != textures.end()) {
            material.maps[MATERIAL_MAP_DIFFUSE].texture = texture->second.texture;
        }
    }
    DrawMesh(mesh->second.mesh, material, transform);
    ++stats.instancesDrawn;
    stats.trianglesDrawn += static_cast<std::size_t>(mesh->second.mesh.triangleCount);
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
    return drawMeshWith(instance.meshId, instance.textureId, instance.tint, transform, stats);
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
    DrawModelEx(rig->model, vec(instance.position), Vector3{0.0F, 1.0F, 0.0F},
                -(instance.yaw + kGltfForwardYaw) * kRadToDeg,
                Vector3{instance.scale, instance.scale, instance.scale},
                colourOf(instance.tint));
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
        impl.textures.emplace(data.id, cached);
        ++stats.texturesUploaded;
    }

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
