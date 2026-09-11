#pragma once

// THE RAYLIB ADAPTER'S CONTRACT -- and the only raylib in the project.
//
// Exactly ONE translation unit includes raylib.h: src/render3d/rl_backend.cpp,
// which implements this header. Nothing else -- not the client, not the tests,
// not granadad-render3d-core -- ever sees a raylib type; they see this. That
// rule is what keeps raylib's global names (Rectangle, CloseWindow, Camera,
// PI...) from colliding with Windows headers and our own, and it is what
// keeps the renderer replaceable.
//
// TWO BUILDS OF THE SAME ADAPTER, chosen at configure time by raylib's own
// PLATFORM / OPENGL_VERSION options (native/cmake/Dependencies.cmake):
//
//   PLATFORM=Memory  OPENGL_VERSION=Software   the docker host check. No
//       window, no GPU, no display server: raylib's software rasterizer
//       (rlsw) draws into memory and readback() hands the pixels over. Pure
//       CPU arithmetic, so two draws of one description are byte-identical
//       in-process and the frame tests can say so.
//
//   PLATFORM=Desktop  OPENGL_VERSION=3.3       the shipped .exe. A GLFW
//       window with an OpenGL 3.3 context; readback() still works (it is how
//       --screenshot captures on the host) but a GPU frame is NEVER hashed --
//       it is proved by stats and by similarity against the rlsw frame.
//
// A FRAME, in order:
//
//     beginFrame(sky)            clear
//     drawScene(description)     the 3D pass (BeginMode3D .. EndMode3D)
//     drawOverlay(framebuffer)   the terminal HUD: a render::Framebuffer
//                                uploaded as a straight-alpha RGBA texture
//                                and drawn point-filtered at the largest
//                                integer scale that fits, centred --
//                                SDL's INTEGER_SCALE presentation, kept
//     endFrame(&capture)         flush, read back if asked, swap, poll
//
// hud.cpp keeps drawing into its Framebuffer exactly as before; over an
// opaque pixel Framebuffer::blend is bit-identical to what it always was,
// which is what keeps the eighteen pixel-exact HUD/page tests green while
// the world underneath moves to 3D. See framebuffer.hpp's clearTransparent.
//
// INPUT comes out of poll() in a NEUTRAL vocabulary -- USB HID usage ids
// (page 7) for keys, which are exactly SDL's scancode numbers, so the client
// can keep its scancode-keyed binding table -- plus mouse edges, deltas and
// wheel in window pixels. The Memory build reports no input at all.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "granadad/render3d/scene.hpp"

namespace granadad::render {
class Framebuffer;
}

namespace granadad::render3d {

enum class VideoKind : std::uint8_t {
    /// rlsw in memory: headless, byte-identical, no window.
    Software,
    /// A real window and a GPU context.
    Gpu,
};

struct BackendConfig {
    /// The frame the HUD is drawn at. On the Desktop build the window opens
    /// at width*windowScale x height*windowScale; on the Memory build the
    /// frame IS the window and windowScale is ignored.
    int width = 640;
    int height = 360;
    int windowScale = 2;
    bool resizable = true;
    bool vsync = true;
    /// Do not steal focus on show -- the owner's standing ask for the
    /// launcher window; the OS still grants click-to-focus.
    bool unfocused = true;
    std::string title = "Granadad: The Darkstreets";
    /// A LANE. Where the skinned actor rigs live: the directory holding
    /// <actorRigFile(rig)> (content/art/lot-3d/characters on the client).
    /// Empty -- every test, the docker gate -- means no file is ever looked
    /// for and every body draws as its placeholder mesh. A named directory
    /// with a file missing falls back the same way, silently, per rig.
    std::string modelDir;
    /// V LANE. Where the static weapon exports live: the directory holding
    /// <viewmodelWeaponFile(kind)> (content/art/lot-3d/static on the
    /// client). Empty means no weapon file is ever looked for; the
    /// placeholder weapon part draws instead.
    std::string weaponDir;
};

/// A key going down or up this frame. `hid` is the USB HID usage id (SDL's
/// scancode value); `repeat` marks an OS key-repeat rather than a fresh press.
struct KeyEdge {
    std::uint16_t hid = 0;
    bool down = false;
    bool repeat = false;
};

/// SDL's button numbering: 1 left, 2 middle, 3 right, 4 X1, 5 X2. Position
/// in WINDOW pixels; overlayPlacement() maps it into framebuffer pixels.
struct MouseButtonEdge {
    std::uint8_t button = 0;
    bool down = false;
    float x = 0.0F;
    float y = 0.0F;
};

struct InputFrame {
    std::vector<KeyEdge> keys;
    std::vector<MouseButtonEdge> buttons;
    /// Unicode code points typed this frame, in order.
    std::vector<std::uint32_t> chars;
    /// Pointer position and motion since the last poll, window pixels.
    float mouseX = 0.0F;
    float mouseY = 0.0F;
    float mouseDeltaX = 0.0F;
    float mouseDeltaY = 0.0F;
    /// Wheel notches, positive away from the user.
    float wheelY = 0.0F;
    /// The close button, or the platform asking to quit.
    bool closeRequested = false;
    bool focused = true;
    int windowWidth = 0;
    int windowHeight = 0;
};

/// Where the overlay lands on the window: the integer scale and the
/// letterbox offsets. The same numbers turn a window-pixel pointer into a
/// framebuffer pixel.
struct OverlayPlacement {
    int scale = 1;
    int offsetX = 0;
    int offsetY = 0;

    [[nodiscard]] int toFrameX(float windowX) const noexcept {
        return (static_cast<int>(windowX) - offsetX) / (scale < 1 ? 1 : scale);
    }
    [[nodiscard]] int toFrameY(float windowY) const noexcept {
        return (static_cast<int>(windowY) - offsetY) / (scale < 1 ? 1 : scale);
    }
};

/// What a scene draw did -- the GPU path's evidence, since its pixels are
/// never hashed.
struct SceneStats {
    std::size_t instancesDrawn = 0;
    std::size_t trianglesDrawn = 0;
    std::size_t meshesUploaded = 0;
    std::size_t texturesUploaded = 0;
    /// A LANE. Bodies drawn this pass (each is one draw of a rig model or of
    /// its placeholder), how many of them were skinned through a loaded glb
    /// this pass, and how many rig files are loaded in the process (0 on
    /// every build without content/art/lot-3d/).
    std::size_t actorsDrawn = 0;
    std::size_t actorsSkinned = 0;
    std::size_t rigModelsLoaded = 0;
    /// V LANE. Placeholder parts drawn in the viewmodel pass (0 when the
    /// pass drew a loaded rig instead, or the description had no hands);
    /// whether that pass drew the skinned arms glb; and whether a static
    /// weapon file was hung on it.
    std::size_t viewmodelPartsDrawn = 0;
    bool viewmodelSkinned = false;
    bool viewmodelWeaponLoaded = false;
};

class Backend {
public:
    /// True when this build renders through rlsw in memory -- i.e. open()
    /// needs no display and readback() is byte-reproducible. The frame tests
    /// gate on this: under a GPU build they skip rather than open a window
    /// inside the test suite.
    [[nodiscard]] static bool headlessCapable() noexcept;

    /// Opens the window (or the memory framebuffer). Null on failure, with
    /// the reason on stdout. One at a time per process.
    [[nodiscard]] static std::unique_ptr<Backend> open(const BackendConfig& config);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] VideoKind kind() const noexcept;
    /// Current window size (frame size on the Memory build).
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    void beginFrame(const Rgba8& clear);
    /// The 3D pass: scene.instances through the mesh cache, then
    /// scene.actors -- each through its rig's glb model when
    /// BackendConfig::modelDir holds one (animated by clip index when
    /// `skinned`, rest pose otherwise), else through the placeholder mesh
    /// the description carries under instance.meshId. Then, when
    /// scene.viewmodel.visible, A SECOND 3D PASS for the hands: a camera at
    /// the origin looking down -Z (the parts are in view space), its
    /// projection's depth squeezed into the front of the range so the arms
    /// win the depth test against every wall and still occlude each other
    /// -- the same picture on rlsw and GL 3.3, no depth clear, no
    /// framebuffer object (rlsw has none).
    SceneStats drawScene(const SceneDescription& scene);
    /// Draws `overlay` over the frame. Its pixels are render::Framebuffer's
    /// 0xAABBGGRR (R,G,B,A in memory), straight alpha.
    void drawOverlay(const render::Framebuffer& overlay);
    /// Flushes and presents. When `capture` is given it receives the finished
    /// frame at window size, top row first, BEFORE the swap -- the only
    /// moment a double-buffered GPU frame is defined.
    void endFrame(render::Framebuffer* capture = nullptr);

    /// Where a framebuffer of this size would be drawn by drawOverlay.
    [[nodiscard]] OverlayPlacement overlayPlacement(int frameWidth, int frameHeight) const noexcept;

    /// Input since the last poll. Call once per frame, after endFrame().
    [[nodiscard]] InputFrame poll();
    /// Relative (mouselook) mode: the pointer is hidden and locked and
    /// mouseDelta keeps flowing. Off = an ordinary visible pointer.
    void setRelativeMouse(bool relative);

    struct Impl;

private:
    explicit Backend(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace granadad::render3d
