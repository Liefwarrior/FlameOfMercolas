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
    // TIME-AND-TENURE BUILD: FIVE ROWS. WAIT is new, second -- the door into
    // the hour-select page (see test cases below) -- above the two
    // visit-once doors CONTROLS and SETTINGS, with QUIT staying last.
    REQUIRE(view.topics.size() == 5);
    CHECK(view.topics[0] == "RESUME");
    CHECK(view.topics[1] == "WAIT");
    CHECK(view.topics[2] == "CONTROLS");
    CHECK(view.topics[3] == "SETTINGS");
    CHECK(view.topics[4] == "QUIT GRANADAD");

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
    REQUIRE(session.pauseRows()[3] == "SETTINGS");

    session.movePauseCursor(3);  // RESUME -> WAIT -> CONTROLS -> SETTINGS
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
    REQUIRE(session.pauseRows()[2] == "CONTROLS");

    session.movePauseCursor(2);  // RESUME -> WAIT -> CONTROLS
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
    REQUIRE(session.pauseRows()[4] == "QUIT GRANADAD");

    session.movePauseCursor(4);  // RESUME -> WAIT -> CONTROLS -> SETTINGS -> QUIT
    session.choosePause();
    // ARMED, NOT FIRED. One press on QUIT must not be indistinguishable from
    // one press on RESUME -- that is the entire defect this file exists over.
    CHECK(session.quitArmed());
    CHECK_FALSE(session.quitRequested());
    CHECK(session.pauseOpen());
    // And the row says so, so a player who did not mean to press it twice can
    // see the state they are in before they do.
    CHECK(session.pauseRows()[4] != "QUIT GRANADAD");
    CHECK(session.pauseRows()[4].find("QUIT") != std::string::npos);

    SUBCASE("a second press on the same row confirms it") {
        session.choosePause();
        CHECK(session.quitRequested());
    }

    SUBCASE("moving off the row disarms it without closing the menu") {
        session.movePauseCursor(-1);  // QUIT -> SETTINGS
        CHECK_FALSE(session.quitArmed());
        CHECK(session.pauseOpen());
        CHECK(session.pauseRows()[4] == "QUIT GRANADAD");
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

    SUBCASE("2 opens the Wait page immediately") {
        // TIME-AND-TENURE BUILD: the new second row, in wait mode -- never
        // sleep mode, whatever tile the body stands on. The healing door is
        // the bed's Interact press and only that.
        session.chooseVisibleTopic(1);
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.waitOpen());
        CHECK_FALSE(session.waitSleeping());
    }

    SUBCASE("3 opens CONTROLS immediately") {
        session.chooseVisibleTopic(2);
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.keysOpen());
    }

    SUBCASE("4 opens settings immediately, cursor and all") {
        session.chooseVisibleTopic(3);
        CHECK_FALSE(session.pauseOpen());
        CHECK(session.optionsOpen());
    }

    SUBCASE("5 arms quit, and 5 again confirms it") {
        session.chooseVisibleTopic(4);
        CHECK(session.quitArmed());
        CHECK_FALSE(session.quitRequested());
        session.chooseVisibleTopic(4);
        CHECK(session.quitRequested());
    }

    SUBCASE("1 resumes even from an armed quit -- picking a different row calls it off") {
        session.chooseVisibleTopic(4);
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

TEST_CASE("the pause stack draws as the composed card and genuinely covers the middle") {
    // THE CLAIM FLIPPED, ON PURPOSE -- the same flip test_firstrun.cpp records
    // for the casebook and the keys page. UI-EA-SPEC 1.7 (strip->card): pause
    // and the options page it opens left the HUD strip for the composed
    // master/detail card, the ship note's own standing item, so they now take
    // the frame the way every composed page does. Nobody is standing in front
    // of you while you read a menu of five verbs. The centre-clear rule is
    // not weakened, only scoped: a live conversation, the HUD and the
    // lockpicking overlay are still held to it by their own cases.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.togglePause();
    REQUIRE(session.pauseOpen());
    session.stepMany(MoveInput{}, 16);  // the card's own ease, fully open
    render::Framebuffer withPause(config.width, config.height);
    session.drawFrame(withPause);

    session.movePauseCursor(3);  // RESUME -> WAIT -> CONTROLS -> SETTINGS
    session.choosePause();
    REQUIRE(session.optionsOpen());
    session.stepMany(MoveInput{}, 16);
    render::Framebuffer withSettings(config.width, config.height);
    session.drawFrame(withSettings);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    bool pauseDiffers = false;
    bool settingsDiffer = false;
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (withPause.pixels()[withPause.index(x, y)] != plain.pixels()[plain.index(x, y)]) {
                pauseDiffers = true;
            }
            if (withSettings.pixels()[withSettings.index(x, y)] !=
                plain.pixels()[plain.index(x, y)]) {
                settingsDiffer = true;
            }
        }
    }
    CHECK(pauseDiffers);
    CHECK(settingsDiffer);
    // And the two states draw DIFFERENT cards -- five verbs is not a bindings
    // list.
    CHECK(withPause.pixels() != withSettings.pixels());
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

// ===========================================================================
// TIME-AND-TENURE BUILD -- the WAIT row, and the hour page behind it
// ===========================================================================

TEST_CASE("WAIT passes the hours anywhere safe -- clock moved, calendar synced, nothing mended") {
    // Eleven at night on the open street: safe by the definition in
    // session.hpp (nobody swinging, nobody hostile in reach, feet on dry
    // ground), and one hour short of midnight so the pick below has to carry
    // the ward's calendar across a day boundary -- the exact class of jump
    // the S7 review's eighth finding was about.
    render::SessionConfig config = fresh();
    config.timeOfDay = 23 * 3600;
    render::Session session(config);
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);
    REQUIRE_FALSE(session.casebookOpen());

    session.togglePause();
    session.movePauseCursor(1);  // RESUME -> WAIT
    session.choosePause();
    REQUIRE(session.waitOpen());
    CHECK_FALSE(session.waitSleeping());
    CHECK_FALSE(session.pauseOpen());
    CHECK(session.waitRefusal().empty());

    // The whole clock now (UI-EA-SPEC 1.7 #38): twenty-four rows, one per
    // hour ahead, in the dieted `N - NAME HH:00` form.
    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.speaker == "WAIT");
    REQUIRE(view.topics.size() == 24);
    CHECK(view.topics[0] == "1 - MIDNIGHT 00:00");
    CHECK(view.topics[6] == "7 - DAWN 06:00");
    CHECK(view.topics[23] == "24 - 23:00");

    // NOTHING MENDS -- the owner's ruling, checked against a real bruise.
    const std::int32_t whole = session.tavern().playerHp();
    session.tavern().injurePlayer(4);
    const std::int32_t bruised = session.tavern().playerHp();
    REQUIRE(bruised < whole);

    const std::int64_t dayBefore = session.tavern().dayNumber();
    session.chooseWaitRow(1);  // 2 HOURS -- past midnight, to 01:00
    CHECK_FALSE(session.waitOpen());
    CHECK(session.lastMessage() == "WAITED UNTIL 01:00.");
    CHECK(session.timeOfDay() == hourOfDay(1));
    CHECK(session.tavern().playerHp() == bruised);
    // The calendar came along: the tavern turned its day and the ward's roll
    // followed it, through the same syncWardToCalendar every skip takes.
    CHECK(session.tavern().dayNumber() == dayBefore + 1);
    CHECK(session.ward().day() == session.tavern().dayNumber());
}

TEST_CASE("WAIT is refused out loud with an enemy in reach, and the refusal is re-checked on the press") {
    // Inside the Gull, two tiles from the bartender, at nine in the evening.
    render::SessionConfig config = fresh();
    config.timeOfDay = 21 * 3600;
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);
    session.toggleCasebook();  // put the opening page down
    REQUIRE_FALSE(session.casebookOpen());

    // Safe while the room likes you fine.
    REQUIRE(session.waitRefusal().empty());

    // Make the nearest body HOSTILE through the ledger the attitude actually
    // reads -- no brawl, no swing, just somebody who hates you within reach.
    const Actor* bartender =
        session.tavern().nearestTo(session.body().x(), session.body().y(), 6 * kSubOne);
    REQUIRE(bartender != nullptr);
    SocialLedger& ledger = session.tavern().dialogue().ledger();
    for (int i = 0; i < 50 && ledger.attitudeOf(bartender->id()) != Attitude::Hostile; ++i) {
        ledger.record(bartender->id(), Deed::Robbed);
    }
    REQUIRE(ledger.attitudeOf(bartender->id()) == Attitude::Hostile);

    CHECK(session.waitRefusal() == "NOT WITH AN ENEMY THIS CLOSE.");

    // The page still opens -- it PRINTS the refusal (the top band carries it)
    // -- and the pick is refused with the clock unmoved: the page and the
    // key name the same door.
    session.openWait(false);
    REQUIRE(session.waitOpen());
    CHECK(session.dialogueView().line == "NOT WITH AN ENEMY THIS CLOSE.");
    const int before = session.timeOfDay();
    session.chooseWaitRow(0);
    CHECK(session.waitOpen());
    CHECK(session.timeOfDay() == before);
    CHECK(session.lastMessage() == "NOT WITH AN ENEMY THIS CLOSE.");
}

TEST_CASE("--wait reaches the page headlessly through the pause row itself") {
    render::SmokeRunConfig run;
    run.session.contentDir = content::contentDir();
    run.steps = 0;
    run.stamp = false;
    run.wait = true;
    const render::SmokeRunResult played = render::runSmoke(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.summary.find("wait open=yes") != std::string::npos);
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
    session.movePauseCursor(4);  // RESUME -> WAIT -> CONTROLS -> SETTINGS -> QUIT
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
