#pragma once

// HOW BIG A METRE IS. One number, and everything that has to agree with a human
// body reads it from here.
//
// WHY THIS FILE EXISTS
//
// sim/vertical_scale.hpp settled how tall a storey is and, in passing, said out
// loud that a tile is "roughly 0.9 m" -- but it said it in a COMMENT. Nothing
// compiled against it. So every number that is really a fact about people --
// how fast a person walks, how high they jump, what a fall does to them -- was
// typed as a raw Q8 constant with a sentence beside it explaining what it was
// supposed to mean, and the sentence and the constant were free to drift apart.
// They had. Walking was quoted as "deliberately unhurried" at 2.3 m/s, which is
// a jog; the fall-damage curve was a flat 24 hit points a band, which is a
// staircase and not gravity.
//
// This file is the assumption, stated once, in integers, with the conversions
// beside it so a comment cannot lie about what a constant is worth.
//
// EVERYTHING HERE IS INTEGER, AND HAS TO BE. These conversions feed simulation
// constants and the fall curve runs inside sim, so there is no float anywhere in
// the chain -- including the square root, which is Newton's method on integers
// and is exact in the sense that matters: the same input gives the same output
// on every machine that will ever run this.
//
// THE UNIT IS THE MILLIMETRE. Not the metre (a person is 1.7 of one, and
// integer maths in units that coarse is all remainder) and not the centimetre
// (gravity is 981 of those per second squared, which reads like a typo).
// Millimetres put every quantity this game cares about -- a stride, a storey, a
// terminal velocity -- in comfortable four- and five-digit integers.

#include <cstdint>

#include "granadad/sim/vertical_scale.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the assumption
// ---------------------------------------------------------------------------

/// HOW WIDE A TILE IS, IN MILLIMETRES. The one number this file is for.
///
/// It is not invented here and it is not adjustable to taste: sim/player.hpp's
/// kBodyRadius has been 90/256 of a tile since S1, so a standing body is 0.70 of
/// a tile across, and a human standing square is about 0.64 m across the
/// shoulders. 0.64 / 0.70 = 0.91. Nine hundred millimetres, and the collision
/// constant that implies it is older than this file.
///
/// CHANGE THIS AND YOU CHANGE THE WORLD, not the numbers below: walk speed,
/// jump height, mantle reach and the fall curve are all quoted in millimetres
/// of real human performance and converted THROUGH this constant, so a different
/// tile size re-derives all of them correctly and automatically.
inline constexpr std::int32_t kMillimetresPerTile = 900;

/// A storey, in millimetres. 2,700 -- floor to floor, which is a real building.
inline constexpr std::int32_t kMillimetresPerBand = kMillimetresPerTile * kTilesPerBand;
static_assert(kMillimetresPerBand == 2700, "a storey stopped being a storey");

/// Movement steps per simulated second.
///
/// MOVED HERE FROM player.hpp because it is half of every speed conversion
/// below: a speed in millimetres per second only becomes a per-step integer once
/// you know how many steps a second there are. player.hpp still declares the
/// body; this file declares the clock the body's numbers are quoted against.
inline constexpr std::int32_t kStepsPerSecond = 60;

/// Standard gravity, millimetres per second squared. 9.81 m/s^2.
inline constexpr std::int32_t kGravityMmPerSecSq = 9810;

// ---------------------------------------------------------------------------
// converting
// ---------------------------------------------------------------------------

/// Millimetres as Q8 tile-widths, rounded to nearest rather than truncated. A
/// truncating conversion is a systematic bias toward zero, and a body built out
/// of a dozen of them ends up measurably shorter than the person it is meant to
/// be.
[[nodiscard]] constexpr std::int32_t tilesQ8FromMm(std::int32_t mm) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(mm) * 256;
    const std::int64_t half = kMillimetresPerTile / 2;
    return static_cast<std::int32_t>((scaled >= 0 ? scaled + half : scaled - half) /
                                     kMillimetresPerTile);
}

/// And back, so a test can assert what a shipped constant is actually worth in
/// the units the design was written in.
[[nodiscard]] constexpr std::int32_t mmFromTilesQ8(std::int32_t tilesQ8) noexcept {
    return static_cast<std::int32_t>(
        (static_cast<std::int64_t>(tilesQ8) * kMillimetresPerTile) / 256);
}

/// Millimetres as BAND-RELATIVE Q8 -- the axis PlayerBody::feetZ() is measured
/// on, where 256 is one whole storey and not one tile. Getting these two Q8 axes
/// mixed up is the single easiest mistake to make in this codebase; see the
/// "WHICH AXIS IS IN WHICH UNIT" note in vertical_scale.hpp.
[[nodiscard]] constexpr std::int32_t bandQ8FromMm(std::int32_t mm) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(mm) * 256;
    const std::int64_t half = kMillimetresPerBand / 2;
    return static_cast<std::int32_t>((scaled >= 0 ? scaled + half : scaled - half) /
                                     kMillimetresPerBand);
}

[[nodiscard]] constexpr std::int32_t mmFromBandQ8(std::int32_t bandQ8) noexcept {
    return static_cast<std::int32_t>(
        (static_cast<std::int64_t>(bandQ8) * kMillimetresPerBand) / 256);
}

/// A real-world speed in millimetres per second, as the Q8 tile-widths per
/// MOVEMENT STEP the body actually adds to its position.
///
/// The rounding is where the honesty is. 1.4 m/s comes out as 7 (not 6.64), so
/// the shipped walk is 1.48 m/s -- and mmPerSecFromQ8PerStep below exists so a
/// case can assert that rather than a comment claiming 1.4 and being wrong by
/// five percent forever.
[[nodiscard]] constexpr std::int32_t speedQ8PerStep(std::int32_t mmPerSecond) noexcept {
    const std::int64_t num = static_cast<std::int64_t>(mmPerSecond) * 256;
    const std::int64_t den = static_cast<std::int64_t>(kMillimetresPerTile) * kStepsPerSecond;
    return static_cast<std::int32_t>((num + den / 2) / den);
}

/// What a shipped per-step constant is really worth, millimetres per second.
[[nodiscard]] constexpr std::int32_t mmPerSecFromQ8PerStep(std::int32_t q8PerStep) noexcept {
    const std::int64_t num = static_cast<std::int64_t>(q8PerStep) * kMillimetresPerTile *
                             kStepsPerSecond;
    return static_cast<std::int32_t>(num / 256);
}

// ---------------------------------------------------------------------------
// gravity
// ---------------------------------------------------------------------------

/// Integer square root, floored. Newton's method, no <cmath>, no float, exact
/// and identical on every target -- which is the whole reason it is written out
/// rather than called.
[[nodiscard]] constexpr std::int64_t isqrt64(std::int64_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    // A generous first guess that is always >= the answer, so the iteration
    // descends monotonically and cannot oscillate.
    std::int64_t guess = value;
    std::int64_t bit = 1;
    while (bit * bit < value && bit < (1LL << 31)) {
        bit <<= 1;
    }
    guess = bit;
    while (true) {
        const std::int64_t next = (guess + value / guess) / 2;
        if (next >= guess) {
            return guess;
        }
        guess = next;
    }
}

/// How fast a body is going, in millimetres per second, when it has fallen
/// `fallMm` from rest. v = sqrt(2 g h), and that IS the physics -- there is no
/// tuning constant hiding in it.
[[nodiscard]] constexpr std::int32_t impactSpeedMmPerSec(std::int32_t fallMm) noexcept {
    if (fallMm <= 0) {
        return 0;
    }
    const std::int64_t vSquared = 2LL * kGravityMmPerSecSq * fallMm;
    return static_cast<std::int32_t>(isqrt64(vSquared));
}

/// How long a body is in the air rising `riseMm` and coming back down, in
/// MOVEMENT STEPS. t = 2 * sqrt(2h/g), done in integers by squaring both sides:
/// steps = 2 * stepsPerSecond * sqrt(2h/g) = 2 * sqrt(2h * stepsPerSecond^2 / g).
[[nodiscard]] constexpr std::int32_t hopStepsForRise(std::int32_t riseMm) noexcept {
    if (riseMm <= 0) {
        return 0;
    }
    const std::int64_t perSec = kStepsPerSecond;
    const std::int64_t inner = (2LL * riseMm * perSec * perSec) / kGravityMmPerSecSq;
    return static_cast<std::int32_t>(2 * isqrt64(inner));
}

// ---------------------------------------------------------------------------
// what a fall does to a person
// ---------------------------------------------------------------------------
//
// THE CURVE READS AS GRAVITY, NOT AS HIT POINTS, and that is the whole brief.
// What shipped before this was `(bandsOver - safeBands) * 24`: a staircase in
// units of storeys, which meant a fall was free right up to a threshold and then
// cost a flat slab. Nothing about it came from the height.
//
// So the input is a HEIGHT IN MILLIMETRES and the model is the one the body
// actually experiences: you are hurt by the speed you arrive at, and the energy
// that has to go somewhere is the square of it. Injury therefore scales as
// (v - vTolerated)^2, and v is sqrt(2gh) with no thumb on it.
//
// The three heights the shipped district actually contains, against a hundred
// hit points, with nothing learnt and nothing soft underfoot:
//
//     1 storey   2.70 m   7.28 m/s      7 hurt   -- you land it, and you feel it
//     2 storeys  5.40 m  10.29 m/s     50 hurt   -- a serious injury
//     3 storeys  8.10 m  12.61 m/s    109 hurt   -- more than a person has
//
// Which is the brief: survivable with a knock, serious, and usually fatal. The
// build still floors the player at kPlayerBrawlFloor because nothing in it kills
// you yet -- but the NUMBER is now honest about what happened, and the day the
// player has hit points of their own it will kill them without being retuned.

/// Fall a body shrugs off with bent knees and nothing else, millimetres.
/// 1.5 m -- a drop off a loading bank, a jump down from a cart bed.
inline constexpr std::int32_t kFreeFallMm = 1500;

/// What one band of the roofs' teaching is worth, in millimetres of fall a body
/// no longer has to pay for. See sim/player.hpp's safeDropBands: a journeyman
/// skyrunner and a guild that has shown you where to land are worth one each.
///
/// 1,100 mm apiece, so a fully taught skyrunner tolerates a 3.7 m landing --
/// which walks off a single storey for nothing, is badly hurt by two and is
/// still broken by three. A skill that made the deepest drop in the district
/// free would have stopped being a skill; the static_assert in player.hpp says
/// so about the band count and this constant is the same rule in metres.
inline constexpr std::int32_t kTaughtLandingMm = 1100;

/// What water or deep mud under a landing is worth. Real, and large: harbour
/// water is the reason people survive falls off quays. Not infinite -- 8 m into
/// two feet of dock water still hurts.
inline constexpr std::int32_t kSoftLandingMm = 1800;

/// Divides the squared excess speed into hit points. Not physics -- this is the
/// one place a number is chosen rather than derived, and it is chosen so the
/// three heights the district contains land on 7 / 50 / 109 of a hundred. Every
/// other property of the curve (that it starts at nothing, that it steepens,
/// that a taught landing shifts it rather than flattening it) comes out of the
/// shape and not out of this.
inline constexpr std::int32_t kFallInjuryDivisor = 470000;

/// What a landing costs, in hit points, having fallen `fallMm` onto a surface
/// this body tolerates `cushionMm` of fall on.
[[nodiscard]] constexpr std::int32_t fallInjury(std::int32_t fallMm,
                                                std::int32_t cushionMm) noexcept {
    const std::int32_t arriving = impactSpeedMmPerSec(fallMm);
    const std::int32_t tolerated = impactSpeedMmPerSec(cushionMm);
    if (arriving <= tolerated) {
        return 0;
    }
    const std::int64_t excess = arriving - tolerated;
    return static_cast<std::int32_t>((excess * excess) / kFallInjuryDivisor);
}

// ---------------------------------------------------------------------------
// what a person can do
// ---------------------------------------------------------------------------
//
// Every number below is a measured human being, in millimetres and millimetres
// per second, and every one of them is converted rather than typed.

/// A deliberate walk. 1.4 m/s is the pace people cross a room at.
inline constexpr std::int32_t kWalkMmPerSec = 1400;

/// The default gait, and it is a JOG. 5 m/s.
///
/// THIS IS THE BIGGEST SINGLE FEEL CHANGE IN THE SPRINT. What shipped as the
/// default was 2.3 m/s, which is a stroll, and the comment beside it called it
/// "deliberately unhurried" -- a warehouse front was meant to take four seconds
/// to walk past. It took four seconds, and it read as wading. Every first-person
/// game made this decade moves at a jog by default and reserves the walk for a
/// modifier, because a player holding forward is going somewhere.
inline constexpr std::int32_t kJogMmPerSec = 5000;

/// Flat out. 7 m/s is a fit person sprinting and is about as fast as a human
/// body goes on the level.
inline constexpr std::int32_t kSprintMmPerSec = 7000;

/// On your haunches. 1.1 m/s is a measured crouch-walk.
///
/// A NUMBER, NOT A PERCENTAGE, and #77 made it one. It used to be
/// `kWalkSpeed * kCrouchSpeedPercent / 100` -- half of whatever the walk
/// happened to be -- so retuning the walk silently retuned every stealth
/// crossing in the game, and it did: the burglary's own capture stopped landing
/// its beats the moment the walk came down to a real 1.4 m/s, because half of
/// that is 0.7 m/s and the scripted walker ran out of steps crossing a taproom.
/// A crouched person moves at the speed a crouched person moves at.
inline constexpr std::int32_t kCrouchMmPerSec = 1100;

/// A standing vertical jump. HALF A METRE, which is LESS THAN ONE TILE and a
/// very long way less than one storey.
///
/// This is the constant that stops the jump key being a climb key. A band is
/// 2,700 mm; this is 500. Nothing in the district can be jumped onto, and that
/// is correct -- a person cannot jump onto a roof. Getting up is a climb, and a
/// climb is what walking into the wall now does.
inline constexpr std::int32_t kJumpRiseMm = 500;

/// How high a body walks over without noticing. 400 mm is a kerb, a doorstep,
/// the lip of a hatch.
///
/// NOTHING IN THE SHIPPED DISTRICT IS THIS SMALL, and that is worth writing down
/// rather than discovering later: the baked world has no vertical feature
/// shorter than a whole band, because a band is the map's own quantum. So this
/// constant currently describes an empty set. It is here because the rule it
/// states -- anything up to a step-up is walked over, never jumped -- is the
/// rule the geometry will be authored against when sub-band detail exists, and
/// because a reader looking for "why do I have to press a key to get over a
/// kerb" deserves to find the answer rather than the absence of one.
inline constexpr std::int32_t kStepUpMm = 400;

/// How high a body hauls itself onto without a run-up: 1,350 mm, chest height,
/// one hand and a knee. A vault.
///
/// AND A BAND IS TWICE THIS, which is the fact that decides what mantling feels
/// like. You do not vault a 2.7 m wall; you CLIMB it, and it takes a second and
/// both hands. That is why a mantle in this build costs kHaulSteps of standing
/// still with the eye rising rather than resolving instantly -- see
/// PlayerBody::mantle. A move the geometry makes impossible to do quickly should
/// not be quick.
inline constexpr std::int32_t kVaultReachMm = 1350;

// --- the same numbers, in the units the body adds up in ---------------------

/// Q8 tile-widths per movement step. 7 -> 1.48 m/s.
inline constexpr std::int32_t kWalkSpeed = speedQ8PerStep(kWalkMmPerSec);
/// 24 -> 5.06 m/s.
inline constexpr std::int32_t kJogSpeed = speedQ8PerStep(kJogMmPerSec);
/// 33 -> 6.96 m/s.
inline constexpr std::int32_t kSprintSpeed = speedQ8PerStep(kSprintMmPerSec);
/// 6 -> 1.27 m/s.
inline constexpr std::int32_t kCrouchSpeed = speedQ8PerStep(kCrouchMmPerSec);

static_assert(kCrouchSpeed < kWalkSpeed && kWalkSpeed < kJogSpeed &&
                  kJogSpeed < kSprintSpeed,
              "a walk that outruns a sprint is a typo, not a design");
/// BEING UNSEEN COSTS TIME, and this is the assertion that keeps it costing.
/// sim/stealth.hpp's kCrouchSpeedPercent is the ratio the stealth rules are
/// balanced against; the crouch may be slower than that but must never be
/// faster, whatever anybody retunes.
static_assert(kCrouchSpeed * 2 <= kJogSpeed,
              "crouching has to cost at least half your speed or it is free");

/// The jump, in the BAND-RELATIVE Q8 the feet are measured on, and how many
/// movement steps it is in the air. 500 mm is 47/256 of a band; the hang time is
/// gravity's and comes to 38 steps, a shade under two thirds of a second.
inline constexpr std::int32_t kJumpRiseQ8 = bandQ8FromMm(kJumpRiseMm);
inline constexpr std::int32_t kJumpSteps = hopStepsForRise(kJumpRiseMm);
static_assert(kJumpRiseQ8 > 0 && kJumpRiseQ8 < 256 / kTilesPerBand,
              "a standing jump must not clear one tile, let alone one storey");

}  // namespace granadad::sim
