#include "internal.hpp"

#include <cstdarg>
#include <cstdlib>

// THE WINDOW: OPEN, CLOSE, BEGIN, AND WHERE THE OVERLAY LANDS.

namespace granadad::render3d {

using namespace rl;

namespace {

/// One window per process, which is also raylib's own rule.
bool g_backendOpen = false;

/// The GL_RENDERER line raylib logs at INFO while it opens the context
/// ("    > Renderer: llvmpipe (LLVM 21.1.8, 256 bits)"). raylib exposes no
/// getter for it and the adapter includes no GL header, so the init log is
/// read for that one line; everything at WARNING and above still prints.
std::string g_rendererSeen;

void initLog(int level, const char* text, va_list args) {
    char line[512];
    std::vsnprintf(line, sizeof line, text, args);
    if (level >= LOG_WARNING) {
        std::fprintf(stderr, "raylib: %s\n", line);
        return;
    }
    const char* marker = std::strstr(line, "Renderer:");
    if (marker != nullptr) {
        marker += std::strlen("Renderer:");
        while (*marker == ' ') {
            ++marker;
        }
        g_rendererSeen = marker;
    }
}

}  // namespace

bool Backend::headlessCapable() noexcept { return kHeadlessCapable; }

Backend::Backend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<Backend> Backend::open(const BackendConfig& config) {
    if (g_backendOpen) {
        std::printf("granadad: render3d: a backend is already open in this process\n");
        return nullptr;
    }
    if (config.headless && !kHeadlessCapable) {
        std::printf("granadad: render3d: this build has no headless GL context (it is not on "
                    "raylib's SDL platform)\n");
        return nullptr;
    }
#if defined(PLATFORM_DESKTOP_SDL)
    if (config.headless) {
        // No display server, no GPU: SDL's offscreen video driver gives
        // raylib a desktop GL 3.3 core context through EGL, which Mesa serves
        // from llvmpipe. A caller that set SDL_VIDEODRIVER itself keeps its
        // choice (a CI box may point it at a virtual display).
        setenv("SDL_VIDEODRIVER", "offscreen", 0);
        // And Mesa's CPU renderer, never a machine's own GPU: the frame
        // tests and the before/after captures must come out of the same
        // rasterizer on every box. (EGL_PLATFORM=surfaceless: a box with
        // DISPLAY or WAYLAND_DISPLAY set -- WSLg, a desktop -- must not
        // route the context to its compositor.)
        setenv("EGL_PLATFORM", "surfaceless", 0);
        setenv("GALLIUM_DRIVER", "llvmpipe", 0);
    }
#endif
    // raylib narrates every init step at LOG_INFO; the test log does not
    // need thirty lines of it per case. initLog keeps the renderer's name
    // and prints warnings and up; the level goes back to WARNING after init.
    g_rendererSeen.clear();
    SetTraceLogCallback(initLog);
    SetTraceLogLevel(LOG_INFO);

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

    const int scale = config.headless ? 1 : std::max(1, config.windowScale);
    const int width = std::max(1, config.width) * scale;
    const int height = std::max(1, config.height) * scale;
    InitWindow(width, height, config.title.c_str());
    SetTraceLogLevel(LOG_WARNING);
    if (!IsWindowReady()) {
        std::printf("granadad: render3d: raylib could not open a %dx%d window\n", width, height);
        return nullptr;
    }
    // The game owns ESC (it backs out of pages and opens the pause menu);
    // raylib must not close the window on it.
    SetExitKey(KEY_NULL);
    // VSYNC, HONOURED. FLAG_VSYNC_HINT is a hint: on a driver that ignores
    // the swap interval for a windowed context (the NVIDIA laptop panel
    // this ships on ran the loop at ~250 fps with it set) the frame pacing
    // is the window's alone, so the cap is enforced here as well -- the
    // panel's own refresh rate as the target, raylib's EndDrawing waiting
    // out the rest of each frame. A headless context has no panel and the
    // shutter opens with vsync off; neither is paced.
    if (config.vsync && !config.headless) {
        const int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(refresh > 0 ? refresh : 60);
    }
    // A district is a few hundred tiles across; the default far plane of
    // 4000 wastes depth precision, the default near of 0.05 is fine but
    // stated. The viewmodel lane draws its second pass with its own planes.
    rlSetClipPlanes(0.1, 512.0);

    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->headless = config.headless;
    impl->renderer = g_rendererSeen;
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

const std::string& Backend::rendererName() const noexcept { return impl_->renderer; }

VideoKind Backend::kind() const noexcept {
    return impl_->headless ? VideoKind::Headless : VideoKind::Gpu;
}

int Backend::width() const noexcept { return GetScreenWidth(); }

int Backend::height() const noexcept { return GetScreenHeight(); }

void Backend::beginFrame(const Rgba8& clear) {
    Impl& impl = *impl_;
    BeginDrawing();
    // The scene is drawn offscreen and put on the window by resolveScene,
    // before the HUD (offscreen.cpp).
    impl.shadingNow = SceneShading{};
    if (GetRenderWidth() > 0 && GetRenderHeight() > 0) {
        impl.ensureTarget(GetRenderWidth(), GetRenderHeight());
        BeginTextureMode(impl.target);
        impl.sceneOpen = true;
    }
    // (A minimised window has no size to draw at: the frame falls through to
    // the window itself and nothing is lost -- nobody is looking.)
    ClearBackground(colourOf(clear));
}

void Backend::setSceneDestination(int x, int y, int width, int height) {
    impl_->destX = x;
    impl_->destY = y;
    impl_->destW = width;
    impl_->destH = height;
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

}  // namespace granadad::render3d
