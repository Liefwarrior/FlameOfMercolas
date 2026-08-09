// #82: THE DISTRICT MAP -- Daggerfall's own travel map, scaled to one
// district: known ground, open leads (each with a bearing and a range from
// where the player is standing) and who among the named will actually talk.
//
// WHY THIS IS NOT A NEW SUBSYSTEM. All three sections are read straight off
// state the game already keeps -- Casebook, NotableRegistry, FactionRegistry
// -- so this file is mostly about the READING, not about anything new being
// tracked. See Session::mapRows' own header for why there is no separate
// "have you been here" or "have you met them" flag to test the persistence
// of: there isn't one.
//
// SAME SHAPE AS test_character.cpp AND test_pause.cpp: Session owns
// toggleMap/mapRows and dialogueView()'s mapOpen_ branch, none of it touches
// SDL, so all of it is driven directly. The keyboard-to-Session wiring in
// main.cpp's route_menu_key (M opens it, the arrows walk it) is not covered
// here for the same reason it never is on the other pages: that needs a real
// SDL harness.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/stealth.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] const CasebookRaws& raws() {
    static const CasebookRaws loaded = CasebookRaws::load(content::contentDir());
    return loaded;
}

[[nodiscard]] render::SessionConfig fresh() {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.openingPage = true;
    return config;
}

/// A session with the opening casebook page already put down, nothing else
/// open -- the state every case below wants to start from. Same shape
/// test_character.cpp's own `standing()` uses.
[[nodiscard]] render::Session standing() {
    render::Session session(fresh());
    MoveInput walk;
    walk.forward = 1;
    session.step(walk);  // closes the opening page, same as test_firstrun.cpp
    REQUIRE_FALSE(session.casebookOpen());
    REQUIRE_FALSE(session.mapOpen());
    return session;
}

/// The exact string bearingLabel() (session.cpp, private to that file) would
/// print for a site from where `session` is standing -- built out of the same
/// two PUBLIC primitives that function is documented to use, sim::bearingTo
/// and sim::compass_point, so this is a claim about the FORMULA and not a
/// magic string copied out of a manual run.
[[nodiscard]] std::string expectedBearing(const render::Session& session, std::int32_t toX,
                                          std::int32_t toY) {
    const std::int32_t fromX = session.body().tileX();
    const std::int32_t fromY = session.body().tileY();
    if (fromX == toX && fromY == toY) {
        return "HERE";
    }
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    const std::int32_t ax = std::abs(dx);
    const std::int32_t ay = std::abs(dy);
    const std::int32_t tiles = ax > ay ? ax : ay;
    return std::string(compass_point(bearingTo(fromX, fromY, toX, toY))) + " " +
           std::to_string(tiles) + "T";
}

}  // namespace

TEST_CASE("M opens the district map, and M closes it") {
    render::Session session = standing();

    session.toggleMap();
    CHECK(session.mapOpen());

    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    CHECK(view.speaker == "THE CHART");
    CHECK_FALSE(view.epithet.empty());
    CHECK_FALSE(view.line.empty());
    CHECK_FALSE(view.topics.empty());

    session.toggleMap();
    CHECK_FALSE(session.mapOpen());
    CHECK_FALSE(session.dialogueView().open);
}

TEST_CASE("the district map's epithet is where the player is actually standing") {
    // THE ONE FIELD THIS PAGE OWES A MAP: a "you are here". Reuses
    // Session::placeLabel() rather than inventing a second opinion about
    // where the body is -- the HUD's own compass ribbon and this page must
    // never be able to disagree about that.
    render::Session session = standing();
    session.toggleMap();
    CHECK(session.dialogueView().epithet == session.placeLabel());
}

TEST_CASE("a man who arrived this morning has heard of exactly one lead, and the map says so") {
    // ONE LEAD STARTS OPEN -- test_casebook.cpp's own RAWS case proves that of
    // casebook.json directly. A fresh arrival's map is therefore exactly three
    // rows: the one place that lead names, the lead itself with a bearing, and
    // whoever it points at, PROVIDED that person answers to a faction.
    render::Session session = standing();
    const std::int32_t start = raws().indexOf("mission-backroom");
    REQUIRE(start >= 0);
    const Lead& lead = raws().leads()[static_cast<std::size_t>(start)];
    REQUIRE(lead.start);

    session.toggleMap();
    const std::vector<std::string> rows = session.mapRows();
    REQUIRE(rows.size() == 3);

    // KNOWN GROUND: the one place a fresh arrival has been pointed at.
    CHECK(rows[0] == "- " + lead.place);

    // OPEN LEADS: a bearing and a range from HERE, then the short name --
    // the whole reason this page exists over the casebook's own list.
    const std::string expectedLead =
        expectedBearing(session, lead.site.x, lead.site.y) + "  " + lead.brief;
    CHECK(rows[1] == expectedLead);

    // WHO WILL TALK: Onna, of the Temple -- see the header on
    // notableJobFamily's own table for why disciple_of_the_flame maps to
    // "clergy" and clergy maps to the temple. THE NAME'S OWN CASE, straight
    // out of notables.json ("Onna") and not upper-cased here: the 4x6 font
    // draws lowercase as uppercase glyphs (hud.cpp), so the STRING keeps
    // whatever case the raws authored it in, same as every speaker name a
    // conversation ever prints.
    CHECK(rows[2] == "! Onna  TEMPLE");
}

TEST_CASE("hearing a second lead adds its own place, its own bearing, and does not touch the first") {
    // THE MAP GROWS BY THE IDENTICAL ACT THE CASEBOOK DOES: hearing a lead.
    // No second "discovered" flag exists to drift from the trail -- see
    // Session::mapRows' own header.
    render::Session session = standing();
    session.toggleMap();
    const std::vector<std::string> before = session.mapRows();
    REQUIRE(before.size() == 3);

    const std::int32_t weighhouse = raws().indexOf("weighhouse-ledger");
    REQUIRE(weighhouse >= 0);
    REQUIRE(session.casebook().hear(weighhouse));

    const std::vector<std::string> after = session.mapRows();
    // ONE MORE PLACE, ONE MORE LEAD, ONE MORE CONTACT (crell, a shopkeeper --
    // merchants). Six rows exactly, and every row `before` printed is still
    // in there somewhere -- NOT necessarily at the same index, because the
    // new place joins the KNOWN GROUND group ahead of the lead rows that
    // follow it (see mapRows' own three-loop shape), so a new place shifts
    // everything after it down by one rather than landing at the tail.
    REQUIRE(after.size() == 6);
    for (const std::string& row : before) {
        INFO("row: ", row);
        CHECK(std::find(after.begin(), after.end(), row) != after.end());
    }

    const Lead& ledger = raws().leads()[static_cast<std::size_t>(weighhouse)];
    CHECK(std::find(after.begin(), after.end(), "- " + ledger.place) != after.end());
    const std::string expectedLead =
        expectedBearing(session, ledger.site.x, ledger.site.y) + "  " + ledger.brief;
    CHECK(std::find(after.begin(), after.end(), expectedLead) != after.end());
    CHECK(std::find(after.begin(), after.end(), "! Ottavan Crell  MERCHANTS") != after.end());
}

TEST_CASE("reading a lead moves it off the open-leads section without erasing known ground") {
    // A FOLLOWED OR COLD LEAD IS STILL SOMEWHERE YOU HAVE BEEN, so its place
    // stays; it is no longer something waiting on a look, so its bearing row
    // goes. examine() is the one verb this build has for reading a lead --
    // see Session::examine's own header -- and the trail's own start site
    // (docks.hpp's LeadSite -> world tile rule) is where it is read from.
    const std::int32_t start = raws().indexOf("mission-backroom");
    REQUIRE(start >= 0);
    const Lead& lead = raws().leads()[static_cast<std::size_t>(start)];

    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.spawnX = lead.site.x;
    config.spawnY = lead.site.y;
    config.spawnBand = lead.site.band;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);
    REQUIRE_FALSE(session.casebookOpen());

    session.examine();
    REQUIRE(session.casebook().state(start) != LeadState::Open);

    session.toggleMap();
    const std::vector<std::string> rows = session.mapRows();
    // Still known ground.
    CHECK(std::find(rows.begin(), rows.end(), "- " + lead.place) != rows.end());
    // No longer an open lead: nothing on the page starts with a bearing to
    // a body standing on the exact site it names ("HERE ...") for a row
    // carrying this lead's own short name.
    CHECK(std::find(rows.begin(), rows.end(), "HERE  " + lead.brief) == rows.end());
}

TEST_CASE("a printed number moves the cursor on the district map and does nothing else") {
    // A ROW HERE IS SOMETHING TO READ, NOT A CHOICE -- the identical no-op
    // the keys page and the character sheet give a number press: there is
    // nothing behind a bearing to choose.
    render::Session session = standing();
    session.toggleMap();
    REQUIRE(session.mapOpen());
    REQUIRE(session.mapRows().size() == 3);

    session.chooseVisibleTopic(1);
    CHECK(session.mapOpen());
    CHECK(session.dialogueView().cursor == 1);

    const int before = session.dialogueView().cursor;
    session.chooseTopic(static_cast<std::size_t>(before));
    CHECK(session.mapOpen());
    CHECK(session.dialogueView().cursor == before);
}

TEST_CASE("the cursor wraps the district map the same way it wraps every other list here") {
    render::Session session = standing();
    session.toggleMap();
    REQUIRE(session.mapRows().size() == 3);

    render::DialogueViewState view = session.dialogueView();
    CHECK(view.page == 0);
    CHECK(view.cursor == 0);

    session.moveTopicCursor(-1);
    view = session.dialogueView();
    CHECK(view.cursor == 2);

    session.moveTopicCursor(1);
    view = session.dialogueView();
    CHECK(view.cursor == 0);
}

TEST_CASE("the district map is exclusive with every other overlay page -- both ways") {
    render::Session session = standing();

    session.toggleMap();
    REQUIRE(session.mapOpen());

    session.toggleCasebook();
    CHECK(session.casebookOpen());
    CHECK_FALSE(session.mapOpen());

    session.toggleMap();
    CHECK(session.mapOpen());
    CHECK_FALSE(session.casebookOpen());

    session.toggleKeys();
    CHECK(session.keysOpen());
    CHECK_FALSE(session.mapOpen());

    session.toggleMap();
    CHECK(session.mapOpen());
    CHECK_FALSE(session.keysOpen());

    session.toggleCharacter();
    CHECK(session.characterOpen());
    CHECK_FALSE(session.mapOpen());

    session.toggleMap();
    CHECK(session.mapOpen());
    CHECK_FALSE(session.characterOpen());

    session.toggleOptions();
    CHECK(session.optionsOpen());
    CHECK_FALSE(session.mapOpen());

    session.toggleMap();
    CHECK(session.mapOpen());
    CHECK_FALSE(session.optionsOpen());

    session.togglePause();
    CHECK(session.pauseOpen());
    CHECK_FALSE(session.mapOpen());

    session.toggleMap();
    CHECK(session.mapOpen());
    CHECK_FALSE(session.pauseOpen());
}

TEST_CASE("the district map never opens over a conversation or a pick in progress") {
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

    session.toggleMap();
    CHECK_FALSE(session.mapOpen());
    CHECK(session.talking());
}

TEST_CASE("the district map never fights over the middle of the screen") {
    // THE SAME MEASUREMENT test_character.cpp, test_casebook.cpp and
    // test_pause.cpp already run for their own pages: draw the frame with the
    // map up and with it down, and require the exclusion rectangle to be
    // pixel-identical.
    render::SessionConfig config = fresh();
    render::Session session(config);
    session.stepMany(MoveInput{}, 4);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());

    render::Framebuffer plain(config.width, config.height);
    session.drawFrame(plain);

    session.toggleMap();
    REQUIRE(session.mapOpen());
    render::Framebuffer withMap(config.width, config.height);
    session.drawFrame(withMap);

    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withMap.pixels()[withMap.index(x, y)] == plain.pixels()[plain.index(x, y)]);
        }
    }
}

TEST_CASE("every word the district map can show is a sentence, not a diagnostic") {
    render::Session session = standing();
    const std::int32_t weighhouse = raws().indexOf("weighhouse-ledger");
    REQUIRE(weighhouse >= 0);
    REQUIRE(session.casebook().hear(weighhouse));
    session.toggleMap();
    REQUIRE(session.mapOpen());

    const auto mustRead = [](const std::string& text) {
        INFO("text: ", text);
        for (const char c : text) {
            CHECK(render::isDrawableGlyph(c));
        }
        CHECK(text.find('_') == std::string::npos);
    };

    for (const std::string& row : session.mapRows()) {
        mustRead(row);
    }
    const render::DialogueViewState view = session.dialogueView();
    mustRead(view.speaker);
    mustRead(view.epithet);
    mustRead(view.line);
}

TEST_CASE("--map opens the district map before the shutter, headless") {
    // THE SAME CALL M MAKES, without a window -- see SmokeRunConfig::map's
    // own header on why this exists at all: without it the page could be
    // unit-tested for its rows and never actually looked at.
    render::SmokeRunConfig config;
    config.session = fresh();
    config.map = true;
    const render::SmokeRunResult result = render::runSmoke(config);
    CHECK(result.ok);
    CHECK(result.scriptedWanted == 1);
    CHECK(result.scriptedLanded == 1);
    CHECK_FALSE(result.scriptFellShort());
}
