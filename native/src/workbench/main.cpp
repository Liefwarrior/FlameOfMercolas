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
#include "granadad/workbench/workbench.hpp"

namespace {

namespace wb = granadad::workbench;

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

int runWindow(wb::Workbench& bench) {
    namespace r3 = granadad::render3d;
    if (r3::Backend::headlessCapable()) {
        std::printf("granadad-workbench: this build renders in memory and has no window -- "
                    "use --script or --serve\n");
        return 2;
    }
    r3::BackendConfig config;
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
        video->beginFrame(r3::Rgba8{0, 0, 0, 255});
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
