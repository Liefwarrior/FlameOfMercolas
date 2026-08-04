// The ledger, the skills and the haggle -- with no map loaded.
//
// Deliberately: the whole social layer was built so it could be reasoned about
// without a world, a tavern or an Actor. If any case in this file ever needs to
// open a .trojsav, the separation has broken.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/barter.hpp"
#include "granadad/sim/social.hpp"

using namespace granadad::sim;

TEST_CASE("the six attitude bands are the six the raws are authored over") {
    // Every one of these names is a real suffix in content/raws/barks/barks.json
    // (greet.serf.hostile, greet.serf.cold, ...). The bands are not invented
    // here; they are read off the content and given numbers.
    CHECK(attitudeFor(0) == Attitude::Neutral);
    CHECK(attitudeFor(kColdAtOrBelow) == Attitude::Cold);
    CHECK(attitudeFor(kColdAtOrBelow + 1) == Attitude::Neutral);
    CHECK(attitudeFor(kHostileAtOrBelow) == Attitude::Hostile);
    CHECK(attitudeFor(kHostileAtOrBelow + 1) == Attitude::Cold);
    CHECK(attitudeFor(kWarmAtOrAbove) == Attitude::Warm);
    CHECK(attitudeFor(kWarmAtOrAbove - 1) == Attitude::Neutral);
    CHECK(attitudeFor(kFriendAtOrAbove) == Attitude::Friend);
    CHECK(attitudeFor(kKinAtOrAbove) == Attitude::Kin);
    CHECK(attitudeFor(kDispositionMin) == Attitude::Hostile);
    CHECK(attitudeFor(kDispositionMax) == Attitude::Kin);

    // The bands are ordered and do not overlap, which is what makes a single
    // integer able to answer "how do they feel".
    Attitude previous = attitudeFor(kDispositionMin);
    for (std::int32_t d = kDispositionMin; d <= kDispositionMax; ++d) {
        const Attitude here = attitudeFor(d);
        REQUIRE(static_cast<int>(here) >= static_cast<int>(previous));
        previous = here;
    }
    // And every key string is non-empty, or a fallback chain would look up "".
    for (int i = 0; i <= static_cast<int>(Attitude::Kin); ++i) {
        REQUIRE_FALSE(attitudeKey(static_cast<Attitude>(i)).empty());
        REQUIRE_FALSE(attitudeName(static_cast<Attitude>(i)).empty());
    }
}

TEST_CASE("an actor remembers who helped and who robbed them, separately") {
    SocialLedger ledger;
    // Never met: a clean slate, and no entry taking up room.
    CHECK(ledger.dispositionOf(7) == 0);
    CHECK_FALSE(ledger.knows(7));
    CHECK(ledger.attitudeOf(7) == Attitude::Neutral);

    ledger.record(7, Deed::BoughtDrink);
    CHECK(ledger.dispositionOf(7) == deedWeight(Deed::BoughtDrink));
    REQUIRE(ledger.knows(7));
    CHECK(ledger.memoryOf(7)->favours == 1);
    CHECK(ledger.memoryOf(7)->injuries == 0);

    ledger.record(7, Deed::Robbed);
    CHECK(ledger.dispositionOf(7) < 0);
    // BOTH are remembered. A single score cannot answer "he remembers you
    // helped him once and robbed him once", and this is why favours and
    // injuries are counted apart from it.
    CHECK(ledger.memoryOf(7)->favours == 1);
    CHECK(ledger.memoryOf(7)->injuries == 1);
    CHECK(ledger.memoryOf(7)->lastDeed == Deed::Robbed);

    // And it is one person's memory, not the world's: nobody else was there.
    CHECK(ledger.dispositionOf(8) == 0);
    CHECK(ledger.reputation() == 0);
}

TEST_CASE("you cannot chat your way into being liked") {
    SocialLedger ledger;
    for (int i = 0; i < 500; ++i) {
        ledger.noteConversation(3);
        ledger.record(3, Deed::Spoke);
        ledger.record(3, Deed::Listened);
    }
    CHECK(ledger.dispositionOf(3) == kTalkCeiling);
    CHECK(ledger.attitudeOf(3) == Attitude::Neutral);
    CHECK(ledger.attitudeOf(3) != Attitude::Warm);

    // One drink bought out of your own purse gets you further than five hundred
    // conversations, which is the rule stated as an assertion.
    ledger.record(3, Deed::BoughtDrink);
    CHECK(ledger.attitudeOf(3) == Attitude::Warm);

    // ...and the ceiling never DRAGS somebody down. Talking to a friend does
    // not cost you the friendship.
    SocialLedger other;
    other.seed(4, 90);
    other.record(4, Deed::Listened);
    CHECK(other.dispositionOf(4) == 90);
}

TEST_CASE("a robbery in a full room is not a private arrangement") {
    SocialLedger ledger;
    ledger.record(1, Deed::Robbed);
    // The victim is not a witness of their own robbery -- the caller decides
    // who saw it -- so the ward has heard nothing yet.
    CHECK(ledger.reputation() == 0);

    for (std::int32_t id = 2; id <= 9; ++id) {
        ledger.witness(id, Deed::Robbed);
    }
    CHECK(ledger.reputation() < 0);
    CHECK(ledger.wardAttitude() == Attitude::Hostile);
    CHECK(ledger.reputationLabel() == "THE WARD WANTS YOU GONE");
    // Everybody who saw it feels it, and none of them as hard as the man whose
    // purse it was.
    for (std::int32_t id = 2; id <= 9; ++id) {
        REQUIRE(ledger.dispositionOf(id) < 0);
        REQUIRE(ledger.dispositionOf(id) > ledger.dispositionOf(1));
    }

    // Watching somebody buy a round is not news and creates no memory at all.
    SocialLedger quiet;
    quiet.witness(5, Deed::Spoke);
    CHECK_FALSE(quiet.knows(5));
    CHECK(quiet.reputation() == 0);
}

TEST_CASE("the ledger survives a round trip through bytes") {
    SocialLedger ledger;
    ledger.record(3, Deed::BoughtDrink);
    ledger.record(1, Deed::Robbed);
    ledger.record(9, Deed::HaggledHard);
    ledger.noteConversation(3);
    ledger.noteConversation(3);
    ledger.witness(2, Deed::Struck);

    const std::vector<std::uint8_t> bytes = ledger.encode();
    SocialLedger restored;
    REQUIRE(SocialLedger::decode(bytes, restored));

    REQUIRE(restored.memories().size() == ledger.memories().size());
    for (std::size_t i = 0; i < ledger.memories().size(); ++i) {
        const Memory& a = ledger.memories()[i];
        const Memory& b = restored.memories()[i];
        REQUIRE(a.actorId == b.actorId);
        REQUIRE(a.disposition == b.disposition);
        REQUIRE(a.favours == b.favours);
        REQUIRE(a.injuries == b.injuries);
        REQUIRE(a.talks == b.talks);
        REQUIRE(a.lastDeed == b.lastDeed);
    }
    CHECK(restored.reputation() == ledger.reputation());
    CHECK(restored.deedsDone() == ledger.deedsDone());
    // Deterministic: the same ledger encodes to the same bytes, every time.
    CHECK(restored.encode() == bytes);

    // And it REFUSES what it does not understand, rather than reinterpreting
    // it. Every one of these is a real way a save file goes wrong.
    SocialLedger reject;
    CHECK_FALSE(SocialLedger::decode({}, reject));
    std::vector<std::uint8_t> wrongMagic = bytes;
    wrongMagic[0] = 'X';
    CHECK_FALSE(SocialLedger::decode(wrongMagic, reject));
    std::vector<std::uint8_t> wrongVersion = bytes;
    wrongVersion[2] = 99;
    CHECK_FALSE(SocialLedger::decode(wrongVersion, reject));
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    CHECK_FALSE(SocialLedger::decode(truncated, reject));
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(SocialLedger::decode(trailing, reject));
}

TEST_CASE("the skill vocabulary comes out of the raws, not out of this file") {
    SkillTrack track = SkillTrack::load(granadad::content::contentDir());
    REQUIRE(track.loaded());
    // Twenty, as content/raws/skills/skills.json's own note says. Pinned so a
    // skill added to the raws is a visible change here rather than a silent one.
    CHECK(track.size() == 20);
    // The two this sprint actually uses have to be in there, or haggling and
    // pickpocketing would both be silently reading zero forever.
    REQUIRE(track.find(kHaggleSkill) != nullptr);
    REQUIRE(track.find(kThieverySkill) != nullptr);
    CHECK(track.find(kHaggleSkill)->displayName == "Streetwise");
    // A skill the raws do not define is a loud nullptr, not a quiet zero.
    CHECK(track.find("persuasion") == nullptr);
    CHECK_FALSE(track.setLevel("persuasion", 40));
    CHECK_FALSE(track.use("persuasion"));
    // Sorted by id, so iteration order is the content's.
    for (std::size_t i = 1; i < track.entries().size(); ++i) {
        REQUIRE(track.entries()[i - 1].id < track.entries()[i].id);
    }
}

TEST_CASE("a skill rises by being used, and rises more slowly the higher it is") {
    SkillTrack track = SkillTrack::load(granadad::content::contentDir());
    REQUIRE(track.loaded());
    CHECK(track.level(kHaggleSkill) == 0);

    std::int32_t uses = 0;
    while (track.level(kHaggleSkill) < 1) {
        track.use(kHaggleSkill);
        ++uses;
        REQUIRE(uses < 1000);
    }
    CHECK(uses == usesForLevel(0));

    // The tenth level costs more than the first. That is the whole of the
    // Morrowind steer: the thing you keep doing keeps getting better, slowly.
    CHECK(usesForLevel(10) > usesForLevel(0));
    CHECK(usesForLevel(40) > usesForLevel(10));

    // A big effort can carry several levels at once, and never past 100.
    REQUIRE(track.setLevel(kHaggleSkill, 98));
    track.use(kHaggleSkill, 100000);
    CHECK(track.level(kHaggleSkill) == 100);
    track.use(kHaggleSkill, 100000);
    CHECK(track.level(kHaggleSkill) == 100);
}

// ===========================================================================
// HAGGLING
// ===========================================================================

namespace {

HaggleTerms roomAt(Attitude attitude, std::int32_t playerSkill = 0,
                   std::int32_t merchantSkill = 30) {
    HaggleTerms terms;
    terms.basePrice = 12;
    terms.attitude = attitude;
    terms.playerSkill = playerSkill;
    terms.merchantSkill = merchantSkill;
    terms.goods = Goods::Room;
    return terms;
}

}  // namespace

TEST_CASE("what they ask depends on what they think of you") {
    // THE ACCEPTANCE CLAIM, as arithmetic: relationship moves the price, in one
    // direction, monotonically, with no roll anywhere.
    const std::int32_t hostile = askingPrice(roomAt(Attitude::Hostile));
    const std::int32_t cold = askingPrice(roomAt(Attitude::Cold));
    const std::int32_t neutral = askingPrice(roomAt(Attitude::Neutral));
    const std::int32_t warm = askingPrice(roomAt(Attitude::Warm));
    const std::int32_t friendly = askingPrice(roomAt(Attitude::Friend));
    const std::int32_t kin = askingPrice(roomAt(Attitude::Kin));

    CHECK(hostile > cold);
    CHECK(cold > neutral);
    CHECK(neutral > warm);
    CHECK(warm > friendly);
    CHECK(friendly > kin);
    // A twelve-coin bed really does move, and by enough to notice.
    CHECK(hostile >= neutral + 4);
    CHECK(kin <= neutral - 3);
    // Nothing is ever free, at any standing, at any skill.
    HaggleTerms free = roomAt(Attitude::Kin, 100, 0);
    CHECK(askingPrice(free) >= 1);
    CHECK(reservePrice(free) >= 1);
}

TEST_CASE("streetwise is worth money, and only the gap in it is") {
    // Two novices trading pay the odds.
    CHECK(skillPercent(0, 0) == 0);
    CHECK(skillPercent(50, 50) == 0);
    // The better haggler pays less, the worse one pays more.
    CHECK(skillPercent(40, 10) < 0);
    CHECK(skillPercent(10, 40) > 0);
    // And it is clamped, so no amount of skill turns a purchase into a gift.
    CHECK(skillPercent(100, 0) == -25);
    CHECK(skillPercent(0, 100) == 25);

    const std::int32_t novice = askingPrice(roomAt(Attitude::Neutral, 0, 30));
    const std::int32_t master = askingPrice(roomAt(Attitude::Neutral, 60, 30));
    CHECK(master < novice);
    // A skilled haggler also opens more ground to argue over.
    CHECK(reservePrice(roomAt(Attitude::Neutral, 60, 30)) <
          reservePrice(roomAt(Attitude::Neutral, 0, 30)) + 1);
}

TEST_CASE("a haggle is an argument with rounds, not a lookup") {
    const HaggleTerms terms = roomAt(Attitude::Neutral);
    const std::int32_t asking = askingPrice(terms);
    const std::int32_t reserve = reservePrice(terms);
    REQUIRE(reserve < asking);

    SUBCASE("meeting the asking price ends it at once, and is remembered kindly") {
        Haggle haggle;
        haggle.open(terms);
        CHECK(haggle.active());
        CHECK(haggle.asking() == asking);
        CHECK(haggle.offer(asking) == HaggleOutcome::Struck);
        CHECK(haggle.settledPrice() == asking);
        CHECK(haggle.lastDeed() == Deed::PaidAsking);
        CHECK(haggle.lastSkillEffort() == 0);
        CHECK_FALSE(haggle.active());
        // A closed haggle answers nothing further.
        CHECK(haggle.offer(1) == HaggleOutcome::Idle);
    }

    SUBCASE("landing on the reserve gets the goods and costs some goodwill") {
        Haggle haggle;
        haggle.open(terms);
        CHECK(haggle.offer(reserve) == HaggleOutcome::Struck);
        CHECK(haggle.settledPrice() == reserve);
        // Ground to the floor: good trading, worse manners, and it teaches you
        // more about haggling than paying up does.
        CHECK(haggle.lastDeed() == Deed::HaggledHard);
        CHECK(haggle.lastSkillEffort() > 0);
    }

    SUBCASE("an insult costs double patience and can end the conversation") {
        Haggle haggle;
        haggle.open(terms);
        const std::int32_t insult = reserve / 2 - 1;
        REQUIRE(insult >= 0);
        CHECK(haggle.offer(insult) == HaggleOutcome::Insulted);
        CHECK(haggle.patience() == kHagglePatience - 2);
        CHECK(haggle.lastDeed() == Deed::Lowballed);
        CHECK(haggle.offer(insult) == HaggleOutcome::Refused);
        CHECK_FALSE(haggle.active());
        CHECK(haggle.settledPrice() == 0);
    }

    SUBCASE("they come down, and never below the reserve") {
        Haggle haggle;
        haggle.open(terms);
        const std::int32_t low = reserve / 2 + 1;
        REQUIRE(low < reserve);
        std::int32_t previous = haggle.asking();
        while (haggle.active()) {
            const HaggleOutcome out = haggle.offer(low);
            REQUIRE(haggle.asking() <= previous);
            REQUIRE(haggle.asking() >= haggle.reserve());
            previous = haggle.asking();
            if (out == HaggleOutcome::Refused) {
                break;
            }
        }
        CHECK_FALSE(haggle.active());
        CHECK(haggle.rounds() > 1);
    }

    SUBCASE("walking out is remembered as walking out") {
        Haggle haggle;
        haggle.open(terms);
        CHECK(haggle.walkAway() == HaggleOutcome::WalkedOut);
        CHECK(haggle.lastDeed() == Deed::WalkedOut);
        CHECK(deedWeight(Deed::WalkedOut) < 0);
    }
}

TEST_CASE("the same offer at the same standing settles at the same price, every time") {
    // No draws anywhere in barter.cpp, and this is what says so: two hundred
    // identical haggles, byte for byte identical outcomes. A roll hiding in
    // here would show up as a disagreement inside one process.
    const HaggleTerms terms = roomAt(Attitude::Cold, 22, 30);
    std::int32_t firstSettled = -1;
    HaggleOutcome firstOutcome = HaggleOutcome::Idle;
    for (int run = 0; run < 200; ++run) {
        Haggle haggle;
        haggle.open(terms);
        haggle.offer(haggle.reserve() - 1);
        const HaggleOutcome out = haggle.offer(haggle.asking() - 1);
        if (run == 0) {
            firstSettled = haggle.settledPrice();
            firstOutcome = out;
            continue;
        }
        REQUIRE(haggle.settledPrice() == firstSettled);
        REQUIRE(out == firstOutcome);
    }
    CHECK(firstOutcome != HaggleOutcome::Idle);
}
