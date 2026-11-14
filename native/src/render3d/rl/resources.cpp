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

    // Always present, so both paths see the same attribute set: the GL 3.3
    // default shader samples texture0 (a 1x1 white) at these coordinates and
    // rlsw does the same in immediate mode.
    mesh.texcoords = rlAlloc<float>(vertexCount * 2);
    if (data.texcoords.size() == vertexCount * 2) {
        std::memcpy(mesh.texcoords, data.texcoords.data(), vertexCount * 2 * sizeof(float));
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
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}
}  // namespace rl

void Backend::Impl::ensureMaterial() {
    if (!materialLoaded) {
        material = LoadMaterialDefault();
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

Mesh Backend::Impl::tinted(const Mesh& mesh, const Rgba8& tint) {
#if defined(GRAPHICS_API_OPENGL_11)
    if (mesh.colors != nullptr && mesh.vertexCount > 0 &&
        (tint.r != 255 || tint.g != 255 || tint.b != 255 || tint.a != 255)) {
        const std::size_t count = static_cast<std::size_t>(mesh.vertexCount) * 4U;
        tintScratch.resize(count);
        const unsigned char factors[4] = {tint.r, tint.g, tint.b, tint.a};
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned int product = static_cast<unsigned int>(mesh.colors[i]) *
                                         static_cast<unsigned int>(factors[i & 3U]);
            tintScratch[i] = static_cast<unsigned char>((product + 127U) / 255U);
        }
        Mesh copy = mesh;
        copy.colors = tintScratch.data();
        return copy;
    }
#else
    (void)tint;
#endif
    return mesh;
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
    if (materialLoaded) {
        // First, while its diffuse map is rlgl's own 1x1 (drawScene puts
        // it back after every pass): UnloadMaterial frees any map whose
        // id is not the default, and ours are freed below, once.
        material.maps[MATERIAL_MAP_DIFFUSE].texture = defaultTexture;
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
