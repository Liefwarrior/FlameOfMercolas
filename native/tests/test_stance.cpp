// FIGHTING MODE -- the stance, asserted at every seam it added.
//
// The owner's sentence: "When I press LMB I want to enter fighting mode and hit
// whoever is in front of me." oblivion-roadmap.md section 3.2 turned that into
// a SIM state (bool handsUp_ + int32 lowerTimer_ in sim::Tavern, hashed beside
// the swing machine) with a raise table and a lower table. Four things here,
// deliberately separate:
//
//   RAISE   the three ways the hands come up -- Attack down from IDLE (the same
//           press swings: a tap raises AND swings, a hold raises AND swings
//           hard), the guard going down, a blow caught -- and the one thing
//           that does not lower them (a guard release).
//   LOWER   the four ways they come down -- USE with nothing in reach, the
//           600-step lull at its exact edge, the cancel list (talking, picking,
//           sleeping, an arrest, a defeat, any page), stated one per case.
//   WALK    the LOWER HANDS slot of the interact walk, proved against its own
//           prompt: after a person, a fixture and a lead, before LOOK.
//   ROW     the FISTS UP row is the room's fact and weapon-named, and the
//           stance is in the hash: a scripted raise/lower/re-raise twin-runs
//           byte-identical, and hands up hashes differently from hands down.

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/nemesis.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

/// The same loop shape test_combat_action's Room has -- sixty movement steps
/// to the tick, the player held at a chosen tile and facing -- because the
/// stance is stepped by the same stepPlayerCombat the swing machine is.
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
    void stepOnce() {
        push();
        tavern_->stepMovement();
    }

    /// One simulated second: sixty steps and a tick.
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

    /// Steps until the swing machine is back to IDLE.
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

    /// Puts the body down somewhere and tells the room, which is what a
    /// movement step does sixty times a second.
    void place(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
        body_->placeAt(tileX, tileY, band);
        push();
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

    /// One tile off `mark` on a standable cardinal axis, facing it. Returns
    /// the yaw chosen, or -1.
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

    /// The combined hash of the world and the room, right now -- what the
    /// twin-run gate compares.
    [[nodiscard]] std::uint64_t hash() const { return engine_->combined_hash(); }

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

/// Hands up with the guard, then the guard let go: the one raise that leaves
/// no swing, no charge and no brawl behind it, so the case after it is about
/// exactly the rule it names.
void raiseByGuard(Tavern& tavern) {
    tavern.setPlayerBlocking(true);
    tavern.setPlayerBlocking(false);
    REQUIRE(tavern.playerHandsUp());
    REQUIRE_FALSE(tavern.playerBlocking());
}

/// A session standing in the Gilded Gull, test_interact_context's own shape.
render::SessionConfig gullAt(int hour, std::int32_t x, std::int32_t y, std::int32_t band,
                             int yawDegrees = 180) {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = band;
    config.spawnYaw = angle_from_degrees(yawDegrees);
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

/// Hands up through the client: a held guard for a step, then released --
/// the hands stay up (lower rule 4), the guard does not.
void raiseByGuard(render::Session& session) {
    // A page owning the keyboard lowers the hands every step, so this only
    // means anything with no page up -- which is how a test session boots
    // (the opening casebook page is a run_client-only default). Said loudly.
    REQUIRE_FALSE(session.casebookOpen());
    session.setBlocking(true);
    session.stepMany(MoveInput{}, 1);
    session.setBlocking(false);
    session.stepMany(MoveInput{}, 1);
    REQUIRE(session.tavern().playerHandsUp());
    REQUIRE_FALSE(session.tavern().playerBlocking());
}

}  // namespace

// ===========================================================================
// RAISE -- the three ways up, and the one release that is not a way down
// ===========================================================================

TEST_CASE("hands are down at boot, and a tap from hands-down raises and swings in one press") {
    // Five in the morning, an empty taproom: nothing on the line, so this is
    // the machine and the stance alone.
    Room room(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    CHECK_FALSE(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == 0);

    // THE OWNER'S SENTENCE. The down-edge from IDLE with the hands down puts
    // them up AND starts the charge -- one press.
    tavern.playerAttackDown();
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);
    CHECK_FALSE(tavern.playerCombatIdle());  // charging: the same press
    room.stepOnce();
    const Tavern::PlayerSwingResult tap = tavern.playerAttackUp();
    CHECK(tap.swung);
    CHECK_FALSE(tap.hard);
    CHECK(tavern.playerHandsUp());

    // And a HOLD from hands-down raises and swings HARD -- the same one press.
    tavern.lowerPlayerHands();
    REQUIRE_FALSE(tavern.playerHandsUp());
    room.settleToIdle();
    tavern.playerAttackDown();
    CHECK(tavern.playerHandsUp());
    for (int i = 0; i < kHardSwingHoldSteps + 1; ++i) {
        room.stepOnce();
    }
    const Tavern::PlayerSwingResult hard = tavern.playerAttackUp();
    CHECK(hard.swung);
    CHECK(hard.hard);
    CHECK(tavern.playerHandsUp());
}

TEST_CASE("the guard raises the hands, and letting the guard go does not lower them") {
    Room room(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    REQUIRE_FALSE(tavern.playerHandsUp());

    // Fists up without violence.
    tavern.setPlayerBlocking(true);
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);
    // Held, the guard keeps the clock full: no lull counts while it is up.
    for (int i = 0; i < 100; ++i) {
        room.stepOnce();
    }
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);
    // Released: hands STILL up (lower rule 4), and only now does the lull run.
    tavern.setPlayerBlocking(false);
    room.stepOnce();
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps - 1);
}

TEST_CASE("a blow caught in a brawl raises the hands, guarded or not") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    REQUIRE(room.standFacing(*mark) != -1);

    // Start the brawl with a tap (which raises the hands itself), then put
    // them down by hand so the raise below can only be the blow's.
    REQUIRE(room.swing(false).targetId == mark->id());
    tavern.lowerPlayerHands();
    REQUIRE_FALSE(tavern.playerHandsUp());
    REQUIRE_FALSE(tavern.currentFight().size() < 2);

    const std::int32_t hpBefore = tavern.playerHp();
    int seconds = 0;
    while (tavern.playerHp() == hpBefore && seconds < 60) {
        room.run(1);
        ++seconds;
    }
    REQUIRE(tavern.playerHp() < hpBefore);  // a blow landed
    // Without this a player being punched would have to press SWING before
    // GUARD meant anything.
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() > 0);
}

// ===========================================================================
// LOWER -- the lull at its edge, and the cancel list one rule per case
// ===========================================================================

TEST_CASE("the lull lowers the hands at 600 idle steps and not at 599") {
    Room room(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();

    // A tap at nobody: hands up, a swing thrown, the recovery lockout, then
    // IDLE. The lull counts IDLE steps only -- the charging step and the
    // recovery are not a lull, and the transition step into IDLE is not
    // either -- so the clock is still full the step the hand is free again.
    tavern.playerAttackDown();
    room.stepOnce();
    REQUIRE(tavern.playerAttackUp().swung);
    REQUIRE(tavern.playerHandsUp());
    room.settleToIdle();
    REQUIRE(tavern.playerCombatIdle());
    REQUIRE(tavern.currentFight().size() == 1);  // nobody swinging: a lull
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);

    for (int i = 0; i < kLowerHandsSteps - 1; ++i) {
        room.stepOnce();
    }
    // 599: still up, one step left on the clock.
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == 1);
    // 600: down.
    room.stepOnce();
    CHECK_FALSE(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == 0);
    // And it stays down: the lull is not a metronome.
    for (int i = 0; i < 100; ++i) {
        room.stepOnce();
    }
    CHECK_FALSE(tavern.playerHandsUp());
}

TEST_CASE("a swing thrown mid-lull refills the clock") {
    Room room(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    tavern.playerAttackDown();
    room.stepOnce();
    REQUIRE(tavern.playerAttackUp().swung);
    room.settleToIdle();
    for (int i = 0; i < 400; ++i) {
        room.stepOnce();
    }
    REQUIRE(tavern.playerLowerTimer() == kLowerHandsSteps - 400);
    // Another tap: the clock is full again and the hands never came down.
    tavern.playerAttackDown();
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);
    room.stepOnce();
    REQUIRE(tavern.playerAttackUp().swung);
    room.settleToIdle();
    CHECK(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == kLowerHandsSteps);
}

TEST_CASE("talking lowers the hands") {
    // Seven in the evening at the bar: the bartender is in talking reach.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    raiseByGuard(tavern);
    REQUIRE(tavern.talkTo());
    CHECK_FALSE(tavern.playerHandsUp());
    CHECK(tavern.playerLowerTimer() == 0);
    tavern.endConversation();
}

TEST_CASE("picking lowers the hands") {
    // A stranger's box on the upper floor (room 1, never rented), the same
    // stand test_interact_context picks from.
    const gull::GuestRoom& box = gull::kRooms[1];
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.place(box.standX, box.standY, gull::kUpperBand);
    REQUIRE(tavern.picks() > 0);
    raiseByGuard(tavern);
    const Tavern::PickResult wire = tavern.beginPick();
    REQUIRE(wire.result == ServiceResult::Served);
    CHECK_FALSE(tavern.playerHandsUp());
    tavern.abandonPick();
}

TEST_CASE("sleeping lowers the hands") {
    // Nine at night: the hour the REST case rents at, so the innkeeper is on.
    Room room(hourOfDay(21), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    // Rent a bed by standing on the innkeeper's own tile, then go up to it.
    const Actor* innkeeper = room.findRole(ActorRole::Innkeeper);
    REQUIRE(innkeeper != nullptr);
    room.place(innkeeper->tileX(), innkeeper->tileY(), gull::kGroundBand);
    REQUIRE(tavern.rentRoom() == ServiceResult::Served);
    const gull::GuestRoom& bed = gull::kRooms[tavern.rentedRoom()];
    room.place(bed.standX, bed.standY, gull::kUpperBand);
    raiseByGuard(tavern);
    REQUIRE(tavern.sleep() == ServiceResult::Served);
    CHECK_FALSE(tavern.playerHandsUp());
}

TEST_CASE("a defeat lowers the hands") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    raiseByGuard(tavern);
    tavern.concedeTo(mark->id());
    REQUIRE(tavern.playerFloored());
    CHECK_FALSE(tavern.playerHandsUp());
    // And the revive does not put them back up: a man waking on the quay
    // apron has his hands at his sides.
    tavern.reviveAfterDefeat();
    CHECK_FALSE(tavern.playerHandsUp());
}

TEST_CASE("an arrest lowers the hands") {
    // The proven-arrest setup (test_combat_action's deference case): a
    // condemned man standing in Watchman Cull's face at eleven at night is
    // taken in seconds. The hands are raised through the guard each second
    // and the guard let go, so nothing but the arrest can put them down.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    CrimeLedger& crimes = tavern.dialogue().crimes();
    crimes.markMurderer();
    (void)crimes.arrest(false, 0, 0);
    REQUIRE(crimes.condemned());
    bool arrested = false;
    for (int s = 0; s < 300 && !arrested; ++s) {
        if (const Actor* cull = room.findByName("Watchman Cull")) {
            room.standFacing(*cull);
        }
        raiseByGuard(tavern);
        room.run(1);
        arrested = tavern.takeArrestRelease();
    }
    REQUIRE(arrested);
    CHECK_FALSE(tavern.playerHandsUp());
}

// ===========================================================================
// WALK -- the LOWER HANDS slot, proved against its own prompt
// ===========================================================================

TEST_CASE("USE with nothing in reach lowers the hands, and the reticle said so first") {
    // Five in the morning on the stair itself: no person, no bed of the
    // player's own, no box underfoot, no lead -- the tile test_interact_context
    // proves resolves to LOOK.
    render::Session session(gullAt(5, gull::kStairX, gull::kStairY, gull::kUpperBand));
    REQUIRE(session.tavern().presentCount() == 0);
    REQUIRE(session.interactPrompt() == "LOOK");

    // A tap at nobody puts the hands up (and says NOBODY IN REACH).
    session.punch();
    REQUIRE(session.tavern().playerHandsUp());
    // THE PROMPT, BEFORE THE PRESS: the new slot, a bare verb with no subject.
    CHECK(session.lowerHandsResolves());
    CHECK(session.interactPrompt() == "LOWER HANDS");
    const render::Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "LOWER HANDS");
    CHECK(aim.subject.empty());
    CHECK(aim.note.empty());
    CHECK(aim.kind == render::AimKind::Nothing);

    // THE PRESS does what the prompt said, and only that.
    session.interact();
    CHECK_FALSE(session.tavern().playerHandsUp());
    CHECK_FALSE(session.talking());
    CHECK_FALSE(session.picking());
    // And the walk falls through to LOOK again with the hands down.
    CHECK_FALSE(session.lowerHandsResolves());
    CHECK(session.interactPrompt() == "LOOK");
}

TEST_CASE("a person in reach outranks LOWER HANDS: the press talks, and the talk lowers them") {
    render::Session session(gullAt(11, gull::kBartenderX, gull::kBarY - 1, gull::kGroundBand));
    session.stepMany(MoveInput{}, 2 * kStepsPerSecond);
    raiseByGuard(session);
    // The person slot comes first in both walks, so the prompt names the talk
    // with the hands up -- the LOWER HANDS slot is never reached.
    CHECK(session.interactPrompt() == "TALK");
    session.interact();
    REQUIRE(session.talking());
    CHECK_FALSE(session.tavern().playerHandsUp());
    session.closeConversation();
}

TEST_CASE("a fixture in reach outranks LOWER HANDS: the press picks the lock, and the pick lowers them") {
    const gull::GuestRoom& box = gull::kRooms[1];
    render::Session session(gullAt(5, box.standX, box.standY, gull::kUpperBand));
    REQUIRE(session.tavern().nearestTo(session.body().x(), session.body().y(), kReachQ8) ==
            nullptr);
    raiseByGuard(session);
    // The fixture slot comes before the LOWER HANDS slot in both walks.
    CHECK(session.interactPrompt() == "PICK LOCK");
    session.interact();
    REQUIRE(session.picking());
    CHECK_FALSE(session.tavern().playerHandsUp());
}

TEST_CASE("a lead the book has heard of outranks LOWER HANDS: the press looks, and a look keeps them up") {
    // The Mission back room -- the casebook's one `start` lead, Open from the
    // first minute, authored at (126,110) on the ground band.
    render::Session session(gullAt(11, 126, 110, gull::kGroundBand));
    REQUIRE(session.casebook().active());
    REQUIRE(session.leadInLookReach() >= 0);
    raiseByGuard(session);
    CHECK_FALSE(session.lowerHandsResolves());
    const render::Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "LOOK");
    CHECK(aim.kind == render::AimKind::Clue);
    session.interact();
    // The look read the lead and touched the stance not at all.
    CHECK(session.casebook().readCount() > 0);
    CHECK(session.tavern().playerHandsUp());
    // Read or not, a named lead is still something in reach: never LOWER HANDS.
    CHECK_FALSE(session.lowerHandsResolves());
    CHECK(session.interactTarget().verb == "LOOK");
}

TEST_CASE("any page lowers the hands and drops a live charge") {
    render::Session session(gullAt(5, gull::kStairX, gull::kStairY, gull::kUpperBand));
    session.stepMany(MoveInput{}, 2);
    // A charge held when the notes come up: the same step lowers the hands and
    // cancels the charge, with no release edge ever firing.
    session.attackDown();
    REQUIRE(session.tavern().playerHandsUp());
    REQUIRE_FALSE(session.tavern().playerCombatIdle());
    session.toggleCasebook();
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.tavern().playerHandsUp());
    CHECK(session.tavern().playerCombatIdle());
    CHECK(session.tavern().playerChargeSteps() == 0);
    // The release that arrives with the page still up throws nothing.
    session.attackUp();
    CHECK_FALSE(session.tavern().playerHandsUp());
    session.toggleCasebook();
    session.stepMany(MoveInput{}, 1);
    CHECK_FALSE(session.tavern().playerHandsUp());
}

// ===========================================================================
// ROW -- the FISTS UP row, and the stance in the hash
// ===========================================================================

TEST_CASE("the FISTS UP row is the room's fact and names the weapon") {
    render::Session session(gullAt(5, gull::kStairX, gull::kStairY, gull::kUpperBand));
    session.stepMany(MoveInput{}, 2);
    CHECK(session.handsLine().empty());

    session.punch();
    REQUIRE(session.tavern().playerHandsUp());
    CHECK(session.handsLine() == "FISTS UP");
    // Weapon-named off the sheet's own casing; STEEL is the register's word
    // for a blade, as the flip line and the epitaph already say it.
    REQUIRE(session.tavern().grantPlayerWeapon(kEvictorWeaponId));
    CHECK(session.handsLine() == "THE EVICTOR UP");
    session.tavern().setPlayerCombat(Weapon::Blunt, Intent::Subdue);
    CHECK(session.handsLine() == "CUDGEL UP");
    session.tavern().setPlayerCombat(Weapon::Improvised, Intent::Subdue);
    CHECK(session.handsLine() == "IMPROVISED UP");
    session.tavern().setPlayerCombat(Weapon::Edged, Intent::Subdue);
    CHECK(session.handsLine() == "STEEL UP");
    session.tavern().setPlayerCombat(Weapon::Fists, Intent::Subdue);
    CHECK(session.handsLine() == "FISTS UP");

    // Down by the verb, and the row says nothing rather than saying so.
    session.tavern().lowerPlayerHands();
    CHECK(session.handsLine().empty());
}

TEST_CASE("the stance is hashed: hands up and hands down are two different rooms") {
    // Two rooms, one seed, one difference -- the guard raised and let go in
    // one of them. playerBlocking_ ends false in both, so the only bytes that
    // can differ are the stance bit and its clock.
    Room up(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Room down(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    REQUIRE(up.hash() == down.hash());
    raiseByGuard(up.tavern());
    REQUIRE_FALSE(down.tavern().playerHandsUp());
    CHECK(up.hash() != down.hash());
    // And the clock alone moves it: one idle step of lull is a different hash.
    const std::uint64_t before = up.hash();
    up.stepOnce();
    CHECK(up.tavern().playerLowerTimer() == kLowerHandsSteps - 1);
    CHECK(up.hash() != before);
}

TEST_CASE("a scripted raise, guard, lower and re-raise twin-runs byte-identical") {
    // TWO ROOMS, ONE SEED, ONE SCRIPT -- the twin-run gate's question, asked
    // at the seam this build added. No wall clock, no new draw: every entry
    // and exit is a step event inside the sim.
    const auto script = [](Room& room) {
        Tavern& tavern = room.tavern();
        tavern.playerAttackDown();
        room.stepOnce();
        (void)tavern.playerAttackUp();
        for (int i = 0; i < 100; ++i) {
            room.stepOnce();
        }
        tavern.setPlayerBlocking(true);
        for (int i = 0; i < 10; ++i) {
            room.stepOnce();
        }
        tavern.setPlayerBlocking(false);
        for (int i = 0; i < 50; ++i) {
            room.stepOnce();
        }
        tavern.lowerPlayerHands();
        for (int i = 0; i < 30; ++i) {
            room.stepOnce();
        }
        tavern.playerAttackDown();
        for (int i = 0; i < kHardSwingHoldSteps + 1; ++i) {
            room.stepOnce();
        }
        (void)tavern.playerAttackUp();
        room.run(2);
        return std::tuple{room.hash(), tavern.playerHandsUp(), tavern.playerLowerTimer()};
    };
    Room one(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    Room two(hourOfDay(5), gull::kBartenderX, gull::kBarY - 1);
    const auto first = script(one);
    const auto second = script(two);
    CHECK(first == second);
    CHECK(std::get<1>(first));  // the script ends hands up, in the lull
    CHECK(std::get<2>(first) > 0);
}
