// THE ONE TRANSLATION UNIT THAT INCLUDES raylib.h. See backend.hpp for the
// contract and for why it is one file and not a sprinkling.
//
// Two things are decided here and nowhere else:
//
//   1. HOW A SceneDescription BECOMES DRAW CALLS. Meshes are uploaded once
//      per (id, version) as raylib Meshes and drawn with the default
//      material -- vertex colours times an optional texture, no lighting
//      shader -- so the rlsw build and the GL 3.3 build rasterize the same
//      bytes. Instances are (scale, yaw about +Y, translate) transforms.
//
//   2. HOW THE PLATFORM DIFFERENCES ARE HIDDEN. raylib's PLATFORM_MEMORY /
//      GRAPHICS_API_OPENGL_SOFTWARE defines reach this file (they are
//      PUBLIC on the raylib target), and every place the headless build has
//      to behave differently is one #if here. Three of them, all verified
//      against raylib 6.0's own source rather than its docs:
//
//      - rcore_memory.c's PollInputEvents polls STDIN for an ESC (a kbhit /
//        getch pair that puts the terminal into raw mode); a test process
//        has no business touching stdin, so the headless build flushes the
//        batch and never calls EndDrawing() at all -- it has no screen to
//        swap. (It does compile on Linux: the file carries its own termios
//        kbhit for non-Windows hosts.)
//      - rlsw keeps its colour buffer bottom row first, the way a GL
//        framebuffer does, and hands it over TOP row first through
//        rlCopyFramebuffer -- the call rcore_memory's own SwapScreenBuffer
//        presents with. LoadImageFromScreen would flip it a second time, so
//        readback on this platform goes through rlCopyFramebuffer instead.
//      - the OpenGL 1.1 draw path (which the software rasterizer is) lets a
//        colour array REPLACE the current colour, so the material tint
//        DrawMesh sets is ignored by every mesh that carries colours -- all
//        of ours. The tint is folded into the vertex colours per draw there,
//        which is the arithmetic the GL 3.3 default shader does anyway.
//
//   3. (A LANE) HOW A BODY IS DRAWN. scene.actors carry a rig index and a
//      clip; the adapter loads <modelDir>/<actorRigFile(rig)> ONCE per file
//      through raylib's cgltf loader (LoadModel + LoadModelAnimations, the
//      glb's animations at index == ActorClip), plays the clip with
//      UpdateModelAnimation (CPU skinning at this pin, SUPPORT_GPU_SKINNING
//      is 0; raylib 6.0's frame wraps by keyframeCount and the glb bakes
//      60 keyframes a second, one per movement step) and draws it with
//      DrawModelEx. No file -- every test, the docker gate -- and the
//      body's placeholder mesh (in the description, under instance.meshId)
//      goes through the same path as any instance. glTF's front is +Z and
//      the scene's yaw 0 faces -Z, hence kGltfForwardYaw.
//
//   5. (S LANE) HOW A STATIC PIECE IS DRAWN. scene.statics name a row of
//      scene.pieces (a glTF file under staticDir); the adapter loads each
//      file ONCE (LoadModel: cgltf reads the .bin and the pack atlas beside
//      it, one raylib Mesh per primitive with its own material, base colour
//      texture only per the export) and draws every mesh of it with the
//      instance's non-uniform scale, yaw and place, the material's own
//      colour factor multiplied by the instance tint. A sub-mesh whose
//      material is translucent (window glass, the water plane) is held
//      back and drawn after the people, so a body behind a window is not
//      cut out by the glass's depth. No file: nothing is drawn and the
//      chunk mesh underneath stands, which is the placeholder rule.
//
//   4. (V LANE) HOW THE PLAYER'S OWN HANDS ARE DRAWN. A second BeginMode3D
//      after the world's, with a camera at the origin looking down -Z (the
//      parts are described in view space), and its projection REPLACED by
//      one whose z row is squeezed into the front fifth of the depth range
//      (kViewmodelDepthSpan): every fragment of the hands lands in front of
//      every fragment of the world, whatever wall the body stands against,
//      while the hands still depth-test among themselves. That is the
//      classic glDepthRange trick done in the matrix, because rlgl exposes
//      no depth range, no depth-only clear and (on rlsw) no framebuffer
//      object -- and a matrix is the one thing both rlgl paths take
//      verbatim: rlLoadIdentity + rlMultMatrixf is RLGL.State.projection on
//      GL 3.3 and glLoadIdentity + glMultMatrixf on the GL 1.1 shim, in
//      the same column-major layout BeginMode3D's own view matrix uses.
//      The placeholder parts draw through the mesh path with a full
//      view-space transform; a licensed arms glb (viewmodel_*.glb, clips at
//      index == ViewmodelState) is played by phase and drawn at the rig
//      offset, with the static weapon .gltf hung on its Hand_R bone.

#include "granadad/render3d/backend.hpp"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render3d {

namespace {

#if defined(PLATFORM_MEMORY)
constexpr bool kHeadless = true;
#else
constexpr bool kHeadless = false;
#endif

/// One window per process, which is also raylib's own rule.
bool g_backendOpen = false;

/// raylib key (GLFW's US-layout physical key tokens) -> USB HID usage id,
/// page 7 -- SDL's scancode numbering. GLFW's tokens name the PHYSICAL key
/// as it sits on a US board, which is the same thing a scancode is.
struct KeyRow {
    int key;
    std::uint16_t hid;
};

constexpr KeyRow kKeyTable[] = {
    {KEY_A, 4},           {KEY_B, 5},           {KEY_C, 6},           {KEY_D, 7},
    {KEY_E, 8},           {KEY_F, 9},           {KEY_G, 10},          {KEY_H, 11},
    {KEY_I, 12},          {KEY_J, 13},          {KEY_K, 14},          {KEY_L, 15},
    {KEY_M, 16},          {KEY_N, 17},          {KEY_O, 18},          {KEY_P, 19},
    {KEY_Q, 20},          {KEY_R, 21},          {KEY_S, 22},          {KEY_T, 23},
    {KEY_U, 24},          {KEY_V, 25},          {KEY_W, 26},          {KEY_X, 27},
    {KEY_Y, 28},          {KEY_Z, 29},          {KEY_ONE, 30},        {KEY_TWO, 31},
    {KEY_THREE, 32},      {KEY_FOUR, 33},       {KEY_FIVE, 34},       {KEY_SIX, 35},
    {KEY_SEVEN, 36},      {KEY_EIGHT, 37},      {KEY_NINE, 38},       {KEY_ZERO, 39},
    {KEY_ENTER, 40},      {KEY_ESCAPE, 41},     {KEY_BACKSPACE, 42},  {KEY_TAB, 43},
    {KEY_SPACE, 44},      {KEY_MINUS, 45},      {KEY_EQUAL, 46},      {KEY_LEFT_BRACKET, 47},
    {KEY_RIGHT_BRACKET, 48}, {KEY_BACKSLASH, 49}, {KEY_SEMICOLON, 51}, {KEY_APOSTROPHE, 52},
    {KEY_GRAVE, 53},      {KEY_COMMA, 54},      {KEY_PERIOD, 55},     {KEY_SLASH, 56},
    {KEY_CAPS_LOCK, 57},  {KEY_F1, 58},         {KEY_F2, 59},         {KEY_F3, 60},
    {KEY_F4, 61},         {KEY_F5, 62},         {KEY_F6, 63},         {KEY_F7, 64},
    {KEY_F8, 65},         {KEY_F9, 66},         {KEY_F10, 67},        {KEY_F11, 68},
    {KEY_F12, 69},        {KEY_PRINT_SCREEN, 70}, {KEY_SCROLL_LOCK, 71}, {KEY_PAUSE, 72},
    {KEY_INSERT, 73},     {KEY_HOME, 74},       {KEY_PAGE_UP, 75},    {KEY_DELETE, 76},
    {KEY_END, 77},        {KEY_PAGE_DOWN, 78},  {KEY_RIGHT, 79},      {KEY_LEFT, 80},
    {KEY_DOWN, 81},       {KEY_UP, 82},         {KEY_NUM_LOCK, 83},   {KEY_KP_DIVIDE, 84},
    {KEY_KP_MULTIPLY, 85}, {KEY_KP_SUBTRACT, 86}, {KEY_KP_ADD, 87},   {KEY_KP_ENTER, 88},
    {KEY_KP_1, 89},       {KEY_KP_2, 90},       {KEY_KP_3, 91},       {KEY_KP_4, 92},
    {KEY_KP_5, 93},       {KEY_KP_6, 94},       {KEY_KP_7, 95},       {KEY_KP_8, 96},
    {KEY_KP_9, 97},       {KEY_KP_0, 98},       {KEY_KP_DECIMAL, 99}, {KEY_KP_EQUAL, 103},
    {KEY_KB_MENU, 118},   {KEY_LEFT_CONTROL, 224}, {KEY_LEFT_SHIFT, 225}, {KEY_LEFT_ALT, 226},
    {KEY_LEFT_SUPER, 227}, {KEY_RIGHT_CONTROL, 228}, {KEY_RIGHT_SHIFT, 229}, {KEY_RIGHT_ALT, 230},
    {KEY_RIGHT_SUPER, 231},
};

/// raylib mouse button -> SDL's numbering (1 left, 2 middle, 3 right, 4/5 the
/// side buttons).
struct MouseRow {
    int button;
    std::uint8_t sdl;
};

constexpr MouseRow kMouseTable[] = {
    {MOUSE_BUTTON_LEFT, 1}, {MOUSE_BUTTON_MIDDLE, 2}, {MOUSE_BUTTON_RIGHT, 3},
    {MOUSE_BUTTON_SIDE, 4}, {MOUSE_BUTTON_EXTRA, 5},
};

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
};

/// S LANE. The four blend corners of a piece and their spans: the light
/// at local (x, z) = (from, fromZ), (to, fromZ), (from, toZ), (to, toZ).
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

[[nodiscard]] Color colourOf(const Rgba8& c) noexcept { return Color{c.r, c.g, c.b, c.a}; }

#if !defined(GRAPHICS_API_OPENGL_11)
/// S LANE. The static pieces' own vertex shader: raylib's default with the
/// tint blended BILINEARLY over the piece's local X and Z between four
/// colours, so a wall segment reads the light of the cells it spans
/// instead of one flat step and a floor block reads its four corners
/// instead of one patch. A run piece passes an empty Z span (its factor
/// 0), a point piece both. GL 3.3 only; the software rasterizer has no
/// shaders and never has the licensed files to draw with it anyway.
/// Two more things the same shader does, by `pieceMode` (scene.hpp): a
/// HALO (1) takes its alpha to nothing at the edge of the quad, radially
/// over the piece's local XY (the span uniform then carries X and Y), so a
/// flame's glow is a soft disc and not a square; a SHADED piece (2) darkens
/// faces that look down (to half) and lifts faces that look up (by an
/// eighth) from the mesh's own normals, the vertical faces untouched: the
/// volume a prop needs when nothing lights it, and the difference between
/// a rowboat and a dome.
constexpr const char* kBlendVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matNormal;\n"
    "uniform vec4 tintA;\n"
    "uniform vec4 tintB;\n"
    "uniform vec4 tintC;\n"
    "uniform vec4 tintD;\n"
    "uniform vec4 tintSpan;\n"
    "uniform int pieceMode;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec2 haloUV;\n"
    "void main() {\n"
    "    float tx = clamp((vertexPosition.x - tintSpan.x) * tintSpan.y, 0.0, 1.0);\n"
    "    float tz = clamp((vertexPosition.z - tintSpan.z) * tintSpan.w, 0.0, 1.0);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    vec4 tint = mix(mix(tintA, tintB, tx), mix(tintC, tintD, tx), tz);\n"
    "    haloUV = vec2(0.0, 0.0);\n"
    "    if (pieceMode == 1) {\n"
    "        tint = tintA;\n"
    "        haloUV = vec2((vertexPosition.x - tintSpan.x) * tintSpan.y * 2.0 - 1.0,\n"
    "                      (vertexPosition.y - tintSpan.z) * tintSpan.w * 2.0 - 1.0);\n"
    "    } else if (pieceMode == 2) {\n"
    "        vec3 n = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "        float f = n.y < 0.0 ? 1.0 + 0.5 * n.y : 1.0 + 0.125 * n.y;\n"
    "        tint.rgb *= f;\n"
    "    }\n"
    "    fragColor = vertexColor * tint;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kBlendFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec2 haloUV;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform int pieceMode;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texelColor = texture(texture0, fragTexCoord);\n"
    "    finalColor = texelColor * colDiffuse * fragColor;\n"
    "    if (pieceMode == 1) {\n"
    "        float fall = max(0.0, 1.0 - dot(haloUV, haloUV));\n"
    "        finalColor.a *= fall * fall;\n"
    "    }\n"
    "}\n";
#endif

[[nodiscard]] Color averageColour(const PieceTints& t) noexcept {
    return Color{static_cast<unsigned char>((t.a.r + t.b.r + t.c.r + t.d.r) / 4),
                 static_cast<unsigned char>((t.a.g + t.b.g + t.c.g + t.d.g) / 4),
                 static_cast<unsigned char>((t.a.b + t.b.b + t.c.b + t.d.b) / 4), t.a.a};
}

[[nodiscard]] float channelOf(unsigned char c) noexcept { return static_cast<float>(c) / 255.0F; }

[[nodiscard]] Vector3 vec(const Vec3& v) noexcept { return Vector3{v.x, v.y, v.z}; }

template <typename T>
[[nodiscard]] T* rlAlloc(std::size_t count) {
    // raylib frees mesh arrays with RL_FREE, which is free(): allocate with
    // the matching malloc so UnloadMesh owns them cleanly.
    return static_cast<T*>(std::calloc(count == 0 ? 1 : count, sizeof(T)));
}

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

}  // namespace

struct Backend::Impl {
    BackendConfig config;
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

    void ensureBlendShader() {
        if (blendTried) {
            return;
        }
        blendTried = true;
#if !defined(GRAPHICS_API_OPENGL_11)
        blend = LoadShaderFromMemory(kBlendVertexShader, kBlendFragmentShader);
        if (blend.id != 0 && blend.id != rlGetShaderIdDefault()) {
            blendTintA = GetShaderLocation(blend, "tintA");
            blendTintB = GetShaderLocation(blend, "tintB");
            blendTintC = GetShaderLocation(blend, "tintC");
            blendTintD = GetShaderLocation(blend, "tintD");
            blendSpan = GetShaderLocation(blend, "tintSpan");
            blendMode = GetShaderLocation(blend, "pieceMode");
        } else {
            blend = Shader{};
        }
#endif
    }
    Material material{};
    bool materialLoaded = false;
    Texture2D defaultTexture{};
    Texture2D overlay{};
    int overlayWidth = 0;
    int overlayHeight = 0;
    bool relativeMouse = false;
    /// The colour array a tinted instance is drawn with on the OpenGL 1.1
    /// path; reused across draws so a tinted crowd allocates nothing per frame.
    std::vector<unsigned char> tintScratch;

    void ensureMaterial() {
        if (!materialLoaded) {
            material = LoadMaterialDefault();
            defaultTexture = material.maps[MATERIAL_MAP_DIFFUSE].texture;
            materialLoaded = true;
        }
    }

    /// THE TINT, ON BOTH PATHS. The GL 3.3 default shader multiplies the
    /// vertex colour by the material's diffuse colour. The OpenGL 1.1 path --
    /// which the software rasterizer is; rlgl defines GRAPHICS_API_OPENGL_11
    /// for it -- does not: a colour array replaces the current colour, so the
    /// material colour DrawMesh sets is ignored the moment a mesh carries
    /// colours, and every mesh of ours does. So on that path the tint is
    /// folded into a scratch copy of the colour array per draw, in integers
    /// with rounding, which is the arithmetic the shader does in floats. A
    /// white tint -- the common case -- costs a compare and nothing else.
    Mesh tinted(const Mesh& mesh, const Rgba8& tint) {
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

    /// The model for a rig, loading it the first time it is asked for.
    /// Null when there is none: the caller draws the placeholder.
    RigModel* rigFor(std::uint8_t rig) {
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

    /// S LANE. The model for a piece file, loading it the first time it is
    /// asked for. Null when there is none: the caller draws nothing.
    StaticModel* staticFor(const std::string& file) {
        if (file.empty() || config.staticDir.empty()) {
            return nullptr;
        }
        auto found = statics.find(file);
        if (found != statics.end()) {
            return found->second.get();
        }
        std::unique_ptr<StaticModel> loaded;
        const std::string path = config.staticDir + "/" + file;
        if (FileExists(path.c_str())) {
            auto candidate = std::make_unique<StaticModel>();
            candidate->model = LoadModel(path.c_str());
            if (candidate->model.meshCount > 0) {
                candidate->loaded = true;
                candidate->baseColour.resize(static_cast<std::size_t>(candidate->model.materialCount));
                for (int m = 0; m < candidate->model.materialCount; ++m) {
                    Material& material = candidate->model.materials[m];
                    candidate->baseColour[static_cast<std::size_t>(m)] =
                        material.maps[MATERIAL_MAP_DIFFUSE].color;
#if !defined(PLATFORM_MEMORY)
                    // The pack atlases are flat-colour sheets a few thousand
                    // texels across drawn on 2.5 m walls: mipmapped and
                    // filtered they read as paint, point-sampled they
                    // shimmer. rlsw has neither, and never has the files.
                    Texture2D& texture = material.maps[MATERIAL_MAP_DIFFUSE].texture;
                    if (texture.id != 0 && texture.id != defaultTexture.id) {
                        GenTextureMipmaps(&texture);
                        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
                    }
#endif
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
                loaded = std::move(candidate);
            } else {
                UnloadModel(candidate->model);
                std::printf("granadad: render3d: piece %s did not load; the chunk stands\n",
                            file.c_str());
            }
        }
        StaticModel* result = loaded.get();
        statics.emplace(file, std::move(loaded));
        return result;
    }

    [[nodiscard]] std::size_t staticsLoaded() const noexcept {
        std::size_t count = 0;
        for (const auto& [name, piece] : statics) {
            (void)name;
            if (piece != nullptr) {
                ++count;
            }
        }
        return count;
    }

    /// S LANE. One piece: every opaque sub-mesh now, the translucent ones
    /// deferred. The tint multiplies the material's own colour factor.
    void drawStatic(const StaticInstance& piece, const SceneDescription& scene,
                    SceneStats& stats) {
        if (piece.piece >= scene.pieces.size()) {
            ++stats.staticsMissing;
            return;
        }
        StaticModel* model = staticFor(scene.pieces[piece.piece].file);
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
        // A translucent instance (a flame's halo, its tint alpha under 255)
        // is held back whole, like a pane of glass.
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

    void drawStaticMesh(StaticModel& model, int i, const Matrix& transform, const PieceTints& tints,
                        bool mirrored, SceneStats& stats) {
        const int m = model.model.meshMaterial[i];
        if (m < 0 || m >= model.model.materialCount) {
            return;
        }
        // The blend: the four tints over the piece when the shader is there
        // and they differ -- or the piece asks for a halo or shading, which
        // only the shader does -- their average when it is not (the
        // software path) or they do not.
        const bool spanned = tints.to > tints.from || tints.toZ > tints.fromZ;
        const bool blended = blend.id != 0 && ((spanned && !tints.uniform()) || tints.mode != kDrawPlain);
        const Color tint = blended ? Color{255, 255, 255, tints.a.a} : averageColour(tints);
        const Color base = model.baseColour[static_cast<std::size_t>(m)];
        const auto ch = [](unsigned char a, unsigned char b) {
            return static_cast<unsigned char>((static_cast<unsigned>(a) * static_cast<unsigned>(b) + 127U) /
                                              255U);
        };
        // Glass wears the pane tint the world scene decided -- dark by day
        // and over an unlit room, warm over a lit room at night -- flat,
        // never blended.
        const bool glass = model.glass[static_cast<std::size_t>(i)];
        const Color paneTint = glass ? tints.pane : tint;
        model.model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color =
            Color{ch(base.r, paneTint.r), ch(base.g, paneTint.g), ch(base.b, paneTint.b),
                  ch(base.a, paneTint.a)};
        if (mirrored) {
            rlDisableBackfaceCulling();
        }
        Material& material = model.model.materials[m];
        const Shader keep = material.shader;
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

    [[nodiscard]] std::size_t rigsLoaded() const noexcept {
        std::size_t count = 0;
        for (const auto& [name, rig] : rigs) {
            (void)name;
            if (rig != nullptr) {
                ++count;
            }
        }
        return count;
    }

    /// One cached mesh through the default material with a tint, a texture
    /// and a full transform. False when the mesh is not uploaded (nothing
    /// drawn). The instances and the viewmodel parts both end here.
    bool drawMeshWith(std::uint32_t meshId, std::uint32_t textureId, const Rgba8& tint,
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
        DrawMesh(tinted(mesh->second.mesh, tint), material, transform);
        ++stats.instancesDrawn;
        stats.trianglesDrawn += static_cast<std::size_t>(mesh->second.mesh.triangleCount);
        return true;
    }

    /// One placeholder-or-chunk instance through the mesh cache. False when
    /// its mesh is not uploaded (nothing drawn).
    bool drawInstance(const Instance& instance, SceneStats& stats) {
        // Scale, then yaw, then place. Yaw is clockwise-from-above in the
        // description and raylib's Y rotation is counter-clockwise, hence
        // the sign -- see scene.hpp on the frame.
        const Matrix transform = MatrixMultiply(
            MatrixMultiply(MatrixScale(instance.scale, instance.scale, instance.scale),
                           MatrixRotateY(-instance.yaw)),
            MatrixTranslate(instance.position.x, instance.position.y, instance.position.z));
        return drawMeshWith(instance.meshId, instance.textureId, instance.tint, transform, stats);
    }

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
    int reseatFusedWeapon(RigModel& rig, const ViewmodelSocket& socket) {
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

    /// V LANE. The arms rig for a hand kind, loaded the first time it is
    /// asked for from <modelDir>/<viewmodelRigFileOf(kind)>; null when
    /// there is none (the placeholder parts draw).
    RigModel* handRigFor(std::uint8_t kind) {
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

    /// V LANE. The static weapon for a hand kind from
    /// <weaponDir>/<viewmodelWeaponFileOf(kind)>, once per file; null when
    /// there is none or nothing hangs on this kind.
    WeaponModel* weaponFor(std::uint8_t kind) {
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

    /// V LANE. THE HANDS, in their own pass -- see the file header, item 4.
    void drawViewmodel(const ViewmodelInstance& hands, SceneStats& stats) {
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
        } else {
            for (const ViewmodelPart& part : hands.parts) {
                if (drawMeshWith(part.meshId, 0, part.tint, partTransform(part), stats)) {
                    ++stats.viewmodelPartsDrawn;
                }
            }
        }
        EndMode3D();
    }

    /// One body: its rig model, animated when skinned, else its placeholder.
    void drawActor(const ActorInstance& actor, SceneStats& stats) {
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

    void release() {
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
};

bool Backend::headlessCapable() noexcept { return kHeadless; }

Backend::Backend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<Backend> Backend::open(const BackendConfig& config) {
    if (g_backendOpen) {
        std::printf("granadad: render3d: a backend is already open in this process\n");
        return nullptr;
    }
    // raylib narrates every init step at LOG_INFO; the test log does not
    // need thirty lines of it per case. Warnings and up still print.
    SetTraceLogLevel(LOG_WARNING);

    unsigned int flags = 0;
    if (config.resizable) {
        flags |= FLAG_WINDOW_RESIZABLE;
    }
    if (config.vsync) {
        flags |= FLAG_VSYNC_HINT;
    }
    if (config.unfocused) {
        flags |= FLAG_WINDOW_UNFOCUSED;
    }
    SetConfigFlags(flags);

    const int scale = kHeadless ? 1 : std::max(1, config.windowScale);
    const int width = std::max(1, config.width) * scale;
    const int height = std::max(1, config.height) * scale;
    InitWindow(width, height, config.title.c_str());
    if (!IsWindowReady()) {
        std::printf("granadad: render3d: raylib could not open a %dx%d window\n", width, height);
        return nullptr;
    }
    // The game owns ESC (it backs out of pages and opens the pause menu);
    // raylib must not close the window on it.
    SetExitKey(KEY_NULL);
    // A district is a few hundred tiles across; the default far plane of
    // 4000 wastes depth precision, the default near of 0.05 is fine but
    // stated. The viewmodel lane draws its second pass with its own planes.
    rlSetClipPlanes(0.1, 512.0);

    auto impl = std::make_unique<Impl>();
    impl->config = config;
    g_backendOpen = true;
    return std::unique_ptr<Backend>(new Backend(std::move(impl)));
}

Backend::~Backend() {
    if (impl_ != nullptr) {
        impl_->release();
        CloseWindow();
        g_backendOpen = false;
    }
}

VideoKind Backend::kind() const noexcept { return kHeadless ? VideoKind::Software : VideoKind::Gpu; }

int Backend::width() const noexcept { return GetScreenWidth(); }

int Backend::height() const noexcept { return GetScreenHeight(); }

void Backend::beginFrame(const Rgba8& clear) {
    BeginDrawing();
    ClearBackground(colourOf(clear));
}

SceneStats Backend::drawScene(const SceneDescription& scene) {
    SceneStats stats;
    Impl& impl = *impl_;
    impl.ensureMaterial();

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

OverlayPlacement Backend::overlayPlacement(int frameWidth, int frameHeight) const noexcept {
    OverlayPlacement placement;
    const int windowWidth = GetScreenWidth();
    const int windowHeight = GetScreenHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return placement;
    }
    // The largest integer scale that fits, never below one -- a window
    // smaller than the frame shows the frame's top-left corner rather than
    // a smoothed shrink, exactly as SDL's integer presentation did.
    placement.scale = std::max(1, std::min(windowWidth / frameWidth, windowHeight / frameHeight));
    placement.offsetX = (windowWidth - frameWidth * placement.scale) / 2;
    placement.offsetY = (windowHeight - frameHeight * placement.scale) / 2;
    return placement;
}

void Backend::drawOverlay(const render::Framebuffer& overlay) {
    Impl& impl = *impl_;
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

void Backend::endFrame(render::Framebuffer* capture) {
    // Everything queued in rlgl's batch is rasterized here, which is what
    // makes the readback below a picture of THIS frame.
    rlDrawRenderBatchActive();
    if (capture != nullptr) {
        const int width = GetRenderWidth();
        const int height = GetRenderHeight();
        if (width > 0 && height > 0) {
            *capture = render::Framebuffer(width, height);
#if defined(PLATFORM_MEMORY)
            // rlsw's colour buffer, top row first, R,G,B,A per pixel (the
            // BGRA output switch is thrown off in Dependencies.cmake) -- which
            // is exactly the framebuffer's own 0xAABBGGRR on a little-endian
            // host. The very call rcore_memory presents with; see the file
            // header on why not LoadImageFromScreen here.
            rlCopyFramebuffer(0, 0, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                              capture->pixels().data());
#else
            // The GPU path: glReadPixels off the back buffer, flipped to top
            // row first by raylib, BEFORE the swap below -- the only moment a
            // double-buffered frame is defined.
            Image image = LoadImageFromScreen();
            if (image.data != nullptr && image.width == width && image.height == height) {
                std::memcpy(capture->pixels().data(), image.data,
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                4U);
            }
            UnloadImage(image);
#endif
            // A frame is opaque by definition. rlsw keeps the blended alpha
            // of every pixel a translucent overlay touched, and a PNG of that
            // would be see-through wherever a HUD wash was.
            for (std::uint32_t& pixel : capture->pixels()) {
                pixel |= 0xFF000000U;
            }
        }
    }
#if defined(PLATFORM_MEMORY)
    // No swap and no input poll -- see the file header. The frame already
    // lives in rlsw's own colour buffer, which is where readback read it.
#else
    EndDrawing();
#endif
}

InputFrame Backend::poll() {
    InputFrame in;
    in.closeRequested = WindowShouldClose();
    in.windowWidth = GetScreenWidth();
    in.windowHeight = GetScreenHeight();
#if defined(PLATFORM_MEMORY)
    in.focused = true;
#else
    in.focused = IsWindowFocused();
    for (const KeyRow& row : kKeyTable) {
        if (IsKeyPressed(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, true, false});
        } else if (IsKeyPressedRepeat(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, true, true});
        }
        if (IsKeyReleased(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, false, false});
        }
    }
    const Vector2 position = GetMousePosition();
    for (const MouseRow& row : kMouseTable) {
        if (IsMouseButtonPressed(row.button)) {
            in.buttons.push_back(MouseButtonEdge{row.sdl, true, position.x, position.y});
        }
        if (IsMouseButtonReleased(row.button)) {
            in.buttons.push_back(MouseButtonEdge{row.sdl, false, position.x, position.y});
        }
    }
    for (int codepoint = GetCharPressed(); codepoint != 0; codepoint = GetCharPressed()) {
        in.chars.push_back(static_cast<std::uint32_t>(codepoint));
    }
    const Vector2 delta = GetMouseDelta();
    in.mouseX = position.x;
    in.mouseY = position.y;
    in.mouseDeltaX = delta.x;
    in.mouseDeltaY = delta.y;
    in.wheelY = GetMouseWheelMove();
#endif
    return in;
}

void Backend::setRelativeMouse(bool relative) {
    Impl& impl = *impl_;
    if (impl.relativeMouse == relative) {
        return;
    }
    impl.relativeMouse = relative;
    if (relative) {
        DisableCursor();
    } else {
        EnableCursor();
    }
}

}  // namespace granadad::render3d
