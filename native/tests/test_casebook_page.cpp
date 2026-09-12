// THE CASEBOOK PASS -- the book as the master/detail frame, the moment leads
// open, and the route from a lead to where it actually is.
//
// THE DEFECT THESE CASES EXIST FOR is not a crash and not a wrong number. The
// owner followed the Bloodletter to Crell at the Weighhouse and it "seemed to
// stop there"; `weighhouse-ledger` opens three leads and the simulation opened
// all three, correctly, and the only thing on the frame that said so was a dim
// grey corner row changing from CASE 4/6 to CASE 4/9. That is a UI defect with
// no failing assertion anywhere in the suite, which is exactly the kind this
// file is here to make impossible to reintroduce.
//
// WHAT A HEADLESS CASE CAN PROVE OF IT:
//
//   * the notice fires on the RISING EDGE of a look that opened something,
//     says how many, and does not fire again for standing there;
//   * every authored lead resolves to a real named place on the ward map, so
//     "show me where" can never be a key that does nothing;
//   * the route hands the MAP's own cursor the answer rather than inventing a
//     second one;
//   * the page never READS a lead by being drawn -- the one bug on this
//     surface that could quietly finish somebody's investigation for them;
//   * state changes the row, the label and the verb TOGETHER;
//   * the composition holds its geometry as the cursor moves, and holds every
//     row whole at every window size the game runs at.
//
// What a case cannot prove -- legibility -- is the mandatory screenshot's job.
// docs/frames/casebook/.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/casebook_page.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/casebook.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

/// The authored trail, loaded once.
const sim::CasebookRaws& raws() {
    static sim::CasebookRaws file = sim::CasebookRaws::load(content::contentDir());
    return file;
}

/// A session standing on a named lead's own site, with that lead already in the
/// book -- which is the state a player is in when they arrive somewhere they
/// were told to go. `hear` is the same call a clue's `opens` list makes.
[[nodiscard]] SessionConfig configAt(const char* leadId) {
    const std::int32_t at = raws().indexOf(leadId);
    REQUIRE(at >= 0);
    const sim::LeadSite site = raws().leads()[static_cast<std::size_t>(at)].site;
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.spawnX = site.x;
    config.spawnY = site.y;
    config.spawnBand = site.band;
    return config;
}

[[nodiscard]] std::string shout(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

}  // namespace

// ===========================================================================
// 1. THE MOMENT
// ===========================================================================

TEST_CASE("three leads open at the Weighhouse and the frame says so, once") {
    const std::int32_t ledger = raws().indexOf("weighhouse-ledger");
    REQUIRE(ledger >= 0);
    // THE CASE IS ABOUT THIS EXACT ROW OF THE RAWS. If the authored trail ever
    // stops opening three leads here, this case should go red and be reread --
    // not quietly pass on a smaller number.
    REQUIRE(raws().leads()[static_cast<std::size_t>(ledger)].opens.size() == 3);

    Session session(configAt("weighhouse-ledger"));
    session.stepMany(sim::MoveInput{}, 2);
    // Heard but not walked to: exactly the state Crell's ledger is in when a
    // player arrives, and the gate the whole investigation runs on.
    REQUIRE(session.casebook().hear(ledger));
    CHECK_FALSE(session.casePlateWanted());

    const std::int32_t before = session.casebook().readCount();
    session.examine();
    REQUIRE(session.casebook().readCount() == before + 1);

    // THE NOTICE. It names the count and the key, and it is not the corner row.
    // UI-EA (LANE HUD): the notice grammar is the spec's own "3 NEW LEADS - J"
    // -- news, dash, keycap. "YOUR CASEBOOK" died in the diet: the key IS the
    // pointer.
    CHECK(session.casePlateWanted());
    const std::string plate(session.casePlateLabel());
    INFO("plate: ", plate);
    CHECK(plate.find("3 NEW LEADS - ") == 0);
    CHECK(plate.find("CASEBOOK") == std::string::npos);
    // The key it names is the REAL bound key for the Menu action, not a letter
    // this file typed in.
    const std::string menuKey(
        keyName(session.controls().primary[static_cast<std::size_t>(Action::Menu)]));
    CHECK(plate == "3 NEW LEADS - " + menuKey);

    // AND IT GOES. Three seconds of steps and the notice is down.
    session.stepMany(sim::MoveInput{}, 200);
    CHECK_FALSE(session.casePlateWanted());

    // AND IT DOES NOT COME BACK FOR STANDING THERE. A second look at a site
    // already read opens nothing new, so there is no edge to fire on -- which
    // is what keeps this a notice rather than a nag.
    session.examine();
    CHECK_FALSE(session.casePlateWanted());
}

TEST_CASE("the lead-opened plate names the key in the hand holding the machine") {
    // THE PLATE GOES THROUGH promptLabel NOW, not keyName(primary[Menu]) --
    // the last of the plate-class literals. A pad player who reads a ledger
    // is told the pad's own way into the book (D-PAD UP), not the keyboard's
    // J; a keyboard player still reads J.
    const std::int32_t ledger = raws().indexOf("weighhouse-ledger");
    REQUIRE(ledger >= 0);

    Session session(configAt("weighhouse-ledger"));
    session.stepMany(sim::MoveInput{}, 2);
    REQUIRE(session.casebook().hear(ledger));
    session.noteInputDevice(InputDevice::Pad);
    session.examine();
    REQUIRE(session.casePlateWanted());
    const std::string plate(session.casePlateLabel());
    INFO("plate: ", plate);
    // UI-EA: LANE HUD's dieted notice grammar carrying LANE FLOW's motif key
    // -- the pad's D-PAD UP prints as the cross+up sentinels (sec. 5), so the
    // whole plate is the news, a dash, and two glyphs that count zero words.
    CHECK(plate == "3 NEW LEADS - \x06\x02");
    CHECK(plate.find("YOUR CASEBOOK") == std::string::npos);
}

TEST_CASE("a lead that opens nothing new announces nothing") {
    // The Outfall is a DEAD END by authorship: the grate is corroded shut from
    // outside and it opens no lead at all. A notice there would be the frame
    // celebrating a wasted walk.
    const std::int32_t outfall = raws().indexOf("the-outfall");
    REQUIRE(outfall >= 0);
    REQUIRE(raws().leads()[static_cast<std::size_t>(outfall)].opens.empty());

    Session session(configAt("the-outfall"));
    session.stepMany(sim::MoveInput{}, 2);
    REQUIRE(session.casebook().hear(outfall));
    session.examine();
    CHECK(session.casebook().state(outfall) == sim::LeadState::Cold);
    CHECK_FALSE(session.casePlateWanted());
}

// ===========================================================================
// 2. THE ROUTE
// ===========================================================================

TEST_CASE("every authored lead resolves to a named place on the ward map") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    Session session(config);

    const std::vector<sim::Lead>& leads = raws().leads();
    // NOT VACUOUS: the trail is twelve leads and this case is worthless if the
    // file ever fails to load.
    REQUIRE(leads.size() >= 12);
    for (std::size_t i = 0; i < leads.size(); ++i) {
        const int place = session.mapPlaceForLead(static_cast<std::int32_t>(i));
        INFO("lead: ", leads[i].id, " place: ", leads[i].place);
        // A lead the map cannot point at is a lead the player cannot be sent
        // to, which is the whole of what this pass adds.
        REQUIRE(place >= 0);
        // AND IT IS THE RIGHT ONE. casebook.json shouts its place and the sign
        // table spells it as a proper noun; the two must still be the same
        // building after a rename on either side.
        CHECK(shout(mapPlaces()[static_cast<std::size_t>(place)].name) == shout(leads[i].place));
    }
}

TEST_CASE("the route hands the ward map its own cursor and its own view") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    // Read the start lead so the book holds more than one row, then point the
    // page at one of the three it opened.
    session.examine();
    session.toggleCasebook();
    REQUIRE(session.casebookPageOpen());
    REQUIRE(session.selectCasebookLead("weighhouse-ledger"));
    // Put the map somewhere else first, so "the route moved it" is a real
    // claim rather than a coincidence.
    session.setDistrictMapTab(3);

    const std::int32_t ledger = raws().indexOf("weighhouse-ledger");
    const int want = session.mapPlaceForLead(ledger);
    REQUIRE(want >= 0);
    session.commitCasebookLead();

    CHECK_FALSE(session.casebookOpen());
    CHECK(session.districtMapOpen());
    CHECK(session.districtMapSelected() == want);
    // OVERVIEW, whatever the map was left on: the question this route asks is
    // "where is it", and Overview is the view that answers it.
    CHECK(session.districtMapTab() == MapTab::Overview);
}

TEST_CASE("standing on an unread lead, the commit verb looks instead of routing") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    const std::int32_t mission = raws().indexOf("mission-backroom");
    REQUIRE(session.casebook().state(mission) == sim::LeadState::Open);

    session.toggleCasebook();
    REQUIRE(session.casebookPageOpen());
    REQUIRE(session.selectCasebookLead("mission-backroom"));
    // The page has already said which verb this is going to be.
    const CasebookPageState page = session.casebookPageState();
    REQUIRE(page.cursor >= 0);
    REQUIRE(page.cursor < static_cast<int>(page.rows.size()));
    CHECK(page.rows[static_cast<std::size_t>(page.cursor)].here);

    const std::int32_t before = session.casebook().readCount();
    session.commitCasebookLead();
    // IT LOOKED. The map is not the answer to "go to where you already are".
    CHECK(session.casebook().readCount() == before + 1);
    CHECK_FALSE(session.districtMapOpen());
    CHECK_FALSE(session.casebookOpen());
}

// ===========================================================================
// 3. THE PAGE
// ===========================================================================

TEST_CASE("state changes the row, the label and the verb together") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();  // the start lead: Followed, and it opened three

    const CasebookPageState page = session.casebookPageState();
    REQUIRE(page.rows.size() >= 4);

    int followed = 0;
    int open = 0;
    for (const CasebookLeadRow& row : page.rows) {
        if (row.state == CasebookLeadState::Followed) {
            ++followed;
            // WHAT IT OPENED, BY NAME. This is the row the owner never saw.
            CHECK_FALSE(row.opened.empty());
            CHECK_FALSE(row.found.empty());
        } else if (row.state == CasebookLeadState::Open) {
            ++open;
            // AND AN OPEN LEAD DOES NOT PRINT ITS OWN CLUE. A book that showed
            // `found` for a lead nobody has stood over would hand the answer
            // over for having been TOLD the lead exists, which turns an
            // investigation into a reading exercise.
            CHECK(row.found.empty());
            CHECK(row.detail.empty());
            CHECK(row.opened.empty());
        }
        // Every row is routable and every row is dated.
        CHECK(row.routable);
        CHECK_FALSE(row.heard.empty());
        CHECK_FALSE(row.place.empty());
    }
    CHECK(followed == 1);
    CHECK(open == 3);

    // The start lead has no opener and the three it opened all name it.
    for (const CasebookLeadRow& row : page.rows) {
        if (row.state == CasebookLeadState::Followed) {
            CHECK(row.from.empty());
        } else {
            CHECK_FALSE(row.from.empty());
        }
    }
}

TEST_CASE("the casebook page never reads a lead by being drawn") {
    // THE ONE BUG ON THIS SURFACE THAT COULD QUIETLY FINISH SOMEBODY'S
    // INVESTIGATION. Casebook::look() opens what a lead opens, stamps its
    // dateline and moves the ward's dread; a page that reached for it while
    // building a detail pane would walk the trail for the player by being
    // looked at. Nothing in casebook_page.cpp calls it, and this is that
    // claim with an assertion behind it.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    session.toggleCasebook();
    REQUIRE(session.casebookPageOpen());

    const std::int32_t read = session.casebook().readCount();
    const std::int32_t dread = session.casebook().dread();
    const std::vector<std::int32_t> known = session.casebook().known();

    Framebuffer target(960, 540);
    for (int i = 0; i < 24; ++i) {
        // Every row, on both views, drawn over and over.
        session.moveCasebookCursor(1);
        session.cycleCasebookTab(1);
        const CasebookPageState page = session.casebookPageState();
        drawCasebookPage(target, page);
    }

    CHECK(session.casebook().readCount() == read);
    CHECK(session.casebook().dread() == dread);
    CHECK(session.casebook().known() == known);
}

TEST_CASE("the page holds its geometry while the cursor walks the book") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 16);
    REQUIRE(session.casebookPageOpen());
    REQUIRE(session.casebookPageState().rows.size() >= 4);

    const int width = 960;
    const int height = 540;
    Framebuffer first(width, height);
    session.setCasebookCursor(0);
    drawCasebookPage(first, session.casebookPageState());
    Framebuffer later(width, height);
    session.setCasebookCursor(3);
    drawCasebookPage(later, session.casebookPageState());

    // THE FRAME ITSELF DOES NOT MOVE. The last two rows of the composition are
    // the rule and the global nav band, and the left border column runs the
    // whole height; none of them has anything to do with which lead is picked,
    // and a list whose furniture shifts as you arrow through it feels broken.
    const PanelMetric metric = panelMetric(height);
    const int rows = metric.rowsIn(height);
    const int cells = metric.cellsIn(width);
    const int originX = (width - metric.widthOf(cells)) / 2;
    const int originY = (height - metric.heightOf(rows)) / 2;
    std::size_t moved = 0;
    for (int y = originY + metric.heightOf(rows - 3); y < originY + metric.heightOf(rows); ++y) {
        for (int x = originX; x < originX + metric.widthOf(cells); ++x) {
            if (first.pixels()[first.index(x, y)] != later.pixels()[later.index(x, y)]) {
                ++moved;
            }
        }
    }
    CHECK(moved == 0);
    for (int y = originY; y < originY + metric.heightOf(rows); ++y) {
        for (int x = originX; x < originX + metric.cellW(); ++x) {
            if (first.pixels()[first.index(x, y)] != later.pixels()[later.index(x, y)]) {
                ++moved;
            }
        }
    }
    CHECK(moved == 0);

    // ...AND THE PAGE DID ACTUALLY CHANGE, so the check above is not passing
    // because nothing was drawn.
    std::size_t differing = 0;
    for (std::size_t i = 0; i < first.pixels().size(); ++i) {
        if (first.pixels()[i] != later.pixels()[i]) {
            ++differing;
        }
    }
    CHECK(differing > 500);
}

TEST_CASE("no row of the book is clipped at any window the game runs at") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    // Walk the whole trail into the book so the list is at its LONGEST and its
    // widest -- the state a real player reaches, and the one a fixed master
    // share has to survive.
    for (std::size_t i = 0; i < raws().leads().size(); ++i) {
        (void)session.casebook().hear(static_cast<std::int32_t>(i));
    }
    const CasebookPageState page = session.casebookPageState();
    REQUIRE(page.rows.size() == raws().leads().size());

    std::size_t widestLabel = 0;
    std::size_t widestValue = 0;
    for (const CasebookLeadRow& row : page.rows) {
        widestLabel = std::max(widestLabel, row.brief.size());
        // What the value column actually carries for an Open lead: the place,
        // with a leading article dropped -- map_view's own shortening rule.
        std::string place = row.place;
        if (place.size() > 4 && place.compare(0, 4, "THE ") == 0) {
            place = place.substr(4);
        }
        widestValue = std::max(widestValue, place.size());
    }
    // NOT VACUOUS.
    REQUIRE(widestLabel >= 8);
    REQUIRE(widestValue >= 15);

    const int sizes[][2] = {{320, 180}, {640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};
    for (const auto& size : sizes) {
        const CasebookPageMetrics geo = casebookPageMetrics(page, size[0], size[1]);
        INFO("at ", size[0], "x", size[1], " master ", geo.masterCells, " label ", geo.labelCells,
             " value ", geo.valueCells);
        REQUIRE(geo.usable);
        // ONE COLUMN, ALWAYS: a case's leads are one sequence and the eye runs
        // down them.
        CHECK(geo.columns == 1);
        // The key column, one cell of gap, and the widest label and value both
        // whole. THE BIGGEST WINDOW IS THE NARROWEST IN CELLS (hudMinorScale
        // steps up with height), so 1920x1080 is the size this can fail at.
        CHECK(geo.labelCells >= static_cast<int>(widestLabel));
        CHECK(geo.valueCells >= static_cast<int>(widestValue));
        // THE CLAIM MOVED WITH THE DIET (UI-EA-SPEC 1.5 #27): the book PAGES
        // at eight rows + `+N` now, so a walked case's list legitimately
        // turns a page -- what holds at every window is the eight-row law
        // itself, not one screenful.
        CasebookPageState paged = page;
        const CasebookPageScroll scroll = casebookPageScroll(paged, size[0], size[1]);
        const int count = static_cast<int>(page.rows.size());
        CHECK(scroll.screens == std::max(1, (count + scroll.perScreen - 1) / scroll.perScreen));
        CHECK(scroll.perScreen <= 8);
    }
}

TEST_CASE("the master/detail split collapses honestly rather than shrinking to nothing") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    const CasebookPageState page = session.casebookPageState();
    // The detail pane is the whole point of this page, so it is present at
    // every size the owner actually plays at...
    for (const int height : {360, 540, 720, 1080}) {
        const CasebookPageMetrics geo =
            casebookPageMetrics(page, height * 16 / 9, height);
        INFO("height ", height);
        CHECK(geo.split);
        CHECK(geo.detailCells >= 30);
    }
    // ...and at 320x180 it gives way to one readable pane rather than two
    // unreadable ones, which is the honest answer and not a bug.
    const CasebookPageMetrics small = casebookPageMetrics(page, 320, 180);
    REQUIRE(small.usable);
    CHECK_FALSE(small.split);
}

TEST_CASE("the mouse hit-test agrees with what the list drew") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    const CasebookPageState page = session.casebookPageState();
    REQUIRE(page.rows.size() >= 4);

    const CasebookPageMetrics geo = casebookPageMetrics(page, 960, 540);
    REQUIRE(geo.usable);
    for (int row = 0; row < static_cast<int>(page.rows.size()); ++row) {
        const int px = geo.master.x + geo.metric.cellW() / 2;
        const int py = geo.master.y + row * geo.metric.cellH() + geo.metric.cellH() / 2;
        INFO("row ", row, " at (", px, ",", py, ")");
        CHECK(casebookLeadAtPixel(page, 960, 540, px, py) == row);
    }
    // Off the list entirely is -1, not a clamp: a click on the frame is not a
    // click on the last row.
    CHECK(casebookLeadAtPixel(page, 960, 540, 2, 2) == -1);
}

TEST_CASE("the printed digits stop at nine and the cursor still reaches every row") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    for (std::size_t i = 0; i < raws().leads().size(); ++i) {
        (void)session.casebook().hear(static_cast<std::int32_t>(i));
    }
    const int count = static_cast<int>(session.casebook().known().size());
    REQUIRE(count > 9);  // twelve leads against nine digits: the case for this rule

    // The digits reach the first nine.
    session.setCasebookCursor(8);
    CHECK(session.casebookLeadCursor() == 8);
    // Out of range does nothing rather than clamping to somewhere arbitrary.
    session.setCasebookCursor(count + 4);
    CHECK(session.casebookLeadCursor() == 8);
    // And the cursor wraps the whole book, which is how the last three are
    // reached at all.
    for (int i = 0; i < count; ++i) {
        session.moveCasebookCursor(1);
    }
    CHECK(session.casebookLeadCursor() == 8);
    session.moveCasebookCursor(-9);
    CHECK(session.casebookLeadCursor() == (8 - 9 + count) % count);
}

TEST_CASE("the two views swap the detail pane and nothing else") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 16);
    REQUIRE(session.casebookTab() == CasebookTab::Leads);

    Framebuffer leads(960, 540);
    drawCasebookPage(leads, session.casebookPageState());
    session.cycleCasebookTab(1);
    CHECK(session.casebookTab() == CasebookTab::Case);
    Framebuffer theCase(960, 540);
    drawCasebookPage(theCase, session.casebookPageState());
    // THE PULL PACK: a third view, the CASES shelf, and then the wrap.
    session.cycleCasebookTab(1);
    CHECK(session.casebookTab() == CasebookTab::Cases);
    Framebuffer shelf(960, 540);
    drawCasebookPage(shelf, session.casebookPageState());
    session.cycleCasebookTab(1);
    CHECK(session.casebookTab() == CasebookTab::Leads);

    const CasebookPageMetrics geo = casebookPageMetrics(session.casebookPageState(), 960, 540);
    REQUIRE(geo.split);
    // THE MASTER LIST IS BIT-IDENTICAL ACROSS THE TAB. A tab that also moved
    // the list would be two compositions wearing one frame.
    std::size_t movedInList = 0;
    for (int y = geo.master.y; y < geo.master.y + geo.master.h; ++y) {
        for (int x = geo.master.x; x < geo.master.x + geo.master.w; ++x) {
            if (leads.pixels()[leads.index(x, y)] != theCase.pixels()[theCase.index(x, y)]) {
                ++movedInList;
            }
        }
    }
    CHECK(movedInList == 0);
    // And the detail pane genuinely changed.
    std::size_t movedInDetail = 0;
    for (int y = geo.detail.y; y < geo.detail.y + geo.detail.h; ++y) {
        for (int x = geo.detail.x; x < geo.detail.x + geo.detail.w; ++x) {
            if (leads.pixels()[leads.index(x, y)] != theCase.pixels()[theCase.index(x, y)]) {
                ++movedInDetail;
            }
        }
    }
    CHECK(movedInDetail > 200);
    // THE SHELF KEEPS THE SAME LIST TOO -- the same rows in the same cells
    // -- but with NO ROW FILLED: on the shelf its own highlight is the one
    // armed cursor, so the only pixels that move in the list are the
    // highlighted row's own fill (the first row: the cursor is on it).
    std::size_t shelfMovedOutsideCursorRow = 0;
    std::size_t shelfMovedInCursorRow = 0;
    std::size_t shelfMovedInDetail = 0;
    // The fill carries a hairline of its accent one scaled pixel above and
    // below the row (drawInvertedFill's own eye candy), so the band is the
    // row plus that hairline each side.
    const int cursorRowY0 = geo.master.y - geo.metric.scale;
    const int cursorRowY1 = geo.master.y + geo.metric.cellH() + geo.metric.scale;
    for (int y = geo.master.y; y < geo.master.y + geo.master.h; ++y) {
        for (int x = geo.master.x; x < geo.master.x + geo.master.w; ++x) {
            if (leads.pixels()[leads.index(x, y)] != shelf.pixels()[shelf.index(x, y)]) {
                if (y >= cursorRowY0 && y < cursorRowY1) {
                    ++shelfMovedInCursorRow;
                } else {
                    ++shelfMovedOutsideCursorRow;
                }
            }
        }
    }
    for (int y = geo.detail.y; y < geo.detail.y + geo.detail.h; ++y) {
        for (int x = geo.detail.x; x < geo.detail.x + geo.detail.w; ++x) {
            if (leads.pixels()[leads.index(x, y)] != shelf.pixels()[shelf.index(x, y)]) {
                ++shelfMovedInDetail;
            }
        }
    }
    CHECK(shelfMovedOutsideCursorRow == 0);
    CHECK(shelfMovedInCursorRow > 0);
    CHECK(shelfMovedInDetail > 200);
}

TEST_CASE("the composed casebook is what the Menu draws on its Journal tile") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 960;
    config.height = 540;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 2);

    CHECK_FALSE(session.casebookPageOpen());
    session.toggleCasebook();
    CHECK(session.casebookPageOpen());
    // The three siblings are still tiles, and the page stands down for them --
    // which is the seam this pass leaves and does not hide.
    session.toggleCharacter();
    CHECK(session.casebookOpen());
    CHECK_FALSE(session.casebookPageOpen());
    session.toggleCasebook();
    CHECK(session.casebookPageOpen());

    // AND THE PAGE ACTUALLY DRAWS, and drawing it moves no body.
    session.stepMany(sim::MoveInput{}, 16);
    const std::int32_t bodyX = session.body().x();
    Framebuffer withBook(config.width, config.height);
    (void)session.drawFrame(withBook);
    CHECK(session.body().x() == bodyX);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    Framebuffer without(config.width, config.height);
    (void)session.drawFrame(without);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < withBook.pixels().size(); ++i) {
        if (withBook.pixels()[i] != without.pixels()[i]) {
            ++differing;
        }
    }
    // A full-frame composition: most of the screen.
    CHECK(differing > withBook.pixels().size() / 2);
}

TEST_CASE("the lead-opened notice keeps the middle of the screen clear, and outranks a crossing") {
    // ONE ANNOUNCEMENT SLOT, AND THE CASE WINS IT. Both notices are edges said
    // once in the same band under the compass ribbon; two of them stacked would
    // be two notices fighting, so hud.cpp enforces the ordering rather than
    // trusting Session to have already guaranteed it.
    for (const int height : {180, 540, 1080}) {
        const int width = height * 16 / 9;
        HudState both;
        both.placePlate = "THE GILDED GULL - ROOMS";
        both.placePlateFade = 1.0F;
        both.placePlateDrift = -1.0F;
        both.casePlate = "3 NEW LEADS - J";
        both.casePlateFade = 1.0F;
        // AT THE LOWEST POINT ITS OWN DRIFT CAN PUT IT -- the one instant it
        // comes closest to the play space.
        both.casePlateDrift = -1.0F;

        HudState placeOnly = both;
        placeOnly.casePlate = {};
        placeOnly.casePlateFade = 0.0F;

        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer withCase(width, height);
        withCase.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(withCase, both);
        Framebuffer withoutCase(width, height);
        withoutCase.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(withoutCase, placeOnly);

        INFO("at ", width, "x", height);
        // THE CENTRE STAYS EMPTY. The HUD's founding rule, and the notice is a
        // centred element in a HUD whose whole rule is that the middle is the
        // play space.
        const CentreRect centre = hudCentreRect(width, height);
        std::size_t inThePlaySpace = 0;
        for (int y = centre.y0; y < centre.y1; ++y) {
            for (int x = centre.x0; x < centre.x1; ++x) {
                if (bare.pixels()[bare.index(x, y)] !=
                    withCase.pixels()[withCase.index(x, y)]) {
                    ++inThePlaySpace;
                }
            }
        }
        CHECK(inThePlaySpace == 0);

        // AND THE CASE PLATE IS THE ONE THAT GOT THE BAND.
        std::size_t differing = 0;
        for (std::size_t i = 0; i < withCase.pixels().size(); ++i) {
            if (withCase.pixels()[i] != withoutCase.pixels()[i]) {
                ++differing;
            }
        }
        CHECK(differing > 0);
    }
}

TEST_CASE("the nav band always names the key that closes the book") {
    // A FOOTER THAT QUIETLY STOPS NAMING THE WAY OUT is worse than a footer
    // that spends a second row, and this is a defect a screenshot found: the
    // nav list's widest key is "LEFT RIGHT" and drawOptionList sizes every
    // column off the widest entry, so four columns want eighty-six cells --
    // which 960x540 has and 1920x1080 does not, because hudMinorScale steps up
    // with height and THE BIGGEST WINDOW IS THE NARROWEST IN CELLS. The one-row
    // band dropped the fourth entry silently, and the fourth entry is CLOSE.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    CasebookPageState page = session.casebookPageState();
    REQUIRE_FALSE(page.closeKey.empty());

    for (const auto& size : {std::pair{320, 180}, std::pair{640, 360}, std::pair{960, 540},
                             std::pair{1280, 720}, std::pair{1920, 1080}}) {
        // ALL THREE VIEWS, because the second and third entries' labels
        // change with the tab and a wider label is a narrower column. FIVE
        // entries since the PULL PACK: LEAD, the next view, the commit,
        // FOLLOW, CLOSE.
        for (const CasebookTab tab : {CasebookTab::Leads, CasebookTab::Case, CasebookTab::Cases}) {
            page.tab = tab;
            const CasebookPageMetrics geo = casebookPageMetrics(page, size.first, size.second);
            INFO("at ", size.first, "x", size.second, " nav rows ", geo.navRows, " shown ",
                 geo.navShown, "/", geo.navEntries);
            REQUIRE(geo.usable);
            CHECK(geo.navEntries == 5);
            CHECK(geo.navShown == geo.navEntries);
        }
    }
}

TEST_CASE("a lead never silently reports fewer things than it opened") {
    // THIS PASS'S OWN DEFECT, ONE PANE DEEPER, AND A CAPTURE FOUND IT. At
    // 1920x1080 the detail pane holds fewer ROWS than at 960x540 -- hudMinorScale
    // steps up with height -- the clue and its paragraph wrapped to eight lines,
    // and drawProse stopped at the bottom of the rect: `OPENED THE LEDGER` was
    // simply not drawn. A lead that opened three things reporting two is exactly
    // what the owner's playthrough ran into, said quietly instead of not at all.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    CasebookPageState page = session.casebookPageState();
    // THE START LEAD OPENS THREE, so this case is about a block that genuinely
    // has something to lose.
    REQUIRE(page.rows.size() >= 4);
    REQUIRE(page.rows.front().opened.size() == 3);

    for (const auto& size : {std::pair{640, 360}, std::pair{960, 540}, std::pair{1280, 720},
                             std::pair{1920, 1080}}) {
        // EVERY LEAD IN THE BOOK, not just the interesting one: the Drowned Hold
        // is named by four separate leads and its own TOLD YOU BY wraps.
        for (int row = 0; row < static_cast<int>(page.rows.size()); ++row) {
            page.cursor = row;
            const CasebookPageMetrics geo = casebookPageMetrics(page, size.first, size.second);
            REQUIRE(geo.usable);
            REQUIRE(geo.split);
            INFO("at ", size.first, "x", size.second, " row ", row, " (",
                 page.rows[static_cast<std::size_t>(row)].brief, ") wanted ",
                 geo.effectRowsWanted, " got ", geo.effectRows);
            CHECK(geo.effectRowsWanted > 0);
            CHECK(geo.effectRows == geo.effectRowsWanted);
        }
    }
}

// ===========================================================================
// THE EMPTY STATES -- the blank rows are worded, and stop being worded the
// moment the words would be a lie
// ===========================================================================

TEST_CASE("the fresh book spends no words on its own remaining room") {
    // UI-EA-SPEC 1.5 #26, prose 14 -> 0: the fourteen-word waiting sentence is
    // RETIRED. The Law of Earned Text says the book's unfilled room is not
    // news -- the stipple already says "left on purpose" -- and the fresh
    // book's detail pane hands a stranger the case's own hook instead. So the
    // waiting book, the book with every lead already heard, and the walked-out
    // book draw the LEADS view byte-identically: `known`, `total` and `closed`
    // reach no drawing on this tab any more.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    CasebookPageState page = session.casebookPageState();
    page.tab = CasebookTab::Leads;
    // The page's ease is Session's shared panel toggle and this one was never
    // opened, so it is sitting at zero -- every draw below would be at alpha 0.
    page.openAmount = 1.0F;
    // A NEW GAME IS ONE LEAD. Not vacuous: if the raws ever start the player
    // with the whole book this case is measuring nothing.
    REQUIRE(page.rows.size() == 1);
    REQUIRE(page.known == 1);
    REQUIRE(page.total > page.known);
    REQUIRE_FALSE(page.closed);
    // AND THE FRESH DETAIL PANE IS THE CASE BRIEF: nothing has been read yet,
    // so the hook is what the pane spends its prose on.
    REQUIRE(page.read == 0);
    REQUIRE_FALSE(page.hook.empty());

    for (const auto& size : {std::pair{320, 180}, std::pair{640, 360}, std::pair{960, 540},
                             std::pair{1920, 1080}}) {
        Framebuffer waiting(size.first, size.second);
        drawCasebookPage(waiting, page);

        CasebookPageState full = page;
        full.known = full.total;
        Framebuffer quiet(size.first, size.second);
        drawCasebookPage(quiet, full);

        CasebookPageState shut = page;
        shut.closed = true;
        Framebuffer done(size.first, size.second);
        drawCasebookPage(done, shut);

        INFO("at ", size.first, "x", size.second);
        // ONE FRAME, THREE STATES: no sentence about room left, under any of
        // the three gates that used to word it.
        CHECK(quiet.pixels() == waiting.pixels());
        CHECK(done.pixels() == waiting.pixels());

        // AND THE HOOK IS REALLY THE THING THE FRESH PANE DRAWS: a fresh book
        // with a hook differs from the same book with the hook blanked --
        // the twenty-word case brief is load-bearing, not incidental.
        CasebookPageState hookless = page;
        hookless.hook.clear();
        Framebuffer bare(size.first, size.second);
        drawCasebookPage(bare, hookless);
        const CasebookPageMetrics geo = casebookPageMetrics(page, size.first, size.second);
        REQUIRE(geo.usable);
        if (geo.split) {
            CHECK(bare.pixels() != waiting.pixels());
        }
    }
}

TEST_CASE("the one-lead book and the twelve-lead book compose to the same geometry") {
    // THE SENTENCE IS DRAWN INTO ROOM THAT WAS ALREADY THERE. It is not allowed
    // to push the frame's foot down, move the nav band or change what the mouse
    // answers -- the detail half is HELD at kDetailHoldRows and the whole point
    // of that floor is that the book's own growth does not move the seat until
    // it outgrows the floor.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    CasebookPageState waiting = session.casebookPageState();
    waiting.tab = CasebookTab::Leads;
    CasebookPageState quiet = waiting;
    quiet.known = quiet.total;

    for (const auto& size : {std::pair{320, 180}, std::pair{640, 360}, std::pair{960, 540},
                             std::pair{1920, 1080}}) {
        const CasebookPageMetrics a = casebookPageMetrics(waiting, size.first, size.second);
        const CasebookPageMetrics b = casebookPageMetrics(quiet, size.first, size.second);
        INFO("at ", size.first, "x", size.second);
        REQUIRE(a.usable);
        CHECK(a.metric.cellH() == b.metric.cellH());
        CHECK(a.master.y == b.master.y);
        CHECK(a.master.h == b.master.h);
        CHECK(a.detail.h == b.detail.h);
        CHECK(a.navRows == b.navRows);
        CHECK(a.listRows == b.listRows);
        // And the mouse still answers for the one row there is.
        CHECK(casebookLeadAtPixel(waiting, size.first, size.second,
                                  a.master.x + a.metric.cellW() / 2,
                                  a.master.y + a.metric.cellH() / 2) == 0);
    }
}

// ===========================================================================
// THE MEASURE -- the book takes the width its widest row earns, not the window
// ===========================================================================

TEST_CASE("the book measures its width off its own rows, and nothing a cursor does moves it") {
    // SHIP NOTE MOVE 1. The book drew 639px wide at a 640px window whatever it
    // held -- 56.1% of the frame black around a full-width letterbox. It now
    // asks masterDetailCellsFor for the interior its own widest master row and
    // widest detail row (EITHER view, ANY lead) survive in, and panelSeatX
    // seats the remainder 45/55.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    for (std::size_t i = 0; i < raws().leads().size(); ++i) {
        (void)session.casebook().hear(static_cast<std::int32_t>(i));
    }
    CasebookPageState page = session.casebookPageState();
    const int count = static_cast<int>(page.rows.size());
    REQUIRE(count >= 12);

    const CasebookPageMetrics geo = casebookPageMetrics(page, 640, 360);
    REQUIRE(geo.usable);
    REQUIRE(geo.split);
    // The interior is master + 2 (a cell of air, the divider) + detail, plus
    // the two border cells -- splitMasterDetail's own arithmetic.
    const int panelCells = geo.masterCells + geo.detailCells + 4;
    // MEASURED, NOT THE WINDOW -- and never under the vocabulary's floor.
    CHECK(panelCells < geo.metric.cellsIn(640));
    CHECK(panelCells >= kPanelMeasureFloorCells);
    // SEATED 45/55 ACROSS: the master pane starts one border cell past the
    // seat the measured width earns.
    CHECK(geo.master.x ==
          panelSeatX(640, geo.metric.widthOf(panelCells)) + geo.metric.cellW());
    CHECK(geo.master.x > 0);
    // The detail half is held at least as wide as the split's own floor, so
    // the fact block and the commit line the measure walked stay whole.
    CHECK(geo.detailCells >= 30);

    // AND THE BORDER BELONGS TO THE BOOK, NOT TO THE CURSOR. Every lead, and
    // both views, compose to the identical panes -- the width measure is a
    // maximum over the whole book, the same discipline kDetailHoldRows keeps
    // for the height.
    for (int at = 0; at < count; ++at) {
        page.cursor = at;
        const CasebookPageMetrics again = casebookPageMetrics(page, 640, 360);
        INFO("cursor ", at);
        CHECK(again.master.x == geo.master.x);
        CHECK(again.master.w == geo.master.w);
        CHECK(again.detail.x == geo.detail.x);
        CHECK(again.detail.w == geo.detail.w);
    }
    page.tab = CasebookTab::Case;
    const CasebookPageMetrics caseView = casebookPageMetrics(page, 640, 360);
    CHECK(caseView.master.x == geo.master.x);
    CHECK(caseView.detail.w == geo.detail.w);

    // 320x180 is already narrower than anything the measure would choose: it
    // comes out exactly as it always did, one full-width pane, the honest
    // answer at a small window.
    const CasebookPageMetrics small = casebookPageMetrics(page, 320, 180);
    REQUIRE(small.usable);
    CHECK_FALSE(small.split);
    CHECK(small.metric.cellsIn(small.master.w) ==
          small.metric.cellsIn(320) - 2);
}

// ===========================================================================
// THE POINTER PASS -- the tab row, and the device-worded commit verbs
// ===========================================================================

TEST_CASE("the tab row answers the mouse: LEADS and THE CASE are findable, ordered, and steady") {
    // The ship note names this row by name: LEFT/RIGHT stepped the views and
    // clicking them did nothing. casebookTabAtPixel runs drawTabRow's own
    // placement walk through the page's own composition, so the case scans
    // the frame instead of hand-placing pixels: both tabs are found, on one
    // text row, above the master list, LEADS left of THE CASE -- and flipping
    // the current view does not move a single answer, because a tab target
    // that shifts as the selection changes is the drift the shared walk ends.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    CasebookPageState page = session.casebookPageState();
    const CasebookPageMetrics geo = casebookPageMetrics(page, 960, 540);
    REQUIRE(geo.usable);

    CasebookPageState onCase = page;
    onCase.tab = CasebookTab::Case;

    int leadsX = -1;
    int caseX = -1;
    int shelfX = -1;
    int rowY = -1;
    for (int py = 0; py < geo.master.y; py += 2) {
        for (int px = 0; px < 960; px += 2) {
            const int at = casebookTabAtPixel(page, 960, 540, px, py);
            CHECK(at == casebookTabAtPixel(onCase, 960, 540, px, py));
            if (at < 0) {
                continue;
            }
            if (rowY < 0) {
                rowY = py;
            }
            // One text row's worth of pixels and no more.
            CHECK(py - rowY < geo.metric.cellH());
            if (at == 0 && leadsX < 0) {
                leadsX = px;
            }
            if (at == 1 && caseX < 0) {
                caseX = px;
            }
            if (at == 2 && shelfX < 0) {
                shelfX = px;
            }
        }
    }
    REQUIRE(leadsX >= 0);
    REQUIRE(caseX >= 0);
    REQUIRE(shelfX >= 0);
    CHECK(leadsX < caseX);
    CHECK(caseX < shelfX);
    CHECK(rowY < geo.master.y);
    // The master list still answers as itself -- the tab row took nothing
    // from the lead hit-test.
    CHECK(casebookLeadAtPixel(page, 960, 540, geo.master.x + geo.metric.cellW() / 2,
                              geo.master.y + geo.metric.cellH() / 2) == 0);
}

TEST_CASE("the commit verbs and GO TO IT wear the state's confirm key, not a hardcoded ENTER") {
    // SHIP NOTE SEAM 3. The page draws its confirm from CasebookPageState::
    // commitKey -- promptConfirmKey's word for the hand holding the machine
    // -- so a pad reads A - SHOW ME WHERE. Proven the way the keys page's
    // live-switch cases prove wording: two frames differing only in the field
    // must differ in pixels, and the default draws byte-identical to the old
    // literal by construction (the default IS the old literal).
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    CasebookPageState page = session.casebookPageState();
    REQUIRE(page.commitKey == "\x01");  // keyboard session: the return motif, sec. 5

    // GATE FIX: the book was never toggled open, so the session's panel anim
    // reads 0 and both draws were pure black -- open the page by hand the way
    // the geometry cases at line ~717 already do.
    page.openAmount = 1.0F;
    Framebuffer keyboard(960, 540);
    drawCasebookPage(keyboard, page);
    page.commitKey = "A";
    Framebuffer pad(960, 540);
    drawCasebookPage(pad, page);
    CHECK(keyboard.pixels() != pad.pixels());
}
