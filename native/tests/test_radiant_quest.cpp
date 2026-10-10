// TASK #81. THE RADIANT QUEST GENERATOR CORE.
//
// Three kinds of case, the same split test_contract.cpp uses for its own
// radiant system:
//
//   RAWS      the templates' own file, and the one claim that keeps this from
//             being a slot machine: a template naming a kind this build
//             cannot evaluate, or a body type that cannot exist (a beast, or
//             nothing at all), is refused at load -- proved against the
//             DELIBERATE bad entries content/raws/quests/radiant_quests.json
//             and content/raws/quests/radiant_quests_skyrunner.json each
//             carry for exactly this, the same way contracts.json carries
//             `nobody_at_all`. This is also where the SECOND file's own cast
//             gets checked -- the Skyrunner templates run through
//             WardType::Thief and its street affiliates rather than the
//             owner's shopkeepers and watchmen.
//   RADIANCE  the same day of the same world offers the same board on any
//             machine, and a different day does not.
//   LIVE       every generated objective names a body that is actually on the
//   BINDING    ward's roll right now, under the ward's own baked name, and a
//             place read off that body's OWN live tile at generation time --
//             never a fabricated noun, never the giver delivering to itself.
//
// A FOURTH KIND OF CASE -- whether hundreds of days of boards read as more
// than one template said over and over, bucketed by template and by the
// giver's own faction -- lives in tests/test_radiant_variety.cpp instead of
// here, on purpose, so a future content-only addition to either radiant raws
// file never has to touch this file at all. See that file's own header.

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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

/// A throwaway synthetic content tree, so a case can author one deliberately
/// bad template without touching the owner's real radiant_quests.json. Same
/// idiom content/tests/test_content_dir.cpp uses for the same reason.
class TempTree {
public:
    explicit TempTree(const std::string& name)
        : root_(std::filesystem::temp_directory_path() /
                ("granadad-radiant-" + name)) {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
    }
    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    void write(const std::string& relative, std::string_view json) const {
        const std::filesystem::path made = root_ / relative;
        std::error_code error;
        std::filesystem::create_directories(made.parent_path(), error);
        std::ofstream file(made, std::ios::binary);
        file << json;
    }

private:
    std::filesystem::path root_;
};

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
    // The six real templates content/raws/quests/radiant_quests.json authors,
    // PLUS the six real templates content/raws/quests/radiant_quests_skyrunner.json
    // authors, PLUS the five real templates
    // content/raws/quests/radiant_quests_flame.json authors, now that
    // RadiantRaws::load merges every *.json in the directory carrying a
    // "templates" array -- seventeen, with all four deliberate bad ones (two
    // in the owner's file, one in the Skyrunner file, one in the flame file)
    // dropped.
    REQUIRE(raws().templates().size() == 17);

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

    // AND THE REFUSAL IS PROVED, NOT ASSUMED, IN ALL THREE FILES.
    // `refused_unknown_kind` names a kind ("escort") this build has no way to
    // evaluate; `refused_beast_only` asks a mouse to carry word; the
    // Skyrunner file's own `refused_skyrunner_unknown_kind` names a kind
    // ("waylay") no more real than the first; the flame file's own
    // `refused_flame_unknown_kind` names a kind ("bless") no more real than
    // either. All four load-parse cleanly as JSON and none is in the table --
    // proving the refusal discipline holds for a THIRD file the loader reads,
    // not just the first one it was ever tested against.
    CHECK(templateNamed("refused_unknown_kind") == nullptr);
    CHECK(templateNamed("refused_beast_only") == nullptr);
    CHECK(templateNamed("refused_skyrunner_unknown_kind") == nullptr);
    CHECK(templateNamed("refused_flame_unknown_kind") == nullptr);

    // Every one of the seventeen real ids is present, so none of the four
    // refusals cost anything else in any file.
    for (std::string_view id :
        {"fetch_counter_short", "fetch_watch_bounty", "fetch_kennel_arrears",
         "deliver_counter_word", "deliver_watch_word", "deliver_quiet_word",
         "fetch_roost_toll", "fetch_fence_reckoning", "fetch_second_bounty",
         "fetch_flower_climb", "deliver_roof_word", "deliver_fence_summons",
         "fetch_mission_scalps", "fetch_mission_wound_quayfire",
         "deliver_mission_burial_word", "deliver_mission_alms_call",
         "deliver_call_the_priest"}) {
        INFO("expected template ", id);
        CHECK(templateNamed(id) != nullptr);
    }
}

TEST_CASE("a template with no pay authored is refused, not offered for a flat single coin") {
    // Regression: RadiantRaws::load()'s "REFUSED BY NAME" discipline dropped
    // a template with no verb, no brief, or (for a fetch) no goods -- but not
    // one that forgot payPerUnit (fetch) or payFlat (deliver). Both default
    // to 0, and refresh()'s own std::max(1, ...) floor turned that into a
    // job that silently paid one flat coin forever, regardless of units or
    // effort -- exactly the kind of job the file's own "not an objective"
    // rule is supposed to catch. Every one of the seventeen real templates
    // happens to author a positive pay (proved above), so this was silent in
    // the owner's own content.
    TempTree tree("no-pay-template");
    tree.write("raws/quests/radiant_quests.json", R"({"templates": [
        {"id": "no_pay_fetch", "kind": "fetch", "verb": "FETCH",
         "brief": "{giver} at {giverPlace} wants {units} {good} off {target} at {targetPlace}.",
         "giverTypes": ["shopkeeper"], "targetTypes": ["shopkeeper"],
         "goods": ["scalp"], "unitsMin": 1, "unitsMax": 1},
        {"id": "no_pay_deliver", "kind": "deliver", "verb": "CARRY WORD",
         "brief": "{giver} at {giverPlace} sends word to {target} at {targetPlace}.",
         "giverTypes": ["shopkeeper"], "targetTypes": ["shopkeeper"]},
        {"id": "paid_fetch", "kind": "fetch", "verb": "FETCH",
         "brief": "{giver} at {giverPlace} wants {units} {good} off {target} at {targetPlace}.",
         "giverTypes": ["shopkeeper"], "targetTypes": ["shopkeeper"],
         "goods": ["scalp"], "unitsMin": 1, "unitsMax": 1, "payPerUnit": 4}
    ]})");

    const RadiantRaws raws = RadiantRaws::load(tree.root());
    REQUIRE(raws.loaded());
    bool sawNoPayFetch = false;
    bool sawNoPayDeliver = false;
    bool sawPaidFetch = false;
    for (const RadiantTemplate& tmpl : raws.templates()) {
        sawNoPayFetch = sawNoPayFetch || tmpl.id == "no_pay_fetch";
        sawNoPayDeliver = sawNoPayDeliver || tmpl.id == "no_pay_deliver";
        sawPaidFetch = sawPaidFetch || tmpl.id == "paid_fetch";
    }
    CHECK_FALSE(sawNoPayFetch);
    CHECK_FALSE(sawNoPayDeliver);
    // ...and the refusal costs nothing else: a template that DOES author pay
    // still loads.
    CHECK(sawPaidFetch);
    REQUIRE(raws.templates().size() == 1);
}

TEST_CASE("the Skyrunner file's own templates carry the roof-runner cast, not the owner's") {
    REQUIRE(raws().loaded());

    // The four Skyrunner FETCH templates run through the roofs' own giver,
    // WardType::Thief -- the type ward_actors.hpp itself calls "cutpurse,
    // robber, roof-runner" and factions.json ties to villain.skyrunner --
    // except where the flow runs the other way.
    for (std::string_view id : {"fetch_roost_toll", "fetch_fence_reckoning",
                                "fetch_second_bounty", "fetch_flower_climb"}) {
        const RadiantTemplate* tmpl = templateNamed(id);
        REQUIRE(tmpl != nullptr);
        INFO("template ", id);
        CHECK(tmpl->kind == RadiantKind::Fetch);
        REQUIRE(tmpl->giverTypes.size() == 1);
        CHECK(tmpl->giverTypes.front() == WardType::Thief);
    }

    // deliver_roof_word: a roof-runner sends word out.
    const RadiantTemplate* roofWord = templateNamed("deliver_roof_word");
    REQUIRE(roofWord != nullptr);
    REQUIRE(roofWord->giverTypes.size() == 1);
    CHECK(roofWord->giverTypes.front() == WardType::Thief);

    // deliver_fence_summons: the street affiliates send word UP to a
    // roof-runner -- factions.json's own "rooftop brotherhood and its street
    // affiliates", the mirror direction.
    const RadiantTemplate* summons = templateNamed("deliver_fence_summons");
    REQUIRE(summons != nullptr);
    REQUIRE(summons->targetTypes.size() == 1);
    CHECK(summons->targetTypes.front() == WardType::Thief);
    for (const WardType type : summons->giverTypes) {
        CHECK((type == WardType::Wastrel || type == WardType::Urchin));
    }

    // Between fetch_fence_reckoning, fetch_second_bounty and fetch_flower_climb,
    // the Skyrunner file reaches every good radiant_quests.json's own three
    // fetch templates never once drew: moonshine and artifact.
    bool sawMoonshine = false;
    bool sawArtifact = false;
    for (std::string_view id : {"fetch_roost_toll", "fetch_fence_reckoning",
                                "fetch_second_bounty", "fetch_flower_climb"}) {
        const RadiantTemplate* tmpl = templateNamed(id);
        REQUIRE(tmpl != nullptr);
        for (const Contraband good : tmpl->goods) {
            sawMoonshine = sawMoonshine || good == Contraband::Moonshine;
            sawArtifact = sawArtifact || good == Contraband::Artifact;
        }
    }
    CHECK(sawMoonshine);
    CHECK(sawArtifact);
}

TEST_CASE("the flame file's own templates carry the Mission's cast, not the owner's") {
    REQUIRE(raws().loaded());

    // fetch_mission_scalps and fetch_mission_wound_quayfire both draw their
    // giver off the Mission -- WardType::PriestOfTheFlame or
    // WardType::DiscipleOfTheFlame, never anybody else.
    for (std::string_view id : {"fetch_mission_scalps", "fetch_mission_wound_quayfire"}) {
        const RadiantTemplate* tmpl = templateNamed(id);
        REQUIRE(tmpl != nullptr);
        INFO("template ", id);
        CHECK(tmpl->kind == RadiantKind::Fetch);
        for (const WardType type : tmpl->giverTypes) {
            CHECK((type == WardType::PriestOfTheFlame || type == WardType::DiscipleOfTheFlame));
        }
    }

    // deliver_mission_burial_word: either the priest or a disciple can bring
    // it -- Father Maell alone would leave this template's giver pool one
    // body wide (he is the ward's only priest), which is exactly the
    // "same two names forever" shape test_radiant_variety.cpp's own
    // giverIds >= 2 bar exists to catch.
    const RadiantTemplate* burial = templateNamed("deliver_mission_burial_word");
    REQUIRE(burial != nullptr);
    for (const WardType type : burial->giverTypes) {
        CHECK((type == WardType::PriestOfTheFlame || type == WardType::DiscipleOfTheFlame));
    }

    // deliver_mission_alms_call: a disciple sends it, never the priest.
    const RadiantTemplate* almsCall = templateNamed("deliver_mission_alms_call");
    REQUIRE(almsCall != nullptr);
    REQUIRE(almsCall->giverTypes.size() == 1);
    CHECK(almsCall->giverTypes.front() == WardType::DiscipleOfTheFlame);

    // deliver_call_the_priest runs the other direction -- an ordinary ward
    // body sends word FOR the Mission; the Mission never sends this one.
    const RadiantTemplate* callPriest = templateNamed("deliver_call_the_priest");
    REQUIRE(callPriest != nullptr);
    REQUIRE(callPriest->targetTypes.size() == 2);
    for (const WardType type : callPriest->targetTypes) {
        CHECK((type == WardType::PriestOfTheFlame || type == WardType::DiscipleOfTheFlame));
    }
    for (const WardType type : callPriest->giverTypes) {
        CHECK_FALSE(type == WardType::PriestOfTheFlame);
        CHECK_FALSE(type == WardType::DiscipleOfTheFlame);
    }

    // Between the two FETCH templates, the flame file reaches scalp (the
    // ward's one legal good, and the one thing DECISIONS.md gives the Church
    // an opinion about) and moonshine (quayfire, wanted here for what it
    // cleans rather than what it pours).
    bool sawScalp = false;
    bool sawMoonshine = false;
    for (std::string_view id : {"fetch_mission_scalps", "fetch_mission_wound_quayfire"}) {
        const RadiantTemplate* tmpl = templateNamed(id);
        REQUIRE(tmpl != nullptr);
        for (const Contraband good : tmpl->goods) {
            sawScalp = sawScalp || good == Contraband::Scalp;
            sawMoonshine = sawMoonshine || good == Contraband::Moonshine;
        }
    }
    CHECK(sawScalp);
    CHECK(sawMoonshine);

    // MAGIC-CANON.md section 5.4: every in-world surface says six, and the
    // word "seven" never appears at all -- checked here rather than trusted
    // from a comment, the same discipline test_letters.cpp already applies to
    // Maell's letters.
    for (std::string_view id : {"fetch_mission_scalps", "fetch_mission_wound_quayfire",
                                "deliver_mission_burial_word", "deliver_mission_alms_call",
                                "deliver_call_the_priest"}) {
        const RadiantTemplate* tmpl = templateNamed(id);
        REQUIRE(tmpl != nullptr);
        INFO("template ", id);
        CHECK(tmpl->brief.find("seven") == std::string::npos);
        CHECK(tmpl->brief.find("Seven") == std::string::npos);
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

// ===========================================================================
// RADIANT BUILD -- the state machine that makes the generator reachable
// ===========================================================================
//
// Everything above proves the board GENERATES honestly. Everything below
// proves it can be TAKEN and SETTLED -- the take/turn-in shape ContractBoard
// already proved, run through the radiant board's own verbs, because a
// generator nobody can reach is the gap this build exists to close.

TEST_CASE("an errand is taken once, refused twice, and capped at three in hand") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    RadiantBoard board;
    board.refresh(3, testfix::kSeed, raws(), ward);
    REQUIRE_FALSE(board.objectives().empty());

    const std::int32_t first = board.objectives().front().id;
    CHECK(board.take(first) == RadiantTakeResult::Taken);
    CHECK(board.find(first)->state == RadiantState::Taken);
    CHECK(board.takenCount() == 1);

    // Taken is taken. Asking again is answered, not crashed on.
    CHECK(board.take(first) == RadiantTakeResult::NotOffered);
    // And a job that was never posted is its own distinct refusal.
    CHECK(board.take(999999) == RadiantTakeResult::NoSuchObjective);

    // The three-in-hand cap, kMaxTakenRadiant's own number. The board posts
    // four a day, so a full day's board is enough to hit it.
    std::int32_t taken = 1;
    for (const RadiantObjective& row : board.objectives()) {
        if (row.id == first) {
            continue;
        }
        const RadiantTakeResult result = board.take(row.id);
        if (taken < kMaxTakenRadiant) {
            CHECK(result == RadiantTakeResult::Taken);
            ++taken;
        } else {
            CHECK(result == RadiantTakeResult::HandsFull);
        }
    }
    CHECK(board.takenCount() == kMaxTakenRadiant);
}

TEST_CASE("a fetch errand is settled out of the sack and pays; a deliver settles by arriving") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    RadiantBoard board;
    // Walk days until the board holds one of each kind -- both exist across
    // the first handful of days (the variety suite proves far more), so this
    // terminates fast and never fakes a row by hand.
    const RadiantObjective* fetch = nullptr;
    const RadiantObjective* deliver = nullptr;
    for (std::int32_t day = 1; day < 40 && (fetch == nullptr || deliver == nullptr); ++day) {
        board.refresh(day, testfix::kSeed, raws(), ward);
        fetch = nullptr;
        deliver = nullptr;
        for (const RadiantObjective& row : board.objectives()) {
            if (row.state != RadiantState::Offered) {
                continue;
            }
            if (row.isFetch() && fetch == nullptr) {
                fetch = &row;
            }
            if (!row.isFetch() && deliver == nullptr) {
                deliver = &row;
            }
        }
    }
    REQUIRE(fetch != nullptr);
    REQUIRE(deliver != nullptr);

    // THE FETCH. Not taken yet: nothing to settle.
    Stash sack;
    CHECK(board.turnIn(fetch->id, sack).result == RadiantTurnInResult::NotTaken);
    REQUIRE(board.take(fetch->id) == RadiantTakeResult::Taken);
    // Short-handed is answered by name, and nothing leaves the sack.
    CHECK(board.turnIn(fetch->id, sack).result == RadiantTurnInResult::Short);
    // With the goods actually carried, it pays the posted pay and the goods
    // leave through the stash's own verb.
    REQUIRE(sack.add(fetch->good, fetch->units) == fetch->units);
    const RadiantSettlement paid = board.turnIn(fetch->id, sack);
    CHECK(paid.result == RadiantTurnInResult::Paid);
    CHECK(paid.pay == fetch->pay);
    CHECK(paid.unitsTaken == fetch->units);
    CHECK(sack.count(fetch->good) == 0);
    CHECK(board.find(fetch->id)->state == RadiantState::Paid);
    // Paid is paid: a second settlement is refused, not double-paid.
    CHECK(board.turnIn(fetch->id, sack).result == RadiantTurnInResult::NotTaken);

    // THE DELIVER. The wrong verb is a named refusal on both sides.
    CHECK(board.turnIn(deliver->id, sack).result == RadiantTurnInResult::WrongKind);
    REQUIRE(board.take(deliver->id) == RadiantTakeResult::Taken);
    CHECK(board.deliver(fetch->id).result == RadiantTurnInResult::WrongKind);
    const RadiantSettlement arrived = board.deliver(deliver->id);
    CHECK(arrived.result == RadiantTurnInResult::Paid);
    CHECK(arrived.pay == deliver->pay);
    CHECK(arrived.unitsTaken == 0);
    CHECK(board.find(deliver->id)->state == RadiantState::Paid);

    // The record of what the day's work earned.
    CHECK(board.paidCount() == 2);
    CHECK(board.coinEarned() == fetch->pay + deliver->pay);
}

TEST_CASE("a taken errand survives the day turning; offered and paid rows are swept") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    RadiantBoard board;
    board.refresh(3, testfix::kSeed, raws(), ward);
    REQUIRE(board.objectives().size() >= 2);

    const std::int32_t held = board.objectives().front().id;
    const std::string heldBrief = board.objectives().front().brief;
    REQUIRE(board.take(held) == RadiantTakeResult::Taken);

    board.refresh(4, testfix::kSeed, raws(), ward);
    // The errand somebody is out walking is still on the board, word for
    // word, alongside the new day's offers.
    const RadiantObjective* carried = board.find(held);
    REQUIRE(carried != nullptr);
    CHECK(carried->state == RadiantState::Taken);
    CHECK(carried->brief == heldBrief);
    // And yesterday's untaken offers are gone: every other row is day 4's.
    for (const RadiantObjective& row : board.objectives()) {
        if (row.id == held) {
            continue;
        }
        CHECK(row.postedOnDay == 4);
        CHECK(row.state == RadiantState::Offered);
    }
}

TEST_CASE("which errands were taken is state the hash can see") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    RadiantBoard untouched;
    RadiantBoard moved;
    untouched.refresh(3, testfix::kSeed, raws(), ward);
    moved.refresh(3, testfix::kSeed, raws(), ward);
    REQUIRE_FALSE(moved.objectives().empty());
    REQUIRE(moved.take(moved.objectives().front().id) == RadiantTakeResult::Taken);

    HashSink still(777);
    untouched.hashInto(still);
    HashSink taken(777);
    moved.hashInto(taken);
    // The same board, one take apart, must not hash the same -- a take the
    // twin-run gate could not see would be a take the gate does not protect.
    CHECK(still.finished() != taken.finished());
}

// ===========================================================================
// RADIANT BUILD -- reachability: the board behind a conversation
// ===========================================================================
//
// The whole point of the build: the same board, reached the only way a player
// can reach anything social in this game -- a topic list built by the real
// DialogueDirector for the real body the generator drew.

#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/ward_voice.hpp"

namespace {

/// The real director over the real content tree, with today's errands posted
/// off the shared ward. Fresh per call: these cases MUTATE boards and purses.
granadad::sim::DialogueDirector directorWithErrands(const WardPopulation& ward,
                                                    std::int32_t day) {
    granadad::sim::DialogueDirector talk =
        granadad::sim::DialogueDirector::load(content::contentDir());
    talk.postRadiant(day, testfix::kSeed, ward);
    return talk;
}

/// The Speaker a Session would hand the director for this ward body -- built
/// by the REAL wardSpeakerFor, so the id lift and the notable resolution are
/// the production path and not this file having its own idea of them.
Speaker wardSpeaker(const WardPopulation& ward, std::int32_t actorId,
                    const granadad::sim::DialogueDirector& talk) {
    const WardActor* actor = ward.byId(actorId);
    REQUIRE(actor != nullptr);
    return wardSpeakerFor(*actor, ward.identity(actorId), talk.notables(), talk.factions(),
                          talk.barks());
}

/// The index of the topic carrying (kind, payload), or -1.
std::int32_t topicIndexOf(const granadad::sim::DialogueDirector& talk, TopicKind kind,
                          std::int32_t payload) {
    const std::vector<Topic>& topics = talk.topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == kind && topics[i].payload == payload) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("an errand is offered by its own giver, taken across the table, and settled there") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    // A day whose board holds a FETCH -- walked for, not assumed, exactly as
    // the board suite above does.
    for (std::int32_t day = 1; day < 40; ++day) {
        granadad::sim::DialogueDirector talk = directorWithErrands(ward, day);
        const RadiantObjective* fetch = nullptr;
        for (const RadiantObjective& row : talk.radiant().objectives()) {
            if (row.isFetch()) {
                fetch = &row;
                break;
            }
        }
        if (fetch == nullptr) {
            continue;
        }
        const std::int32_t id = fetch->id;
        const std::int32_t pay = fetch->pay;
        const std::int32_t units = fetch->units;
        const Contraband good = fetch->good;
        const std::int32_t giverId = fetch->giverActorId;
        const std::string brief = fetch->brief;

        // THE GIVER OFFERS IT. Their own topic list, their own label.
        REQUIRE(talk.open(wardSpeaker(ward, giverId, talk), hourOfDay(8)));
        const std::int32_t offer = topicIndexOf(talk, TopicKind::TakeRadiant, id);
        REQUIRE(offer >= 0);

        // TAKEN: the reply carries the brief the journal will show, and the
        // board row is genuinely Taken.
        const Reply took = talk.choose(static_cast<std::size_t>(offer));
        CHECK(took.ok);
        CHECK(took.radiantId == id);
        CHECK(took.journalLine == brief);
        REQUIRE(talk.radiant().find(id) != nullptr);
        CHECK(talk.radiant().find(id)->state == RadiantState::Taken);
        // The list the player is looking at rebuilt under the press: the
        // offer row is gone, the settlement row is up.
        CHECK(topicIndexOf(talk, TopicKind::TakeRadiant, id) < 0);
        CHECK(topicIndexOf(talk, TopicKind::SettleRadiant, id) >= 0);

        // SHORT-HANDED IS AN ANSWER, NOT A PAYDAY.
        const std::int32_t shortIndex = topicIndexOf(talk, TopicKind::SettleRadiant, id);
        const Reply refused = talk.choose(static_cast<std::size_t>(shortIndex));
        CHECK_FALSE(refused.ok);
        CHECK(refused.coinDelta == 0);
        CHECK(talk.radiant().find(id)->state == RadiantState::Taken);

        // WITH THE GOODS CARRIED, IT PAYS -- through the same stash a
        // contract pays out of.
        REQUIRE(talk.crimes().stash().add(good, units) == units);
        const std::int32_t settleIndex = topicIndexOf(talk, TopicKind::SettleRadiant, id);
        REQUIRE(settleIndex >= 0);
        const Reply paid = talk.choose(static_cast<std::size_t>(settleIndex));
        CHECK(paid.ok);
        CHECK(paid.coinDelta == pay);
        CHECK(paid.radiantId == id);
        CHECK(talk.radiant().find(id)->state == RadiantState::Paid);
        CHECK(talk.crimes().stash().count(good) == 0);
        // And the settled row removed itself from the open list.
        CHECK(topicIndexOf(talk, TopicKind::SettleRadiant, id) < 0);
        talk.close();
        return;
    }
    FAIL("no fetch errand on any board in forty days -- the raws have changed shape");
}

TEST_CASE("a deliver errand settles at its TARGET, and the wrong party is refused") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    for (std::int32_t day = 1; day < 40; ++day) {
        granadad::sim::DialogueDirector talk = directorWithErrands(ward, day);
        const RadiantObjective* word = nullptr;
        for (const RadiantObjective& row : talk.radiant().objectives()) {
            if (!row.isFetch()) {
                word = &row;
                break;
            }
        }
        if (word == nullptr) {
            continue;
        }
        const std::int32_t id = word->id;
        const std::int32_t pay = word->pay;
        const std::int32_t giverId = word->giverActorId;
        const std::int32_t targetId = word->targetActorId;

        // Taken at the giver's side.
        REQUIRE(talk.open(wardSpeaker(ward, giverId, talk), hourOfDay(8)));
        const std::int32_t offer = topicIndexOf(talk, TopicKind::TakeRadiant, id);
        REQUIRE(offer >= 0);
        CHECK(talk.choose(static_cast<std::size_t>(offer)).ok);
        // The GIVER shows no settlement row for a deliver: the word is not
        // for them, and a courier who never left the doorstep earns nothing.
        CHECK(topicIndexOf(talk, TopicKind::SettleRadiant, id) < 0);
        talk.close();

        // Settled at the target's side, and the word pays where it lands.
        REQUIRE(talk.open(wardSpeaker(ward, targetId, talk), hourOfDay(8)));
        const std::int32_t arrive = topicIndexOf(talk, TopicKind::SettleRadiant, id);
        REQUIRE(arrive >= 0);
        const Reply landed = talk.choose(static_cast<std::size_t>(arrive));
        CHECK(landed.ok);
        CHECK(landed.coinDelta == pay);
        CHECK(talk.radiant().find(id)->state == RadiantState::Paid);
        talk.close();
        return;
    }
    FAIL("no deliver errand on any board in forty days -- the raws have changed shape");
}

TEST_CASE("errands are the giver's alone: strangers, the Gull's fourteen, and a soured tone") {
    const WardPopulation& ward = testfix::wardAt(8, 0);
    granadad::sim::DialogueDirector talk = directorWithErrands(ward, 3);
    REQUIRE_FALSE(talk.radiant().objectives().empty());
    const RadiantObjective& row = talk.radiant().objectives().front();

    // A ward body who is NOT the giver never offers it. The target is a
    // guaranteed such body (never the giver -- the generator's own rule).
    REQUIRE(talk.open(wardSpeaker(ward, row.targetActorId, talk), hourOfDay(8)));
    CHECK(topicIndexOf(talk, TopicKind::TakeRadiant, row.id) < 0);
    talk.close();

    // One of the Gull's fourteen never does either, whatever their id: the
    // two id spaces are kept apart by kWardSpeakerIdBase and a tavern
    // speaker's raw id must never collide into a board row. The bartender's
    // shape, id'd exactly where a giver id COULD sit.
    Speaker keeper;
    keeper.actorId = row.giverActorId;  // the collision this guards against
    keeper.name = "Keeper Fenner";
    keeper.family = JobFamily::Trade;
    REQUIRE(talk.open(keeper, hourOfDay(20)));
    for (const Topic& topic : talk.topics()) {
        CHECK(topic.kind != TopicKind::TakeRadiant);
        CHECK(topic.kind != TopicKind::SettleRadiant);
    }
    talk.close();

    // And the #84 answer, pinned: a soured register hides the OFFER -- asking
    // a favour of somebody you are being ugly to is not a thing -- while
    // Normal tone on a stranger shows it.
    REQUIRE(talk.open(wardSpeaker(ward, row.giverActorId, talk), hourOfDay(8)));
    CHECK(topicIndexOf(talk, TopicKind::TakeRadiant, row.id) >= 0);
    talk.setTone(Tone::Blunt);
    CHECK(topicIndexOf(talk, TopicKind::TakeRadiant, row.id) < 0);
    talk.setTone(Tone::Normal);
    CHECK(topicIndexOf(talk, TopicKind::TakeRadiant, row.id) >= 0);
    talk.close();
}

// ===========================================================================
// ERRANDS A HUMAN CAN FINISH (2026-10-09): the brief's English
// ===========================================================================

namespace {

/// Every label docks::placeLabelAt can answer with: the authored places and
/// the band fallbacks, the latter asked of the bands the ward actually uses.
std::vector<std::string> everyPlaceLabel() {
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < docks::kPlaceCount; ++i) {
        labels.emplace_back(docks::kPlaces[i].name);
    }
    for (const std::int32_t band : {docks::kBandQuayside, docks::kBandMidSlope, docks::kBandUpper,
                                    docks::kBandQuayside - 1, docks::kBandUpper + 5}) {
        labels.emplace_back(docks::bandFallbackLabel(band));
    }
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}  // namespace

TEST_CASE("every label the ward can name has its own two shapes, never the HUD's shout") {
    // Pinned whole: the prose IS the feature, and a label added to kPlaces
    // without a row here fails the coverage check below rather than falling
    // back to title case in a sentence nobody proofread.
    struct Want {
        const char* label;
        const char* bare;
        const char* at;
    };
    const Want wants[] = {
        {"THE GILDED GULL", "the Gilded Gull", "in the Gilded Gull"},
        {"THE GILDED GULL - ROOMS", "the rooms over the Gilded Gull",
         "in the rooms over the Gilded Gull"},
        {"TARWALK", "the Tarwalk", "on the Tarwalk"},
        {"ROPEWYND", "the Ropewynd", "on the Ropewynd"},
        {"ROPEWYND - THE DIRT END", "the dirt end of the Ropewynd",
         "at the dirt end of the Ropewynd"},
        {"SALTGATE RISE", "Saltgate Rise", "on Saltgate Rise"},
        {"GALLOWS ROW", "Gallows Row", "on Gallows Row"},
        {"THE LONG PIERS", "the Long Piers", "out on the Long Piers"},
        {"THE DOCKS - QUAYSIDE", "the quayside", "down on the quayside"},
        {"THE DOCKS - MID SLOPE", "the lanes up the slope", "in the lanes up the slope"},
        {"THE DOCKS - UPPER", "the upper ward", "in the upper ward"},
        {"UNDER THE PIERS", "the footings under the piers", "down under the piers"},
        {"THE DOCKS", "the Docks", "somewhere in the Docks"},
    };
    for (const Want& want : wants) {
        INFO(want.label);
        const PlacePhrase place = radiantPlacePhrase(want.label);
        CHECK(place.bare == want.bare);
        CHECK(place.at == want.at);
    }
    // COVERAGE: every label placeLabelAt can actually answer with has a row.
    for (const std::string& label : everyPlaceLabel()) {
        INFO(label);
        bool pinned = false;
        for (const Want& want : wants) {
            pinned = pinned || label == want.label;
        }
        CHECK(pinned);
    }
    // A label outside the closed set still reads as English.
    const PlacePhrase odd = radiantPlacePhrase("THE WEIGHHOUSE");
    CHECK(odd.bare == "the Weighhouse");
    CHECK(odd.at == "at the Weighhouse");
}

TEST_CASE("every authored brief reads as English in every place the ward can name") {
    // The bug this pins, verbatim off a capture: "Old Cobb keeps the kennel
    // the Docks - Mid Slope". Every template in every shipped file, composed
    // against every place label in both seats, must come out with no token,
    // no HUD dash, and no preposition stacked on a locative.
    REQUIRE(raws().loaded());
    const std::vector<std::string> labels = everyPlaceLabel();
    const char* const stacked[] = {"near on ",  "near in ",   "near at ",  "near out on ",
                                   "near down ", "to on ",    "to in ",    "to at ",
                                   "of on ",    "of in ",     "of at ",    "before on ",
                                   "before in ", "leaving on ", "leaving in ", "sent on ",
                                   "sent in ",  "seen near on ", "  ",     " ,"};
    std::size_t composed = 0;
    for (const RadiantTemplate& tmpl : raws().templates()) {
        for (const std::string& giverPlace : labels) {
            for (const std::string& targetPlace : labels) {
                const std::string brief =
                    composeRadiantBrief(tmpl.brief, "Old Cobb", giverPlace, "Kessa Inkwater",
                                        targetPlace, 3, tmpl.kind == RadiantKind::Fetch ? "dust" : "");
                INFO(tmpl.id << " | " << giverPlace << " | " << targetPlace << " | " << brief);
                CHECK(brief.find('{') == std::string::npos);
                CHECK(brief.find(" - ") == std::string::npos);
                const std::string folded = lower(brief);
                for (const char* bad : stacked) {
                    CHECK(folded.find(bad) == std::string::npos);
                }
                // "the the" is the other way a place goes in twice.
                CHECK(folded.find("the the ") == std::string::npos);
                ++composed;
            }
        }
    }
    CHECK(composed > 0);
}

TEST_CASE("the kennel brief that shipped broken reads as a sentence now") {
    const RadiantTemplate* kennel = templateNamed("fetch_kennel_arrears");
    REQUIRE(kennel != nullptr);
    CHECK(composeRadiantBrief(kennel->brief, "Old Cobb", "THE DOCKS - MID SLOPE",
                              "Kessa Inkwater", "TARWALK", 3, "dust") ==
          "Old Cobb keeps the kennel in the lanes up the slope, and the kennel is behind on 3 "
          "dust. Kessa Inkwater, on the Tarwalk, has been asked before and has not answered.");
    // The same place in both seats of one sentence: bare after "near",
    // locative standing alone.
    const RadiantTemplate* word = templateNamed("deliver_counter_word");
    REQUIRE(word != nullptr);
    const std::string brief = composeRadiantBrief(word->brief, "Old Cobb", "SALTGATE RISE",
                                                  "Kessa Inkwater", "SALTGATE RISE", 0, "");
    CHECK(brief.find("Old Cobb, on Saltgate Rise,") != std::string::npos);
    CHECK(brief.find("near Saltgate Rise") != std::string::npos);
}

TEST_CASE("a sending verb takes its place with 'to', a leaving verb takes it bare") {
    CHECK(composeRadiantBrief("{giver} has been sent {giverPlace} tonight.", "Finch", "TARWALK",
                              "", "", 0, "") == "Finch has been sent to the Tarwalk tonight.");
    CHECK(composeRadiantBrief("{giver} cannot be seen leaving {giverPlace}.", "Finch",
                              "THE GILDED GULL", "", "", 0, "") ==
          "Finch cannot be seen leaving the Gilded Gull.");
    CHECK(composeRadiantBrief("{target}, last seen {targetPlace}, waits.", "", "", "Onna",
                              "THE LONG PIERS", 0, "") ==
          "Onna, last seen out on the Long Piers, waits.");
}

TEST_CASE("anybody in the ward can say who needs a hand, and naming it moves nothing") {
    // ERRANDS A HUMAN CAN FINISH (2026-10-09). The board's givers are four
    // bodies among six hundred; the A QUEST branch is where a stranger tells
    // you which one is asking, and where.
    const WardPopulation& ward = testfix::wardAt(8, 0);
    granadad::sim::DialogueDirector talk = directorWithErrands(ward, 3);
    REQUIRE_FALSE(talk.radiant().objectives().empty());
    // A body who gives nothing today.
    std::int32_t asked = -1;
    for (const WardActor& actor : ward.actors()) {
        bool gives = false;
        for (const RadiantObjective& row : talk.radiant().objectives()) {
            gives = gives || row.giverActorId == actor.id || row.targetActorId == actor.id;
        }
        if (!gives && isPerson(actor.type)) {
            asked = actor.id;
            break;
        }
    }
    REQUIRE(asked >= 0);
    REQUIRE(talk.open(wardSpeaker(ward, asked, talk), hourOfDay(8)));
    // Through the tree's own doors: TELL ME ABOUT, then A QUEST.
    std::int32_t ask = -1;
    for (std::size_t i = 0; i < talk.topics().size(); ++i) {
        if (talk.topics()[i].kind == TopicKind::Ask) {
            ask = static_cast<std::int32_t>(i);
        }
    }
    REQUIRE(ask >= 0);
    (void)talk.choose(static_cast<std::size_t>(ask));
    std::int32_t questBranch = -1;
    for (std::size_t i = 0; i < talk.topics().size(); ++i) {
        if (talk.topics()[i].kind == TopicKind::Category && talk.topics()[i].arg == kAskQuest) {
            questBranch = static_cast<std::int32_t>(i);
        }
    }
    REQUIRE(questBranch >= 0);
    (void)talk.choose(static_cast<std::size_t>(questBranch));
    const std::int32_t hand = topicIndexOf(talk, TopicKind::AskErrand, 0);
    REQUIRE(hand >= 0);
    CHECK(talk.topics()[static_cast<std::size_t>(hand)].label == "WHO NEEDS A HAND?");

    const RadiantObjective* named = talk.errandToName();
    REQUIRE(named != nullptr);
    CHECK(named->state == RadiantState::Offered);
    const std::string giver = [&] {
        std::string out = named->giverName;
        for (char& c : out) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return out;
    }();
    HashSink before(4242);
    talk.hashInto(before);
    const Reply answer = talk.choose(static_cast<std::size_t>(hand));
    INFO(answer.line);
    CHECK(answer.ok);
    CHECK(answer.line.find(giver + ", ") == 0);
    CHECK(answer.line.find("WANTS SOMEBODY TO") != std::string::npos);
    CHECK(answer.line.find(" - ") == std::string::npos);
    CHECK(answer.coinDelta == 0);
    // Asking is free and changes nothing the gate can see.
    HashSink after(4242);
    talk.hashInto(after);
    CHECK(before.finished() == after.finished());
    // The same person names the same errand all day.
    CHECK(talk.errandToName() == named);
}
