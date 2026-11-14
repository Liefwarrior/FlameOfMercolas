#pragma once

// THE ADAPTER'S PRIVATE HEADER. Everything under src/render3d/rl/ includes
// raylib.h through this file, and nothing outside this folder ever does: the
// public contract is include/granadad/render3d/backend.hpp. See that header
// for why raylib's global names (Rectangle, Camera, CloseWindow...) are kept
// off every other translation unit.
//
// The adapter is one class, Backend::Impl, whose members are defined across
// the files of this folder -- the same shape as render/session*.cpp:
//
//   context.cpp    the window: open, close, the frame's begin and overlay
//   resources.cpp  meshes and textures on the GPU, the default material, release
//   shaders.cpp    the GLSL sources and their loaders
//   models.cpp     glb rigs, glTF pieces, the glass cut, the trim
//   pieces.cpp     drawing a static building piece (S lane)
//   passes.cpp     the 3D pass of a SceneDescription: instances, bodies
//   viewmodel_pass.cpp   the player's hands (V lane)
//   offscreen.cpp  the offscreen scene target and the post pass
//   capture.cpp    the one place a frame is read back
//   input.cpp      keys, mouse and the HID vocabulary

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/shaft_square.hpp"

namespace granadad::render3d::rl {

/// Whether this build's raylib platform can open a context with no window and
/// no display server. Only the SDL platform can: SDL's `offscreen` video
/// driver creates a desktop GL 3.3 core context through EGL, which on a box
/// with no GPU is Mesa's llvmpipe. The shipped .exe's GLFW platform cannot, and
/// does not need to.
#if defined(PLATFORM_DESKTOP_SDL)
constexpr bool kHeadlessCapable = true;
#else
constexpr bool kHeadlessCapable = false;
#endif

struct CachedMesh {
    std::uint32_t version = 0;
    Mesh mesh{};
};

struct CachedTexture {
    std::uint32_t version = 0;
    Texture2D texture{};
};

/// A glb rig, loaded once per FILE (several WardTypes share a file) and
/// animated in place: UpdateModelAnimation writes the pose into the model's
/// own animVertices, so one model serves every body of its kind as long as
/// update and draw stay paired per body, which they do below.
struct RigModel {
    Model model{};
    ModelAnimation* clips = nullptr;
    int clipCount = 0;
    bool loaded = false;
    /// True while the model holds a skinned pose: the rest pose is put back
    /// once per pass before the unskinned bodies draw.
    bool posed = false;
    /// V LANE. The Hand_R bone's index when the rig has one (the viewmodel
    /// hangs a weapon on it), else -1.
    int handBone = -1;
};

/// V LANE. A static weapon model (the mace, the dagger) loaded once per file.
struct WeaponModel {
    Model model{};
    bool loaded = false;
};

/// S LANE. One building piece, loaded once per file. `baseColour` keeps each
/// material's own colour factor (the water's 0.7 alpha, a glass pane's) so
/// the instance tint multiplies it rather than replacing it per draw.
struct StaticModel {
    Model model{};
    bool loaded = false;
    std::vector<Color> baseColour;
    /// Per mesh: its material's alpha is under 255 -- drawn in the late pass.
    std::vector<bool> translucent;
    /// Per mesh: translucent AND untextured -- a pane of glass, drawn with
    /// the instance's pane tint (dark by day and over an unlit room, warm
    /// over a lit room at night) rather than as a white wash over the wall
    /// the chunk mesh puts behind it.
    std::vector<bool> glass;
    /// Which meshes were rebuilt with the glass ramp in their vertex
    /// colours (applyGlass): the pane tint multiplies the ramp in the shader.
    std::vector<bool> ramped;
    /// Whether a catalogue glass cut has been applied to this model. The
    /// cache is by file and two rows can share one (the hung lantern and
    /// the kit's carried one): whichever loads first, the first ref that
    /// names a cut gets it applied.
    bool glassCut = false;
};

/// S LANE. The four blend corners of a piece and their spans: the light
/// at local (x, z) = (from, fromZ), (to, fromZ), (from, toZ), (to, toZ).
/// The late glass pass runs only where the fog takes a real share off a
/// pane (its alpha under this, a share under 0.94): a night fog past about
/// a metre and a third qualifies; an overcast's trace of a veil (a share of
/// 0.97 at four metres, 0.86 at twenty) does not until eight and a half
/// metres out, where it is harmless either way with the halos drawn last.
constexpr unsigned char kLateGlassBelow = 240;

struct PieceTints {
    Color a{};
    Color b{};
    Color c{};
    Color d{};
    float from = 0.0F;
    float to = 0.0F;
    float fromZ = 0.0F;
    float toZ = 0.0F;
    /// What a pane of glass in the piece is drawn with.
    Color pane{};
    /// kDrawPlain / kDrawHalo / kDrawShaded (scene.hpp).
    std::uint8_t mode = 0;

    [[nodiscard]] bool uniform() const noexcept {
        const auto same = [](const Color& p, const Color& q) {
            return p.r == q.r && p.g == q.g && p.b == q.b;
        };
        return same(a, b) && same(a, c) && same(a, d);
    }
};

/// S LANE. A translucent sub-mesh held back for the late pass.
struct DeferredMesh {
    StaticModel* model = nullptr;
    int mesh = 0;
    Matrix transform{};
    PieceTints tints{};
    bool mirrored = false;
};

/// A glTF asset faces +Z (the spec's own convention, and the asset lane's
/// export); the scene's yaw 0 faces -Z. Half a turn, applied to models only.
constexpr float kGltfForwardYaw = 3.14159265358979323846F;
constexpr float kRadToDeg = 180.0F / 3.14159265358979323846F;
constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;

/// V LANE. The viewmodel pass's own clip planes: the fists sit 0.5..1.3
/// tiles out and nothing of the hands is ever four tiles away.
constexpr double kViewmodelNear = 0.05;
constexpr double kViewmodelFar = 4.0;
/// The depth squeeze: NDC z' = kViewmodelDepthSpan * z - (1 -
/// kViewmodelDepthSpan), so the hands occupy depth [0, 0.2] and the world --
/// whose nearest surface is the body's own radius (90/256 of a tile) from
/// the eye, well past the 0.125 tiles where the world's own projection
/// crosses NDC -0.6 -- always sits behind them.
constexpr float kViewmodelDepthSpan = 0.2F;

[[nodiscard]] inline Color colourOf(const Rgba8& c) noexcept { return Color{c.r, c.g, c.b, c.a}; }

[[nodiscard]] inline Color averageColour(const PieceTints& t) noexcept {
    return Color{static_cast<unsigned char>((t.a.r + t.b.r + t.c.r + t.d.r) / 4),
                 static_cast<unsigned char>((t.a.g + t.b.g + t.c.g + t.d.g) / 4),
                 static_cast<unsigned char>((t.a.b + t.b.b + t.c.b + t.d.b) / 4), t.a.a};
}

[[nodiscard]] inline float channelOf(unsigned char c) noexcept { return static_cast<float>(c) / 255.0F; }

[[nodiscard]] inline Vector3 vec(const Vec3& v) noexcept { return Vector3{v.x, v.y, v.z}; }

template <typename T>
[[nodiscard]] inline T* rlAlloc(std::size_t count) {
    // raylib frees mesh arrays with RL_FREE, which is free(): allocate with
    // the matching malloc so UnloadMesh owns them cleanly.
    return static_cast<T*>(std::calloc(count == 0 ? 1 : count, sizeof(T)));
}

/// Upload a description's mesh or texture to the GPU (resources.cpp).
Mesh uploadMesh(const MeshData& data);
Texture2D uploadTexture(const TextureData& data);

}  // namespace granadad::render3d::rl

namespace granadad::render3d {

using namespace rl;

struct Backend::Impl {
    BackendConfig config;
    /// The context was opened on the offscreen driver (BackendConfig::headless):
    /// no window, no input, no pacing.
    bool headless = false;
    /// GL_RENDERER of the context ("llvmpipe (LLVM 21.1.8, 256 bits)").
    std::string renderer;
    std::map<std::uint32_t, CachedMesh> meshes;
    std::map<std::uint32_t, CachedTexture> textures;
    /// Rig models by FILE NAME; a null entry is a file that was looked for
    /// and not found (or empty modelDir), so it is never asked for again.
    std::map<std::string, std::unique_ptr<RigModel>> rigs;
    /// V LANE. The arms rigs and the static weapons, the same way.
    std::map<std::string, std::unique_ptr<RigModel>> handRigs;
    std::map<std::string, std::unique_ptr<WeaponModel>> weapons;
    /// S LANE. Building pieces by file; a null entry is a file looked for
    /// and not found, never asked for again.
    std::map<std::string, std::unique_ptr<StaticModel>> statics;
    std::vector<DeferredMesh> deferred;
    /// The halos, held back further still -- past the weather's veils --
    /// so a lamp's glow is seen THROUGH the fog rather than veiled with the
    /// wall behind it (the player's second: lanterns went to fog grey
    /// before the sacks beside them). The world scene has already taken the
    /// fog's share off their alpha by distance (kHaloFogShare).
    std::vector<DeferredMesh> halos;
    /// Bright glass (a lit pane, a lamp's) whose pane alpha is under 255:
    /// the world scene's fog share. Drawn once more past the veils at that
    /// alpha over its own veiled pixels, so it reads through the fog by the
    /// share and goes to the fog's grey with distance.
    std::vector<DeferredMesh> lateGlass;
    /// The blend shader (GL 3.3) and its uniform slots; id 0 = not
    /// available, and the pieces draw with the average of their two tints.
    Shader blend{};
    bool blendTried = false;
    int blendTintA = -1;
    int blendTintB = -1;
    int blendTintC = -1;
    int blendTintD = -1;
    int blendSpan = -1;
    int blendMode = -1;

    void ensureBlendShader();
    /// THE SCENE SHADER (every mesh the description names) and the POST PASS
    /// that puts the offscreen scene on the window, with their uniform
    /// slots; id 0 = it did not compile and raylib's default stands in.
    Shader scene{};
    Shader post{};
    bool sceneTried = false;
    int sceneLightMix = -1;
    int sceneSun = -1;
    int sceneSunColour = -1;
    int sceneAmbient = -1;
    int sceneFogDensity = -1;
    int sceneFogColour = -1;
    int postExposure = -1;
    int postVignette = -1;
    /// The shading of the scene being drawn, kept for the post pass.
    SceneShading shadingNow{};
    /// THE OFFSCREEN SCENE: the 3D pass draws into this (colour + a 24-bit
    /// depth renderbuffer) at the window's render size, and resolveScene puts
    /// it on the window through the post shader before the HUD goes over it.
    RenderTexture2D target{};
    bool sceneOpen = false;
    Material material{};
    bool materialLoaded = false;
    /// THE HALO'S FAN (scene.hpp haloFanMesh): one unit disc, uploaded the
    /// first time a flame is drawn and fitted to each halo's own local span
    /// by a matrix. Never in the mesh cache -- it has no id in the
    /// description and no version to move, because it is the same disc for
    /// every lamp in the district.
    Mesh haloFan{};
    bool haloFanReady = false;
    /// THE PANE'S RAMP (scene.hpp paneRampMesh): one unit quad, uploaded
    /// the first time a timber pane is drawn and fitted to each pane's own
    /// local span by a matrix, the same way as the fan.
    Mesh paneRamp{};
    bool paneRampReady = false;
    Texture2D defaultTexture{};
    Texture2D overlay{};
    int overlayWidth = 0;
    int overlayHeight = 0;
    bool relativeMouse = false;
    void ensureMaterial();

    /// Loads the scene and post shaders the first time (shaders.cpp).
    void ensureSceneShader();
    /// Uploads the description's shading uniforms to the scene shader.
    void applyShading(const SceneShading& shading);
    /// Makes the offscreen target exactly `w` x `h` (offscreen.cpp).
    void ensureTarget(int w, int h);
    /// Closes the offscreen pass, if one is open, and draws it over the
    /// window through the post shader (offscreen.cpp).
    void resolveScene();

    void ensureHaloFan();

    void ensurePaneRamp();

    /// The model for a rig, loading it the first time it is asked for.
    /// Null when there is none: the caller draws the placeholder.
    RigModel* rigFor(std::uint8_t rig);




    /// THE GLASS, CUT AND RAMPED (scene.hpp StaticPieceRef). Once, at load:
    /// where the catalogue names a glass band, the triangles of every mesh
    /// that lie in it (by centroid Y and by the atlas patch they sample)
    /// are cut out into a glass mesh of their own, opaque, appended after
    /// the rest; then every glass mesh -- cut here, or a translucent
    /// untextured material's own (a kit window's pane) -- is rebuilt with
    /// the ramp in its vertex colours, so a candle behind glass is hottest
    /// where the flame stands and eases off toward the head, the same on
    /// both rasterizers. The metal keeps its atlas and draws as it did.
    void applyGlass(StaticModel& model, const StaticPieceRef& ref);


    /// S LANE. The model for a piece file, loading it the first time it is
    /// asked for. Null when there is none: the caller draws nothing.
    StaticModel* staticFor(const StaticPieceRef& ref);

    [[nodiscard]] std::size_t staticsLoaded() const noexcept;

    /// S LANE. One piece: every opaque sub-mesh now, the translucent ones
    /// deferred. The tint multiplies the material's own colour factor.
    void drawStatic(const StaticInstance& piece, const SceneDescription& scene,
                    SceneStats& stats);

    /// S LANE. A FLAME'S GLOW: the unit fan (haloFanMesh, scene.hpp) fitted
    /// to the piece's own local span and then put through the piece's own
    /// transform, so it lands where the flame quad used to and turns with
    /// the billboard the world scene aimed. Its alpha is the fan's -- flat
    /// hot core, smooth skirt, nothing at the rim -- times the lamp's own,
    /// which already carries the day fade and the weather's share off
    /// relightPieces.
    ///
    /// Drawn through the backend's OWN default material and no shader: a
    /// glow is light, it does not wear the quad's atlas, and the headless frame
    /// and the GL frame come out the same disc. Two-sided, because a
    /// billboard is light from either face. Depth-TESTED and never written,
    /// the rule the halo has had since it stopped being a bar.
    void drawHaloFan(const Matrix& transform, const PieceTints& tints, SceneStats& stats);

    /// S LANE. A TIMBER PANE: the unit ramp quad (paneRampMesh, scene.hpp)
    /// fitted to the piece's own local span -- gradientFrom..To across and
    /// gradientFromZ..ToZ up, the origin at the quad's bottom-left like the
    /// thin wall it stands in for -- through the piece's own transform, in
    /// the pane tint the world scene decided. Through the backend's own
    /// default material and no shader: the ramp is in the vertex colours,
    /// so the headless frame and the GL frame come out the same glass.
    /// Opaque and depth-written, drawn both sides (a pane seen from the
    /// room is the same pane).
    void drawPaneRamp(const Matrix& transform, const PieceTints& tints, SceneStats& stats);

    /// BRIGHT GLASS THROUGH A FOG: the glass mesh (or the ramp quad, mesh
    /// -1) drawn once more past the veils, blended over its own veiled
    /// pixels at the pane's alpha -- the fog's share the world scene set --
    /// so a lamp at five metres reads hot through the fog and one at twenty
    /// is the fog's grey with a warmth in it. Depth-tested (equal passes),
    /// never written.
    void drawLateGlass(const DeferredMesh& late, SceneStats& stats);

    void drawStaticMesh(StaticModel& model, int i, const Matrix& transform, const PieceTints& tints,
                        bool mirrored, SceneStats& stats);

    [[nodiscard]] std::size_t rigsLoaded() const noexcept;

    /// One cached mesh through the default material with a tint, a texture
    /// and a full transform. False when the mesh is not uploaded (nothing
    /// drawn). The instances and the viewmodel parts both end here.
    bool drawMeshWith(std::uint32_t meshId, std::uint32_t textureId, const Rgba8& tint,
                      const Matrix& transform, SceneStats& stats);

    /// One placeholder-or-chunk instance through the mesh cache. False when
    /// its mesh is not uploaded (nothing drawn).
    bool drawInstance(const Instance& instance, SceneStats& stats);




    /// V LANE. THE FUSED BLADE, RE-SEATED ONCE AT LOAD. The sword export
    /// bakes SM_Wep_Sword_01 into the arms' one skin, parented to Hand_R
    /// with no offset, so its grip runs down the bone's +Y -- through the
    /// wrist and out of the fist's heel. The weapon's vertices are found
    /// by what they are: welded by position, the mesh falls into connected
    /// pieces, and a piece whose every vertex is weighted wholly to Hand_R
    /// (the hand's own skin always shares a vertex with a finger bone) is
    /// the weapon. Those vertices are moved by the kind's socket in the
    /// bone's frame -- the same socket a hung weapon is drawn by -- and
    /// left skinned to the hand, so they follow every clip as before.
    /// Returns how many vertices moved (none on a rig with no fused
    /// weapon, which is the fists).
    int reseatFusedWeapon(RigModel& rig, const ViewmodelSocket& socket);

    /// V LANE. The arms rig for a hand kind, loaded the first time it is
    /// asked for from <modelDir>/<viewmodelRigFileOf(kind)>; null when
    /// there is none (the placeholder parts draw).
    RigModel* handRigFor(std::uint8_t kind);

    /// V LANE. The static weapon for a hand kind from
    /// <weaponDir>/<viewmodelWeaponFileOf(kind)>, once per file; null when
    /// there is none or nothing hangs on this kind.
    WeaponModel* weaponFor(std::uint8_t kind);

    /// V LANE. THE HANDS, in their own pass -- see the file header, item 4.
    void drawViewmodel(const ViewmodelInstance& hands, SceneStats& stats);

    /// One body: its rig model, animated when skinned, else its placeholder.
    void drawActor(const ActorInstance& actor, SceneStats& stats);

    void release();
};

}  // namespace granadad::render3d
