#pragma once

// THE SCENE DESCRIPTION -- the one thing the 3D renderer draws.
//
// Everything the raylib adapter (granadad-render3d-rl) puts on screen comes
// through this struct, and nothing else: meshes as plain vertex arrays,
// textures as plain RGBA bytes, instances as (mesh, transform, tint), a camera
// and a clear colour. It is built by PURE C++ in granadad-render3d-core --
// the chunk mesher, the actor instancer, the viewmodel -- as a function of
// Session state, and it never sees raylib.h.
//
// WHY A DESCRIPTION AND NOT DRAW CALLS. The determinism claim of this project
// stops at the GPU: a frame drawn by a driver cannot be hashed across
// machines, and the plan says so out loud. What CAN be hashed is this struct.
// sceneHash() takes an FNV-1a over its canonical byte image, so two sessions
// driven by the same script must produce identical scene bytes -- the 3D
// equivalent of `CHECK(a.pixels() == b.pixels())`, float-only-from-integers,
// and it runs with no renderer at all. The software rasterizer (rlsw) then
// draws the SAME description in the docker gate and its pixels are compared
// byte for byte in-process; the GPU build is proved by stats and by a
// similarity report, never by a hash. See docs/3d (the renderer plan) for the
// three tiers.
//
// SCENE SPACE. raylib's right-handed, Y-up frame at ONE UNIT = ONE TILE:
//
//     X = east   (world x, tiles)
//     Y = up     (world z, in TILES -- render::bandSurface() already scales
//                bands to tiles; sim/vertical_scale.hpp owns the number)
//     Z = south  (world y, tiles)
//
// (east, up, south) is a proper right-handed triple -- east x up = south --
// so the mapping is a rotation, not a mirror, and counter-clockwise triangles
// stay counter-clockwise. Front faces are CCW seen from outside; the adapter
// culls back faces. Yaw follows the body: 0 faces north (-Z) and increases
// CLOCKWISE seen from above, exactly render::Camera's own convention.
//
// MESH IDS are owned by the lane that emits them and never reused for a
// different shape (the adapter caches uploads by id and re-uploads only when
// `version` moves):
//
//     1 .. 999          starter / debug meshes (this header); 900 is the
//                       sky dome -- world_scene.hpp
//     1000 .. 99999     chunk meshes  -- chunk_mesher.hpp
//     100000 .. 199999  actor rigs    -- actor_instances.hpp
//     200000 .. 209999  viewmodel     -- viewmodel.hpp
//
// STATIC PIECES (static_pieces.hpp) carry no mesh id at all: a StaticInstance
// names a row of the description's own `pieces` table (a glTF file under the
// static model directory) and the adapter draws the loaded model there, or
// nothing when the file is absent -- the chunk mesh under it is the
// placeholder, always present, so a build without the licensed export sees
// the atlas-textured district it always saw.
//
// Floats are legal here and only here (render-side). Nothing in this header
// is allowed anywhere near simulation state.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/viewmodel_machine.hpp"

namespace granadad::render {
struct Camera;
}

namespace granadad::render3d {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Rgba8 {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

/// Mesh id spaces. See the header on who owns which range.
inline constexpr std::uint32_t kStarterGroundMeshId = 1;
inline constexpr std::uint32_t kStarterCubeMeshId = 2;
inline constexpr std::uint32_t kChunkMeshIdBase = 1000;
inline constexpr std::uint32_t kActorMeshIdBase = 100000;
inline constexpr std::uint32_t kViewmodelMeshIdBase = 200000;

/// Plain vertex arrays. Indexed triangles, CCW front faces, at most 65535
/// vertices (raylib indexes with unsigned short; a chunk is 16x16 columns so
/// it never gets near that, and an actor rig is a few thousand).
struct MeshData {
    /// Stable identity -- the adapter keys its upload cache on it.
    std::uint32_t id = 0;
    /// Bumped whenever the arrays change (a chunk rebuilt, a recolour when the
    /// time-of-day bucket moves). Same id + same version = nothing re-uploads.
    std::uint32_t version = 0;
    /// xyz per vertex.
    std::vector<float> positions;
    /// uv per vertex. Empty means "all zero" (untextured).
    std::vector<float> texcoords;
    /// rgba per vertex -- THIS IS THE LIGHTING. Vertex colours, no shaders:
    /// what makes the rlsw frame and the GPU frame the same picture. Empty
    /// means white.
    std::vector<std::uint8_t> colours;
    /// Three per triangle.
    std::vector<std::uint16_t> indices;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size() / 3; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

/// An RGBA8 image the adapter uploads once per (id, version). The tile atlas
/// (content/art/custom/tiles.png) and actor skins ride in here.
struct TextureData {
    std::uint32_t id = 0;
    std::uint32_t version = 0;
    int width = 0;
    int height = 0;
    /// R,G,B,A per pixel, top row first -- render::Framebuffer's own order.
    std::vector<std::uint8_t> pixels;
};

/// One drawn thing: a mesh at a place. Positions arrive as floats DERIVED
/// FROM the sim's Q8 integers at description time and are never written back
/// (the 2026-07-31 ruling quoted in session.cpp's wardSprites).
struct Instance {
    std::uint32_t meshId = 0;
    /// 0 = untextured (the mesh's vertex colours alone).
    std::uint32_t textureId = 0;
    Vec3 position;
    /// Radians, clockwise from above, 0 faces north (-Z).
    float yaw = 0.0F;
    float scale = 1.0F;
    /// Multiplies the vertex colours. Fog and distance dimming live here on
    /// the rlsw path, where there is no shader to do it.
    Rgba8 tint;
};

struct SceneCamera {
    Vec3 position;
    Vec3 target;
    Vec3 up{0.0F, 1.0F, 0.0F};
    /// Vertical field of view in degrees -- what raylib's Camera3D takes.
    float fovyDegrees = 60.0F;
};

/// Which clip a body plays, decided off sim state alone -- see
/// actor_instances.hpp for the table (Activity / moved-this-tick -> clip).
/// THE ORDER IS THE ASSET CONTRACT: content/art/lot-3d/characters/*.glb carry
/// their animations at index 0..7 in exactly this order (idle, walk,
/// punch_l, punch_r, block, hit, recover, death -- the asset lane's job
/// file), so `static_cast<int>(clip)` is the animation index the adapter
/// plays. Nothing may be inserted in the middle.
enum class ActorClip : std::uint8_t { Idle, Walk, PunchLeft, PunchRight, Block, Hit, Recover, Death };
inline constexpr std::size_t kActorClipCount = 8;

/// ONE BODY, DRAWN. The A lane's unit: a placeholder-or-rig instance plus
/// what it is doing. `instance.meshId` is always the KIND's PLACEHOLDER mesh
/// (actorRigMeshId(kind), which the core puts into the description), so a
/// build without the licensed glb files -- every test, the docker gate --
/// draws a box figure through the generic mesh path; the adapter swaps in
/// the skinned model by the rig's file name when it has one.
struct ActorInstance {
    Instance instance;
    /// The rig index: the sim's WardType value (0..15) for a kind's own look,
    /// or a variant look above that (the townswoman, 16) -- a pure function
    /// of the kind and the actor id (actor_instances.hpp, actorRigFor).
    /// instance.meshId stays the kind's placeholder; actorRigFile(rig) is
    /// the glb the adapter looks for.
    std::uint8_t rig = 0;
    ActorClip clip = ActorClip::Idle;
    /// Clip frame at 60 per second (one per movement step, which is what
    /// the glb keyframes are baked at): body().stepCount() phase-shifted by
    /// the actor id so a crowd does not march in step. Render-only, derived
    /// from integers, and part of the hash.
    std::uint32_t clipFrame = 0;
    /// Inside the skin radius: the adapter animates the rig. Outside: a rest
    /// pose, no per-body skinning -- the cheap far draw.
    bool skinned = false;
};

/// The glb file (no directory) the adapter loads for this rig, or empty for
/// a rig that only has its placeholder (every beast, today). The names are
/// the asset lane's: content/art/lot-3d/characters/<name>.glb, one skin,
/// animations 0..7 in ActorClip order (8 and 9, a sword swing and a spell
/// blast, ride along unused). Defined in actor_instances.cpp.
[[nodiscard]] std::string_view actorRigFile(std::uint8_t rig) noexcept;

/// True for a clip that plays once and holds its last frame (a corpse stays
/// down); false for one that loops.
[[nodiscard]] constexpr bool actorClipOneShot(ActorClip clip) noexcept {
    return clip == ActorClip::Death || clip == ActorClip::Recover || clip == ActorClip::Hit;
}

/// V LANE. The arms glb (under BackendConfig::modelDir) and the static
/// weapon .gltf (under BackendConfig::weaponDir, empty when nothing hangs
/// on the hand) for a ViewmodelInstance::kind byte -- declared here, keyed
/// by the byte, so the one raylib TU never includes the sim's weapon
/// header. Defined in viewmodel.cpp beside their typed twins.
[[nodiscard]] std::string_view viewmodelRigFileOf(std::uint8_t kind) noexcept;
[[nodiscard]] std::string_view viewmodelWeaponFileOf(std::uint8_t kind) noexcept;

/// V LANE. True for a kind whose weapon is FUSED into the arms glb's one
/// skin (the sword: the export parents SM_Wep_Sword_01 to Hand_R and bakes
/// it into the mesh), so the socket below is applied to those vertices
/// once at load rather than to a hung model per frame. Defined in
/// viewmodel.cpp.
[[nodiscard]] bool viewmodelWeaponFusedOf(std::uint8_t kind) noexcept;

/// V LANE. ONE PART OF THE PLAYER'S OWN HANDS, IN VIEW SPACE: +X right, +Y
/// up, -Z forward, the eye at the origin -- the frame the adapter's second
/// 3D pass draws in (a camera at the origin looking down -Z), so the arms
/// ride the camera wherever it points and jolt WITH it, the way a body's
/// own hands do. Rotation is applied roll (about Z), then pitch (about X),
/// then yaw (about Y), then the part is placed; scale first of all. The
/// angles are plain right-handed radians about the VIEW axes -- not the
/// scene's clockwise yaw: positive pitch lifts a part's -Z front upwards,
/// positive yaw turns it towards -X (the left of the frame).
struct ViewmodelPart {
    std::uint32_t meshId = 0;
    Vec3 position;
    /// Radians: x = pitch, y = yaw, z = roll -- see the order above.
    Vec3 rotation;
    float scale = 1.0F;
    /// Multiplies the vertex colours, the Instance rule.
    Rgba8 tint;
};

/// V LANE. THE VIEWMODEL, DRAWN: the placeholder parts (always, in every
/// build) plus what a licensed rig would need instead of them. The adapter
/// draws the parts through the generic mesh path when it has no
/// viewmodel glb for `kind`, and the skinned arms (clip == state, scrubbed
/// or played by `phase`/`stateSteps`) when it does. Hashed whole.
struct ViewmodelInstance {
    /// False leaves the second pass out entirely (a --2d frame, a test that
    /// wants the world alone).
    bool visible = false;
    /// render3d::ViewmodelKind as a byte: which hand model (fists, club,
    /// dagger, sword) -- picks the placeholder set and the glb.
    std::uint8_t kind = 0;
    render::ViewmodelState state = render::ViewmodelState::Idle;
    /// Steps into the state (the machine's own counter).
    std::int32_t stateSteps = 0;
    /// Where in the clip, 0..1: the charge fraction while Charging, the
    /// one-shot's progress while a swing/cast/hit plays, 0 for a loop.
    float phase = 0.0F;
    /// Vertical field of view of the second pass, degrees. Fixed, so the
    /// hands frame the same whatever the world's FOV slider says.
    float fovyDegrees = 55.0F;
    /// THE RIG'S PLACEMENT, the glb path's framing: its origin (the feet)
    /// at rigOffset in view space, turned rigYaw about +Y (radians, the
    /// scene's clockwise convention; a half turn faces the glTF front down
    /// -Z), scaled rigScale, and the whole of it then pitched rigPitch
    /// about the EYE's own X axis (radians, positive lifts what is in front
    /// of the eye) -- the lean-back that brings a body's forearms up from
    /// the bottom of the frame the way a first-person rig is framed, since
    /// the clips are a standing body's and its guard is at its hips. The
    /// adapter composes scale, yaw, offset, pitch in that order.
    Vec3 rigOffset;
    float rigYaw = 0.0F;
    float rigScale = 1.0F;
    float rigPitch = 0.0F;
    /// WHICH CLIP the adapter plays and WHERE in it, 0..1 over the clip's
    /// keyframes: the V lane's own choice per state (a guard held on one
    /// frame of the block clip, a punch scrubbed from its cock, the swing
    /// from there to its end), never the raw state index. Decided in
    /// viewmodel.cpp, so a case can pin it without a renderer.
    std::uint8_t rigClip = 0;
    float rigFrame = 0.0F;
    /// THE WEAPON SOCKET for this kind: where the held weapon's origin sits
    /// in the Hand_R bone's own frame (metres along the bone's axes) and
    /// how it is turned there (radians about the bone's X, then Y, then Z),
    /// so a blade lies across the closed fingers and points out of the
    /// thumb side of the fist instead of hanging down the wrist. Applied by
    /// the adapter to a hung weapon per frame and to a fused one once at
    /// load. A pure function of the kind (viewmodelSocketOf), carried here
    /// so the hash covers what is drawn.
    Vec3 socketOffset;
    Vec3 socketRotation;
    /// The light where the body stands, as a tint over every part.
    Rgba8 tint;
    std::vector<ViewmodelPart> parts;
};

/// V LANE. The weapon socket for a kind byte -- see ViewmodelInstance --
/// declared here so the one raylib TU can apply it to a fused blade at load
/// without the description in hand. Defined in viewmodel.cpp.
struct ViewmodelSocket {
    Vec3 offset;
    Vec3 rotation;
};
[[nodiscard]] ViewmodelSocket viewmodelSocketOf(std::uint8_t kind) noexcept;

/// S LANE. One row of the static-piece table: the glTF file (pack directory
/// and file name, "PolygonGeneric/SM_Bld_Base_Wall_01.gltf") the adapter
/// loads once under BackendConfig::staticDir. The table is the catalogue's
/// (content/raws/world3d/*.json) in its own order, put by the world scene;
/// StaticInstance::piece indexes it. Hashed with the placements, so the
/// description is a function of the tiles AND the catalogue.
struct StaticPieceRef {
    std::string file;
};

/// S LANE. ONE SYNTY BUILDING PIECE, PLACED: a wall segment on a wall tile's
/// exposed face, a corner on a corner tile, a door frame in a door gap, a
/// plank over a pier, a barrel against a wall. Positions and yaws come off
/// the integer tile grid through the placement rules in static_pieces.cpp
/// (never off the sim's state), the scale is NON-UNIFORM because the kit's
/// 2.5 m module is fitted to whole tile runs, and the tint is the light where
/// the piece stands times the catalogue's own tint for the role and material
/// -- the same ambient + baked + dynamic light the chunk faces wear.
struct StaticInstance {
    /// Index into SceneDescription::pieces.
    std::uint16_t piece = 0;
    /// static_pieces.hpp's PieceRole as a byte: what rule put it here.
    std::uint8_t role = 0;
    Vec3 position;
    /// Radians, clockwise from above, 0 faces north (-Z) -- the Instance rule.
    float yaw = 0.0F;
    /// Radians about the piece's own X, applied after the scale and before
    /// the yaw: what stands a flat plank quad up as a timber wall.
    float pitch = 0.0F;
    /// Radians about the piece's own Z, applied after the scale and before
    /// the pitch: what stands a plank quad up with its planks ACROSS (a
    /// hull's strakes), and leans it.
    float roll = 0.0F;
    Vec3 scale{1.0F, 1.0F, 1.0F};
    /// The light at the piece's local x = gradientFrom end...
    Rgba8 tint;
    /// ...and at its local x = gradientTo end, blended across the piece so a
    /// wall segment under a lamp is lit along its length the way the cells
    /// it spans are, not as one flat step. Equal ends (or an empty span) are
    /// a flat tint; the GPU adapter blends, the software one averages.
    Rgba8 tint2;
    float gradientFrom = 0.0F;
    float gradientTo = 0.0F;
    /// The second row of the blend, for a flat block lit at its four
    /// corners: tint3 / tint4 are the light at the local z = gradientToZ
    /// edge (over x = gradientFrom .. gradientTo), tint / tint2 the
    /// z = gradientFromZ edge; the adapter blends bilinearly. A run piece
    /// carries tint3 == tint and tint4 == tint2 with an empty Z span.
    Rgba8 tint3;
    Rgba8 tint4;
    float gradientFromZ = 0.0F;
    float gradientToZ = 0.0F;
    /// What a pane of glass in the piece is drawn with: dark (the room
    /// behind it is unlit, or it is day) or the warm lit-window tint. The
    /// world scene decides; the adapter draws it verbatim.
    Rgba8 pane{40, 44, 52, 255};
    /// How the adapter shades the piece: kDrawPlain (the tints alone);
    /// kDrawHalo (the piece's own mesh is NOT drawn at all -- the adapter
    /// draws haloFanMesh() fitted to the piece's local span instead,
    /// gradientFrom..To across and gradientFromZ..ToZ up, a flame's glow);
    /// kDrawShaded (the tint darkened on faces that look down and lifted on
    /// faces that look up, from the mesh's own normals: the volume a prop
    /// needs when nothing lights it).
    std::uint8_t mode = 0;
};

inline constexpr std::uint8_t kDrawPlain = 0;
inline constexpr std::uint8_t kDrawHalo = 1;
inline constexpr std::uint8_t kDrawShaded = 2;

/// THE HALO IS GEOMETRY, NOT A SHADER. It used to be the flame's quad with a
/// radial alpha in the GL 3.3 blend shader -- which rlsw has not got, so the
/// headless twin drew the same lamp as a flat pale RECTANGLE with corners in
/// it, and the shipped exe and the gate disagreed about the brightest thing
/// on the street. The critic called it "a quad, not a flame" and was reading
/// the software frame.
///
/// So the falloff lives in vertex colours now, which both rasterizers carry
/// verbatim: a fan of `kHaloFanSegments` wedges over `kHaloFanRings` rings in
/// a UNIT DISC (local XY, z = 0, radius 1), white, its alpha TWO-STOP -- flat
/// and full out to kHaloCoreFraction of the radius (the flame's own hot core,
/// about 0.12 m on a lantern's halo) and then a smooth (1 - t)^2 skirt to
/// exactly nothing at the rim. Zero at the rim is what lets the disc be as
/// wide as the light it stands for: where the far edge of a tipped billboard
/// sinks into the plaster the depth test cuts it, and a cut through alpha 0
/// is a cut nobody can see.
///
/// The adapter uploads it ONCE and fits it to each halo's own local span.
inline constexpr int kHaloFanSegments = 24;
inline constexpr int kHaloFanRings = 5;
inline constexpr float kHaloCoreFraction = 0.22F;

[[nodiscard]] MeshData haloFanMesh();

struct SceneDescription {
    SceneCamera camera;
    /// The sky -- what the frame is cleared to before anything draws.
    Rgba8 clearColour{0, 0, 0, 255};
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
    std::vector<Instance> instances;
    /// The people, after the world. Hashed like everything else here.
    std::vector<ActorInstance> actors;
    /// The player's own hands, after the people, in their own pass.
    ViewmodelInstance viewmodel;
    /// S LANE. The static-piece table and the pieces placed this frame (only
    /// those within their role's reach of the eye), drawn after the chunks
    /// and before the people. Hashed like everything else here.
    std::vector<StaticPieceRef> pieces;
    std::vector<StaticInstance> statics;
    /// WEATHER. The veils: translucent instances drawn LAST inside the 3D
    /// pass -- after the people and the deferred glass, before the hands --
    /// with the depth test on, so whatever stands nearer than a veil is
    /// clear of it and whatever stands beyond it is seen through it. The
    /// harbour fog is one of these (world_scene.hpp, buildVeil): nested
    /// shells round the eye, far to near. Empty in clear weather, which is
    /// every frame drawn before the weather lane. Hashed like everything
    /// else here.
    std::vector<Instance> veils;

    [[nodiscard]] const MeshData* findMesh(std::uint32_t id) const noexcept;
    [[nodiscard]] MeshData* findMesh(std::uint32_t id) noexcept;
    /// Adds or replaces the mesh with this id.
    void putMesh(MeshData mesh);
    /// Drops the mesh with this id, if it is here. The adapter keeps its
    /// upload (nothing references it), and the description is once again
    /// what it was before the mesh was put.
    void removeMesh(std::uint32_t id);
    void putTexture(TextureData texture);
};

/// FNV-1a, 64-bit, streamed. The hash every determinism claim on this side of
/// the line rests on, so it is spelled out rather than borrowed.
class Fnv1a64 {
public:
    static constexpr std::uint64_t kOffset = 0xCBF29CE484222325ULL;
    static constexpr std::uint64_t kPrime = 0x100000001B3ULL;

    void mix(const void* bytes, std::size_t count) noexcept;
    void mixU8(std::uint8_t value) noexcept { mix(&value, 1); }
    void mixU16(std::uint16_t value) noexcept;
    void mixU32(std::uint32_t value) noexcept;
    void mixU64(std::uint64_t value) noexcept;
    void mixI32(std::int32_t value) noexcept { mixU32(static_cast<std::uint32_t>(value)); }
    /// The float's IEEE bit pattern, so -0.0 and 0.0 hash apart on purpose:
    /// a description that differs in a sign bit is a different description.
    void mixF32(float value) noexcept;

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = kOffset;
};

/// The canonical byte image of a description, hashed. Field by field, in a
/// fixed order, with every count written before the bytes it counts -- never
/// a memcpy of a struct, so padding and layout cannot leak in.
[[nodiscard]] std::uint64_t sceneHash(const SceneDescription& scene) noexcept;

/// The camera adapter: render::Camera (x east, y south, z up in tiles; yaw
/// BAM-derived radians clockwise from north; pitch radians positive up;
/// tan(half horizontal fov)) into scene space. `aspect` is the frame's
/// width/height -- the vertical fov raylib wants is derived from the
/// horizontal one the settings page carries.
[[nodiscard]] SceneCamera cameraFrom(const render::Camera& camera, float aspect) noexcept;

/// World tiles (east, south, up) to scene space. The one place the axis swap
/// is written down.
[[nodiscard]] constexpr Vec3 toScene(float east, float south, float up) noexcept {
    return Vec3{east, up, south};
}

}  // namespace granadad::render3d
