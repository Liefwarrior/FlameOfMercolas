#include "internal.hpp"

// V LANE: THE PLAYER'S OWN HANDS.

namespace granadad::render3d {

using namespace rl;

/// V LANE. A viewmodel part's transform: scale, roll (Z), pitch (X), yaw
/// (Y) -- right-handed about the view axes, as scene.hpp states -- then
/// its view-space place.
[[nodiscard]] static Matrix partTransform(const ViewmodelPart& part) noexcept {
    return MatrixMultiply(
        MatrixMultiply(
            MatrixMultiply(MatrixMultiply(MatrixScale(part.scale, part.scale, part.scale),
                                          MatrixRotateZ(part.rotation.z)),
                           MatrixRotateX(part.rotation.x)),
            MatrixRotateY(part.rotation.y)),
        MatrixTranslate(part.position.x, part.position.y, part.position.z));
}

/// V LANE. THE SOCKET AS A MATRIX in the Hand_R bone's own frame
/// (metres along its axes): the turn about X, then Y, then Z, then the
/// slide -- scene.hpp's order.
[[nodiscard]] static Matrix socketMatrix(const ViewmodelSocket& socket) noexcept {
    return MatrixMultiply(
        MatrixMultiply(MatrixMultiply(MatrixRotateX(socket.rotation.x), MatrixRotateY(socket.rotation.y)),
                       MatrixRotateZ(socket.rotation.z)),
        MatrixTranslate(socket.offset.x, socket.offset.y, socket.offset.z));
}

/// V LANE. A bone's frame WITHOUT its scale: the export keeps the
/// FantasyHero skeleton in centimetres under a 0.01 root, so the bind
/// pose's scale is that root's and a socket in metres wants the
/// rotation and the place alone.
[[nodiscard]] static Matrix boneFrame(const Transform& bone) noexcept {
    return MatrixMultiply(QuaternionToMatrix(bone.rotation),
                          MatrixTranslate(bone.translation.x, bone.translation.y, bone.translation.z));
}

int Backend::Impl::reseatFusedWeapon(RigModel& rig, const ViewmodelSocket& socket) {
    if (rig.handBone < 0 || rig.model.skeleton.bindPose == nullptr) {
        return 0;
    }
    int moved = 0;
    const Matrix frame = boneFrame(rig.model.skeleton.bindPose[rig.handBone]);
    const Matrix reseat = MatrixMultiply(MatrixMultiply(MatrixInvert(frame), socketMatrix(socket)), frame);
    const Matrix reseatNormals = MatrixTranspose(MatrixInvert(reseat));
    for (int m = 0; m < rig.model.meshCount; ++m) {
        Mesh& mesh = rig.model.meshes[m];
        const int count = mesh.vertexCount;
        if (count <= 0 || mesh.vertices == nullptr || mesh.boneIndices == nullptr ||
            mesh.boneWeights == nullptr) {
            continue;
        }
        // Weld: one key per distinct position, a tenth of a millimetre.
        std::map<std::tuple<long, long, long>, int> keys;
        std::vector<int> weld(static_cast<std::size_t>(count));
        for (int v = 0; v < count; ++v) {
            const auto q = [&mesh, v](int axis) {
                return std::lround(static_cast<double>(mesh.vertices[v * 3 + axis]) * 10000.0);
            };
            const auto key = std::make_tuple(q(0), q(1), q(2));
            const auto found = keys.find(key);
            if (found == keys.end()) {
                const int id = static_cast<int>(keys.size());
                keys.emplace(key, id);
                weld[static_cast<std::size_t>(v)] = id;
            } else {
                weld[static_cast<std::size_t>(v)] = found->second;
            }
        }
        // Union-find over the triangles.
        std::vector<int> parent(keys.size());
        for (std::size_t i = 0; i < parent.size(); ++i) {
            parent[i] = static_cast<int>(i);
        }
        const auto find = [&parent](int a) {
            while (parent[static_cast<std::size_t>(a)] != a) {
                parent[static_cast<std::size_t>(a)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(a)])];
                a = parent[static_cast<std::size_t>(a)];
            }
            return a;
        };
        const auto unite = [&parent, &find](int a, int b) {
            const int ra = find(a);
            const int rb = find(b);
            if (ra != rb) {
                parent[static_cast<std::size_t>(ra)] = rb;
            }
        };
        const int triangles = mesh.triangleCount;
        for (int t = 0; t < triangles; ++t) {
            int corner[3];
            for (int c = 0; c < 3; ++c) {
                corner[c] = mesh.indices != nullptr ? static_cast<int>(mesh.indices[t * 3 + c]) : t * 3 + c;
            }
            if (corner[0] >= count || corner[1] >= count || corner[2] >= count) {
                continue;
            }
            unite(weld[static_cast<std::size_t>(corner[0])], weld[static_cast<std::size_t>(corner[1])]);
            unite(weld[static_cast<std::size_t>(corner[0])], weld[static_cast<std::size_t>(corner[2])]);
        }
        // A component is the weapon when every vertex in it is the
        // hand bone's alone.
        std::vector<std::uint8_t> wholeHand(keys.size(), 1U);
        for (int v = 0; v < count; ++v) {
            bool whole = false;
            for (int j = 0; j < 4; ++j) {
                const float w = mesh.boneWeights[v * 4 + j];
                const int b = static_cast<int>(mesh.boneIndices[v * 4 + j]);
                if (w > 0.999F && b == rig.handBone) {
                    whole = true;
                } else if (w > 0.001F) {
                    whole = false;
                    break;
                }
            }
            if (!whole) {
                wholeHand[static_cast<std::size_t>(find(weld[static_cast<std::size_t>(v)]))] = 0U;
            }
        }
        for (int v = 0; v < count; ++v) {
            if (wholeHand[static_cast<std::size_t>(find(weld[static_cast<std::size_t>(v)]))] == 0U) {
                continue;
            }
            const Vector3 p = Vector3Transform(
                Vector3{mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]}, reseat);
            mesh.vertices[v * 3] = p.x;
            mesh.vertices[v * 3 + 1] = p.y;
            mesh.vertices[v * 3 + 2] = p.z;
            if (mesh.animVertices != nullptr) {
                mesh.animVertices[v * 3] = p.x;
                mesh.animVertices[v * 3 + 1] = p.y;
                mesh.animVertices[v * 3 + 2] = p.z;
            }
            if (mesh.normals != nullptr) {
                const Vector3 n = Vector3Normalize(Vector3Transform(
                    Vector3{mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]},
                    reseatNormals));
                mesh.normals[v * 3] = n.x;
                mesh.normals[v * 3 + 1] = n.y;
                mesh.normals[v * 3 + 2] = n.z;
                if (mesh.animNormals != nullptr) {
                    mesh.animNormals[v * 3] = n.x;
                    mesh.animNormals[v * 3 + 1] = n.y;
                    mesh.animNormals[v * 3 + 2] = n.z;
                }
            }
            ++moved;
        }
        if (moved > 0 && mesh.vboId != nullptr) {
            // The bind-pose buffers on the GPU, for the frame before the
            // first clip is played over them.
            UpdateMeshBuffer(mesh, 0, mesh.vertices, count * 3 * static_cast<int>(sizeof(float)), 0);
            if (mesh.normals != nullptr) {
                UpdateMeshBuffer(mesh, 2, mesh.normals, count * 3 * static_cast<int>(sizeof(float)), 0);
            }
        }
    }
    return moved;
}

void Backend::Impl::drawViewmodel(const ViewmodelInstance& hands, SceneStats& stats) {
    if (!hands.visible) {
        return;
    }
    Camera3D eye{};
    eye.position = Vector3{0.0F, 0.0F, 0.0F};
    eye.target = Vector3{0.0F, 0.0F, -1.0F};
    eye.up = Vector3{0.0F, 1.0F, 0.0F};
    eye.fovy = hands.fovyDegrees;
    eye.projection = CAMERA_PERSPECTIVE;
    BeginMode3D(eye);
    // The projection BeginMode3D just built (the world's clip planes)
    // is replaced by the hands' own with its z row squeezed: row 2 of
    // the matrix becomes span*row2 + shift*row3, and row 3 of a
    // perspective matrix is (0, 0, -1, 0), so only m10 and m14 move.
    const float aspect = static_cast<float>(std::max(1, GetRenderWidth())) /
                         static_cast<float>(std::max(1, GetRenderHeight()));
    Matrix proj = MatrixPerspective(static_cast<double>(hands.fovyDegrees * kDegToRad),
                                    static_cast<double>(aspect), kViewmodelNear,
                                    kViewmodelFar);
    const float shift = -(1.0F - kViewmodelDepthSpan);
    proj.m10 = kViewmodelDepthSpan * proj.m10 + shift * proj.m11;
    proj.m14 = kViewmodelDepthSpan * proj.m14 + shift * proj.m15;
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(proj));
    rlMatrixMode(RL_MODELVIEW);

    RigModel* rig = handRigFor(hands.kind);
    if (rig != nullptr && rig->loaded) {
        // THE CLIP AND THE FRAME are the description's (viewmodel.cpp's
        // policy: a guard held on the block clip's guard frame, a swing
        // from its cock), never the raw state.
        const int clipIndex = static_cast<int>(hands.rigClip);
        int frame = 0;
        bool played = false;
        if (clipIndex < rig->clipCount && rig->clips[clipIndex].keyframeCount > 0) {
            const ModelAnimation& clip = rig->clips[clipIndex];
            const int last = clip.keyframeCount - 1;
            frame = static_cast<int>(hands.rigFrame * static_cast<float>(last) + 0.5F);
            frame = std::clamp(frame, 0, last);
            UpdateModelAnimation(rig->model, clip, static_cast<float>(frame));
            rig->posed = true;
            played = true;
        }
        // THE FRAMING: scale, the yaw about +Y, the feet's place, then
        // the lean about the eye (scene.hpp's order) -- so the arms come
        // up from the bottom of the frame the way a first-person rig
        // is framed. Drawn mesh by mesh, since DrawModelEx has no
        // second rotation.
        const Matrix rigWorld = MatrixMultiply(
            MatrixMultiply(
                MatrixMultiply(MatrixScale(hands.rigScale, hands.rigScale, hands.rigScale),
                               MatrixRotateY(-hands.rigYaw)),
                MatrixTranslate(hands.rigOffset.x, hands.rigOffset.y, hands.rigOffset.z)),
            MatrixRotateX(hands.rigPitch));
        const Matrix rigFull = MatrixMultiply(rig->model.transform, rigWorld);
        for (int i = 0; i < rig->model.meshCount; ++i) {
            const int m = rig->model.meshMaterial[i];
            if (m < 0 || m >= rig->model.materialCount) {
                continue;
            }
            rig->model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color = colourOf(hands.tint);
            DrawMesh(rig->model.meshes[i], rig->model.materials[m], rigFull);
            stats.trianglesDrawn += static_cast<std::size_t>(rig->model.meshes[i].triangleCount);
        }
        ++stats.instancesDrawn;
        stats.viewmodelSkinned = true;
        // The weapon on the hand: the bone's model-space pose (raylib
        // composes glTF joints to model space when it loads them) less
        // its scale, the kind's socket ahead of it (a hammer grip: the
        // grip across the fingers, the head out of the thumb side),
        // under the rig's own placement. A fused weapon was re-seated
        // by the same socket at load and rides the skin.
        WeaponModel* weapon = weaponFor(hands.kind);
        if (weapon != nullptr && weapon->loaded && rig->handBone >= 0 &&
            rig->model.skeleton.bindPose != nullptr && !viewmodelWeaponFusedOf(hands.kind)) {
            Transform bone = rig->model.skeleton.bindPose[rig->handBone];
            if (played) {
                const ModelAnimation& clip = rig->clips[clipIndex];
                if (rig->handBone < clip.boneCount && clip.keyframePoses != nullptr) {
                    bone = clip.keyframePoses[frame][rig->handBone];
                }
            }
            const ViewmodelSocket socket{hands.socketOffset, hands.socketRotation};
            const Matrix boneWorld =
                MatrixMultiply(MatrixMultiply(socketMatrix(socket), boneFrame(bone)), rigWorld);
            for (int i = 0; i < weapon->model.meshCount; ++i) {
                const int m = weapon->model.meshMaterial[i];
                weapon->model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color =
                    colourOf(hands.tint);
                DrawMesh(weapon->model.meshes[i], weapon->model.materials[m],
                         MatrixMultiply(weapon->model.transform, boneWorld));
                stats.trianglesDrawn +=
                    static_cast<std::size_t>(weapon->model.meshes[i].triangleCount);
            }
            ++stats.instancesDrawn;
            stats.viewmodelWeaponLoaded = true;
        }
        // PHASE 9: THE SHIELD over the licensed arms. The rig carries none,
        // so the description's own shield part is drawn here as it is over
        // the placeholder -- on the pose table's left fist, in the rig's
        // light -- and rises with the table's block like the arms' guard.
        for (const ViewmodelPart& part : hands.parts) {
            if (isViewmodelShieldMeshId(part.meshId) &&
                drawMeshWith(part.meshId, 0, hands.tint, partTransform(part), stats, kSurfaceLit, true)) {
                ++stats.viewmodelPartsDrawn;
            }
        }
    } else {
        for (const ViewmodelPart& part : hands.parts) {
            if (drawMeshWith(part.meshId, 0, part.tint, partTransform(part), stats, kSurfaceLit, true)) {
                ++stats.viewmodelPartsDrawn;
            }
        }
    }
    EndMode3D();
}

}  // namespace granadad::render3d
