// The guilds: who there is to join, what a rung costs, and what one buys.
//
// Three kinds of case, deliberately separate:
//
//   RAWS   the owner's factions.json and the S4 ladders hung off it, read and
//          cross-checked. A ladder naming a faction the registry does not have
//          must be dropped rather than allowed to invent one.
//   RULES  the ledger on its own -- earning a rung, the mirror between the
//          Watch and the roofs, the unlock tokens, the byte codec -- with no
//          world anywhere near it.
//   ROOM   the whole thing running in the Gilded Gull: what a rung does to the
//          price of a mug, what a rival's colours do to the man at the corner
//          table, and the Priest of the Flame line played end to end.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/spellforge.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const FactionRegistry& registry() {
    static const FactionRegistry loaded = FactionRegistry::load(content::contentDir());
    return loaded;
}

/// A skill track with one skill forced to a level, so a ladder's skill gate can
/// be tested without playing an evening to earn it.
SkillTrack skillsAt(std::string_view id, std::int32_t level) {
    SkillTrack track = SkillTrack::load(content::contentDir());
    // Discarded on purpose: a skill the raws do not define reads false here, and
    // every id this file passes is one skills.json is asserted to have above.
    (void)track.setLevel(id, level);
    return track;
}

/// The Gull, an engine and a body -- the same shape test_tavern.cpp uses.
class Room {
public:
    explicit Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(0x4752414E41444144ull, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand,
                                             kFacingSouth)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, 0x4752414E41444144ull,
                                               content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] const TileQuery& tiles() const noexcept { return *tiles_; }

    /// Stands the player exactly where an actor is standing, which is the one
    /// unambiguous way to be "within reach" of a named person: distance zero
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

    /// The index of the first topic of this kind on the open list, or -1.
    [[nodiscard]] int topicOfKind(TopicKind kind) const {
        const std::vector<Topic>& topics = tavern_->dialogue().topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind == kind) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /// Picks the first topic of a kind. Returns false when it was not offered.
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

TEST_CASE("the five factions come out of the owner's file and the ladders hang off it") {
    const FactionRegistry& reg = registry();
    REQUIRE(reg.loaded());
    // factions.json's own note states the numbering it expects from a sorted
    // read. Pinned here, because every other call in this file takes that
    // index and a re-sort would silently move every membership in a save.
    CHECK(reg.size() == 5);
    CHECK(reg.indexOf("dockhands") == 0);
    CHECK(reg.indexOf("merchants") == 1);
    CHECK(reg.indexOf("skyrunners") == 2);
    CHECK(reg.indexOf("temple") == 3);
    CHECK(reg.indexOf("watch") == 4);
    CHECK(reg.indexOf("no-such-guild") == -1);

    // EVERY faction is laddered, and every ladder is a different shape. "Each
    // with its own ranking system" is not five copies of one ladder.
    std::vector<std::size_t> lengths;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(reg.size()); ++i) {
        const FactionLadder* ladder = reg.ladder(i);
        REQUIRE(ladder != nullptr);
        CHECK_FALSE(ladder->ranks.empty());
        CHECK_FALSE(ladder->skill.empty());
        lengths.push_back(ladder->ranks.size());
        // Rungs never get cheaper as they go up, or a player could climb past
        // one by meeting the one above it.
        for (std::size_t r = 1; r < ladder->ranks.size(); ++r) {
            CHECK(ladder->ranks[r].standing >= ladder->ranks[r - 1].standing);
            CHECK(ladder->ranks[r].skillLevel >= ladder->ranks[r - 1].skillLevel);
        }
    }
    CHECK(std::count(lengths.begin(), lengths.end(), lengths.front()) <
          static_cast<long>(lengths.size()));

    // The ladders are measured in skills the owner's skills.json actually has.
    const SkillTrack skills = SkillTrack::load(content::contentDir());
    REQUIRE(skills.loaded());
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(reg.size()); ++i) {
        CHECK(skills.find(reg.ladder(i)->skill) != nullptr);
    }
}

TEST_CASE("an actor's guild is derived from the owner's job lists, not from a second table") {
    const FactionRegistry& reg = registry();
    // factions.json says trade.stallkeep belongs to the merchants and
    // clergy.acolyte to the temple. Nothing in C++ repeats that.
    CHECK(reg.factionForJobPrefix("trade") == reg.indexOf("merchants"));
    CHECK(reg.factionForJobPrefix("clergy") == reg.indexOf("temple"));
    CHECK(reg.factionForJobPrefix("watch") == reg.indexOf("watch"));
    CHECK(reg.factionForJobPrefix("villain") == reg.indexOf("skyrunners"));
    // Three families all land on the labouring mass, which is the owner's own
    // grouping and not ours.
    CHECK(reg.factionForJobPrefix("serf") == reg.indexOf("dockhands"));
    CHECK(reg.factionForJobPrefix("maritime") == reg.indexOf("dockhands"));
    CHECK(reg.factionForJobPrefix("husbandry") == reg.indexOf("dockhands"));
    // Deliberately unaffiliated in the raws, and it stays that way.
    CHECK(reg.factionForJobPrefix("wastrel") == -1);
    CHECK(reg.factionForJobPrefix("beast") == -1);
    // The dot is required, so a prefix can never match half a job id.
    CHECK(reg.factionForJobPrefix("trad") == -1);
    CHECK(reg.factionForJobPrefix("") == -1);
}

TEST_CASE("the Watch and the roofs are the only declared rivals, and the Church has none") {
    const FactionRegistry& reg = registry();
    const std::vector<std::int32_t> watchRivals = reg.rivalsOf(reg.indexOf("watch"));
    REQUIRE(watchRivals.size() == 1);
    CHECK(watchRivals.front() == reg.indexOf("skyrunners"));
    const std::vector<std::int32_t> roofRivals = reg.rivalsOf(reg.indexOf("skyrunners"));
    REQUIRE(roofRivals.size() == 1);
    CHECK(roofRivals.front() == reg.indexOf("watch"));
    // DECISIONS.md: the Church never opposes anyone openly.
    CHECK(reg.rivalsOf(reg.indexOf("temple")).empty());
}

// ===========================================================================
// RULES -- the ledger, with no world in sight
// ===========================================================================

TEST_CASE("every rung is earned, including the first") {
    const FactionRegistry& reg = registry();
    const std::int32_t temple = reg.indexOf("temple");
    auto shared = std::make_shared<const FactionRegistry>(FactionRegistry::load(
        content::contentDir()));

    FactionLedger ledger;
    ledger.attach(shared);
    const SkillTrack none = skillsAt(kCraftingSkill, 0);

    // A stranger cannot sign on: the first rung costs standing too.
    CHECK(ledger.join(temple, none) == LadderResult::NeedsStanding);
    CHECK_FALSE(ledger.isMember(temple));
    CHECK(ledger.rank(temple) == 0);

    ledger.addStanding(temple, 12);
    CHECK(ledger.join(temple, none) == LadderResult::Granted);
    CHECK(ledger.isMember(temple));
    CHECK(ledger.rank(temple) == 1);
    CHECK(ledger.rankTitle(temple) == "Disciple");
    CHECK(ledger.join(temple, none) == LadderResult::AlreadyThere);

    // The second rung is measured in a SKILL as well as in standing, and
    // standing alone will not buy it. This is the Morrowind steer as a gate.
    ledger.addStanding(temple, 60);
    CHECK(ledger.advance(temple, none) == LadderResult::NeedsSkill);
    CHECK(ledger.rank(temple) == 1);
    CHECK(ledger.advance(temple, skillsAt(kCraftingSkill, 2)) == LadderResult::Granted);
    CHECK(ledger.rank(temple) == 2);

    // And a ladder you are not on cannot be climbed at all.
    CHECK(ledger.advance(reg.indexOf("watch"), skillsAt("kit_keeping", 40)) ==
          LadderResult::NotAMember);
}

TEST_CASE("a rung unlocks by NAME, and only up to the rung you hold") {
    auto shared = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    const std::int32_t temple = shared->indexOf("temple");
    FactionLedger ledger;
    ledger.attach(shared);

    CHECK_FALSE(ledger.unlocked(temple, "teaching"));
    ledger.addStanding(temple, 12);
    REQUIRE(ledger.join(temple, skillsAt(kCraftingSkill, 0)) == LadderResult::Granted);
    // The Disciple's rung opens the teaching and NOT the workshop. That gap is
    // the whole reason a rank is worth having.
    CHECK(ledger.unlocked(temple, "teaching"));
    CHECK_FALSE(ledger.unlocked(temple, "forge"));
    CHECK_FALSE(ledger.unlocked(temple, "no-such-token"));

    ledger.addStanding(temple, 80);
    REQUIRE(ledger.advance(temple, skillsAt(kCraftingSkill, 3)) == LadderResult::Granted);
    REQUIRE(ledger.advance(temple, skillsAt(kCraftingSkill, 3)) == LadderResult::Granted);
    CHECK(ledger.rank(temple) == 3);
    CHECK(ledger.unlocked(temple, "forge"));
    // Still holds what the rungs below granted.
    CHECK(ledger.unlocked(temple, "teaching"));
}

TEST_CASE("the mirror ledger: what the Watch gains the roofs lose") {
    auto shared = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    const std::int32_t watch = shared->indexOf("watch");
    const std::int32_t roofs = shared->indexOf("skyrunners");
    const std::int32_t temple = shared->indexOf("temple");
    FactionLedger ledger;
    ledger.attach(shared);

    const std::int32_t roofsBefore = ledger.standing(roofs);
    ledger.addStanding(watch, 20);
    CHECK(ledger.standing(watch) == 20);
    // Half the opposite, onto the declared rival and onto nobody else.
    CHECK(ledger.standing(roofs) == roofsBefore - 10);
    CHECK(ledger.standing(temple) == 0);

    // Influence is the mirror at full weight, because a district only has so
    // much regard to go round.
    const std::int32_t watchInfluence = ledger.influence(watch);
    const std::int32_t roofInfluence = ledger.influence(roofs);
    ledger.shiftInfluence(roofs, 15);
    CHECK(ledger.influence(roofs) == roofInfluence + 15);
    CHECK(ledger.influence(watch) == watchInfluence - 15);
    // Clamped at both ends, always.
    ledger.shiftInfluence(roofs, 500);
    CHECK(ledger.influence(roofs) == kInfluenceMax);
    CHECK(ledger.influence(watch) == kInfluenceMin);
}

TEST_CASE("a guild hears about a deed done to one of its own, and hears it smaller") {
    auto shared = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    const std::int32_t merchants = shared->indexOf("merchants");
    FactionLedger ledger;
    ledger.attach(shared);

    ledger.recordDeed(merchants, Deed::BoughtDrink);
    CHECK(ledger.standing(merchants) == factionDeedWeight(Deed::BoughtDrink));
    // Every weight is smaller than what the same deed does to the person, and
    // carries the same sign -- no guild thanks you for robbing its members.
    for (std::size_t i = 0; i < kDeedCount; ++i) {
        const Deed deed = static_cast<Deed>(i);
        const std::int32_t guild = factionDeedWeight(deed);
        const std::int32_t person = deedWeight(deed);
        CHECK(std::abs(guild) <= std::abs(person));
        if (guild != 0 && person != 0) {
            CHECK(((guild > 0) == (person > 0)));
        }
    }
}

TEST_CASE("the faction ledger round-trips through its own bytes") {
    auto shared = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    FactionLedger ledger;
    ledger.attach(shared);
    ledger.addStanding(shared->indexOf("temple"), 40);
    REQUIRE(ledger.join(shared->indexOf("temple"), skillsAt(kCraftingSkill, 0)) ==
            LadderResult::Granted);
    ledger.shiftInfluence(shared->indexOf("watch"), 7);

    const std::vector<std::uint8_t> bytes = ledger.encode();
    FactionLedger restored;
    restored.attach(shared);
    REQUIRE(FactionLedger::decode(bytes, restored));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(shared->size()); ++i) {
        CHECK(restored.isMember(i) == ledger.isMember(i));
        CHECK(restored.rank(i) == ledger.rank(i));
        CHECK(restored.standing(i) == ledger.standing(i));
        CHECK(restored.influence(i) == ledger.influence(i));
    }
    // A version byte nobody wrote is refused rather than reinterpreted.
    std::vector<std::uint8_t> corrupt = bytes;
    corrupt[0] = 0xEE;
    FactionLedger nothing;
    CHECK_FALSE(FactionLedger::decode(corrupt, nothing));
    CHECK_FALSE(FactionLedger::decode({}, nothing));
}

TEST_CASE("a guild's weight and a member's rung both move a price") {
    auto shared = std::make_shared<const FactionRegistry>(
        FactionRegistry::load(content::contentDir()));
    const std::int32_t merchants = shared->indexOf("merchants");
    FactionLedger ledger;
    ledger.attach(shared);

    // A stranger against a guild at its authored base influence pays about par.
    const std::int32_t stranger = guildPricePercent(ledger, merchants);
    CHECK(stranger >= -5);
    CHECK(stranger <= 5);

    // A guild that owns the ward prices strangers UP.
    ledger.shiftInfluence(merchants, 40);
    CHECK(guildPricePercent(ledger, merchants) > stranger);

    // A member pays the member's rate, and a deeper rung pays less than a
    // shallower one.
    ledger.addStanding(merchants, 12);
    REQUIRE(ledger.join(merchants, skillsAt("streetwise", 0)) == LadderResult::Granted);
    const std::int32_t member = guildPricePercent(ledger, merchants);
    CHECK(member < 0);
    ledger.addStanding(merchants, 30);
    REQUIRE(ledger.advance(merchants, skillsAt("streetwise", 6)) == LadderResult::Granted);
    CHECK(guildPricePercent(ledger, merchants) < member);
    // Never a gift, however far the ladder goes.
    CHECK(guildPricePercent(ledger, merchants) >= -40);
    // An index nobody has is worth nothing rather than a crash.
    CHECK(guildPricePercent(ledger, -1) == 0);
    CHECK(guildPricePercent(ledger, 9999) == 0);
}

// ===========================================================================
// ROOM -- the guilds where a player can feel them
// ===========================================================================

TEST_CASE("the room knows which guild each of its people belongs to") {
    Room room(hourOfDay(20), gull::kBartenderX, gull::kBartenderY + 1);
    const FactionRegistry& reg = room.tavern().dialogue().factions();

    auto factionNameOf = [&](std::string_view who) {
        for (const Actor& actor : room.tavern().actors()) {
            if (actor.name() == who) {
                const Faction* faction = reg.at(room.tavern().factionOf(actor));
                return faction == nullptr ? std::string() : faction->id;
            }
        }
        return std::string("?");
    };
    CHECK(factionNameOf("Master Venn") == "merchants");
    CHECK(factionNameOf("Gerta Saltcotte") == "merchants");
    CHECK(factionNameOf("Father Maell") == "temple");
    CHECK(factionNameOf("Captain Ivo Wake") == "dockhands");
    CHECK(factionNameOf("Bram Marrow") == "dockhands");
    // Presented identity versus true identity: Finch greets you as a wastrel
    // and belongs to the roofs.
    CHECK(factionNameOf("Finch") == "skyrunners");
    // And the ward's law drinks in the same room after nine, which is what
    // makes the mirror something a player can walk into.
    CHECK(factionNameOf("Watchman Cull") == "watch");
}

TEST_CASE("a rung on the Row is worth real coin across a real counter") {
    Room room(hourOfDay(20), gull::kBartenderX, gull::kBartenderY + 1);
    const std::int32_t before = room.tavern().drinkPriceForPlayer();
    const std::int32_t roomBefore = room.tavern().roomPriceForPlayer();

    FactionLedger& ledger = room.tavern().dialogue().standings();
    const std::int32_t merchants = room.tavern().dialogue().factions().indexOf("merchants");
    ledger.addStanding(merchants, 40);
    REQUIRE(ledger.join(merchants, room.tavern().dialogue().skills()) == LadderResult::Granted);
    REQUIRE(ledger.advance(merchants, skillsAt("streetwise", 20)) == LadderResult::Granted);
    // Trader unlocks 'price'. The counter charges a member less for the same
    // mug, with no conversation open and nobody haggling.
    CHECK(room.tavern().roomPriceForPlayer() < roomBefore);
    CHECK(room.tavern().drinkPriceForPlayer() <= before);
}

TEST_CASE("the ward's balance of power decides how much rope a house gives") {
    Room room(hourOfDay(20), gull::kBartenderX, gull::kBartenderY + 1);
    const std::int32_t base = room.tavern().graceSecondsForPlayer();
    CHECK(base >= kGraceSecondsFloor);
    CHECK(base <= kGraceSecondsCeiling);

    FactionLedger& ledger = room.tavern().dialogue().standings();
    const std::int32_t roofs = room.tavern().dialogue().factions().indexOf("skyrunners");
    // The roofs take the ward. The Watch is not coming, so the house stops
    // waiting for it.
    ledger.shiftInfluence(roofs, 60);
    const std::int32_t tighter = room.tavern().graceSecondsForPlayer();
    CHECK(tighter < base);
    CHECK(tighter >= kGraceSecondsFloor);

    // And the other way: a garrison with the district in hand buys a drunk more
    // time to finish his drink.
    ledger.shiftInfluence(roofs, -120);
    CHECK(room.tavern().graceSecondsForPlayer() > tighter);
}

TEST_CASE("putting on the Watch's colours makes an enemy of the man at the corner table") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    // Finch keeps the small hours; at eleven he is in the snug.
    const Actor* wisp = nullptr;
    for (const Actor& actor : room.tavern().actors()) {
        if (actor.name() == "Finch" && actor.present()) {
            wisp = &actor;
        }
    }
    REQUIRE(wisp != nullptr);
    const std::int32_t wispId = wisp->id();
    CHECK(room.tavern().dialogue().ledger().attitudeOf(wispId) != Attitude::Hostile);
    CHECK(room.tavern().enemyPresence() == 0);

    FactionLedger& ledger = room.tavern().dialogue().standings();
    const std::int32_t watch = room.tavern().dialogue().factions().indexOf("watch");
    ledger.addStanding(watch, 20);
    REQUIRE(ledger.join(watch, skillsAt("kit_keeping", 0)) == LadderResult::Granted);
    room.tavern().applyRivalHostility();

    // A skyrunner in the room with a Watch runner is enemy presence, and he
    // greets you like one.
    CHECK(room.tavern().enemyPresence() >= 1);
    CHECK(room.tavern().dialogue().ledger().attitudeOf(wispId) == Attitude::Hostile);
    REQUIRE(room.standBy("Finch") != nullptr);
    REQUIRE(room.tavern().talkTo());
    CHECK(room.tavern().dialogue().greetingKey() == "greet.wastrel.hostile");
}

TEST_CASE("a player can take the oath, climb the Mission's ladder and finish the priest's line") {
    // THE SCRIPTED PLAYTHROUGH THIS SPRINT IS JUDGED ON, driven through exactly
    // the calls a keypress makes: talkTo(), chooseTopic(), commitForge().
    Room room(hourOfDay(20), 150, 74);
    Tavern& gull = room.tavern();
    const DialogueDirector& talk = gull.dialogue();
    const std::int32_t temple = talk.factions().indexOf("temple");
    const Questline* line = talk.quests().find("flame-disciple");
    REQUIRE(line != nullptr);
    REQUIRE(line->stages.size() == 6);
    REQUIRE(line->faction == "temple");

    // --- stage 1: the oath ---------------------------------------------------
    REQUIRE(room.standBy("Father Maell") != nullptr);
    REQUIRE(gull.talkTo());
    CHECK(talk.speaker().notableId == "maell");
    REQUIRE(room.topicOfKind(TopicKind::Join) >= 0);
    // The label is the questline's, not a generic sign-on.
    CHECK(talk.topics()[static_cast<std::size_t>(room.topicOfKind(TopicKind::Join))].label ==
          "TAKE THE DISCIPLE'S OATH");
    REQUIRE(room.pick(TopicKind::Join));
    CHECK(talk.standings().isMember(temple));
    CHECK(talk.standings().rank(temple) == 1);
    CHECK(talk.standings().rankTitle(temple) == "Disciple");
    CHECK(talk.journal().stage("flame-disciple") == 1);
    CHECK(talk.journal().log().size() == 1);
    // The oath is gone from the list and cannot be taken twice.
    CHECK(room.topicOfKind(TopicKind::Join) < 0);

    // --- stage 2: the night pot ---------------------------------------------
    // Turning it in early does not pass it, and he says the task again.
    REQUIRE(room.topicOfKind(TopicKind::QuestBeat) >= 0);
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stage("flame-disciple") == 1);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(room.pick(TopicKind::BuyDrinkFor));
    }
    CHECK(talk.journal().counter("flame-disciple") == 3);
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stage("flame-disciple") == 2);

    // --- stage 3: the captain who was out there ------------------------------
    // The gate is SOCIAL-TOPOLOGICAL: the beat is not on the priest's list any
    // more, because the priest is not the party to it.
    CHECK(room.topicOfKind(TopicKind::QuestBeat) < 0);
    REQUIRE(room.standBy("Captain Ivo Wake") != nullptr);
    REQUIRE(gull.talkTo());
    CHECK(talk.speaker().notableId == "wake");
    REQUIRE(room.topicOfKind(TopicKind::QuestBeat) >= 0);
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stage("flame-disciple") == 3);
    CHECK(talk.journal().log().size() == 3);

    // Three stages is the acceptance bar; the rest of the line is below.
    CHECK(talk.journal().stagesDone("flame-disciple") >= 3);

    // --- stage 4: back to the Mission with it --------------------------------
    REQUIRE(room.standBy("Father Maell") != nullptr);
    REQUIRE(gull.talkTo());
    REQUIRE(room.pick(TopicKind::QuestBeat));
    CHECK(talk.journal().stage("flame-disciple") == 4);

    // --- stage 5: he teaches -------------------------------------------------
    CHECK(talk.grimoire().size() == 0);
    REQUIRE(room.pick(TopicKind::Learn));
    CHECK(talk.grimoire().size() == 1);
    CHECK(talk.grimoire().learnedCount() == 1);
    CHECK(talk.grimoire().craftedCount() == 0);
    CHECK(talk.journal().stage("flame-disciple") == 5);
    // What he handed over came out of the owner's raws.
    REQUIRE(talk.spellbook().loaded());
    CHECK(talk.spellbook().find(talk.grimoire().spells().front().id) != nullptr);

    // --- stage 6: the workshop, which the ladder has to open first -----------
    // Asked too early, it is refused: the rung is a real gate.
    REQUIRE(room.topicOfKind(TopicKind::Forge) >= 0);
    REQUIRE(room.pick(TopicKind::Forge));
    CHECK_FALSE(talk.isForging());
    CHECK(talk.journal().stage("flame-disciple") == 5);

    // Sit with him until there is nothing left on the shallow shelf, which is
    // what the Mission measures its third rung in.
    for (int i = 0; i < 16 && room.pick(TopicKind::Learn); ++i) {
    }
    CHECK(talk.skills().level(kCraftingSkill) >= 3);
    for (int i = 0; i < 4 && room.pick(TopicKind::Advance); ++i) {
    }
    CHECK(talk.standings().rank(temple) >= 3);
    CHECK(talk.standings().rankTitle(temple) == "Acolyte");
    CHECK(talk.standings().unlocked(temple, "forge"));

    REQUIRE(room.pick(TopicKind::Forge));
    REQUIRE(talk.isForging());
    const Reply made = gull.commitForge();
    CHECK(made.ok);
    CHECK(talk.grimoire().craftedCount() == 1);
    CHECK(talk.journal().done("flame-disciple"));
    CHECK(talk.journal().stagesDone("flame-disciple") == 6);
    CHECK(talk.journal().log().size() == 6);
    // The crafting the player made is theirs and is NOT on the owner's shelf.
    const Spell* forged = nullptr;
    for (const Spell& spell : talk.grimoire().spells()) {
        if (spell.id.rfind("forged.", 0) == 0) {
            forged = &spell;
        }
    }
    REQUIRE(forged != nullptr);
    CHECK(talk.spellbook().find(forged->id) == nullptr);
    CHECK(forged->skill == kCraftingSkill);

    // Climbing the Mission's ladder moved what the Mission is worth in the ward.
    CHECK(talk.standings().influence(temple) > registry().ladder(temple)->baseInfluence);
}

TEST_CASE("the priest's authored voice never says there are seven") {
    // MAGIC-CANON.md section 5.4 is a HARD CONTENT RULE: every in-world surface
    // says SIX, and the Flame's place at the top of the count is out-of-world
    // knowledge no citizen has. A test rather than a promise, because the next
    // content pass will not have read that dossier.
    const BarkTables tables = BarkTables::load(content::contentDir());
    REQUIRE(tables.loaded());
    auto mentions = [](std::string_view text, std::string_view needle) {
        std::string lowered;
        lowered.reserve(text.size());
        for (const char c : text) {
            lowered.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        }
        return lowered.find(needle) != std::string::npos;
    };
    std::int32_t flameTables = 0;
    for (const BarkTables::Table& table : tables.tables()) {
        const bool ours = table.key.rfind("quest.flame-disciple", 0) == 0 ||
                          table.key.rfind("faction.", 0) == 0 ||
                          table.key.rfind("teaching.", 0) == 0 ||
                          table.key.rfind("forge.", 0) == 0;
        if (ours) {
            ++flameTables;
        }
        for (const std::string& row : table.rows) {
            CHECK_FALSE(mentions(row, "seventh"));
            CHECK_FALSE(mentions(row, "seven powers"));
            CHECK_FALSE(mentions(row, "seven systems"));
        }
    }
    // And the S4 tables really are loaded, so the check above is not vacuous.
    CHECK(flameTables >= 20);
    // The one line that names the count says six, out loud.
    const std::vector<std::string>* lamp = tables.rows("quest.flame-disciple.lamp.maell");
    REQUIRE(lamp != nullptr);
    bool countsSix = false;
    for (const std::string& row : *lamp) {
        if (mentions(row, "six powers")) {
            countsSix = true;
        }
    }
    CHECK(countsSix);
    // And he says out loud what he is NOT handing over.
    bool disclaims = false;
    for (const std::string& row : *lamp) {
        if (mentions(row, "this is not the flame")) {
            disclaims = true;
        }
    }
    CHECK(disclaims);
}
