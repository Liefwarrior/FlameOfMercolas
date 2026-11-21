#include "internal.hpp"

// MESHES AND TEXTURES ON THE GPU, THE DEFAULT MATERIAL, AND TEARDOWN.

namespace granadad::render3d {

using namespace rl;

namespace rl {
Mesh uploadMesh(const MeshData& data) {
    Mesh mesh{};
    const std::size_t vertexCount = data.vertexCount();
    const std::size_t triangleCount = data.triangleCount();
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(triangleCount);

    mesh.vertices = rlAlloc<float>(vertexCount * 3);
    std::memcpy(mesh.vertices, data.positions.data(), vertexCount * 3 * sizeof(float));

    // Always present: the shader samples texture0 (a 1x1 white when the
    // instance is untextured) at these coordinates.
    mesh.texcoords = rlAlloc<float>(vertexCount * 2);
    if (data.texcoords.size() == vertexCount * 2) {
        std::memcpy(mesh.texcoords, data.texcoords.data(), vertexCount * 2 * sizeof(float));
    }

    // The second texture coordinate of a mesh on a tiling atlas (attribute 5,
    // vertexTexCoord2): the atlas cell its surface lives in. Absent is
    // raylib's own default (zero), which the shader reads only when the
    // bound texture is a tiling atlas.
    if (data.texcoords2.size() == vertexCount * 2) {
        mesh.texcoords2 = rlAlloc<float>(vertexCount * 2);
        std::memcpy(mesh.texcoords2, data.texcoords2.data(), vertexCount * 2 * sizeof(float));
    }

    // Normals for the scene shader (attribute 2, vertexNormal); absent means
    // up, which the shader's pass-through default never reads.
    mesh.normals = rlAlloc<float>(vertexCount * 3);
    if (data.normals.size() == vertexCount * 3) {
        std::memcpy(mesh.normals, data.normals.data(), vertexCount * 3 * sizeof(float));
    } else {
        for (std::size_t v = 0; v < vertexCount; ++v) {
            mesh.normals[v * 3 + 1] = 1.0F;
        }
    }

    mesh.colors = rlAlloc<unsigned char>(vertexCount * 4);
    if (data.colours.size() == vertexCount * 4) {
        std::memcpy(mesh.colors, data.colours.data(), vertexCount * 4);
    } else {
        std::memset(mesh.colors, 0xFF, vertexCount * 4);
    }

    mesh.indices = rlAlloc<unsigned short>(triangleCount * 3);
    std::memcpy(mesh.indices, data.indices.data(), triangleCount * 3 * sizeof(unsigned short));

    UploadMesh(&mesh, false);
    return mesh;
}

Texture2D uploadTexture(const TextureData& data) {
    Image image{};
    // LoadTextureFromImage reads the pixels and does not take ownership.
    image.data = const_cast<std::uint8_t*>(data.pixels.data());
    image.width = data.width;
    image.height = data.height;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Texture2D texture = LoadTextureFromImage(image);
    if (data.mipmapped) {
        // THE WORLD'S SURFACES: a mip chain and trilinear filtering, wrapped
        // by the shader itself (it folds the coordinates into the atlas cell,
        // so the sampler stays clamped and never reads a neighbour cell).
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    } else {
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    }
    return texture;
}
}  // namespace rl

void Backend::Impl::ensureMaterial() {
    if (!materialLoaded) {
        ensureSceneShader();
    material = LoadMaterialDefault();
    if (scene.id != 0) {
        material.shader = scene;
    }
        defaultTexture = material.maps[MATERIAL_MAP_DIFFUSE].texture;
        materialLoaded = true;
    }
}

void Backend::Impl::ensureHaloFan() {
    if (!haloFanReady) {
        haloFan = uploadMesh(haloFanMesh());
        haloFanReady = true;
    }
}

void Backend::Impl::ensurePaneRamp() {
    if (!paneRampReady) {
        paneRamp = uploadMesh(paneRampMesh());
        paneRampReady = true;
    }
}

void Backend::Impl::release() {
    const auto unloadRigs = [](std::map<std::string, std::unique_ptr<RigModel>>& set) {
        for (auto& [name, rig] : set) {
            (void)name;
            if (rig != nullptr && rig->loaded) {
                if (rig->clips != nullptr) {
                    UnloadModelAnimations(rig->clips, rig->clipCount);
                }
                UnloadModel(rig->model);
            }
        }
        set.clear();
    };
    unloadRigs(rigs);
    unloadRigs(handRigs);
    for (auto& [name, weapon] : weapons) {
        (void)name;
        if (weapon != nullptr && weapon->loaded) {
            UnloadModel(weapon->model);
        }
    }
    weapons.clear();
    for (auto& [name, piece] : statics) {
        (void)name;
        if (piece != nullptr && piece->loaded) {
            UnloadModel(piece->model);
        }
    }
    statics.clear();
    deferred.clear();
    halos.clear();
    lateGlass.clear();
    if (haloFanReady) {
        UnloadMesh(haloFan);
        haloFan = Mesh{};
        haloFanReady = false;
    }
    if (paneRampReady) {
        UnloadMesh(paneRamp);
        paneRamp = Mesh{};
        paneRampReady = false;
    }
    if (blend.id != 0) {
        UnloadShader(blend);
        blend = Shader{};
    }
    if (scene.id != 0) {
        UnloadShader(scene);
        scene = Shader{};
    }
    if (post.id != 0) {
        UnloadShader(post);
        post = Shader{};
    }
    sceneTried = false;
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = RenderTexture2D{};
    }
    sceneOpen = false;
    if (materialLoaded) {
        // First, while its diffuse map is rlgl's own 1x1 (drawScene puts
        // it back after every pass): UnloadMaterial frees any map whose
        // id is not the default, and ours are freed below, once.
        material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
        // Our shaders are unloaded below, once: UnloadMaterial would free
        // the scene shader it holds.
        material.shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadMaterial(material);
        materialLoaded = false;
    }
    for (auto& [id, cached] : meshes) {
        (void)id;
        UnloadMesh(cached.mesh);
    }
    meshes.clear();
    for (auto& [id, cached] : textures) {
        (void)id;
        UnloadTexture(cached.texture);
    }
    textures.clear();
    if (overlay.id != 0) {
        UnloadTexture(overlay);
        overlay = Texture2D{};
        overlayWidth = 0;
        overlayHeight = 0;
    }
}

}  // namespace granadad::render3d
