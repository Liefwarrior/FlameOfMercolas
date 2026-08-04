// The contraband economy: five goods, radiant work over them, and the Watch
// that takes both away from you.
//
// Four kinds of case, in the order the rest of this suite uses:
//
//   RAWS      the contract board's own file, and the ONE claim that makes a
//             generated job not a slot machine: every proper noun in it is a
//             cross-reference into the owner's notables.json, and an id that
//             file does not have never reaches a brief.
//   RULES     the stash, the Watch's arithmetic and the board's own lifecycle,
//             with no world anywhere near them.
//   RADIANCE  the same night of the same world always offers the same four
//             jobs; a different night does not.
//   ROOM      the sprint's acceptance, played in the Gilded Gull through the
//             calls a keypress makes:
//               * ACCEPT, PERFORM, GET PAID -- the ward's own bounty, taken
//                 from Watchman Cull, signed for by the Flame, hunted for on
//                 the taproom floor and handed back over the same table.
//               * GET CAUGHT, on a separate run -- a load, a warrant, a
//                 watchman across the room, and a job that dies in the impound
//                 with the goods that were going to pay for it.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/contract.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/watch.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const NotableRegistry& who() {
    static const NotableRegistry loaded = NotableRegistry::load(content::contentDir());
    return loaded;
}

const FactionRegistry& guilds() {
    static const FactionRegistry loaded = FactionRegistry::load(content::contentDir());
    return loaded;
}

const ContractRaws& board() {
    static const ContractRaws loaded =
        ContractRaws::load(content::contentDir(), who(), guilds());
    return loaded;
}

/// The Gull, an engine and a body -- the same shape test_crime.cpp uses.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         std::int32_t band = gull::kGroundBand)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(0x4752414E41444144ull, world_)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, 0x4752414E41444144ull,
                                               content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }

    void run(int seconds) {
        for (int i = 0; i < seconds; ++i) {
            engine_->tick();
        }
    }

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

    /// The first topic of this kind whose payload is a contract for `good`.
    [[nodiscard]] int topicForGood(TopicKind kind, Contraband good) const {
        const std::vector<Topic>& topics = tavern_->dialogue().topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind != kind || topics[i].payload < 0) {
                continue;
            }
            const Contract* row = tavern_->dialogue().contracts().find(topics[i].payload);
            if (row != nullptr && row->good == good) {
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
    Tavern* tavern_ = nullptr;
};

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("a generated job can only ever name somebody the owner's file has") {
    // THE ONE CLAIM THAT MAKES RADIANT WORK NOT SLOT-MACHINE FILLER. The SHAPE
    // is drawn; every proper noun is authored, and this is the case that can go
    // red if that ever stops being true.
    REQUIRE(board().loaded());
    REQUIRE(who().loaded());
    REQUIRE(board().brokers().size() == 3);
    REQUIRE(board().offers().size() >= 8);

    for (const ContractBroker& broker : board().brokers()) {
        INFO("broker ", broker.id);
        // One of the Forty, speaking for one of the owner's five factions.
        REQUIRE(who().find(broker.id) != nullptr);
        REQUIRE(guilds().indexOf(broker.faction) >= 0);
        REQUIRE_FALSE(broker.label.empty());
        REQUIRE((broker.needs == "none" || broker.needs == "acquaintance" ||
                 broker.needs == "member"));
        // Every broker has work of their own, or the board would have a slot it
        // could not fill.
        REQUIRE(board().offersFor(broker.id) >= 1);
    }

    for (const ContractOffer& offer : board().offers()) {
        INFO("offer ", offer.id);
        REQUIRE(board().broker(offer.broker) != nullptr);
        REQUIRE_FALSE(offer.verb.empty());
        REQUIRE_FALSE(offer.brief.empty());
        REQUIRE(offer.unitsMin >= 1);
        REQUIRE(offer.unitsMax >= offer.unitsMin);
        REQUIRE(offer.payPerUnit > 0);
        REQUIRE(offer.days >= 1);
        REQUIRE_FALSE(offer.patrons.empty());
        REQUIRE_FALSE(offer.sources.empty());
        for (const std::string& id : offer.patrons) {
            INFO("patron ", id);
            REQUIRE(who().find(id) != nullptr);
            // And the board carries their name and their place, so a brief can
            // be composed without a second reference to the registry.
            const ContractPerson* person = board().person(id);
            REQUIRE(person != nullptr);
            REQUIRE(person->name == who().find(id)->name);
            REQUIRE_FALSE(person->place.empty());
        }
        for (const std::string& id : offer.sources) {
            INFO("source ", id);
            REQUIRE(who().find(id) != nullptr);
            REQUIRE(board().person(id) != nullptr);
        }
        // An offer that FETCHES something has to say what.
        if (offer.good == Contraband::Artifact) {
            REQUIRE_FALSE(offer.things.empty());
        }
    }

    // AND THE REFUSAL IS PROVED BY A CASE RATHER THAN BY THE FILE HAPPENING TO
    // BE CORRECT. content/raws/contracts/contracts.json carries one DELIBERATE
    // bad id -- `nobody_at_all`, in bounty_rats' patron list -- because a filter
    // that has never had anything to filter is a filter nobody has tested. It
    // is dropped, and the four real patrons beside it survive.
    {
        const ContractOffer* rats = nullptr;
        for (const ContractOffer& offer : board().offers()) {
            if (offer.id == "bounty_rats") {
                rats = &offer;
                break;
            }
        }
        REQUIRE(rats != nullptr);
        CHECK(std::find(rats->patrons.begin(), rats->patrons.end(), "nobody_at_all") ==
              rats->patrons.end());
        CHECK(rats->patrons.size() == 4);
        CHECK(board().person("nobody_at_all") == nullptr);
    }

    // The ward's own word for a site, and a sensible fallback for one the table
    // has not been told about.
    CHECK(board().siteName("K25_KENNEL_ROW") == "Kennel Row");
    CHECK(board().siteName("K08_BRANNS") == "Brann's chandlery");
    CHECK(board().siteName("K99_NOWHERE_AT_ALL") == "nowhere at all");
}

TEST_CASE("a broker, patron or source the registry does not have is refused at load") {
    // The load path proves itself against an EMPTY registry: with nobody named,
    // every offer loses its patrons and its sources and the board is empty
    // rather than full of jobs for people who do not exist.
    const NotableRegistry nobody;
    const ContractRaws refused =
        ContractRaws::load(content::contentDir(), nobody, guilds());
    CHECK_FALSE(refused.loaded());
    CHECK(refused.offers().empty());
    CHECK(refused.brokers().empty());

    // And a missing file is a missing file, not a crash: every raws loader in
    // this build boots silent rather than not at all.
    const ContractRaws absent =
        ContractRaws::load("/definitely-not-a-content-directory", who(), guilds());
    CHECK_FALSE(absent.loaded());
    CHECK(absent.offers().empty());
}

// ===========================================================================
// RULES
// ===========================================================================

TEST_CASE("what a sack holds is measured in weight, and that is what gets you caught") {
    Stash sack;
    CHECK(sack.empty());
    CHECK(sack.units() == 0);
    CHECK(sack.roomLeft() == kStashDrams);

    CHECK(sack.add(Contraband::Scalp, 4) == 4);
    CHECK(sack.count(Contraband::Scalp) == 4);
    CHECK(sack.weight() == 4 * contrabandWeight(Contraband::Scalp));
    // A SCALP IS LEGAL. It is the one thing on the list the ward pays for, so
    // it weighs nothing a watchman cares about and raises no heat at all.
    CHECK(sack.illicitUnits() == 0);
    CHECK(sack.illicitWeight() == 0);
    CHECK(sack.heatIfSearched() == 0);

    CHECK(sack.add(Contraband::Flower, 3) == 3);
    CHECK(sack.illicitUnits() == 3);
    CHECK(sack.illicitWeight() == 3 * contrabandWeight(Contraband::Flower));
    CHECK(sack.heatIfSearched() == 3 * contrabandHeat(Contraband::Flower));

    // A JAR IS A JAR. Ten of them fill the sack by weight alone, which is what
    // makes a spirit run something a watchman can see somebody doing and a dust
    // run something he cannot.
    Stash jars;
    const std::int32_t fitted = jars.add(Contraband::Moonshine, 99);
    CHECK(fitted == kStashDrams / contrabandWeight(Contraband::Moonshine));
    CHECK(jars.roomLeft() < contrabandWeight(Contraband::Moonshine));
    CHECK(jars.add(Contraband::Moonshine, 1) == 0);

    // And the Watch takes the illicit half and leaves the rest.
    CHECK(sack.seizeIllicit() == 3);
    CHECK(sack.count(Contraband::Flower) == 0);
    CHECK(sack.count(Contraband::Scalp) == 4);

    // Round-trips through its own bytes, and refuses a foreign blob.
    const std::vector<std::uint8_t> bytes = sack.encode();
    Stash back;
    REQUIRE(Stash::decode(bytes, back));
    CHECK(back.count(Contraband::Scalp) == 4);
    Stash wrecked;
    CHECK_FALSE(Stash::decode({}, wrecked));
    std::vector<std::uint8_t> bent = bytes;
    bent[3] = 99;
    CHECK_FALSE(Stash::decode(bent, wrecked));
}

TEST_CASE("every good resolves both ways, and only one of the five is legal") {
    std::int32_t legal = 0;
    for (std::size_t i = 0; i < kContrabandCount; ++i) {
        const Contraband good = static_cast<Contraband>(i);
        INFO("good ", contrabandSymbol(good));
        REQUIRE_FALSE(contrabandSymbol(good).empty());
        REQUIRE_FALSE(contrabandLabel(good).empty());
        REQUIRE(contrabandValue(good) > 0);
        REQUIRE(contrabandWeight(good) > 0);
        REQUIRE_FALSE(contrabandSkill(good).empty());
        Contraband back = Contraband::Scalp;
        REQUIRE(contrabandFromSymbol(contrabandSymbol(good), back));
        REQUIRE(back == good);
        if (contrabandLegal(good)) {
            ++legal;
            // Legal means no heat, or the word means nothing.
            REQUIRE(contrabandHeat(good) == 0);
        } else {
            REQUIRE(contrabandHeat(good) > 0);
        }
    }
    CHECK(legal == 1);
    // And the one that is legal is the one the Church has an opinion about.
    CHECK(contrabandNeedsSanction(Contraband::Scalp));
    CHECK_FALSE(contrabandNeedsSanction(Contraband::Dust));

    Contraband nothing = Contraband::Scalp;
    CHECK_FALSE(contrabandFromSymbol("brandy", nothing));
    CHECK_FALSE(contrabandFromSymbol("", nothing));
}

TEST_CASE("a watchman notices a load, not a count, and never a man with nothing on him") {
    // A CLEAN MAN IS NEVER TAKEN FOR A LOAD HE IS NOT CARRYING. Stated as a
    // rule rather than left to the arithmetic.
    CHECK(noticePermille(0, 0, 40) == 0);
    CHECK(noticePermille(-5, 0, 40) == 0);

    // Weight is what he sees: four jars of spirit are conspicuous and four
    // twists of dust are not, even though the count is the same.
    const std::int32_t jars = noticePermille(4 * contrabandWeight(Contraband::Moonshine), 0, 25);
    const std::int32_t twists = noticePermille(4 * contrabandWeight(Contraband::Dust), 0, 25);
    CHECK(jars > twists);

    // Streetwise is the only thing a player can raise against it, and it works.
    CHECK(noticePermille(40, 30, 25) < noticePermille(40, 0, 25));
    // Never certain in either direction.
    CHECK(noticePermille(9999, 0, 100) <= 750);
    CHECK(noticePermille(1, 9999, 0) >= 0);

    // CAUSE: two independent halves. A wanted man with nothing on him is still
    // wanted; a clean man with a bale is still carrying it.
    CHECK(watchCause(false, false, 0) == WatchCause::None);
    CHECK(watchCause(true, false, 0) == WatchCause::Warrant);
    CHECK(watchCause(false, true, 3) == WatchCause::Contraband);
    CHECK(watchCause(true, true, 3) == WatchCause::Both);
    // Noticing an empty sack is not a cause.
    CHECK(watchCause(false, true, 0) == WatchCause::None);
}

TEST_CASE("the sentence is canon's: a night for anybody, the hand and then the rope for the roofs") {
    // DECISIONS.md, Eli 2026-07-14, verbatim: "guards should arrest criminals
    // for 1-3 days before turning them loose unless they're a skyrunner then
    // it's cut off their hand first offense and hanging on the second."
    CHECK(sentenceFor(false, true, 0) == Sentence::Held);
    CHECK(sentenceFor(false, true, 1) == Sentence::Held);
    CHECK(sentenceFor(false, true, 9) == Sentence::Held);
    CHECK(sentenceFor(true, true, 0) == Sentence::Maimed);
    CHECK(sentenceFor(true, true, 1) == Sentence::Condemned);
    CHECK(sentenceFor(true, true, 5) == Sentence::Condemned);
    // No paper on you is no cell for you, whoever you are.
    CHECK(sentenceFor(false, false, 0) == Sentence::Fined);
    CHECK(sentenceFor(true, false, 3) == Sentence::Fined);

    // One to three days, and never outside it.
    for (std::uint64_t draw = 0; draw < 200; ++draw) {
        const std::int32_t hours = heldHours(draw);
        REQUIRE(hours >= kHeldHoursMin);
        REQUIRE(hours <= kHeldHoursMax);
    }
    // A fine is small on purpose: the punishment is the night and the seizure.
    CHECK(fineFor(0, 0) == 0);
    CHECK(fineFor(80, 4) > fineFor(80, 0));
    CHECK(fineFor(80, 4) < 60);
}

TEST_CASE("an arrest empties the sack, tears up the paper and remembers the hand") {
    CrimeLedger ledger;
    ledger.stash().add(Contraband::Flower, 4);
    ledger.stash().add(Contraband::Scalp, 2);
    ledger.takeBale(Contraband::Dust, 3);
    ledger.addHeat(80);
    REQUIRE(ledger.warrant());

    // An ordinary thief: a cell, and out.
    const CrimeLedger::ArrestOutcome held = ledger.arrest(false, 100, 7);
    CHECK(held.sentence == Sentence::Held);
    CHECK(held.unitsSeized == 4);
    CHECK(held.fine > 0);
    CHECK(held.heldHours >= kHeldHoursMin);
    CHECK(ledger.stash().count(Contraband::Flower) == 0);
    // The legal half stays. The ward pays for scalps; it does not confiscate
    // them.
    CHECK(ledger.stash().count(Contraband::Scalp) == 2);
    CHECK_FALSE(ledger.carryingBale());
    CHECK_FALSE(ledger.warrant());
    CHECK(ledger.heat() == kHeatAfterSentence);
    CHECK(ledger.arrests() == 1);
    CHECK_FALSE(ledger.maimed());
    CHECK(ledger.takePercent() == 100);

    // A fine never puts anybody in debt.
    CrimeLedger poor;
    poor.stash().add(Contraband::Dust, 4);
    const CrimeLedger::ArrestOutcome searched = poor.arrest(false, 3, 1);
    CHECK(searched.sentence == Sentence::Fined);
    CHECK(searched.fine == 3);
    CHECK(poor.arrests() == 0);
    // A search is not a punishment for what the ward had already heard.
    CHECK(poor.heat() == 0);

    // A Skyrunner: the hand, and then the rope.
    CrimeLedger roofs;
    roofs.addHeat(90);
    const CrimeLedger::ArrestOutcome hand = roofs.arrest(true, 50, 3);
    CHECK(hand.sentence == Sentence::Maimed);
    CHECK(roofs.maimed());
    CHECK(roofs.takePercent() == kMaimedTakePercent);
    CHECK_FALSE(roofs.condemned());
    roofs.addHeat(90);
    const CrimeLedger::ArrestOutcome rope = roofs.arrest(true, 50, 3);
    CHECK(rope.sentence == Sentence::Condemned);
    CHECK(roofs.condemned());
    // The rope does not un-take the hand.
    CHECK(roofs.maimed());
    CHECK(roofs.arrests() == 2);
}

TEST_CASE("the crime ledger's own bytes carry the sack and the record") {
    CrimeLedger before;
    before.commit(Crime::Burgle, true);
    before.stash().add(Contraband::Dust, 3);
    before.stash().add(Contraband::Scalp, 2);
    before.takeBale(Contraband::Flower, kBaleUnits);
    before.addHeat(90);
    (void)before.arrest(true, 40, 5);

    CrimeLedger after;
    REQUIRE(CrimeLedger::decode(before.encode(), after));
    CHECK(after.stash().count(Contraband::Scalp) == before.stash().count(Contraband::Scalp));
    CHECK(after.stash().count(Contraband::Dust) == before.stash().count(Contraband::Dust));
    CHECK(after.arrests() == before.arrests());
    CHECK(after.maimed() == before.maimed());
    CHECK(after.condemned() == before.condemned());
    CHECK(after.lastSentence() == before.lastSentence());
    CHECK(after.heat() == before.heat());
}

// ===========================================================================
// RADIANCE
// ===========================================================================

TEST_CASE("the same night of the same world offers the same work, and the next night does not") {
    auto shared = std::make_shared<const ContractRaws>(
        ContractRaws::load(content::contentDir(), who(), guilds()));
    FactionLedger standings;
    standings.attach(std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir())));

    ContractBoard first;
    ContractBoard second;
    first.attach(shared);
    second.attach(shared);
    first.refresh(3, 0x4752414E41444144ull, standings);
    second.refresh(3, 0x4752414E41444144ull, standings);

    REQUIRE(first.contracts().size() == static_cast<std::size_t>(kOffersPerDay));
    REQUIRE(second.contracts().size() == first.contracts().size());
    for (std::size_t i = 0; i < first.contracts().size(); ++i) {
        const Contract& a = first.contracts()[i];
        const Contract& b = second.contracts()[i];
        INFO("slot ", i);
        REQUIRE(a.id == b.id);
        REQUIRE(a.offerId == b.offerId);
        REQUIRE(a.patron == b.patron);
        REQUIRE(a.source == b.source);
        REQUIRE(a.units == b.units);
        REQUIRE(a.pay == b.pay);
        REQUIRE(a.brief == b.brief);
    }

    // EVERY BROKER HAS SOMETHING TONIGHT. A board that left the ward with no
    // bounty on two nights in five would read as broken rather than as quiet.
    for (const ContractBroker& broker : shared->brokers()) {
        INFO("broker ", broker.id);
        REQUIRE_FALSE(first.offeredBy(broker.id).empty());
    }

    // And a different night is different work.
    ContractBoard later;
    later.attach(shared);
    later.refresh(3, 0x4752414E41444144ull, standings);
    const std::string wasFirst = later.contracts().front().brief;
    later.refresh(4, 0x4752414E41444144ull, standings);
    CHECK(later.contracts().front().brief != wasFirst);

    // A brief names people and places, and NOTHING is left unsubstituted: a
    // "{patron}" on screen is the failure this line exists to catch.
    for (const Contract& row : first.contracts()) {
        INFO("brief ", row.brief);
        REQUIRE(row.brief.find('{') == std::string::npos);
        REQUIRE(row.brief.find('}') == std::string::npos);
        REQUIRE(who().find(row.patron) != nullptr);
        REQUIRE(who().find(row.source) != nullptr);
        REQUIRE(row.patron != row.source);
        REQUIRE(row.brief.find(who().find(row.patron)->name) != std::string::npos);
        REQUIRE(row.units >= 1);
        REQUIRE(row.pay >= 1);
        REQUIRE(row.dueOnDay >= row.postedOnDay);
        // The label is what a player reads off a topic list, and it names the
        // number, the goods and the person.
        REQUIRE(row.label.find(std::string(contrabandLabel(row.good))) != std::string::npos);
    }
}

TEST_CASE("a job survives the sunrise it was given three nights for, and dies on the fourth") {
    auto shared = std::make_shared<const ContractRaws>(
        ContractRaws::load(content::contentDir(), who(), guilds()));
    FactionLedger standings;
    standings.attach(std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir())));
    ContractBoard board;
    board.attach(shared);
    board.refresh(1, 0x4752414E41444144ull, standings);

    const std::int32_t id = board.contracts().front().id;
    const std::int32_t due = board.contracts().front().dueOnDay;
    REQUIRE(board.take(id) == TakeResult::Taken);
    REQUIRE(board.takenCount() == 1);
    // Taking it twice is not taking two.
    CHECK(board.take(id) == TakeResult::NotOffered);
    CHECK(board.take(9999) == TakeResult::NoSuchContract);

    // Every offered job goes at sunrise. A TAKEN one does not.
    board.refresh(due, 0x4752414E41444144ull, standings);
    REQUIRE(board.find(id) != nullptr);
    CHECK(board.find(id)->live());

    // Hands full at three.
    ContractBoard full;
    full.attach(shared);
    full.refresh(1, 0x4752414E41444144ull, standings);
    for (std::int32_t i = 0; i < kMaxTakenContracts; ++i) {
        REQUIRE(full.take(full.contracts()[static_cast<std::size_t>(i)].id) ==
                TakeResult::Taken);
    }
    CHECK(full.take(full.contracts().back().id) == TakeResult::HandsFull);

    // And the morning after the deadline, the job is gone and counted lost.
    board.refresh(due + 1, 0x4752414E41444144ull, standings);
    REQUIRE(board.find(id) == nullptr);
    CHECK(board.failedCount() == 1);
}

TEST_CASE("a bounty is not paid without the Flame's mark, and pay is the ward's own economy") {
    auto shared = std::make_shared<const ContractRaws>(
        ContractRaws::load(content::contentDir(), who(), guilds()));
    auto registry = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    FactionLedger standings;
    standings.attach(registry);

    ContractBoard board;
    board.attach(shared);
    board.refresh(2, 0x4752414E41444144ull, standings);

    // Slot 0 is the Watch's, so it is a bounty, so it wants a priest.
    const Contract* bounty = nullptr;
    for (const Contract& row : board.contracts()) {
        if (row.good == Contraband::Scalp) {
            bounty = &row;
            break;
        }
    }
    REQUIRE(bounty != nullptr);
    const std::int32_t id = bounty->id;
    const std::int32_t units = bounty->units;
    const std::int32_t pay = bounty->pay;
    REQUIRE(board.take(id) == TakeResult::Taken);

    Stash sack;
    // Short.
    sack.add(Contraband::Scalp, units - 1);
    CHECK(board.turnIn(id, sack, 2).result == TurnInResult::Short);
    sack.add(Contraband::Scalp, 1);
    // THE CHURCH SIGNS FOR BLOOD MONEY OR THE WATCH DOES NOT PAY FOR IT.
    // DECISIONS.md's tenure ruling, made mechanical.
    CHECK(board.turnIn(id, sack, 2).result == TurnInResult::NeedsSanction);
    CHECK(sack.count(Contraband::Scalp) == units);

    CHECK(board.sanction() == 1);
    const Settlement settled = board.turnIn(id, sack, 2);
    CHECK(settled.result == TurnInResult::Paid);
    CHECK(settled.pay == pay);
    CHECK(settled.unitsTaken == units);
    CHECK(sack.count(Contraband::Scalp) == 0);
    CHECK(board.paidCount() == 1);
    CHECK(board.coinEarned() == pay);
    // Handed in twice is paid once.
    CHECK(board.turnIn(id, sack, 2).result == TurnInResult::NotYours);

    // AND THE PAY IS THE WARD'S OWN. A guild that sells to you as one of its
    // own pays you as one of its own, through exactly the guildPricePercent a
    // mug of ale moves by. Same night, same seed, different standing.
    FactionLedger liked;
    liked.attach(registry);
    const std::int32_t watch = registry->indexOf("watch");
    REQUIRE(watch >= 0);
    liked.addStanding(watch, 90);
    ContractBoard richer;
    richer.attach(shared);
    richer.refresh(2, 0x4752414E41444144ull, liked);
    const Contract* same = richer.find(id);
    REQUIRE(same != nullptr);
    REQUIRE(same->offerId == bounty->offerId);
    REQUIRE(same->units == units);
    CHECK(same->pay > pay);
}

// ===========================================================================
// ROOM -- the sprint's acceptance
// ===========================================================================

TEST_CASE("a contract taken, performed and paid: the ward's bounty, end to end") {
    // ACCEPT, PERFORM, GET PAID, driven through the calls a keypress makes.
    //
    // Quarter past nine: Watchman Cull is on his after-shift drink and Father
    // Maell has fifteen minutes of his evening hour left. That overlap is the
    // whole shape of the job -- the ward pays for the scalp and the Church
    // signs for the taking, and you need both men before you need the knife.
    Room room(hourOfDay(21, 15), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    const std::int32_t watch = talk.factions().indexOf("watch");
    REQUIRE(watch >= 0);

    // 1. ACCEPT. The ward's bounty is public work: no rung, no oath, no favour.
    REQUIRE(room.standBy("Watchman Cull") != nullptr);
    REQUIRE(gull.talkTo());
    const int offer = room.topicForGood(TopicKind::TakeContract, Contraband::Scalp);
    REQUIRE(offer >= 0);
    const Topic taken = talk.topics()[static_cast<std::size_t>(offer)];
    const Contract* job = talk.contracts().find(taken.payload);
    REQUIRE(job != nullptr);
    const std::int32_t wanted = job->units;
    const std::int32_t pay = job->pay;
    const std::int32_t id = job->id;
    REQUIRE(wanted >= 2);
    REQUIRE(wanted <= kVerminPerNight);

    const Reply accepted = gull.chooseTopic(static_cast<std::size_t>(offer));
    CHECK(accepted.ok);
    CHECK(accepted.contractId == id);
    // The brief goes where a questline's log line goes, so the journal and the
    // HUD need to know nothing about contracts to show it.
    CHECK_FALSE(accepted.journalLine.empty());
    CHECK(accepted.journalLine.find('{') == std::string::npos);
    REQUIRE(talk.contracts().find(id)->live());
    gull.endConversation();

    // 2. THE MARK. A scalp is redeemed under the Flame's own sanction and not
    //    otherwise, and the priest is the only man in the room who can give it.
    REQUIRE(room.standBy("Father Maell") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Sanction));
    CHECK(talk.contracts().find(id)->sanctioned);
    gull.endConversation();

    // 3. PERFORM. Two hours on and the room has thinned enough for the rats.
    gull.skipHours(2);
    REQUIRE(gull.verminPresent() == kVerminPerNight);
    std::int32_t taken_scalps = 0;
    for (std::int32_t attempt = 0; attempt < 40 && taken_scalps < wanted; ++attempt) {
        const Actor* rat = nullptr;
        for (const Actor& actor : gull.actors()) {
            if (actor.role() == ActorRole::Vermin && actor.present() &&
                actor.activity() != Activity::Downed) {
                rat = &actor;
                break;
            }
        }
        if (rat == nullptr) {
            break;
        }
        room.standAt(rat->tileX(), rat->tileY(), rat->band());
        // A fist, through exactly the punch key. A rat is not a person: no
        // offence is reported and no bouncer crosses the room about it.
        const Standing before = gull.playerStanding();
        while (gull.actorById(rat->id())->activity() != Activity::Downed) {
            const Tavern::PunchResult swing = gull.playerPunchNearest();
            REQUIRE(swing.swung);
            REQUIRE(swing.targetId == rat->id());
        }
        CHECK(gull.playerStanding() == before);
        const Tavern::StealResult skinned = gull.takeScalp();
        REQUIRE(skinned.result == ServiceResult::Served);
        ++taken_scalps;
        // A skinned rat does not get up and does not come back tonight.
        CHECK(gull.actorById(rat->id())->activity() == Activity::Away);
    }
    CHECK(taken_scalps == wanted);
    CHECK(talk.crimes().stash().count(Contraband::Scalp) == wanted);
    // AND NONE OF IT WAS A CRIME. The ward pays for these: no heat, no tally,
    // no warrant, nothing on the roll at all.
    CHECK(talk.crimes().heat() == 0);
    CHECK(talk.crimes().crimesCommitted() == 0);
    CHECK(talk.crimes().stash().illicitUnits() == 0);

    // 4. GET PAID, over the same table it was taken across.
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t standingBefore = talk.standings().standing(watch);
    REQUIRE(room.standBy("Watchman Cull") != nullptr);
    REQUIRE(gull.talkTo());
    const int handOver = room.topicOfKind(TopicKind::TurnIn);
    REQUIRE(handOver >= 0);
    const Reply paid = gull.chooseTopic(static_cast<std::size_t>(handOver));
    CHECK(paid.ok);
    CHECK(paid.contractId == id);
    CHECK(paid.coinDelta == pay);
    CHECK(gull.playerCoin() == purse + pay);
    CHECK(talk.crimes().stash().count(Contraband::Scalp) == 0);
    CHECK(talk.contracts().find(id)->state == ContractState::Paid);
    CHECK(talk.contracts().paidCount() == 1);
    CHECK(talk.contracts().coinEarned() == pay);
    // Work done for a guild is a deed done to that guild.
    CHECK(talk.standings().standing(watch) > standingBefore);
    // And the hands got better at what they did. The Morrowind steer, applied
    // to a trade: FIELDCRAFT is what a knife beside a carcass is.
    CHECK(talk.skills().find(contrabandSkill(Contraband::Scalp))->uses > 0);
}

TEST_CASE("a warrant alone is enough, given long enough in front of the wrong man") {
    // THE HEADLINE S5 COULD NOT MAKE GOOD ON: "NOTHING ARRESTS". A warrant with
    // no load behind it is cause on its own -- it simply takes a while, because
    // an off-duty watchman has to connect a face to paper he is not carrying.
    //
    // Nothing is being carried here at all, which is the point: this is the
    // paper doing the work and not the sack.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    talk.crimes().addHeat(kWarrantAt + 10);
    REQUIRE(talk.crimes().warrant());
    REQUIRE(talk.crimes().stash().empty());

    for (int second = 0; second < 900 && !gull.lastArrest().happened; ++second) {
        (void)room.standBy("Watchman Cull");
        room.run(1);
    }
    const Tavern::ArrestReport& arrest = gull.lastArrest();
    REQUIRE(arrest.happened);
    CHECK(arrest.cause == WatchCause::Warrant);
    CHECK(arrest.unitsSeized == 0);
    // Not a Skyrunner, so the ordinary answer: a cell, and out.
    CHECK(arrest.sentence == Sentence::Held);
    CHECK(arrest.heldHours >= kHeldHoursMin);
    CHECK(arrest.heldHours <= kHeldHoursMax);
    CHECK_FALSE(talk.crimes().warrant());
    CHECK(talk.crimes().arrests() == 1);
    CHECK_FALSE(talk.crimes().maimed());
}

TEST_CASE("caught: a load, a warrant, and a job that dies in the impound") {
    // THE SEPARATE RUN. Same room, same seed, a different choice at every step.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    REQUIRE(roofs >= 0);

    // 1. Sworn to the roofs, which is what makes Finch talk about work at all
    //    -- and, later, what decides the sentence.
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Join));
    REQUIRE(talk.standings().isMember(roofs));
    // A broker who would not deal with a stranger deals with a member. The
    // gradient is the point: the ward's bounty is public and the roofs are not.
    const int work = room.topicOfKind(TopicKind::TakeContract);
    REQUIRE(work >= 0);
    const Topic offered = talk.topics()[static_cast<std::size_t>(work)];
    REQUIRE(offered.payload >= 0);
    REQUIRE(gull.chooseTopic(static_cast<std::size_t>(work)).ok);
    gull.endConversation();
    const std::int32_t job = offered.payload;
    REQUIRE(talk.contracts().find(job)->live());

    // 2. A LOAD. Four boxes above the stair, four pieces with names on them --
    //    which is what a recovery contract is for and what a watchman hangs on
    //    you.
    //
    //    S9: AND FOUR LOCKS. A burglar who is in a hurry and has no wire puts
    //    a shoulder to the lid, which always works, is the loudest thing in the
    //    building, and costs half the coin -- exactly the trade this job is
    //    about. There is nobody on the guest floor to hear it, which is why the
    //    hour of the night still matters.
    for (std::int32_t i = 0; i < gull::kRoomCount; ++i) {
        room.standAt(gull::kRooms[static_cast<std::size_t>(i)].standX,
                     gull::kRooms[static_cast<std::size_t>(i)].standY, gull::kUpperBand);
        REQUIRE(gull.forceLock().result == ServiceResult::Served);
        REQUIRE(gull.crackStrongbox().result == ServiceResult::Served);
    }
    CHECK(talk.crimes().stash().count(Contraband::Artifact) == gull::kRoomCount);
    CHECK(talk.crimes().stash().illicitWeight() > 0);

    // 3. AND PAPER. A bale carried out of the door past the man who keeps the
    //    impound is the one act on the ward's list the state is actually
    //    organised to care about, and leaning on the taproom is what it hears
    //    about next. Heat feeds menace, so the room gets easier to lean on the
    //    hotter you are -- which is the ledger's own arithmetic doing the work.
    room.standAt(gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    REQUIRE(gull.handleBale().result == ServiceResult::Served);
    REQUIRE(talk.crimes().carryingBale());
    // A bale HAS CONTENTS now, and this is where S5's boolean stopped being one.
    CHECK(talk.crimes().baleUnits() == kBaleUnits);
    gull.stepMovement();
    room.standAt(gull::kStreetX, gull::kStreetY, gull::kGroundBand);
    gull.stepMovement();
    CHECK_FALSE(talk.crimes().carryingBale());
    CHECK(talk.crimes().tally(Crime::Smuggle) == 1);
    CHECK(talk.crimes().heat() >= crimeHeat(Crime::Smuggle));

    for (int pass = 0; pass < 4 && !talk.crimes().warrant(); ++pass) {
        for (const char* mark : {"Tarn Wrenhale", "Sella Brinewall", "Wick Hempson",
                                 "Hobbin Mastwright", "Edda Pierpont", "Colm Tarbeck"}) {
            if (talk.crimes().warrant()) {
                break;
            }
            if (room.standBy(mark) != nullptr && gull.talkTo()) {
                (void)room.pick(TopicKind::Lean);
                gull.endConversation();
            }
        }
    }
    REQUIRE(talk.crimes().warrant());
    REQUIRE(talk.crimes().heat() >= kWarrantAt);

    // 4. AND A WATCHMAN ACROSS THE TABLE. Everything the arrest is about to
    //    cost, measured before it happens.
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t carried = talk.crimes().stash().illicitUnits();
    std::int32_t doomed = 0;
    for (const Contract& row : talk.contracts().contracts()) {
        if (row.live() && !contrabandLegal(row.good) &&
            talk.crimes().stash().count(row.good) > 0) {
            ++doomed;
        }
    }
    REQUIRE(room.standBy("Watchman Cull") != nullptr);
    CHECK(gull.watchStance() == Tavern::WatchStance::Idle);

    // He looks up, he crosses the table, and he takes you. Seconds, because you
    // are standing in front of him -- the whole counterplay is not being.
    for (int second = 0; second < 180 && !gull.lastArrest().happened; ++second) {
        (void)room.standBy("Watchman Cull");
        room.run(1);
    }
    const Tavern::ArrestReport& arrest = gull.lastArrest();
    REQUIRE(arrest.happened);
    CHECK(arrest.officer == "Watchman Cull");
    CHECK(arrest.cause != WatchCause::None);
    CHECK_FALSE(arrest.line.empty());

    // A SKYRUNNER'S FIRST OFFENCE IS THE HAND. Canon, and it is the only
    // lasting statistical penalty in this build.
    CHECK(arrest.sentence == Sentence::Maimed);
    CHECK(talk.crimes().maimed());
    CHECK_FALSE(talk.crimes().condemned());
    CHECK(talk.crimes().takePercent() == kMaimedTakePercent);
    CHECK(talk.crimes().arrests() == 1);

    // The goods went to the impound, the coin went to the ward, and the paper
    // went with the sentence.
    CHECK(arrest.unitsSeized == carried);
    CHECK(talk.crimes().stash().illicitUnits() == 0);
    CHECK(gull.playerCoin() == purse - arrest.fine);
    CHECK_FALSE(talk.crimes().warrant());
    CHECK(talk.crimes().heat() <= kHeatAfterSentence);
    CHECK(arrest.heldHours >= kHeldHoursMin);

    // AND THE JOB DIED WITH THEM. This is what makes an arrest cost more than a
    // night: a contract whose goods are in the impound cannot be delivered.
    CHECK(arrest.contractsLost == doomed);
    for (const Contract& row : talk.contracts().contracts()) {
        if (row.id == job && !contrabandLegal(row.good) &&
            arrest.contractsLost > 0) {
            CHECK(row.state != ContractState::Taken);
        }
    }
    // The body is turned loose, and whoever owns it is told exactly once.
    CHECK(gull.takeArrestRelease());
    CHECK_FALSE(gull.takeArrestRelease());
}

// ===========================================================================
// S7 -- THE S6 FINDINGS, AS CASES THAT CAN GO RED
// ===========================================================================

TEST_CASE("a watchman's eye is on the load AT THE CALL SITE, not only in the arithmetic") {
    // THE S6 MUTATION SURVIVOR. Swapping illicitWeight() for illicitUnits() at
    // tavern.cpp's one call to noticePermille left all 371 cases green: the
    // only case that tested the claim -- "a watchman notices a load, not a
    // count" -- drove the pure function, and nothing drove the wire.
    //
    // Four jars of quayfire is ninety-six drams. Four twists of dust is
    // twelve. Under the mutation both are four, both weigh the same to the
    // ward, and the whole good-choice tradeoff the contraband economy is built
    // on evaporates in silence. So: the same room, the same seed, the same man
    // across the same table, and the only difference is what is in the sack.
    const auto secondsUntilNoticed = [](Contraband good, std::int32_t units,
                                        std::int32_t hour) -> int {
        Room room(hourOfDay(hour), gull::kBartenderX, gull::kBartenderY + 1);
        Tavern& gull = room.tavern();
        REQUIRE(gull.dialogue().crimes().stash().add(good, units) == units);
        if (room.standBy("Watchman Cull") == nullptr) {
            return -1;
        }
        constexpr int kCeiling = 400;
        for (int second = 0; second < kCeiling; ++second) {
            (void)room.standBy("Watchman Cull");
            room.run(1);
            if (gull.watchStance() != Tavern::WatchStance::Idle || gull.lastArrest().happened) {
                return second;
            }
        }
        return kCeiling;
    };

    // Summed over three different nights so the answer is a property of the
    // arithmetic and not of one lucky draw.
    int jars = 0;
    int twists = 0;
    // Watchman Cull drinks from nine until one, so these are three hours he is
    // actually in the room for.
    for (const std::int32_t hour : {21, 22, 23}) {
        const int withJars = secondsUntilNoticed(Contraband::Moonshine, 4, hour);
        const int withTwists = secondsUntilNoticed(Contraband::Dust, 4, hour);
        REQUIRE(withJars >= 0);
        REQUIRE(withTwists >= 0);
        jars += withJars;
        twists += withTwists;
    }
    INFO("seconds to be noticed carrying jars ", jars, ", carrying dust ", twists);
    // Ninety-six drams is seen sooner than twelve. Under the mutation these two
    // numbers are IDENTICAL, because four is four.
    CHECK(jars < twists);
}

TEST_CASE("the rope is not an amnesty: a condemned man is the one face the ward knows") {
    // THE S6 ENDGAME HOLE. Tavern::tickWatch returned early on condemned() --
    // idle stance, no watchman, no notice, no arrest, permanently -- so two
    // Skyrunner arrests bought the rest of the game at zero risk and the
    // harshest sentence in the ward was mechanically its safest state.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();

    // Sentenced as a Skyrunner until the ward runs out of worse to do: the
    // hand, and then the rope. A sentence needs paper behind it, so the heat
    // that puts paper out is raised before each one.
    for (int pass = 0; pass < 6 && !crimes.condemned(); ++pass) {
        crimes.addHeat(kWarrantAt);
        crimes.arrest(true, 0, static_cast<std::uint64_t>(pass) * 7U + 3U);
    }
    REQUIRE(crimes.condemned());
    const std::int32_t arrestsBefore = crimes.arrests();

    // A sack, and the man who keeps the impound sitting across the table.
    REQUIRE(crimes.stash().add(Contraband::Moonshine, 4) == 4);
    REQUIRE(room.standBy("Watchman Cull") != nullptr);

    bool taken = false;
    for (int second = 0; second < 900 && !taken; ++second) {
        (void)room.standBy("Watchman Cull");
        room.run(1);
        taken = gull.lastArrest().happened;
    }
    // He is taken. There is no paper to connect any more -- the ward passed
    // sentence on this face in public -- and the arrest costs what an arrest
    // costs.
    CHECK(taken);
    CHECK(gull.lastArrest().cause != WatchCause::None);
    CHECK(crimes.stash().illicitUnits() == 0);
    CHECK(crimes.arrests() >= arrestsBefore);
    // And a condemned man is recognised markedly more readily than a merely
    // wanted one, which is the number that replaced the early return.
    CHECK(kCondemnedRecognisePermille > kRecognisePermille);
}

TEST_CASE("a recovery job is settled by the piece it named, not by a count in a sack") {
    // THE OBJECT WAS A DECORATION. content/raws/contracts/contracts.json
    // authors four of them -- "a christening cup with two names filed off it"
    // -- and S6 substituted them into prose and nowhere else, while the stash
    // held an anonymous Artifact count. Any box in the ward settled any job.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();

    // Four pieces in the sack BEFORE anybody asked for one. Fenced goods, not
    // somebody's christening cup.
    REQUIRE(talk.crimes().stash().add(Contraband::Artifact, 4) == 4);

    ContractBoard& board = talk.contracts();
    // Walk the nights until the tide posts a recovery job, so this case does
    // not depend on which four are up tonight.
    std::int32_t job = -1;
    std::int32_t onDay = 0;
    for (std::int32_t day = 0; day < 60 && job < 0; ++day) {
        board.refresh(day, 0x4752414E41444144ull, talk.standings());
        for (const Contract& row : board.contracts()) {
            if (row.good == Contraband::Artifact && row.state == ContractState::Offered) {
                job = row.id;
                onDay = day;
                break;
            }
        }
    }
    REQUIRE(job >= 0);
    REQUIRE(board.take(job) == TakeResult::Taken);
    REQUIRE(board.find(job) != nullptr);
    // The brief promised an object and the object has a name out of the
    // owner's own file.
    CHECK_FALSE(board.find(job)->thing.empty());
    CHECK(board.find(job)->recovered == 0);
    const std::int32_t wanted = board.find(job)->units;

    // Four pieces in hand and it is STILL short, because none of them is the
    // one that was asked for.
    const Settlement early = board.turnIn(job, talk.crimes().stash(), onDay);
    CHECK(early.result == TurnInResult::Short);
    CHECK(talk.crimes().stash().count(Contraband::Artifact) == 4);

    // Now go and get it. One box, one named object, and the board says which
    // job it belonged to.
    for (std::int32_t i = 0; i < wanted; ++i) {
        const Contract* got = board.recoverPiece();
        REQUIRE(got != nullptr);
        CHECK(got->id == job);
    }
    CHECK(board.find(job)->recovered == wanted);
    const Settlement paid = board.turnIn(job, talk.crimes().stash(), onDay);
    CHECK(paid.result == TurnInResult::Paid);
    CHECK(paid.pay > 0);
}

TEST_CASE("a boat lands what the ward ordered, so the night's board can be filled at all") {
    // THE BOARD WAS UNDELIVERABLE. Two bales, three units, ONE good drawn per
    // night out of three: at most six units of a good the player had a
    // one-in-three chance of wanting, against a board asking 3-6 flower, 1-3
    // dust TONIGHT and 2-5 quayfire. Most nights offered work nobody could take.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();

    // Which goods tonight's board asks for, out of the three a hull carries.
    std::vector<Contraband> wanted;
    for (const Contract& row : talk.contracts().contracts()) {
        const std::int32_t index = static_cast<std::int32_t>(row.good);
        if (index >= 1 && index <= kBoatGoodCount &&
            std::find(wanted.begin(), wanted.end(), row.good) == wanted.end()) {
            wanted.push_back(row.good);
        }
    }
    REQUIRE_FALSE(wanted.empty());

    // Sworn to the roofs, because nobody hands a stranger a bale.
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::Join));
    gull.endConversation();

    // Pick up every bale in the snug and see what came off the boat. THREE
    // now, not two, and the good is drawn per bale rather than per night.
    std::int32_t bales = 0;
    for (std::int32_t i = 0; i < kBalesPerNight; ++i) {
        room.standAt(gull::kBaleX, gull::kBaleY, gull::kGroundBand);
        const Tavern::StealResult got = gull.handleBale();
        REQUIRE(got.result == ServiceResult::Served);
        ++bales;
        const Contraband landed = talk.crimes().baleGood();
        INFO("bale ", i, " landed ", contrabandSymbol(landed));
        // EVERY BALE IS SOMETHING SOMEBODY ASKED FOR. Not a lottery: a
        // smuggler's boat lands what has already been paid for.
        CHECK(std::find(wanted.begin(), wanted.end(), landed) != wanted.end());
        // Put it down again so the next one can be picked up.
        room.standAt(gull::kBaleX, gull::kBaleY, gull::kGroundBand);
        REQUIRE(gull.handleBale().result == ServiceResult::Served);
    }
    CHECK(bales == kBalesPerNight);
    CHECK(kBalesPerNight * kBaleUnits == 9);
}
