// COURIER CASE -- THE QUIET TENANT. The owner's second authored case, held to
// the same bar its own file's invariants declare.
//
//   RAWS   mission_sheet.json on its own -- that it loads, that it carries NO
//          start lead (the courier's hand opens it, not the boot), exactly one
//          close, and that every lead is reachable from the courier's first
//          lead rather than from a start flag the file deliberately omits.
//   WORLD  every lead's site checked against the BAKED DOCKS, the same claim
//          test_casebook.cpp holds the Bloodletter's twelve to.
//   PAPER  the mission sheet cross-referenced to a real lead, `handed` true,
//          and readable prose with no id leaking through.
//   CANON  the Bloodletter case is byte-untouched and still loads its twelve.

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/letters.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

[[nodiscard]] const CasebookRaws& sheet() {
    static const CasebookRaws loaded =
        CasebookRaws::loadFile(missionSheetRawsPath(content::contentDir()));
    return loaded;
}

[[nodiscard]] const LetterRaws& sheetLetters() {
    static const LetterRaws loaded =
        LetterRaws::loadFile(missionSheetLetterRawsPath(content::contentDir()));
    return loaded;
}

/// The same prose bar test_letters.cpp keeps: printable ASCII only, and no
/// raw id leaking where a sentence was meant.
void checkReadableProse(const std::string& text) {
    INFO("text: ", text);
    CHECK_FALSE(text.empty());
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        CHECK(u >= 0x20);
        CHECK(u < 0x7F);
    }
    CHECK(text.find('_') == std::string::npos);
}

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("the courier case loads, with no start lead and exactly one close") {
    const CasebookRaws& file = sheet();
    REQUIRE(file.loaded());
    CHECK_FALSE(file.title().empty());
    CHECK_FALSE(file.hook().empty());
    CHECK_FALSE(file.close().empty());

    std::int32_t starts = 0;
    std::int32_t closes = 0;
    for (const Lead& lead : file.leads()) {
        // THE WHOLE POINT OF THE SESSION-SCRIPTED DELIVERY: no lead is open at
        // boot. The case enters the book by the courier's own hand, so a run
        // that never meets the courier never shows the errand -- and the
        // per-file test that would otherwise pin "exactly one start" is
        // replaced by "exactly zero", which is the courier design stated as an
        // invariant.
        if (lead.start) {
            ++starts;
        }
        if (lead.close) {
            ++closes;
        }
    }
    CHECK(starts == 0);
    CHECK(closes == 1);
}

TEST_CASE("every courier lead is a readable row, and its short name fits the grid") {
    const CasebookRaws& file = sheet();
    REQUIRE(file.loaded());
    for (const Lead& lead : file.leads()) {
        INFO("lead ", lead.id);
        checkReadableProse(lead.place);
        checkReadableProse(lead.brief);
        checkReadableProse(lead.what);
        checkReadableProse(lead.found);
        checkReadableProse(lead.detail);
        // The casebook grid is three columns; casebook.hpp authors `short`
        // rather than truncating a proper noun at draw time. Twelve is the
        // same ceiling test_casebook.cpp holds the Bloodletter's briefs to.
        CHECK(lead.brief.size() <= 12);
        // A lead that yields nothing to the ward's nerve is a lead that does
        // not belong on a dread track.
        CHECK(lead.dread > 0);
        // No dead ends in this errand -- flagged in the file's own notes.
        CHECK_FALSE(lead.deadEnd);
    }
}

TEST_CASE("every courier lead is reachable from the lead the courier hands over") {
    // NOT FROM A START FLAG -- there is none. The reachable set is grown from
    // gull-door, which is the lead Session's courier beat calls hear() on, so
    // this checks the graph a player actually walks rather than one a boot
    // flag would have opened.
    const CasebookRaws& file = sheet();
    REQUIRE(file.loaded());
    const std::int32_t first = file.indexOf("gull-door");
    REQUIRE(first >= 0);

    std::set<std::int32_t> reached{first};
    std::vector<std::int32_t> frontier{first};
    while (!frontier.empty()) {
        std::vector<std::int32_t> next;
        for (const std::int32_t at : frontier) {
            for (const std::string& id : file.leads()[static_cast<std::size_t>(at)].opens) {
                const std::int32_t to = file.indexOf(id);
                INFO("opens ", id);
                CHECK(to >= 0);  // no dangling cross-reference
                if (to >= 0 && reached.insert(to).second) {
                    next.push_back(to);
                }
            }
        }
        frontier.swap(next);
    }
    CHECK(reached.size() == file.leads().size());
}

TEST_CASE("the close lead shares the Mission back-room site the Bloodletter already proves") {
    // The errand ends where casebook.json's own mission-backroom lead begins:
    // the map's authored clue_c1_mission_backroom anchor, standable and proven
    // by test_casebook.cpp. Reusing it is the HARD RULE respected -- no .tmx
    // edit, no new anchor.
    const CasebookRaws& file = sheet();
    const CasebookRaws bloodletter = CasebookRaws::load(content::contentDir());
    REQUIRE(file.loaded());
    REQUIRE(bloodletter.loaded());
    const std::int32_t close = file.indexOf("bring-him-in");
    const std::int32_t body = bloodletter.indexOf("mission-backroom");
    REQUIRE(close >= 0);
    REQUIRE(body >= 0);
    const LeadSite& a = file.leads()[static_cast<std::size_t>(close)].site;
    const LeadSite& b = bloodletter.leads()[static_cast<std::size_t>(body)].site;
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.band == b.band);
}

// ===========================================================================
// WORLD
// ===========================================================================

TEST_CASE("every courier lead stands somewhere a body can stand in the baked Docks") {
    const content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    const TileQuery tiles(world);
    const CasebookRaws& file = sheet();
    REQUIRE(file.loaded());

    for (const Lead& lead : file.leads()) {
        INFO("lead " << lead.id << " at (" << lead.site.x << ',' << lead.site.y << ",z"
                     << lead.site.band << ')');
        CHECK(tiles.inBounds(lead.site.x, lead.site.y, lead.site.band));
        bool standable = false;
        for (std::int32_t dy = -kLookRangeTiles; dy <= kLookRangeTiles && !standable; ++dy) {
            for (std::int32_t dx = -kLookRangeTiles; dx <= kLookRangeTiles; ++dx) {
                if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > kLookRangeTiles) {
                    continue;
                }
                if (tiles.standable(lead.site.x + dx, lead.site.y + dy, lead.site.band)) {
                    standable = true;
                    break;
                }
            }
        }
        CHECK(standable);
    }
}

// ===========================================================================
// PAPER
// ===========================================================================

TEST_CASE("the mission sheet is handed paper tied to a real courier lead") {
    const LetterRaws& letters = sheetLetters();
    const CasebookRaws& file = sheet();
    REQUIRE(letters.loaded());
    REQUIRE(file.loaded());

    // Exactly one document: an errand this quiet writes no more paper than the
    // sheet itself (the file's own notes state the restraint).
    CHECK(letters.letters().size() == 1);
    for (const Letter& letter : letters.letters()) {
        INFO("letter ", letter.id);
        // HANDED: put into the player's own hand by the courier, so it unlocks
        // on Open where the Bloodletter's five keep the read-not-received gate.
        CHECK(letter.handed);
        // The `lead` resolves in the courier case -- the same string cross-
        // reference test_letters.cpp holds the Bloodletter's five to.
        CHECK(file.indexOf(letter.lead) >= 0);
        checkReadableProse(letter.from);
        checkReadableProse(letter.to);
        checkReadableProse(letter.dateline);
        checkReadableProse(letter.closing);
        checkReadableProse(letter.signature);
        CHECK_FALSE(letter.body.empty());
        for (const std::string& paragraph : letter.body) {
            checkReadableProse(paragraph);
        }
    }
}

TEST_CASE("the sheet names Gabri and the Mission, and invents no place the ward cannot hold") {
    // The brief: the author is the local Mission, the word comes from Gabri,
    // the location is in the sheet. This pins those three and pins the
    // restraint -- the sheet must not locate Gabri, who is off-map novel canon.
    const LetterRaws& letters = sheetLetters();
    REQUIRE(letters.loaded());
    REQUIRE(letters.letters().size() == 1);
    std::string whole = letters.letters()[0].from;
    for (const std::string& paragraph : letters.letters()[0].body) {
        whole += " " + paragraph;
    }
    CHECK(whole.find("Gabri") != std::string::npos);
    CHECK(whole.find("Mission of the Flame") != std::string::npos);
    CHECK(whole.find("Gilded Gull") != std::string::npos);
    // Gabri is off-map: word HAS COME from him, and the sheet says outright it
    // will not put down from where.
    CHECK(whole.find("I will not put down how") != std::string::npos);
}

// ===========================================================================
// CANON: the Bloodletter is untouched
// ===========================================================================

TEST_CASE("the Bloodletter case still loads its own twelve, unmoved by the second file") {
    const CasebookRaws bloodletter = CasebookRaws::load(content::contentDir());
    REQUIRE(bloodletter.loaded());
    CHECK(bloodletter.leads().size() == 12);
    CHECK(bloodletter.indexOf("drowned-hold") >= 0);
    // Exactly one start and one close, the shape test_casebook.cpp pins -- a
    // second case file in the same directory changed none of it.
    std::int32_t starts = 0;
    std::int32_t closes = 0;
    for (const Lead& lead : bloodletter.leads()) {
        if (lead.start) {
            ++starts;
        }
        if (lead.close) {
            ++closes;
        }
    }
    CHECK(starts == 1);
    CHECK(closes == 1);
}
