#pragma once

// The player's body: continuous, sub-tile, and inside the simulation.
//
// WHY THIS IS SIM AND NOT CLIENT
//
// docs/design/COMBAT-FEEL-REFERENCE.md section 2, ruled by Eli 2026-07-31:
// movement is continuous Barony-style, and it must NOT be solved by making the
// player's position client-only float state. The moment combat, shoving,
// occupancy or line of sight needs to know precisely where the player is
// standing, the authoritative answer has to be here and not in the renderer.
// So: Q8 sub-tile integers, a BAM facing, and not one float in the file.
//
// THE TWO CLOCKS
//
// The world ticks once a simulated second (TickClock::MILLIS_PER_TICK == 1000,
// Dwarf-Fortress style). A body cannot move at 1 Hz and feel like Barony. So
// the body advances on its own fixed cadence — kStepsPerSecond MOVEMENT STEPS a
// second — and every speed in this file is quoted per step, as an integer, so
// no division happens at runtime and no remainder accumulates. The client runs
// a fixed-timestep accumulator and calls step() a whole number of times; the
// world tick is a separate, slower beat that this file does not touch.
//
// That keeps determinism intact in the way that matters: a session is a
// SEQUENCE OF STEPS, and replaying the same input sequence produces the same
// integers on any machine. Frame rate changes how many steps run per frame; it
// never changes what a step does.
//
// COLLISION
//
// The body is an axis-aligned square of half-extent kBodyRadius, resolved one
// axis at a time so a body sliding along a warehouse front keeps its tangential
// speed instead of sticking. Each axis move is capped at kMaxStepQ8 and
// substepped, so nothing can tunnel through a wall no matter what a future
// sprint does to the speed constants.

#include <cstdint>
#include <string_view>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the numbers, all integers, all per movement step
// ---------------------------------------------------------------------------

/// Movement steps per simulated second.
inline constexpr std::int32_t kStepsPerSecond = 60;

/// Half-extent of the body's collision square, Q8. 90/256 of a tile, so the
/// body is 0.70 of a tile across: it fits a one-tile doorway with room either
/// side and cannot squeeze a diagonal gap between two wall corners.
inline constexpr std::int32_t kBodyRadius = 90;

/// Eye height above the surface the body stands on, in the BAND-RELATIVE Q8 of
/// this file's z axis: 256 here is one whole band, not one tile.
///
/// DERIVED, NEVER TYPED. The height of a person is a fact about people and it
/// is stated once, in tiles, in sim/vertical_scale.hpp. This is that fact
/// converted into the axis feetZ_ happens to be measured on, and the conversion
/// is exact: 435 / 3 == 145, and 145 * 3 == 435 back again.
///
/// IT USED TO BE 205 and that was the whole of Eli's complaint. 205/256 is 0.80
/// of a band, and a band was being drawn one tile tall, so the eye stood 0.72 m
/// off the ground and looked DOWN on the parapet of a two-storey warehouse.
inline constexpr std::int32_t kEyeHeight = kEyeHeightTilesQ8 / kTilesPerBand;
static_assert(kEyeHeight * kTilesPerBand == kEyeHeightTilesQ8,
              "the eye must sit at the same height whichever axis you ask on");

/// Walk and run speed, Q8 per step. 11 -> 660 Q8/s -> 2.58 tiles a second;
/// 18 -> 4.22 tiles a second. Deliberately unhurried: a warehouse front is
/// twelve tiles and should take four seconds to walk past.
inline constexpr std::int32_t kWalkSpeed = 11;
inline constexpr std::int32_t kRunSpeed = 18;

/// Keyboard turn, BAM per step. 197 * 60 = 11820 BAM/s = 64.9 degrees a second.
///
/// Measured Barony is 60-70 deg/s and reads weighty; the Java build's 165 deg/s
/// read as twitchy (COMBAT-FEEL-REFERENCE.md section 2). This is the former.
inline constexpr Angle kTurnRate = 197;

/// How far the eye climbs or falls toward the surface of a new band per step,
/// band-relative Q8.
///
/// HALVED WHEN THE STOREY GREW. This used to be 16, which crossed a band in 16
/// steps — a quarter of a second, and fine when a band was one tile. A band is
/// three tiles now (vertical_scale.hpp), so the old rate would lift the eye
/// ELEVEN TILES A SECOND, four times as fast as the body walks. Every ramp on
/// Saltgate Rise would have read as a lift shaft.
///
/// At 8 a band takes 32 steps, just over half a second: 5.6 tiles a second, a
/// brisk flight of stairs. There is no getting away from a band change being a
/// whole storey — the map authors one stair cell per storey and the walking
/// rule steps one band at a time — so the honest thing is to make it look like
/// a climb rather than to pretend it is a kerb.
inline constexpr std::int32_t kEyeEaseRate = 8;

/// Longest single-axis displacement resolved without substepping, Q8. Anything
/// larger is split, so tunnelling is impossible by construction rather than by
/// the speed constants happening to be small.
inline constexpr std::int32_t kMaxStepQ8 = 64;

/// Pitch is clamped just short of straight up and straight down. Exactly
/// vertical is legal geometry and a degenerate camera basis, and nothing in the
/// game needs it.
inline constexpr Angle kMaxPitch = kTurnQuarter - 512;

// ---------------------------------------------------------------------------
// S5: the roof moves
// ---------------------------------------------------------------------------
//
// THE SKYRUNNERS ARE NAMED FOR THIS. DOCKS-GAZETTEER section 2.5 rules that
// rooftops are unseemly for every Trojian except a presented Wielder, "which is
// WHY the poor live there and WHY burglars/assassins ('Skyrunners') use the
// rooftop-slum layer as their highway". Section 2.6 then files the roof planes'
// same-z isolation as deliberate "until the law/economy layers learn to climb
// (S5+)".
//
// Three verbs, and between them they turn 8,132 standable-but-unreachable cells
// of the baked district into a road:
//
//   MANTLE   haul yourself one band onto the top of the wall you are facing.
//   LEAP     line a gap up and cross it, in the air, landing at your own band
//            or up to two below.
//   DROP     step off a ledge and fall to the first floor under you.
//
// All three are integer, all three go through the same collision the walking
// step does, and none of them rolls a die: what a skill buys is REACH and a
// SAFE HEIGHT, never a chance.

// WHAT A BAND IS WORTH IN METRES, restated here because these three constants
// are counted in BANDS and a band stopped being a kerb.
//
// vertical_scale.hpp makes a band three tiles, and a tile about 0.9 m, so:
//
//     1 band  =  ~2.7 m  — off a shed roof, land on your feet
//     2 bands =  ~5.5 m  — off a first-floor window, land badly
//     3 bands =  ~8.2 m  — off the Gull's roof into the alley
//
// None of the GEOMETRY below moved when the storey grew: the map is the same
// map, the fill in test_roofrun.cpp counts the same cells, and the pinned
// reachability numbers in docks.hpp are untouched on purpose. What moved is
// what a fall COSTS, because the same three bands now describe a fall three
// times as long.

/// Levels a body may fall in one drop before there is simply nothing under it.
/// Three, and deliberately unchanged: it is the reach of the landingBand search
/// that decides which roofs join up, and every reachability count in docks.hpp
/// is derived from it.
inline constexpr std::int32_t kMaxDropBands = 3;

/// Levels a body may fall without being hurt, before any guild has shown it
/// where to put its feet. One, and it is no longer free money: one band is
/// 2.7 m, which is a drop you take on your feet with your knees bent and not a
/// step off a kerb.
inline constexpr std::int32_t kSafeDropBands = 1;

/// The most bands ANY body may fall unhurt, however good it is on the roofs.
///
/// NEW WHEN THE STOREY GREW, and it is the correction that matters most. The
/// old cap was kMaxDropBands, so a skyrunner at level 10 whom the roofs had
/// taught walked away from a three-band drop for nothing — 8.2 m, which is a
/// fall that breaks people. Two bands (5.5 m) is the ceiling now: the deepest
/// drop the geometry allows always hurts somebody, which is the difference
/// between a skill and a cheat.
inline constexpr std::int32_t kMaxSafeDropBands = 2;
static_assert(kMaxSafeDropBands < kMaxDropBands,
              "the deepest fall in the district must never be free");

/// How far a running leap carries, in tiles, before any guild teaching. Three
/// is measured, not guessed: the alley between the Gilded Gull's roof and its
/// neighbour's is two tiles of air, and a body has to land on the far side of
/// it.
inline constexpr std::int32_t kLeapReachTiles = 3;

/// Movement steps a leap spends in the air per tile crossed. Eight at sixty
/// steps a second is an eighth of a second a tile — fast enough to read as a
/// jump, slow enough to see the street go past underneath.
inline constexpr std::int32_t kLeapStepsPerTile = 8;

/// How high the arc of a leap lifts the feet at its top, band-relative Q8.
/// Cosmetic in the sense that nothing collides against it, simulation state in
/// the sense that it is integer and it is in the digest.
///
/// LEFT ALONE WHEN THE STOREY GREW, and that is a decision rather than an
/// oversight: 56/256 of a band was a 0.20 m hop when a band was one tile, and
/// the same number is a 0.60 m lift now, which is what a running jump off a
/// roof actually looks like. The scale change fixed this constant for free.
inline constexpr std::int32_t kLeapArcQ8 = 56;

/// What a roof move did, or why it did not.
enum class RoofMove : std::uint8_t {
    /// It happened.
    Done = 0,
    /// Nothing in front of you to grip: no wall face, or its top is not a
    /// surface.
    NoLedge = 1,
    /// A ceiling over your own head. You cannot stand up into a floor slab.
    NoHeadroom = 2,
    /// The far side is walkable — that is a step, not a leap.
    NoGap = 3,
    /// Air all the way to the end of your reach, or a wall in the middle of it.
    NoLanding = 4,
    /// Already in the air.
    Airborne = 5,
    /// The body would not fit where it came down.
    Blocked = 6,
};

/// The DIAGNOSTIC name of a move: "no ledge", "no gap". Logs and cases only.
[[nodiscard]] std::string_view roofMoveName(RoofMove move) noexcept;

/// WHY THE BODY DID NOT GO, as a whole sentence. The client used to weld the
/// diagnostic name onto a prefix -- "NO WAY UP - NO LEDGE", "NOTHING TO DROP TO
/// - NO LANDING" -- which reads as a fault code twice over and, in the second
/// case, says the same thing twice. Each of these answers the press on its own.
[[nodiscard]] std::string_view roofRefusal(RoofMove move) noexcept;

/// What a roof move cost.
struct RoofResult {
    RoofMove move = RoofMove::NoLedge;
    /// Bands risen (a mantle) or fallen (a drop, or a leap that came down
    /// lower than it left). Never negative.
    std::int32_t bands = 0;
    /// Tiles crossed. Only a leap moves more than one.
    std::int32_t tiles = 0;

    [[nodiscard]] bool ok() const noexcept { return move == RoofMove::Done; }
};

/// Bands a body may fall unhurt with this much SKYRUNNING behind it and,
/// separately, with the roofs' own teaching. Integer, monotonic, and capped:
/// nobody ever walks off a three-storey roof for free.
[[nodiscard]] std::int32_t safeDropBands(std::int32_t skyrunningLevel,
                                         bool taughtByTheRoofs) noexcept;

/// How far a leap carries with this much SKYRUNNING and the roofs' teaching.
[[nodiscard]] std::int32_t leapReachTiles(std::int32_t skyrunningLevel,
                                          bool taughtByTheRoofs) noexcept;

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------

/// One step's worth of intent. Everything here is an integer because the whole
/// point is that a recorded session replays to the same bits.
struct MoveInput {
    /// -1 back, 0, +1 forward.
    std::int32_t forward = 0;
    /// -1 left, 0, +1 right. Strafe, not turn.
    std::int32_t strafe = 0;
    /// -1 left, 0, +1 right. Keyboard turn, at kTurnRate.
    std::int32_t turn = 0;
    /// Mouse look for this step, in BAM, already scaled by sensitivity.
    Angle yawDelta = 0;
    Angle pitchDelta = 0;
    /// Hold to run.
    bool run = false;
    /// S9. Down on the haunches. Halves the walk (kCrouchSpeedPercent, in
    /// sim/stealth.hpp, which is where every stealth number lives) and refuses
    /// to be a run at the same time -- crouch-running is a thing this game does
    /// not have and is the cheapest way to make crouching free.
    bool crouch = false;
};

// ---------------------------------------------------------------------------
// the body
// ---------------------------------------------------------------------------

class PlayerBody {
public:
    /// Places a body at the centre of a tile. Throws nothing: an unstandable
    /// tile is the caller's problem to check with TileQuery::standable, and
    /// spawnedLegally() reports it.
    PlayerBody(const TileQuery& tiles, std::int32_t tileX, std::int32_t tileY, std::int32_t band,
               Angle yaw) noexcept;

    /// One movement step. See the header comment on the two clocks.
    void step(const MoveInput& input) noexcept;

    // --- S5: the roof moves -------------------------------------------------

    /// Hauls the body one band onto the ledge it is FACING. The facing is
    /// snapped to the four-point compass first (angle.hpp, facing_step) -- you
    /// line a climb up before you take it.
    RoofResult mantle() noexcept;

    /// Steps off the ledge in front and falls to the first floor under it.
    /// Refuses when the tile ahead is walkable, because that is a step.
    RoofResult dropOff() noexcept;

    /// Lines up the gap in front and crosses it. `reachTiles` is how far this
    /// body can carry -- see leapReachTiles(); the guild's teaching is a
    /// caller's fact, not the body's.
    ///
    /// The body is AIRBORNE for kLeapStepsPerTile steps per tile afterwards:
    /// step() flies it along the arc and lands it, so a leap is something the
    /// player watches happen rather than a teleport with a sound effect.
    RoofResult leap(std::int32_t reachTiles) noexcept;

    /// True while a leap is still in the air.
    [[nodiscard]] bool airborne() const noexcept { return leapStepsLeft_ > 0; }

    /// Bands the last landing fell through, cleared by reading it. This is how
    /// whoever owns the player's hit points learns that the roof hurt.
    [[nodiscard]] std::int32_t takeFallBands() noexcept;

    /// The lowest band a roof move will ever put this body on.
    ///
    /// THIS EXISTS BECAUSE THE ROOF MOVES FOUND A TRAPDOOR. Below the Docks'
    /// harbour surface the map is DUNGEON: the smuggler undercellars, the
    /// sewer outfall and the drowned structure of DOCKS-GAZETTEER section 2.1,
    /// none of which this build simulates. The harbour bed at world z16 is
    /// 4,066 cells of FLOOR whose own FLUID lane is clear -- the water stands
    /// in the two cells above it -- so the walkability rule calls it standable,
    /// and 313 of the columns over it are dry all the way down. A body that
    /// stepped off the mudflats into one of those shafts landed on the seabed
    /// and COULD NOT GET BACK UP: z17 has not one standable cell to mantle
    /// onto. A trapdoor into an unfinished level is not a feature.
    ///
    /// So the caller says where the world's floor is, out loud. The Docks says
    /// docks::kHarbourSurfaceBand, and the day the dungeon is built it says
    /// something lower on purpose rather than by accident.
    void setLandingFloor(std::int32_t band) noexcept { landingFloor_ = band; }
    [[nodiscard]] std::int32_t landingFloor() const noexcept { return landingFloor_; }

    /// Displaces the body by a Q8 impulse it did not ask for -- a shove.
    ///
    /// Goes through exactly the same axis-separated, substepped, collision-
    /// checked path a walking step does, so being put out of a tavern door
    /// cannot push a body through a wall, off a quay or up a level. This is the
    /// bouncer's whole job, expressed in one call.
    void push(std::int32_t dxQ8, std::int32_t dyQ8) noexcept;

    // --- where it is -------------------------------------------------------

    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    /// The z-level whose surface the feet are on.
    [[nodiscard]] std::int32_t band() const noexcept { return band_; }
    /// Height of the feet, Q8, eased across a band change.
    [[nodiscard]] std::int32_t feetZ() const noexcept { return feetZ_; }
    /// Height of the eye, Q8.
    [[nodiscard]] std::int32_t eyeZ() const noexcept { return feetZ_ + kEyeHeight; }

    [[nodiscard]] std::int32_t tileX() const noexcept { return q8_tile(x_); }
    [[nodiscard]] std::int32_t tileY() const noexcept { return q8_tile(y_); }

    [[nodiscard]] Angle yaw() const noexcept { return yaw_; }
    [[nodiscard]] Angle pitch() const noexcept { return pitch_; }

    /// Whether the tile the body was placed on was actually standable. A false
    /// here means the caller spawned it inside geometry.
    [[nodiscard]] bool spawnedLegally() const noexcept { return spawnedLegally_; }

    /// Steps taken since construction. Part of the body's hashable state and
    /// the thing a replay counts.
    [[nodiscard]] std::int64_t stepCount() const noexcept { return steps_; }

    // --- test/setup seams --------------------------------------------------

    /// Puts the body down at the centre of a tile, with no walk, no collision
    /// slide and no fall to charge.
    ///
    /// S6 ADDS THIS FOR ONE CALLER and it is worth naming: the Watch takes you
    /// off the taproom floor and turns you loose on the Tarwalk in the morning,
    /// and nothing between those two facts is simulated. It is the same honest
    /// jump sleeping in a rented bed already makes. It is NOT a teleport verb
    /// for the player -- nothing the player can press reaches it.
    void placeAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept;

    void setYaw(Angle yaw) noexcept { yaw_ = yaw & (kTurnFull - 1); }
    void setPitch(Angle pitch) noexcept;

    /// A 64-bit digest of everything above. Two runs that agree here agree
    /// about the body; the twin-run gate and any future save can compare it
    /// without reaching into fields one at a time.
    [[nodiscard]] std::uint64_t digest() const noexcept;

private:
    /// Whether the body's square, centred at (cx, cy) in Q8, fits in the world
    /// at `band` without overlapping anything it cannot stand on.
    [[nodiscard]] bool bodyFits(std::int32_t cx, std::int32_t cy, std::int32_t band) const noexcept;

    /// Moves along one axis by `delta` Q8, stopping at the first refusal.
    /// Returns the band the body ended on.
    void moveAxis(std::int32_t deltaX, std::int32_t deltaY) noexcept;

    /// Eases the feet toward the band's own surface. One rule, three callers.
    void settleFeet() noexcept;
    /// Advances one step of a leap already in the air.
    void flyLeapStep() noexcept;
    /// Commits the body to an arc between here and a validated landing.
    RoofResult armLeap(std::int32_t tileX, std::int32_t tileY, std::int32_t toBand,
                       std::int32_t tiles) noexcept;
    /// The lowest band this body may come down on right now: the deeper of its
    /// own falling limit and the world's floor.
    [[nodiscard]] std::int32_t deepestLanding() const noexcept;
    /// Puts the body down on a tile at a band, reporting how far it fell.
    RoofResult land(std::int32_t tileX, std::int32_t tileY, std::int32_t fromBand,
                    std::int32_t toBand, std::int32_t tiles) noexcept;

    const TileQuery* tiles_;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t band_ = 0;
    std::int32_t feetZ_ = 0;
    Angle yaw_ = 0;
    Angle pitch_ = 0;
    std::int64_t steps_ = 0;
    bool spawnedLegally_ = false;

    // --- S5: a leap in progress ---------------------------------------------
    //
    // All integer, all in the digest: a body caught mid-air when a save or a
    // gate fingerprint is taken is a body two runs have to agree about.
    std::int32_t leapStepsLeft_ = 0;
    std::int32_t leapStepsTotal_ = 0;
    std::int32_t leapFromX_ = 0;
    std::int32_t leapFromY_ = 0;
    std::int32_t leapToX_ = 0;
    std::int32_t leapToY_ = 0;
    std::int32_t leapFromBand_ = 0;
    std::int32_t leapToBand_ = 0;
    /// Bands the last landing fell through, waiting to be read.
    std::int32_t fallBands_ = 0;
    /// See setLandingFloor. INT32_MIN is "no floor at all", which is what a
    /// synthetic test world wants and what the shipped district must not have.
    std::int32_t landingFloor_ = INT32_MIN;
};

}  // namespace granadad::sim
