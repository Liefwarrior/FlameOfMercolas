// Granadad: The Darkstreets -- the WORKBENCH, a Unity-shaped editor over the live
// ward. See include/granadad/workbench/workbench.hpp for the panes, the command
// vocabulary and why every command is a public verb of the game's own Session.
//
//   granadad-workbench                       a window: click, scroll, type
//   granadad-workbench --script=FILE         run commands, print, exit
//   granadad-workbench --serve=DIR           keep one world alive for any number
//                                            of workflows: drop DIR/inbox/*.cmd,
//                                            read DIR/outbox/, DIR/frame.png,
//                                            DIR/state.json, DIR/log.txt
//
// Floats are legal in this file: it is a window and a loop, like src/client.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene_rig.hpp"
#include "granadad/workbench/workbench.hpp"

namespace {

namespace wb = granadad::workbench;
namespace r3 = granadad::render3d;

/// The 3D scene of the bench's session, rebuilt when the session is (a `load`
/// boots a new one, and the rig borrows the old one's tiles).
class WorldView {
public:
    explicit WorldView(const wb::Workbench& bench) : bench_(&bench) {}

    r3::SceneDescription& refresh(float aspect) {
        if (generation_ != bench_->sessionGeneration()) {
            rig_ = r3::SceneRig{};
            generation_ = bench_->sessionGeneration();
        }
        rig_.refresh(bench_->session(), aspect);
        return rig_.scene;
    }

private:
    const wb::Workbench* bench_;
    r3::SceneRig rig_;
    int generation_ = 0;
};

[[nodiscard]] r3::BackendConfig worldBackendConfig() {
    r3::BackendConfig config;
    config.modelDir = r3::rigModelDir();
    config.weaponDir = r3::weaponModelDir();
    config.staticDir = r3::staticModelDir();
    return config;
}

void printUsage() {
    std::printf(
        "usage: granadad-workbench [options]\n"
        "  (no mode)            open the workbench in a window\n"
        "  --script=FILE        run FILE's commands against a fresh world, print\n"
        "                       every answer, exit (non-zero if a command was\n"
        "                       not understood)\n"
        "  --serve=DIR          keep one world alive: every DIR/inbox/*.cmd any\n"
        "                       workflow drops is run in name order; answers go\n"
        "                       to DIR/outbox/<name>.out, and DIR/frame.png,\n"
        "                       DIR/state.json and DIR/log.txt are the shared\n"
        "                       view. A `quit` command ends it\n"
        "  --time=HH            the hour the world boots at (default 8)\n"
        "  --scenes=DIR         where `save NAME` writes and `load NAME` reads\n"
        "                       (default ./workbench-scenes)\n"
        "  --help               this\n"
        "\n"
        "In the window: click a hierarchy row or a body in the scene to select\n"
        "it, the wheel scrolls the list or zooms the scene, SPACE plays and\n"
        "pauses, `.` steps once, UP and DOWN walk the list, and typing goes to\n"
        "the command line (ENTER runs it). Type HELP for the commands.\n");
}

bool startsWith(const char* arg, const char* prefix, const char** value) {
    const std::size_t n = std::strlen(prefix);
    if (std::strncmp(arg, prefix, n) != 0) {
        return false;
    }
    *value = arg + n;
    return true;
}

/// THE PICTURE'S WORLD for a picture on disk (`shot`, `pane`, serve's
/// frame.png): where this build can open a headless 3D context (Mesa
/// llvmpipe), the GAME pane's world is the real 3D frame, read back the way a
/// capture is; anywhere else the HUD is drawn over the dark ground and the
/// picture says so by being dark.
class HeadlessWorld {
public:
    /// Opens nothing until the first picture is asked for, so a script of
    /// plain commands starts as fast as it ever did and never says a word
    /// about GL.
    explicit HeadlessWorld(wb::Workbench& bench) : bench_(&bench) {
        bench.setWorldPainter([this](const granadad::render::Session&,
                                     granadad::render::Framebuffer& world) {
            return paint(world);
        });
    }

private:
    bool paint(granadad::render::Framebuffer& world) {
        if (!tried_) {
            tried_ = true;
            open();
        }
        if (video_ == nullptr) {
            return false;
        }
        r3::SceneDescription& scene = view_->refresh(
            static_cast<float>(std::max(1, video_->width())) /
            static_cast<float>(std::max(1, video_->height())));
        video_->beginFrame(scene.clearColour);
        (void)video_->drawScene(scene);
        video_->endFrame(&world);
        return true;
    }

    void open() {
        if (!r3::Backend::headlessCapable()) {
            std::printf("granadad-workbench: no headless 3D context here -- the game pane's "
                        "picture carries the HUD over the dark ground\n");
            return;
        }
        r3::BackendConfig config = worldBackendConfig();
        config.width = bench_->session().config().width;
        config.height = bench_->session().config().height;
        config.windowScale = 1;
        config.headless = true;
        config.vsync = false;
        config.resizable = false;
        video_ = r3::Backend::open(config);
        if (video_ != nullptr) {
            view_ = std::make_unique<WorldView>(*bench_);
        }
    }

    wb::Workbench* bench_;
    bool tried_ = false;
    std::unique_ptr<r3::Backend> video_;
    std::unique_ptr<WorldView> view_;
};

int runWindow(wb::Workbench& bench) {
    r3::BackendConfig config = worldBackendConfig();
    config.width = wb::kFrameWidth;
    config.height = wb::kFrameHeight;
    config.windowScale = 1;
    config.resizable = true;
    config.vsync = true;
    config.title = "Granadad Workbench";
    std::unique_ptr<r3::Backend> video = r3::Backend::open(config);
    if (video == nullptr) {
        std::printf("granadad-workbench: the window would not open\n");
        return 1;
    }
    // HID usage ids -- SDL's scancode values, the backend's own vocabulary.
    constexpr std::uint16_t kEnter = 40;
    constexpr std::uint16_t kEscape = 41;
    constexpr std::uint16_t kBackspace = 42;
    constexpr std::uint16_t kDown = 81;
    constexpr std::uint16_t kUp = 82;
    constexpr std::uint16_t kKeypadEnter = 88;
    // THE GAME PANE IS THE 3D WORLD: the backend draws it into the pane's
    // rectangle and the bench leaves the pane transparent for it.
    bench.setWorldInWindow(true);
    WorldView worldView(bench);
    const wb::Panes panes = wb::workbenchPanes();
    granadad::render::Framebuffer frame(wb::kFrameWidth, wb::kFrameHeight);
    while (!bench.quitRequested()) {
        const r3::InputFrame in = video->poll();
        if (in.closeRequested) {
            break;
        }
        const r3::OverlayPlacement place = video->overlayPlacement(wb::kFrameWidth, wb::kFrameHeight);
        for (const r3::KeyEdge& key : in.keys) {
            if (!key.down) {
                continue;
            }
            if (key.hid == kEnter || key.hid == kKeypadEnter) {
                (void)bench.submit();
            } else if (key.hid == kBackspace) {
                bench.backspace();
            } else if (key.hid == kEscape) {
                while (!bench.commandLine().empty()) {
                    bench.backspace();
                }
            } else if (key.hid == kUp && bench.commandLine().empty()) {
                (void)bench.run("prev");
            } else if (key.hid == kDown && bench.commandLine().empty()) {
                (void)bench.run("next");
            }
        }
        for (const std::uint32_t c : in.chars) {
            // An EMPTY command line reads the two transport keys; anything
            // else typed starts a command.
            if (bench.commandLine().empty() && c == ' ') {
                (void)bench.run(bench.playing() ? "pause" : "play");
            } else if (bench.commandLine().empty() && c == '.') {
                (void)bench.run("step");
            } else if (c < 128) {
                bench.typeChar(static_cast<char>(c));
            }
        }
        for (const r3::MouseButtonEdge& button : in.buttons) {
            if (button.down && button.button == 1) {
                (void)bench.click(place.toFrameX(button.x), place.toFrameY(button.y));
            }
        }
        if (in.wheelY != 0.0F) {
            bench.wheel(place.toFrameX(in.mouseX), place.toFrameY(in.mouseY),
                        in.wheelY > 0.0F ? 1 : -1);
        }
        bench.frame();
        bench.draw(frame);
        video->setSceneDestination(place.offsetX + panes.game.x * place.scale,
                                   place.offsetY + panes.game.y * place.scale,
                                   panes.game.w * place.scale, panes.game.h * place.scale);
        r3::SceneDescription& scene = worldView.refresh(
            static_cast<float>(std::max(1, video->width())) /
            static_cast<float>(std::max(1, video->height())));
        video->beginFrame(scene.clearColour);
        (void)video->drawScene(scene);
        video->drawOverlay(frame);
        video->endFrame();
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string script;
    std::string serve;
    std::string scenes;
    int hour = 8;
    for (int i = 1; i < argc; ++i) {
        const char* value = nullptr;
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage();
            return 0;
        }
        if (startsWith(argv[i], "--script=", &value)) {
            script = value;
        } else if (startsWith(argv[i], "--serve=", &value)) {
            serve = value;
        } else if (startsWith(argv[i], "--scenes=", &value)) {
            scenes = value;
        } else if (startsWith(argv[i], "--time=", &value)) {
            hour = std::atoi(value);
            if (hour < 0 || hour > 23) {
                std::printf("granadad-workbench: --time wants 0 to 23\n");
                return 2;
            }
        } else {
            std::printf("granadad-workbench: unknown option %s\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    granadad::render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.timeOfDay = hour * 3600;
    config.timeOfDayGiven = true;
    wb::Workbench bench(config);
    if (!scenes.empty()) {
        bench.setSceneDir(scenes);
    }

    // A picture on disk (a `shot`, a `pane`, serve's frame.png) carries the 3D
    // world; the window draws it itself.
    std::unique_ptr<HeadlessWorld> headlessWorld;
    if (!script.empty() || !serve.empty()) {
        headlessWorld = std::make_unique<HeadlessWorld>(bench);
    }

    if (!script.empty()) {
        const std::string answer = bench.runFile(script);
        std::fputs(answer.c_str(), stdout);
        return answer.find("NO COMMAND CALLED") != std::string::npos ||
                       answer.find("NO SUCH FILE") != std::string::npos
                   ? 1
                   : 0;
    }
    if (!serve.empty()) {
        std::printf("granadad-workbench: serving %s -- drop *.cmd files into %s/inbox\n",
                    serve.c_str(), serve.c_str());
        std::fflush(stdout);
        // One picture of the world before anybody has asked anything, so a
        // workflow that arrives first still has something to look at.
        {
            std::filesystem::create_directories(std::filesystem::path(serve) / "inbox");
            std::filesystem::create_directories(std::filesystem::path(serve) / "outbox");
            granadad::render::Framebuffer frame(wb::kFrameWidth, wb::kFrameHeight);
            bench.draw(frame);
            (void)granadad::render::writePng(frame, std::filesystem::path(serve) / "frame.png");
        }
        while (!bench.quitRequested()) {
            if (wb::serveOnce(bench, serve) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        return 0;
    }
    return runWindow(bench);
}
