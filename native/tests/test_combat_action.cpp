// The action-combat build, asserted at the seams it added.
//
// Five things, and they are deliberately separate:
//
//   MACHINE   the swing state machine as a table -- IDLE/CHARGING/RECOVERY, the
//             charge tiers off the step count, the winded refusal -- driven
//             through the public verbs with no world assumptions in it.
//   RAYCAST   the sightline target, cross-checked against an independent
//             re-implementation of its own draw-free projection over the real
//             roster: where you look is who you hit, and the species preference
//             is gone.
//   LETHAL    a killing resolves in the world: Activity::Dead, the corpse
//             conventions, everyone can die, the vitality floor lifted.
//   LAW       the murder hook -- witnessed heat, the Condemned arrest -- and
//             the Watch's absolute deference to a presented Wielder.
//   VERB      intent-by-verb: the first hard swing means Harm, so a bloodied
//             man hard-swung goes Lethal with no blade drawn.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/watch.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// The same loop shape test_tavern's Room has -- sixty movement steps to the
/// tick -- with the player held at a chosen tile and facing, because the swing
/// machine and the sightline both read a body the caller is placing on purpose.
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
    [[nodiscard]] PlayerBody& body() noexcept { return *body_; }

    /// One movement step with the player standing still and facing `yaw_`.
    /// stepMovement drives stepPlayerCombat and stepBrawl, so the machine and
    /// the NPC cadence both advance here, exactly as the client's loop does.
    void stepOnce() {
        push();
        tavern_->stepMovement();
    }

    /// One simulated second: sixty steps and a tick (the tick runs the 1 Hz
    /// tickBrawl/tickWatch beside the per-step work).
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

    /// Steps until the swing machine is back to IDLE, so the next attackDown is
    /// heard rather than dropped in the recovery lockout.
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

    /// The first present, non-floored actor of a role (or any role when
    /// std::nullopt), or nullptr. Vermin are never returned.
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

    /// Puts the player one tile off `mark` on a standable cardinal axis and
    /// faces it, so the sightline runs straight through it (along ~= one tile,
    /// perp 0). Returns the yaw chosen, or -1 if no standable side exists.
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
            {0, -1, kFacingSouth},  // north of the mark, look south
            {0, 1, kFacingNorth},   // south of the mark, look north
            {-1, 0, kFacingEast},   // west of the mark, look east
            {1, 0, kFacingWest},    // east of the mark, look west
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

    [[nodiscard]] Angle yaw() const noexcept { return yaw_; }
    [[nodiscard]] std::int32_t px() const noexcept { return body_->x(); }
    [[nodiscard]] std::int32_t py() const noexcept { return body_->y(); }

    /// The first present, non-floored body on the look-ray, computed here from
    /// the projection VETO 1 specifies -- the reference sightlineTarget() is
    /// cross-checked against. Returns the actor id, or -1.
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

}  // namespace

// ===========================================================================
// MACHINE -- the swing state machine as a table
// ===========================================================================

TEST_CASE("the swing machine walks IDLE -> CHARGING -> RECOVERY and back") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();

    // At rest.
    CHECK(tavern.playerCombatIdle());
    CHECK(tavern.playerChargeSteps() == 0);
    CHECK_FALSE(tavern.playerChargeHard());

    // Down starts the charge; the counter climbs a step at a time and reads
    // hard once it reaches the hold threshold.
    tavern.playerAttackDown();
    CHECK_FALSE(tavern.playerCombatIdle());
    for (int i = 0; i < kHardSwingHoldSteps - 1; ++i) {
        room.stepOnce();
    }
    CHECK(tavern.playerChargeSteps() == kHardSwingHoldSteps - 1);
    CHECK_FALSE(tavern.playerChargeHard());
    room.stepOnce();
    CHECK(tavern.playerChargeSteps() == kHardSwingHoldSteps);
    CHECK(tavern.playerChargeHard());

    // Holding past the threshold changes nothing further -- exactly two tiers.
    for (int i = 0; i < 40; ++i) {
        room.stepOnce();
    }
    CHECK(tavern.playerChargeSteps() == kHardSwingHoldSteps);

    // Release: the swing resolves and the hand enters the recovery lockout.
    const Tavern::PlayerSwingResult result = tavern.playerAttackUp();
    CHECK(result.swung);
    CHECK(result.hard);
    CHECK_FALSE(tavern.playerCombatIdle());
    CHECK(tavern.playerChargeSteps() == 0);
    // The lockout counts down over the hard-swing recovery and returns to idle.
    for (int i = 0; i < kHardSwingRecoverySteps - 1; ++i) {
        room.stepOnce();
    }
    CHECK_FALSE(tavern.playerCombatIdle());
    room.stepOnce();
    CHECK(tavern.playerCombatIdle());
}

TEST_CASE("a tap is below the hold threshold; a down-edge in recovery is dropped") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();

    tavern.playerAttackDown();
    room.stepOnce();  // one step held: a tap
    const Tavern::PlayerSwingResult tap = tavern.playerAttackUp();
    CHECK(tap.swung);
    CHECK_FALSE(tap.hard);

    // Now in recovery: a down-edge is dropped, not buffered into a queued hard.
    REQUIRE_FALSE(tavern.playerCombatIdle());
    tavern.playerAttackDown();
    CHECK(tavern.playerChargeSteps() == 0);  // still recovering, not charging
    const Tavern::PlayerSwingResult none = tavern.playerAttackUp();
    CHECK_FALSE(none.swung);  // a release with no charge behind it throws nothing
}

TEST_CASE("a cast, a page or a conversation cancels a raised charge with no cost") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    tavern.playerAttackDown();
    room.stepOnce();
    room.stepOnce();
    REQUIRE(tavern.playerChargeSteps() > 0);
    tavern.cancelPlayerCharge();
    CHECK(tavern.playerCombatIdle());
    CHECK(tavern.playerChargeSteps() == 0);
    // A release after a cancel is a no-op, not a phantom swing.
    CHECK_FALSE(tavern.playerAttackUp().swung);
}

// ===========================================================================
// RAYCAST -- the sightline, against its own projection
// ===========================================================================

TEST_CASE("the sightline is a raycast: where you look is who you hit") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);  // let the room fill and settle

    // Stand one tile off a body and look straight at it. The sim's raycast must
    // agree with an independent re-implementation of the veto's own along/perp
    // projection -- the check is against the arithmetic, not a captured id --
    // and a body dead ahead must be on the line.
    const Actor* mark = room.findRole(ActorRole::Patron);
    if (mark == nullptr) {
        mark = room.findRole(ActorRole::Bouncer);
    }
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(room.standFacing(*mark) != -1);
    room.settleToIdle();
    room.stepOnce();  // one settled step: positions fixed, flag recomputed

    const std::int32_t ahead = room.expectedSightlineId();
    CHECK(ahead >= 0);                       // somebody is dead ahead
    CHECK(tavern.playerSightlineTarget());   // and the live flag says so
    tavern.playerAttackDown();
    CHECK(tavern.playerAttackUp().targetId == ahead);  // sim == reference

    // Turned to look the other way, the body is BEHIND the crosshair (along <
    // 0) and off the line -- where you look is who you hit.
    room.setYaw((room.yaw() + kTurnHalf) & (kTurnFull - 1));
    room.settleToIdle();
    room.stepOnce();
    const std::int32_t behind = room.expectedSightlineId();
    CHECK(behind != markId);  // the mark left the line when the crosshair turned
    tavern.playerAttackDown();
    CHECK(tavern.playerAttackUp().targetId == behind);  // sim == reference here too
}

TEST_CASE("a body off to the side, past the beam's half-width, is not on the line") {
    // Stand one tile off a body and look straight at it -- it is dead ahead.
    // Swing the crosshair a quarter turn without moving: the body's
    // perpendicular offset is now a whole tile, far past kBodyHalfWidth, so the
    // turned crosshair does not pass through it.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    const Actor* mark = room.findRole(ActorRole::Patron);
    if (mark == nullptr) {
        mark = room.findRole(ActorRole::Bouncer);
    }
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(room.standFacing(*mark) != -1);
    room.settleToIdle();
    room.stepOnce();
    // Dead ahead: on the line.
    REQUIRE(room.expectedSightlineId() >= 0);
    // A quarter turn: the same body is now a full tile off the ray.
    room.setYaw((room.yaw() + kTurnQuarter) & (kTurnFull - 1));
    room.stepOnce();
    const std::int32_t offToSide = room.expectedSightlineId();
    CHECK(offToSide != markId);  // a whole tile of perp is past the beam
    // And the sim's own raycast agrees with the reference at the turned facing.
    room.tavern().playerAttackDown();
    CHECK(room.tavern().playerAttackUp().targetId == offToSide);
}

// ===========================================================================
// LETHAL -- a killing resolves in the world
// ===========================================================================

TEST_CASE("under lethal rules a blow kills: Activity::Dead, and the corpse is a Downed that never stands") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    REQUIRE(room.standFacing(*mark) != -1);

    // Steel out. Edged is above the brawl line, so every swing is a lethal one.
    tavern.setPlayerCombat(Weapon::Edged, Intent::Subdue);

    // Beat the man on the line to death.
    std::int32_t victim = -1;
    for (int swings = 0; swings < 12 && victim < 0; ++swings) {
        const Tavern::PlayerSwingResult result = room.swing(true);
        if (result.killed) {
            victim = result.targetId;
            CHECK(result.fight == FightClass::Lethal);
        }
    }
    REQUIRE(victim >= 0);

    const Actor* corpse = tavern.actorById(victim);
    REQUIRE(corpse != nullptr);
    CHECK(corpse->activity() == Activity::Dead);
    CHECK(corpse->hp() == 0);

    // The corpse conventions: it is never removed from the roster, it never
    // heals or stands, and nearestTo skips it (a dead man closes no cases).
    CHECK(tavern.actorById(victim) != nullptr);
    room.run(120);  // two minutes: a Downed man would be up at a quarter
    const Actor* still = tavern.actorById(victim);
    REQUIRE(still != nullptr);
    CHECK(still->activity() == Activity::Dead);
    CHECK(still->hp() == 0);
}

TEST_CASE("everyone can die: no roster actor is shielded from a killing blow") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    // A named roster body -- the kind an old plot-armor classifier would have
    // shielded. No shield here: on the line, under steel, he dies, and his
    // identity had nothing to do with it.
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    REQUIRE(room.standFacing(*mark) != -1);
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);

    bool killed = false;
    for (int swings = 0; swings < 30 && !killed; ++swings) {
        const Tavern::PlayerSwingResult result = room.swing(true);
        killed = result.killed && result.targetId == markId;
    }
    REQUIRE(killed);
    const Actor* corpse = tavern.actorById(markId);
    REQUIRE(corpse != nullptr);
    CHECK(corpse->activity() == Activity::Dead);
    // Never removed from the roster -- a corpse is presence, not absence.
    CHECK(tavern.actorById(markId) != nullptr);
}

// ===========================================================================
// LAW -- murder, and the Watch's deference
// ===========================================================================

TEST_CASE("a witnessed killing is murder: instant paper, and the arrest condemns") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    REQUIRE(room.standFacing(*mark) != -1);
    CrimeLedger& crimes = tavern.dialogue().crimes();
    const std::int32_t heatBefore = crimes.heat();
    CHECK_FALSE(crimes.murderer());

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    bool killed = false;
    for (int swings = 0; swings < 30 && !killed; ++swings) {
        killed = room.swing(true).killed;
    }
    REQUIRE(killed);

    // A full taproom saw it (the three-clause witness rule), so it is instant
    // paper -- kMurderHeat is exactly the warrant line -- and it marks the
    // killer for the Condemned hook.
    CHECK(crimes.heat() >= heatBefore + kMurderHeat - 1);  // -1: cooling may nibble a point
    CHECK(crimes.warrant());
    CHECK(crimes.murderer());

    // The hook: a murderer, taken, is CONDEMNED whatever the theft ladder said.
    const CrimeLedger::ArrestOutcome outcome =
        crimes.arrest(/*skyrunner=*/false, /*purse=*/50, /*draw=*/0x1234ull);
    CHECK(outcome.sentence == Sentence::Condemned);
    CHECK(crimes.condemned());
}

TEST_CASE("an ordinary thief is only held; the murder mark is what condemns") {
    // The arrest hook in isolation, so the ladder and the override are both
    // visible. A warrant with no murder behind it holds; the mark condemns.
    CrimeLedger held;
    held.addHeat(kWarrantAt);  // paper, but no blood
    REQUIRE(held.warrant());
    CHECK(held.arrest(false, 100, 0x1ull).sentence == Sentence::Held);

    CrimeLedger slain;
    slain.markMurderer();  // raises kMurderHeat == kWarrantAt AND marks
    REQUIRE(slain.warrant());
    REQUIRE(slain.murderer());
    CHECK(slain.arrest(false, 100, 0x1ull).sentence == Sentence::Condemned);
}

TEST_CASE("the Watch never goes hostile to a presented Wielder") {
    // A CONDEMNED man standing where Watchman Cull can see him (hour 23, the
    // watch tests' own spot). Condemnation is recognised on sight at
    // kCondemnedRecognisePermille whatever the paper says, so with deference
    // OFF the Watch takes him deterministically; with it ON it never does --
    // no arrest, no closing, however long he stands there.
    const auto standCondemned = [](bool presentsAsWielder) {
        auto room = std::make_unique<Room>(hourOfDay(23), gull::kBartenderX,
                                           gull::kBartenderY + 1);
        Tavern& tavern = room->tavern();
        room->run(2);
        CrimeLedger& crimes = tavern.dialogue().crimes();
        crimes.markMurderer();             // a witnessed killing's paper
        (void)crimes.arrest(false, 0, 0);  // murderer -> Condemned on the record
        REQUIRE(crimes.condemned());
        tavern.setPlayerPresentsAsWielder(presentsAsWielder);
        bool arrested = false;
        for (int s = 0; s < 300 && !arrested; ++s) {
            // Stand in front of Cull each second -- the proven-arrest setup
            // (test_contract): a man standing in a watchman's face is taken in
            // seconds, so the counterplay is not being seen, and the deference
            // rule is what removes even that.
            if (const Actor* cull = room->findByName("Watchman Cull")) {
                room->standFacing(*cull);
            }
            room->run(1);
            arrested = tavern.takeArrestRelease();
        }
        return arrested;
    };
    // Off: the law is real -- a condemned man in a watchman's sight is taken.
    CHECK(standCondemned(false));
    // On: absolute deference. Not one arrest, ever.
    CHECK_FALSE(standCondemned(true));
}

// ===========================================================================
// VERB -- intent-by-verb
// ===========================================================================

TEST_CASE("intent-by-verb: the first hard swing means Harm") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    REQUIRE(room.standFacing(*mark) != -1);
    // Default is Subdue -- a bar fight forever with taps.
    REQUIRE(tavern.currentFight().front().intent == Intent::Subdue);
    REQUIRE(room.swing(false).targetId >= 0);  // a tap landed on the man
    CHECK(tavern.currentFight().front().intent == Intent::Subdue);
    // The first HARD swing on him sets Harm.
    const Tavern::PlayerSwingResult hard = room.swing(true);
    REQUIRE(hard.targetId >= 0);
    CHECK(tavern.currentFight().front().intent == Intent::Harm);
}

TEST_CASE("a bloodied man hard-swung goes Lethal with no blade drawn") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* markActor = room.findRole(ActorRole::Patron);
    REQUIRE(markActor != nullptr);
    const std::int32_t mark = markActor->id();
    REQUIRE(room.standFacing(*markActor) != -1);

    // Fists, the default. Tap him toward the bloodied quarter; taps are Subdue,
    // so this stays a brawl the whole way down (max fist tap is 5, so the tap
    // that crosses the quarter lands him bloodied-but-alive, never past it).
    bool everLethalTap = false;
    bool bloodied = false;
    for (int i = 0; i < 40 && !bloodied; ++i) {
        const Tavern::PlayerSwingResult tap = room.swing(false);
        if (tap.targetId == mark) {
            everLethalTap = everLethalTap || tap.fight == FightClass::Lethal;
        }
        const Actor* him = tavern.actorById(mark);
        bloodied = him != nullptr && him->activity() != Activity::Dead &&
                   isBloodied(him->hp(), him->hpMax());
    }
    REQUIRE(bloodied);
    // Fists + Subdue is a bar fight even at the bloodied quarter.
    CHECK_FALSE(everLethalTap);
    // A HARD swing now means Harm, and B3 (a bloodied man, meant Harm) is
    // Lethal -- with never a blade out. Beating a man to death with fists is
    // possible, is Lethal, and is murder.
    const Tavern::PlayerSwingResult hard = room.swing(true);
    REQUIRE(hard.targetId == mark);
    CHECK(hard.fight == FightClass::Lethal);
}

TEST_CASE("intent-by-verb, proven on the classifier directly: harm + bloodied is lethal, subdue is not") {
    // The rule the verb drives, stated without a room in the way (the same
    // shape test_tavern's classify table uses).
    const std::vector<Fighter> subdueBloodied{
        Fighter{0, Weapon::Fists, Intent::Subdue, 40, 40},
        Fighter{1, Weapon::Fists, Intent::Subdue, 5, 40}};
    CHECK(classifyFight(subdueBloodied) == FightClass::Brawl);
    const std::vector<Fighter> harmBloodied{
        Fighter{0, Weapon::Fists, Intent::Harm, 40, 40},
        Fighter{1, Weapon::Fists, Intent::Subdue, 5, 40}};
    CHECK(classifyFight(harmBloodied) == FightClass::Lethal);
}
