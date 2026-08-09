// THE PAUSE MENU: RESUME, SETTINGS, QUIT -- and the one key that used to skip
// straight past all three.
//
// THE BUG THIS CLOSES. src/client/main.cpp's ESC case carried this, verbatim,
// for every sprint since #77 added a page ESC could back out OF:
//
//   ESCAPE BACKS OUT OF WHATEVER IS OPEN, and only quits when nothing is.
//
// and then did exactly that: `running = false`, no confirmation, no way back
// if the finger that pressed it did not mean to. Every OTHER key in this game
// treats "close the window" as too large a thing to do on one press -- QUIT
// itself now asks twice -- and ESC at the top level was the one door with no
// second step behind it.
//
// WHAT IS AND IS NOT TESTED HERE. Session owns togglePause/pauseRows/
// movePauseCursor/choosePause/quitArmed/quitRequested, and none of it touches
// SDL, so all of it is driven directly -- the same shape test_controls.cpp and
// test_firstrun.cpp already use for the pages either side of this one. The
// keyboard-to-Session wiring in main.cpp's route_menu_key (ESC opens the menu,
// the arrows walk it, ENTER and the printed numbers act on it) is NOT covered
// here for the same reason the scancode table above it never has been --
// closing that hole needs a real SDL harness, which is what
// docs/frames/p77-controls exists for and drive-windowed.ps1 is the tool for.

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"

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

/// A session with the opening casebook page already put down, the way every
/// case below wants to start: standing in the world, nothing already open.
[[nodiscard]] render::Session standing() {
    render::Session session(fresh());
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);  // closes the opening page, same as test_firstrun.cpp
    REQUIRE_FALSE(session.casebookOpen());
    REQUIRE_FALSE(session.pauseOpen());
    return session;
}

}  // namespace

TEST_CASE("ESC opens a menu now, not the window's close button") {
    render::Session session = standing();

    // toggling is what main.cpp's ESC case does when nothing else has already
    // claimed the keyboard -- see the case comment there.
    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.quitRequested());

    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    // NOT "PAUSED". The world keeps ticking under this page exactly like it
    // does under the casebook, the keys page and options -- see the header on
    // dialogueView's pauseOpen_ branch -- and a label promising a freeze this
    // build does not do is the same class of bug as an enum name in a bark.
    CHECK(view.speaker == "MENU");
    CHECK(view.speaker != "PAUSED");
    // MORROWIND ROUND: FOUR ROWS, NOT THREE. CONTROLS is new -- Keys' own
    // relocated door, the identical shape SETTINGS already was for Options
    // -- since the tiled Menu's four tiles have no room left for either.
    REQUIRE(view.topics.size() == 4);
    CHECK(view.topics[0] == "RESUME");
    CHECK(view.topics[1] == "CONTROLS");
    CHECK(view.topics[2] == "SETTINGS");
    CHECK(view.topics[3] == "QUIT GRANADAD");

    // The second press resumes -- the same key, the same toggle, exactly the
    // way F1 and F2 already behave.
    session.togglePause();
    CHECK_FALSE(session.pauseOpen());
    CHECK_FALSE(session.dialogueView().open);
}

TEST_CASE("the pause menu is exclusive with the casebook, the keys page and options -- both ways") {
    // #77 shipped this asymmetric: opening OPTIONS over KEYS correctly closed
    // the keys page, but opening KEYS over OPTIONS left optionsOpen_ true --
    // the keys page drew (dialogueView() checks it first) while
    // route_menu_key kept routing the arrow keys to moveOptionCursor
    // underneath it, because that check ran before the keys/casebook one. The
    // page on screen and the page reading the keyboard were two different
    // pages. This case is the fix for every direction that bug could point,
    // pause menu included, since it is the newest way into the same trap.
    render::Session session = standing();

    session.toggleOptions();
    REQUIRE(session.optionsOpen());

    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.optionsOpen());

    session.toggleOptions();
    CHECK(session.optionsOpen());
    CHECK_FALSE(session.keysOpen());

    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.optionsOpen());

    session.toggleCasebook();
    CHECK(session.casebookOpen());
    CHECK_FALSE(session.pauseOpen());

    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.casebookOpen());

    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.pauseOpen());

    // And never over a real conversation, or while a lock is being picked --
    // the same guard every other overlay already carries.
    session.toggleKeys();  // put the keys page down first
    REQUIRE_FALSE(session.keysOpen());
}

TEST_CASE("SETTINGS reaches the controls round's rebinding screen from the menu a player pauses on") {
    render::Session session = standing();

    session.togglePause();
    REQUIRE(session.pauseOpen());
    REQUIRE(session.pauseRows()[2] == "SETTINGS");

    session.movePauseCursor(2);  // RESUME -> CONTROLS -> SETTINGS
    session.choosePause();

    // The exact page F2 used to open, reached a different way. Rebinding a
    // key here is rebinding a key, full stop -- there is only one options
    // page.
    CHECK_FALSE(session.pauseOpen());
    CHECK(session.optionsOpen());
    CHECK(session.dialogueView().speaker == "OPTIONS");
}

TEST_CASE("MORROWIND ROUND: CONTROLS reaches the keys page from the menu a player pauses on") {
    // Keys' own new door -- the identical shape SETTINGS already proves for
    // Options, above -- since the tiled Menu's four tiles have no room left
    // for a long, read-top-to-bottom reference list.
    render::Session session = standing();

    session.togglePause();
    REQUIRE(session.pauseOpen());
    REQUIRE(session.pauseRows()[1] == "CONTROLS");

    session.movePauseCursor(1);  // RESUME -> CONTROLS
    session.choosePause();

    // The exact page F1 used to open, reached a different way -- the keys
    // page itself is unchanged.
    CHECK_FALSE(session.pauseOpen());
    CHECK(session.keysOpen());
    CHECK(session.dialogueView().speaker == "CONTROLS");
}

TEST_CASE("QUIT asks twice, and moving the cursor or pressing ESC calls it off") {
    render::Session session = standing();

    session.togglePause();
    REQUIRE(session.pauseOpen());
    REQUIRE(session.pauseRows()[3] == "QUIT GRANADAD");

    session.movePauseCursor(3);  // RESUME -> CONTROLS -> SETTINGS -> QUIT
    session.choosePause();
    // ARMED, NOT FIRED. One press on QUIT must not be indistinguishable from
    // one press on RESUME -- that is the entire defect this file exists over.
    CHECK(session.quitArmed());
    CHECK_FALSE(session.quitRequested());
    CHECK(session.pauseOpen());
    // And the row says so, so a player who did not mean to press it twice can
    // see the state they are in before they do.
    CHECK(session.pauseRows()[3] != "QUIT GRANADAD");
    CHECK(session.pauseRows()[3].find("QUIT") != std::string::npos);

    SUBCASE("a second press on the same row confirms it") {
        session.choosePause();
        CHECK(session.quitRequested());
    }

    SUBCASE("moving off the row disarms it without closing the menu") {
        session.movePauseCursor(-1);  // QUIT -> SETTINGS
        CHECK_FALSE(session.quitArmed());
        CHECK(session.pauseOpen());
        CHECK(session.pauseRows()[3] == "QUIT GRANADAD");
    }

    SUBCASE("ESC disarms it on the first press, and closes the menu on the second") {
        session.closeConversation();  // what main.cpp's ESC case calls
        CHECK_FALSE(session.quitArmed());
        CHECK(session.pauseOpen());
        session.closeConversation();
        CHECK_FALSE(session.pauseOpen());
    }

    SUBCASE("it never fires on its own -- a fresh menu is never armed") {
        // Regression guard for the obvious way to get this wrong: quitArmed_
        // surviving a close/reopen and firing on the first press of a second
        // sitting down at the menu.
        session.togglePause();  // closes it, armed and all
        session.togglePause();  // opens a fresh one
        CHECK_FALSE(session.quitArmed());
        CHECK_FALSE(session.quitRequested());
    }
}

TEST_CASE("the printed number picks a pause row exactly the way it picks a topic") {
    // dialogue_view.cpp's topicRowsFor prints "1 ", "2 ", "3 " ahead of every
    // row this surface draws, on every list it draws, this one included. A
    // page that left those digits live on screen but unanswered by the
    // keyboard would be worse than a page with no numbers at all.
    render::Session session = standing();

    session.togglePause();
    REQUIRE(session.pauseOpen());

    SUBCASE("2 opens CONTROLS immediately") {
        session.chooseVisibleTopic(1);
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.keysOpen());
    }

    SUBCASE("3 opens settings immediately, cursor and all") {
        session.chooseVisibleTopic(2);
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.optionsOpen());
    }

    SUBCASE("4 arms quit, and 4 again confirms it") {
        session.chooseVisibleTopic(3);
        CHECK(session.quitArmed());
        CHECK_FALSE(session.quitRequested());
        session.chooseVisibleTopic(3);
        CHECK(session.quitRequested());
    }

    SUBCASE("1 resumes even from an armed quit -- picking a different row calls it off") {
        session.chooseVisibleTopic(3);
        REQUIRE(session.quitArmed());
        session.chooseVisibleTopic(0);
        CHECK_FALSE(session.pauseOpen());
        CHECK_FALSE(session.quitArmed());
        CHECK_FALSE(session.quitRequested());
    }

    SUBCASE("a slot past the last row does nothing") {
        session.chooseVisibleTopic(5);
        CHECK(session.pauseOpen());
        CHECK_FALSE(session.quitArmed());
    }
}

TEST_CASE("the pause menu never opens over a conversation or a pick in progress") {
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

    session.togglePause();
    CHECK_FALSE(session.pauseOpen());
    CHECK(session.talking());
}

TEST_CASE("the menu, the options it opens and the plain scene never fight over the middle of the screen") {
    // THE SAME MEASUREMENT test_firstrun.cpp already runs for the casebook and
    // the keys page, extended to the two ways into this one -- ESC's own menu,
    // and the options page it opens. #77's own review found this exact class
    // of defect on the options page nothing scripted ever opened; this is the
    // regression guard for it happening again on the page ESC now opens by
    // default.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.togglePause();
    REQUIRE(session.pauseOpen());
    render::Framebuffer withPause(config.width, config.height);
    session.drawFrame(withPause);

    session.movePauseCursor(2);  // RESUME -> CONTROLS -> SETTINGS
    session.choosePause();
    REQUIRE(session.optionsOpen());
    render::Framebuffer withSettings(config.width, config.height);
    session.drawFrame(withSettings);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withPause.pixels()[withPause.index(x, y)] ==
                    plain.pixels()[plain.index(x, y)]);
            REQUIRE(withSettings.pixels()[withSettings.index(x, y)] ==
                    plain.pixels()[plain.index(x, y)]);
        }
    }
}

TEST_CASE("--pause reaches the menu headlessly, for an environment that cannot drive a real window") {
    // scripts/drive-windowed.ps1 is how every other page in this game got
    // PHOTOGRAPHED rather than merely argued for -- see its header and
    // docs/frames/p77-controls. It needs a desktop able to grant a window the
    // foreground, and it correctly REFUSES to drive anything when one is not
    // there. This flag is the fallback for exactly that environment: proof
    // the menu (and the settings page and the armed QUIT it can reach) draws
    // legibly, even where the real thing cannot be driven.
    const auto play = [](std::string where) {
        render::SmokeRunConfig run;
        run.session.contentDir = content::contentDir();
        run.steps = 0;
        run.stamp = false;
        run.pause = true;
        run.pauseEnd = std::move(where);
        return render::runSmoke(run);
    };

    SUBCASE("menu") {
        const render::SmokeRunResult played = play("menu");
        INFO(played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
    }
    SUBCASE("controls") {
        // MORROWIND ROUND: KEYS' OWN NEW PAUSE-SIDE DOOR.
        const render::SmokeRunResult played = play("controls");
        INFO(played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
    }
    SUBCASE("settings") {
        const render::SmokeRunResult played = play("settings");
        INFO(played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
    }
    SUBCASE("armed") {
        // "landed" here is defined as pauseOpen() && quitArmed() -- see
        // runSmoke's own config.pause block -- which is true after exactly
        // ONE choosePause() on the QUIT row and false again the moment a
        // second one confirms it (quitArmed_ drops, quitRequested_ takes
        // over). A miscount that fired the confirming press here instead of
        // stopping at the arm would flip this false, not true, so this one
        // case is standing in for both halves of "asks twice": the arm
        // registers, and the confirm has not also silently happened.
        const render::SmokeRunResult played = play("armed");
        INFO(played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
    }
}

TEST_CASE("every word the pause menu can show is a sentence, not a diagnostic") {
    render::Session session = standing();
    session.togglePause();
    REQUIRE(session.pauseOpen());

    const auto mustRead = [](const std::string& text) {
        INFO("text: ", text);
        for (const char c : text) {
            CHECK(render::isDrawableGlyph(c));
        }
        CHECK(text.find('_') == std::string::npos);
    };

    for (const std::string& row : session.pauseRows()) {
        mustRead(row);
    }
    session.movePauseCursor(3);  // RESUME -> CONTROLS -> SETTINGS -> QUIT
    session.choosePause();
    REQUIRE(session.quitArmed());
    for (const std::string& row : session.pauseRows()) {
        mustRead(row);
    }

    const render::DialogueViewState view = session.dialogueView();
    mustRead(view.speaker);
    mustRead(view.epithet);
    mustRead(view.line);
}
