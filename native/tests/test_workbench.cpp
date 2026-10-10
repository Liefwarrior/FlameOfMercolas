// THE WORKBENCH (2026-10-09) -- the editor over the live ward, headless.
//
// Everything a window does goes through the same Workbench methods a script
// and the serve loop call, so these cases drive it exactly as an agent would:
// commands in, answers and pictures out. The one promise that matters most is
// the scene: a saved log of the world-changing commands, replayed onto a fresh
// world, lands on the identical world hash -- the workbench is a hand on the
// game's own deterministic verbs, never a second simulation beside them.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/workbench/workbench.hpp"

namespace render = granadad::render;
namespace wb = granadad::workbench;

namespace {

[[nodiscard]] render::SessionConfig bootConfig() {
    render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.timeOfDay = 10 * 3600;
    config.timeOfDayGiven = true;
    return config;
}

/// A scratch directory under the system temp, emptied on the way in and out.
class Scratch {
public:
    explicit Scratch(const std::string& name)
        : root_(std::filesystem::temp_directory_path() / ("granadad-workbench-" + name)) {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
    }
    ~Scratch() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

[[nodiscard]] bool overlaps(const render::PanelRect& a, const render::PanelRect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

}  // namespace

TEST_CASE("the workbench's six panes and its command line tile the frame without overlapping") {
    const wb::Panes panes = wb::workbenchPanes();
    const render::PanelRect all[] = {panes.toolbar, panes.hierarchy, panes.scene, panes.game,
                                     panes.inspector, panes.console, panes.command};
    long area = 0;
    for (std::size_t i = 0; i < std::size(all); ++i) {
        INFO(i);
        CHECK_FALSE(all[i].empty());
        CHECK(all[i].x >= 0);
        CHECK(all[i].y >= 0);
        CHECK(all[i].right() <= wb::kFrameWidth);
        CHECK(all[i].bottom() <= wb::kFrameHeight);
        area += static_cast<long>(all[i].w) * all[i].h;
        for (std::size_t j = i + 1; j < std::size(all); ++j) {
            CHECK_FALSE(overlaps(all[i], all[j]));
        }
    }
    CHECK(area == static_cast<long>(wb::kFrameWidth) * wb::kFrameHeight);
    // The GAME pane is the session's own frame at its native size.
    CHECK(panes.game.w == 640);
    CHECK(panes.game.h == 360);
}

TEST_CASE("the workbench answers its commands: help, an unknown word, the lists, a selection") {
    wb::Workbench bench(bootConfig());
    CHECK(bench.run("help").find("step") != std::string::npos);
    CHECK(bench.run("teleport") == "NO COMMAND CALLED TELEPORT. TYPE HELP.\n");

    // PEOPLE: the whole ward, filtered by a name.
    CHECK(bench.hierarchyRows().size() > 600);
    (void)bench.run("filter brann");
    REQUIRE_FALSE(bench.hierarchyRows().empty());
    CHECK(bench.run("select 1").rfind("SELECTED: NAME     BRANN", 0) == 0);
    REQUIRE(bench.selection().kind == wb::Selection::Kind::Person);
    // The inspector says what the sim knows: his counter, among the rest.
    bool keeps = false;
    for (const std::string& line : bench.inspectorLines()) {
        keeps = keeps || line == "KEEPS    BRANN'S CHANDLERY";
    }
    CHECK(keeps);

    // PLACES and the BOARD.
    (void)bench.run("filter");
    CHECK(bench.run("tab places").rfind("TAB PLACES, ", 0) == 0);
    CHECK(bench.run("select place fenner's pawn").rfind("SELECTED: PLACE    FENNER'S PAWN", 0) ==
          0);
    CHECK(bench.run("tab board").rfind("TAB BOARD, ", 0) == 0);
    CHECK_FALSE(bench.hierarchyRows().empty());
    CHECK(bench.run("tab nonsense") == "TAB WANTS PEOPLE, PLACES, BOARD OR KIT.\n");
    // An id nobody has is refused, never selected past the end of a list.
    CHECK(bench.run("select place #99999") == "NOTHING TO SELECT THERE.\n");
}

TEST_CASE("the workbench drives the world through the game's own verbs: step, focus, talk, buy") {
    wb::Workbench bench(bootConfig());
    const int before = bench.session().timeOfDay();
    (void)bench.run("step 600");
    CHECK(bench.stepsRun() == 600);
    CHECK(bench.session().timeOfDay() != before);

    REQUIRE(bench.run("select person brann").rfind("SELECTED:", 0) == 0);
    const std::string focused = bench.run("focus");
    CHECK(focused.find("BESIDE BRANN") == 0);
    const std::string talked = bench.run("talk");
    INFO(talked);
    REQUIRE(bench.session().talking());
    CHECK(talked.rfind("BRANN: ", 0) == 0);
    CHECK(talked.find("SEE THE WARES") != std::string::npos);
    CHECK(talked.find("TONE: NORMAL") != std::string::npos);
    (void)bench.run("leave");
    CHECK_FALSE(bench.session().talking());

    CHECK(bench.run("coin 77") == "THE PURSE HOLDS 77C.\n");
    CHECK(bench.session().tavern().playerCoin() == 77);
    CHECK(bench.run("give lantern") == "GIVEN 1 LANTERN.\n");
    (void)bench.run("tab kit");
    CHECK(bench.hierarchyRows().size() >= 1);
}

TEST_CASE("a saved scene replays onto a fresh world to the identical hash") {
    Scratch scratch("scenes");
    std::uint64_t played = 0;
    {
        wb::Workbench bench(bootConfig());
        bench.setSceneDir(scratch.root());
        (void)bench.run("step 300");
        (void)bench.run("select person brann");
        (void)bench.run("focus");
        (void)bench.run("talk");
        (void)bench.run("leave");
        (void)bench.run("coin 40");
        (void)bench.run("give rope");
        (void)bench.run("hour 21");
        (void)bench.run("step 120");
        CHECK(bench.run("save evening").rfind("SAVED ", 0) == 0);
        played = bench.session().simHash();
    }
    CHECK(std::filesystem::exists(scratch.root() / "evening.wbs"));
    wb::Workbench fresh(bootConfig());
    fresh.setSceneDir(scratch.root());
    // A world that has done nothing is not the one that did all that...
    CHECK(fresh.session().simHash() != played);
    // ...and the replayed one is, to the bit.
    CHECK(fresh.run("load evening").rfind("LOADED ", 0) == 0);
    CHECK(fresh.session().simHash() == played);
    CHECK(fresh.session().tavern().playerCoin() == 40);
}

TEST_CASE("the workbench draws a frame every pane has something in, and crops a pane by name") {
    wb::Workbench bench(bootConfig());
    (void)bench.run("select person brann");
    render::Framebuffer frame(1, 1);
    bench.draw(frame);
    REQUIRE(frame.width() == wb::kFrameWidth);
    REQUIRE(frame.height() == wb::kFrameHeight);
    const wb::Panes panes = wb::workbenchPanes();
    const auto colours = [&frame](const render::PanelRect& rect) {
        std::vector<std::uint32_t> seen;
        for (int y = rect.y; y < rect.bottom(); y += 3) {
            for (int x = rect.x; x < rect.right(); x += 3) {
                const std::uint32_t p = frame.pixels()[frame.index(x, y)];
                if (std::find(seen.begin(), seen.end(), p) == seen.end()) {
                    seen.push_back(p);
                    if (seen.size() > 8) {
                        return seen.size();
                    }
                }
            }
        }
        return seen.size();
    };
    // A blank pane is one colour; every pane here is drawing something.
    CHECK(colours(panes.scene) > 4);
    CHECK(colours(panes.game) > 4);
    CHECK(colours(panes.hierarchy) > 2);
    CHECK(colours(panes.inspector) > 2);
    const render::Framebuffer scene = bench.drawPane("scene");
    CHECK(scene.width() == panes.scene.w);
    CHECK(scene.height() == panes.scene.h);
    CHECK(bench.drawPane("nonsense").width() == 1);
}

TEST_CASE("serving: any workflow's command file runs, answers in the outbox, and the shared view refreshes") {
    Scratch scratch("serve");
    wb::Workbench bench(bootConfig());
    std::filesystem::create_directories(scratch.root() / "inbox");
    {
        std::ofstream cmd(scratch.root() / "inbox" / "a-feature.cmd");
        cmd << "# the FEATURE workflow\nstep 60\nselect person fenner\ninspect\n";
    }
    {
        std::ofstream cmd(scratch.root() / "inbox" / "b-scorer.cmd");
        cmd << "state\n";
    }
    CHECK(wb::serveOnce(bench, scratch.root()) == 2);
    CHECK(std::filesystem::exists(scratch.root() / "outbox" / "a-feature.out"));
    CHECK(std::filesystem::exists(scratch.root() / "outbox" / "b-scorer.out"));
    CHECK(std::filesystem::exists(scratch.root() / "done" / "a-feature.cmd"));
    CHECK_FALSE(std::filesystem::exists(scratch.root() / "inbox" / "a-feature.cmd"));
    CHECK(std::filesystem::exists(scratch.root() / "frame.png"));
    CHECK(std::filesystem::exists(scratch.root() / "state.json"));
    // The second workflow sees what the first one did: the same world.
    std::ifstream scorer(scratch.root() / "outbox" / "b-scorer.out");
    const std::string state((std::istreambuf_iterator<char>(scorer)), std::istreambuf_iterator<char>());
    CHECK(state.find("\"steps\":60") != std::string::npos);
    CHECK(state.find("FENNER") != std::string::npos);
    // Nothing waiting: nothing run.
    CHECK(wb::serveOnce(bench, scratch.root()) == 0);
}

TEST_CASE("a click on a hierarchy row selects it, and on the tab row switches the list") {
    wb::Workbench bench(bootConfig());
    const wb::Panes panes = wb::workbenchPanes();
    // Row 3 of the pane's grid is the first list row (title, tabs, filter).
    const int firstRowY = panes.hierarchy.y + 4 + 3 * 14 + 2;
    (void)bench.click(panes.hierarchy.x + 20, firstRowY);
    CHECK(bench.selection().kind == wb::Selection::Kind::Person);
    // The second tab word, PLACES, eight cells along.
    (void)bench.click(panes.hierarchy.x + 4 + 8 * 10 + 5, panes.hierarchy.y + 4 + 14 + 2);
    CHECK(bench.tab() == wb::HierarchyTab::Places);
}
