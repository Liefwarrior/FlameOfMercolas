// The wind: the pool's own arithmetic, the FatigueTerm's ends, the four
// attribute readers, and the same-roll discipline the whole build is under --
// no mechanic in the fatigue build adds a draw anywhere, and at a full pool
// with a base-40 sheet every touched roll is bit-identical to what shipped.

#include <doctest/doctest.h>

#include <cstdint>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/fatigue.hpp"
#include "granadad/sim/player.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

AttributeBlock sheetWith(AttributeId attribute, std::int32_t value) {
    AttributeBlock block;
    block.setValue(attribute, value);
    return block;
}

}  // namespace

// ---------------------------------------------------------------------------
// the pool's size, and the mapping that derives it
// ---------------------------------------------------------------------------

TEST_CASE("the base sheet's pool is the stated constant, and VIG carries double") {
    // The constant a default-constructed pool is born at must be exactly what
    // the formula answers for the sheet chargen starts from -- the two are
    // stated in different files and this is what stops them drifting.
    CHECK(fatigueMaxPoints(AttributeBlock{}) == kFatigueBasePoints);
    CHECK(kFatigueBasePoints == 160);

    // 2*VIG + MGT + AGI: ten points of Vigor are worth twenty of pool, ten of
    // Might or Agility ten each, and Wit -- deliberately, see fatigue.hpp's
    // header -- none at all.
    CHECK(fatigueMaxPoints(sheetWith(AttributeId::Vigor, 50)) == 180);
    CHECK(fatigueMaxPoints(sheetWith(AttributeId::Might, 50)) == 170);
    CHECK(fatigueMaxPoints(sheetWith(AttributeId::Agility, 50)) == 170);
    CHECK(fatigueMaxPoints(sheetWith(AttributeId::Wit, 100)) == 160);
}

TEST_CASE("a fresh pool is full at the base size, and resetFor resizes and refills") {
    PlayerFatigue pool;
    CHECK(pool.maxPoints() == kFatigueBasePoints);
    CHECK(pool.currentFine() == pool.maxFine());
    CHECK_FALSE(pool.winded());

    pool.drain(10 * kFatiguePointFine);
    pool.resetFor(sheetWith(AttributeId::Vigor, 100));
    CHECK(pool.maxPoints() == 280);
    CHECK(pool.currentFine() == pool.maxFine());
    CHECK_FALSE(pool.winded());
}

// ---------------------------------------------------------------------------
// the term
// ---------------------------------------------------------------------------

TEST_CASE("the FatigueTerm runs 320 full to 192 empty and clamps its inputs") {
    const std::int32_t max = 160 * kFatiguePointFine;
    CHECK(fatigueTermQ8(max, max) == kFatigueTermFullQ8);
    CHECK(fatigueTermQ8(0, max) == kFatigueTermEmptyQ8);
    // Dead centre of the pool is the exact neutral 256 -- the linear map's
    // own midpoint, which is what lets "half wind" read as "no modifier".
    CHECK(fatigueTermQ8(max / 2, max) == 256);
    // Out-of-range currents clamp rather than extrapolate.
    CHECK(fatigueTermQ8(-100, max) == kFatigueTermEmptyQ8);
    CHECK(fatigueTermQ8(max + 100, max) == kFatigueTermFullQ8);
    // A degenerate pool reads FULL -- the neutral baseline, never a penalty.
    CHECK(fatigueTermQ8(0, 0) == kFatigueTermFullQ8);
    // Monotone: more wind is never a worse term.
    std::int32_t last = kFatigueTermEmptyQ8;
    for (std::int32_t fine = 0; fine <= max; fine += kFatiguePointFine) {
        const std::int32_t term = fatigueTermQ8(fine, max);
        CHECK(term >= last);
        last = term;
    }
    CHECK(last == kFatigueTermFullQ8);
}

// ---------------------------------------------------------------------------
// drain, regen, and the winded latch
// ---------------------------------------------------------------------------

TEST_CASE("drain floors at zero, sets winded, and regen recovers through the latch") {
    PlayerFatigue pool;
    const std::int32_t max = pool.maxFine();

    // Arithmetic is exact: one sprint step costs exactly the constant.
    pool.drain(kSprintDrainFinePerStep);
    CHECK(pool.currentFine() == max - kSprintDrainFinePerStep);
    CHECK_FALSE(pool.winded());

    // Overdraining floors at zero -- empty is a state, not a debt.
    pool.drain(2 * max);
    CHECK(pool.currentFine() == 0);
    CHECK(pool.winded());

    // Winded HOLDS below the recovery line (max/8), whatever trickles back --
    // the hysteresis that stops the sprint gate strobing.
    pool.regen(max / kWindedRecoverDivisor - 1);
    CHECK(pool.winded());
    pool.regen(1);
    CHECK_FALSE(pool.winded());

    // Regen caps at max.
    pool.regen(2 * max);
    CHECK(pool.currentFine() == max);
}

TEST_CASE("regen rate: VIG and grit raise it, moving halves it") {
    // Base sheet, standing easy: 8 + 40/8 + 0 = 13 fine a step (3.0 pts/s).
    CHECK(fatigueRegenFinePerStep(40, 0, false) == 13);
    // Walking halves it.
    CHECK(fatigueRegenFinePerStep(40, 0, true) == 6);
    // VIG's share: a Vigor-100 body makes 8 + 12 = 20.
    CHECK(fatigueRegenFinePerStep(100, 0, false) == 20);
    // GRIT's nudge, the skill a player levels by working: +1 fine per 4
    // levels, +5 at level 20 -- felt, and never the attribute's equal.
    CHECK(fatigueRegenFinePerStep(40, 20, false) == 18);
    CHECK(fatigueRegenFinePerStep(40, 40, false) == 23);
    // Garbage clamps rather than exploding.
    CHECK(fatigueRegenFinePerStep(-5, -5, false) == fatigueRegenFinePerStep(10, 0, false));
}

// ---------------------------------------------------------------------------
// the verbs' costs: AGI prices them, skyrunning discounts them
// ---------------------------------------------------------------------------

TEST_CASE("vertical costs are exact at the base sheet and discount with AGI and skyrunning") {
    // Base sheet, unskilled: the cost is the stated points, to the fine unit.
    CHECK(verticalFatigueCostFine(kMantleFatiguePoints, 40, 0) ==
          kMantleFatiguePoints * kFatiguePointFine);
    CHECK(verticalFatigueCostFine(kJumpFatiguePoints, 40, 0) ==
          kJumpFatiguePoints * kFatiguePointFine);

    // AGI discounts, and never to free.
    const std::int32_t spry = verticalFatigueCostFine(kMantleFatiguePoints, 100, 0);
    CHECK(spry < kMantleFatiguePoints * kFatiguePointFine);
    CHECK(spry > 0);
    // A clumsy body pays a premium.
    CHECK(verticalFatigueCostFine(kMantleFatiguePoints, 10, 0) >
          kMantleFatiguePoints * kFatiguePointFine);

    // Skyrunning level 20 has taken the climb down by exactly the stated
    // share: (64-20)/64 of the AGI-priced cost.
    const std::int32_t taught = verticalFatigueCostFine(kMantleFatiguePoints, 40, 20);
    CHECK(taught == (kMantleFatiguePoints * kFatiguePointFine * 44) / 64);
    // Monotone across the whole run a player levels through.
    std::int32_t last = verticalFatigueCostFine(kMantleFatiguePoints, 40, 0);
    for (std::int32_t level = 1; level <= 20; ++level) {
        const std::int32_t cost = verticalFatigueCostFine(kMantleFatiguePoints, 40, level);
        CHECK(cost <= last);
        last = cost;
    }
}

// ---------------------------------------------------------------------------
// the four readers, each neutral at base 40
// ---------------------------------------------------------------------------

TEST_CASE("every attribute reader is exactly neutral at the base-40 sheet") {
    CHECK(meleeDamageBonus(40) == 0);
    CHECK(agilitySpeedScaleQ8(40) == 256);
    CHECK(castWitBonusPercent(40) == 0);
    CHECK(witScaledCooldown(30, 40) == 30);
}

TEST_CASE("the readers' ends: modest, monotone, and never absurd") {
    // MGT: +4 damage at the ceiling, -2 at the floor -- beside fists' base 3.
    CHECK(meleeDamageBonus(100) == 4);
    CHECK(meleeDamageBonus(10) == -2);
    // AGI: 304/256 at the ceiling (~19% over), 232/256 at the floor.
    CHECK(agilitySpeedScaleQ8(100) == 304);
    CHECK(agilitySpeedScaleQ8(10) == 232);
    // WIT: +12 points of cast chance at the ceiling; recovery at 0.8x, and a
    // real cooldown never scales to nothing.
    CHECK(castWitBonusPercent(100) == 12);
    CHECK(witScaledCooldown(30, 100) == 24);
    CHECK(witScaledCooldown(1, 100) == 1);
    CHECK(witScaledCooldown(0, 100) == 0);
}

// ---------------------------------------------------------------------------
// same-roll discipline: strike() at a full pool is the shipped strike()
// ---------------------------------------------------------------------------

TEST_CASE("strike at full term with no bonus is bit-identical to the shipped roll") {
    // The shipped behaviour, restated: miss iff the low three bits are zero,
    // else base damage plus (roll>>3)%3. The sweep runs the high bits through
    // real values too, so the tired-miss band is PROVEN closed at full term
    // rather than merely untriggered.
    for (std::uint64_t seed = 0; seed < 4096; ++seed) {
        const std::uint64_t roll = seed * 0x9E3779B97F4A7C15ull + seed;
        Fighter target;
        target.hp = 1000;
        target.hpMax = 1000;
        const Blow blow = strike(Weapon::Fists, target, roll, 0, kFatigueTermFullQ8);
        if ((roll & 0x7U) == 0U) {
            CHECK_FALSE(blow.landed);
            CHECK(blow.damage == 0);
        } else {
            CHECK(blow.landed);
            CHECK(blow.damage ==
                  baseDamage(Weapon::Fists) + static_cast<std::int32_t>((roll >> 3) % 3U));
        }
        // And the two-argument call IS the five-argument call at defaults.
        Fighter twin;
        twin.hp = 1000;
        twin.hpMax = 1000;
        const Blow viaDefaults = strike(Weapon::Fists, twin, roll);
        CHECK(viaDefaults.landed == blow.landed);
        CHECK(viaDefaults.damage == blow.damage);
    }
}

// ---------------------------------------------------------------------------
// action-combat: the charge tier scales the ROLLED weapon damage
// ---------------------------------------------------------------------------

TEST_CASE("the charge tier: kSwingChargeQ8 is the shipped blow, kHardSwingChargeQ8 doubles it") {
    // ((base+variance)*chargeQ8)>>8: at 256 that is base+variance to the bit
    // (so a tap is the shipped blow, and the default third arg), and at 512 it
    // doubles the rolled weapon damage. The whiff carving is identical at both
    // tiers -- a hard swing is not a truer swing -- and the crown, Evictor's
    // own, rides the same roll at both. Swept across every weapon and the high
    // bits so the tired band and the crown are exercised, not merely present.
    for (const Weapon weapon :
         {Weapon::Fists, Weapon::Improvised, Weapon::Blunt, Weapon::Evictor, Weapon::Edged}) {
        for (std::uint64_t seed = 0; seed < 2048; ++seed) {
            const std::uint64_t roll = seed * 0x9E3779B97F4A7C15ull + seed;
            Fighter tap;
            tap.hp = 100000;
            tap.hpMax = 100000;
            Fighter hard;
            hard.hp = 100000;
            hard.hpMax = 100000;
            Fighter shipped;
            shipped.hp = 100000;
            shipped.hpMax = 100000;
            const Blow tapBlow =
                strike(weapon, tap, roll, 0, kFatigueTermFullQ8, kSwingChargeQ8);
            const Blow hardBlow =
                strike(weapon, hard, roll, 0, kFatigueTermFullQ8, kHardSwingChargeQ8);
            // kSwingChargeQ8 is the default third argument, and the shipped blow.
            const Blow byDefault = strike(weapon, shipped, roll);
            CHECK(tapBlow.landed == byDefault.landed);
            CHECK(tapBlow.damage == byDefault.damage);
            CHECK(tapBlow.crowned == byDefault.crowned);
            // Same roll, same whiff, same crown -- only the rolled damage scales.
            CHECK(hardBlow.landed == tapBlow.landed);
            CHECK(hardBlow.crowned == tapBlow.crowned);
            if (tapBlow.landed) {
                CHECK(hardBlow.damage == 2 * tapBlow.damage);
            } else {
                CHECK(hardBlow.damage == 0);
            }
        }
    }
}

TEST_CASE("the MGT bonus lands AFTER the charge scale: a hard swing doubles the weapon, not the arm") {
    // ((base+variance)*chargeQ8>>8) + bonus, then max(1,..): the bonus is added
    // once, after the tier, so a hard swing doubles the WEAPON and carries the
    // MGT behind it unchanged. roll 0x11 lands (low bits set) with variance
    // (0x11>>3)%3 == 2.
    const std::uint64_t roll = 0x11ull;
    const std::int32_t bonus = meleeDamageBonus(100);  // +4 at the ceiling
    REQUIRE(bonus == 4);
    Fighter tap;
    tap.hp = 1000;
    tap.hpMax = 1000;
    Fighter hard;
    hard.hp = 1000;
    hard.hpMax = 1000;
    const Blow tapBlow = strike(Weapon::Fists, tap, roll, bonus, kFatigueTermFullQ8, kSwingChargeQ8);
    const Blow hardBlow =
        strike(Weapon::Fists, hard, roll, bonus, kFatigueTermFullQ8, kHardSwingChargeQ8);
    const std::int32_t rolled = baseDamage(Weapon::Fists) + 2;  // base 3 + variance 2
    CHECK(tapBlow.damage == rolled + bonus);
    CHECK(hardBlow.damage == 2 * rolled + bonus);
}

TEST_CASE("an emptying pool only ever ADDS misses, off the same roll") {
    std::int32_t fullMisses = 0;
    std::int32_t emptyMisses = 0;
    for (std::uint64_t seed = 0; seed < 4096; ++seed) {
        const std::uint64_t roll = seed * 0x9E3779B97F4A7C15ull + seed;
        Fighter one;
        one.hp = 1000;
        one.hpMax = 1000;
        Fighter two;
        two.hp = 1000;
        two.hpMax = 1000;
        const Blow atFull = strike(Weapon::Fists, one, roll, 0, kFatigueTermFullQ8);
        const Blow atEmpty = strike(Weapon::Fists, two, roll, 0, kFatigueTermEmptyQ8);
        // Monotone per-roll: a swing that whiffed rested still whiffs
        // exhausted -- the tired band is a second band, never a reshuffle.
        if (!atFull.landed) {
            CHECK_FALSE(atEmpty.landed);
        }
        // And a blow that lands lands for the same hit points: exhaustion
        // argues with WHETHER, never with HOW HARD -- that is MGT's seam.
        if (atEmpty.landed) {
            CHECK(atEmpty.damage == atFull.damage);
        }
        fullMisses += atFull.landed ? 0 : 1;
        emptyMisses += atEmpty.landed ? 0 : 1;
    }
    // The empty band is real: materially more whiffs across the sweep --
    // 1/8 at full against 1/8 + (7/8)*(16/64) =~ 1/3 at empty.
    CHECK(emptyMisses > fullMisses + 400);
    CHECK(fullMisses > 300);  // and the shipped 1-in-8 is still there (~512)
}

TEST_CASE("the MGT bonus lands on damage, floored so a landed blow still bruises") {
    for (std::uint64_t roll : {0x11ull, 0x3F2ull, 0x7E9ull}) {
        if ((roll & 0x7U) == 0U) {
            continue;
        }
        Fighter plain;
        plain.hp = 1000;
        plain.hpMax = 1000;
        Fighter strong;
        strong.hp = 1000;
        strong.hpMax = 1000;
        const Blow bare = strike(Weapon::Fists, plain, roll, 0, kFatigueTermFullQ8);
        const Blow heavy = strike(Weapon::Fists, strong, roll, meleeDamageBonus(100),
                                  kFatigueTermFullQ8);
        CHECK(heavy.damage == bare.damage + 4);
    }
    // The floor: even an absurd negative bonus cannot turn a landed blow into
    // a refund.
    Fighter target;
    target.hp = 1000;
    target.hpMax = 1000;
    const Blow soft = strike(Weapon::Fists, target, 0x11ull, -100, kFatigueTermFullQ8);
    CHECK(soft.landed);
    CHECK(soft.damage == 1);
}

// ---------------------------------------------------------------------------
// AGILITY moves the legs -- the one reader that lives on the body
// ---------------------------------------------------------------------------

namespace {

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

const TileQuery& docksTiles() {
    static const TileQuery tiles(docksWorld());
    return tiles;
}

// test_player.cpp's own walking tile: open street, nothing to climb.
constexpr std::int32_t kWalkTileX = 143;
constexpr std::int32_t kWalkTileY = 61;

}  // namespace

TEST_CASE("the AGI speed scale moves a real body measurably, and 256 is the shipped legs") {
    MoveInput forward;
    forward.forward = 1;
    forward.snapVelocity = true;  // measuring a speed, not a ramp

    PlayerBody stock(docksTiles(), kWalkTileX, kWalkTileY, docks::kSpawnBand, kFacingNorth);
    PlayerBody spry(docksTiles(), kWalkTileX, kWalkTileY, docks::kSpawnBand, kFacingNorth);
    spry.setSpeedScaleQ8(agilitySpeedScaleQ8(100));
    PlayerBody untouched(docksTiles(), kWalkTileX, kWalkTileY, docks::kSpawnBand,
                         kFacingNorth);

    // Twenty steps: the spry body covers ~2.2 tiles of the open street, the
    // stock one ~1.9 -- both well inside the clear run north of the walking
    // tile, so nothing here is measuring a wall.
    for (int i = 0; i < 20; ++i) {
        stock.step(forward);
        spry.step(forward);
        untouched.step(forward);
    }
    // Default scale IS the shipped body, to the bit.
    CHECK(stock.x() == untouched.x());
    CHECK(stock.y() == untouched.y());
    // The ceiling sheet visibly outpaces it: (24*304)>>8 = 28 against 24 Q8 a
    // step, about 19% -- felt, and not a different game.
    const std::int32_t stockMoved = stock.y() < spry.y() ? spry.y() - stock.y()
                                                         : stock.y() - spry.y();
    CHECK(stockMoved > 0);
    // And the scale is in the digest: two bodies in the same place with
    // different legs are not the same body.
    CHECK(stock.digest() != spry.digest());
}
