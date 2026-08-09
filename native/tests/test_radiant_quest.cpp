// TASK #81. THE RADIANT QUEST GENERATOR CORE.
//
// Three kinds of case, the same split test_contract.cpp uses for its own
// radiant system:
//
//   RAWS      the templates' own file, and the one claim that keeps this from
//             being a slot machine: a template naming a kind this build
//             cannot evaluate, or a body type that cannot exist (a beast, or
//             nothing at all), is refused at load -- proved against two
//             DELIBERATE bad entries content/raws/quests/radiant_quests.json
//             carries for exactly this, the same way contracts.json carries
//             `nobody_at_all`.
//   RADIANCE  the same day of the same world offers the same board on any
//             machine, and a different day does not.
//   LIVE       every generated objective names a body that is actually on the
//   BINDING    ward's roll right now, under the ward's own baked name, and a
//             place read off that body's OWN live tile at generation time --
//             never a fabricated noun, never the giver delivering to itself.

#include <doctest/doctest.h>

#include <cctype>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/radiant_quest.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/world_hash.hpp"

#include "support/ward_fixture.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace testfix = granadad::testfix;

namespace {

const RadiantRaws& raws() {
    static const RadiantRaws loaded = RadiantRaws::load(content::contentDir());
    return loaded;
}

/// A template by id, or nullptr. What "did the deliberate bad one survive"
/// asks in one line.
const RadiantTemplate* templateNamed(std::string_view id) {
    for (const RadiantTemplate& tmpl : raws().templates()) {
        if (tmpl.id == id) {
            return &tmpl;
        }
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("a radiant template naming a kind this build cannot evaluate is refused at load") {
    REQUIRE(raws().loaded());
    // Exactly the six real templates content/raws/quests/radiant_quests.json
    // authors -- the two deliberate bad ones dropped, nothing else lost.
    REQUIRE(raws().templates().size() == 6);

    for (const RadiantTemplate& tmpl : raws().templates()) {
        INFO("template ", tmpl.id);
        REQUIRE_FALSE(tmpl.verb.empty());
        REQUIRE_FALSE(tmpl.brief.empty());
        REQUIRE_FALSE(tmpl.giverTypes.empty());
        REQUIRE_FALSE(tmpl.targetTypes.empty());
        for (const WardType type : tmpl.giverTypes) {
            REQUIRE(isPerson(type));
        }
        for (const WardType type : tmpl.targetTypes) {
            REQUIRE(isPerson(type));
        }
        REQUIRE(tmpl.unitsMin >= 1);
        REQUIRE(tmpl.unitsMax >= tmpl.unitsMin);
        if (tmpl.kind == RadiantKind::Fetch) {
            REQUIRE_FALSE(tmpl.goods.empty());
            REQUIRE(tmpl.payPerUnit > 0);
        } else {
            REQUIRE(tmpl.payFlat > 0);
        }
    }

    // AND THE REFUSAL IS PROVED, NOT ASSUMED. `refused_unknown_kind` names a
    // kind ("escort") this build has no way to evaluate; `refused_beast_only`
    // asks a mouse to carry word. Both load-parse cleanly as JSON and both are
    // still not in the table.
    CHECK(templateNamed("refused_unknown_kind") == nullptr);
    CHECK(templateNamed("refused_beast_only") == nullptr);

    // Every one of the six real ids is present, so the two refusals cost
    // nothing else in the file.
    for (std::string_view id : {"fetch_counter_short", "fetch_watch_bounty",
                                "fetch_kennel_arrears", "deliver_counter_word",
                                "deliver_watch_word", "deliver_quiet_word"}) {
        INFO("expected template ", id);
        CHECK(templateNamed(id) != nullptr);
    }
}

TEST_CASE("a missing radiant file boots silent, and the board offers nothing") {
    const RadiantRaws absent = RadiantRaws::load("/definitely-not-a-content-directory");
    CHECK_FALSE(absent.loaded());
    CHECK(absent.templates().empty());

    RadiantBoard board;
    board.refresh(1, testfix::kSeed, absent, testfix::wardAt(8, 0));
    CHECK(board.objectives().empty());
}

// ===========================================================================
// RADIANCE
// ===========================================================================

TEST_CASE("the same day of the same world offers the same board, and a different day does not") {
    const WardPopulation& ward = testfix::wardAt(8, 0);

    RadiantBoard first;
    RadiantBoard second;
    first.refresh(3, testfix::kSeed, raws(), ward);
    second.refresh(3, testfix::kSeed, raws(), ward);

    REQUIRE_FALSE(first.objectives().empty());
    REQUIRE(second.objectives().size() == first.objectives().size());
    for (std::size_t i = 0; i < first.objectives().size(); ++i) {
        const RadiantObjective& a = first.objectives()[i];
        const RadiantObjective& b = second.objectives()[i];
        INFO("slot ", i);
        CHECK(a.id == b.id);
        CHECK(a.templateId == b.templateId);
        CHECK(a.giverActorId == b.giverActorId);
        CHECK(a.targetActorId == b.targetActorId);
        CHECK(a.good == b.good);
        CHECK(a.units == b.units);
        CHECK(a.pay == b.pay);
        CHECK(a.brief == b.brief);
    }

    HashSink sinkA(12345);
    first.hashInto(sinkA);
    HashSink sinkB(12345);
    second.hashInto(sinkB);
    CHECK(sinkA.finished() == sinkB.finished());

    // A different day is a different board.
    RadiantBoard later;
    later.refresh(3, testfix::kSeed, raws(), ward);
    const std::string wasFirst = later.objectives().front().brief;
    later.refresh(4, testfix::kSeed, raws(), ward);
    CHECK_FALSE(later.objectives().empty());
    CHECK(later.objectives().front().brief != wasFirst);

    HashSink sinkC(12345);
    later.hashInto(sinkC);
    CHECK(sinkC.finished() != sinkA.finished());
}

TEST_CASE("refresh(day) is a no-op for the day it already holds") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    RadiantBoard board;
    board.refresh(5, testfix::kSeed, raws(), ward);
    const std::size_t first = board.objectives().size();
    const std::int32_t firstId = board.objectives().empty() ? -1 : board.objectives().front().id;
    board.refresh(5, testfix::kSeed, raws(), ward);
    CHECK(board.objectives().size() == first);
    if (!board.objectives().empty()) {
        CHECK(board.objectives().front().id == firstId);
    }
}

// ===========================================================================
// LIVE BINDING -- real nouns, never placeholder text
// ===========================================================================

TEST_CASE("every generated objective names a real body and a real place, never itself") {
    const WardPopulation& ward = testfix::wardAt(8, 0);

    std::int32_t fetchSeen = 0;
    std::int32_t deliverSeen = 0;

    // Several days, so the assertion is not one lucky draw.
    for (std::int32_t day = 0; day < 12; ++day) {
        RadiantBoard board;
        board.refresh(day, testfix::kSeed, raws(), ward);

        for (const RadiantObjective& row : board.objectives()) {
            INFO("day ", day, " objective ", row.id, " brief ", row.brief);

            // NOTHING IS LEFT UNSUBSTITUTED. A "{giver}" on screen is the
            // failure this line exists to catch -- test_contract.cpp's own
            // radiance case makes the identical claim about a contract brief.
            CHECK(row.brief.find('{') == std::string::npos);
            CHECK(row.brief.find('}') == std::string::npos);

            // THE GIVER AND THE TARGET ARE BOTH REAL BODIES ON THE WARD'S OWN
            // ROLL, UNDER THE WARD'S OWN NAME FOR THEM.
            const WardActor* giver = ward.byId(row.giverActorId);
            const WardActor* target = ward.byId(row.targetActorId);
            REQUIRE(giver != nullptr);
            REQUIRE(target != nullptr);
            CHECK(isPerson(giver->type));
            CHECK(isPerson(target->type));
            CHECK_FALSE(giver->dead);
            CHECK_FALSE(target->dead);
            CHECK(row.giverName == ward.identity(row.giverActorId).name);
            CHECK(row.targetName == ward.identity(row.targetActorId).name);
            CHECK_FALSE(row.giverName.empty());
            CHECK_FALSE(row.targetName.empty());

            // NOBODY DELIVERS TO THEMSELVES OR FETCHES FROM THEIR OWN COUNTER.
            CHECK(row.giverActorId != row.targetActorId);

            // AND THE PLACE IS READ OFF THAT BODY'S OWN LIVE TILE AT
            // GENERATION TIME -- recomputed here, independently, off the same
            // ward the board was just handed.
            CHECK(row.giverPlace ==
                 std::string(docks::placeLabelAt(giver->x, giver->y, giver->band)));
            CHECK(row.targetPlace ==
                 std::string(docks::placeLabelAt(target->x, target->y, target->band)));
            CHECK_FALSE(row.giverPlace.empty());
            CHECK_FALSE(row.targetPlace.empty());

            // The composed sentence actually SAYS the two real names -- not
            // just that they were computed, but that substitution reached the
            // string a player would read.
            CHECK(row.brief.find(row.giverName) != std::string::npos);
            CHECK(row.brief.find(row.targetName) != std::string::npos);

            if (row.isFetch()) {
                ++fetchSeen;
                CHECK(row.units >= 1);
                CHECK(row.pay >= 1);
                // The good is one of the trade vocabulary's own five, and its
                // lower-cased label actually made it into the sentence.
                const std::string good = std::string(contrabandLabelFor(row.good, row.units));
                std::string lowered = good;
                for (char& c : lowered) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                CHECK(row.brief.find(lowered) != std::string::npos);
            } else {
                ++deliverSeen;
                CHECK(row.units == 0);
                CHECK(row.pay >= 1);
            }
            CHECK(row.postedOnDay == day);
        }
    }

    // Both kinds this file supports actually got generated across the run --
    // a suite that only ever exercised one kind would not be proving the
    // other one works.
    CHECK(fetchSeen > 0);
    CHECK(deliverSeen > 0);
}
