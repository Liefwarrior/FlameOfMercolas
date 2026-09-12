// THE PULL PACK, PINNED -- the street says where next, and the simulation
// cannot tell.
//
// Four claims, each its own case:
//
//   1. THE FOLLOWED LEAD IS OUTSIDE EVERY HASH. Two sessions from one config
//      run the same sim acts; one also follows a lead, fronts a case, walks
//      the shelf and draws frames. The engine's combined hash, the tavern's
//      own section and the three books come out byte-identical. This is the
//      proof the lane's charter asked for by name.
//   2. THE RIBBON LINE IS THE PAGE'S OWN BEARING. One arithmetic
//      (pullBearing), two surfaces; the line names a PLACE and never a
//      person or a clue (owner ruling D8).
//   3. THE TOAST FIRES ON A REAL LEVEL, in situ, and queues behind itself.
//   4. THE BOOK NEWS: a silent hear is announced, a stage advance is
//      announced, a site's own plate is never said twice.
//
// Plus the shelf's own contract (front, refuse, fall back on close) and the
// ticks. The strip's word budget and its width at 320x180 and 1920x1080 are
// pinned in test_hud_diet.cpp, beside the budget they grew.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/casebook_page.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/pull.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

const sim::CasebookRaws& raws() {
    static sim::CasebookRaws file = sim::CasebookRaws::load(content::contentDir());
    return file;
}

/// A session standing on a named lead's own site -- test_casebook_page's own
/// fixture, so the two files agree about where a body starts.
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
    config.width = 960;
    config.height = 540;
    return config;
}

[[nodiscard]] std::uint64_t tavernHash(const Session& session) {
    sim::WorldHasher hasher;
    session.tavern().hash_into(hasher.section_sink(session.tavern().id()));
    return hasher.section_hash(session.tavern().id());
}

[[nodiscard]] std::uint64_t booksHash(const Session& session) {
    sim::WorldHasher hasher;
    sim::HashSink& sink = hasher.section_sink(0x424F4F4Bu);
    session.casebook().hashInto(sink);
    session.sheetBook().hashInto(sink);
    session.evictBook().hashInto(sink);
    return hasher.combined_hash();
}

[[nodiscard]] const CasebookLeadRow* rowFor(const CasebookPageState& page, std::string_view brief) {
    for (const CasebookLeadRow& row : page.rows) {
        if (row.brief == brief) {
            return &row;
        }
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// 1. OUTSIDE EVERY HASH
// ===========================================================================

TEST_CASE("the followed lead, the fronted case and the toast move no hash: tavern, world and books byte-identical") {
    Session plain(configAt("mission-backroom"));
    Session pulled(configAt("mission-backroom"));
    const auto same = [&](const char* where) {
        INFO("at ", where);
        CHECK(plain.simHash() == pulled.simHash());
        CHECK(tavernHash(plain) == tavernHash(pulled));
        CHECK(booksHash(plain) == booksHash(pulled));
    };
    same("construction");

    // THE SAME SIM ACT IN BOTH: one step of walking (which is what ends the
    // first-run gate on the courier's countdown in a real session -- a page
    // toggle ends it too, so both sessions end it the same way, by moving),
    // then the look that opens the trail.
    sim::MoveInput nudge;
    nudge.forward = 1;
    plain.step(nudge);
    pulled.step(nudge);
    plain.stepMany(sim::MoveInput{}, 2);
    pulled.stepMany(sim::MoveInput{}, 2);
    plain.examine();
    pulled.examine();
    plain.stepMany(sim::MoveInput{}, 20);
    pulled.stepMany(sim::MoveInput{}, 20);
    same("after the look");

    // THE PULL, IN ONE ONLY: follow a lead that is not the authored default,
    // front the book off the shelf, walk the page, draw a frame.
    const std::int32_t outfall = raws().indexOf("the-outfall");
    REQUIRE(outfall >= 0);
    REQUIRE(pulled.casebook().state(outfall) == sim::LeadState::Open);
    REQUIRE(pulled.followLead(CaseBookId::Bloodletter, outfall));
    CHECK(pulled.followedLead().chosen());
    CHECK(pulled.pullTarget().lead == outfall);
    CHECK(pulled.frontCase(CaseBookId::Bloodletter));
    pulled.toggleCasebook();
    pulled.stepMany(sim::MoveInput{}, 4);
    pulled.cycleCasebookTab(1);
    pulled.cycleCasebookTab(1);
    pulled.moveCasebookCursor(1);
    pulled.followCasebookSelection();
    Framebuffer frame(960, 540);
    (void)pulled.drawFrame(frame);
    pulled.toggleCasebook();
    plain.stepMany(sim::MoveInput{}, 4);
    same("after the follow");
    CHECK_FALSE(pulled.pullLineNow().empty());
    CHECK(plain.pullLineNow() != pulled.pullLineNow());

    // AND THE TOAST: a level earned in both (the same sim act), shown in
    // both -- the showing is render state, the level is not.
    for (Session* session : {&plain, &pulled}) {
        sim::SkillTrack& skills = session->tavern().dialogue().skills();
        while (!skills.use(sim::kBlockSkill)) {
        }
    }
    same("after the level");

    // WALK. Sixty steps of the same input, the whole body-and-ward machine
    // running in both, the ribbon counting paces down in one.
    sim::MoveInput walk;
    walk.forward = 1;
    plain.stepMany(walk, 60);
    pulled.stepMany(walk, 60);
    Framebuffer again(960, 540);
    (void)pulled.drawFrame(again);
    same("after the walk");
    CHECK(plain.tavern().dialogue().skills().level(sim::kBlockSkill) ==
          pulled.tavern().dialogue().skills().level(sim::kBlockSkill));
    CHECK(pulled.skillToastLabel() == "SHIELDWALL RISES TO 1");
}

// ===========================================================================
// 2. THE RIBBON LINE IS THE PAGE'S OWN BEARING, AND NAMES A PLACE
// ===========================================================================

TEST_CASE("the ribbon reads the followed lead in the page's own bearing, and the default is the authored next lead") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    // Before the look: the start lead is Open and the body stands on it, so
    // the default pull is that lead and the line says HERE.
    const std::int32_t start = raws().indexOf("mission-backroom");
    REQUIRE(start >= 0);
    CHECK(session.pullTarget().set);
    CHECK(session.pullTarget().lead == start);
    CHECK_FALSE(session.pullTarget().chosen);
    CHECK(session.pullLineNow() == "HERE  MISSION OF THE FLAME");

    session.examine();
    session.stepMany(sim::MoveInput{}, 2);
    // The look read the start lead; the default moves to the next Open lead
    // in authored order -- Casebook::nextOpen, the same rule the corner row
    // has always used.
    const std::int32_t next = session.casebook().nextOpen();
    REQUIRE(next >= 0);
    CHECK(session.pullTarget().lead == next);
    CHECK_FALSE(session.pullTarget().chosen);

    // FOLLOW the Outfall: the line is the page's own bearing to it.
    const std::int32_t outfall = raws().indexOf("the-outfall");
    REQUIRE(session.followLead(CaseBookId::Bloodletter, outfall));
    session.stepMany(sim::MoveInput{}, 1);
    const CasebookPageState page = session.casebookPageState();
    const CasebookLeadRow* row = rowFor(page, "THE OUTFALL");
    REQUIRE(row != nullptr);
    CHECK(row->followed);
    CHECK(session.pullLineNow() == pullLine(row->bearing, row->place));
    CHECK(session.pullLineNow() == row->bearing + "  THE OUTFALL");
    // The page's other rows are not followed; the default row is not either
    // once a choice stands.
    int followedRows = 0;
    for (const CasebookLeadRow& r : page.rows) {
        followedRows += r.followed ? 1 : 0;
    }
    CHECK(followedRows == 1);
    // The corner row names the same door.
    CHECK(session.caseLine().find("THE OUTFALL") != std::string::npos);

    // THE DOCTRINE: a place, never a person, never a clue. The Outfall has
    // no witness; the ledger has Crell -- follow it and the line still says
    // THE WEIGHHOUSE and nothing about him or the ledger.
    const std::int32_t ledger = raws().indexOf("weighhouse-ledger");
    if (session.casebook().state(ledger) == sim::LeadState::Open) {
        REQUIRE(session.followLead(CaseBookId::Bloodletter, ledger));
        const std::string line = session.pullLineNow();
        CHECK(line.find("THE WEIGHHOUSE") != std::string::npos);
        CHECK(line.find("CRELL") == std::string::npos);
        CHECK(line.find("LEDGER") == std::string::npos);
        for (const sim::Lead& lead : raws().leads()) {
            CHECK(line.find(lead.what) == std::string::npos);
            if (!lead.found.empty()) {
                CHECK(line.find(lead.found) == std::string::npos);
            }
        }
    }

    // FOLLOW IS ITS OWN UNDO: the followed lead again lets it go, and the
    // authored default is back.
    const PullTarget chosen = session.pullTarget();
    REQUIRE(chosen.chosen);
    REQUIRE(session.followLead(chosen.book, chosen.lead));
    CHECK_FALSE(session.followedLead().chosen());
    CHECK(session.pullTarget().lead == session.casebook().nextOpen());

    // A DEAD LEAD IS NOT A DIRECTION: the read start lead refuses.
    CHECK_FALSE(session.followLead(CaseBookId::Bloodletter, start));
    CHECK_FALSE(session.followedLead().chosen());
}

TEST_CASE("the choice lapses when the followed lead is stood over, and the bearing is the one arithmetic") {
    // pullBearing agrees with itself on every authored lead from a fixed
    // spot, HERE included, and the page prints exactly it.
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    session.stepMany(sim::MoveInput{}, 1);
    const CasebookPageState page = session.casebookPageState();
    const std::int32_t px = session.body().tileX();
    const std::int32_t py = session.body().tileY();
    for (const std::int32_t index : session.casebook().known()) {
        const sim::Lead& lead = raws().leads()[static_cast<std::size_t>(index)];
        const std::string want = pullBearing(px, py, session.body().band(), lead.site, false);
        bool found = false;
        for (const CasebookLeadRow& row : page.rows) {
            if (row.place == lead.place && row.brief == (lead.brief.empty() ? lead.place : lead.brief)) {
                CHECK(row.bearing == want);
                found = true;
            }
        }
        CHECK(found);
    }
    // The line to a lead on another band names the band, the page's rule.
    const sim::LeadSite below{px + 10, py, session.body().band() - 1};
    CHECK(pullBearing(px, py, session.body().band(), below, false).find("BAND ") !=
          std::string::npos);
    CHECK(pullBearing(px, py, session.body().band(), below, true).rfind("HERE", 0) == 0);
    CHECK(pullLine("NE 40", "THE WEIGHHOUSE") == "NE 40  THE WEIGHHOUSE");
    CHECK(pullLine("W 66  BAND 18", "BRANN'S CHANDLERY") == "W 66  BRANN'S CHANDLERY  BAND 18");
    CHECK(pullLine("", "THE WEIGHHOUSE").empty());

    // THE LAPSE: follow the flagstones (six tiles west, same building), walk
    // there and look. The choice was on an Open lead; the look makes it
    // Followed; the pull falls back to the authored default on its own.
    const std::int32_t flags = raws().indexOf("mission-flagstones");
    REQUIRE(flags >= 0);
    if (session.casebook().state(flags) == sim::LeadState::Open) {
        REQUIRE(session.followLead(CaseBookId::Bloodletter, flags));
        REQUIRE(session.pullTarget().chosen);
        const sim::LeadSite site = raws().leads()[static_cast<std::size_t>(flags)].site;
        session.placeBodyAt(site.x, site.y, site.band);
        session.stepMany(sim::MoveInput{}, 2);
        session.examine();
        session.stepMany(sim::MoveInput{}, 1);
        REQUIRE(session.casebook().state(flags) != sim::LeadState::Open);
        CHECK_FALSE(session.pullTarget().chosen);
        CHECK(session.pullTarget().lead != flags);
    }
}

// ===========================================================================
// 3. THE TOAST
// ===========================================================================

TEST_CASE("a skill rising toasts in situ, holds, queues behind itself and goes back to sleep") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 5);
    REQUIRE_FALSE(session.skillToastWanted());
    CHECK(session.skillToastLabel().empty());

    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    REQUIRE(skills.find(sim::kRoofSkill) != nullptr);
    // Uses that do not level yet toast nothing.
    (void)skills.use(sim::kBlockSkill);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.skillToastWanted());
    // The level: the very next step says so, in the raws' own name.
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    const HudState hud = session.pullHud();
    CHECK(hud.skillToast == "SHIELDWALL RISES TO 1");
    CHECK(hud.skillToastFade > 0.0F);

    // A second rise while the first is up waits its turn rather than
    // overwriting the words mid-fade.
    while (!skills.use(sim::kRoofSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    session.stepMany(sim::MoveInput{}, 200);
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel().rfind("SKYRUNNING RISES TO ", 0) == 0);
    // And then sleep.
    session.stepMany(sim::MoveInput{}, 200);
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.pullHud().skillToastFade == 0.0F);
}

TEST_CASE("the toast draws in the free corner, whole, at both pinned sizes, and clears the play space") {
    for (const auto& size : {std::pair{320, 180}, std::pair{1920, 1080}}) {
        for (const char* text : {"SKYRUNNING RISES TO 12", "CRACKSMANSHIP RISES TO 100"}) {
            HudState quiet;
            quiet.timeOfDaySeconds = 20 * 3600;
            quiet.clockFade = 1.0F;
            Framebuffer bare(size.first, size.second);
            bare.clear(Rgb{0.10F, 0.12F, 0.14F});
            Framebuffer without(size.first, size.second);
            without.clear(Rgb{0.10F, 0.12F, 0.14F});
            drawHud(without, quiet);
            HudState lit = quiet;
            lit.skillToast = text;
            lit.skillToastFade = 1.0F;
            Framebuffer with(size.first, size.second);
            with.clear(Rgb{0.10F, 0.12F, 0.14F});
            drawHud(with, lit);
            const CentreRect centre = hudCentreRect(size.first, size.second);
            const int scale = hudScale(size.second);
            const int stripX = (size.first - std::min(size.first / 4, 120 * scale)) / 2;
            std::size_t ink = 0;
            for (int y = 0; y < size.second; ++y) {
                for (int x = 0; x < size.first; ++x) {
                    const std::size_t i = with.index(x, y);
                    if (with.pixels()[i] == without.pixels()[i]) {
                        continue;
                    }
                    ++ink;
                    INFO(text, " at ", size.first, "x", size.second, " pixel ", x, ",", y);
                    // Inside the top band, left of the ribbon, never in the
                    // play space.
                    CHECK(y < centre.y0);
                    CHECK(x < stripX);
                }
            }
            CHECK(ink > 0);
        }
    }
}

// ===========================================================================
// 4. THE BOOK NEWS
// ===========================================================================

TEST_CASE("a silent hear is announced on the plate, a site's own plate is never said twice, a stage moves on") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 5);
    REQUIRE_FALSE(session.casePlateWanted());

    // A SITE'S OWN PLATE: the courier hands the sheet over and says so
    // itself. The watcher sees the same hear on the next step and must not
    // turn "A MISSION SHEET" into "1 NEW LEAD".
    session.courierDeliverNow();
    REQUIRE(session.casePlateWanted());
    const std::string sheet(session.casePlateLabel());
    CHECK(sheet.rfind("A MISSION SHEET", 0) == 0);
    session.stepMany(sim::MoveInput{}, 3);
    CHECK(session.casePlateLabel() == sheet);
    session.stepMany(sim::MoveInput{}, 200);
    REQUIRE_FALSE(session.casePlateWanted());

    // THE SILENT HEAR -- the exact call interact()'s TAKE HIM UP makes into
    // the courier book with no plate beside it. The watcher fills the gap.
    const std::int32_t close = session.sheetRaws().indexOf("bring-him-in");
    REQUIRE(close >= 0);
    REQUIRE(session.sheetBook().hear(close));
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.casePlateWanted());
    CHECK(std::string(session.casePlateLabel()).rfind("1 NEW LEAD", 0) == 0);
    session.stepMany(sim::MoveInput{}, 200);
    REQUIRE_FALSE(session.casePlateWanted());

    // A STAGE ADVANCE: the journal moves and the plate names the line.
    sim::DialogueDirector& talk = session.tavern().dialogue();
    const sim::Questline* oath = talk.quests().find("flame-disciple");
    REQUIRE(oath != nullptr);
    talk.journal().start(oath->id);
    REQUIRE(talk.journal().advance(*oath));
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.casePlateWanted());
    CHECK(std::string(session.casePlateLabel()).rfind("THE DISCIPLE'S OATH MOVES ON", 0) == 0);
    session.stepMany(sim::MoveInput{}, 200);
    REQUIRE_FALSE(session.casePlateWanted());

    // The shelf lists the started line with its stage.
    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 1);
    const CasebookPageState page = session.casebookPageState();
    bool listed = false;
    for (const CasebookShelfRow& row : page.shelf) {
        if (row.title == "THE DISCIPLE'S OATH") {
            listed = true;
            CHECK_FALSE(row.book);
            CHECK(row.state.rfind("STAGE ", 0) == 0);
            CHECK_FALSE(row.next.empty());
        }
    }
    CHECK(listed);
}

// ===========================================================================
// 5. THE SHELF
// ===========================================================================

TEST_CASE("the CASES shelf lists the three books, fronts one the player has, refuses one they do not, and falls back when it closes") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    session.examine();
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.frontedBook() == CaseBookId::Bloodletter);

    // Not yet handed the courier's sheet: the shelf says NOT YET and the
    // commit refuses.
    CasebookPageState page = session.casebookPageState();
    REQUIRE(page.shelf.size() >= 3);
    CHECK(page.shelf[0].title == "THE BLOODLETTER");
    CHECK(page.shelf[0].fronted);
    CHECK(page.shelf[0].followed);
    CHECK(page.shelf[1].state == "NOT YET");
    CHECK_FALSE(page.shelf[1].selectable);
    CHECK(page.shelf[2].state == "NOT YET");
    CHECK_FALSE(session.frontCase(CaseBookId::Courier));
    CHECK(session.frontedBook() == CaseBookId::Bloodletter);
    CHECK(page.followKey == "F");

    // The courier arrives: the auto rule fronts the errand, and the shelf
    // says READ 0/1 for it.
    session.courierDeliverNow();
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.frontedBook() == CaseBookId::Courier);
    page = session.casebookPageState();
    CHECK(page.caseTitle == "THE QUIET TENANT");
    CHECK(page.shelf[1].fronted);
    CHECK(page.shelf[1].state == "READ 0/1");
    CHECK(page.shelf[1].followed);
    CHECK_FALSE(page.shelf[0].fronted);

    // THE SWITCHER: walk the shelf with the page's own cursor and READ the
    // Bloodletter -- the page shows it while the errand still lives.
    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 2);
    session.cycleCasebookTab(1);
    session.cycleCasebookTab(1);
    REQUIRE(session.casebookTab() == CasebookTab::Cases);
    session.moveCasebookCursor(1);
    session.moveCasebookCursor(-1);
    CHECK(session.casebookShelfCursor() == 0);
    session.commitCasebookLead();
    CHECK(session.frontedBook() == CaseBookId::Bloodletter);
    CHECK(session.casebookTab() == CasebookTab::Leads);
    page = session.casebookPageState();
    CHECK(page.caseTitle == "THE BLOODLETTER");
    CHECK(page.shelf[0].fronted);
    // The corner row and the ribbon follow the page's book.
    CHECK(session.pullTarget().book == CaseBookId::Bloodletter);
    // FOLLOW off the shelf follows that case's own next lead.
    session.cycleCasebookTab(-1);
    REQUIRE(session.casebookTab() == CasebookTab::Cases);
    session.moveCasebookCursor(1);
    CHECK(session.casebookShelfCursor() == 1);
    session.followCasebookSelection();
    CHECK(session.followedLead().chosen());
    CHECK(session.followedLead().book == CaseBookId::Courier);
    CHECK(session.pullTarget().book == CaseBookId::Courier);
    CHECK(session.pullLineNow().find("THE GILDED GULL") != std::string::npos);
    session.toggleCasebook();

    // THE FALL-BACK: front the courier book, then close it by the same
    // scripted hear-and-look the case itself performs, and the page hands
    // itself back to the auto rule on the step the book closes.
    REQUIRE(session.frontCase(CaseBookId::Courier));
    CHECK(session.frontedBook() == CaseBookId::Courier);
    const std::int32_t close = session.sheetRaws().indexOf("bring-him-in");
    REQUIRE(close >= 0);
    (void)session.sheetBook().hear(close);
    const sim::LeadSite site = session.sheetRaws().leads()[static_cast<std::size_t>(close)].site;
    (void)session.sheetBook().look(site.x, site.y, site.band);
    REQUIRE(session.sheetBook().closed());
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.frontedBook() == CaseBookId::Bloodletter);
    // ...and the ribbon's courier choice, now pointing at a read lead, has
    // lapsed to the Bloodletter's own next lead.
    CHECK(session.pullTarget().book == CaseBookId::Bloodletter);
    CHECK_FALSE(session.pullTarget().chosen);
    // Rereading the closed book is one shelf commit away.
    CHECK(session.frontCase(CaseBookId::Courier));
    CHECK(session.frontedBook() == CaseBookId::Courier);
    CHECK(session.casebookPageState().shelf[1].state == "CLOSED");
}

// ===========================================================================
// 6. THE TICKS
// ===========================================================================

TEST_CASE("discovered named places tick the ribbon, the followed lead's tick is its own, and nothing is drawn for a person") {
    Session session(configAt("mission-backroom"));
    session.stepMany(sim::MoveInput{}, 2);
    // Standing IN the Mission on its own lead: the Mission is discovered
    // twice over (stood in, named by the book) and the pull's own tick is
    // suppressed while the body is on the site.
    HudState hud = session.pullHud();
    CHECK(hud.pullLabel == "HERE  MISSION OF THE FLAME");
    session.examine();
    session.stepMany(sim::MoveInput{}, 2);
    hud = session.pullHud();
    // Several leads opened: several named places tick, each a bearing in
    // 0..65535, and the followed lead's tick is set and points at its site.
    CHECK(hud.placeTickBams.size() >= 2);
    for (const std::int32_t bam : hud.placeTickBams) {
        CHECK(bam >= 0);
        CHECK(bam < 65536);
    }
    const PullTarget target = session.pullTarget();
    REQUIRE(target.set);
    const sim::Lead& lead = raws().leads()[static_cast<std::size_t>(target.lead)];
    CHECK(hud.pullTickBam == (sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             lead.site.x, lead.site.y) &
                              65535));

    // THE TICKS DRAW ON THE STRIP'S OWN RAIL AND NOWHERE ELSE, at both pinned
    // sizes -- inside the ribbon's rectangle, never in the play space.
    for (const auto& size : {std::pair{320, 180}, std::pair{1920, 1080}}) {
        HudState quiet;
        quiet.yawBam = hud.pullTickBam;  // facing the followed lead: its tick under the mark
        Framebuffer without(size.first, size.second);
        without.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(without, quiet);
        HudState ticked = quiet;
        ticked.placeTickBams = hud.placeTickBams;
        ticked.pullTickBam = hud.pullTickBam;
        Framebuffer with(size.first, size.second);
        with.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(with, ticked);
        const int scale = hudScale(size.second);
        const int stripW = std::min(size.first / 4, 120 * scale);
        const int x0 = (size.first - stripW) / 2 - scale;
        const int x1 = x0 + stripW + 2 * scale;
        std::size_t ink = 0;
        for (int y = 0; y < size.second; ++y) {
            for (int x = 0; x < size.first; ++x) {
                const std::size_t i = with.index(x, y);
                if (with.pixels()[i] == without.pixels()[i]) {
                    continue;
                }
                ++ink;
                INFO("tick ink at ", x, ",", y, " (", size.first, "x", size.second, ")");
                CHECK(x >= x0);
                CHECK(x < x1);
                CHECK(y < hudCentreRect(size.first, size.second).y0);
            }
        }
        CHECK(ink > 0);
    }
}
