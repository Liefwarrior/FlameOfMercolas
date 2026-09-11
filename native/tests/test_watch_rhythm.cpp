// WATCH & RHYTHM BUILD -- the Watch on seen violence, and the rhythm bundle.
//
// The owner's sentence: "I'd expect the watch and crowd to react appropriately
// to the violence they're witnessing." Two things here, deliberately separate:
//
//   WATCH    WatchCause::Violence as a table -- a SEEN lethal fight closes the
//            watchman (Closing, the halt, the arrest at reach through the
//            contraband path's own applyArrest); an unseen one does nothing; a
//            brawl-class fist fight does nothing (the house's law); a
//            presented Wielder is never closed on (deference, absolute). And
//            the blow on the closing watchman: Kill intent, Lethal by B2, his
//            death a murder through slayActor.
//   RHYTHM   the wind-up telegraph and its exact length; the pre-empt stagger
//            and the hard-swing stagger; the recoil on a tap into a guard and
//            the block-stagger on a hard swing into the player's guard (the
//            Oblivion asymmetry); the guard and hard bands carved off the
//            NPC's OWN swing roll (same-roll: the pending roll, one seq bump
//            per swing, no new draw); patrons standing back on the escalation
//            edge and the rota holding them; bouncers refusing steel; the rout.
//            And a scripted fight twin-runs byte-identical.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/nemesis.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/watch.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// test_combat_action's Room, again: sixty movement steps to the tick, the
/// player held at a chosen tile and facing.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         Angle yaw = kFacingSouth, std::uint64_t seed = 0x4752414E41444144ull)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(seed, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand, yaw)),
          yaw_(yaw) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, seed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        push();
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] std::uint64_t hash() const { return engine_->combined_hash(); }

    void stepOnce() {
        push();
        tavern_->stepMovement();
    }

    void second() {
        for (int i = 0; i < kStepsPerSecond; ++i) {
            stepOnce();
        }
        push();
        engine_->tick();
    }

    void run(int seconds) {
        for (int s = 0; s < seconds; ++s) {
            second();
        }
    }

    void settleToIdle() {
        for (int i = 0; i < kHardSwingRecoverySteps + 4 && !tavern_->playerCombatIdle(); ++i) {
            stepOnce();
        }
    }

    /// A whole swing through the public verbs: down, hold to the tier, release.
    Tavern::PlayerSwingResult swing(bool hard) {
        settleToIdle();
        tavern_->playerAttackDown();
        const int hold = hard ? kHardSwingHoldSteps + 1 : 1;
        for (int i = 0; i < hold; ++i) {
            stepOnce();
        }
        return tavern_->playerAttackUp();
    }

    void setYaw(Angle yaw) {
        yaw_ = yaw & (kTurnFull - 1);
        body_->setYaw(yaw_);
        push();
    }

    [[nodiscard]] const Actor* findRole(ActorRole role) const {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.present() && !isFloored(actor.activity()) &&
                actor.role() != ActorRole::Vermin && actor.role() == role) {
                return &actor;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Actor* findByName(std::string_view name) const {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.name() == name) {
                return &actor;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Actor* byId(std::int32_t id) const { return tavern_->actorById(id); }

    /// Puts the player one tile off `mark` on a standable cardinal axis and
    /// faces it. Returns the yaw chosen, or -1 if no standable side exists.
    Angle standFacing(const Actor& mark) {
        const std::int32_t ax = mark.tileX();
        const std::int32_t ay = mark.tileY();
        const std::int32_t band = mark.band();
        struct Side {
            std::int32_t dx;
            std::int32_t dy;
            Angle yaw;
        };
        const Side sides[] = {
            {0, -1, kFacingSouth},
            {0, 1, kFacingNorth},
            {-1, 0, kFacingEast},
            {1, 0, kFacingWest},
        };
        for (const Side& s : sides) {
            const std::int32_t px = ax + s.dx;
            const std::int32_t py = ay + s.dy;
            if (tiles_->standable(px, py, band)) {
                body_->placeAt(px, py, band);
                setYaw(s.yaw);
                return s.yaw;
            }
        }
        return -1;
    }

    /// The first present, upright PATRON who is not a professional and whom
    /// the player can stand facing. The mark every rhythm case uses.
    const Actor* standFacingPatron() {
        for (const Actor& actor : tavern_->actors()) {
            if (!actor.present() || isFloored(actor.activity()) ||
                actor.role() != ActorRole::Patron || tavern_->isProfessional(actor)) {
                continue;
            }
            if (standFacing(actor) != -1) {
                return &actor;
            }
        }
        return nullptr;
    }

    /// The first present, upright PATRON (not a professional) the player can
    /// stand facing AND whom `watcher` can then SEE the player from (the
    /// three-clause notice rule, asked of the room itself) -- or, with
    /// `seen` false, one the watcher can NOT see the player from. nullptr
    /// when the room offers none.
    const Actor* standFacingPatronFor(const Actor& watcher, bool seen) {
        for (const Actor& actor : tavern_->actors()) {
            if (!actor.present() || isFloored(actor.activity()) ||
                actor.role() != ActorRole::Patron || tavern_->isProfessional(actor)) {
                continue;
            }
            if (standFacing(actor) == -1) {
                continue;
            }
            if (tavern_->noticeBy(watcher).seen == seen) {
                return &actor;
            }
        }
        return nullptr;
    }

    /// The first present, non-floored body on the look-ray, computed here
    /// from the projection VETO 1 specifies (test_combat_action's own
    /// cross-check). Returns the actor id, or -1.
    [[nodiscard]] std::int32_t expectedSightlineId() const {
        const std::int64_t fx = forward_x_q16(yaw_);
        const std::int64_t fy = forward_y_q16(yaw_);
        std::int32_t best = -1;
        std::int64_t bestAlong = static_cast<std::int64_t>(kMeleeReach) + 1;
        for (const Actor& actor : tavern_->actors()) {
            if (!actor.present() || isFloored(actor.activity())) {
                continue;
            }
            const std::int64_t dx = static_cast<std::int64_t>(actor.x()) - px();
            const std::int64_t dy = static_cast<std::int64_t>(actor.y()) - py();
            const std::int64_t along = (fx * dx + fy * dy) >> 16;
            if (along <= 0 || along > kMeleeReach) {
                continue;
            }
            const std::int64_t perp = (-fy * dx + fx * dy) >> 16;
            if (perp > kBodyHalfWidth || perp < -kBodyHalfWidth) {
                continue;
            }
            if (along < bestAlong) {
                bestAlong = along;
                best = actor.id();
            }
        }
        return best;
    }

    [[nodiscard]] std::int32_t px() const noexcept { return body_->x(); }
    [[nodiscard]] std::int32_t py() const noexcept { return body_->y(); }
    [[nodiscard]] bool insideFootprint() const noexcept {
        return gull::insideFootprint(body_->tileX(), body_->tileY());
    }

private:
    void push() {
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
        tavern_->setPlayerYaw(yaw_);
    }

    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
    Angle yaw_ = kFacingSouth;
};

/// Raises the hands WITHOUT violence: the guard down and up again (raise
/// rule 2; a guard release does not lower).
void raiseHands(Room& room) {
    room.tavern().setPlayerBlocking(true);
    room.tavern().setPlayerBlocking(false);
}

/// Steps until neither the recoil nor the block-stagger refuses a press.
void waitRhythm(Room& room) {
    for (int i = 0; i < kRecoilSteps + kBlockStaggerSteps + 4 &&
                    (room.tavern().playerRecoilSteps() > 0 ||
                     room.tavern().playerBlockStaggerSteps() > 0);
         ++i) {
        room.stepOnce();
    }
}

/// Keeps facing the mark and taps him once; re-stands first because a
/// staggered or routing man moves, and waits out the player's own clocks so
/// the press is heard.
Tavern::PlayerSwingResult tapAt(Room& room, std::int32_t markId, bool hard) {
    const Actor* mark = room.byId(markId);
    REQUIRE(mark != nullptr);
    waitRhythm(room);
    REQUIRE(room.standFacing(*mark) != -1);
    // Somebody else on the line (a bouncer walking over to warn, a patron
    // standing back) would take the blow and join the fight instead; wait a
    // few steps for the line to clear rather than swing at the wrong man.
    for (int i = 0; i < 30 && room.expectedSightlineId() != markId; ++i) {
        room.stepOnce();
        REQUIRE(room.standFacing(*mark) != -1);
    }
    return room.swing(hard);
}

}  // namespace

// ===========================================================================
// WATCH -- WatchCause::Violence as a table
// ===========================================================================

TEST_CASE("a SEEN lethal fight closes the watchman: Violence, the halt, the arrest at reach") {
    // Hour 23: Watchman Cull is in the Gull (21:00-01:00), the watch tests'
    // own spot beside the bar. A blade drawn on a patron in his sight.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    REQUIRE(cull->present());
    // A spot he can SEE the player from -- the three-clause rule asked of the
    // room, not assumed from a tile count: a stool two tiles off with a table
    // between is not in his sight, and the first draft of this case stood
    // there and proved only that the rule is real.
    const Actor* mark = room.standFacingPatronFor(*cull, /*seen=*/true);
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(tavern.noticeBy(*cull).seen);
    REQUIRE(tavern.watchStance() == Tavern::WatchStance::Idle);

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
    REQUIRE(tap.targetId == markId);
    REQUIRE(tap.fight == FightClass::Lethal);
    REQUIRE(tavern.lethalFightLive());

    // Closing on VIOLENCE, with the halt in his mouth, within seconds -- no
    // notice die, no recognition lottery: violence is seen or it is not.
    bool closing = false;
    for (int s = 0; s < 20 && !closing; ++s) {
        room.run(1);
        closing = tavern.watchStance() == Tavern::WatchStance::Closing;
    }
    REQUIRE(closing);
    CHECK(tavern.watchInterest() == WatchCause::Violence);
    CHECK(watchCauseName(WatchCause::Violence) == "violence");
    CHECK(tavern.respondingWatchman() != nullptr);
    CHECK(tavern.respondingWatchman()->name() == "Watchman Cull");
    CHECK_FALSE(tavern.lastDemand().empty());
    CHECK(tavern.lastDemand().rfind("Watchman Cull: ", 0) == 0);

    // And the arrest at reach, exactly as the contraband path does it: stand
    // still and he takes you. The report names the cause.
    bool arrested = false;
    for (int s = 0; s < 60 && !arrested; ++s) {
        room.run(1);
        arrested = tavern.takeArrestRelease();
    }
    REQUIRE(arrested);
    CHECK(tavern.lastArrest().happened);
    CHECK(tavern.lastArrest().cause == WatchCause::Violence);
    CHECK(tavern.lastArrest().officer == "Watchman Cull");
    CHECK(tavern.watchStance() == Tavern::WatchStance::Idle);
    CHECK_FALSE(tavern.playerHandsUp());  // taken: hands down
}

TEST_CASE("an UNSEEN lethal fight is nothing to the Watch") {
    // Hour 19: no watchman in the house (Cull comes in at 21:00). The same
    // blade, the same patron, and nobody with cause to see it.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    REQUIRE_FALSE(cull->present());
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, mark->id(), false);
    REQUIRE(tap.fight == FightClass::Lethal);
    for (int s = 0; s < 20; ++s) {
        room.run(1);
        CHECK(tavern.watchStance() == Tavern::WatchStance::Idle);
        CHECK(tavern.watchInterest() == WatchCause::None);
    }
    CHECK_FALSE(tavern.takeArrestRelease());
}

TEST_CASE("an UNSEEN lethal fight with the watchman IN THE ROOM is still nothing: out of his sight is out of it") {
    // Hour 23, Cull at his drink, and a patron he cannot see the player from
    // (the notice rule's own answer: a table in the line, the wrong side of
    // the room). The Watch closes on what it SEES; violence it does not see
    // is a rumour, and a rumour is not cause.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    REQUIRE(cull->present());
    const Actor* mark = room.standFacingPatronFor(*cull, /*seen=*/false);
    if (mark == nullptr) {
        MESSAGE("every patron tonight stands in Cull's sight; nothing to prove here");
        return;
    }
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, mark->id(), false);
    REQUIRE(tap.fight == FightClass::Lethal);
    REQUIRE(tavern.violenceInView());  // the cause is there; the sight is not
    bool everSeen = false;
    for (int s = 0; s < 20; ++s) {
        room.run(1);
        everSeen = everSeen || tavern.noticeBy(*cull).seen;
        if (!everSeen) {
            CHECK(tavern.watchStance() == Tavern::WatchStance::Idle);
        }
    }
    INFO("Cull came to see it: ", everSeen);
}

TEST_CASE("a BRAWL-class fist fight is the house's law: no violence cause, even hands up over the downed man") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    REQUIRE(room.findByName("Watchman Cull")->present());
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    // Fists and Subdue, the default: a bar fight forever with taps.
    bool downed = false;
    for (int s = 0; s < 60 && !downed && !tavern.playerFloored(); ++s) {
        const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
        CHECK(tap.fight == FightClass::Brawl);
        downed = tap.blow.downed;
        room.run(1);
        CHECK_FALSE(tavern.violenceInView());
        const bool closingOnViolence = tavern.watchStance() == Tavern::WatchStance::Closing &&
                                       tavern.watchInterest() == WatchCause::Violence;
        CHECK_FALSE(closingOnViolence);
    }
    REQUIRE(downed);
    // Hands up, standing over a man on the floor -- Downed, not Dead -- and
    // still nothing: B3 says a Subdue beating is a brawl, and the Watch has
    // no cause in a brawl.
    CHECK(room.byId(markId)->activity() == Activity::Downed);
    raiseHands(room);
    REQUIRE(tavern.playerHandsUp());
    for (int s = 0; s < 10; ++s) {
        room.run(1);
        CHECK_FALSE(tavern.violenceInView());
        CHECK(tavern.watchInterest() != WatchCause::Violence);
    }
}

TEST_CASE("the Watch never closes on a presented Wielder, whatever the violence") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    REQUIRE(room.findByName("Watchman Cull")->present());
    tavern.setPlayerPresentsAsWielder(true);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, mark->id(), false);
    REQUIRE(tap.fight == FightClass::Lethal);
    REQUIRE(tavern.violenceInView());  // the cause is there; the deference outranks it
    for (int s = 0; s < 40; ++s) {
        room.run(1);
        CHECK(tavern.watchStance() == Tavern::WatchStance::Idle);
    }
    CHECK_FALSE(tavern.takeArrestRelease());
}

TEST_CASE("steel UP in the hands is cause on its own; hands down it is not") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    tavern.setPlayerCombat(Weapon::Edged, Intent::Subdue);
    REQUIRE_FALSE(tavern.playerHandsUp());
    CHECK_FALSE(tavern.violenceInView());
    // A swing at the air raises the hands: STEEL UP, and that is what the
    // Watch reads -- the whole reason the stance is sim state.
    raiseHands(room);
    REQUIRE(tavern.playerHandsUp());
    CHECK(tavern.violenceInView());
    tavern.lowerPlayerHands();
    CHECK_FALSE(tavern.violenceInView());
}

TEST_CASE("a blow on the closing watchman makes him a brawler with Kill intent; killing him is murder") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    REQUIRE(cull->present());
    const std::int32_t cullId = cull->id();
    REQUIRE(cull->intent() == Intent::Subdue);

    // STEEL UP in his sight brings him over.
    tavern.setPlayerCombat(Weapon::Edged, Intent::Subdue);
    REQUIRE(room.standFacing(*cull) != -1);
    raiseHands(room);
    REQUIRE(tavern.violenceInView());
    bool closing = false;
    for (int s = 0; s < 20 && !closing; ++s) {
        if (!tavern.playerHandsUp()) {
            raiseHands(room);
        }
        room.run(1);
        closing = tavern.watchStance() == Tavern::WatchStance::Closing &&
                  tavern.respondingWatchman() != nullptr &&
                  tavern.respondingWatchman()->id() == cullId;
        if (tavern.takeArrestRelease()) {
            break;
        }
    }
    REQUIRE(closing);
    CHECK(tavern.watchInterest() == WatchCause::Violence);

    // THE BLOW. He is a brawler now, and he means it: Kill, Lethal by B2.
    Tavern::PlayerSwingResult blow;
    for (int tries = 0; tries < 10 && blow.targetId != cullId; ++tries) {
        blow = tapAt(room, cullId, false);
    }
    REQUIRE(blow.targetId == cullId);
    CHECK(room.byId(cullId)->intent() == Intent::Kill);
    CHECK(blow.fight == FightClass::Lethal);
    const std::vector<Fighter> fight = tavern.currentFight();
    CHECK(std::any_of(fight.begin(), fight.end(),
                      [cullId](const Fighter& f) { return f.actorId == cullId; }));
    // He is fighting, not arresting: no arrest lands while the fight is on.
    room.run(1);
    CHECK_FALSE(tavern.takeArrestRelease());
    CHECK(tavern.watchStance() == Tavern::WatchStance::Closing);

    // KILLING HIM IS MURDER, not "resisting": slayActor's own law, witnessed
    // by the room -- instant paper and the Condemned hook.
    CrimeLedger& crimes = tavern.dialogue().crimes();
    REQUIRE_FALSE(crimes.murderer());
    bool killed = false;
    for (int swings = 0; swings < 40 && !killed && !tavern.playerFloored(); ++swings) {
        const Tavern::PlayerSwingResult hard = tapAt(room, cullId, true);
        killed = hard.killed;
        room.stepOnce();
    }
    REQUIRE(killed);
    CHECK(room.byId(cullId)->activity() == Activity::Dead);
    CHECK(crimes.murderer());
    CHECK(crimes.warrant());
    // A dead watchman closes on nobody: the stance resets (a corpse cannot
    // see -- noticeBy's floored rule now includes the Dead).
    room.run(2);
    CHECK(tavern.watchStance() == Tavern::WatchStance::Idle);
    CHECK_FALSE(tavern.takeArrestRelease());
}

// ===========================================================================
// RHYTHM -- the telegraph, the staggers, the guard
// ===========================================================================

TEST_CASE("an NPC blow is telegraphed: the wind-up runs kNpcWindupSteps (or the hard tell) and then lands") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
    REQUIRE(tap.targetId == markId);
    REQUIRE(tavern.playerInBrawl());

    // Watch him step by step: the first step the wind-up shows, it shows the
    // whole tell (the step that begins it does not decrement), the hard bit
    // says which tell, and the blow lands exactly when it reaches zero.
    int windupsSeen = 0;
    int lands = 0;
    for (int step = 0; step < 60 * 40 && windupsSeen < 3 && !tavern.playerFloored(); ++step) {
        const Actor* him = room.byId(markId);
        const std::int32_t before = him->npcWindup();
        const std::int32_t seqBefore = him->npcSwingSeq();
        room.stepOnce();
        him = room.byId(markId);
        if (before == 0 && him->npcWindup() > 0) {
            ++windupsSeen;
            CHECK(him->npcSwingSeq() == seqBefore + 1);  // one draw, at the wind-up
            const std::int32_t tell = him->npcWindup();
            const bool hard = him->npcWindupHard();
            CHECK(tell == (hard ? kNpcHardWindupSteps : kNpcWindupSteps));
            // The tell runs down one a step; on the landing step the hp moves
            // (or the die whiffed) and the wind-up is back to zero.
            const std::int32_t hpBefore = tavern.playerHp();
            for (int t = 1; t < tell; ++t) {
                room.stepOnce();
                CHECK(room.byId(markId)->npcWindup() == tell - t);
                CHECK(room.byId(markId)->npcSwingSeq() == seqBefore + 1);  // no second draw
            }
            room.stepOnce();
            CHECK(room.byId(markId)->npcWindup() == 0);
            CHECK(room.byId(markId)->npcSwingSeq() == seqBefore + 1);
            if (tavern.playerHp() < hpBefore) {
                ++lands;
            }
        }
    }
    INFO("wind-ups seen ", windupsSeen, ", blows landed ", lands);
    REQUIRE(windupsSeen >= 1);
}

TEST_CASE("a hit inside the wind-up pre-empts it: he staggers kStaggerSteps and the blow is lost") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(tapAt(room, markId, false).targetId == markId);

    bool preempted = false;
    for (int attempt = 0; attempt < 12 && !preempted && !tavern.playerFloored(); ++attempt) {
        // Wait for a wind-up on an OPEN man (guard down: the pre-empt is the
        // open-guard rule; a guard is the next case's business).
        bool winding = false;
        for (int step = 0; step < 60 * 10 && !winding; ++step) {
            room.stepOnce();
            const Actor* him = room.byId(markId);
            winding = him->npcWindup() > 2 && !him->npcGuard() && !isFloored(him->activity());
        }
        if (!winding) {
            break;
        }
        room.settleToIdle();
        const Actor* him = room.byId(markId);
        if (him->npcWindup() == 0 || him->npcGuard()) {
            continue;  // it landed while the arm settled; try the next one
        }
        const std::int32_t hpBefore = him->hp();
        const std::int32_t seqBefore = him->npcSwingSeq();
        // A TAP, straight into the tell.
        tavern.playerAttackDown();
        room.stepOnce();
        if (room.byId(markId)->npcWindup() == 0) {
            (void)tavern.playerAttackUp();
            continue;
        }
        const Tavern::PlayerSwingResult tap = tavern.playerAttackUp();
        if (!tap.blow.landed || tap.blow.downed) {
            continue;  // the 1-in-8 whiff, or he went down: no stagger to read
        }
        him = room.byId(markId);
        CHECK(tap.staggered);
        CHECK_FALSE(tap.recoiled);
        CHECK(him->hp() < hpBefore);
        CHECK(him->npcWindup() == 0);       // the blow he was winding up is lost
        CHECK(him->npcStagger() == kStaggerSteps);
        CHECK(him->npcSwingTimer() == kNpcSwingIntervalSteps);  // re-armed in full
        CHECK(him->npcSwingSeq() == seqBefore);  // the lost swing draws nothing more
        // The stagger runs down one a step; while it runs he does not wind up.
        for (int t = 1; t <= kStaggerSteps; ++t) {
            room.stepOnce();
            CHECK(room.byId(markId)->npcStagger() == kStaggerSteps - t);
            CHECK(room.byId(markId)->npcWindup() == 0);
        }
        preempted = true;
    }
    REQUIRE(preempted);
}

TEST_CASE("a HARD swing on an open man staggers him kStaggerSteps") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    bool proven = false;
    for (int attempt = 0; attempt < 12 && !proven && !tavern.playerFloored(); ++attempt) {
        const Actor* him = room.byId(markId);
        if (isFloored(him->activity())) {
            break;
        }
        if (him->npcGuard()) {
            room.run(1);  // wait out the guard (his next swing re-rolls it)
            continue;
        }
        const Tavern::PlayerSwingResult hard = tapAt(room, markId, true);
        if (hard.targetId != markId || !hard.blow.landed || hard.blow.downed) {
            continue;
        }
        him = room.byId(markId);
        CHECK(hard.hard);
        CHECK(hard.staggered);
        CHECK_FALSE(hard.blow.blocked);
        CHECK(him->npcStagger() == kStaggerSteps);
        proven = true;
    }
    REQUIRE(proven);
}

TEST_CASE("the guard and hard bands are carved off the NPC's OWN swing roll: same roll, one draw, no new stream") {
    // A bouncer: the professional band (half his swings guarded), so both
    // outcomes show up in a short fight. Every wind-up's guard bit and hard
    // bit are re-derived here from the pending roll the actor kept, and the
    // draw count is exactly one per wind-up.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* bouncer = room.findRole(ActorRole::Bouncer);
    REQUIRE(bouncer != nullptr);
    REQUIRE(tavern.isProfessional(*bouncer));
    const std::int32_t bId = bouncer->id();
    REQUIRE(room.standFacing(*bouncer) != -1);
    REQUIRE(room.swing(false).targetId == bId);
    // Guard held while he is watched: a bouncer's cudgel is seven a blow, and
    // the point is to see a dozen swings, not to lose the fight.
    tavern.setPlayerBlocking(true);

    int windups = 0;
    int guarded = 0;
    int open = 0;
    int hards = 0;
    std::int32_t seqAtStart = room.byId(bId)->npcSwingSeq();
    for (int step = 0; step < 60 * 60 && windups < 8 && !tavern.playerFloored(); ++step) {
        const std::int32_t before = room.byId(bId)->npcWindup();
        room.stepOnce();
        const Actor* him = room.byId(bId);
        if (before == 0 && him->npcWindup() > 0) {
            ++windups;
            const std::uint64_t roll = him->npcPendingRoll();
            const bool guardBit =
                ((roll >> kNpcGuardRollShift) & 0xFFU) < kNpcProfessionalGuardBand256;
            const bool hardBit = ((roll >> kNpcHardRollShift) & 0xFFU) < kNpcHardBand256;
            CHECK(him->npcGuard() == guardBit);
            CHECK(him->npcWindupHard() == hardBit);
            CHECK(him->npcWindup() == (hardBit ? kNpcHardWindupSteps : kNpcWindupSteps));
            guardBit ? ++guarded : ++open;
            if (hardBit) {
                ++hards;
            }
        }
    }
    INFO("wind-ups ", windups, " guarded ", guarded, " open ", open, " hard ", hards);
    REQUIRE(windups >= 4);
    // ONE DRAW PER SWING: the sequence moved exactly once per wind-up.
    CHECK(room.byId(bId)->npcSwingSeq() - seqAtStart == windups);
    CHECK(guarded >= 1);
    CHECK(open >= 1);
}

TEST_CASE("a tap into a raised guard is softened and RECOILS the arm for kRecoilSteps; a hard swing breaks it") {
    // One room per half, so each needs only one guarded window from the
    // bouncer (half his swings) before his cudgel settles the matter.
    const auto prove = [](bool hard) {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
        Tavern& tavern = room.tavern();
        room.run(2);
        const Actor* bouncer = room.findRole(ActorRole::Bouncer);
        REQUIRE(bouncer != nullptr);
        const std::int32_t bId = bouncer->id();
        REQUIRE(room.standFacing(*bouncer) != -1);
        REQUIRE(room.swing(false).targetId == bId);
        bool proven = false;
        for (int attempt = 0; attempt < 20 && !proven && !tavern.playerFloored(); ++attempt) {
            // Wait for his guard to come up (his own swing roll decides),
            // guard held meanwhile.
            tavern.setPlayerBlocking(true);
            bool up = false;
            for (int step = 0; step < 60 * 6 && !up; ++step) {
                room.stepOnce();
                const Actor* him = room.byId(bId);
                up = him->npcGuard() && him->npcStagger() == 0 && !isFloored(him->activity());
            }
            tavern.setPlayerBlocking(false);
            if (!up) {
                continue;
            }
            waitRhythm(room);
            room.settleToIdle();
            if (!room.byId(bId)->npcGuard()) {
                continue;
            }
            const std::int32_t hpBefore = room.byId(bId)->hp();
            const Tavern::PlayerSwingResult blow = tapAt(room, bId, hard);
            if (blow.targetId != bId || !blow.blow.landed || blow.blow.downed) {
                continue;
            }
            CHECK(blow.blow.blocked);
            // Softened: blockedDamage at level zero of what the roll would
            // have done, and never to nothing.
            CHECK(blow.blow.damage >= 1);
            CHECK(room.byId(bId)->hp() == hpBefore - blow.blow.damage);
            if (!hard) {
                CHECK(blow.recoiled);
                CHECK_FALSE(blow.staggered);
                CHECK(tavern.playerRecoilSteps() == kRecoilSteps);
                // A recoiling arm refuses the press, exactly as recovery
                // does; it counts down one a step, and the press is heard at
                // zero.
                room.settleToIdle();
                REQUIRE(tavern.playerCombatIdle());
                REQUIRE(tavern.playerRecoilSteps() > 0);
                tavern.playerAttackDown();
                CHECK(tavern.playerCombatIdle());  // dropped
                while (tavern.playerRecoilSteps() > 0) {
                    room.stepOnce();
                }
                tavern.playerAttackDown();
                CHECK_FALSE(tavern.playerCombatIdle());  // heard
                (void)tavern.playerAttackUp();
            } else {
                CHECK(blow.staggered);
                CHECK_FALSE(blow.recoiled);
                CHECK(tavern.playerRecoilSteps() == 0);
                CHECK(room.byId(bId)->npcStagger() == kBlockStaggerSteps);
                CHECK_FALSE(room.byId(bId)->npcGuard());  // broken
            }
            proven = true;
        }
        return proven;
    };
    CHECK(prove(false));
    CHECK(prove(true));
}

TEST_CASE("the player's guard catches a HARD swing and BREAKS: block-staggered kBlockStaggerSteps, guard not honoured, press refused") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(tapAt(room, markId, false).targetId == markId);
    room.settleToIdle();
    tavern.setPlayerBlocking(true);

    bool normalCaught = false;
    bool hardCaught = false;
    for (int step = 0; step < 60 * 120 && !(normalCaught && hardCaught) && !tavern.playerFloored();
         ++step) {
        const Actor* him = room.byId(markId);
        if (isFloored(him->activity()) || him->routing()) {
            break;
        }
        const bool landingNow = him->npcWindup() == 1;
        const bool hard = him->npcWindupHard();
        const std::int32_t hpBefore = tavern.playerHp();
        const std::int32_t staggerBefore = tavern.playerBlockStaggerSteps();
        room.stepOnce();
        if (!landingNow || tavern.playerHp() == hpBefore) {
            continue;  // no landing this step, or the die whiffed
        }
        if (staggerBefore > 0) {
            continue;  // the guard was already broken; this blow was unsoftened
        }
        if (hard) {
            CHECK(tavern.playerBlockStaggerSteps() == kBlockStaggerSteps);
            // Broken: the press is refused and the guard is not honoured
            // until the clock runs out.
            room.settleToIdle();
            tavern.playerAttackDown();
            CHECK(tavern.playerCombatIdle());
            while (tavern.playerBlockStaggerSteps() > 0) {
                room.stepOnce();
            }
            tavern.playerAttackDown();
            CHECK_FALSE(tavern.playerCombatIdle());
            (void)tavern.playerAttackUp();
            hardCaught = true;
        } else {
            CHECK(tavern.playerBlockStaggerSteps() == 0);  // a caught tap breaks nothing
            normalCaught = true;
        }
    }
    INFO("normal caught ", normalCaught, " hard caught ", hardCaught);
    CHECK(normalCaught);
    CHECK(hardCaught);
}

// ===========================================================================
// RHYTHM -- the room: stand back, refuse steel, rout
// ===========================================================================

TEST_CASE("patrons STAND BACK on the escalation edge, and the rota holds them there") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    // Who is close, before the blade: present, upright, not the mark, not a
    // professional, within kStandBackRadiusTiles of the player.
    struct Near {
        std::int32_t id;
        std::int32_t distance;
        std::int32_t destX;
        std::int32_t destY;
    };
    std::vector<Near> near;
    for (const Actor& actor : tavern.actors()) {
        if (!actor.present() || isFloored(actor.activity()) || actor.role() == ActorRole::Vermin ||
            actor.id() == markId || tavern.isProfessional(actor) ||
            actor.band() != gull::kGroundBand) {
            continue;
        }
        if (actor.distanceTo(room.px(), room.py()) <= kStandBackRadiusTiles * kSubOne) {
            near.push_back({actor.id(), actor.distanceTo(room.px(), room.py()),
                            actor.destinationX(), actor.destinationY()});
        }
    }
    REQUIRE_FALSE(near.empty());

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
    REQUIRE(tap.fight == FightClass::Lethal);
    REQUIRE(tavern.escalated());

    // The edge sent every one of them a tile or two directly away: a new
    // destination, further from the player than where they stood.
    int moved = 0;
    for (const Near& n : near) {
        const Actor* actor = room.byId(n.id);
        const std::int32_t dx = actor->destinationX() * kSubOne + kSubOne / 2 - room.px();
        const std::int32_t dy = actor->destinationY() * kSubOne + kSubOne / 2 - room.py();
        const std::int32_t destDistance = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
        if (actor->destinationX() != n.destX || actor->destinationY() != n.destY) {
            ++moved;
            CHECK(destDistance > n.distance);
            CHECK(actor->activity() == Activity::Walking);
            CHECK(gull::insideFootprint(actor->destinationX(), actor->destinationY()));
        }
    }
    INFO(near.size(), " near, ", moved, " sent back");
    CHECK(moved >= 1);
    // And the rota holds it: three seconds of a live lethal fight, and the
    // stand-back destinations stand (nobody is re-seated on a stool).
    std::vector<std::pair<std::int32_t, std::int32_t>> held;
    for (const Near& n : near) {
        held.emplace_back(room.byId(n.id)->destinationX(), room.byId(n.id)->destinationY());
    }
    for (int s = 0; s < 3 && tavern.lethalFightLive(); ++s) {
        room.run(1);
        for (std::size_t i = 0; i < near.size(); ++i) {
            const Actor* actor = room.byId(near[i].id);
            CHECK(actor->destinationX() == held[i].first);
            CHECK(actor->destinationY() == held[i].second);
        }
    }
}

TEST_CASE("bouncers REFUSE STEEL: the ejection ladder does not walk into a lethal fight; they hold the door") {
    // The brawl-class control first: a tap earns a warning that is DELIVERED
    // (Standing::Warned) inside a few seconds.
    {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
        Tavern& tavern = room.tavern();
        room.run(2);
        const Actor* mark = room.standFacingPatron();
        REQUIRE(mark != nullptr);
        REQUIRE(tapAt(room, mark->id(), false).targetId == mark->id());
        REQUIRE(tavern.playerStanding() == Standing::BeingWarned);
        bool warned = false;
        for (int s = 0; s < 30 && !warned; ++s) {
            room.run(1);
            warned = tavern.playerStanding() != Standing::BeingWarned;
        }
        CHECK(warned);
    }
    // Steel out: the same offence, and the ladder stands still while the
    // responder holds the door tile, facing the fight, Watching.
    {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
        Tavern& tavern = room.tavern();
        room.run(2);
        const Actor* mark = room.standFacingPatron();
        REQUIRE(mark != nullptr);
        tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
        const Tavern::PlayerSwingResult tap = tapAt(room, mark->id(), false);
        REQUIRE(tap.fight == FightClass::Lethal);
        REQUIRE(tavern.playerStanding() == Standing::BeingWarned);
        int seconds = 0;
        for (; seconds < 30; ++seconds) {
            room.run(1);
            if (!tavern.lethalFightLive() || tavern.playerFloored()) {
                // The fight ended inside that second (the room's blows can
                // kill under lethal rules now): the ladder is free to resume
                // and a defeat sets BeingEjected itself. Nothing to check.
                break;
            }
            CHECK(tavern.playerStanding() == Standing::BeingWarned);
            const Actor* door = tavern.respondingBouncer();
            REQUIRE(door != nullptr);
            CHECK(door->activity() == Activity::Watching);
            CHECK(door->destinationX() == gull::kDoorX0);
            CHECK(door->destinationY() == gull::kDoorY + 1);
        }
        INFO("held the door for ", seconds, " seconds of lethal fight");
        CHECK(seconds >= 3);
    }
}

TEST_CASE("ROUT: a bloodied non-professional under lethal rules breaks off for the street and leaves the fight on arrival") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.standFacingPatron();
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE_FALSE(tavern.isProfessional(*mark));
    // Fists, meaning it: Lethal by B2 with never a blade out, and taps only
    // so the man is BLOODIED before he is down.
    tavern.setPlayerCombat(Weapon::Fists, Intent::Kill);
    bool routing = false;
    for (int swings = 0; swings < 60 && !routing && !tavern.playerFloored(); ++swings) {
        const Actor* him = room.byId(markId);
        if (isFloored(him->activity())) {
            break;
        }
        const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
        if (tap.targetId != markId) {
            // Somebody stood back across the line mid-swing and caught it
            // (friendly fire is the aim's fault, VETO 1): he is in the fight
            // now, which is the rule working. The rout is the mark's.
            continue;
        }
        REQUIRE(tap.fight == FightClass::Lethal);
        for (int i = 0; i < 4 && !routing; ++i) {
            room.stepOnce();
            routing = room.byId(markId)->routing();
        }
    }
    REQUIRE(routing);
    const Actor* him = room.byId(markId);
    CHECK(isBloodied(him->hp(), him->hpMax()));
    CHECK(him->activity() == Activity::Walking);
    CHECK(him->destinationX() == gull::kStreetX);
    CHECK(him->destinationY() == gull::kStreetY);
    CHECK(him->npcWindup() == 0);
    // Still on the list while he walks, then off it the step he arrives.
    const auto markInFight = [&]() {
        const std::vector<Fighter> fight = tavern.currentFight();
        return std::any_of(fight.begin(), fight.end(),
                           [markId](const Fighter& f) { return f.actorId == markId; });
    };
    REQUIRE(markInFight());
    bool arrived = false;
    for (int s = 0; s < 60 && !arrived; ++s) {
        room.run(1);
        arrived = !markInFight();
    }
    REQUIRE(arrived);
    him = room.byId(markId);
    CHECK_FALSE(gull::insideFootprint(him->tileX(), him->tileY()));
    CHECK(him->hp() > 0);
    // He keeps the street (still routing, no rota) until the fight he ran
    // from is over -- with him alone in it, that is this same step; with a
    // bystander who caught a tap still swinging, it is that man's fight to
    // finish -- and then the rout ends with the fight and the hand means
    // Subdue again (a defeat resets it too, now).
    for (int s = 0; s < 90 && tavern.playerInBrawl(); ++s) {
        CHECK(him->routing());
        room.run(1);
    }
    REQUIRE_FALSE(tavern.playerInBrawl());
    CHECK_FALSE(him->routing());
    CHECK(tavern.currentFight().front().intent == Intent::Subdue);
}

TEST_CASE("a bloodied PROFESSIONAL does not rout") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* bouncer = room.findRole(ActorRole::Bouncer);
    REQUIRE(bouncer != nullptr);
    const std::int32_t bId = bouncer->id();
    tavern.setPlayerCombat(Weapon::Fists, Intent::Kill);
    tavern.setPlayerBlocking(true);  // his cudgel is seven a blow; the guard buys the time
    bool bloodied = false;
    for (int swings = 0; swings < 80 && !bloodied && !tavern.playerFloored(); ++swings) {
        const Actor* him = room.byId(bId);
        if (isFloored(him->activity())) {
            break;
        }
        (void)tapAt(room, bId, false);
        room.stepOnce();
        him = room.byId(bId);
        bloodied = isBloodied(him->hp(), him->hpMax()) && !isFloored(him->activity());
        CHECK_FALSE(him->routing());
    }
    INFO("bloodied: ", bloodied, " floored: ", tavern.playerFloored());
    if (bloodied) {
        for (int i = 0; i < 60; ++i) {
            room.stepOnce();
            CHECK_FALSE(room.byId(bId)->routing());
        }
    }
}

TEST_CASE("the rhythm is hashed and a scripted fight twin-runs byte-identical") {
    const auto script = [](Room& room) {
        Tavern& tavern = room.tavern();
        room.run(2);
        const Actor* bouncer = room.findRole(ActorRole::Bouncer);
        REQUIRE(bouncer != nullptr);
        const std::int32_t bId = bouncer->id();
        REQUIRE(room.standFacing(*bouncer) != -1);
        (void)room.swing(false);
        for (int s = 0; s < 6; ++s) {
            room.run(1);
            (void)tapAt(room, bId, s % 2 == 0);
        }
        tavern.setPlayerBlocking(true);
        room.run(3);
        tavern.setPlayerBlocking(false);
        room.run(2);
        return std::tuple{room.hash(), tavern.playerRecoilSteps(),
                          tavern.playerBlockStaggerSteps(), room.byId(bId)->npcSwingSeq(),
                          room.byId(bId)->npcPendingRoll()};
    };
    Room one(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Room two(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    const auto first = script(one);
    const auto second = script(two);
    CHECK(first == second);
    CHECK(std::get<3>(first) > 0);  // he swung
}

// ===========================================================================
// BARKS LANE (feel/build) -- the rows the fight lanes keyed, now authored
// ===========================================================================

namespace {

/// Whether `spoken` is `<name>: <row>` for some row of `key`'s own table --
/// the table itself, not a fallback, so watch.halt falling through to
/// watch.demand would be red here.
bool saidFromTable(const Tavern& tavern, const std::string& spoken, std::string_view name,
                   std::string_view key) {
    const std::string prefix = std::string(name) + ": ";
    if (spoken.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::vector<std::string>* rows = tavern.dialogue().barks().rows(key);
    if (rows == nullptr) {
        return false;
    }
    const std::string rest = spoken.substr(prefix.size());
    return std::find(rows->begin(), rows->end(), rest) != rows->end();
}

}  // namespace

TEST_CASE("the halt is watch.halt's own row, the man who steps in says brawl.join, the room says crowd.flee") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    // The three sheets are on the roster, three to five rows each, ASCII.
    for (std::string_view key : {"watch.halt", "brawl.join", "crowd.flee"}) {
        const std::vector<std::string>* rows = tavern.dialogue().barks().rows(key);
        INFO("key ", key);
        REQUIRE(rows != nullptr);
        CHECK(rows->size() >= 3);
        CHECK(rows->size() <= 5);
        for (const std::string& row : *rows) {
            REQUIRE_FALSE(row.empty());
            for (const char c : row) {
                REQUIRE(static_cast<unsigned char>(c) < 128);
            }
        }
    }
    CHECK(tavern.lastJoin().empty());
    CHECK(tavern.lastFlee().empty());
    CHECK(tavern.lastDemand().empty());

    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    const Actor* mark = room.standFacingPatronFor(*cull, /*seen=*/true);
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    const std::string markName = mark->name();
    // Somebody besides the mark within the stand-back radius, so the edge has
    // a patron to send back and a mouth to put the panic line in.
    bool bystander = false;
    for (const Actor& actor : tavern.actors()) {
        if (actor.id() != markId && actor.present() && !isFloored(actor.activity()) &&
            actor.role() != ActorRole::Vermin && !tavern.isProfessional(actor) &&
            actor.distanceTo(room.px(), room.py()) <=
                kStandBackRadiusTiles * kSubOne) {
            bystander = true;
        }
    }

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    const Tavern::PlayerSwingResult tap = tapAt(room, markId, false);
    REQUIRE(tap.targetId == markId);
    REQUIRE(tavern.lethalFightLive());
    // BRAWL.JOIN: the man the sightline found is in the fight and said so, in
    // his own name, off his own sheet.
    REQUIRE_FALSE(tavern.lastJoin().empty());
    CHECK(saidFromTable(tavern, tavern.lastJoin(), markName, "brawl.join"));
    // CROWD.FLEE: the escalation edge sent a patron back, and he said why.
    if (bystander) {
        REQUIRE_FALSE(tavern.lastFlee().empty());
        const std::string who = tavern.lastFlee().substr(0, tavern.lastFlee().find(": "));
        CHECK(saidFromTable(tavern, tavern.lastFlee(), who, "crowd.flee"));
        CHECK(who != "Watchman Cull");
    }

    // WATCH.HALT: Closing on Violence, and the halt is the halt -- not the
    // contraband demand it fell through to before the row was authored.
    bool closing = false;
    for (int s = 0; s < 20 && !closing; ++s) {
        room.run(1);
        closing = tavern.watchStance() == Tavern::WatchStance::Closing;
    }
    REQUIRE(closing);
    CHECK(tavern.watchInterest() == WatchCause::Violence);
    CHECK(saidFromTable(tavern, tavern.lastDemand(), "Watchman Cull", "watch.halt"));
    CHECK_FALSE(saidFromTable(tavern, tavern.lastDemand(), "Watchman Cull", "watch.demand"));
}
