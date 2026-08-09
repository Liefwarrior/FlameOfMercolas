// #85. Two smaller pieces of the same consolidation: Vertical (Jump +
// Traverse + DropDown, resolved by what is directly ahead or below) and the
// Menu router (Journal/Character/Map/Letters/Keys/Options behind one button
// with PagePrev/PageNext flipping between them).
//
// Neither of these touches the six pages' or the three roof moves' own
// behaviour -- test_roofrun.cpp and the pause/casebook/keys/character/map/
// letters cases already prove those in depth, unchanged. What is new here is
// the ORCHESTRATION on top: which of several tried-in-order attempts a single
// button resolves to, and that flipping pages never leaves two pages open or
// loses track of which one is showing.

#include <doctest/doctest.h>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

SessionConfig onTheStreet() {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.spawnX = sim::docks::kSpawnTileX;
    config.spawnY = sim::docks::kSpawnTileY;
    config.spawnBand = sim::docks::kSpawnBand;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

TEST_CASE("Vertical resolves to an ordinary jump on flat, open ground") {
    // THE AUTHORED SPAWN: a street, not a roof edge -- nothing to climb and
    // nothing to step off, so both of vertical()'s first two attempts refuse
    // and the third (an ordinary standing jump) is what is left.
    Session session(onTheStreet());
    session.stepMany(sim::MoveInput{}, 2);
    REQUIRE_FALSE(session.body().jumping());
    REQUIRE_FALSE(session.awaitingLanding());

    session.vertical();
    // A STANDING JUMP, NOT A CLIMB OR A DROP: jumping() is only ever true for
    // PlayerBody::jump()'s own arc (climb()/dropDown() charge a landing and a
    // roof move instead, and awaitingLanding()/lastRoofMove() would say so).
    CHECK(session.body().jumping());
    CHECK_FALSE(session.awaitingLanding());
}

TEST_CASE("Vertical does nothing while talking, same as the three verbs it folds") {
    // A DETERMINISTIC CONVERSATION, not left to whoever the ward happens to
    // put in reach: the same bartender position test_tavern_render.cpp's own
    // "E opens a conversation" case uses.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = 11 * 3600;
    config.spawnX = sim::gull::kBartenderX;
    config.spawnY = sim::gull::kBarY - 1;
    config.spawnBand = sim::gull::kGroundBand;
    config.width = 320;
    config.height = 180;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    session.interact();
    REQUIRE(session.talking());

    const bool wasJumping = session.body().jumping();
    session.vertical();
    CHECK(session.body().jumping() == wasJumping);
    CHECK(session.talking());
}

TEST_CASE("Menu opens on the casebook, and the key that opened it closes it") {
    Session session(onTheStreet());
    REQUIRE_FALSE(session.menuOpen());

    session.toggleMenu();
    CHECK(session.menuOpen());
    CHECK(session.casebookOpen());
    CHECK_FALSE(session.characterOpen());
    CHECK_FALSE(session.mapOpen());
    CHECK_FALSE(session.lettersOpen());
    CHECK_FALSE(session.keysOpen());
    CHECK_FALSE(session.optionsOpen());

    session.toggleMenu();
    CHECK_FALSE(session.menuOpen());
    CHECK_FALSE(session.casebookOpen());
}

TEST_CASE("PagePrev/PageNext cycle the six pages in order and wrap both ways") {
    Session session(onTheStreet());
    session.toggleMenu();
    REQUIRE(session.casebookOpen());

    // FORWARD, ALL SIX, BACK TO THE START. Exactly one page open at every
    // step -- the six toggle*() methods' own exclusivity blocks are what
    // guarantees that, not anything new here, but this is the case that
    // would go red if the router ever called the wrong one.
    session.menuPageNext();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.casebookOpen());

    session.menuPageNext();
    CHECK(session.mapOpen());

    session.menuPageNext();
    CHECK(session.lettersOpen());

    session.menuPageNext();
    CHECK(session.keysOpen());

    session.menuPageNext();
    CHECK(session.optionsOpen());

    // WRAPS FORWARD, BACK TO THE CASEBOOK.
    session.menuPageNext();
    CHECK(session.casebookOpen());

    // AND WRAPS BACKWARD, THE OTHER WAY, LANDING ON OPTIONS FIRST.
    session.menuPagePrev();
    CHECK(session.optionsOpen());
    session.menuPagePrev();
    CHECK(session.keysOpen());

    // Exactly one page open throughout -- spot-checked at the far end of the
    // cycle rather than at every step above.
    CHECK_FALSE(session.casebookOpen());
    CHECK_FALSE(session.characterOpen());
    CHECK_FALSE(session.mapOpen());
    CHECK_FALSE(session.lettersOpen());
    CHECK_FALSE(session.optionsOpen());
}

TEST_CASE("PagePrev/PageNext do nothing while Menu is not open") {
    Session session(onTheStreet());
    REQUIRE_FALSE(session.menuOpen());
    session.menuPageNext();
    CHECK_FALSE(session.menuOpen());
    session.menuPagePrev();
    CHECK_FALSE(session.menuOpen());
}

TEST_CASE("Pause stays a separate system from Menu, per controls.hpp's own note") {
    Session session(onTheStreet());
    session.togglePause();
    REQUIRE(session.pauseOpen());
    CHECK_FALSE(session.menuOpen());

    // Opening Menu while Pause is up stands Pause down, the same exclusivity
    // every one of the six pages already enforces against each other and
    // against Pause -- see toggleCasebook()'s own comment.
    session.toggleMenu();
    CHECK(session.menuOpen());
    CHECK_FALSE(session.pauseOpen());

    // AND OPTIONS IS THE SAME STATE REACHED FROM PAUSE'S OWN SETTINGS ROW.
    // Reachable two ways, never two screens: toggling to the Options page of
    // Menu is exactly optionsOpen_, the same flag choosePause()'s SETTINGS
    // row has always driven through toggleOptions().
    for (int i = 0; i < 5; ++i) {
        session.menuPageNext();
    }
    REQUIRE(session.optionsOpen());
    session.toggleMenu();  // closes Options, the currently-open page
    CHECK_FALSE(session.optionsOpen());
    CHECK_FALSE(session.menuOpen());
}
