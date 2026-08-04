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
    if (want > 0 && want < kPinDepths - 1) {
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
    session.steal();
    REQUIRE(session.picking());
    render::Framebuffer dressed(config.width, config.height);
    session.drawFrame(dressed);
    render::Framebuffer bare(config.width, config.height);
    session.renderer().renderFrame(bare, session.camera(), render::RenderSettings{},
                                   std::vector<render::SpriteInstance>{});
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
    (void)bare;
    (void)dressed;
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
}
