#include "internal.hpp"

// THE WINDOW: OPEN, CLOSE, BEGIN, AND WHERE THE OVERLAY LANDS.

namespace granadad::render3d {

using namespace rl;

namespace {

/// One window per process, which is also raylib's own rule.
bool g_backendOpen = false;

}  // namespace

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
    // VSYNC, HONOURED. FLAG_VSYNC_HINT is a hint: on a driver that ignores
    // the swap interval for a windowed context (the NVIDIA laptop panel
    // this ships on ran the loop at ~250 fps with it set) the frame pacing
    // is the window's alone, so the cap is enforced here as well -- the
    // panel's own refresh rate as the target, raylib's EndDrawing waiting
    // out the rest of each frame. The headless build has no panel and no
    // EndDrawing, and the shutter opens with vsync off; neither is paced.
    if (config.vsync && !kHeadless) {
        const int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(refresh > 0 ? refresh : 60);
    }
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
