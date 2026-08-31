// #85. Two smaller pieces of the same consolidation: Vertical (Jump +
// Traverse + DropDown, resolved by what is directly ahead or below) and the
// Menu router.
//
// MORROWIND ROUND: THE MENU HALF OF THIS FILE IS REWRITTEN. #85's Menu used
// to flip between six independent pages one at a time; it is now a tiled
// overview -- Character/Map/Letters/Journal drawn simultaneously (see
// menu_view.hpp), Keys and Options relocated to Pause's own CONTROLS/
// SETTINGS rows (test_pause.cpp) -- and PagePrev/PageNext step which of the
// four tiles has FOCUS instead of which page is showing. The pages'
// individual content is unchanged and still proved by test_casebook.cpp,
// test_character.cpp and test_map.cpp; what is new here is the tiled
// exclusivity (all four together, or none) and the focus cycle.
//
// Neither half touches the three roof moves' own behaviour -- test_roofrun.cpp
// already proves that in depth, unchanged. What is new here is the
// ORCHESTRATION on top: which of several tried-in-order attempts a single
// button resolves to, and that stepping focus never opens or closes a tile.

#include <doctest/doctest.h>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/menu_view.hpp"
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

TEST_CASE("Menu opens all four tiles at once, and the key that opened it closes all four") {
    Session session(onTheStreet());
    REQUIRE_FALSE(session.menuOpen());

    session.toggleMenu();
    CHECK(session.menuOpen());
    // MORROWIND ROUND: ALL FOUR TILES TOGETHER, NOT ONE PAGE AT A TIME.
    // casebookOpen()/characterOpen()/mapOpen()/lettersOpen() are the
    // identical bool now (Session::casebookOpen()'s own header) -- the four
    // tiles draw simultaneously, per Eli's brief: "just show them like
    // Morrowind does".
    CHECK(session.casebookOpen());
    CHECK(session.characterOpen());
    CHECK(session.mapOpen());
    CHECK(session.lettersOpen());
    // Keys and Options are NOT tiles -- they relocated to Pause's own
    // CONTROLS/SETTINGS rows (test_pause.cpp) -- so opening the tiled Menu
    // never opens either.
    CHECK_FALSE(session.keysOpen());
    CHECK_FALSE(session.optionsOpen());
    // Opens focused on the Journal tile, the same tile #85's six-page Menu
    // always opened on first.
    CHECK(session.menuFocus() == kMenuFocusJournal);

    session.toggleMenu();
    CHECK_FALSE(session.menuOpen());
    CHECK_FALSE(session.casebookOpen());
    CHECK_FALSE(session.characterOpen());
    CHECK_FALSE(session.mapOpen());
    CHECK_FALSE(session.lettersOpen());
}

TEST_CASE("PagePrev/PageNext step which tile has focus, and wrap both ways, without closing any tile") {
    Session session(onTheStreet());
    session.toggleMenu();
    REQUIRE(session.casebookOpen());
    REQUIRE(session.menuFocus() == kMenuFocusJournal);

    // FORWARD, ALL FOUR, BACK TO THE START. All four tiles stay open
    // throughout -- there is no "page" left to close, only which one is
    // reading the keyboard right now.
    session.menuPageNext();
    CHECK(session.menuFocus() == kMenuFocusCharacter);
    CHECK(session.characterOpen());
    CHECK(session.casebookOpen());  // the Journal tile is STILL open

    session.menuPageNext();
    CHECK(session.menuFocus() == kMenuFocusMap);

    session.menuPageNext();
    CHECK(session.menuFocus() == kMenuFocusLetters);

    // WRAPS FORWARD, BACK TO THE JOURNAL.
    session.menuPageNext();
    CHECK(session.menuFocus() == kMenuFocusJournal);

    // AND WRAPS BACKWARD, THE OTHER WAY, LANDING ON LETTERS FIRST.
    session.menuPagePrev();
    CHECK(session.menuFocus() == kMenuFocusLetters);
    session.menuPagePrev();
    CHECK(session.menuFocus() == kMenuFocusMap);

    // All four tiles stayed open throughout -- spot-checked at the far end
    // of the cycle rather than at every step above.
    CHECK(session.casebookOpen());
    CHECK(session.characterOpen());
    CHECK(session.mapOpen());
    CHECK(session.lettersOpen());
}

TEST_CASE("PagePrev/PageNext do nothing while Menu is not open") {
    Session session(onTheStreet());
    REQUIRE_FALSE(session.menuOpen());
    session.menuPageNext();
    CHECK_FALSE(session.menuOpen());
    session.menuPagePrev();
    CHECK_FALSE(session.menuOpen());
}

TEST_CASE("PagePrev/PageNext do nothing while Keys or Options (not the tiled Menu) is open") {
    // MORROWIND ROUND. Keys and Options have no tiles of their own to step
    // between -- they are single lists, reached through Pause now -- so a
    // PagePrev/PageNext press while either is open must not silently open
    // or refocus the tiled Menu underneath it.
    Session session(onTheStreet());
    session.toggleKeys();
    REQUIRE(session.keysOpen());
    session.menuPageNext();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.casebookOpen());
    session.toggleKeys();

    session.toggleOptions();
    REQUIRE(session.optionsOpen());
    session.menuPagePrev();
    CHECK(session.optionsOpen());
    CHECK_FALSE(session.casebookOpen());
}

TEST_CASE("Pause stays a separate system from Menu, per controls.hpp's own note") {
    Session session(onTheStreet());
    session.togglePause();
    REQUIRE(session.pauseOpen());
    CHECK_FALSE(session.menuOpen());

    // Opening Menu while Pause is up stands Pause down, the same exclusivity
    // every overlay already enforces against each other and against Pause --
    // see toggleCasebook()'s own comment.
    session.toggleMenu();
    CHECK(session.menuOpen());
    CHECK_FALSE(session.pauseOpen());

    // MORROWIND ROUND: OPTIONS AND KEYS NO LONGER LIVE ON THE TILED MENU'S
    // OWN CYCLE. PagePrev/PageNext only step which of the FOUR TILES has
    // focus now -- Options and Keys have no tile to land on, so cycling
    // never reaches either.
    for (int i = 0; i < 6; ++i) {
        session.menuPageNext();
    }
    CHECK_FALSE(session.optionsOpen());
    CHECK_FALSE(session.keysOpen());
    CHECK(session.casebookOpen());
    session.toggleMenu();
    CHECK_FALSE(session.menuOpen());

    // AND OPTIONS IS THE SAME STATE REACHED FROM PAUSE'S OWN SETTINGS ROW.
    // Reachable two ways, never two screens: choosePause()'s SETTINGS row
    // has always driven through toggleOptions() (controls.hpp's own note),
    // relocated here from Menu's own now-defunct Options page.
    session.togglePause();
    REQUIRE(session.pauseOpen());
    session.movePauseCursor(3);  // RESUME -> WAIT -> CONTROLS -> SETTINGS
    session.choosePause();
    REQUIRE(session.optionsOpen());
    session.toggleOptions();  // closes it
    CHECK_FALSE(session.optionsOpen());
    CHECK_FALSE(session.menuOpen());
}

// ===========================================================================
// THE EMPTY STATES -- three of these four tiles ship a stranger a header over
// a void, and UI-REFERENCE-TERMINAL.md rules on that by name
// ===========================================================================

TEST_CASE("the tiles that ship empty say what they are waiting for, in room the rows did not want") {
    // THE LETTERS holds NOTHING on a new game -- a quarter-screen panel with a
    // title and black under it. THE CHART holds three rows and the casebook
    // tile holds one. `line` cannot answer this: the three top tiles suppress
    // it on purpose (see drawMenuTiles), and it says what the panel IS rather
    // than what would be in it. So each carries its own `emptyLine`.
    Session session(onTheStreet());
    session.stepMany(sim::MoveInput{}, 2);

    session.toggleLetters();
    REQUIRE(session.menuFocus() == kMenuFocusLetters);
    const DialogueViewState letters = session.dialogueView();
    REQUIRE(letters.speaker == "THE LETTERS");
    // THE ONE PANEL THAT IS GENUINELY EMPTY. Not vacuous: if a letter ever
    // ships unlocked this case is measuring the wrong state.
    REQUIRE(letters.topics.empty());
    CHECK_FALSE(letters.emptyLine.empty());
    // It names the act that fills it, and the act is standing over a lead --
    // unlockedLetters()' own gate, not merely hearing one.
    CHECK(letters.emptyLine.find("STAND OVER") != std::string::npos);

    session.menuPageNext();  // Letters -> Journal
    REQUIRE(session.menuFocus() == kMenuFocusJournal);
    const DialogueViewState book = session.dialogueView();
    REQUIRE(book.speaker == "THE CASEBOOK");
    REQUIRE(book.topics.size() >= 1);
    CHECK_FALSE(book.emptyLine.empty());
    // ONE STRING, TWO SURFACES: the tile and the full-screen page word the same
    // absence with the same sentence rather than drifting apart.
    CHECK(book.emptyLine == std::string(kBookWaitingLine));

    session.menuPageNext();  // Journal -> Character
    session.menuPageNext();  // Character -> Map
    REQUIRE(session.menuFocus() == kMenuFocusMap);
    const DialogueViewState chart = session.dialogueView();
    REQUIRE(chart.speaker == "THE CHART");
    CHECK_FALSE(chart.emptyLine.empty());
    CHECK(chart.emptyLine.find("CASEBOOK") != std::string::npos);

    // AND THE TILES ACTUALLY DRAW IT. Everything else about the two frames is
    // identical, so the difference IS the sentences.
    MenuTileState tiles;
    tiles.open = true;
    tiles.map = chart;
    tiles.letters = letters;
    tiles.journal = book;
    for (const int height : {180, 360, 540, 1080}) {
        const int width = height * 16 / 9;
        Framebuffer worded(width, height);
        drawMenuTiles(worded, tiles);
        MenuTileState blank = tiles;
        blank.map.emptyLine.clear();
        blank.letters.emptyLine.clear();
        blank.journal.emptyLine.clear();
        Framebuffer silent(width, height);
        drawMenuTiles(silent, blank);
        INFO("at ", width, "x", height);
        CHECK(worded.pixels() != silent.pixels());
    }
}

TEST_CASE("a tile stops claiming it is empty once it is not") {
    // THE WORDING IS PER-STATE AND BELONGS TO THE CALLER -- a Letters tile
    // holding a document must not still say you have read nobody's post. The
    // casebook tile's gate is the harder one: it goes silent when every lead
    // in the file is already in the book, because then there is nothing left
    // for a lead to open.
    Session session(onTheStreet());
    session.stepMany(sim::MoveInput{}, 2);
    session.toggleCasebook();
    REQUIRE(session.menuFocus() == kMenuFocusJournal);
    const std::size_t before = session.dialogueView().topics.size();
    for (int i = 0; i < 64; ++i) {
        (void)session.casebook().hear(i);
    }
    const DialogueViewState book = session.dialogueView();
    REQUIRE(book.topics.size() > before);
    CHECK(book.emptyLine.empty());
}
