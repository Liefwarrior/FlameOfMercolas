#pragma once

// THE WORKBENCH (2026-10-09). A Unity-shaped editor over the LIVE ward.
//
// WHAT IT IS. One frame, six panes, over one running render::Session -- the
// same Session the game drives, through the same public verbs:
//
//   +--------------------------------------------------------------------+
//   | TOOLBAR  day, clock, PLAYING/PAUSED, steps, world hash, selection  |
//   +-------------+------------------------------+-----------------------+
//   | HIERARCHY   | SCENE  the ward from above,  | INSPECTOR  everything |
//   | people,     | every body a dot, the player | the selection is, in  |
//   | places,     | a wedge, the selection ringed| the sim's own words   |
//   | the board,  +------------------------------+-----------------------+
//   | the Kit     | GAME  what the player sees   | CONSOLE  what the ward|
//   |             | (the software raycaster)     | said, what you asked  |
//   +-------------+------------------------------+-----------------------+
//   | > the command line                                                 |
//   +--------------------------------------------------------------------+
//
// TWO HANDS ON ONE WORLD. A person drives it in a window (granadad-workbench
// with no flags): the mouse picks rows and bodies, SPACE plays and pauses,
// `.` steps, and the command line takes the same words a script does. An
// AGENT drives it headless: `--script=FILE` runs commands and exits;
// `--serve=DIR` keeps one world alive and executes every DIR/inbox/*.cmd any
// number of workflows drop there, writing each one's output to DIR/outbox/
// and the shared picture of the world to DIR/frame.png and DIR/state.json --
// so several workflows can work in, and all SEE, the same ward.
//
// WHAT IT IS NOT. A second simulation, or a back door into this one: every
// command is a public Session/Tavern verb the game itself calls, so the
// determinism the twin-run gate protects is the workbench's too -- a SCENE
// (`save NAME`) is the log of the world-changing commands since boot, and
// `load` replays it onto a fresh world to the identical hash. Nothing here
// writes content/: scenes go to their own directory as new files.
//
// Floats are legal here: this is render-side, like everything under
// src/render. Nothing in src/sim includes this file.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/render/session.hpp"

namespace granadad::workbench {

/// The frame the workbench composes, in pixels. Fixed so a pane is always the
/// same picture in a window, a script's PNG and a test: the GAME pane is the
/// session's own 640x360, at its native size.
inline constexpr int kFrameWidth = 1360;
inline constexpr int kFrameHeight = 768;

/// What the HIERARCHY lists.
enum class HierarchyTab : std::uint8_t {
    People = 0,
    Places = 1,
    Board = 2,
    Kit = 3,
};
[[nodiscard]] std::string_view hierarchyTabName(HierarchyTab tab) noexcept;

/// What is selected. `id` is the kind's own key: a WardActor id, a mapPlaces()
/// index, a radiant objective id, an item registry index.
struct Selection {
    enum class Kind : std::uint8_t { None, Person, Place, Errand, Item };
    Kind kind = Kind::None;
    std::int32_t id = -1;
};

/// Where each pane sits in the frame. Pure layout, for the mouse and the
/// per-pane PNGs.
struct Panes {
    render::PanelRect toolbar;
    render::PanelRect hierarchy;
    render::PanelRect scene;
    render::PanelRect game;
    render::PanelRect inspector;
    render::PanelRect console;
    render::PanelRect command;
};
[[nodiscard]] Panes workbenchPanes() noexcept;

/// One row of the hierarchy as it is listed: what it says and what picking it
/// selects.
struct HierarchyRow {
    std::string label;
    std::string note;
    Selection select;
};

class Workbench {
public:
    explicit Workbench(render::SessionConfig config);
    ~Workbench();
    Workbench(const Workbench&) = delete;
    Workbench& operator=(const Workbench&) = delete;

    /// Executes one command line and answers what it printed. The answer is
    /// also written to the console pane. Unknown words answer how to ask for
    /// help rather than failing silently. See `help` for the vocabulary.
    std::string run(std::string_view line);
    /// Runs every line of a file (blank lines and `#` comments skipped),
    /// answering everything printed, in order.
    std::string runFile(const std::filesystem::path& file);

    /// One frame of PLAY: the session stepped `speed()` times while playing,
    /// not at all while paused. The window calls this once per frame.
    void frame();

    /// Composes the whole workbench into `target` (resized to kFrame*).
    void draw(render::Framebuffer& target) const;
    /// The same frame, cropped to one pane by name (toolbar, hierarchy, scene,
    /// game, inspector, console). Empty framebuffer for an unknown name.
    [[nodiscard]] render::Framebuffer drawPane(std::string_view pane) const;

    // --- the mouse (window coordinates already mapped into the frame) -------
    /// A press at frame pixel (x, y): a hierarchy row selects, a tab switches,
    /// a body or a place in the scene selects. Answers what it did, or empty.
    std::string click(int x, int y);
    /// The wheel over the hierarchy scrolls it; over the scene, zooms it.
    void wheel(int x, int y, int notches);

    // --- the command line the window types into -------------------------------
    [[nodiscard]] const std::string& commandLine() const noexcept { return commandLine_; }
    void typeChar(char c);
    void backspace();
    /// Runs the command line and clears it.
    std::string submit();

    // --- state, for the tests and the serve loop ------------------------------
    [[nodiscard]] render::Session& session() noexcept { return *session_; }
    [[nodiscard]] const render::Session& session() const noexcept { return *session_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] int speed() const noexcept { return speed_; }
    [[nodiscard]] std::int64_t stepsRun() const noexcept { return stepsRun_; }
    [[nodiscard]] const Selection& selection() const noexcept { return selection_; }
    [[nodiscard]] HierarchyTab tab() const noexcept { return tab_; }
    [[nodiscard]] bool quitRequested() const noexcept { return quit_; }
    /// The hierarchy's rows for the current tab and filter, in order.
    [[nodiscard]] std::vector<HierarchyRow> hierarchyRows() const;
    /// The inspector's lines for the current selection (or the player).
    [[nodiscard]] std::vector<std::string> inspectorLines() const;
    /// The console, oldest first.
    [[nodiscard]] const std::vector<std::string>& consoleLines() const noexcept {
        return console_;
    }
    /// The world-changing commands since boot -- what `save` writes.
    [[nodiscard]] const std::vector<std::string>& sceneLog() const noexcept { return sceneLog_; }
    /// A one-object JSON summary: clock, steps, hash, selection, player.
    [[nodiscard]] std::string stateJson() const;
    /// Where `save NAME` writes and `load NAME` looks: workbench-scenes/ under
    /// the working directory unless set.
    void setSceneDir(std::filesystem::path dir) { sceneDir_ = std::move(dir); }

private:
    void say(std::string line);
    /// Rebuilds the session from the boot config, the way `load` starts.
    void reboot();
    std::string runOne(std::string_view line, bool logged);
    void stepWorld(int steps);
    bool selectByName(Selection::Kind kind, std::string_view text);
    void clampCursor();

    void drawToolbar(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawHierarchy(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawScene(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawGame(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawInspector(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawConsole(render::Framebuffer& target, const render::PanelRect& pane) const;
    void drawCommand(render::Framebuffer& target, const render::PanelRect& pane) const;

    render::SessionConfig config_;
    std::unique_ptr<render::Session> session_;
    bool playing_ = false;
    int speed_ = 1;
    std::int64_t stepsRun_ = 0;
    Selection selection_;
    HierarchyTab tab_ = HierarchyTab::People;
    std::string filter_;
    int cursor_ = 0;
    int scroll_ = 0;
    int zoom_ = 0;
    std::vector<std::string> console_;
    std::vector<std::string> sceneLog_;
    std::string commandLine_;
    std::filesystem::path sceneDir_ = "workbench-scenes";
    bool quit_ = false;
};

/// THE SERVE LOOP, one pass: executes every DIR/inbox/*.cmd in name order,
/// writes each one's answer to DIR/outbox/<name>.out, moves it to DIR/done/,
/// and refreshes DIR/frame.png, DIR/state.json and DIR/log.txt. Answers how
/// many command files it ran. The executable calls this on a poll.
int serveOnce(Workbench& bench, const std::filesystem::path& dir);

}  // namespace granadad::workbench
