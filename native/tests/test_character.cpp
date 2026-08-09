// THE CHARACTER SHEET: the five Legend tracks, the skills this build actually
// levels, and what the ward and the purse currently say.
//
// THE GAP THIS CLOSES. legend.hpp was built to answer "who am I in this city
// yet" on five tracks at once, and it has been derived fresh every frame since
// -- see its own header. Before this file, the only place any of it reached
// the screen was one HUD row (the track the player happens to be highest on)
// and Session::legendLine(), a summary sentence with exactly two callers and
// both of them tests. The other four tracks, and every rung of all five, were
// computed and thrown away every single frame with nowhere on screen to read
// them.
//
// WHAT THIS IS NOT. There is no item and no equipment-slot model in this
// build -- session.hpp's own quick-bar comment says so (#77's own
// VERIFICATION GAP) -- so this is not a paper doll and it draws no armour
// rating. It is the state the simulation actually has, laid out where a
// player can look themselves up: five derived standings and the four skills a
// verb in this build actually levels.
//
// WHAT IS AND IS NOT TESTED HERE. Session owns toggleCharacter/characterRows,
// and dialogueView()'s characterOpen_ branch, and none of it touches SDL, so
// all of it is driven directly -- the same shape test_pause.cpp and
// test_casebook.cpp already use for the pages either side of this one. The
// keyboard-to-Session wiring in main.cpp's route_menu_key (C opens it, the
// arrows walk it) is not covered here for the same reason it never is on the
// other pages: that needs a real SDL harness.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/menu_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/legend.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SessionConfig fresh() {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.openingPage = true;
    return config;
}

/// A session with the opening casebook page already put down, nothing else
/// open -- the state every case below wants to start from.
[[nodiscard]] render::Session standing() {
    render::Session session(fresh());
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);  // closes the opening page, same as test_firstrun.cpp
    REQUIRE_FALSE(session.casebookOpen());
    REQUIRE_FALSE(session.characterOpen());
    return session;
}

}  // namespace

TEST_CASE("C opens the character sheet, and C closes it") {
    render::Session session = standing();

    session.toggleCharacter();
    CHECK(session.characterOpen());

    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    CHECK(view.speaker == "CHARACTER");
    CHECK_FALSE(view.epithet.empty());
    CHECK_FALSE(view.line.empty());
    CHECK_FALSE(view.topics.empty());

    session.toggleCharacter();
    CHECK_FALSE(session.characterOpen());
    CHECK_FALSE(session.dialogueView().open);
}

TEST_CASE("a man who arrived this morning reads NOBODY IN PARTICULAR, not a blank epithet") {
    // legendLine() is empty at rung zero across the board -- see
    // test_casebook.cpp's "the ward has no opinion of a man who arrived this
    // morning" -- and the HUD's own rule is that absence costs nothing there.
    // A SHEET THE PLAYER OPENED ON PURPOSE is the opposite case: showing
    // nothing where the identity line goes would read as a rendering defect,
    // not as "you have not done anything yet". kReputationUnremarkable is the
    // exact phrase the rest of this build already uses for that thought.
    render::Session session = standing();
    REQUIRE(session.legend().totalRungs() == 0);
    REQUIRE(session.legendLine().empty());

    session.toggleCharacter();
    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.epithet == std::string(kReputationUnremarkable));
}

TEST_CASE("the character sheet lists all five Legend tracks and the four skills this build levels") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    const std::vector<std::string> rows = session.characterRows();
    // Five tracks, four wired skills, and REPUTATION/COIN/HEAT -- twelve rows,
    // fixed, because the simulation has exactly this much to say about a
    // player and no item system to pad it with. See toggleCharacter's own
    // header on why the fifth track's neighbours are not sixteen more skill
    // rows: the raws carry them, nothing in this build levels them yet, and a
    // wall of LV 0 would claim the game is watching a skill it is not.
    REQUIRE(rows.size() == kLegendTracks + 4 + 3);

    // THE FIVE TRACKS, IN legend.hpp's OWN ORDER -- Wire, Roofs, Flame, Trade,
    // Law -- each a short name and a rung title, "THE " dropped off the front
    // of the name (legendTrackName() still returns the full form everywhere
    // else; this is a display choice made once, here).
    CHECK(rows[0].rfind("WIRE", 0) == 0);
    CHECK(rows[1].rfind("ROOFS", 0) == 0);
    CHECK(rows[2].rfind("FLAME", 0) == 0);
    CHECK(rows[3].rfind("TRADE", 0) == 0);
    CHECK(rows[4].rfind("LAW", 0) == 0);
    // A fresh arrival is NOBODY on every track, and the row says so -- the
    // same title legend.hpp's own table gives rung zero.
    for (std::size_t i = 0; i < kLegendTracks; ++i) {
        CHECK(rows[i].find("NOBODY") != std::string::npos);
    }

    // THE FOUR SKILLS A VERB IN THIS BUILD ACTUALLY LEVELS, each at LV 0 for a
    // fresh arrival -- streetlevel, not raws-vocabulary. See sim::kRoofSkill,
    // kThieverySkill, kHaggleSkill (social.hpp) and kCraftingSkill
    // (spellforge.hpp): the only four call sites in this build that ever call
    // SkillTrack::use.
    CHECK(rows[5].rfind("SKYRUNNING", 0) == 0);
    CHECK(rows[6].rfind("CRACKSMANSHIP", 0) == 0);
    CHECK(rows[7].rfind("STREETWISE", 0) == 0);
    CHECK(rows[8].rfind("LINKCRAFT", 0) == 0);
    for (std::size_t i = 5; i < 9; ++i) {
        CHECK(rows[i].find("LV 0") != std::string::npos);
    }

    // AND WHAT THE WARD AND THE PURSE SAY, always present -- see
    // characterRows' own comment on why a sheet opened on purpose prints a
    // zero rather than dropping the row the way the ambient HUD would.
    CHECK(rows[9].rfind("REPUTATION", 0) == 0);
    CHECK(rows[9].find("NOBODY IN PARTICULAR") != std::string::npos);
    CHECK(rows[10].rfind("COIN", 0) == 0);
    CHECK(rows[11].rfind("HEAT", 0) == 0);
    CHECK(rows[11].find("HEAT  0") != std::string::npos);
}

TEST_CASE("twelve rows is two pages, and the character sheet turns like every other list here") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    REQUIRE(session.characterRows().size() == 12);

    render::DialogueViewState view = session.dialogueView();
    CHECK(view.page == 0);
    CHECK(view.cursor == 0);

    // Walking past the ninth row turns the page, the identical contract
    // moveTopicCursor already gives the casebook and the keys page.
    for (int i = 0; i < 9; ++i) {
        session.moveTopicCursor(1);
    }
    view = session.dialogueView();
    CHECK(view.cursor == 9);
    CHECK(view.page == 1);
    CHECK(render::topicPageOf(view.cursor) == view.page);

    // 0 -- the MORE key -- turns the page directly, same as F1 and the
    // casebook.
    session.nextTopicPage();
    view = session.dialogueView();
    CHECK(view.page == 0);

    // The cursor wraps rather than stopping dead at either end.
    session.moveTopicCursor(-1);
    view = session.dialogueView();
    CHECK(view.cursor == 11);
}

TEST_CASE("a printed number moves the cursor on the character sheet and does nothing else") {
    // A CHARACTER SHEET ROW IS SOMETHING TO READ, NOT A CHOICE -- the same
    // no-op the keys page gives a number press, and for the identical reason:
    // there is nothing behind row six to choose.
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    session.chooseVisibleTopic(3);
    CHECK(session.characterOpen());
    CHECK(session.dialogueView().cursor == 3);

    // And past the end of the list, nothing moves at all.
    session.chooseVisibleTopic(8);
    session.chooseVisibleTopic(3);  // back onto the grid first
    const int before = session.dialogueView().cursor;
    session.chooseTopic(static_cast<std::size_t>(before));
    CHECK(session.characterOpen());
    CHECK(session.dialogueView().cursor == before);
}

TEST_CASE("the character tile stays open alongside the map, letters and journal tiles, and is exclusive with keys, options and pause -- both ways") {
    // MORROWIND ROUND. Character is one of the tiled Menu's four SIMULTANEOUS
    // panels now (Session::casebookOpen()'s own header): opening any one of
    // the four opens all four together, so "exclusive with the casebook" is
    // no longer a claim this page can make about itself -- see
    // test_vertical_menu.cpp's own "Menu opens all four tiles at once" case
    // for that proof. What is still true, unchanged, is that the tiled Menu
    // as a WHOLE remains exclusive with Keys, Options and Pause, which is
    // this case's own remaining claim.
    render::Session session = standing();

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    CHECK(session.casebookOpen());
    CHECK(session.mapOpen());
    CHECK(session.lettersOpen());

    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.keysOpen());

    session.toggleOptions();
    CHECK(session.optionsOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.optionsOpen());

    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.pauseOpen());
}

TEST_CASE("PagePrev/PageNext move focus onto the character tile without closing it") {
    // The Morrowind-round replacement for the old "toggleCasebook() steals
    // the page back" case above: switching focus to another tile no longer
    // closes this one, because there is no longer a "this one" to close --
    // all four are always open together.
    render::Session session = standing();

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    REQUIRE(session.menuFocus() == render::kMenuFocusCharacter);

    session.menuPageNext();  // Character -> Map
    CHECK(session.menuFocus() == render::kMenuFocusMap);
    CHECK(session.characterOpen());  // still open, just not focused

    session.menuPagePrev();  // back to Character
    CHECK(session.menuFocus() == render::kMenuFocusCharacter);
}

TEST_CASE("the character sheet never opens over a conversation or a pick in progress") {
    render::SessionConfig config = fresh();
    config.timeOfDay = 21 * 3600;
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);
    session.toggleCasebook();  // put the opening page down
    session.interact();
    REQUIRE(session.talking());

    session.toggleCharacter();
    CHECK_FALSE(session.characterOpen());
    CHECK(session.talking());
}

TEST_CASE("the tiled Menu covers the middle of the screen on purpose, unlike a live conversation") {
    // MORROWIND ROUND: THE CLAIM THIS CASE PROVES IS INVERTED FROM WHAT IT
    // USED TO BE. Character was one of #85's six single-panel pages and
    // shared drawDialogue's own centre-clear guarantee with every other one
    // of them; it is now one tile of a Morrowind-style tiled OVERVIEW
    // (menu_view.hpp), and that overview is deliberately exempt from the
    // centre-clear rule -- see that file's own header for why ("there is
    // nobody TO look at while it is up"). A live conversation (still
    // drawDialogue, still exempt from nothing) is the control: it MUST still
    // leave the centre alone, so this proves both halves of the claim at
    // once rather than only the new one.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.toggleCharacter();
    REQUIRE(session.characterOpen());
    render::Framebuffer withMenu(config.width, config.height);
    session.drawFrame(withMenu);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    bool anyDiffer = false;
    for (int y = centre.y0; y < centre.y1 && !anyDiffer; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (withMenu.pixels()[withMenu.index(x, y)] != plain.pixels()[plain.index(x, y)]) {
                anyDiffer = true;
                break;
            }
        }
    }
    CHECK(anyDiffer);
}

TEST_CASE("every word the character sheet can show is a sentence, not a diagnostic") {
    render::Session session = standing();
    session.toggleCharacter();
    REQUIRE(session.characterOpen());

    const auto mustRead = [](const std::string& text) {
        INFO("text: ", text);
        for (const char c : text) {
            CHECK(render::isDrawableGlyph(c));
        }
        CHECK(text.find('_') == std::string::npos);
    };

    for (const std::string& row : session.characterRows()) {
        mustRead(row);
    }
    const render::DialogueViewState view = session.dialogueView();
    mustRead(view.speaker);
    mustRead(view.epithet);
    mustRead(view.line);

    // AND EVERY RUNG A TRACK CAN NAME, not just the fresh-arrival ones --
    // legend.hpp's kTitles table is authored prose and this is the copy bar
    // applied to all twenty of its entries, the same way test_casebook.cpp's
    // "the five tracks are five different people" case walks the whole table
    // for collisions.
    for (std::int32_t track = 0; track < static_cast<std::int32_t>(kLegendTracks); ++track) {
        for (std::int32_t rung = 0; rung <= kLegendRungs; ++rung) {
            mustRead(std::string(legendTitle(static_cast<LegendTrack>(track), rung)));
        }
    }
}
