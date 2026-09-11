// JUSTICE BUILD -- the criminal tag, the arrest that goes to the Mission, and
// the plea and the check before the Flame's bench.
//
// THE RULING (Eli, 2026-09-02): "If the player is tagged as a criminal it
// should be like Daggerfall where you can go to court and you can face jail or
// execution (game over)." Four kinds of case, in the suite's usual order:
//
//   TAG     the three states the ledger keeps (paper, blood, the rope passed)
//           and the one HUD row that says them: WANTED / WANTED FOR BLOOD /
//           CONDEMNED / MAIMED. Cooling clears the paper and never the blood;
//           only the bench clears the blood; nothing clears the rope.
//   RULES   the charge sheet written draw-free off a seeded ledger; the
//           weighing's shape held against Ward::weighPetition's (the base,
//           the band, the lines); I DID IT the same answer under every draw;
//           I DID NOT moving by exactly the band, spared at the top and
//           DOUBLED below it; a witnessed murder never SPARED and a nobody
//           hanged (the spec's own worked examples, to the point); the rope
//           tier only where ruled; the hand sparable at 38; mercy once; and
//           the v5 codec round-tripping every field the court added.
//   ROOM    the Gilded Gull: an arrest with paper opens a hearing and the
//           clock does NOT jump; a search at the door is the shipped fast
//           path, untouched; the plea as a stepped input that trains the
//           tongue; a killing counts its witnesses onto the sheet; and two
//           rooms running one script to byte-identical hashes, diverging at
//           and only at the plea.
//   TWO PATHS  combat defeat never sets the rope's bit, and the court never
//           calls the quay: the canon's two player-end paths, kept apart in
//           code and asserted in both directions.
//   SENTENCES (the sentences lane) every branch's integers off a judged
//           hearing -- the fine and its shortfall days, the nights by charge
//           and doubled, BOUND's five, COMMUTED's twelve, the hand; the room
//           serving them (the purse, the world clock, the mirror, the Flame,
//           the body, the record AFTER the skip, the release); the world
//           moving while you are held through the shipped systems (the
//           ward's roll runs the days and the ground penny falls due, a taken
//           job dies at its night, the nemesis does NOT rise); COMMUTED
//           clearing the blood and never the condemned bit, mercy once; THE
//           ROPE as the run's end that revives nothing and refuses the world;
//           a Violence arrest flowing to the same bench; the sentence
//           twin-running byte-identical.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/contract.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/justice.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/watch.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

/// The Gull, an engine and a body -- test_contract's room with
/// test_combat_action's swing verbs, because a court case needs both a
/// watchman across the table and, once, a corpse.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(kSeed, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand,
                                             kFacingSouth)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, kSeed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        push();
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] std::uint64_t hash() const { return engine_->combined_hash(); }

    /// Ticks only: the 1 Hz watch loop, as test_contract drives an arrest.
    void run(int seconds) {
        for (int i = 0; i < seconds; ++i) {
            engine_->tick();
        }
    }

    /// One movement step, the body pushed first -- the swing machine's clock.
    void stepOnce() {
        push();
        tavern_->stepMovement();
    }

    void settleToIdle() {
        for (int i = 0; i < kHardSwingRecoverySteps + 4 && !tavern_->playerCombatIdle(); ++i) {
            stepOnce();
        }
    }

    Tavern::PlayerSwingResult swing(bool hard) {
        settleToIdle();
        tavern_->playerAttackDown();
        const int hold = hard ? kHardSwingHoldSteps + 1 : 1;
        for (int i = 0; i < hold; ++i) {
            stepOnce();
        }
        return tavern_->playerAttackUp();
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

    /// The first present, upright PATRON (not a professional) the player can
    /// stand facing AND whom `watcher` can then SEE the player from -- the
    /// three-clause notice rule asked of the room itself, test_watch_rhythm's
    /// own helper. nullptr when the room offers none.
    const Actor* standFacingPatronFor(const Actor& watcher) {
        for (const Actor& actor : tavern_->actors()) {
            if (!actor.present() || isFloored(actor.activity()) ||
                actor.role() != ActorRole::Patron || tavern_->isProfessional(actor) ||
                actor.id() == watcher.id()) {
                continue;
            }
            if (standFacing(actor) == -1) {
                continue;
            }
            if (tavern_->noticeBy(watcher).seen) {
                return &actor;
            }
        }
        return nullptr;
    }

    /// The first present, non-floored body on the look-ray, from the
    /// projection VETO 1 specifies. Returns the actor id, or -1.
    [[nodiscard]] std::int32_t expectedSightlineId() const {
        const std::int64_t fx = forward_x_q16(yaw_);
        const std::int64_t fy = forward_y_q16(yaw_);
        std::int32_t best = -1;
        std::int64_t bestAlong = static_cast<std::int64_t>(kMeleeReach) + 1;
        for (const Actor& actor : tavern_->actors()) {
            if (!actor.present() || isFloored(actor.activity())) {
                continue;
            }
            const std::int64_t dx = static_cast<std::int64_t>(actor.x()) - body_->x();
            const std::int64_t dy = static_cast<std::int64_t>(actor.y()) - body_->y();
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

    /// One tile off `mark` on a standable cardinal side, facing it, so the
    /// sightline runs through it. Returns the yaw chosen, or -1.
    Angle standFacing(const Actor& mark) {
        struct Side {
            std::int32_t dx;
            std::int32_t dy;
            Angle yaw;
        };
        const Side sides[] = {
            {0, -1, kFacingSouth}, {0, 1, kFacingNorth}, {-1, 0, kFacingEast}, {1, 0, kFacingWest},
        };
        for (const Side& s : sides) {
            const std::int32_t px = mark.tileX() + s.dx;
            const std::int32_t py = mark.tileY() + s.dy;
            if (tiles_->standable(px, py, mark.band())) {
                body_->placeAt(px, py, mark.band());
                yaw_ = s.yaw;
                body_->setYaw(yaw_);
                push();
                return s.yaw;
            }
        }
        return -1;
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
    Angle yaw_ = kFacingSouth;
    Tavern* tavern_ = nullptr;
};

/// Stands in Watchman Cull's face every second until he takes you, or gives
/// up after `ceiling` seconds. The proven-arrest setup test_contract uses.
[[nodiscard]] bool standUntilTaken(Room& room, int ceiling = 900) {
    for (int second = 0; second < ceiling && !room.tavern().lastArrest().happened; ++second) {
        (void)room.standBy("Watchman Cull");
        room.run(1);
    }
    return room.tavern().lastArrest().happened;
}

/// Puts `markId` down for good under lethal rules, in as many hard swings as
/// it takes: re-stands on him each time (a staggered man moves), waits out
/// the player's own recoil and block-stagger clocks so the press is heard,
/// and waits for the line to clear rather than swing at whoever stepped in.
[[nodiscard]] bool killInSight(Room& room, std::int32_t markId) {
    Tavern& tavern = room.tavern();
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    for (int swings = 0; swings < 40; ++swings) {
        const Actor* mark = room.byId(markId);
        if (mark == nullptr) {
            return false;
        }
        if (mark->activity() == Activity::Dead) {
            return true;
        }
        for (int i = 0; i < kRecoilSteps + kBlockStaggerSteps + 4 &&
                        (tavern.playerRecoilSteps() > 0 || tavern.playerBlockStaggerSteps() > 0);
             ++i) {
            room.stepOnce();
        }
        if (room.standFacing(*mark) == -1) {
            return false;
        }
        for (int i = 0; i < 30 && room.expectedSightlineId() != markId; ++i) {
            room.stepOnce();
            if (room.standFacing(*mark) == -1) {
                return false;
            }
        }
        if (room.swing(true).killed) {
            return true;
        }
    }
    return false;
}

/// A sheet written by hand, so the weighing can be driven as the pure
/// function it is. Tier PAPER unless said otherwise; every plea input zero.
[[nodiscard]] ChargeSheet sheetOf(Sentence tier) {
    ChargeSheet sheet;
    sheet.written = true;
    sheet.tier = tier;
    sheet.heatAtArrest = kWarrantAt;
    return sheet;
}

/// The draw whose band above the nights reads exactly `jitter` (-10..+10):
/// bit sixteen and up carry `jitter + 10`, the low residue is left at zero.
[[nodiscard]] std::uint64_t drawWithBand(std::int32_t jitter) {
    return static_cast<std::uint64_t>(jitter + kPriestBand) << kPriestBandShift;
}

[[nodiscard]] const ArraignmentTerm* termNamed(const Arraignment& answer, std::string_view name) {
    for (const ArraignmentTerm& term : answer.terms) {
        if (term.name == name) {
            return &term;
        }
    }
    return nullptr;
}

[[nodiscard]] granadad::render::SessionConfig quietDocks(int hour = 20) {
    granadad::render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnYaw = kFacingNorth;
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

// ===========================================================================
// TAG
// ===========================================================================

TEST_CASE("the tag is three states the ledger keeps, and each clears by its own road") {
    // WANTED: paper. Cooling clears it, and so does the bench.
    CrimeLedger paper;
    paper.addHeat(kWarrantAt + 4);
    CHECK(paper.warrant());
    CHECK_FALSE(paper.murderer());
    CHECK_FALSE(paper.condemned());
    paper.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax);
    CHECK_FALSE(paper.warrant());

    // WANTED FOR BLOOD: paper with a corpse behind it. Cooling clears the
    // paper and NEVER the blood; a lost file and a week in a roost do not
    // touch it either; the next arrest on any paper is a rope hearing.
    CrimeLedger blood;
    blood.markMurderer(3);
    CHECK(blood.warrant());
    CHECK(blood.murderer());
    CHECK(blood.slewWitnesses() == 3);
    blood.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax);
    CHECK_FALSE(blood.warrant());
    CHECK(blood.murderer());
    blood.quashWarrant();
    blood.lieLow();
    CHECK(blood.murderer());
    CHECK(blood.charge(false, 0, 0, 0).tier == Sentence::Condemned);
    // Only the bench clears it: COMMUTED serves the blood.
    blood.sentence(Judgment::Commuted, 12);
    CHECK_FALSE(blood.murderer());

    // CONDEMNED: the rope passed and commuted, for the rest of the run. The
    // hand went with it, mercy is spent, and NOTHING clears it.
    CHECK(blood.condemned());
    CHECK(blood.commuted());
    CHECK(blood.maimed());
    CHECK(blood.takePercent() == kMaimedTakePercent);
    blood.lieLow();
    blood.quashWarrant();
    blood.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax * 2);
    CHECK(blood.condemned());
    CHECK(blood.commuted());
    // And the rope itself is its own bit, set by no other judgment.
    CHECK_FALSE(blood.executed());
    for (const Judgment mercy : {Judgment::Spared, Judgment::Fined, Judgment::Held,
                                 Judgment::Bound, Judgment::TheHand, Judgment::Commuted}) {
        CrimeLedger lived;
        lived.sentence(mercy, 1);
        CHECK_FALSE(lived.executed());
    }
    CrimeLedger hanged;
    hanged.sentence(Judgment::TheRope, 0);
    CHECK(hanged.executed());
}

TEST_CASE("the status row says the tag: WANTED, WANTED FOR BLOOD, CONDEMNED, MAIMED") {
    using granadad::render::Session;
    Session session(quietDocks());
    CrimeLedger& crimes = session.tavern().dialogue().crimes();
    CHECK(session.heatLine().empty());  // the ward has heard nothing

    crimes.addHeat(kWarrantAt + 10);
    CHECK(session.heatLine() == "WANTED  HEAT 70");

    // Paper with a corpse behind it: the new phrase, and the promote rule
    // (hud.cpp reads WANTED off the front of the label) still fires on it.
    crimes.markMurderer(2);
    CHECK(session.heatLine() == "WANTED FOR BLOOD  HEAT 100");
    CHECK(session.heatLine().rfind("WANTED", 0) == 0);

    // A murderer whose heat has cooled is not WANTED on the row -- the
    // paper lapsed -- and the row invents no word for the blood: heat stays
    // a number, standing stays a phrase, and a ward that has heard nothing
    // prints nothing, exactly as shipped. The blood is on the ledger for the
    // next arrest to read, not on the HUD.
    crimes.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax);
    CHECK(crimes.murderer());
    CHECK(session.heatLine().empty());
    // Warm again, and the phrase is back: the paper carries the corpse.
    crimes.addHeat(kWarrantAt);
    CHECK(session.heatLine() == "WANTED FOR BLOOD  HEAT " + std::to_string(kWarrantAt));

    // The rope passed and commuted: the shipped word with the court's
    // meaning, for the rest of the run, over kHeatAfterSentence.
    crimes.sentence(Judgment::Commuted, 12);
    CHECK(session.heatLine() == "CONDEMNED  HEAT " + std::to_string(kHeatAfterSentence));

    // And the hand alone, on a fresh record.
    Session other(quietDocks());
    other.tavern().dialogue().crimes().sentence(Judgment::TheHand, 1);
    CHECK(other.heatLine() == "MAIMED  HEAT " + std::to_string(kHeatAfterSentence));
}

// ===========================================================================
// RULES -- the charge sheet
// ===========================================================================

TEST_CASE("the charge sheet is written draw-free off the ledger, and names what is new") {
    CrimeLedger ledger;
    ledger.commit(Crime::Lift, true);
    ledger.commit(Crime::Lift, true);
    ledger.commit(Crime::Burgle, true);
    ledger.commit(Crime::RoofRun, false);
    ledger.addHeat(30);  // 8 + 8 + 18 + 30 = 64: paper
    REQUIRE(ledger.warrant());

    const ChargeSheet first = ledger.charge(false, 12, 30, -15);
    CHECK(first.written);
    CHECK(first.tier == Sentence::Held);
    CHECK_FALSE(first.blood);
    // The one line the sheet names is the highest-heat act with a count.
    CHECK(first.hasWorst);
    CHECK(first.worst == Crime::Burgle);
    CHECK(first.since[static_cast<std::size_t>(Crime::Lift)] == 2);
    CHECK(first.since[static_cast<std::size_t>(Crime::Burgle)] == 1);
    CHECK(first.since[static_cast<std::size_t>(Crime::RoofRun)] == 1);
    CHECK(first.since[static_cast<std::size_t>(Crime::Smuggle)] == 0);
    CHECK(first.priors == 0);
    CHECK(first.heatAtArrest == ledger.heat());
    // The plea inputs are captured, verbatim, at the arrest.
    CHECK(first.streetwise == 12);
    CHECK(first.templeStanding == 30);
    CHECK(first.reputation == -15);
    CHECK_FALSE(first.condemnedBefore);
    CHECK_FALSE(first.commutedBefore);
    // And writing it changed nothing: the same call is the same sheet.
    const ChargeSheet again = ledger.charge(false, 12, 30, -15);
    CHECK(again.worst == first.worst);
    CHECK(again.heatAtArrest == first.heatAtArrest);
    CHECK(ledger.arrests() == 0);
    CHECK(ledger.warrant());

    // A sentence served covers the tallies: the next sheet names only what
    // came after it, and the prior is on it.
    ledger.sentence(Judgment::Held, 2);
    CHECK(ledger.servedTally(Crime::Lift) == 2);
    CHECK(ledger.servedTally(Crime::Burgle) == 1);
    ledger.commit(Crime::Lift, true);
    ledger.addHeat(kWarrantAt);
    const ChargeSheet second = ledger.charge(false, 0, 0, 0);
    CHECK(second.tier == Sentence::Held);
    CHECK(second.worst == Crime::Lift);
    CHECK(second.since[static_cast<std::size_t>(Crime::Lift)] == 1);
    CHECK(second.since[static_cast<std::size_t>(Crime::Burgle)] == 0);
    CHECK(second.priors == 1);

    // Blood names the killing over any theft, and the tier is the rope.
    CrimeLedger killer;
    killer.commit(Crime::Smuggle, true);
    killer.markMurderer(3);
    const ChargeSheet rope = killer.charge(false, 0, 0, 0);
    CHECK(rope.tier == Sentence::Condemned);
    CHECK(rope.blood);
    CHECK(rope.witnesses == 3);
    CHECK_FALSE(rope.secondRung);

    // The roofs: the hand on the first, the rope on the second -- the
    // ladder's own answer, and THE SECOND RUNG is on the second's sheet.
    CrimeLedger roofs;
    roofs.addHeat(90);
    const ChargeSheet hand = roofs.charge(true, 0, 0, 0);
    CHECK(hand.tier == Sentence::Maimed);
    CHECK(hand.skyrunner);
    CHECK_FALSE(hand.secondRung);
    roofs.sentence(Judgment::TheHand, 1);
    roofs.addHeat(90);
    const ChargeSheet twice = roofs.charge(true, 0, 0, 0);
    CHECK(twice.tier == Sentence::Condemned);
    CHECK(twice.secondRung);
    CHECK_FALSE(twice.blood);
    CHECK(twice.priors == 1);
}

TEST_CASE("a paperless search never reaches the bench") {
    CrimeLedger clean;
    clean.stash().add(Contraband::Moonshine, 4);
    const ChargeSheet search = clean.charge(false, 0, 0, 0);
    CHECK(search.tier == Sentence::Fined);
    // The weighing refuses it, and so does the hearing.
    CHECK_FALSE(weighArraignment(search, Plea::Guilty).heard);
    CHECK_FALSE(weighArraignment(search, Plea::NotGuilty).heard);
    clean.openHearing(search, 4, 0x1234u, "Watchman Cull");
    CHECK_FALSE(clean.hearingPending());
    CHECK_FALSE(clean.plead(Plea::Guilty).heard);
    // An unwritten sheet is not a sheet.
    CHECK_FALSE(weighArraignment(ChargeSheet{}, Plea::Guilty).heard);
}

// ===========================================================================
// RULES -- the weighing
// ===========================================================================

TEST_CASE("weighArraignment is weighPetition's shape: the base, the caps, the band, the lines") {
    // THE FLAME leads every sum at weighPetition's own zero point.
    const Arraignment bare = weighArraignment(sheetOf(Sentence::Held), Plea::Guilty);
    REQUIRE(bare.heard);
    REQUIRE_FALSE(bare.terms.empty());
    CHECK(bare.terms.front().name == kTermFlame);
    CHECK(bare.terms.front().value == kFlameBase);
    CHECK(kFlameBase == 24);
    CHECK(bare.weight == kFlameBase);  // nothing else weighed: zero lines are not printed
    CHECK(bare.pleaTerm == kConfessedTerm);
    CHECK(bare.scored == kFlameBase + kConfessedTerm);

    // Every term capped where the spec caps it.
    ChargeSheet loaded = sheetOf(Sentence::Held);
    loaded.streetwise = 100;
    loaded.templeStanding = 100;
    loaded.reputation = -100;
    loaded.priors = 9;
    loaded.heatAtArrest = 100;
    const Arraignment capped = weighArraignment(loaded, Plea::Guilty);
    REQUIRE(termNamed(capped, kTermTongue) != nullptr);
    CHECK(termNamed(capped, kTermTongue)->value == 20);
    REQUIRE(termNamed(capped, kTermDoor) != nullptr);
    CHECK(termNamed(capped, kTermDoor)->value == 24);
    REQUIRE(termNamed(capped, kTermWard) != nullptr);
    CHECK(termNamed(capped, kTermWard)->value == -20);
    REQUIRE(termNamed(capped, kTermTakenBefore) != nullptr);
    CHECK(termNamed(capped, kTermTakenBefore)->value == -30);
    REQUIRE(termNamed(capped, kTermHeat) != nullptr);
    CHECK(termNamed(capped, kTermHeat)->value == -10);
    CHECK(termNamed(capped, kTermBlood) == nullptr);  // no corpse, no line
    CHECK(capped.weight == 24 + 20 + 24 - 20 - 30 - 10);
    ChargeSheet seen = sheetOf(Sentence::Condemned);
    seen.blood = true;
    seen.witnesses = 9;
    const Arraignment crowd = weighArraignment(seen, Plea::Guilty);
    REQUIRE(termNamed(crowd, kTermSawIt) != nullptr);
    CHECK(termNamed(crowd, kTermSawIt)->value == -16);
    CHECK(termNamed(crowd, kTermSawIt)->count == 9);
    REQUIRE(termNamed(crowd, kTermBlood) != nullptr);
    CHECK(termNamed(crowd, kTermBlood)->value == -30);

    // THE BAND is compound.cpp's own `draw % 21 - 10`, read off the bits
    // above the nights: for every value of the band, the term is exactly the
    // band less ten, and the nights the same draw gives are watch.hpp's own.
    for (std::int32_t jitter = -kPriestBand; jitter <= kPriestBand; ++jitter) {
        ChargeSheet sheet = sheetOf(Sentence::Held);
        sheet.draw = drawWithBand(jitter);
        const Arraignment denial = weighArraignment(sheet, Plea::NotGuilty);
        REQUIRE(denial.heard);
        CHECK(denial.pleaTerm == jitter);
        REQUIRE(termNamed(denial, kTermPriestIsAMan) != nullptr);
        CHECK(termNamed(denial, kTermPriestIsAMan)->value == jitter);
        CHECK(denial.scored == denial.weight + jitter);
        CHECK(heldHours(sheet.draw) == kHeldHoursMin + static_cast<std::int32_t>(sheet.draw % 49U));
        // SAME-ROLL DISCIPLINE: the band is blind to the nights' residue. The
        // same high bits over ANY low bits read the same doubt, while the
        // nights under them move -- one draw, two residues, never shared.
        ChargeSheet lowBits = sheet;
        lowBits.draw |= 0xBEEFu;
        const Arraignment same = weighArraignment(lowBits, Plea::NotGuilty);
        CHECK(same.pleaTerm == jitter);
        CHECK(same.judgment == denial.judgment);
        CHECK(heldHours(lowBits.draw) != heldHours(sheet.draw));
    }
    // A denial never confesses and a confession is never a man's doubt.
    const Arraignment confessed = weighArraignment(sheetOf(Sentence::Held), Plea::Guilty);
    CHECK(termNamed(confessed, kTermPriestIsAMan) == nullptr);
    CHECK(termNamed(confessed, kTermConfessed) != nullptr);
    const Arraignment denied = weighArraignment(sheetOf(Sentence::Held), Plea::NotGuilty);
    CHECK(termNamed(denied, kTermConfessed) == nullptr);

    // THE LINES, monotone: 55 SPARED (a denial only) / 38 FINED / 14 HELD /
    // BOUND under the last.
    CHECK(paperBand(kSparedLine, Plea::NotGuilty) == Judgment::Spared);
    CHECK(paperBand(kSparedLine, Plea::Guilty) == Judgment::Fined);
    CHECK(paperBand(kSparedLine - 1, Plea::NotGuilty) == Judgment::Fined);
    CHECK(paperBand(kFinedLine, Plea::Guilty) == Judgment::Fined);
    CHECK(paperBand(kFinedLine - 1, Plea::Guilty) == Judgment::Held);
    CHECK(paperBand(kHeldLine, Plea::Guilty) == Judgment::Held);
    CHECK(paperBand(kHeldLine - 1, Plea::Guilty) == Judgment::Bound);
    CHECK(paperBand(-40, Plea::NotGuilty) == Judgment::Bound);
    // No plea at all is nothing, and a hearing with a plea refuses silence.
    CHECK_FALSE(weighArraignment(sheetOf(Sentence::Held), Plea::None).heard);
    CHECK_FALSE(weighArraignment(sheetOf(Sentence::Held), Plea::NoPlea).heard);
}

TEST_CASE("I DID IT is draw-free: the same record is the same answer under every draw") {
    ChargeSheet sheet = sheetOf(Sentence::Held);
    sheet.streetwise = 12;
    sheet.templeStanding = 30;
    sheet.reputation = -15;
    sheet.heatAtArrest = 68;
    const Arraignment reference = weighArraignment(sheet, Plea::Guilty);
    REQUIRE(reference.heard);
    // The spec's first-time thief: 24 + 6 + 10 - 3 - 2 = 35; confessed, 41,
    // FINED and no cell. Daggerfall's lesson -- confess the small ones.
    CHECK(reference.weight == 35);
    CHECK(reference.scored == 41);
    CHECK(reference.judgment == Judgment::Fined);
    CHECK_FALSE(reference.doubled);
    for (const std::uint64_t draw : {0ull, 1ull, 0x1234ull, 0xFFFFull, 0xFFFFFFFFull,
                                     0xDEADBEEFCAFEull, ~0ull}) {
        sheet.draw = draw;
        const Arraignment same = weighArraignment(sheet, Plea::Guilty);
        CHECK(same.scored == reference.scored);
        CHECK(same.judgment == reference.judgment);
        CHECK(same.pleaTerm == kConfessedTerm);
        CHECK_FALSE(same.doubled);
        REQUIRE(same.terms.size() == reference.terms.size());
        for (std::size_t i = 0; i < same.terms.size(); ++i) {
            CHECK(same.terms[i].name == reference.terms[i].name);
            CHECK(same.terms[i].value == reference.terms[i].value);
        }
    }
    // And a confession is never SPARED, however good the record: the best
    // case is the fine.
    ChargeSheet saint = sheetOf(Sentence::Held);
    saint.streetwise = 40;
    saint.templeStanding = 84;
    saint.reputation = 100;
    const Arraignment best = weighArraignment(saint, Plea::Guilty);
    CHECK(best.scored >= kSparedLine);
    CHECK(best.judgment == Judgment::Fined);
}

TEST_CASE("I DID NOT moves with the band: spared at the top, doubled below it") {
    // The same thief who confessed to FINED above, denying: 25..45 across the
    // band -- HELD doubled thirteen times in twenty-one, FINED doubled eight,
    // never SPARED. The spec's own count.
    ChargeSheet thief = sheetOf(Sentence::Held);
    thief.streetwise = 12;
    thief.templeStanding = 30;
    thief.reputation = -15;
    thief.heatAtArrest = 68;
    int heldDoubled = 0;
    int finedDoubled = 0;
    int spared = 0;
    for (std::int32_t jitter = -kPriestBand; jitter <= kPriestBand; ++jitter) {
        thief.draw = drawWithBand(jitter);
        const Arraignment answer = weighArraignment(thief, Plea::NotGuilty);
        REQUIRE(answer.heard);
        CHECK(answer.scored == 35 + jitter);
        if (answer.judgment == Judgment::Spared) {
            ++spared;
        } else if (answer.judgment == Judgment::Held) {
            CHECK(answer.doubled);
            ++heldDoubled;
        } else if (answer.judgment == Judgment::Fined) {
            CHECK(answer.doubled);
            ++finedDoubled;
        }
    }
    CHECK(spared == 0);
    CHECK(heldDoubled == 13);
    CHECK(finedDoubled == 8);

    // A better record: the band reaches SPARED at the top, and a denial that
    // walked is not a lie the bench caught -- nothing doubles.
    ChargeSheet tongue = sheetOf(Sentence::Held);
    tongue.streetwise = 30;   // +15
    tongue.templeStanding = 52;  // +17
    // 24 + 15 + 17 = 56 -> 46..66
    tongue.draw = drawWithBand(kPriestBand);
    const Arraignment walked = weighArraignment(tongue, Plea::NotGuilty);
    CHECK(walked.scored == 66);
    CHECK(walked.judgment == Judgment::Spared);
    CHECK_FALSE(walked.doubled);
    tongue.draw = drawWithBand(-kPriestBand);
    const Arraignment caught = weighArraignment(tongue, Plea::NotGuilty);
    CHECK(caught.scored == 46);
    CHECK(caught.judgment == Judgment::Fined);
    CHECK(caught.doubled);
    // And the same record confessing is fixed: 62, FINED, never doubled --
    // no rope for the tongue, and no mercy from it either.
    const Arraignment fixed = weighArraignment(tongue, Plea::Guilty);
    CHECK(fixed.scored == 62);
    CHECK(fixed.judgment == Judgment::Fined);
    CHECK_FALSE(fixed.doubled);
    // Under the last line a disbelieved denial is BOUND, doubled.
    ChargeSheet nobody = sheetOf(Sentence::Held);
    nobody.priors = 3;  // 24 - 30 = -6 -> -16..4
    nobody.draw = drawWithBand(-kPriestBand);
    const Arraignment bound = weighArraignment(nobody, Plea::NotGuilty);
    CHECK(bound.judgment == Judgment::Bound);
    CHECK(bound.doubled);
}

TEST_CASE("a witnessed murder is never SPARED: COMMUTED at the mercy line, else THE ROPE") {
    // THE SPEC'S WORKED EXAMPLES, to the point.
    // A first murderer, nobody, streetwise 10, two saw it: 24 + 5 - 30 - 8 =
    // -9. I DID IT -> -3, THE ROPE. I DID NOT -> -19..+1, THE ROPE. A nobody
    // hangs.
    ChargeSheet nobody = sheetOf(Sentence::Condemned);
    nobody.blood = true;
    nobody.witnesses = 2;
    nobody.streetwise = 10;
    const Arraignment confessed = weighArraignment(nobody, Plea::Guilty);
    REQUIRE(confessed.heard);
    CHECK(confessed.weight == -9);
    CHECK(confessed.scored == -3);
    CHECK(confessed.judgment == Judgment::TheRope);
    CHECK_FALSE(confessed.doubled);  // nothing under the rope to double
    for (std::int32_t jitter = -kPriestBand; jitter <= kPriestBand; ++jitter) {
        nobody.draw = drawWithBand(jitter);
        const Arraignment denied = weighArraignment(nobody, Plea::NotGuilty);
        CHECK(denied.judgment == Judgment::TheRope);
        CHECK_FALSE(denied.doubled);
    }

    // The Mission's own Acolyte (temple 52 -> +17) with a tongue (20 -> +10),
    // one prior: 24 + 10 + 17 - 10 - 30 - 8 = 3; I DID IT -> 9, THE ROPE.
    ChargeSheet acolyte = nobody;
    acolyte.templeStanding = 52;
    acolyte.streetwise = 20;
    acolyte.priors = 1;
    const Arraignment devout = weighArraignment(acolyte, Plea::Guilty);
    CHECK(devout.weight == 3);
    CHECK(devout.judgment == Judgment::TheRope);

    // A Shepherd (84 -> +24), streetwise 30 (+15), the ward warm (+4), no
    // priors, two saw it: 29. I DID IT -> 35, COMMUTED, certain. I DID NOT ->
    // 19..39, COMMUTED sixteen times in twenty-one. The devout may confess
    // and live.
    ChargeSheet shepherd = sheetOf(Sentence::Condemned);
    shepherd.blood = true;
    shepherd.witnesses = 2;
    shepherd.templeStanding = 84;
    shepherd.streetwise = 30;
    shepherd.reputation = 20;
    const Arraignment lived = weighArraignment(shepherd, Plea::Guilty);
    CHECK(lived.weight == 29);
    CHECK(lived.scored == 35);
    CHECK(lived.judgment == Judgment::Commuted);
    int commuted = 0;
    for (std::int32_t jitter = -kPriestBand; jitter <= kPriestBand; ++jitter) {
        shepherd.draw = drawWithBand(jitter);
        const Arraignment denied = weighArraignment(shepherd, Plea::NotGuilty);
        CHECK(denied.judgment != Judgment::Spared);
        commuted += denied.judgment == Judgment::Commuted ? 1 : 0;
    }
    CHECK(commuted == 16);

    // NEVER SPARED, whatever the record: the best a man can bring to a rope
    // bench with the band at its top is still COMMUTED.
    ChargeSheet saint = sheetOf(Sentence::Condemned);
    saint.blood = true;
    saint.witnesses = 1;
    saint.templeStanding = 100;
    saint.streetwise = 40;
    saint.reputation = 100;
    saint.draw = drawWithBand(kPriestBand);
    const Arraignment best = weighArraignment(saint, Plea::NotGuilty);
    CHECK(best.scored >= kSparedLine);
    CHECK(best.judgment == Judgment::Commuted);
    CHECK(best.band == Judgment::None);  // the paper ladder is not consulted

    // A Skyrunner's second, Acolyte, tongue 20, one prior: 24 + 10 + 17 - 10
    // - 24 = 17; I DID IT -> 23, THE ROPE by one. "Hanging on the second"
    // stands for anyone the Flame does not know well.
    ChargeSheet second = sheetOf(Sentence::Condemned);
    second.skyrunner = true;
    second.secondRung = true;
    second.templeStanding = 52;
    second.streetwise = 20;
    second.priors = 1;
    const Arraignment rung = weighArraignment(second, Plea::Guilty);
    CHECK(rung.weight == 17);
    CHECK(rung.scored == kMercyLine - 1);
    CHECK(rung.judgment == Judgment::TheRope);
    CHECK(termNamed(rung, kTermSecondRung) != nullptr);
    CHECK(termNamed(rung, kTermBlood) == nullptr);
    // And one point of the Flame's regard turns it.
    second.templeStanding = 55;  // +18
    CHECK(weighArraignment(second, Plea::Guilty).judgment == Judgment::Commuted);
}

TEST_CASE("the rope tier is only where ruled: blood, or the roofs' second -- never theft, never repeat violence") {
    // Nine priors and every purse in the ward: a cell, still.
    CrimeLedger thief;
    for (int i = 0; i < 9; ++i) {
        thief.commit(Crime::Lift, true);
        thief.addHeat(kWarrantAt);
        thief.sentence(Judgment::Held, 1);
    }
    thief.addHeat(kWarrantAt);
    CHECK(thief.arrests() == 9);
    CHECK(thief.charge(false, 0, 0, 0).tier == Sentence::Held);
    // Leaning on half the taproom, again and again: heat, and a cell.
    CrimeLedger bully;
    for (int i = 0; i < 5; ++i) {
        bully.commit(Crime::Extort, true);
    }
    CHECK(bully.warrant());
    CHECK(bully.charge(false, 0, 0, 0).tier == Sentence::Held);
    // The roofs: the hand, then the rope.
    CrimeLedger roofs;
    roofs.addHeat(kWarrantAt);
    CHECK(roofs.charge(true, 0, 0, 0).tier == Sentence::Maimed);
    roofs.sentence(Judgment::TheHand, 1);
    roofs.addHeat(kWarrantAt);
    CHECK(roofs.charge(true, 0, 0, 0).tier == Sentence::Condemned);
    // Blood, with or without paper, whoever you are.
    CrimeLedger killer;
    killer.markMurderer(1);
    CHECK(killer.charge(false, 0, 0, 0).tier == Sentence::Condemned);
    killer.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax);
    REQUIRE_FALSE(killer.warrant());
    CHECK(killer.charge(false, 0, 0, 0).tier == Sentence::Condemned);

    // And no score on a PAPER or HAND sheet reaches the rope, at any band,
    // either plea: the judgment set of those tiers has no rope in it.
    for (const Sentence tier : {Sentence::Held, Sentence::Maimed}) {
        for (const std::int32_t priors : {0, 3, 9}) {
            for (const Plea plea : {Plea::Guilty, Plea::NotGuilty}) {
                for (std::int32_t jitter = -kPriestBand; jitter <= kPriestBand; ++jitter) {
                    ChargeSheet sheet = sheetOf(tier);
                    sheet.priors = priors;
                    sheet.heatAtArrest = kHeatMax;
                    sheet.reputation = -100;
                    sheet.condemnedBefore = true;
                    sheet.draw = drawWithBand(jitter);
                    const Arraignment worst = weighArraignment(sheet, plea);
                    REQUIRE(worst.heard);
                    CHECK(worst.judgment != Judgment::TheRope);
                    CHECK(worst.judgment != Judgment::Commuted);
                    CHECK(worst.judgment != Judgment::None);
                }
            }
        }
    }
}

TEST_CASE("the hand is sparable: HELD at the fine's line, the hand below it, BOUND's days under the last") {
    // THE HAND tier is the PAPER bands with one substitution.
    ChargeSheet hand = sheetOf(Sentence::Maimed);
    hand.streetwise = 40;  // +20; 24 + 20 - 12 THE ROOFS = 32 -> confessed 38
    const Arraignment spared = weighArraignment(hand, Plea::Guilty);
    REQUIRE(termNamed(spared, kTermRoofs) != nullptr);
    CHECK(termNamed(spared, kTermRoofs)->value == -12);
    CHECK(spared.scored == kFinedLine);
    CHECK(spared.band == Judgment::Fined);
    CHECK(spared.judgment == Judgment::Held);  // the priest overruled the sergeant
    CHECK_FALSE(spared.doubled);

    hand.streetwise = 20;  // +10; 22 -> confessed 28: the hand, HELD's nights
    const Arraignment taken = weighArraignment(hand, Plea::Guilty);
    CHECK(taken.scored == 28);
    CHECK(taken.band == Judgment::Held);
    CHECK(taken.judgment == Judgment::TheHand);

    hand.streetwise = 0;
    hand.priors = 1;  // 24 - 12 - 10 = 2 -> confessed 8: the hand, BOUND's five days
    const Arraignment bound = weighArraignment(hand, Plea::Guilty);
    CHECK(bound.scored == 8);
    CHECK(bound.band == Judgment::Bound);
    CHECK(bound.judgment == Judgment::TheHand);

    // SPARED at the top on a denial, exactly as PAPER.
    hand.priors = 0;
    hand.streetwise = 40;
    hand.templeStanding = 84;  // 24 + 20 + 24 - 12 = 56
    hand.draw = drawWithBand(0);
    const Arraignment walked = weighArraignment(hand, Plea::NotGuilty);
    CHECK(walked.scored == 56);
    CHECK(walked.judgment == Judgment::Spared);
    CHECK_FALSE(walked.doubled);
    hand.draw = drawWithBand(-kPriestBand);
    const Arraignment caught = weighArraignment(hand, Plea::NotGuilty);
    CHECK(caught.scored == 46);
    CHECK(caught.judgment == Judgment::Held);
    CHECK(caught.doubled);
}

TEST_CASE("mercy is given once: a commuted man before a rope bench has no plea and one answer") {
    CrimeLedger ledger;
    ledger.markMurderer(2);
    REQUIRE(ledger.warrant());
    // The Shepherd's record: COMMUTED, certain, on a confession.
    const ChargeSheet first = ledger.charge(false, 30, 84, 20);
    REQUIRE(first.tier == Sentence::Condemned);
    ledger.openHearing(first, 0, 0x5EEDull, "Watchman Cull");
    REQUIRE(ledger.hearingPending());
    CHECK(ledger.hearing().awaitingPlea());
    CHECK(ledger.hearing().officer == "Watchman Cull");
    const Arraignment lived = ledger.plead(Plea::Guilty);
    REQUIRE(lived.heard);
    CHECK(lived.judgment == Judgment::Commuted);
    CHECK(ledger.hearing().judged());
    CHECK(ledger.hearing().judgment == Judgment::Commuted);
    CHECK(ledger.hearings() == 1);
    CHECK(ledger.lastPlea() == Plea::Guilty);
    // A second plea on a judged hearing is refused, and so is none at all.
    CHECK_FALSE(ledger.plead(Plea::NotGuilty).heard);
    CHECK(ledger.hearings() == 1);
    // Served: the record, and the hearing closed.
    ledger.sentence(lived.judgment, 12);
    CHECK_FALSE(ledger.hearingPending());
    CHECK(ledger.condemned());
    CHECK(ledger.commuted());
    CHECK(ledger.maimed());
    CHECK_FALSE(ledger.murderer());
    CHECK(ledger.arrests() == 1);
    CHECK(ledger.daysServed() == 12);
    CHECK(ledger.heat() == kHeatAfterSentence);
    CHECK_FALSE(ledger.warrant());

    // Taken again on a lift, with paper: an ordinary hearing, THE ROPE ONCE
    // weighed against him, the rope not on the table.
    ledger.commit(Crime::Lift, true);
    ledger.addHeat(kWarrantAt);
    const ChargeSheet lift = ledger.charge(false, 30, 84, 20);
    CHECK(lift.tier == Sentence::Held);
    CHECK(lift.condemnedBefore);
    CHECK(lift.commutedBefore);
    const Arraignment weighed = weighArraignment(lift, Plea::Guilty);
    REQUIRE(termNamed(weighed, kTermRopeOnce) != nullptr);
    CHECK(termNamed(weighed, kTermRopeOnce)->value == -16);
    CHECK(weighed.judgment != Judgment::TheRope);

    // But a second killing is a rope bench with mercy spent: no plea, no
    // weighing, the rope. Either row he tries to speak is the one row.
    ledger.markMurderer(1);
    const ChargeSheet again = ledger.charge(false, 30, 84, 20);
    REQUIRE(again.tier == Sentence::Condemned);
    REQUIRE(again.commutedBefore);
    ledger.openHearing(again, 0, 0x5EEDull, "Watchman Cull");
    const Arraignment silence = ledger.plead(Plea::NotGuilty);
    REQUIRE(silence.heard);
    CHECK(silence.plea == Plea::NoPlea);
    CHECK(silence.terms.empty());
    CHECK(silence.judgment == Judgment::TheRope);
    CHECK(ledger.hearing().plea == Plea::NoPlea);
    CHECK(ledger.hearing().judgment == Judgment::TheRope);
    CHECK(ledger.hearings() == 2);
    ledger.sentence(Judgment::TheRope, 0);
    CHECK(ledger.executed());
    CHECK_FALSE(ledger.hearingPending());
}

TEST_CASE("the crime ledger's v5 bytes carry the court's record and the open hearing") {
    CrimeLedger before;
    before.commit(Crime::Lift, true);
    before.commit(Crime::Burgle, true);
    before.commit(Crime::Smuggle, true);
    before.stash().add(Contraband::Dust, 3);
    before.addHeat(kWarrantAt);
    // A sentence served, so the served tallies and the days are non-zero...
    before.sentence(Judgment::Held, 2);
    // ...a killing since, with witnesses...
    before.commit(Crime::Lift, true);
    before.markMurderer(4);
    // ...and a hearing open on it, pleaded but not yet served.
    const ChargeSheet sheet = before.charge(false, 22, 40, -30);
    before.openHearing(sheet, 3, 0x0102030405060708ull, "Watchman Cull");
    const Arraignment answer = before.plead(Plea::NotGuilty);
    REQUIRE(answer.heard);
    REQUIRE(before.hearingPending());

    const std::vector<std::uint8_t> bytes = before.encode();
    CrimeLedger after;
    REQUIRE(CrimeLedger::decode(bytes, after));
    // The v4 record still.
    CHECK(after.arrests() == before.arrests());
    CHECK(after.heat() == before.heat());
    CHECK(after.warrant() == before.warrant());
    CHECK(after.murderer() == before.murderer());
    CHECK(after.maimed() == before.maimed());
    CHECK(after.condemned() == before.condemned());
    CHECK(after.lastSentence() == before.lastSentence());
    // The v5 record.
    CHECK(after.commuted() == before.commuted());
    CHECK(after.executed() == before.executed());
    CHECK(after.lastPlea() == before.lastPlea());
    CHECK(after.lastJudgment() == before.lastJudgment());
    CHECK(after.hearings() == before.hearings());
    CHECK(after.daysServed() == before.daysServed());
    CHECK(after.slewWitnesses() == before.slewWitnesses());
    CHECK(after.slewWitnesses() == 4);
    for (std::size_t i = 0; i < kCrimeCount; ++i) {
        CHECK(after.servedTally(static_cast<Crime>(i)) ==
              before.servedTally(static_cast<Crime>(i)));
    }
    // The hearing, field for field.
    const HearingState& was = before.hearing();
    const HearingState& now = after.hearing();
    CHECK(now.stage == was.stage);
    CHECK(now.judged());
    CHECK(now.officer == "Watchman Cull");
    CHECK(now.plea == was.plea);
    CHECK(now.judgment == was.judgment);
    CHECK(now.band == was.band);
    CHECK(now.weight == was.weight);
    CHECK(now.scored == was.scored);
    CHECK(now.doubled == was.doubled);
    CHECK(now.sheet.written == was.sheet.written);
    CHECK(now.sheet.tier == was.sheet.tier);
    CHECK(now.sheet.blood == was.sheet.blood);
    CHECK(now.sheet.hasWorst == was.sheet.hasWorst);
    CHECK(now.sheet.worst == was.sheet.worst);
    for (std::size_t i = 0; i < kSheetCrimes; ++i) {
        CHECK(now.sheet.since[i] == was.sheet.since[i]);
    }
    CHECK(now.sheet.heatAtArrest == was.sheet.heatAtArrest);
    CHECK(now.sheet.unitsSeized == 3);
    CHECK(now.sheet.witnesses == 4);
    CHECK(now.sheet.priors == 1);
    CHECK(now.sheet.skyrunner == was.sheet.skyrunner);
    CHECK(now.sheet.secondRung == was.sheet.secondRung);
    CHECK(now.sheet.condemnedBefore == was.sheet.condemnedBefore);
    CHECK(now.sheet.commutedBefore == was.sheet.commutedBefore);
    CHECK(now.sheet.streetwise == 22);
    CHECK(now.sheet.templeStanding == 40);
    CHECK(now.sheet.reputation == -30);
    CHECK(now.sheet.draw == 0x0102030405060708ull);
    // And the record decoded weighs to the same answer as the one encoded:
    // the hearing reopens at the bench with the same arithmetic.
    const Arraignment replayed = weighArraignment(now.sheet, now.plea);
    CHECK(replayed.scored == was.scored);
    CHECK(replayed.judgment == was.judgment);
    // The same bytes hash the same.
    HashSink one(0x434F5552u);
    HashSink two(0x434F5552u);
    before.hashInto(one);
    after.hashInto(two);
    CHECK(one.finished() == two.finished());

    // A v4 blob is refused by version, a truncated v5 by length, and a
    // judgment this build has no name for by its ceiling.
    CrimeLedger wrecked;
    std::vector<std::uint8_t> bent = bytes;
    bent[2] = 4;
    CHECK_FALSE(CrimeLedger::decode(bent, wrecked));
    bent = bytes;
    bent.resize(bytes.size() - 6);
    CHECK_FALSE(CrimeLedger::decode(bent, wrecked));
    // An empty record round-trips too: no hearing, nothing served.
    const CrimeLedger blank;
    CrimeLedger blankAgain;
    REQUIRE(CrimeLedger::decode(blank.encode(), blankAgain));
    CHECK_FALSE(blankAgain.hearingPending());
    CHECK(blankAgain.hearings() == 0);
    CHECK(blankAgain.hearing().officer.empty());
}

// ===========================================================================
// ROOM
// ===========================================================================

TEST_CASE("an arrest with paper opens a hearing at the Mission, and the clock does not jump") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    talk.crimes().commit(Crime::Lift, true);
    talk.crimes().addHeat(kWarrantAt + 2);  // 70: paper, and two lines of it over the warrant
    REQUIRE(talk.crimes().warrant());
    const std::int32_t heatBefore = talk.crimes().heat();
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t dayBefore = gull.dayNumber();
    const std::int32_t clockBefore = gull.timeOfDay();

    REQUIRE(standUntilTaken(room));
    const Tavern::ArrestReport& arrest = gull.lastArrest();
    CHECK(arrest.cause == WatchCause::Warrant);
    CHECK(arrest.officer == "Watchman Cull");
    // The ask is on the report, as it always was; the answer is the bench's.
    CHECK(arrest.sentence == Sentence::Held);
    CHECK(arrest.fine == 0);
    CHECK(arrest.heldHours == 0);
    CHECK(arrest.line.rfind("Watchman Cull: ", 0) == 0);

    // THE HEARING IS OPEN: the sheet, the officer, the draw.
    REQUIRE(gull.hearingPending());
    const HearingState& hearing = gull.hearing();
    CHECK(hearing.awaitingPlea());
    CHECK(hearing.sheet.tier == Sentence::Held);
    CHECK(hearing.sheet.hasWorst);
    CHECK(hearing.sheet.worst == Crime::Lift);
    CHECK(hearing.sheet.since[static_cast<std::size_t>(Crime::Lift)] == 1);
    CHECK(hearing.sheet.heatAtArrest <= heatBefore);
    CHECK(hearing.sheet.heatAtArrest >= kWarrantAt);
    CHECK(hearing.officer == "Watchman Cull");
    CHECK(hearing.sheet.streetwise == talk.skills().level(kHaggleSkill));
    CHECK(hearing.sheet.reputation == talk.ledger().reputation());
    // The nights are drawn at the arrest, off the one draw, and inside canon.
    CHECK(heldHours(hearing.sheet.draw) >= kHeldHoursMin);
    CHECK(heldHours(hearing.sheet.draw) <= kHeldHoursMax);

    // NOTHING SERVED YET: no clock jump, no fine, no prior, the paper stands.
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(gull.timeOfDay() - clockBefore < 1000);  // the seconds it took, not a night
    CHECK(gull.playerCoin() == purse);
    CHECK(talk.crimes().arrests() == 0);
    CHECK(talk.crimes().warrant());
    CHECK_FALSE(talk.crimes().maimed());
    CHECK_FALSE(gull.executed());
    // The room let go of the player and the body is somebody else's to move.
    CHECK(gull.watchStance() == Tavern::WatchStance::Idle);
    CHECK(gull.takeArrestRelease());
    CHECK_FALSE(gull.takeArrestRelease());
}

TEST_CASE("a search at the door is the shipped fast path: fined, seized, released, no bench") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    REQUIRE(talk.crimes().stash().add(Contraband::Moonshine, 4) == 4);
    REQUIRE_FALSE(talk.crimes().warrant());
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t dayBefore = gull.dayNumber();

    REQUIRE(standUntilTaken(room));
    const Tavern::ArrestReport& arrest = gull.lastArrest();
    CHECK(arrest.cause == WatchCause::Contraband);
    CHECK(arrest.sentence == Sentence::Fined);
    CHECK(arrest.unitsSeized == 4);
    CHECK(arrest.fine > 0);
    CHECK(arrest.fine <= purse);
    CHECK(gull.playerCoin() == purse - arrest.fine);
    CHECK(talk.crimes().stash().illicitUnits() == 0);
    CHECK(talk.crimes().arrests() == 0);
    CHECK(talk.crimes().lastSentence() == Sentence::Fined);
    CHECK(gull.dayNumber() == dayBefore);
    // No paper, so no bench: nothing opened, nothing to plead.
    CHECK_FALSE(gull.hearingPending());
    CHECK_FALSE(gull.plead(Plea::Guilty).heard);
    CHECK(gull.takeArrestRelease());
}

TEST_CASE("the plea is a stepped input: it weighs the sheet, trains the tongue, and leaves the sentence to come") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    talk.crimes().addHeat(kWarrantAt + 10);
    REQUIRE(standUntilTaken(room));
    REQUIRE(gull.hearingPending());
    REQUIRE(talk.skills().find(kHaggleSkill) != nullptr);
    const std::int32_t usesBefore = talk.skills().find(kHaggleSkill)->uses;
    const std::int32_t levelBefore = talk.skills().find(kHaggleSkill)->level;
    // One use of the tongue, however the track banks it: a use recorded, or
    // -- at a charge boundary -- the level it tipped.
    const auto tongueUsed = [&](std::int32_t uses) {
        const SkillTrack::Entry* tongue = talk.skills().find(kHaggleSkill);
        return tongue->uses == usesBefore + uses ||
               (uses > 0 && tongue->level > levelBefore);
    };
    const std::int32_t dayBefore = gull.dayNumber();

    const Arraignment answer = gull.plead(Plea::Guilty);
    REQUIRE(answer.heard);
    CHECK(answer.plea == Plea::Guilty);
    CHECK(answer.pleaTerm == kConfessedTerm);
    CHECK_FALSE(answer.doubled);
    CHECK(answer.judgment != Judgment::Spared);
    CHECK(answer.judgment != Judgment::TheRope);
    CHECK(answer.judgment != Judgment::Commuted);
    // Written on the hearing, and the hearing counted.
    CHECK(gull.hearing().judged());
    CHECK(gull.hearing().judgment == answer.judgment);
    CHECK(gull.hearing().scored == answer.scored);
    CHECK(talk.crimes().hearings() == 1);
    CHECK(talk.crimes().lastJudgment() == answer.judgment);
    // A plea is a haggle with your neck on the table.
    CHECK(tongueUsed(1));
    // Pleaded, not served: the hearing is still open, the clock still where
    // it was, the record untouched. What a judgment DOES is the next step.
    CHECK(gull.hearingPending());
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(talk.crimes().arrests() == 0);
    CHECK(talk.crimes().warrant());
    // And the bench does not hear the same man twice on one paper: refused,
    // and the tongue not trained for a plea that was not taken.
    const std::int32_t usesAfterPlea = talk.skills().find(kHaggleSkill)->uses;
    const std::int32_t levelAfterPlea = talk.skills().find(kHaggleSkill)->level;
    CHECK_FALSE(gull.plead(Plea::NotGuilty).heard);
    CHECK(talk.skills().find(kHaggleSkill)->uses == usesAfterPlea);
    CHECK(talk.skills().find(kHaggleSkill)->level == levelAfterPlea);
    CHECK(talk.crimes().hearings() == 1);
}

TEST_CASE("a killing counts who saw it onto the record, and the sheet asks for the rope") {
    // Hour 19: a full taproom and no watchman (Cull drinks from ten), so the
    // killing resolves on its own and the sheet is read off the ledger.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const Actor* mark = room.findRole(ActorRole::Patron);
    REQUIRE(mark != nullptr);
    REQUIRE(room.standFacing(*mark) != -1);
    CrimeLedger& crimes = tavern.dialogue().crimes();
    REQUIRE_FALSE(crimes.murderer());

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    bool killed = false;
    for (int swings = 0; swings < 30 && !killed; ++swings) {
        killed = room.swing(true).killed;
    }
    REQUIRE(killed);
    REQUIRE(crimes.murderer());
    // A full taproom saw it: more than one face, and the sheet names the
    // count for the bench's own term.
    CHECK(crimes.slewWitnesses() >= 1);
    const ChargeSheet sheet = crimes.charge(false, 0, 0, 0);
    CHECK(sheet.tier == Sentence::Condemned);
    CHECK(sheet.blood);
    CHECK(sheet.witnesses == crimes.slewWitnesses());
    const Arraignment weighed = weighArraignment(sheet, Plea::Guilty);
    REQUIRE(termNamed(weighed, kTermSawIt) != nullptr);
    CHECK(termNamed(weighed, kTermSawIt)->count == crimes.slewWitnesses());
    CHECK(termNamed(weighed, kTermSawIt)->value == -std::min(16, crimes.slewWitnesses() * 4));
}

TEST_CASE("a scripted arrest and plea twin-runs byte-identical, and diverges at and only at the plea") {
    // TWO ROOMS, ONE SEED, ONE SCRIPT -- the twin-run gate's question, asked
    // at the seam this build added. The arrest spends its one draw where it
    // always did; the plea spends none.
    const auto script = [](Room& room, Plea plea) {
        Tavern& gull = room.tavern();
        gull.dialogue().crimes().addHeat(kWarrantAt + 10);
        REQUIRE(standUntilTaken(room));
        const Arraignment answer = gull.plead(plea);
        REQUIRE(answer.heard);
        room.run(3);
        return std::tuple{room.hash(), answer.judgment, answer.scored, gull.hearing().plea};
    };
    Room one(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Room two(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    const auto first = script(one, Plea::Guilty);
    const auto second = script(two, Plea::Guilty);
    CHECK(first == second);
    // The other plea on the same seed: the same arrest, a different record.
    Room three(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    const auto other = script(three, Plea::NotGuilty);
    CHECK(std::get<0>(other) != std::get<0>(first));
    CHECK(std::get<3>(other) == Plea::NotGuilty);
    CHECK(three.tavern().hearing().sheet.draw == one.tavern().hearing().sheet.draw);
}

// ===========================================================================
// TWO PATHS
// ===========================================================================

TEST_CASE("the two player-end paths never meet: a defeat sets no rope, and the court calls no quay") {
    // COMBAT DEFEAT is the nemesis ruling -- you live. The room's defeat path
    // writes nothing the court reads: no hearing, no judgment, no rope.
    {
        Room fight(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
        Tavern& gull = fight.tavern();
        fight.run(2);
        const Actor* rival = fight.findRole(ActorRole::Patron);
        REQUIRE(rival != nullptr);
        const std::int32_t dayBefore = gull.dayNumber();
        gull.concedeTo(rival->id());
        REQUIRE(gull.takeDefeatRelease());
        gull.reviveAfterDefeat();
        CHECK(gull.playerHp() == gull.playerHpMax());  // the quay: you live
        CHECK_FALSE(gull.executed());
        CHECK_FALSE(gull.hearingPending());
        CHECK(gull.dialogue().crimes().lastJudgment() == Judgment::None);
        CHECK(gull.dialogue().crimes().arrests() == 0);
        CHECK(gull.dayNumber() >= dayBefore);  // the blackout is the quay's clock, not a cell
    }

    // THE COURT'S own path, end to end: arrest, plea, the rope. Nothing on it
    // is a defeat -- no blow landed, no release for the quay, no blackout
    // hours -- and the bit it sets is its own.
    {
        Room court(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
        Tavern& gull = court.tavern();
        gull.dialogue().crimes().markMurderer(3);
        REQUIRE(standUntilTaken(court));
        REQUIRE(gull.hearingPending());
        REQUIRE(gull.hearing().sheet.tier == Sentence::Condemned);
        const std::int32_t dayBefore = gull.dayNumber();
        const std::int32_t hpBefore = gull.playerHp();
        const Arraignment answer = gull.plead(Plea::Guilty);
        REQUIRE(answer.heard);
        CHECK(answer.judgment == Judgment::TheRope);  // a nobody hangs
        CHECK_FALSE(gull.takeDefeatRelease());
        CHECK(gull.dayNumber() == dayBefore);
        gull.dialogue().crimes().sentence(answer.judgment, 0);
        CHECK(gull.executed());
        CHECK_FALSE(gull.takeDefeatRelease());
        CHECK_FALSE(gull.playerFloored());
        CHECK(gull.playerHp() == hpBefore);  // the rope is not a beating
    }
}

// ===========================================================================
// SENTENCES -- the branch, in integers
// ===========================================================================

namespace {

/// A judged hearing written by hand, so the sentence can be driven as the
/// pure function it is. The sheet's heat and seizure are what the fine reads;
/// the draw's low residue is what the nights read.
[[nodiscard]] HearingState judgedOf(Judgment judgment, Judgment band, bool doubled,
                                    std::int32_t heat = 68, std::int32_t units = 2,
                                    std::uint64_t draw = 10) {
    HearingState hearing;
    hearing.stage = HearingStage::Judged;
    hearing.sheet = sheetOf(Sentence::Held);
    hearing.sheet.heatAtArrest = heat;
    hearing.sheet.unitsSeized = units;
    hearing.sheet.draw = draw;
    hearing.judgment = judgment;
    hearing.band = band;
    hearing.doubled = doubled;
    return hearing;
}

}  // namespace

TEST_CASE("every branch's integers: the fine and its shortfall days, the nights by charge and doubled, BOUND five, COMMUTED twelve, the hand") {
    // The spec's own thief: heat 68 and two jars -> fineFor = 17 + 6 = 23.
    CHECK(fineFor(68, 2) == 23);
    // Nothing judged is nothing served.
    HearingState open = judgedOf(Judgment::Held, Judgment::Held, false);
    open.stage = HearingStage::Arraigned;
    CHECK_FALSE(sentenceTerms(open, 100).served);
    CHECK_FALSE(sentenceTerms(judgedOf(Judgment::None, Judgment::None, false), 100).served);

    // SPARED: walked out clean. Coin 0, clock 0, the Mission's door.
    const SentenceTerms spared =
        sentenceTerms(judgedOf(Judgment::Spared, Judgment::Spared, false), 100);
    REQUIRE(spared.served);
    CHECK(spared.fineAsked == 0);
    CHECK(spared.hours == 0);
    CHECK(spared.days == 0);
    CHECK(spared.releaseHere);
    CHECK_FALSE(spared.hand);
    CHECK_FALSE(spared.mends);
    CHECK(spared.roofsDelta == 0);
    CHECK(spared.templeDelta == 0);

    // FINED: the shipped fine, out of a purse that can pay it; no cell, no
    // day, nothing mended, nothing moved on the ladders. The Mission's door.
    const SentenceTerms fined =
        sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, false), 100);
    REQUIRE(fined.served);
    CHECK(fined.fineAsked == 23);
    CHECK(fined.finePaid == 23);
    CHECK(fined.shortfall == 0);
    CHECK(fined.shortfallDays == 0);
    CHECK(fined.cellHours == 0);
    CHECK(fined.bondDays == 0);
    CHECK(fined.hours == 0);
    CHECK(fined.releaseHere);
    CHECK_FALSE(fined.mends);
    CHECK(fined.roofsDelta == 0);
    CHECK(fined.templeDelta == 0);
    // Doubled on a disbelieved denial, and the Flame remembers the lie.
    const SentenceTerms lied = sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, true), 100);
    CHECK(lied.fineAsked == 46);
    CHECK(lied.finePaid == 46);
    CHECK(lied.templeDelta == -kLieTempleCost);
    CHECK(lied.roofsDelta == 0);
    // THE SHORTFALL is worked off: a day per four Royals, rounded up, capped
    // at seven, and the purse is never put in debt. "Payable in Royals or in
    // yourself." A fine's yard days still end at the Mission's door.
    const SentenceTerms shortPurse =
        sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, false), 5);
    CHECK(shortPurse.finePaid == 5);
    CHECK(shortPurse.shortfall == 18);
    CHECK(shortPurse.shortfallDays == 5);  // 18 / 4 rounded up
    CHECK(shortPurse.bondDays == 5);
    CHECK(shortPurse.hours == 120);
    CHECK(shortPurse.days == 5);
    CHECK(shortPurse.mends);
    CHECK(shortPurse.releaseHere);
    const SentenceTerms oneRoyalShort =
        sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, false), 22);
    CHECK(oneRoyalShort.shortfall == 1);
    CHECK(oneRoyalShort.shortfallDays == 1);
    const SentenceTerms emptyPurse =
        sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, true), 0);
    CHECK(emptyPurse.finePaid == 0);
    CHECK(emptyPurse.shortfall == 46);
    CHECK(emptyPurse.shortfallDays == kShortfallDaysMax);  // 46 / 4 -> 12, capped at 7
    CHECK(emptyPurse.days == kShortfallDaysMax);
    const SentenceTerms owed = sentenceTerms(judgedOf(Judgment::Fined, Judgment::Fined, false), -4);
    CHECK(owed.finePaid == 0);  // a purse below zero pays nothing and owes nothing more

    // HELD: the fine AND the cell -- the shipped one-to-three nights off the
    // arrest's own draw, exactly where they were. A conviction: the roofs
    // warm, the Tarwalk at the end of it, the body mended by the days.
    const SentenceTerms held = sentenceTerms(judgedOf(Judgment::Held, Judgment::Held, false), 100);
    REQUIRE(held.served);
    CHECK(held.fineAsked == 23);
    CHECK(held.cellHours == heldHours(10));
    CHECK(held.cellHours == kHeldHoursMin + 10);
    CHECK(held.bondDays == 0);
    CHECK(held.hours == 34);
    CHECK(held.days == 1);
    CHECK(held.mends);
    CHECK_FALSE(held.releaseHere);
    CHECK_FALSE(held.hand);
    CHECK(held.roofsDelta == kConvictionRoofsGain);
    CHECK(held.templeDelta == 0);
    // THE NIGHTS BY CHARGE, over every residue the draw can give: one to
    // three days, and two to six doubled. The nights are the draw's low
    // residue; the band above them (the plea) never touches them.
    for (std::uint64_t residue = 0; residue < 49; ++residue) {
        const std::uint64_t draw = residue | (static_cast<std::uint64_t>(7) << kPriestBandShift);
        const SentenceTerms once =
            sentenceTerms(judgedOf(Judgment::Held, Judgment::Held, false, 68, 0, draw), 100);
        const SentenceTerms twice =
            sentenceTerms(judgedOf(Judgment::Held, Judgment::Held, true, 68, 0, draw), 100);
        CHECK(once.cellHours == heldHours(draw));
        CHECK(once.cellHours >= kHeldHoursMin);
        CHECK(once.cellHours <= kHeldHoursMax);
        CHECK(once.days >= 1);
        CHECK(once.days <= 3);
        CHECK(twice.cellHours == 2 * once.cellHours);
        CHECK(twice.days >= 2);
        CHECK(twice.days <= 6);
        CHECK(twice.fineAsked == 2 * once.fineAsked);
        CHECK(twice.templeDelta == -kLieTempleCost);
    }
    // A cell plus a short purse: the shortfall's days on top of the nights.
    const SentenceTerms heldShort =
        sentenceTerms(judgedOf(Judgment::Held, Judgment::Held, false), 3);
    CHECK(heldShort.finePaid == 3);
    CHECK(heldShort.shortfallDays == 5);  // 20 / 4
    CHECK(heldShort.hours == 34 + 120);
    CHECK(heldShort.days == 6);

    // BOUND: the fine forgiven, five days in the Mission's yard, ten for a
    // lie; the work was the Flame's (+4), and the lie is remembered (-8).
    const SentenceTerms bound =
        sentenceTerms(judgedOf(Judgment::Bound, Judgment::Bound, false), 100);
    REQUIRE(bound.served);
    CHECK(bound.fineAsked == 0);
    CHECK(bound.finePaid == 0);
    CHECK(bound.cellHours == 0);
    CHECK(bound.bondDays == kBoundDays);
    CHECK(bound.hours == 120);
    CHECK(bound.days == 5);
    CHECK(bound.mends);
    CHECK_FALSE(bound.releaseHere);
    CHECK(bound.templeDelta == kBoundTempleGain);
    CHECK(bound.roofsDelta == kConvictionRoofsGain);
    const SentenceTerms boundTwice =
        sentenceTerms(judgedOf(Judgment::Bound, Judgment::Bound, true), 0);
    CHECK(boundTwice.bondDays == 2 * kBoundDays);
    CHECK(boundTwice.days == 10);
    CHECK(boundTwice.fineAsked == 0);  // an empty purse changes nothing: no fine to fall short on
    CHECK(boundTwice.templeDelta == kBoundTempleGain - kLieTempleCost);

    // THE HAND: the hand, plus HELD's coin and nights at HELD's band, plus
    // BOUND's days (and BOUND's forgiven fine, and the Flame's regard for
    // the yard) under the last line. At the fine's line the judgment is HELD
    // itself -- the priest overruled the sergeant -- and no hand comes off.
    const SentenceTerms hand = sentenceTerms(judgedOf(Judgment::TheHand, Judgment::Held, false), 100);
    REQUIRE(hand.served);
    CHECK(hand.hand);
    CHECK(hand.fineAsked == 23);
    CHECK(hand.cellHours == heldHours(10));
    CHECK(hand.bondDays == 0);
    CHECK(hand.days == 1);
    CHECK(hand.roofsDelta == kConvictionRoofsGain);
    CHECK(hand.templeDelta == 0);
    CHECK_FALSE(hand.releaseHere);
    const SentenceTerms handBound =
        sentenceTerms(judgedOf(Judgment::TheHand, Judgment::Bound, true), 100);
    CHECK(handBound.hand);
    CHECK(handBound.fineAsked == 0);
    CHECK(handBound.cellHours == 0);
    CHECK(handBound.bondDays == 2 * kBoundDays);
    CHECK(handBound.days == 10);
    CHECK(handBound.templeDelta == kBoundTempleGain - kLieTempleCost);
    const SentenceTerms handSpared =
        sentenceTerms(judgedOf(Judgment::Held, Judgment::Fined, false), 100);
    CHECK_FALSE(handSpared.hand);
    CHECK(handSpared.cellHours == heldHours(10));

    // COMMUTED: the hand ("the rope does not un-take the hand"), twelve days
    // bondsworn, the fine forgiven, the Flame's mercy remembered. Never
    // doubled -- the rope tier has nothing under it to double.
    const SentenceTerms commuted =
        sentenceTerms(judgedOf(Judgment::Commuted, Judgment::None, false), 100);
    REQUIRE(commuted.served);
    CHECK(commuted.hand);
    CHECK(commuted.fineAsked == 0);
    CHECK(commuted.cellHours == 0);
    CHECK(commuted.bondDays == kCommutedDays);
    CHECK(commuted.hours == 288);
    CHECK(commuted.days == 12);
    CHECK(commuted.mends);
    CHECK_FALSE(commuted.releaseHere);
    CHECK_FALSE(commuted.rope);
    CHECK(commuted.templeDelta == kCommutedTempleGain);
    CHECK(commuted.roofsDelta == kConvictionRoofsGain);

    // THE ROPE: the run. Nothing else applies.
    const SentenceTerms rope =
        sentenceTerms(judgedOf(Judgment::TheRope, Judgment::None, false), 100);
    REQUIRE(rope.served);
    CHECK(rope.rope);
    CHECK(rope.hours == 0);
    CHECK(rope.fineAsked == 0);
    CHECK_FALSE(rope.hand);
    CHECK_FALSE(rope.releaseHere);
    CHECK_FALSE(rope.mends);
    CHECK(rope.templeDelta == 0);
    CHECK(rope.roofsDelta == 0);

    // The lines the page prints, as the header fixes them.
    CHECK(kBoundDays == 5);
    CHECK(kCommutedDays == 12);
    CHECK(kShortfallRoyalsPerDay == 4);
    CHECK(kShortfallDaysMax == 7);
}

TEST_CASE("HELD is served in the room: the fine, the nights on the world clock, the mirror, the body, the record after the skip, the Tarwalk") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    CrimeLedger& crimes = talk.crimes();
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);  // 70: paper, and HELD to a confession (24 - 2 + 6 = 28)
    REQUIRE(crimes.warrant());
    gull.injurePlayer(3);
    REQUIRE(gull.playerHp() < gull.playerHpMax());
    REQUIRE(standUntilTaken(room));
    REQUIRE(gull.takeArrestRelease());  // the walk to the Mission's door
    // Nothing to serve before the plea.
    CHECK_FALSE(gull.serveSentence().served);
    const Arraignment answer = gull.plead(Plea::Guilty);
    REQUIRE(answer.heard);
    REQUIRE(answer.judgment == Judgment::Held);
    // The hearing, copied: serving it closes the record's copy.
    const HearingState hearing = gull.hearing();
    const std::int32_t purse = gull.playerCoin();
    const SentenceTerms expected = sentenceTerms(hearing, purse);
    REQUIRE(expected.served);
    REQUIRE(expected.days >= 1);
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t watch = talk.factions().indexOf("watch");
    const std::int32_t temple = talk.factions().indexOf("temple");
    REQUIRE(roofs >= 0);
    REQUIRE(watch >= 0);
    REQUIRE(temple >= 0);
    const std::int32_t roofsBefore = talk.standings().standing(roofs);
    const std::int32_t watchBefore = talk.standings().standing(watch);
    const std::int32_t templeBefore = talk.standings().standing(temple);
    const std::int32_t reputationBefore = talk.ledger().reputation();
    const std::int32_t dayBefore = gull.dayNumber();
    const std::int32_t clockBefore = gull.timeOfDay();
    const std::int32_t defeatsBefore = gull.nemesis().defeats();

    const Tavern::SentenceReport& served = gull.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.judgment == Judgment::Held);
    CHECK_FALSE(served.terms.doubled);
    // THE INTEGERS are the pure function's, to the Royal and the hour.
    CHECK(served.terms.fineAsked ==
          fineFor(hearing.sheet.heatAtArrest, hearing.sheet.unitsSeized));
    CHECK(served.terms.finePaid == expected.finePaid);
    CHECK(served.terms.shortfallDays == expected.shortfallDays);
    CHECK(served.terms.cellHours == heldHours(hearing.sheet.draw));
    CHECK(served.terms.hours == expected.hours);
    CHECK(served.terms.days == expected.days);
    CHECK(served.coinBefore == purse);
    CHECK(served.coinAfter == purse - expected.finePaid);
    // THE COIN left the purse, and never more than was in it.
    CHECK(gull.playerCoin() == purse - expected.finePaid);
    CHECK(gull.playerCoin() >= 0);
    // THE CLOCK moved by exactly the hours, on the world's own calendar: the
    // day count moved by the days (one more where the remainder crossed
    // midnight), and the clock face is where a skip of that length lands.
    CHECK(gull.timeOfDay() == (clockBefore + expected.hours * 3600) % kSecondsPerDay);
    CHECK(gull.dayNumber() - dayBefore >= expected.days);
    CHECK(gull.dayNumber() - dayBefore <= expected.days + 1);
    CHECK(served.dayReleased == gull.dayNumber());
    CHECK(served.timeReleased == gull.timeOfDay());
    // THE RECORD, written after the skip: the heat the ward keeps is live
    // (not cooled to nothing by the nights), the paper is gone, the prior is
    // on it, the days are counted, the hearing is closed.
    CHECK(crimes.heat() == kHeatAfterSentence);
    CHECK_FALSE(crimes.warrant());
    CHECK(crimes.arrests() == 1);
    CHECK(crimes.daysServed() == expected.days);
    CHECK(crimes.lastJudgment() == Judgment::Held);
    CHECK_FALSE(gull.hearingPending());
    CHECK_FALSE(crimes.maimed());
    CHECK_FALSE(gull.executed());
    // THE MIRROR: the roofs warm to whoever the Watch corrects, and the Watch
    // cools by half. The Flame is untouched by a confession served; the
    // social ledger is untouched by any sentence.
    CHECK(talk.standings().standing(roofs) == roofsBefore + kConvictionRoofsGain);
    CHECK(talk.standings().standing(watch) == watchBefore - kConvictionRoofsGain / 2);
    CHECK(talk.standings().standing(temple) == templeBefore);
    CHECK(talk.ledger().reputation() == reputationBefore);
    // THE BODY mended: the days did it.
    CHECK(gull.playerHp() == gull.playerHpMax());
    CHECK_FALSE(gull.playerFloored());
    // THE NEMESIS DID NOT RISE: losing to the law is not losing a fight.
    CHECK(gull.nemesis().defeats() == defeatsBefore);
    CHECK_FALSE(gull.takeDefeatRelease());
    // TURNED LOOSE ON THE TARWALK, through the same release the arrest fired.
    CHECK_FALSE(served.terms.releaseHere);
    CHECK(gull.takeArrestRelease());
    CHECK_FALSE(gull.takeArrestRelease());
    // And nothing is served twice.
    CHECK_FALSE(gull.serveSentence().served);
    CHECK(crimes.arrests() == 1);
}

TEST_CASE("FINED and SPARED walk out of the Mission's door: coin only, no clock, nothing mended, nothing mirrored") {
    // A tongue and a door: 24 + 10 TONGUE + 10 THE DOOR - 2 HEAT = 42; a
    // confession is 48, FINED. The sheet is the ledger's, opened by its own
    // verb in the arrest's shape (the Gull's arrest proves that path); the
    // bench is the same whoever walked you in.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    DialogueDirector& talk = gull.dialogue();
    CrimeLedger& crimes = talk.crimes();
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);
    const ChargeSheet sheet = crimes.charge(false, 20, 30, 0);
    crimes.openHearing(sheet, 0, 0x2Bull, "Watchman Cull");
    REQUIRE(gull.plead(Plea::Guilty).judgment == Judgment::Fined);
    gull.injurePlayer(2);
    const std::int32_t hpBefore = gull.playerHp();
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t fine = fineFor(sheet.heatAtArrest, 0);
    REQUIRE(fine > 0);
    REQUIRE(purse >= fine);
    const std::int32_t dayBefore = gull.dayNumber();
    const std::int32_t clockBefore = gull.timeOfDay();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t roofsBefore = talk.standings().standing(roofs);

    const Tavern::SentenceReport& served = gull.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.judgment == Judgment::Fined);
    CHECK(served.terms.finePaid == fine);
    CHECK(served.terms.hours == 0);
    CHECK(gull.playerCoin() == purse - fine);
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(gull.timeOfDay() == clockBefore);
    CHECK(gull.playerHp() == hpBefore);                      // a fine heals nothing
    CHECK(talk.standings().standing(roofs) == roofsBefore);  // a charge, not a correction
    CHECK(crimes.arrests() == 1);                            // but a conviction
    CHECK(crimes.heat() == kHeatAfterSentence);
    CHECK_FALSE(crimes.warrant());
    CHECK(served.terms.releaseHere);  // the Mission's door
    CHECK(gull.takeArrestRelease());

    // SPARED: a denial at the top of the band. No prior, nothing paid, the
    // paper torn up all the same, and the door.
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt);
    // 24 + 20 + 24 + 4 - 10 TAKEN BEFORE - 5 HEAT = 57; the band at its top, 67.
    ChargeSheet clean = crimes.charge(false, 40, 84, 20);
    clean.draw = drawWithBand(kPriestBand);
    crimes.openHearing(clean, 0, clean.draw, "Watchman Cull");
    const Arraignment walked = gull.plead(Plea::NotGuilty);
    REQUIRE(walked.judgment == Judgment::Spared);
    const std::int32_t purseBefore = gull.playerCoin();
    const Tavern::SentenceReport& sparedServed = gull.serveSentence();
    REQUIRE(sparedServed.served);
    CHECK(sparedServed.terms.judgment == Judgment::Spared);
    CHECK(gull.playerCoin() == purseBefore);
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(crimes.arrests() == 1);  // no prior for a man the bench spared
    CHECK(crimes.hearings() == 2);
    CHECK(crimes.heat() == kHeatAfterSentence);
    CHECK_FALSE(crimes.warrant());
    CHECK(sparedServed.terms.releaseHere);
    CHECK(gull.takeArrestRelease());
}

TEST_CASE("the world moves while you are held: the ward runs the days and the penny falls due, a taken job dies at its night, the nemesis does not rise") {
    using granadad::render::Session;
    Session session(quietDocks(20));
    Tavern& gull = session.tavern();
    DialogueDirector& talk = gull.dialogue();
    CrimeLedger& crimes = talk.crimes();
    Ward& ward = session.ward();
    REQUIRE(ward.loaded());
    // Eighty-eight days on, through the sentence's own verb and the wait
    // machinery's own sync, so that the ninetieth day -- a quarter-day on
    // the roll -- falls INSIDE the sentence to come.
    gull.skipHours(24 * 88);
    session.skipSeconds(60);
    REQUIRE(gull.dayNumber() == 88);
    REQUIRE(ward.day() == 88);
    // A roof of your own on the Quayward: a house-owner owes the plot's Den
    // Duke a ground penny every quarter, and nobody finds it from a cell.
    ward.setPlayerCoin(kHousePriceMansion);
    REQUIRE(ward.buyHouse(0) == TenureResult::Done);
    REQUIRE(ward.playerHousehold() >= 0);
    REQUIRE(ward.households()[static_cast<std::size_t>(ward.playerHousehold())].arrears == 0);
    // Tonight's board, and a job taken off it with a night or three on it.
    session.stepMany(MoveInput{}, kStepsPerSecond);
    ContractBoard& board = talk.contracts();
    std::int32_t jobId = -1;
    for (const Contract& row : board.contracts()) {
        if (row.state == ContractState::Offered) {
            jobId = row.id;
            break;
        }
    }
    REQUIRE(jobId >= 0);
    REQUIRE(board.take(jobId) == TakeResult::Taken);
    REQUIRE(board.find(jobId) != nullptr);
    REQUIRE(board.find(jobId)->dueOnDay < 88 + kCommutedDays);
    const std::int32_t failedBefore = board.failedCount();

    // A witnessed killing by the Mission's own Shepherd, and the bench's own
    // answer to a confession: COMMUTED, twelve days. Opened by the ledger's
    // verbs in the arrest's shape; the Gull's arrest proves that path.
    crimes.markMurderer(2);
    const ChargeSheet sheet = crimes.charge(false, 30, 84, 20);
    crimes.openHearing(sheet, 0, 0x5EEDull, "Watchman Cull");
    REQUIRE(gull.plead(Plea::Guilty).judgment == Judgment::Commuted);
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t watch = talk.factions().indexOf("watch");
    const std::int32_t temple = talk.factions().indexOf("temple");
    const std::int32_t roofsBefore = talk.standings().standing(roofs);
    const std::int32_t watchBefore = talk.standings().standing(watch);
    const std::int32_t templeBefore = talk.standings().standing(temple);
    const std::int64_t wardDaysBefore = ward.stats().days;
    const std::int64_t quartersBefore = ward.stats().quarters;
    const std::int64_t penniesShortBefore = ward.stats().penniesShort;
    const std::int64_t petitionsBefore = ward.stats().petitions;
    REQUIRE(ward.households()[static_cast<std::size_t>(ward.playerHousehold())].groundPenny > 0);
    const std::int32_t defeatsBefore = gull.nemesis().defeats();
    const std::size_t rivalsBefore = gull.nemesis().rivals().size();
    const std::int32_t purse = gull.playerCoin();
    gull.injurePlayer(2);
    REQUIRE(gull.playerHp() < gull.playerHpMax());

    const Tavern::SentenceReport& served = session.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.judgment == Judgment::Commuted);
    CHECK(served.terms.days == kCommutedDays);
    CHECK(served.terms.hours == kCommutedDays * 24);
    CHECK(served.terms.finePaid == 0);
    CHECK(served.terms.hand);
    CHECK(gull.playerCoin() == purse);  // the fine forgiven
    // THE CLOCK, and the ward's roll behind it: twelve days ran on the land,
    // the wage and the meal, and the quarter-day that fell inside them
    // charged the ground penny -- unpaid, because nobody in a cell finds it,
    // so the house-owner is in arrears and a quarter behind. The two courts
    // touch here, and that is the design.
    CHECK(gull.dayNumber() == 100);
    CHECK(session.timeOfDay() == gull.timeOfDay());
    CHECK(ward.day() == 100);
    CHECK(ward.stats().days == wardDaysBefore + kCommutedDays);
    CHECK(ward.stats().quarters == quartersBefore + 1);
    const Household& home = ward.households()[static_cast<std::size_t>(ward.playerHousehold())];
    CHECK(home.quartersKept == 1);
    CHECK(ward.stats().penniesShort >= penniesShortBefore + home.groundPenny);
    // ...and the Duke brought his petition to the same Mission that quarter
    // (the player's household is the last on the roll, so it is the last
    // heard): the convict is the tenant in the OTHER hearing. Allowed, not
    // prevented -- the two courts touching is the design.
    CHECK(ward.stats().petitions >= petitionsBefore + 1);
    CHECK(ward.lastHearing().heard);
    CHECK(ward.lastHearing().household == ward.playerHousehold());
    // A JOB PAST ITS NIGHT DIES at the next doors-open refresh, exactly as
    // shipped: one tick of the world, and the board no longer has it.
    session.stepMany(MoveInput{}, kStepsPerSecond);
    CHECK(board.failedCount() == failedBefore + 1);
    CHECK(board.find(jobId) == nullptr);
    // STANDING: the roofs warm, the Watch cools by half, the Flame remembers
    // its mercy.
    CHECK(talk.standings().standing(roofs) == roofsBefore + kConvictionRoofsGain);
    CHECK(talk.standings().standing(watch) == watchBefore - kConvictionRoofsGain / 2);
    CHECK(talk.standings().standing(temple) == templeBefore + kCommutedTempleGain);
    // THE RECORD: the blood served and the face remembered, the hand taken,
    // mercy spent, the heat live at twelve and the paper gone.
    CHECK_FALSE(crimes.murderer());
    CHECK(crimes.condemned());
    CHECK(crimes.commuted());
    CHECK(crimes.maimed());
    CHECK(crimes.heat() == kHeatAfterSentence);
    CHECK_FALSE(crimes.warrant());
    CHECK(crimes.arrests() == 1);
    CHECK(crimes.daysServed() == kCommutedDays);
    CHECK_FALSE(gull.hearingPending());
    CHECK_FALSE(gull.executed());
    // THE BODY mended. THE NEMESIS DID NOT RISE: no defeat, no rival, no
    // release for the quay.
    CHECK(gull.playerHp() == gull.playerHpMax());
    CHECK(gull.nemesis().defeats() == defeatsBefore);
    CHECK(gull.nemesis().rivals().size() == rivalsBefore);
    CHECK_FALSE(gull.takeDefeatRelease());
    // Turned loose on the Tarwalk (the release was consumed by the world's
    // own step above, exactly as the arrest's is).
    CHECK_FALSE(served.terms.releaseHere);
}

TEST_CASE("COMMUTED clears the blood and never the condemned bit, and a condemned murderer's rope hearing cannot commute") {
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    // The Shepherd's killing, commuted.
    crimes.markMurderer(2);
    crimes.openHearing(crimes.charge(false, 30, 84, 20), 0, 0x5EEDull, "Watchman Cull");
    REQUIRE(gull.plead(Plea::Guilty).judgment == Judgment::Commuted);
    const std::int32_t dayBefore = gull.dayNumber();
    REQUIRE(gull.serveSentence().served);
    CHECK(gull.dayNumber() - dayBefore >= kCommutedDays);
    CHECK_FALSE(crimes.murderer());
    CHECK(crimes.condemned());
    CHECK(crimes.commuted());
    CHECK(crimes.maimed());
    CHECK(crimes.takePercent() == kMaimedTakePercent);
    CHECK_FALSE(gull.executed());
    CHECK(gull.takeArrestRelease());
    // Nothing clears the condemned bit: not a served cell afterwards, not
    // cooling, not the roost, not a lost file.
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt);
    crimes.openHearing(crimes.charge(false, 30, 84, 20), 0, 0x5EEDull, "Watchman Cull");
    const Arraignment lift = gull.plead(Plea::Guilty);
    REQUIRE(lift.heard);
    CHECK(lift.judgment != Judgment::TheRope);
    CHECK(lift.judgment != Judgment::Commuted);
    REQUIRE(gull.serveSentence().served);
    crimes.cool(static_cast<std::int64_t>(kHeatCoolSeconds) * kHeatMax);
    crimes.lieLow();
    crimes.quashWarrant();
    CHECK(crimes.condemned());
    CHECK(crimes.commuted());
    CHECK(gull.takeArrestRelease());
    // A second killing, the same Shepherd's record: the rope bench has no
    // plea for him. Mercy was given once; the answer is the rope, and the
    // sentence serves it -- no clock, no coin, the run.
    crimes.markMurderer(1);
    crimes.openHearing(crimes.charge(false, 30, 84, 20), 0, 0x5EEDull, "Watchman Cull");
    const Arraignment silence = gull.plead(Plea::Guilty);
    REQUIRE(silence.heard);
    CHECK(silence.plea == Plea::NoPlea);
    CHECK(silence.judgment == Judgment::TheRope);
    const std::int32_t dayOfTheDrop = gull.dayNumber();
    const Tavern::SentenceReport& served = gull.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.rope);
    CHECK(gull.executed());
    CHECK(gull.runEnded());
    CHECK(gull.runEnd().ended);
    CHECK(gull.runEnd().blood);
    CHECK(gull.dayNumber() == dayOfTheDrop);
    CHECK_FALSE(gull.takeArrestRelease());
}

TEST_CASE("THE ROPE ends the run: the end is written off the record, nothing revives, and the room refuses the world") {
    // Hour 23, Watchman Cull at his drink. A patron put down in his sight --
    // a REAL corpse on the roster, so the end can name him -- and the Watch
    // closing on what it saw.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    REQUIRE(cull->present());
    const std::int32_t cullId = cull->id();
    const Actor* mark = room.standFacingPatronFor(*cull);
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    const std::string victim = mark->name();
    REQUIRE(killInSight(room, markId));
    REQUIRE(room.byId(markId)->activity() == Activity::Dead);
    REQUIRE(crimes.murderer());
    REQUIRE(crimes.slewWitnesses() >= 1);

    // Taken: on the violence he saw, or on the paper the killing wrote --
    // either is the same bench. The sheet is the rope's.
    REQUIRE(standUntilTaken(room));
    const Tavern::ArrestReport& arrest = gull.lastArrest();
    CHECK((arrest.cause == WatchCause::Violence || arrest.cause == WatchCause::Warrant));
    REQUIRE(gull.hearingPending());
    REQUIRE(gull.hearing().sheet.tier == Sentence::Condemned);
    CHECK(gull.hearing().sheet.blood);
    REQUIRE(gull.takeArrestRelease());  // to the Mission's door
    // A nobody hangs: 24 - 30 BLOOD - N SAW IT (+ 6 CONFESSED) is under the
    // mercy line whatever the tongue.
    const Arraignment answer = gull.plead(Plea::Guilty);
    REQUIRE(answer.heard);
    REQUIRE(answer.judgment == Judgment::TheRope);

    const std::int32_t dayBefore = gull.dayNumber();
    const std::int32_t clockBefore = gull.timeOfDay();
    const std::int32_t purse = gull.playerCoin();
    const std::int32_t hpBefore = gull.playerHp();
    const std::int32_t defeatsBefore = gull.nemesis().defeats();
    const Tavern::SentenceReport& served = gull.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.rope);
    CHECK(served.terms.hours == 0);
    CHECK(served.terms.finePaid == 0);
    // THE END, off the record: the place the ward hangs a man, the corpse's
    // own name, the clock at the drop. The plate composes the lines.
    CHECK(gull.executed());
    CHECK(gull.runEnded());
    const RunEnd& end = gull.runEnd();
    CHECK(end.ended);
    CHECK(end.place == std::string(kRopePlace));
    CHECK(end.reason == victim);
    CHECK(end.blood);
    CHECK(end.day == dayBefore);
    CHECK(end.secondOfDay == clockBefore);
    CHECK(crimes.lastJudgment() == Judgment::TheRope);
    CHECK_FALSE(gull.hearingPending());
    // NOT A DEFEAT: no clock jump, no coin, no blow, no release of either
    // kind, no rise.
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(gull.timeOfDay() == clockBefore);
    CHECK(gull.playerCoin() == purse);
    CHECK(gull.playerHp() == hpBefore);
    CHECK_FALSE(gull.playerFloored());
    CHECK_FALSE(gull.takeArrestRelease());
    CHECK_FALSE(gull.takeDefeatRelease());
    CHECK(gull.nemesis().defeats() == defeatsBefore);

    // NOTHING REVIVES. The quay is the nemesis ruling's and only its: the
    // revive is refused outright, clock and body untouched.
    gull.reviveAfterDefeat();
    CHECK(gull.executed());
    CHECK(gull.dayNumber() == dayBefore);
    CHECK(gull.timeOfDay() == clockBefore);
    CHECK(gull.playerHp() == hpBefore);
    // THE ROOM REFUSES THE WORLD: no blow lands on the body, no defeat is
    // applied and no rival rises, no swing is armed, the Watch takes nobody,
    // nothing is pleaded and nothing is served twice.
    gull.injurePlayer(5);
    CHECK(gull.playerHp() == hpBefore);
    gull.concedeTo(cullId);
    CHECK_FALSE(gull.playerFloored());
    CHECK_FALSE(gull.takeDefeatRelease());
    CHECK(gull.nemesis().defeats() == defeatsBefore);
    gull.playerAttackDown();
    CHECK(gull.playerCombatIdle());
    CHECK_FALSE(gull.playerHandsUp());
    crimes.addHeat(kHeatMax);
    for (int second = 0; second < 120; ++second) {
        (void)room.standBy("Watchman Cull");
        room.run(1);
    }
    CHECK_FALSE(gull.hearingPending());
    CHECK_FALSE(gull.takeArrestRelease());
    CHECK(gull.watchStance() == Tavern::WatchStance::Idle);
    CHECK_FALSE(gull.plead(Plea::Guilty).heard);
    CHECK_FALSE(gull.serveSentence().served);
    CHECK(gull.executed());
}

TEST_CASE("a Skyrunner's second is hanged FOR THE SECOND RUNG, and only the rope ends the run") {
    // The roofs' rope with no corpse behind it: the end names the rung.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    crimes.addHeat(kWarrantAt);
    crimes.sentence(Judgment::TheHand, 1);  // the first: the hand, served
    crimes.addHeat(kWarrantAt);
    const ChargeSheet second = crimes.charge(true, 0, 0, 0);
    REQUIRE(second.tier == Sentence::Condemned);
    REQUIRE(second.secondRung);
    REQUIRE_FALSE(second.blood);
    crimes.openHearing(second, 0, 0x5EEDull, "Watchman Cull");
    // 24 - 10 TAKEN BEFORE - 3 HEAT - 24 THE SECOND RUNG + 6 CONFESSED = -7.
    REQUIRE(gull.plead(Plea::Guilty).judgment == Judgment::TheRope);
    REQUIRE(gull.serveSentence().served);
    CHECK(gull.executed());
    CHECK(gull.runEnd().reason == std::string(kRopeForSecondRung));
    CHECK_FALSE(gull.runEnd().blood);
    CHECK(gull.runEnd().place == std::string(kRopePlace));

    // AND ONLY THE ROPE. Every other judgment served leaves the run alive:
    // the bench's six mercies, each on its own record, its terms never the
    // rope's and its bit never set.
    for (const Judgment mercy : {Judgment::Spared, Judgment::Fined, Judgment::Held,
                                 Judgment::Bound, Judgment::TheHand, Judgment::Commuted}) {
        CrimeLedger record;
        record.addHeat(kWarrantAt);
        record.openHearing(record.charge(false, 0, 0, 0), 0, 0, "Watchman Cull");
        // Any plea the bench takes, then the judgment set by hand on the
        // hearing's own shape: the terms serve what was judged.
        REQUIRE(record.plead(Plea::Guilty).heard);
        HearingState judged = record.hearing();
        judged.judgment = mercy;
        judged.band = mercy == Judgment::TheHand ? Judgment::Held : mercy;
        const SentenceTerms terms = sentenceTerms(judged, 100);
        REQUIRE(terms.served);
        CHECK_FALSE(terms.rope);
        record.sentence(mercy, terms.days);
        CHECK_FALSE(record.executed());
        CHECK(record.heat() == kHeatAfterSentence);
    }
}

TEST_CASE("a combat defeat still revives after a served sentence; the two paths never share a line") {
    // HELD served, then put on the floor: the quay, exactly as before the
    // bench ever heard you. The rope's bit is not set by a beating, and the
    // court's release was never the quay's.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);
    REQUIRE(standUntilTaken(room));
    REQUIRE(gull.takeArrestRelease());
    REQUIRE(gull.plead(Plea::Guilty).judgment == Judgment::Held);
    REQUIRE(gull.serveSentence().served);
    REQUIRE(gull.takeArrestRelease());
    CHECK_FALSE(gull.executed());
    // Back to a full taproom (the shipped wait), and a man to lose to.
    gull.skipTo(hourOfDay(23));
    room.run(2);
    const Actor* rival = room.findRole(ActorRole::Patron);
    REQUIRE(rival != nullptr);
    const std::int32_t dayBefore = gull.dayNumber();
    const std::int32_t defeatsBefore = gull.nemesis().defeats();
    gull.concedeTo(rival->id());
    CHECK(gull.playerFloored());
    CHECK(gull.nemesis().defeats() == defeatsBefore + 1);  // he rose: a fight lost is his
    CHECK(gull.takeDefeatRelease());
    CHECK_FALSE(gull.takeArrestRelease());
    gull.reviveAfterDefeat();
    CHECK(gull.playerHp() == gull.playerHpMax());
    CHECK_FALSE(gull.playerFloored());
    CHECK(gull.dayNumber() >= dayBefore);
    CHECK_FALSE(gull.executed());
    CHECK_FALSE(gull.runEnd().ended);
    CHECK(crimes.lastJudgment() == Judgment::Held);
}

TEST_CASE("a Violence arrest with paper flows to the same bench and the same sentence") {
    // The combat-feel path: a blade drawn on a patron in the watchman's
    // sight closes him on VIOLENCE (test_watch_rhythm's own case). With paper
    // already out, the arrest opens the hearing like any other, and the
    // sentence serves it like any other.
    Room room(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& gull = room.tavern();
    CrimeLedger& crimes = gull.dialogue().crimes();
    crimes.commit(Crime::Lift, true);
    crimes.addHeat(kWarrantAt + 2);
    REQUIRE(crimes.warrant());
    room.run(2);
    const Actor* cull = room.findByName("Watchman Cull");
    REQUIRE(cull != nullptr);
    const Actor* mark = room.standFacingPatronFor(*cull);
    REQUIRE(mark != nullptr);
    const std::int32_t markId = mark->id();
    gull.setPlayerCombat(Weapon::Edged, Intent::Kill);
    for (int i = 0; i < 30 && room.expectedSightlineId() != markId; ++i) {
        room.stepOnce();
        REQUIRE(room.standFacing(*mark) != -1);
    }
    const Tavern::PlayerSwingResult tap = room.swing(false);
    REQUIRE(tap.targetId == markId);
    REQUIRE(tap.fight == FightClass::Lethal);
    bool closing = false;
    for (int s = 0; s < 20 && !closing; ++s) {
        room.run(1);
        closing = gull.watchStance() == Tavern::WatchStance::Closing;
    }
    REQUIRE(closing);
    REQUIRE(gull.watchInterest() == WatchCause::Violence);
    bool taken = false;
    for (int s = 0; s < 60 && !taken; ++s) {
        room.run(1);
        taken = gull.takeArrestRelease();
    }
    REQUIRE(taken);
    CHECK(gull.lastArrest().cause == WatchCause::Violence);
    // THE SAME BENCH: a hearing open on the paper, nothing served at the
    // door, the room let go of the fight.
    REQUIRE(gull.hearingPending());
    CHECK(gull.hearing().awaitingPlea());
    CHECK(gull.hearing().sheet.tier != Sentence::Fined);
    CHECK(gull.lastArrest().fine == 0);
    CHECK(gull.lastArrest().heldHours == 0);
    CHECK(crimes.arrests() == 0);
    CHECK_FALSE(gull.playerInBrawl());
    // THE SAME SENTENCE.
    const Arraignment answer = gull.plead(Plea::Guilty);
    REQUIRE(answer.heard);
    CHECK(answer.judgment != Judgment::Spared);
    const Tavern::SentenceReport& served = gull.serveSentence();
    REQUIRE(served.served);
    CHECK(served.terms.judgment == answer.judgment);
    CHECK(crimes.heat() == kHeatAfterSentence);
    CHECK_FALSE(crimes.warrant());
    CHECK_FALSE(gull.hearingPending());
    CHECK(gull.takeArrestRelease());
}

TEST_CASE("a scripted arrest, plea and sentence twin-runs byte-identical, and the other plea's sentence diverges") {
    const auto script = [](Room& room, Plea plea) {
        Tavern& gull = room.tavern();
        gull.dialogue().crimes().commit(Crime::Lift, true);
        gull.dialogue().crimes().addHeat(kWarrantAt + 2);
        REQUIRE(standUntilTaken(room));
        REQUIRE(gull.takeArrestRelease());
        const Arraignment answer = gull.plead(plea);
        REQUIRE(answer.heard);
        const Tavern::SentenceReport& served = gull.serveSentence();
        REQUIRE(served.served);
        REQUIRE(gull.takeArrestRelease());
        room.run(3);
        return std::tuple{room.hash(),           served.terms.judgment, served.terms.hours,
                          served.terms.finePaid, gull.dayNumber(),      gull.playerCoin()};
    };
    Room one(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    Room two(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    const auto first = script(one, Plea::Guilty);
    const auto second = script(two, Plea::Guilty);
    CHECK(first == second);
    // The same seed denying: the same arrest and the same draw, a doubled
    // sentence or a spared one, and a different world at the end of it.
    Room three(hourOfDay(23), gull::kBartenderX, gull::kBartenderY + 1);
    const auto other = script(three, Plea::NotGuilty);
    CHECK(std::get<0>(other) != std::get<0>(first));
    CHECK(three.tavern().lastServed().terms.doubled ==
          (three.tavern().dialogue().crimes().lastJudgment() != Judgment::Spared));
}
