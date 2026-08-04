// The six criminal acts, what the Watch remembers, and the Skyrunner line
// played end to end in the room it happens in.
//
// Three kinds of case, in the same order test_faction.cpp uses:
//
//   RAWS   the S5 quest file and the vocabulary it is authored against. A
//          stage counting something nothing ever counts is a stage that cannot
//          be finished, and that is the ONE failure this schema exists to
//          prevent, so it is checked rather than promised.
//   RULES  the crime ledger on its own -- heat, the warrant's hysteresis, the
//          fence's rate, the byte codec -- with no world anywhere near it.
//   ROOM   all of it in the Gilded Gull: a hand, a box, a bale past a
//          watchman, a fence, a lean, and the nine-stage line finished.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const QuestBook& book() {
    static const QuestBook loaded = QuestBook::load(content::contentDir());
    return loaded;
}

/// The Gull, an engine and a body -- the same shape test_faction.cpp uses.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         std::int32_t band = gull::kGroundBand)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(0x4752414E41444144ull, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, band, kFacingSouth)) {
        body_->setLandingFloor(docks::kLandingFloor);
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, 0x4752414E41444144ull,
                                               content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] PlayerBody& body() noexcept { return *body_; }
    [[nodiscard]] const TileQuery& tiles() const noexcept { return *tiles_; }

    void run(int seconds) {
        for (int i = 0; i < seconds; ++i) {
            engine_->tick();
        }
    }

    /// Stands the player exactly where an actor is standing: distance zero
    /// beats every tie-break there is.
    [[nodiscard]] const Actor* standBy(std::string_view name) {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.name() == name && actor.present()) {
                tavern_->setPlayer(q8_tile_centre(actor.tileX()), q8_tile_centre(actor.tileY()),
                                   actor.band());
                return tavern_->actorById(actor.id());
            }
        }
        return nullptr;
    }

    void standAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

    [[nodiscard]] int topicOfKind(TopicKind kind) const {
        const std::vector<Topic>& topics = tavern_->dialogue().topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind == kind) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool pick(TopicKind kind) {
        const int at = topicOfKind(kind);
        if (at < 0) {
            return false;
        }
        tavern_->chooseTopic(static_cast<std::size_t>(at));
        return true;
    }

private:
    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
};

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("the Skyrunner line is authored against a vocabulary that can finish it") {
    const Questline* line = book().find("skyrunner-tenant");
    REQUIRE(line != nullptr);
    CHECK(line->faction == "skyrunners");
    CHECK(line->giver == "finch");
    // Nine stages, and the point of them is COVERAGE: a player who finishes
    // this line has done every criminal act the ward has, once each.
    REQUIRE(line->stages.size() == 9);

    // Every counted stage names a counter, and every counter it names is one
    // something in this build actually produces. A stage counting a string
    // nothing ever emits is a quest that cannot be finished, which is worse
    // than authoring none -- so it is checked here rather than trusted.
    std::vector<std::string> emitted;
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        emitted.emplace_back(crimeTally(static_cast<Crime>(i)));
    }
    // The two the BODY emits rather than the crime vocabulary: a mantle and a
    // leap are movement, not offences, and Session::settleLanding tallies them.
    emitted.emplace_back("climbs");
    emitted.emplace_back("leaps");
    emitted.emplace_back("drinks");

    std::int32_t counted = 0;
    for (const QuestStage& stage : line->stages) {
        CHECK_FALSE(stage.key.empty());
        CHECK_FALSE(stage.label.empty());
        CHECK_FALSE(stage.objective.empty());
        CHECK_FALSE(stage.log.empty());
        CHECK_FALSE(stage.barkKey.empty());
        CHECK(stage.party == "finch");
        if (stage.kind == StageKind::Tally || stage.kind == StageKind::Alms) {
            ++counted;
            REQUIRE_FALSE(stage.counter.empty());
            INFO("counter ", stage.counter);
            REQUIRE(std::find(emitted.begin(), emitted.end(), stage.counter) != emitted.end());
            CHECK(stage.count > 0);
        }
    }
    // Six of the nine are counted acts; the other three are the oath, and the
    // two beats you stand in front of him for.
    CHECK(counted == 7);
    CHECK(line->stages.back().terminal);
}

TEST_CASE("an alms stage still counts drinks without saying so, and the Mission's line is untouched") {
    // `tally` is `alms` with its counter named. The owner's S4 line has no
    // `counter` field at all, and it must keep meaning exactly what it meant.
    const Questline* flame = book().find("flame-disciple");
    REQUIRE(flame != nullptr);
    bool sawAlms = false;
    for (const QuestStage& stage : flame->stages) {
        if (stage.kind == StageKind::Alms) {
            sawAlms = true;
            CHECK(stage.counter == "drinks");
        }
    }
    CHECK(sawAlms);
    CHECK(stageKindOf("tally") == StageKind::Tally);
    CHECK(stageKindOf("alms") == StageKind::Alms);
    CHECK(stageKindName(StageKind::Tally) == "tally");
}

// ===========================================================================
// RULES
// ===========================================================================

TEST_CASE("a counted stage refuses to be turned in until it has been done") {
    // THE ONE THING A COUNTED STAGE IS FOR. S5's own scripted capture found
    // this: `tally` was added beside `alms` and the guard in
    // DialogueDirector::choose still named only `alms`, so every Skyrunner
    // stage advanced whether the act had been done or not -- a nine-stage line
    // finishable by pressing one key nine times.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();

    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Join));
    REQUIRE(talk.journal().stagesDone("skyrunner-tenant") == 1);

    // Stage two wants two purses and has had none.
    REQUIRE(room.topicOfKind(TopicKind::QuestBeat) >= 0);
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 1);
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 1);

    // One is not two either.
    talk.noteTally("lifts");
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 1);

    talk.noteTally("lifts");
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 2);

    // And a tally the CURRENT stage does not count moves nothing: the counter
    // is per stage, so the roofs cannot be farmed ahead of being asked.
    const std::int32_t at = talk.journal().counter("skyrunner-tenant");
    talk.noteTally("leaps");
    CHECK(talk.journal().counter("skyrunner-tenant") == at);
}

TEST_CASE("heat is what the Watch HEARD, and an act nobody saw raises none of it") {
    CrimeLedger ledger;
    CHECK(ledger.heat() == 0);
    ledger.commit(Crime::Burgle, false);
    CHECK(ledger.tally(Crime::Burgle) == 1);
    CHECK(ledger.crimesCommitted() == 1);
    CHECK(ledger.heat() == 0);

    ledger.commit(Crime::Burgle, true);
    CHECK(ledger.tally(Crime::Burgle) == 2);
    CHECK(ledger.heat() == crimeHeat(Crime::Burgle));

    // Every act has a cost and a worth, and being on a roof is worth more to
    // the roofs than it costs with the law.
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        const Crime crime = static_cast<Crime>(i);
        REQUIRE(crimeHeat(crime) >= 0);
        REQUIRE(crimeStanding(crime) > 0);
        REQUIRE_FALSE(crimeName(crime).empty());
        REQUIRE_FALSE(crimeTally(crime).empty());
    }
    CHECK(crimeStanding(Crime::RoofRun) > crimeHeat(Crime::RoofRun));
    CHECK(crimeHeat(Crime::Smuggle) > crimeHeat(Crime::Lift));
}

TEST_CASE("a warrant is issued high and lapses low, so one cooled point cannot flicker it") {
    CrimeLedger ledger;
    ledger.addHeat(kWarrantAt - 1);
    CHECK_FALSE(ledger.warrant());
    ledger.addHeat(1);
    CHECK(ledger.warrant());
    // Still wanted well below where it was issued. A single threshold would
    // turn the paper on and off every five minutes.
    ledger.addHeat(-(kWarrantAt - kWarrantLapsesAt - 1));
    CHECK(ledger.heat() > kWarrantLapsesAt);
    CHECK(ledger.warrant());
    ledger.addHeat(-(ledger.heat() - kWarrantLapsesAt + 1));
    CHECK_FALSE(ledger.warrant());
    // And nothing ever leaves the scale.
    ledger.addHeat(1000);
    CHECK(ledger.heat() == kHeatMax);
    ledger.addHeat(-1000);
    CHECK(ledger.heat() == 0);
}

TEST_CASE("the ward forgets at one rate whether it is watched or slept through") {
    CrimeLedger watched;
    CrimeLedger slept;
    watched.addHeat(50);
    slept.addHeat(50);

    // Two hours, one second at a time.
    for (std::int64_t t = 1; t <= 2 * 3600; ++t) {
        watched.cool(t);
    }
    // And two hours in one jump, which is what sleeping in a rented bed does.
    slept.cool(2 * 3600);
    CHECK(watched.heat() == slept.heat());
    CHECK(watched.heat() == 50 - (2 * 3600) / kHeatCoolSeconds);

    // The remainder is still owed rather than thrown away: a clock read every
    // second must not round a point off every time it is asked.
    CrimeLedger dribble;
    dribble.addHeat(10);
    for (std::int64_t t = 1; t <= kHeatCoolSeconds - 1; ++t) {
        dribble.cool(t);
    }
    CHECK(dribble.heat() == 10);
    dribble.cool(kHeatCoolSeconds);
    CHECK(dribble.heat() == 9);
}

TEST_CASE("a fence pays a share, and the rung is what moves it") {
    const std::int32_t stranger = fenceRatePercent(0, 0);
    const std::int32_t robber = fenceRatePercent(3, 40);
    const std::int32_t top = fenceRatePercent(4, 100);
    CHECK(stranger == kFenceBaseRate);
    CHECK(robber > stranger);
    CHECK(top > robber);
    // Clamped at both ends, so no rung ever makes stolen goods worth more than
    // honest ones and no grudge ever makes them worthless.
    for (std::int32_t rank = 0; rank <= 8; ++rank) {
        for (std::int32_t standing = -100; standing <= 100; standing += 10) {
            const std::int32_t rate = fenceRatePercent(rank, standing);
            REQUIRE(rate >= kFenceRateFloor);
            REQUIRE(rate <= kFenceRateCeiling);
        }
    }

    CrimeLedger ledger;
    ledger.takeLoot(3);
    CHECK(ledger.loot() == 3);
    // Selling more than you have sells what you have.
    const std::int32_t paid = ledger.sellLoot(10, 50);
    CHECK(ledger.loot() == 0);
    CHECK(paid == 3 * kLootValue * 50 / 100);
    CHECK(ledger.sellLoot(1, 50) == 0);
}

TEST_CASE("going to ground and losing the file are two different favours") {
    CrimeLedger ground;
    ground.addHeat(80);
    REQUIRE(ground.warrant());
    ground.lieLow();
    CHECK(ground.heat() == 0);
    CHECK_FALSE(ground.warrant());
    CHECK(ground.timesLaidLow() == 1);

    CrimeLedger file;
    file.addHeat(80);
    REQUIRE(file.warrant());
    file.quashWarrant();
    // The paper goes. What the ward SAW, it still saw.
    CHECK_FALSE(file.warrant());
    CHECK(file.heat() == 80);
}

TEST_CASE("the crime ledger round-trips through its own bytes") {
    CrimeLedger before;
    before.commit(Crime::Lift, true);
    before.commit(Crime::Lift, false);
    before.commit(Crime::Smuggle, true);
    before.commit(Crime::RoofRun, false);
    before.takeLoot(4);
    before.takeBale();
    before.cool(4 * kHeatCoolSeconds);

    const std::vector<std::uint8_t> bytes = before.encode();
    CrimeLedger after;
    REQUIRE(CrimeLedger::decode(bytes, after));
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        REQUIRE(after.tally(static_cast<Crime>(i)) == before.tally(static_cast<Crime>(i)));
    }
    CHECK(after.heat() == before.heat());
    CHECK(after.loot() == before.loot());
    CHECK(after.carryingBale() == before.carryingBale());
    CHECK(after.warrant() == before.warrant());

    // A truncated or foreign blob is refused rather than reinterpreted.
    CrimeLedger wrecked;
    CHECK_FALSE(CrimeLedger::decode({}, wrecked));
    std::vector<std::uint8_t> bent = bytes;
    bent[2] = 99;
    CHECK_FALSE(CrimeLedger::decode(bent, wrecked));
    bent = bytes;
    bent.resize(6);
    CHECK_FALSE(CrimeLedger::decode(bent, wrecked));
}

// ===========================================================================
// ROOM
// ===========================================================================

TEST_CASE("a crime moves the tally, the heat and BOTH sides of the mirror at once") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    DialogueDirector& talk = room.tavern().dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t watch = talk.factions().indexOf("watch");
    const std::int32_t roofsBefore = talk.standings().standing(roofs);
    const std::int32_t watchBefore = talk.standings().standing(watch);

    talk.noteCrime(Crime::Burgle, true);

    CHECK(talk.crimes().tally(Crime::Burgle) == 1);
    CHECK(talk.crimes().heat() == crimeHeat(Crime::Burgle));
    // THE MIRROR, driven by an act rather than by a conversation. This is the
    // first thing in the build that moves a faction number with nobody talking.
    CHECK(talk.standings().standing(roofs) > roofsBefore);
    CHECK(talk.standings().standing(watch) < watchBefore);
}

TEST_CASE("a box above the stair is cracked, and your own is not a crime") {
    Room room(hourOfDay(23), gull::kRooms[1].standX, gull::kRooms[1].standY, gull::kUpperBand);
    Tavern& gull = room.tavern();
    const std::int32_t coinBefore = gull.playerCoin();

    const Tavern::StealResult took = gull.crackStrongbox();
    REQUIRE(took.result == ServiceResult::Served);
    CHECK(took.coin >= kStrongboxCoin);
    CHECK(took.loot >= 1);
    CHECK(gull.playerCoin() == coinBefore + took.coin);
    CHECK(gull.dialogue().crimes().tally(Crime::Burgle) == 1);
    CHECK(gull.dialogue().crimes().loot() == took.loot);

    // Once. A box is emptied, not a faucet.
    CHECK(gull.crackStrongbox().result == ServiceResult::OutOfStock);
    CHECK(gull.dialogue().crimes().tally(Crime::Burgle) == 1);

    // From the taproom there is nothing to open at all.
    room.standAt(gull::kRooms[1].standX, gull::kRooms[1].standY, gull::kGroundBand);
    CHECK(gull.crackStrongbox().result == ServiceResult::TooFar);
}

TEST_CASE("nobody hands a stranger a bale, and carrying one out past the law is what pays") {
    Room room(hourOfDay(23), gull::kBaleX, gull::kBaleY);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();

    // Not on the roll: the bale is not yours to pick up.
    CHECK(gull.handleBale().result == ServiceResult::Refused);
    CHECK_FALSE(talk.crimes().carryingBale());

    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    talk.standings().addStanding(roofs, 40);
    REQUIRE(talk.standings().join(roofs, talk.skills()) == LadderResult::Granted);

    REQUIRE(gull.handleBale().result == ServiceResult::Served);
    CHECK(talk.crimes().carryingBale());

    const std::int32_t coinBefore = gull.playerCoin();
    // Standing still inside with it is not a run.
    gull.stepMovement();
    CHECK(talk.crimes().tally(Crime::Smuggle) == 0);

    // Out through the door. The RUN is the crossing, and nothing else is.
    room.standAt(gull::kStreetX, gull::kStreetY, gull::kGroundBand);
    gull.stepMovement();
    CHECK(talk.crimes().tally(Crime::Smuggle) == 1);
    CHECK_FALSE(talk.crimes().carryingBale());
    CHECK(gull.playerCoin() == coinBefore + kBalePay);
    CHECK(talk.crimes().balesRun() == 1);

    // And standing in the street holding nothing is not a second one.
    gull.stepMovement();
    CHECK(talk.crimes().tally(Crime::Smuggle) == 1);
}

TEST_CASE("a cutpurse is not a fence: the second rung is what makes somebody buy") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    talk.crimes().takeLoot(3);

    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    // The topic is OFFERED to anybody -- being refused out loud is the lesson,
    // and a hidden topic teaches nothing.
    REQUIRE(room.topicOfKind(TopicKind::Fence) >= 0);
    const std::int32_t coinBefore = gull.playerCoin();
    REQUIRE(room.pick(TopicKind::Fence));
    CHECK(gull.playerCoin() == coinBefore);
    CHECK(talk.crimes().loot() == 3);

    // Two rungs up, `fence` is unlocked and the same topic pays.
    talk.standings().addStanding(roofs, 40);
    REQUIRE(talk.standings().join(roofs, talk.skills()) == LadderResult::Granted);
    (void)talk.skills().setLevel(kRoofSkill, 6);
    REQUIRE(talk.standings().advance(roofs, talk.skills()) == LadderResult::Granted);
    REQUIRE(talk.standings().unlocked(roofs, "fence"));

    gull.endConversation();
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Fence));
    CHECK(gull.playerCoin() > coinBefore);
    CHECK(talk.crimes().loot() == 0);
    CHECK(talk.crimes().tally(Crime::Fence) == 1);
}

TEST_CASE("leaning on somebody works on what the ward has heard, not on what you say") {
    Room quiet(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    REQUIRE(quiet.standBy("Sella Brinewall") != nullptr);
    REQUIRE(quiet.tavern().talkTo());
    REQUIRE(quiet.topicOfKind(TopicKind::Lean) >= 0);
    const std::int32_t before = quiet.tavern().playerCoin();
    REQUIRE(quiet.pick(TopicKind::Lean));
    // A nobody asking for money is a nobody asking for money.
    CHECK(quiet.tavern().playerCoin() == before);

    Room feared(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    DialogueDirector& talk = feared.tavern().dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    talk.standings().addStanding(roofs, 80);
    (void)talk.skills().setLevel(kRoofSkill, 20);
    REQUIRE(talk.standings().join(roofs, talk.skills()) == LadderResult::Granted);
    REQUIRE(talk.standings().advance(roofs, talk.skills()) == LadderResult::Granted);
    REQUIRE(talk.standings().advance(roofs, talk.skills()) == LadderResult::Granted);
    talk.crimes().addHeat(kWarrantAt + 10);
    REQUIRE(talk.crimes().warrant());

    REQUIRE(feared.standBy("Sella Brinewall") != nullptr);
    REQUIRE(feared.tavern().talkTo());
    const std::int32_t paidBefore = feared.tavern().playerCoin();
    REQUIRE(feared.pick(TopicKind::Lean));
    CHECK(feared.tavern().playerCoin() > paidBefore);
    CHECK(talk.crimes().tally(Crime::Extort) == 1);

    // Nobody leans on a bouncer or on the law.
    Room law(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    REQUIRE(law.standBy("Watchman Cull") != nullptr);
    REQUIRE(law.tavern().talkTo());
    CHECK(law.topicOfKind(TopicKind::Lean) == -1);
}

TEST_CASE("the top rungs finally buy something: go to ground, and lose the file") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    DialogueDirector& talk = room.tavern().dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");

    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(room.tavern().talkTo());
    // Not offered without the rung.
    CHECK(room.topicOfKind(TopicKind::Favour) == -1);

    talk.standings().addStanding(roofs, 100);
    (void)talk.skills().setLevel(kRoofSkill, 30);
    REQUIRE(talk.standings().join(roofs, talk.skills()) == LadderResult::Granted);
    while (talk.standings().nextRung(roofs) != nullptr) {
        REQUIRE(talk.standings().advance(roofs, talk.skills()) == LadderResult::Granted);
    }
    REQUIRE(talk.standings().unlocked(roofs, "lair"));
    talk.crimes().addHeat(kWarrantAt + 20);
    REQUIRE(talk.crimes().warrant());

    room.tavern().endConversation();
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(room.tavern().talkTo());
    REQUIRE(room.topicOfKind(TopicKind::Favour) >= 0);
    REQUIRE(room.pick(TopicKind::Favour));
    CHECK(talk.crimes().heat() == 0);
    CHECK_FALSE(talk.crimes().warrant());
    CHECK(talk.crimes().timesLaidLow() == 1);
}

TEST_CASE("a watchman gets longer to finish his drink and a wanted man gets none") {
    Room plain(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    const std::int32_t ordinary = plain.tavern().graceSecondsForPlayer();

    Room sworn(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    DialogueDirector& talk = sworn.tavern().dialogue();
    const std::int32_t watch = talk.factions().indexOf("watch");
    talk.standings().addStanding(watch, 60);
    (void)talk.skills().setLevel("kit_keeping", 10);
    REQUIRE(talk.standings().join(watch, talk.skills()) == LadderResult::Granted);
    REQUIRE(talk.standings().advance(watch, talk.skills()) == LadderResult::Granted);
    REQUIRE(talk.standings().unlocked(watch, "grace"));
    // THE TOKEN, not the influence. The S4 review found that the swing here
    // read the ward's balance of power and had nothing to do with the rung
    // that shares the word.
    CHECK(sworn.tavern().graceSecondsForPlayer() > ordinary);

    talk.crimes().addHeat(kWarrantAt + 5);
    REQUIRE(talk.crimes().warrant());
    CHECK(sworn.tavern().graceSecondsForPlayer() < ordinary + 8);
}

TEST_CASE("a player can sign on with the roofs and finish the Skyrunner line") {
    // THE SCRIPTED PLAYTHROUGH THIS SPRINT IS JUDGED ON, driven through the
    // calls a keypress makes: talkTo(), chooseTopic(), crackStrongbox(),
    // handleBale(), and the body's own three roof verbs.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const Questline* line = talk.quests().find("skyrunner-tenant");
    REQUIRE(line != nullptr);

    // 1. the oath.
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Join));
    CHECK(talk.standings().isMember(roofs));
    CHECK(talk.standings().rank(roofs) == 1);
    CHECK(talk.standings().unlocked(roofs, "roof"));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 1);
    gull.endConversation();

    // 2. two purses.
    for (const char* mark : {"Sella Brinewall", "Tarn Wrenhale", "Wick Hempson",
                             "Hobbin Mastwright", "Colm Tarbeck"}) {
        if (talk.journal().counter("skyrunner-tenant") >= 2) {
            break;
        }
        if (room.standBy(mark) != nullptr && gull.talkTo()) {
            (void)room.pick(TopicKind::PickPocket);
            gull.endConversation();
        }
    }
    CHECK(talk.crimes().tally(Crime::Lift) >= 2);
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 2);
    gull.endConversation();

    // GO, DO THE ONE THING, COME BACK AND SAY SO. A counted stage counts what
    // is done WHILE IT IS THE STAGE, so the line has to be walked in order --
    // which is the whole shape of a questline and the reason this case is
    // twice as long as it looks like it should be.
    const auto reportToFinch = [&]() {
        REQUIRE(room.standBy("Finch") != nullptr);
        REQUIRE(gull.talkTo());
        REQUIRE(room.pick(TopicKind::QuestBeat));
        gull.endConversation();
    };

    // 3. a box above the stair.
    room.standAt(gull::kRooms[0].standX, gull::kRooms[0].standY, gull::kUpperBand);
    REQUIRE(gull.crackStrongbox().result == ServiceResult::Served);
    room.standAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    reportToFinch();
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 3);

    // 4. the way up, with the real body. Placed against the guest floor's north
    //    wall, which is the burglar's own route: in the door, up the stair, and
    //    out over the wall of the room you rented. The tally is the one
    //    Session::settleLanding raises on a landing.
    PlayerBody climber(room.tiles(), 150, 67, gull::kUpperBand, kFacingNorth);
    climber.setLandingFloor(docks::kLandingFloor);
    REQUIRE(climber.mantle().ok());
    REQUIRE(climber.band() == gull::kRoofBand);
    talk.noteTally("climbs");
    talk.noteCrime(Crime::RoofRun, false);
    room.standAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    reportToFinch();
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 4);

    // 5. and the way across.
    PlayerBody jumper(room.tiles(), gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    jumper.setLandingFloor(docks::kLandingFloor);
    REQUIRE(jumper.leap(kLeapReachTiles).ok());
    talk.noteTally("leaps");
    reportToFinch();
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 5);

    // 6. sell it. THE SECOND RUNG HAS TO BE EARNED FIRST -- a cutpurse is not a
    //    fence -- and it is measured in SKYRUNNING, which is what all that
    //    climbing was for.
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    // Short of the skill, the rung is refused out loud.
    CHECK_FALSE(talk.standings().unlocked(roofs, "fence"));
    (void)talk.skills().setLevel(kRoofSkill, 6);
    REQUIRE(room.pick(TopicKind::Advance));
    REQUIRE(talk.standings().unlocked(roofs, "fence"));
    REQUIRE(room.pick(TopicKind::Fence));
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 6);
    gull.endConversation();

    // 7. lean on somebody.
    for (const char* mark : {"Tarn Wrenhale", "Colm Tarbeck", "Sella Brinewall",
                             "Hobbin Mastwright"}) {
        if (talk.crimes().tally(Crime::Extort) > 0) {
            break;
        }
        if (room.standBy(mark) != nullptr && gull.talkTo()) {
            (void)room.pick(TopicKind::Lean);
            gull.endConversation();
        }
    }
    CHECK(talk.crimes().tally(Crime::Extort) >= 1);
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 7);
    gull.endConversation();

    // 8. the bale, out past the Watch.
    room.standAt(gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    REQUIRE(gull.handleBale().result == ServiceResult::Served);
    gull.stepMovement();
    room.standAt(gull::kStreetX, gull::kStreetY, gull::kGroundBand);
    gull.stepMovement();
    CHECK(talk.crimes().tally(Crime::Smuggle) == 1);

    room.standAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 8);

    // 9. and what a tenant is.
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stagesDone("skyrunner-tenant") == 9);
    CHECK(talk.journal().done("skyrunner-tenant"));

    // The journal wrote every line it earned, in order.
    CHECK(talk.journal().log().size() >= 9);
    // And the ward's law thinks less of you than it did, without one word
    // having been exchanged with it.
    const std::int32_t watch = talk.factions().indexOf("watch");
    CHECK(talk.standings().standing(watch) < 0);
    CHECK(talk.standings().standing(roofs) > 0);
}
