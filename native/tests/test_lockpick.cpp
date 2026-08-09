// The lock, the wire, and what happens when the wire loses.
//
//   RULES  lockpick.hpp on its own: the pins are a pure function of the seed,
//          what skill buys, and the four ways an attempt can end.
//   ROOM   the four strongboxes above the Gull's stair, and the burglary they
//          are the point of.
//   PLAY   the same lock through the keys a player presses, and the one row of
//          HUD that draws it.

#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/lockpick.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         std::int32_t band = gull::kGroundBand)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(kSeed, world_)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, kSeed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }

    void standAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

private:
    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    Tavern* tavern_ = nullptr;
};

/// Opens a lock the way somebody who has done it before does: straight to each
/// pin's depth. pinDepth() is public and pure, so this is memory, not a cheat.
void setEveryPin(Lockpicking& wire, const Lock& lock, std::int32_t& picks) {
    for (std::int32_t pin = 0; pin < lock.pins; ++pin) {
        wire.moveDepth(pinDepth(kSeed, lock, pin) - wire.depth());
        wire.probe(picks);
    }
}

}  // namespace

// ===========================================================================
// RULES
// ===========================================================================

TEST_CASE("a lock's pins are a pure function of the seed and the lock, and never re-rolled") {
    const Lock box{3, 3, 1};
    // Same tuple, same answer, every time and in any order. This is what lets a
    // player walk away from a half-picked box and come back to the same box --
    // and what stops a retry being a re-roll.
    for (std::int32_t pin = 0; pin < box.pins; ++pin) {
        const std::int32_t once = pinDepth(kSeed, box, pin);
        CHECK(pinDepth(kSeed, box, pin) == once);
        CHECK(once >= 0);
        CHECK(once < kPinDepths);
    }
    // A different lock is a different lock, and a different world is a
    // different world.
    const Lock other{4, 3, 1};
    bool differs = false;
    for (std::int32_t pin = 0; pin < box.pins; ++pin) {
        differs = differs || pinDepth(kSeed, other, pin) != pinDepth(kSeed, box, pin);
        differs = differs || pinDepth(kSeed ^ 1ull, box, pin) != pinDepth(kSeed, box, pin);
    }
    CHECK(differs);

    // WARDS RAISE THE FLOOR. A hard lock never has a shallow pin, so an
    // apprentice's habit of trying zero first stops working on the good rooms.
    const Lock warded{9, 4, 3};
    for (std::int32_t pin = 0; pin < warded.pins; ++pin) {
        CHECK(pinDepth(kSeed, warded, pin) >= 3);
    }

    // AND THE PINS ARE NOT ALL THE SAME PIN. A lock whose three tumblers sat at
    // one depth would be a one-probe lock wearing three.
    std::set<std::int32_t> spread;
    for (std::int32_t id = 1; id <= 40; ++id) {
        const Lock each{id, 3, 1};
        for (std::int32_t pin = 0; pin < each.pins; ++pin) {
            spread.insert(pinDepth(kSeed, each, pin));
        }
    }
    CHECK(spread.size() >= 6);
}

TEST_CASE("skill buys information and forgiveness, and never buys success") {
    // An apprentice is told nothing and gets no slack.
    CHECK_FALSE(hasFeel(0));
    CHECK(pickTolerance(0) == 0);
    // The feel arrives at a level the raws' own TRAINED aptitude can reach.
    CHECK(hasFeel(kFeelLevel));
    CHECK(hasFeel(kFeelLevel + 20));
    // Tolerance rises and then stops. A lock that opened at any depth would not
    // be a lock.
    CHECK(pickTolerance(kTolerancePerCraftLevels) == 1);
    CHECK(pickTolerance(100) == kToleranceCap);
    CHECK(pickTolerance(1000) == kToleranceCap);
    // Strain: more wire in a steadier hand, less against a warded lock, and
    // never less than one wrong probe.
    CHECK(pickStrain(40, 0) > pickStrain(0, 0));
    CHECK(pickStrain(0, 3) <= pickStrain(0, 0));
    CHECK(pickStrain(0, 3) >= 1);
    CHECK(pickStrain(10000, 0) == kStrainCeiling);
}

TEST_CASE("a pin drops when the wire finds it, and the lock opens when the last one does") {
    const Lock box{3, 3, 1};
    Lockpicking wire;
    std::int32_t picks = kStartingPicks;
    wire.begin(box, kSeed, 40);
    REQUIRE(wire.open());
    CHECK(wire.pinsSet() == 0);

    setEveryPin(wire, box, picks);
    CHECK(wire.opened());
    CHECK_FALSE(wire.open());
    CHECK(wire.lastFeel() == Feel::Open);
    CHECK(picks == kStartingPicks);
    CHECK(wire.probes() == box.pins);
}

TEST_CASE("a wrong probe strains the wire, and enough of them snap it") {
    const Lock box{7, 3, 0};
    Lockpicking wire;
    std::int32_t picks = 2;
    // Untrained: no feel, no tolerance, and the least wire.
    wire.begin(box, kSeed, 0);
    const std::int32_t limit = wire.strainLimit();
    REQUIRE(limit >= 1);

    // Park the pick somewhere the first pin is not, and lean on it.
    const std::int32_t want = pinDepth(kSeed, box, 0);
    wire.moveDepth((want == 0 ? kPinDepths - 1 : 0) - wire.depth());
    for (std::int32_t i = 0; i < limit - 1; ++i) {
        const Feel felt = wire.probe(picks);
        CHECK(felt == Feel::NoFeel);
        CHECK(wire.strain() == i + 1);
    }
    CHECK(wire.probe(picks) == Feel::Broke);
    CHECK(picks == 1);
    CHECK(wire.strain() == 0);
    // AND EVERY PIN YOU HAD SET HAS DROPPED BACK. A snapped pick is not a
    // checkpoint, and that is the whole reason a lock is a risk.
    CHECK(wire.pinsSet() == 0);
    CHECK(wire.open());
}

TEST_CASE("the last pick snapping jams the lock, and only force opens it then") {
    const Lock box{11, 3, 0};
    Lockpicking wire;
    std::int32_t picks = 1;
    wire.begin(box, kSeed, 0);
    const std::int32_t want = pinDepth(kSeed, box, 0);
    wire.moveDepth((want == 0 ? kPinDepths - 1 : 0) - wire.depth());
    Feel felt = Feel::Idle;
    for (int guard = 0; guard < 20 && felt != Feel::Jammed; ++guard) {
        felt = wire.probe(picks);
    }
    CHECK(felt == Feel::Jammed);
    CHECK(picks == 0);
    CHECK(wire.jammed());
    CHECK_FALSE(wire.open());
    // Nothing more happens under the wire.
    CHECK(wire.probe(picks) == Feel::Nothing);
}

TEST_CASE("the feel tells a trained hand which way it was wrong, and an apprentice nothing") {
    const Lock box{13, 3, 0};
    const std::int32_t want = pinDepth(kSeed, box, 0);
    // A pin that is not at either end, so both answers are reachable.
    //
    // S10: THIS IS A REQUIRE AND NOT AN `if`. The S9 review's minor finding --
    // it computed pinDepth(kSeed, Lock{13,3,0}, 0) = 3 by hand and confirmed
    // the body does run today, but a case whose assertions sit behind a
    // data-dependent branch is a case that can silently stop testing anything
    // the day a constant moves. If this ever fails, pick a different lock id.
    REQUIRE(want > 0);
    REQUIRE(want < kPinDepths - 1);
    {
        Lockpicking trained;
        std::int32_t picks = kStartingPicks;
        trained.begin(box, kSeed, kFeelLevel);
        trained.moveDepth(-trained.depth());
        trained.moveDepth(want - 1);
        CHECK(trained.probe(picks) == Feel::TooShallow);
        trained.moveDepth(2);
        CHECK(trained.probe(picks) == Feel::TooDeep);

        Lockpicking green;
        std::int32_t greenPicks = kStartingPicks;
        green.begin(box, kSeed, kFeelLevel - 1);
        green.moveDepth(want - 1 - green.depth());
        CHECK(green.probe(greenPicks) == Feel::NoFeel);
    }
    // A master is forgiven being one notch out entirely.
    Lockpicking master;
    std::int32_t masterPicks = kStartingPicks;
    master.begin(box, kSeed, kTolerancePerCraftLevels);
    master.moveDepth(want - master.depth());
    if (want < kPinDepths - 1) {
        master.moveDepth(1);
    } else {
        master.moveDepth(-1);
    }
    CHECK(master.probe(masterPicks) == Feel::Set);
}

TEST_CASE("the pick cannot be held off the end of the track") {
    Lockpicking wire;
    wire.begin(Lock{17, 3, 0}, kSeed, 0);
    wire.moveDepth(-100);
    CHECK(wire.depth() == 0);
    wire.moveDepth(100);
    CHECK(wire.depth() == kPinDepths - 1);
}

// ===========================================================================
// ROOM
// ===========================================================================

TEST_CASE("the box above the stair is locked, and cracksmanship is what opens it") {
    Room room(hourOfDay(4), gull::kRooms[2].standX, gull::kRooms[2].standY, gull::kUpperBand);
    Tavern& gull = room.tavern();

    // S9's whole point in one assertion: a hand no longer goes straight in.
    CHECK(gull.crackStrongbox().result == ServiceResult::Refused);
    CHECK(gull.dialogue().crimes().tally(Crime::Burgle) == 0);
    CHECK(gull.openedLocks() == 0);

    const std::int32_t picksBefore = gull.picks();
    REQUIRE(gull.beginPick().result == ServiceResult::Served);
    CHECK(gull.picking().open());
    CHECK(gull.picking().lock().pins == kStrongboxPins);
    CHECK(gull.picking().lock().wards == strongboxWards(2));

    const Lock lock = Tavern::strongboxLock(2);
    for (std::int32_t pin = 0; pin < lock.pins; ++pin) {
        gull.movePick(pinDepth(kSeed, lock, pin) - gull.picking().depth());
        gull.probeLock();
    }
    CHECK((gull.openedLocks() & (1 << 2)) != 0);
    CHECK_FALSE(gull.picking().open());
    // Clean: nothing broke.
    CHECK(gull.picks() == picksBefore);

    // AND NOW THE HAND GOES IN.
    const Tavern::StealResult took = gull.crackStrongbox();
    CHECK(took.result == ServiceResult::Served);
    CHECK(took.coin >= kStrongboxCoin);
    CHECK(gull.dialogue().crimes().tally(Crime::Burgle) == 1);
}

TEST_CASE("forcing a lock always works, is the loudest thing in the house, and costs half") {
    Room quiet(hourOfDay(4), gull::kRooms[2].standX, gull::kRooms[2].standY, gull::kUpperBand);
    Tavern& forced = quiet.tavern();
    CHECK(forced.playerNoise() == 0);
    const Tavern::PickResult boot = forced.forceLock();
    CHECK(boot.result == ServiceResult::Served);
    CHECK(boot.feel == Feel::Forced);
    CHECK((forced.openedLocks() & (1 << 2)) != 0);
    CHECK((forced.forcedLocks() & (1 << 2)) != 0);
    CHECK(forced.playerNoise() == kForceNoise);
    const std::int32_t forcedCoin = forced.crackStrongbox().coin;

    // The same box, on the same seed, opened with wire instead.
    Room careful(hourOfDay(4), gull::kRooms[2].standX, gull::kRooms[2].standY, gull::kUpperBand);
    Tavern& picked = careful.tavern();
    REQUIRE(picked.beginPick().result == ServiceResult::Served);
    const Lock lock = Tavern::strongboxLock(2);
    for (std::int32_t pin = 0; pin < lock.pins; ++pin) {
        picked.movePick(pinDepth(kSeed, lock, pin) - picked.picking().depth());
        picked.probeLock();
    }
    const std::int32_t pickedCoin = picked.crackStrongbox().coin;

    CHECK(forcedCoin > 0);
    CHECK(pickedCoin > forcedCoin);
    CHECK(forcedCoin == (pickedCoin * kForcedYieldPercent) / 100);
}

TEST_CASE("a jammed lock is permanent, and the room says so with the box still shut") {
    Room room(hourOfDay(4), gull::kRooms[3].standX, gull::kRooms[3].standY, gull::kUpperBand);
    Tavern& gull = room.tavern();
    gull.setPicks(1);
    REQUIRE(gull.beginPick().result == ServiceResult::Served);

    // Lean on the wrong depth until the wire is gone.
    const Lock lock = Tavern::strongboxLock(3);
    const std::int32_t want = pinDepth(kSeed, lock, 0);
    gull.movePick((want == 0 ? kPinDepths - 1 : 0) - gull.picking().depth());
    for (int guard = 0; guard < 20 && gull.picking().open(); ++guard) {
        gull.probeLock();
    }
    CHECK((gull.jammedLocks() & (1 << 3)) != 0);
    CHECK(gull.picks() == 0);

    // The wire will not go back in, and the box will not open.
    CHECK(gull.beginPick().result == ServiceResult::Refused);
    CHECK(gull.crackStrongbox().result == ServiceResult::Refused);
    // Force is the only way left, and it still works.
    CHECK(gull.forceLock().result == ServiceResult::Served);
    CHECK(gull.crackStrongbox().result == ServiceResult::Served);
}

TEST_CASE("your own rented room is not a lock to pick") {
    Room room(hourOfDay(23), gull::kStairX, gull::kStairY, gull::kGroundBand);
    Tavern& gull = room.tavern();
    // The innkeeper is at the stair after seven; rent a bed off him.
    REQUIRE(gull.rentRoom() == ServiceResult::Served);
    const std::int32_t mine = gull.rentedRoom();
    REQUIRE(mine >= 0);
    room.standAt(gull::kRooms[static_cast<std::size_t>(mine)].standX,
                 gull::kRooms[static_cast<std::size_t>(mine)].standY, gull::kUpperBand);
    CHECK(gull.beginPick().result == ServiceResult::Refused);
    CHECK(gull.forceLock().result == ServiceResult::Refused);
    CHECK(gull.openedLocks() == 0);
}

TEST_CASE("probing a lock is a noise, and a house with people in it hears it") {
    // FOUR IN THE MORNING, UPSTAIRS, ALONE. Nobody is on the guest floor and
    // nobody hears a thing.
    Room alone(hourOfDay(4), gull::kRooms[2].standX, gull::kRooms[2].standY, gull::kUpperBand);
    Tavern& gull = alone.tavern();
    REQUIRE(gull.beginPick().result == ServiceResult::Served);
    const Tavern::PickResult felt = gull.probeLock();
    CHECK(gull.playerNoise() > 0);
    CHECK_FALSE(felt.seen);

    // AND THE SAME PROBE WITH THE ROOM STANDING AROUND YOU. Put the body on the
    // taproom floor at nine with the house full: there is nothing to pick down
    // there, which is the refusal, but the NOISE rule is the same one and the
    // stealth suite proves it end to end. What this pins is that a probe
    // charges the air at all -- kProbeNoise is not zero and the fade is real.
    CHECK(gull.playerNoise() <= kProbeNoise);
    for (int i = 0; i < kNoiseFadeSteps + 1; ++i) {
        gull.stepMovement();
    }
    CHECK(gull.playerNoise() == 0);
}

TEST_CASE("Finch sells wire to his own and to nobody else") {
    Room room(hourOfDay(1), gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    Tavern& gull = room.tavern();
    const Actor* finch = nullptr;
    for (const Actor& actor : gull.actors()) {
        if (actor.role() == ActorRole::SkyrunnerContact && actor.present()) {
            finch = &actor;
            break;
        }
    }
    REQUIRE(finch != nullptr);
    room.standAt(finch->tileX(), finch->tileY(), gull::kGroundBand);

    // Not one of the roofs: refused, and it costs nothing to be told so.
    const std::int32_t coinBefore = gull.playerCoin();
    CHECK(gull.buyPicks().result == ServiceResult::Refused);
    CHECK(gull.picks() == kStartingPicks);
    CHECK(gull.playerCoin() == coinBefore);

    // Sign on, and he sells.
    const std::int32_t roofs = gull.dialogue().factions().indexOf("skyrunners");
    REQUIRE(roofs >= 0);
    // The roofs' first rung wants standing, exactly as it does everywhere else
    // in this build -- see test_crime.cpp on the same three lines.
    gull.dialogue().standings().addStanding(roofs, 40);
    REQUIRE(gull.dialogue().standings().join(roofs, gull.dialogue().skills()) ==
            LadderResult::Granted);
    const Tavern::StealResult bought = gull.buyPicks();
    CHECK(bought.result == ServiceResult::Served);
    CHECK(gull.picks() == kStartingPicks + kPicksPerSet);
    CHECK(gull.playerCoin() == coinBefore - kPickPrice * kPicksPerSet);
}

// TASK #81. legend.hpp's own file header calls picksPerSetBonus() one of
// three boons "wired at exactly one call site each" -- Tavern::buyPicks --
// and until this task it was derived correctly and read by nothing. This is
// that call site, proved: a hand the ward calls LIGHT FINGERS gets more wire
// for the SAME coin, not cheaper wire, because the price is what the goods
// are worth and the bonus is what the ward thinks of the hands buying them.
TEST_CASE("a rung on the Wire buys more wire for the same coin, not cheaper wire") {
    Room room(hourOfDay(1), gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    Tavern& gull = room.tavern();
    const Actor* finch = nullptr;
    for (const Actor& actor : gull.actors()) {
        if (actor.role() == ActorRole::SkyrunnerContact && actor.present()) {
            finch = &actor;
            break;
        }
    }
    REQUIRE(finch != nullptr);
    room.standAt(finch->tileX(), finch->tileY(), gull::kGroundBand);

    const std::int32_t roofs = gull.dialogue().factions().indexOf("skyrunners");
    REQUIRE(roofs >= 0);
    gull.dialogue().standings().addStanding(roofs, 40);
    REQUIRE(gull.dialogue().standings().join(roofs, gull.dialogue().skills()) ==
            LadderResult::Granted);

    // A cracksman's hand, not a bought favour: cracksmanship is what THE WIRE
    // is measured in (legend.cpp's own wire score), so setting it directly is
    // the same rung a run of burgled boxes would have earned.
    REQUIRE(gull.dialogue().skills().setLevel(kThieverySkill, 8));

    const std::int32_t coinBefore = gull.playerCoin();
    const Tavern::StealResult bought = gull.buyPicks();
    CHECK(bought.result == ServiceResult::Served);
    // MORE PICKS. The price did not move -- it is still one set's worth of
    // coin -- so a bonus that changed the price instead of the count would
    // pass a same-coin-different-picks assertion for the wrong reason; both
    // are checked.
    CHECK(gull.picks() > kStartingPicks + kPicksPerSet);
    CHECK(gull.playerCoin() == coinBefore - kPickPrice * kPicksPerSet);
}

// ===========================================================================
// PLAY
// ===========================================================================

TEST_CASE("the lock row draws the whole minigame, and the centre of the screen stays empty") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 4 * 3600;
    config.spawnX = gull::kRooms[2].standX;
    config.spawnY = gull::kRooms[2].standY;
    config.spawnBand = gull::kUpperBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    CHECK(session.lockLine().empty());
    CHECK_FALSE(session.picking());

    // G on a locked box puts the wire in rather than opening it.
    session.steal();
    REQUIRE(session.picking());
    const std::string row = session.lockLine();
    REQUIRE_FALSE(row.empty());
    CHECK(row.find("PINS") != std::string::npos);
    CHECK(row.find("DEPTH") != std::string::npos);
    CHECK(row.find("STRAIN") != std::string::npos);
    CHECK(row.find("PICKS") != std::string::npos);
    // ONE ROW. This is the element that would otherwise become a panel in the
    // middle of the screen, which is exactly what the Java build's first-person
    // view died of.
    CHECK(row.find('\n') == std::string::npos);

    // W and S move the pick and the row follows it.
    session.movePick(1);
    CHECK(session.lockpicking().depth() == 1);
    const std::string moved = session.lockLine();
    CHECK(moved != row);

    // ESC takes the wire out and the lock relocks.
    session.stopPicking();
    CHECK_FALSE(session.picking());
    CHECK(session.lockLine().empty());
    CHECK(session.tavern().openedLocks() == 0);

    // AND THE FRAME IT DRAWS INTO KEEPS ITS MIDDLE. The HUD rule is a testable
    // claim and not a preference; this is it, with the lock row on screen.
    //
    // S10 deleted two framebuffers from here that were rendered and then
    // `(void)`-discarded -- dead work left by an abandoned comparison, and the
    // S9 review's second minor finding. The comparison that actually proves the
    // rule is the with-lock/without-lock pair below.
    session.steal();
    REQUIRE(session.picking());
    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    // The world under the two differs (one ran the renderer directly), so this
    // asserts what the RULE says: nothing the HUD draws is inside the box. Text
    // is drawn opaque over whatever is beneath it, so a centre with no glyph in
    // it is a centre where the two agree on where the ink went -- checked by
    // drawing the same frame twice, once with a lock open and once without.
    render::Framebuffer withLock(config.width, config.height);
    session.drawFrame(withLock);
    session.stopPicking();
    render::Framebuffer without(config.width, config.height);
    session.drawFrame(without);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withLock.pixels()[withLock.index(x, y)] ==
                    without.pixels()[without.index(x, y)]);
        }
    }
}

TEST_CASE("a burglary is played from the keys: crouch, cross, lift, climb, pick, empty") {
    render::SmokeRunConfig run;
    run.session.contentDir = content::contentDir();
    run.burgle = true;
    run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
    run.steps = 0;
    run.stamp = false;

    const render::SmokeRunResult played = render::runSmoke(run);
    INFO(played.summary);
    CHECK(played.ok);
    // The hour the line sets itself: two in the morning, when the doors have
    // just been barred and the lanterns are out but the night staff are still
    // in the building.
    CHECK(render::scriptedStartHour(run) == 2);
    CHECK(played.burgleBeats == 7);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.summary.find("burgle beats=7/7") != std::string::npos);
    // AND THE SUMMARY SAYS WHICH WAY THE LIFT WENT rather than leaving a reader
    // to assume it landed. See runBurgleLine on why the beat is the hand and
    // not the coin.
    CHECK(played.summary.find("lift=tried") != std::string::npos);

    // AND THE STEALTH BEAT IS A CLAIM THAT CAN FAIL.
    //
    // The S9 review's second finding, and it was proved rather than argued:
    // this acceptance passed with `Notice::seen` hard-wired to true. Its only
    // stealth beat was `mark(session.hidden())` at a doorway that is EMPTY at
    // two in the morning, so it landed because nobody was there, not because
    // the burglar was unseen. Beat 2 now needs BOTH -- somebody awake, upright
    // and in range, AND that somebody failing to make him out -- and the run
    // says so in its own summary.
    CHECK(played.summary.find(" hidden ") != std::string::npos);
    CHECK(played.summary.find("watchers=") != std::string::npos);
    CHECK(played.summary.find("watchers=0 ") == std::string::npos);
    // Bit 1 is beat 2. Named here because a mask of 127 says nothing about
    // WHICH claim held, and this is the one the review found hollow.
    CHECK((played.burgleBeatMask & 0x2) != 0);
}

TEST_CASE("a lock opens to a hand that only has what a player has") {
    // THE CASE THE S9 REVIEW SAID DID NOT EXIST, verbatim: "there is no test
    // and no scripted run anywhere in which a lock is picked open without
    // foreknowledge of its pins."
    //
    // So: a solver with a player's information and no more. It may look at the
    // depth the pick is held at, how many pins have dropped, and WHAT THE LAST
    // PROBE FELT LIKE -- the three things the HUD's own lock row prints. It
    // never calls pinDepth(), never reads Lock::id, never touches the seed.
    // Given feel, it bisects; that is what the feel is for.
    //
    // If this case ever goes red, the tuning is wrong and not the test.
    const auto crackIt = [](const Lock& lock, std::uint64_t seed, std::int32_t craft,
                            std::int32_t picksInRoll) {
        struct Attempt {
            bool opened = false;
            bool jammed = false;
            std::int32_t probes = 0;
            std::int32_t picksLeft = 0;
        };
        Lockpicking wire;
        std::int32_t picks = picksInRoll;
        wire.begin(lock, seed, craft);
        std::int32_t low = 0;
        std::int32_t high = kPinDepths - 1;
        std::uint32_t tried = 0;
        std::int32_t pinsSeen = 0;
        Attempt out;
        for (int guard = 0; guard < 400 && wire.open(); ++guard) {
            if (wire.pinsSet() != pinsSeen) {
                pinsSeen = wire.pinsSet();
                low = 0;
                high = kPinDepths - 1;
                tried = 0;
            }
            if (low > high) {
                low = 0;
                high = kPinDepths - 1;
                tried = 0;
            }
            std::int32_t aim = low + (high - low) / 2;
            if ((tried & (1U << aim)) != 0U) {
                aim = -1;
                const std::int32_t mid = low + (high - low) / 2;
                for (std::int32_t spread = 1; spread < kPinDepths && aim < 0; ++spread) {
                    if (mid - spread >= low && (tried & (1U << (mid - spread))) == 0U) {
                        aim = mid - spread;
                    } else if (mid + spread <= high &&
                               (tried & (1U << (mid + spread))) == 0U) {
                        aim = mid + spread;
                    }
                }
                if (aim < 0) {
                    low = 0;
                    high = kPinDepths - 1;
                    aim = 0;
                    while (aim < kPinDepths && (tried & (1U << aim)) != 0U) {
                        ++aim;
                    }
                    if (aim >= kPinDepths) {
                        tried = 0;
                        aim = 0;
                    }
                }
            }
            wire.moveDepth(aim - wire.depth());
            const Feel felt = wire.probe(picks);
            ++out.probes;
            tried |= 1U << aim;
            if (felt == Feel::TooShallow) {
                low = aim + 1;
            } else if (felt == Feel::TooDeep) {
                high = aim - 1;
            } else if (felt != Feel::NoFeel) {
                low = 0;
                high = kPinDepths - 1;
                tried = 0;
            }
        }
        out.opened = wire.opened();
        out.jammed = wire.jammed();
        out.picksLeft = picks;
        return out;
    };

    // EVERY LOCK IN THE WARD, at the level the feel arrives at, out of a full
    // roll. All four have to give: a minigame that is winnable on three boxes
    // out of four is a minigame with a wall in it.
    for (std::int32_t room = 0; room < 4; ++room) {
        const Lock box = Tavern::strongboxLock(room);
        const auto got = crackIt(box, kSeed, kFeelLevel, kStartingPicks);
        INFO("room " << room << " probes " << got.probes << " picks left " << got.picksLeft);
        CHECK(got.opened);
        CHECK_FALSE(got.jammed);
        // And it does not take all day. Three pins, nine notches, feel: a
        // bisect is four probes a pin at the outside.
        CHECK(got.probes <= 4 * box.pins);
    }

    // AND ACROSS SEEDS, so this is not one lucky world. Ten different worlds,
    // the same four boxes, the same hand.
    std::int32_t opened = 0;
    for (std::uint64_t bump = 0; bump < 10; ++bump) {
        for (std::int32_t room = 0; room < 4; ++room) {
            const auto got =
                crackIt(Tavern::strongboxLock(room), kSeed + bump * 0x9E3779B97F4A7C15ull,
                        kFeelLevel, kStartingPicks);
            opened += got.opened ? 1 : 0;
        }
    }
    CHECK(opened == 40);

    // AND THE SAME HAND WITH NO FEEL IS A DIFFERENT GAME. One notch below the
    // band, the wire tells it nothing, and a blind sweep of nine depths against
    // four probes of slack loses far more often than it wins. That asymmetry IS
    // the skill: what CRACKSMANSHIP buys is information, and the case that
    // proves the feel works is the case that proves its absence hurts.
    std::int32_t blindOpened = 0;
    for (std::uint64_t bump = 0; bump < 10; ++bump) {
        for (std::int32_t room = 0; room < 4; ++room) {
            const auto got =
                crackIt(Tavern::strongboxLock(room), kSeed + bump * 0x9E3779B97F4A7C15ull,
                        kFeelLevel - 1, kStartingPicks);
            blindOpened += got.opened ? 1 : 0;
        }
    }
    INFO("blind opened " << blindOpened << " of 40");
    CHECK(blindOpened < opened);
}

TEST_CASE("the burglar's second box is opened by hands the first one taught") {
    // THE ARC, PLAYED. `--burgle=lock` used to refill the roll out of nowhere
    // and drive the pick straight to a pinDepth() lookup -- S9's fourth
    // finding. What it does now is the game: the first box costs every pick in
    // the roll and is forced, the eighteen probes that cost buy CRACKSMANSHIP 3
    // which is kFeelLevel, the burglar takes the Skyrunners' first rung off
    // Finch and buys wire, and the second box is worked by a hand that can hear
    // it. Nothing in that path knows where a pin is.
    render::SmokeRunConfig run;
    run.session.contentDir = content::contentDir();
    run.burgle = true;
    run.burgleEnd = "lock";
    run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
    run.steps = 0;
    run.stamp = false;

    const render::SmokeRunResult played = render::runSmoke(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK(played.burgleBeats == 7);
    // THE FIRST BOX IS PICKED, NOT KICKED. `openedLocks` bit 2 is the box the
    // seven beats are about, and `forcedLocks` staying clear of it is the whole
    // retuning in one assertion: S9 shipped `jammed=4 forced=4` on every mode.
    CHECK((played.summary.find("locks open=4 ") != std::string::npos));
    CHECK(played.summary.find("forced=0") != std::string::npos);
    // The hands were taught by working it. Every probe is a use, including the
    // wrong ones -- Morrowind's rule, and this project's since S3.
    CHECK(played.craftLevel > 0);
    // AND THE FRAME IS A LIVE ATTEMPT, NOT A POSE. A second lock is under the
    // wire when the shutter goes; the wire in it was bought off Finch with the
    // same Join-then-buy a keyboard reaches; and the probes on it were aimed by
    // workTheWire, which cannot see a pin. S9's version of this frame called
    // setPicks() to refill the roll out of nowhere and drove the pick to a
    // pinDepth() lookup, under a comment that said "Nothing is faked".
    CHECK(played.summary.find("picking=yes") != std::string::npos);
    CHECK(played.summary.find("nextprobes=0 ") == std::string::npos);
    CHECK(played.summary.find("nextprobes=") != std::string::npos);
    // Wire in the roll at capture: bought, not conjured.
    CHECK(played.summary.find("picks=0 ") == std::string::npos);
}

TEST_CASE("the lock row is the only thing drawn on the lock's row") {
    // A DEFECT FOUND IN A PNG, closed with pixels.
    //
    // The lock row is drawn at height - margin - 31*scale and the objective row
    // at 32*scale off the same edge -- one scaled pixel apart, on a font six
    // rows tall. docs/frames/s10-08-the-lock.png caught the two of them
    // interleaved: "THE 1OCKUNPINS -TENADEPTH ....+....". Same class of defect
    // the S7 review found with the alert over the topic grid, same fix: the
    // wire is a MODE and a mode owns its row.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 2 * 3600;
    config.spawnX = gull::kRooms[2].standX;
    config.spawnY = gull::kRooms[2].standY;
    config.spawnBand = gull::kUpperBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);
    // With the wire OUT, this session is on a rung and has something to say in
    // both of the rows the lock is about to want. That is the precondition:
    // asserting a suppression against two empty strings proves nothing.
    const std::int32_t roofs = session.tavern().dialogue().factions().indexOf("skyrunners");
    REQUIRE(roofs >= 0);
    session.tavern().dialogue().standings().addStanding(roofs, 40);
    REQUIRE(session.tavern().dialogue().standings().join(
                roofs, session.tavern().dialogue().skills()) == LadderResult::Granted);
    REQUIRE_FALSE(session.guildLine().empty());

    session.steal();
    REQUIRE(session.picking());
    REQUIRE_FALSE(session.lockLine().empty());
    // AND THE TWO ROWS UNDER IT STAND DOWN. Same pixel row; a mode owns it.
    CHECK(session.guildLine().empty());
    CHECK(session.objectiveLine().empty());

    // Draw the frame with the wire in, and the same frame with it out. The row
    // the lock occupies must be the lock's alone: with the wire out, whatever
    // the objective row wanted to say is free to say it.
    render::Framebuffer withWire(config.width, config.height);
    session.drawFrame(withWire);
    session.stopPicking();
    REQUIRE_FALSE(session.picking());
    render::Framebuffer without(config.width, config.height);
    session.drawFrame(without);

    // The two frames differ -- the lock row is really there.
    CHECK(withWire.pixels() != without.pixels());
    // AND THE CENTRE IS STILL EMPTY IN BOTH, which is the rule the whole HUD
    // is built against and the reason the row cannot simply be moved.
    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withWire.pixels()[withWire.index(x, y)] ==
                    without.pixels()[without.index(x, y)]);
        }
    }
}
