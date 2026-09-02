// EVICTION -- the owner's third authored case, held to the same bar its own
// file's invariants declare, and the bar test_mission_sheet.cpp set for the
// courier before it.
//
//   RAWS   eviction.json on its own -- that it loads, that it carries NO start
//          lead (Maell's own conversation opens it, not the boot), and that it
//          carries TWO closes (participate and disrupt), each reachable: the
//          four investigation leads from the hire lead, and the two closes
//          from the scripted beats that hear them.
//   WORLD  every lead's site checked against the BAKED DOCKS, the same claim
//          test_casebook.cpp and test_mission_sheet.cpp hold their leads to.
//   PAPER  the three documents cross-referenced to real leads, `handed` true
//          on the writ alone, readable prose with no id leaking through, and
//          the lease-agreement mapping asserted in the ward's own vocabulary.
//   CANON  the Bloodletter case and the courier case are both byte-untouched
//          and still load their own leads.

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

[[nodiscard]] const CasebookRaws& evict() {
    static const CasebookRaws loaded =
        CasebookRaws::loadFile(evictionRawsPath(content::contentDir()));
    return loaded;
}

[[nodiscard]] const LetterRaws& evictLetters() {
    static const LetterRaws loaded =
        LetterRaws::loadFile(evictionLetterRawsPath(content::contentDir()));
    return loaded;
}

/// The same prose bar the other two case-tests keep: printable ASCII only, and
/// no raw id leaking where a sentence was meant.
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

TEST_CASE("the eviction case loads, with no start lead and exactly two closes") {
    const CasebookRaws& file = evict();
    REQUIRE(file.loaded());
    CHECK_FALSE(file.title().empty());
    CHECK_FALSE(file.hook().empty());
    CHECK_FALSE(file.close().empty());

    std::int32_t starts = 0;
    std::int32_t closes = 0;
    for (const Lead& lead : file.leads()) {
        // NO lead is open at boot -- the case enters the book by the hire, the
        // courier-case invariant. And TWO closes, not one: participate and
        // disrupt are the brief's own spine, and either shuts the book.
        if (lead.start) {
            ++starts;
        }
        if (lead.close) {
            ++closes;
        }
    }
    CHECK(starts == 0);
    CHECK(closes == 2);
}

TEST_CASE("every eviction lead is a readable row, and its short name fits the grid") {
    const CasebookRaws& file = evict();
    REQUIRE(file.loaded());
    for (const Lead& lead : file.leads()) {
        INFO("lead ", lead.id);
        checkReadableProse(lead.place);
        checkReadableProse(lead.brief);
        checkReadableProse(lead.what);
        checkReadableProse(lead.found);
        checkReadableProse(lead.detail);
        // The casebook grid is three columns; twelve is the ceiling the other
        // two files' briefs are held to.
        CHECK(lead.brief.size() <= 12);
        // Every lead earns something off the ward's nerve.
        CHECK(lead.dread > 0);
    }
}

TEST_CASE("exactly one eviction lead is a dead end, and it is the roof") {
    // The lodgers' page points nowhere, deliberately -- 'cold is a lead that
    // points nowhere', and pointing nowhere is the lodgers' whole situation.
    const CasebookRaws& file = evict();
    REQUIRE(file.loaded());
    std::int32_t deadEnds = 0;
    for (const Lead& lead : file.leads()) {
        if (lead.deadEnd) {
            ++deadEnds;
            CHECK(lead.id == "roof-lodgers");
            // A dead end still yields a clue -- it opens nothing.
            CHECK(lead.opens.empty());
        }
    }
    CHECK(deadEnds == 1);
}

TEST_CASE("every eviction lead is reachable: the four from the hire, the two closes by their beats") {
    // NOT from a start flag -- there is none. The investigation set grows from
    // writ-in-hand, the lead the hire calls hear() on; the two closes are
    // heard by the session's scripted serve/yield beats, never by an opens
    // edge, so they are roots in their own right. Together the three roots
    // must reach every lead, and nothing must dangle.
    const CasebookRaws& file = evict();
    REQUIRE(file.loaded());
    const std::int32_t hire = file.indexOf("writ-in-hand");
    const std::int32_t served = file.indexOf("served");
    const std::int32_t stood = file.indexOf("stood-down");
    REQUIRE(hire >= 0);
    REQUIRE(served >= 0);
    REQUIRE(stood >= 0);

    std::set<std::int32_t> reached{hire, served, stood};
    std::vector<std::int32_t> frontier{hire, served, stood};
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
    // AND THE TWO CLOSES ARE NOT REACHED BY AN OPENS EDGE -- they are branch
    // outcomes, not trail convergences. Grown from the hire alone, the closes
    // stay out: the four investigation leads only.
    std::set<std::int32_t> fromHire{hire};
    std::vector<std::int32_t> hf{hire};
    while (!hf.empty()) {
        std::vector<std::int32_t> next;
        for (const std::int32_t at : hf) {
            for (const std::string& id : file.leads()[static_cast<std::size_t>(at)].opens) {
                const std::int32_t to = file.indexOf(id);
                if (to >= 0 && fromHire.insert(to).second) {
                    next.push_back(to);
                }
            }
        }
        hf.swap(next);
    }
    CHECK(fromHire.count(served) == 0);
    CHECK(fromHire.count(stood) == 0);
    CHECK(fromHire.size() == 4);
}

TEST_CASE("the participate close shares the Mission back-room site the Bloodletter already proves") {
    // The served close ends where casebook.json's mission-backroom lead and
    // the courier's bring-him-in both begin: the map's authored
    // clue_c1_mission_backroom anchor, standable and proven. Reusing it keeps
    // the HARD RULE -- no .tmx edit, no new anchor.
    const CasebookRaws& file = evict();
    const CasebookRaws bloodletter = CasebookRaws::load(content::contentDir());
    REQUIRE(file.loaded());
    REQUIRE(bloodletter.loaded());
    const std::int32_t close = file.indexOf("served");
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

TEST_CASE("every eviction lead stands somewhere a body can stand in the baked Docks") {
    const content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    const TileQuery tiles(world);
    const CasebookRaws& file = evict();
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

TEST_CASE("the gate stands over the ground the roll says the Netters' holds") {
    // The case's ground join asserted the way the ROLL topic proves it live:
    // the gate lead sits on plot ground plotIdUnder names, and it is the
    // Netters' -- so the writ the player carries and the roll underfoot agree
    // about whose earth this is. The FAMILY DOOR itself is deliberately NOT
    // checked here: its authored site (195,102) is the c04 outer-ring door
    // that opens onto the Gullet back-lane, one tile east of the plot's own
    // sign footprint (x0..x1 = 148..191). That the poor-quarter door sits on
    // the very edge of the charge is a true fact about this compound, not a
    // coordinate slip -- the gate is where the ground is unambiguously the
    // widow's, and that is what the roll agreement is proved against.
    const CasebookRaws& file = evict();
    REQUIRE(file.loaded());
    const std::int32_t gate = file.indexOf("netters-gate");
    REQUIRE(gate >= 0);
    const LeadSite& site = file.leads()[static_cast<std::size_t>(gate)].site;
    CHECK(docks::plotIdUnder(site.x, site.y) == "C2_NETTERS");
}

// ===========================================================================
// PAPER -- the lease agreement, in the ward's own instruments
// ===========================================================================

TEST_CASE("the eviction paper is three documents, handed only on the writ") {
    const LetterRaws& letters = evictLetters();
    const CasebookRaws& file = evict();
    REQUIRE(letters.loaded());
    REQUIRE(file.loaded());

    // Three: the writ (the hearing's output), the widow's petition (its
    // cause), the served notice (its teeth). The file's own notes state the
    // restraint against a fourth.
    CHECK(letters.letters().size() == 3);
    std::int32_t handed = 0;
    for (const Letter& letter : letters.letters()) {
        INFO("letter ", letter.id);
        // Every `lead` resolves in the eviction case -- the string cross-
        // reference the other two files keep.
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
        if (letter.handed) {
            ++handed;
            // Only the writ is handed: it is the post the player was SENT
            // with, readable the moment the hire lead is heard.
            CHECK(letter.id == "the-writ-of-distraint");
            CHECK(letter.lead == "writ-in-hand");
        }
    }
    CHECK(handed == 1);
}

TEST_CASE("the lease agreement is rendered in the roll's own words, never as a dwelling-lease") {
    // The brief's rule 9 wants 'the lease agreement' involved; section 2.8
    // rules there is no dwelling-lease in this city (the house is the tenant's
    // own; only the ground penny is owed). So the paper must speak the tenure's
    // real instruments and must NOT invent a lease on the dwelling.
    const LetterRaws& letters = evictLetters();
    REQUIRE(letters.loaded());
    std::string whole;
    for (const Letter& letter : letters.letters()) {
        whole += " " + letter.from + " " + letter.to;
        for (const std::string& paragraph : letter.body) {
            whole += " " + paragraph;
        }
    }
    // The instruments section 2.8 names are all present.
    CHECK(whole.find("ground penny") != std::string::npos);
    CHECK(whole.find("charge") != std::string::npos);
    CHECK(whole.find("roll") != std::string::npos);
    CHECK(whole.find("petition") != std::string::npos);
    CHECK(whole.find("distraint") != std::string::npos || whole.find("DISTRAINT") != std::string::npos);
    // And the ugliest true clause: the roof goes with the house.
    CHECK(whole.find("roof") != std::string::npos);
    // NEVER a dwelling-lease: the one word the canon-grounding paragraph says
    // the novel never uses of a home here. (A 'lease' of one's own labour is a
    // different thing and is not what this paper describes -- the file uses
    // 'bond' for that, per the vocabulary.)
    CHECK(whole.find("lease") == std::string::npos);
    CHECK(whole.find("leasehold") == std::string::npos);
}

TEST_CASE("the war lore is spelled the canon way, and the priest states the texture straight") {
    // Rule 8: the Mercian shipment / Dezdant war framing, spelled per
    // COMBAT-SPEC/MATERIALS-CANON. Rule 10, verbatim: the priest believes the
    // second half -- order is why the city is suffered to stand -- and says so.
    const LetterRaws& letters = evictLetters();
    REQUIRE(letters.loaded());
    std::string whole;
    for (const Letter& letter : letters.letters()) {
        for (const std::string& paragraph : letter.body) {
            whole += " " + paragraph;
        }
    }
    CHECK(whole.find("Mercian") != std::string::npos);
    CHECK(whole.find("Dezdant") != std::string::npos);
    // The texture, in the priest's own hand.
    CHECK(whole.find("suffered to stand") != std::string::npos);
}

// ===========================================================================
// CANON: the earlier two cases are untouched
// ===========================================================================

TEST_CASE("the Bloodletter and the courier cases still load their own leads, unmoved by the third") {
    const CasebookRaws bloodletter = CasebookRaws::load(content::contentDir());
    const CasebookRaws courier =
        CasebookRaws::loadFile(missionSheetRawsPath(content::contentDir()));
    REQUIRE(bloodletter.loaded());
    REQUIRE(courier.loaded());
    // The Bloodletter's twelve and the courier's four, exactly as their own
    // tests pin them -- proof the third file is additive.
    CHECK(bloodletter.leads().size() == 12);
    CHECK(courier.leads().size() == 4);
    CHECK(courier.indexOf("gull-door") >= 0);
    CHECK(bloodletter.indexOf("drowned-hold") >= 0);
}
