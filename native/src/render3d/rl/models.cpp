#include "internal.hpp"

// glb RIGS, glTF BUILDING PIECES, THE GLASS CUT AND THE TRIM.

namespace granadad::render3d {

using namespace rl;

RigModel* Backend::Impl::rigFor(std::uint8_t rig) {
    const std::string_view file = actorRigFile(rig);
    if (file.empty() || config.modelDir.empty()) {
        return nullptr;
    }
    const std::string key(file);
    auto found = rigs.find(key);
    if (found != rigs.end()) {
        return found->second.get();
    }
    std::unique_ptr<RigModel> loaded;
    const std::string path = config.modelDir + "/" + key;
    if (FileExists(path.c_str())) {
        auto candidate = std::make_unique<RigModel>();
        candidate->model = LoadModel(path.c_str());
        if (candidate->model.meshCount > 0) {
            candidate->clips = LoadModelAnimations(path.c_str(), &candidate->clipCount);
            if (candidate->clips == nullptr) {
                candidate->clipCount = 0;
            }
            candidate->loaded = true;
            std::printf("granadad: render3d: rig %s -- %d mesh(es), %d bone(s), %d clip(s)\n",
                        key.c_str(), candidate->model.meshCount,
                        candidate->model.skeleton.boneCount, candidate->clipCount);
            loaded = std::move(candidate);
        } else {
            UnloadModel(candidate->model);
            std::printf("granadad: render3d: rig %s did not load; placeholder stands\n",
                        key.c_str());
        }
    }
    RigModel* result = loaded.get();
    rigs.emplace(key, std::move(loaded));
    return result;
}

/// A GLASS RAMP in vertex colour, by a vertex's height `t` (0 the
/// bottom of the glass, 1 its head) with the flame at `wick`: full
/// white across a band round the wick, easing to kPaneRampHead
/// (scene.hpp) at the head and to a shade under white at the foot.
/// A window's wick is its sill (0): the candle stands on it. A
/// lantern's is a third of the way up its glass.
[[nodiscard]] static Rgba8 glassRamp(float t, float wick) noexcept {
    // A window's hot band is its sill up to the knee; a lantern's is
    // a hand's breadth either side of the wick.
    const float lo = std::max(0.0F, wick - 0.15F);
    const float hi = std::min(1.0F, wick + (wick > 0.0F ? 0.15F : kPaneRampKnee));
    const auto lerp = [](const Rgba8& a, const Rgba8& b, float u) {
        const auto ch = [u](std::uint8_t x, std::uint8_t y) {
            return static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * u, 0.0F,
                255.0F) + 0.5F);
        };
        return Rgba8{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 255};
    };
    constexpr Rgba8 kWhite{255, 255, 255, 255};
    constexpr Rgba8 kFoot{224, 218, 210, 255};
    if (t < lo) {
        return lerp(kFoot, kWhite, lo > 0.0F ? t / lo : 1.0F);
    }
    if (t <= hi) {
        return kWhite;
    }
    return lerp(kWhite, kPaneRampHead, hi < 1.0F ? (t - hi) / (1.0F - hi) : 1.0F);
}

/// The triangles of `src` that `keep(i)` passes, copied out DE-INDEXED
/// into a mesh of their own with a vertex colour from `colourAt(y)`
/// (the vertex's local y), uploaded. Empty (no triangles) when none
/// pass. The source is left alone.
template <typename Keep, typename ColourAt>
[[nodiscard]] static Mesh rebuiltTriangles(const Mesh& src, Keep keep, ColourAt colourAt,
                                           bool withColours = true) {
    Mesh out{};
    const int triangles = src.triangleCount;
    const auto index = [&src](int tri, int corner) {
        const int slot = tri * 3 + corner;
        return src.indices != nullptr ? static_cast<int>(src.indices[slot]) : slot;
    };
    int kept = 0;
    for (int tri = 0; tri < triangles; ++tri) {
        kept += keep(tri) ? 1 : 0;
    }
    if (kept == 0) {
        return out;
    }
    out.vertexCount = kept * 3;
    out.triangleCount = kept;
    out.vertices = rlAlloc<float>(static_cast<std::size_t>(out.vertexCount) * 3U);
    out.texcoords = src.texcoords != nullptr ? rlAlloc<float>(static_cast<std::size_t>(out.vertexCount) * 2U)
                                             : nullptr;
    out.normals = src.normals != nullptr ? rlAlloc<float>(static_cast<std::size_t>(out.vertexCount) * 3U)
                                         : nullptr;
    // No colour array unless asked: a metal mesh keeps the default white
    // vertex colour, so its tint and its night cap come from the material.
    out.colors =
        withColours ? rlAlloc<unsigned char>(static_cast<std::size_t>(out.vertexCount) * 4U) : nullptr;
    int at = 0;
    for (int tri = 0; tri < triangles; ++tri) {
        if (!keep(tri)) {
            continue;
        }
        for (int corner = 0; corner < 3; ++corner) {
            const int from = index(tri, corner);
            out.vertices[at * 3] = src.vertices[from * 3];
            out.vertices[at * 3 + 1] = src.vertices[from * 3 + 1];
            out.vertices[at * 3 + 2] = src.vertices[from * 3 + 2];
            if (out.texcoords != nullptr) {
                out.texcoords[at * 2] = src.texcoords[from * 2];
                out.texcoords[at * 2 + 1] = src.texcoords[from * 2 + 1];
            }
            if (out.normals != nullptr) {
                out.normals[at * 3] = src.normals[from * 3];
                out.normals[at * 3 + 1] = src.normals[from * 3 + 1];
                out.normals[at * 3 + 2] = src.normals[from * 3 + 2];
            }
            if (out.colors != nullptr) {
                const Rgba8 c = colourAt(src.vertices[from * 3 + 1]);
                out.colors[at * 4] = c.r;
                out.colors[at * 4 + 1] = c.g;
                out.colors[at * 4 + 2] = c.b;
                out.colors[at * 4 + 3] = c.a;
            }
            ++at;
        }
    }
    UploadMesh(&out, false);
    return out;
}

/// The local Y extent of the vertices the kept triangles of `src` use.
template <typename Keep>
static void yExtentOf(const Mesh& src, Keep keep, float& lo, float& hi) {
    lo = std::numeric_limits<float>::max();
    hi = std::numeric_limits<float>::lowest();
    for (int tri = 0; tri < src.triangleCount; ++tri) {
        if (!keep(tri)) {
            continue;
        }
        for (int corner = 0; corner < 3; ++corner) {
            const int slot = tri * 3 + corner;
            const int from = src.indices != nullptr ? static_cast<int>(src.indices[slot]) : slot;
            const float y = src.vertices[from * 3 + 1];
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }
    if (hi <= lo) {
        lo = 0.0F;
        hi = 1.0F;
    }
}

void Backend::Impl::applyGlass(StaticModel& model, const StaticPieceRef& ref) {
    Model& m = model.model;
    if (ref.cutsGlass() && !model.glassCut) {
        model.glassCut = true;
        const int count = m.meshCount;
        for (int i = 0; i < count; ++i) {
            const Mesh& src = m.meshes[i];
            if (src.vertices == nullptr || src.triangleCount <= 0) {
                continue;
            }
            // Glass is a triangle whose EVERY vertex lies in the band and
            // on the patch: a rim ring that straddles the band's edge is
            // metal whole, not a sawtooth of half its quads (the auditor's
            // first).
            const auto inGlass = [&src, &ref](int tri) {
                for (int corner = 0; corner < 3; ++corner) {
                    const int slot = tri * 3 + corner;
                    const int from = src.indices != nullptr ? static_cast<int>(src.indices[slot]) : slot;
                    const float y = src.vertices[from * 3 + 1];
                    if (y < ref.glassBandLo || y > ref.glassBandHi) {
                        return false;
                    }
                    if (src.texcoords != nullptr) {
                        const float u = src.texcoords[from * 2];
                        const float v = src.texcoords[from * 2 + 1];
                        if (u < ref.glassU0 || u > ref.glassU1 || v < ref.glassV0 || v > ref.glassV1) {
                            return false;
                        }
                    }
                }
                return true;
            };
            float lo = 0.0F;
            float hi = 1.0F;
            yExtentOf(src, inGlass, lo, hi);
            Mesh glass = rebuiltTriangles(src, inGlass, [lo, hi](float y) {
                return glassRamp((y - lo) / (hi - lo), 0.33F);
            });
            if (glass.triangleCount == 0) {
                continue;
            }
            Mesh rest = rebuiltTriangles(
                src, [&inGlass](int tri) { return !inGlass(tri); },
                [](float) { return Rgba8{255, 255, 255, 255}; }, false);
            if (rest.triangleCount == 0) {
                // Every triangle was glass: the glass takes the slot.
                UnloadMesh(m.meshes[i]);
                m.meshes[i] = glass;
                model.glass[static_cast<std::size_t>(i)] = true;
                model.translucent[static_cast<std::size_t>(i)] = false;
                model.ramped.resize(static_cast<std::size_t>(m.meshCount), false);
                model.ramped[static_cast<std::size_t>(i)] = true;
                continue;
            }
            // The model's arrays, one longer: the rest in the glass's old
            // slot, the glass appended on the same material.
            const int n = m.meshCount + 1;
            Mesh* meshes = rlAlloc<Mesh>(static_cast<std::size_t>(n));
            int* materials = rlAlloc<int>(static_cast<std::size_t>(n));
            for (int k = 0; k < m.meshCount; ++k) {
                meshes[k] = m.meshes[k];
                materials[k] = m.meshMaterial[k];
            }
            UnloadMesh(meshes[i]);
            meshes[i] = rest;
            meshes[n - 1] = glass;
            materials[n - 1] = m.meshMaterial[i];
            std::free(m.meshes);
            std::free(m.meshMaterial);
            m.meshes = meshes;
            m.meshMaterial = materials;
            m.meshCount = n;
            model.translucent.push_back(false);
            model.glass.push_back(true);
            model.ramped.resize(static_cast<std::size_t>(n), false);
            model.ramped[static_cast<std::size_t>(n - 1)] = true;
        }
    }
    model.ramped.resize(static_cast<std::size_t>(m.meshCount), false);
    for (int i = 0; i < m.meshCount; ++i) {
        if (!model.glass[static_cast<std::size_t>(i)] || model.ramped[static_cast<std::size_t>(i)]) {
            continue;
        }
        const Mesh& src = m.meshes[i];
        if (src.vertices == nullptr || src.triangleCount <= 0) {
            continue;
        }
        const auto all = [](int) { return true; };
        float lo = 0.0F;
        float hi = 1.0F;
        yExtentOf(src, all, lo, hi);
        Mesh ramped = rebuiltTriangles(src, all, [lo, hi](float y) {
            return glassRamp((y - lo) / (hi - lo), 0.0F);
        });
        if (ramped.triangleCount == 0) {
            continue;
        }
        UnloadMesh(m.meshes[i]);
        m.meshes[i] = ramped;
        model.ramped[static_cast<std::size_t>(i)] = true;
    }
}

/// THE TRIM (scene.hpp StaticPieceRef), once, at load: a triangle whose
/// EVERY vertex lies in the trim band and on the trim patch is dropped,
/// the glass cut's own test, so a ring that only touches the band stays
/// whole. A mesh the trim would empty is left as it was: a trim that
/// names a whole file is a catalogue mistake, not a reason to draw
/// nothing.
static void trimModel(Model& m, const StaticPieceRef& ref) {
    for (int i = 0; i < m.meshCount; ++i) {
        const Mesh& src = m.meshes[i];
        if (src.vertices == nullptr || src.triangleCount <= 0) {
            continue;
        }
        const auto inTrim = [&src, &ref](int tri) {
            for (int corner = 0; corner < 3; ++corner) {
                const int slot = tri * 3 + corner;
                const int from = src.indices != nullptr ? static_cast<int>(src.indices[slot]) : slot;
                const float y = src.vertices[from * 3 + 1];
                if (y < ref.trimBandLo || y > ref.trimBandHi) {
                    return false;
                }
                if (src.texcoords != nullptr) {
                    const float u = src.texcoords[from * 2];
                    const float v = src.texcoords[from * 2 + 1];
                    if (u < ref.trimU0 || u > ref.trimU1 || v < ref.trimV0 || v > ref.trimV1) {
                        return false;
                    }
                }
            }
            return true;
        };
        int dropped = 0;
        for (int tri = 0; tri < src.triangleCount; ++tri) {
            dropped += inTrim(tri) ? 1 : 0;
        }
        if (dropped == 0 || dropped == src.triangleCount) {
            continue;
        }
        Mesh rest = rebuiltTriangles(
            src, [&inTrim](int tri) { return !inTrim(tri); },
            [](float) { return Rgba8{255, 255, 255, 255}; }, false);
        if (rest.triangleCount == 0) {
            continue;
        }
        UnloadMesh(m.meshes[i]);
        m.meshes[i] = rest;
    }
}

StaticModel* Backend::Impl::staticFor(const StaticPieceRef& ref) {
    const std::string& file = ref.file;
    const float squareHalf = ref.squareHalf;
    if (file.empty() || config.staticDir.empty()) {
        return nullptr;
    }
    // A squared shaft is its own model: the same file may dress a
    // hitching post as it comes. So is a trimmed one: the table lantern
    // drops the cord the hung lantern hangs by.
    std::string key = squareHalf > 0.0F ? file + "#square" : file;
    if (ref.cutsTrim()) {
        key += "#trim" + std::to_string(ref.trimBandLo) + "," + std::to_string(ref.trimBandHi) + "," +
               std::to_string(ref.trimU0) + "," + std::to_string(ref.trimU1) + "," +
               std::to_string(ref.trimV0) + "," + std::to_string(ref.trimV1);
    }
    auto found = statics.find(key);
    if (found != statics.end()) {
        // Two rows can share one file (the hung lantern and the kit's
        // carried one): a later ref that names a glass cut still gets
        // it. applyGlass is idempotent past the first cut.
        if (found->second != nullptr && found->second->loaded && ref.cutsGlass()) {
            applyGlass(*found->second, ref);
        }
        return found->second.get();
    }
    std::unique_ptr<StaticModel> loaded;
    const std::string path = config.staticDir + "/" + file;
    if (FileExists(path.c_str())) {
        auto candidate = std::make_unique<StaticModel>();
        candidate->model = LoadModel(path.c_str());
        if (candidate->model.meshCount > 0) {
            candidate->loaded = true;
            if (squareHalf > 0.0F) {
                // The catalogue asked for a square shaft
                // (shaft_square.hpp): the hewn corners pushed out to
                // the arris, then the buffers on the GPU again.
                for (int i = 0; i < candidate->model.meshCount; ++i) {
                    Mesh& mesh = candidate->model.meshes[i];
                    const int bytes = mesh.vertexCount * 3 * static_cast<int>(sizeof(float));
                    if (squareShaft(mesh.vertices, mesh.normals,
                                    static_cast<std::size_t>(mesh.vertexCount), squareHalf) > 0 &&
                        mesh.vboId != nullptr) {
                        UpdateMeshBuffer(mesh, 0, mesh.vertices, bytes, 0);
                        if (mesh.normals != nullptr) {
                            UpdateMeshBuffer(mesh, 2, mesh.normals, bytes, 0);
                        }
                    }
                }
            }
            if (ref.cutsTrim()) {
                trimModel(candidate->model, ref);
            }
            candidate->baseColour.resize(static_cast<std::size_t>(candidate->model.materialCount));
            for (int m = 0; m < candidate->model.materialCount; ++m) {
                Material& material = candidate->model.materials[m];
                candidate->baseColour[static_cast<std::size_t>(m)] =
                    material.maps[MATERIAL_MAP_DIFFUSE].color;
                // The pack atlases are flat-colour sheets a few thousand
                // texels across drawn on 2.5 m walls: mipmapped and
                // filtered they read as paint, point-sampled they
                // shimmer.
                Texture2D& texture = material.maps[MATERIAL_MAP_DIFFUSE].texture;
                if (texture.id != 0 && texture.id != defaultTexture.id) {
                    GenTextureMipmaps(&texture);
                    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
                }
            }
            candidate->translucent.resize(static_cast<std::size_t>(candidate->model.meshCount));
            candidate->glass.resize(static_cast<std::size_t>(candidate->model.meshCount));
            for (int i = 0; i < candidate->model.meshCount; ++i) {
                const int m = candidate->model.meshMaterial[i];
                const bool inRange = m >= 0 && m < candidate->model.materialCount;
                const bool translucent =
                    inRange && candidate->model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color.a < 255;
                candidate->translucent[static_cast<std::size_t>(i)] = translucent;
                candidate->glass[static_cast<std::size_t>(i)] =
                    translucent &&
                    (candidate->model.materials[m].maps[MATERIAL_MAP_DIFFUSE].texture.id == 0 ||
                     candidate->model.materials[m].maps[MATERIAL_MAP_DIFFUSE].texture.id ==
                         defaultTexture.id);
            }
            applyGlass(*candidate, ref);
            loaded = std::move(candidate);
        } else {
            UnloadModel(candidate->model);
            std::printf("granadad: render3d: piece %s did not load; the chunk stands\n",
                        file.c_str());
        }
    }
    StaticModel* result = loaded.get();
    statics.emplace(key, std::move(loaded));
    return result;
}

std::size_t Backend::Impl::staticsLoaded() const noexcept {
    std::size_t count = 0;
    for (const auto& [name, piece] : statics) {
        (void)name;
        if (piece != nullptr) {
            ++count;
        }
    }
    return count;
}

std::size_t Backend::Impl::rigsLoaded() const noexcept {
    std::size_t count = 0;
    for (const auto& [name, rig] : rigs) {
        (void)name;
        if (rig != nullptr) {
            ++count;
        }
    }
    return count;
}

RigModel* Backend::Impl::handRigFor(std::uint8_t kind) {
    const std::string_view file = viewmodelRigFileOf(kind);
    if (file.empty() || config.modelDir.empty()) {
        return nullptr;
    }
    const std::string key(file);
    auto found = handRigs.find(key);
    if (found != handRigs.end()) {
        return found->second.get();
    }
    std::unique_ptr<RigModel> loaded;
    const std::string path = config.modelDir + "/" + key;
    if (FileExists(path.c_str())) {
        auto candidate = std::make_unique<RigModel>();
        candidate->model = LoadModel(path.c_str());
        if (candidate->model.meshCount > 0) {
            candidate->clips = LoadModelAnimations(path.c_str(), &candidate->clipCount);
            if (candidate->clips == nullptr) {
                candidate->clipCount = 0;
            }
            for (int b = 0; b < candidate->model.skeleton.boneCount; ++b) {
                if (std::strcmp(candidate->model.skeleton.bones[b].name, "Hand_R") == 0) {
                    candidate->handBone = b;
                    break;
                }
            }
            candidate->loaded = true;
            const int reseated = viewmodelWeaponFusedOf(kind)
                                     ? reseatFusedWeapon(*candidate, viewmodelSocketOf(kind))
                                     : 0;
            std::printf("granadad: render3d: hands %s -- %d mesh(es), %d bone(s), %d clip(s), "
                        "Hand_R %s, %d fused weapon vertices re-seated\n",
                        key.c_str(), candidate->model.meshCount,
                        candidate->model.skeleton.boneCount, candidate->clipCount,
                        candidate->handBone >= 0 ? "found" : "absent", reseated);
            loaded = std::move(candidate);
        } else {
            UnloadModel(candidate->model);
            std::printf("granadad: render3d: hands %s did not load; placeholder stands\n",
                        key.c_str());
        }
    }
    RigModel* result = loaded.get();
    handRigs.emplace(key, std::move(loaded));
    return result;
}

WeaponModel* Backend::Impl::weaponFor(std::uint8_t kind) {
    const std::string_view file = viewmodelWeaponFileOf(kind);
    if (file.empty() || config.weaponDir.empty()) {
        return nullptr;
    }
    const std::string key(file);
    auto found = weapons.find(key);
    if (found != weapons.end()) {
        return found->second.get();
    }
    std::unique_ptr<WeaponModel> loaded;
    const std::string path = config.weaponDir + "/" + key;
    if (FileExists(path.c_str())) {
        auto candidate = std::make_unique<WeaponModel>();
        candidate->model = LoadModel(path.c_str());
        if (candidate->model.meshCount > 0) {
            candidate->loaded = true;
            std::printf("granadad: render3d: weapon %s -- %d mesh(es)\n", key.c_str(),
                        candidate->model.meshCount);
            loaded = std::move(candidate);
        } else {
            UnloadModel(candidate->model);
            std::printf("granadad: render3d: weapon %s did not load; placeholder stands\n",
                        key.c_str());
        }
    }
    WeaponModel* result = loaded.get();
    weapons.emplace(key, std::move(loaded));
    return result;
}

}  // namespace granadad::render3d
