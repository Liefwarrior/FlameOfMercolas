// The first ten minutes: what a new player is shown, and what they can find
// out without leaving the game.
//
// THE BRIEF THIS ANSWERS, verbatim: "a real first-run experience -- the player
// starts somewhere sensible, understands what they can do, and is not dropped
// into a systems demo with no orientation", and "controls are documented
// in-game".
//
// Every claim in that sentence is a testable one and this file tests it. The
// hard part is not showing the player things; it is showing them without
// breaking the HUD rule, so half of these cases are about the centre of the
// screen staying empty while all of it is on.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SessionConfig fresh() {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    // The hour the client itself starts a new game at -- see main.cpp. Dawn on
    // the Tarwalk, which is when the gazetteer says the Wielder arrives.
    config.timeOfDay = 8 * 3600;
    // THE SAME FLAG main.cpp SETS for a new game. See SessionConfig.
    config.openingPage = true;
    return config;
}

}  // namespace

TEST_CASE("a new game opens on the case, not on a systems demo") {
    render::Session session(fresh());

    // The notes are up before a single step is taken, with the hook on them.
    CHECK(session.firstRun());
    CHECK(session.casebookOpen());
    const render::DialogueViewState opening = session.dialogueView();
    CHECK(opening.open);
    CHECK(opening.speaker == "THE CASEBOOK");
    CHECK_FALSE(opening.line.empty());
    CHECK_FALSE(opening.epithet.empty());
    // Exactly one lead: the body. Not a list of twelve places, which would be
    // a map screen and not an investigation.
    REQUIRE(opening.topics.size() == 1);

    // AND THE KEYS ARE ON THE MESSAGE ROW, so the first thing a player reads is
    // how to put the notes down.
    INFO(session.lastMessage());
    CHECK(session.lastMessage().find("F1") != std::string::npos);
    CHECK(session.lastMessage().find("J") != std::string::npos);

    // WALKING PUTS IT AWAY, AND IT NEVER COMES BACK BY ITSELF.
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);
    CHECK_FALSE(session.firstRun());
    CHECK_FALSE(session.casebookOpen());
    session.stepMany(walk, 30);
    CHECK_FALSE(session.casebookOpen());
}

TEST_CASE("the spawn is the authored one, on a street, facing the ward") {
    render::Session session(fresh());
    session.stepMany(MoveInput{}, 2);

    // SOMEWHERE SENSIBLE, and "sensible" is checkable: the authored Docks
    // spawn, which is six tiles off the Gilded Gull's door on the Tarwalk.
    CHECK(session.body().tileX() == docks::kSpawnTileX);
    CHECK(session.body().tileY() == docks::kSpawnTileY);
    CHECK(session.body().band() == docks::kSpawnBand);
    // On a cell a body can stand on, which is not a given for a coordinate in
    // a header.
    CHECK(session.tiles().standable(session.body().tileX(), session.body().tileY(),
                                    session.body().band()));
    // And the HUD names the place rather than guessing a band label -- the S2
    // defect that started kPlaces.
    const std::string where = session.placeLabel();
    CHECK_FALSE(where.empty());
    CHECK(where.find("TARWALK") != std::string::npos);
}

TEST_CASE("the objective row tells a new player where to go") {
    render::Session session(fresh());
    session.stepMany(MoveInput{}, 2);

    // With no job taken and no questline started, the corner is not empty: it
    // names the one lead the ward has given you.
    const render::DialogueViewState ignored = session.dialogueView();
    (void)ignored;
    session.toggleCasebook();  // put the opening page down
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer frame(session.config().width, session.config().height);
    session.drawFrame(frame);

    // The line itself, through the same accessor the HUD reads.
    const std::string row = session.caseLine();
    REQUIRE_FALSE(row.empty());
    CHECK(row.find("CASE 0/1") != std::string::npos);
    // AND WHERE TO GO. Nothing read yet, one lead in the book, and the row
    // names it -- which is the whole of the orientation this sprint owed a new
    // player and the corner of the frame did not have.
    CHECK(row.find(" > MISSION OF THE FLAME") != std::string::npos);
}

TEST_CASE("the keys are in the game, and every verb the client binds is on the list") {
    render::Session session(fresh());
    session.stepMany(MoveInput{}, 2);

    session.toggleKeys();
    REQUIRE(session.keysOpen());
    const render::DialogueViewState keys = session.dialogueView();
    CHECK(keys.open);
    CHECK(keys.speaker == "CONTROLS");
    CHECK_FALSE(keys.line.empty());
    REQUIRE(keys.topics.size() >= 15);

    // EVERY VERB THE CLIENT BINDS HAS A ROW. A controls screen that has fallen
    // behind the keyboard is worse than none, because it is believed.
    const auto mentions = [&keys](const char* fragment) {
        for (const std::string& row : keys.topics) {
            if (row.find(fragment) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    // #77 CHECKS WHOLE ROWS, NOT FRAGMENTS. The page is GENERATED from the live
    // binding table now (Session::keyRows), so the interesting failure is no
    // longer "a row went missing" -- it is "a row and its key disagree", and a
    // fragment match cannot see that. These are the exact strings the shipped
    // layout produces.
    for (const char* row : {"MOUSE  LOOK", "W  FORWARD", "S  BACK", "A  STEP LEFT",
                            "D  STEP RIGHT", "LSHIFT  SPRINT", "LCTRL  CROUCH",
                            "SPACE  JUMP", "LALT  WALK", "E  TALK", "Q  LOOK AT IT",
                            "G  HANDS ON IT", "T  PICK A PURSE", "F  PUNCH", "R  SLEEP",
                            "V  CLIMB", "X  DOWN", "TAB  CASEBOOK", "F1  THIS LIST",
                            "ESC  BACK OUT", "F2  OPTIONS", "1-0  QUICK BAR",
                            "F12  SCREENSHOT", "LOCK:"}) {
        INFO("missing key row: " << row);
        CHECK(mentions(row));
    }

    // AND THE ONE THING ON THIS PAGE THAT IS NOT A KEY AT ALL. Contextual
    // traversal has no binding to print, which is the entire point of it, so the
    // page has to say so in words or a player will never find out that walking
    // at a wall is how you get on top of it.
    CHECK(mentions("WALK AT A LEDGE"));

    // A REBINDING SHOWS UP HERE, BY CONSTRUCTION. This page used to be a static
    // array of strings a hundred lines from the client's switch statement, with
    // a comment claiming it could not drift from the bindings. Nothing connected
    // the two, so it could not help drifting. Now it IS the binding table read
    // out loud, and this is the assertion that says so.
    render::ControlSettings rebound = session.controls();
    rebound.bind(render::Action::Jump, render::Key::MouseX1);
    session.setControls(rebound);
    const render::DialogueViewState after = session.dialogueView();
    const auto mentionsAfter = [&after](const char* fragment) {
        for (const std::string& row : after.topics) {
            if (row.find(fragment) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    CHECK(mentionsAfter("MOUSE4  JUMP"));
    CHECK_FALSE(mentionsAfter("SPACE  JUMP"));
    session.setControls(render::ControlSettings::defaults());

    // AND EVERY ROW FITS THE COLUMN IT IS DRAWN IN. Sixteen characters is what
    // a third of the bottom band holds at 640x360; the first S10 capture of
    // this page shipped "SPACE  UP: MANT." and a player reads a truncation as
    // the binding.
    for (const std::string& row : keys.topics) {
        INFO("key row too wide: " << row);
        CHECK(row.size() <= 16);
    }

    // IT PAGES. Twenty rows against nine keys is exactly the shape that dropped
    // a topic silently in S3, and the fix is the same fix: the list is paged
    // and every row is reachable from the numbers.
    CHECK(render::topicPageCount(keys.topics.size()) > 1);
    session.nextTopicPage();
    const render::DialogueViewState second = session.dialogueView();
    CHECK(second.page == 1);
    CHECK(second.topics.size() == keys.topics.size());

    // F1 again puts it down.
    session.toggleKeys();
    CHECK_FALSE(session.keysOpen());
}

TEST_CASE("the notes, the keys and the world never fight over the middle of the screen") {
    // THE HUD RULE, WITH EVERY S10 SURFACE ON. The centre stays pixel-identical
    // whichever of the three is up, which is the only way to prove a rule that
    // is about absence.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    // Put the opening page down so `plain` really is the plain frame.
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    render::Framebuffer withNotes(config.width, config.height);
    session.drawFrame(withNotes);

    session.toggleKeys();
    REQUIRE(session.keysOpen());
    REQUIRE_FALSE(session.casebookOpen());
    render::Framebuffer withKeys(config.width, config.height);
    session.drawFrame(withKeys);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withNotes.pixels()[withNotes.index(x, y)] ==
                    plain.pixels()[plain.index(x, y)]);
            REQUIRE(withKeys.pixels()[withKeys.index(x, y)] == plain.pixels()[plain.index(x, y)]);
        }
    }
}

TEST_CASE("the two surfaces are exclusive, and neither opens over a conversation") {
    render::Session session(fresh());
    session.stepMany(MoveInput{}, 2);

    // One at a time. Two lists in one band is the overprint the S7 review
    // caught, and this is the version of it that could happen by keypress.
    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.casebookOpen());
    session.toggleCasebook();
    CHECK(session.casebookOpen());
    CHECK_FALSE(session.keysOpen());
    session.toggleCasebook();
    CHECK_FALSE(session.casebookOpen());

    // AND NEITHER OPENS WHILE SOMEBODY IS TALKING TO YOU. Stand in front of
    // the bartender at an hour the house is open and start a conversation.
    render::SessionConfig config = fresh();
    config.timeOfDay = 21 * 3600;
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    render::Session talking(config);
    talking.stepMany(MoveInput{}, 2);
    talking.toggleCasebook();  // put the opening page down
    talking.interact();
    REQUIRE(talking.talking());
    talking.toggleCasebook();
    CHECK_FALSE(talking.casebookOpen());
    talking.toggleKeys();
    CHECK_FALSE(talking.keysOpen());
    // And the look key does nothing mid-sentence either.
    talking.examine();
    CHECK(talking.talking());
}
