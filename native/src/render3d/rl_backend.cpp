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

#include "granadad/render3d/backend.hpp"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

[[nodiscard]] Color colourOf(const Rgba8& c) noexcept { return Color{c.r, c.g, c.b, c.a}; }

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

    void release() {
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
        const auto mesh = impl.meshes.find(instance.meshId);
        if (mesh == impl.meshes.end()) {
            continue;
        }
        impl.material.maps[MATERIAL_MAP_DIFFUSE].color = colourOf(instance.tint);
        impl.material.maps[MATERIAL_MAP_DIFFUSE].texture = impl.defaultTexture;
        if (instance.textureId != 0) {
            const auto texture = impl.textures.find(instance.textureId);
            if (texture != impl.textures.end()) {
                impl.material.maps[MATERIAL_MAP_DIFFUSE].texture = texture->second.texture;
            }
        }
        // Scale, then yaw, then place. Yaw is clockwise-from-above in the
        // description and raylib's Y rotation is counter-clockwise, hence
        // the sign -- see scene.hpp on the frame.
        const Matrix transform = MatrixMultiply(
            MatrixMultiply(MatrixScale(instance.scale, instance.scale, instance.scale),
                           MatrixRotateY(-instance.yaw)),
            MatrixTranslate(instance.position.x, instance.position.y, instance.position.z));
        DrawMesh(impl.tinted(mesh->second.mesh, instance.tint), impl.material, transform);
        ++stats.instancesDrawn;
        stats.trianglesDrawn += static_cast<std::size_t>(mesh->second.mesh.triangleCount);
    }
    EndMode3D();
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
