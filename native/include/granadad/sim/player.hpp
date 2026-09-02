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
// EVERY NUMBER IN THIS FILE THAT IS A FACT ABOUT PEOPLE COMES FROM HERE. Walk,
// jog, sprint, jump height, hang time, mantle reach and the fall curve are all
// quoted in millimetres of real human performance in sim/human_scale.hpp and
// converted through one metres-per-tile constant. kStepsPerSecond lives there
// too, because half of every speed conversion is the clock.
#include "granadad/sim/human_scale.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the numbers, all integers, all per movement step
// ---------------------------------------------------------------------------

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

// THE THREE GAITS ARE IN sim/human_scale.hpp -- kWalkSpeed (1.5 m/s),
// kJogSpeed (5.1 m/s, the DEFAULT) and kSprintSpeed (7.0 m/s) -- because they
// are facts about a human body and not facts about this class. What used to be
// here was `kWalkSpeed = 11; kRunSpeed = 18;`: 2.3 m/s by default with a 3.8 m/s
// run, described in its own comment as "deliberately unhurried". It was, and
// that is what Eli meant by archaic. A player holding forward is going
// somewhere.

/// KEYBOARD turn, BAM per step. 425 * 60 = 25500 BAM/s = 140 degrees a second.
///
/// READ THE WORD KEYBOARD. This rate has NOTHING to do with how fast the camera
/// turns in normal play -- mouse look is the aim path, it applies raw relative
/// deltas through MoveInput::yawDelta with no rate limit of any kind, and it
/// must never inherit a number measured off a keyboard.
///
/// THE MISTAKE THIS COMMENT EXISTS TO UNDO. This was 197 BAM (64.9 deg/s),
/// sourced from docs/design/COMBAT-FEEL-REFERENCE.md's measurement of Barony at
/// "60-70 deg/s". That measurement is real, and it is a measurement of a 2015
/// roguelike's KEYBOARD FALLBACK -- the same document says in the same bullet
/// that mouse-look "was not measurable". Letting it set the feel generally made
/// turning round in the Docks take five and a half seconds. The reference doc
/// now says keyboard-only on the line itself so nobody generalises it again.
///
/// 140 deg/s is where modern keyboard turning sits, and arrow-key turning is
/// what it now is: an ACCESSIBILITY FALLBACK for playing without a mouse, not a
/// design statement.
inline constexpr Angle kTurnRate = 425;

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

/// MOVEMENT STEPS A MANTLE COSTS, standing still, with the eye rising.
///
/// A MANTLE IN THIS BUILD IS A CLIMB, and the geometry is why. human_scale.hpp's
/// kVaultReachMm is 1,350 mm -- chest height, one hand and a knee, and instant.
/// A band is 2,700 mm. There is no such thing in this district as a wall you can
/// vault; every one of them is twice a vault, and a 2.7 m wall takes a fit
/// person about a second and both hands.
///
/// So the band change resolves at once -- the simulation is never in a state
/// where the body is half inside a wall -- and the LEGS are locked for this long
/// afterwards while kEyeEaseRate carries the eye up. 32 steps is the ease's own
/// length (256/8) and matching it exactly means the haul ends on the step the
/// view arrives, rather than freeing the player mid-rise or holding them after
/// it. Just over half a second.
///
/// It also stops the automatic version being a lift. Walking into a stack of
/// ledges with a held forward key fires one climb, not four in a tenth of a
/// second.
inline constexpr std::int32_t kHaulSteps = 256 / kEyeEaseRate;

// ---------------------------------------------------------------------------
// #77: the eye's own two verbs, in the axis the eye is measured on
// ---------------------------------------------------------------------------
//
// human_scale.hpp states the drop and the dip in millimetres, the units the
// design is written in; these convert them through the one place a
// millimetre becomes a band-relative Q8, the same way kJumpRiseQ8 does for
// the jump.

/// The crouch's eye drop, band-relative Q8.
inline constexpr std::int32_t kCrouchEyeDropQ8 = bandQ8FromMm(kCrouchEyeDropMm);

/// The dip a landing of `bands` deserves, band-relative Q8. Wraps
/// landingDipMm so the eye and the millimetre the design was written in
/// cannot drift apart.
[[nodiscard]] constexpr std::int32_t landingDipQ8(std::int32_t bands) noexcept {
    return bandQ8FromMm(landingDipMm(bands));
}

/// Movement steps a jump asked for too early is remembered before it is
/// dropped for real. 8 at 60 Hz is a shade over a tenth of a second: long
/// enough to catch a tap that landed a beat before a haul finished or a hop
/// touched down, short enough that a jump can never fire long after the
/// press that asked for it.
inline constexpr std::int32_t kJumpBufferSteps = 8;

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
    /// FATIGUE BUILD: no wind left to haul with. PLAYERBODY NEVER RETURNS
    /// THIS -- the body knows geometry, not stamina; the pool lives with the
    /// room (sim/fatigue.hpp) and it is the CALLER that owns both who refuses
    /// the verb before the body is ever asked. The value lives in this enum
    /// so the refusal rides the same RoofResult/roofRefusal path every other
    /// refused climb already rides.
    Winded = 7,
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
    /// -1 left, 0, +1 right. Keyboard turn, at kTurnRate. ACCESSIBILITY ONLY --
    /// see the constant. Nothing about the game's feel is allowed to depend on
    /// it.
    std::int32_t turn = 0;
    /// Mouse look for this step, in BAM, already scaled by sensitivity. RAW: no
    /// smoothing, no acceleration, no per-step rate cap. What the mouse did is
    /// what the head does.
    Angle yawDelta = 0;
    Angle pitchDelta = 0;
    /// Hold to sprint. kSprintSpeed, 7 m/s.
    bool sprint = false;
    /// Hold to slow to a deliberate kWalkSpeed walk, 1.5 m/s. The DEFAULT gait
    /// with neither modifier held is kJogSpeed -- this is the brake, not the
    /// accelerator, which is the way round every game made this decade has it.
    bool walk = false;
    /// S9. Down on the haunches. kCrouchSpeed, 1.3 m/s, and it beats both other
    /// modifiers -- crouch-sprinting is a thing this game does not have and is
    /// the cheapest way to make crouching free. What being unseen costs in a
    /// first-person game is TIME.
    bool crouch = false;
    /// A jump was asked for THIS STEP. Edge, not level: the client sends it once
    /// per press, so holding the key does not pogo.
    ///
    /// It is a JUMP and not a climb. kJumpRiseMm is half a metre; the shortest
    /// thing in the district is a 2.7 m storey. Nothing can be jumped onto, on
    /// purpose. Getting up is what walking into the wall does -- see
    /// `autoTraverse`.
    bool jump = false;
    /// Whether the body may haul itself onto a ledge it walks into. On by
    /// default because CONTEXTUAL TRAVERSAL IS THE PRIMARY PATH: you get onto
    /// things by trying to go there, not by learning a verb key.
    ///
    /// The flag exists because two callers want it off. A scripted capture
    /// steers by pressing forward into walls to slide along them and must not
    /// start climbing the warehouse it is sliding past, and a case that is
    /// testing collision wants collision.
    bool autoTraverse = true;
    /// Whether the legs skip their own ramp and are simply AT the gait they
    /// were asked for this step.
    ///
    /// FALSE FOR THE PLAYER, ALWAYS. A body has mass; see kAccelSteps.
    ///
    /// TRUE FOR THE CAPTURE SCRIPT, and this flag exists because the first
    /// version of acceleration cost six scripted lines their beats. That walker
    /// (render/session.cpp, stepToward) steers by pressing forward one step at a
    /// time along a compass direction and reading "did the body move" as "is
    /// that way open" -- so momentum carried from the PREVIOUS direction reads
    /// to it as the CURRENT direction being clear, and a walker that believes a
    /// wall is a corridor never arrives. It is a test harness with a stopwatch,
    /// not a person, and it does not get legs.
    ///
    /// Nothing a player can press reaches this.
    bool snapVelocity = false;

    /// Memberwise. What the case-watch tape's twin-run check compares -- see
    /// render::WatchOp. No behavioural weight; two inputs are the same input
    /// exactly when every field agrees.
    [[nodiscard]] bool operator==(const MoveInput&) const = default;
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

    // --- the ordinary jump --------------------------------------------------

    /// A STANDING JUMP. Half a metre up and back down in 38 steps, and it gets
    /// you onto exactly nothing.
    ///
    /// That is not a limitation, it is the point. human_scale.hpp's kJumpRiseMm
    /// is what a person clears; the shortest vertical feature in the baked
    /// district is a whole 2.7 m storey. A jump key that cleared a storey would
    /// be the archaic thing wearing a modern binding. The player keeps walking
    /// and steering while airborne (air control is what makes a jump feel like a
    /// jump rather than a cutscene), the arc is the same integer parabola a leap
    /// uses, and landing on the band you left costs nothing.
    ///
    /// Returns false if the body is already off the ground.
    bool jump() noexcept;

    /// True while a standing jump is still in the air.
    [[nodiscard]] bool jumping() const noexcept { return jumpStepsLeft_ > 0; }

    /// True while the legs are locked hauling the body over a ledge. See
    /// kHaulSteps: a 2.7 m wall is a climb, and a climb takes a second.
    [[nodiscard]] bool hauling() const noexcept { return haulStepsLeft_ > 0; }

    /// The traversal the last step took by itself, cleared by reading it.
    ///
    /// CONTEXTUAL TRAVERSAL IS THE PRIMARY PATH and this is how the rest of the
    /// game hears about it. Walk forward into a ledge you can reach and the body
    /// climbs it: no verb, no key, no prompt. The room still charges the climb,
    /// counts it toward the Skyrunners' regard and bills the fall if the far
    /// side was lower, because it reads the result out of here on the step it
    /// happened -- exactly the same path an explicit press goes down.
    [[nodiscard]] RoofResult takeAutoMove() noexcept;

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
    /// Height of the eye, Q8. feetZ() plus the fixed standing height, LESS
    /// whatever the eye's own two verbs currently owe it: crouching low and
    /// climbing back out of a landing's dip. See kCrouchEyeDropQ8 and
    /// landingDipQ8 -- neither ever touches feetZ_, so a camera that reads
    /// only feetZ() (there is none, but there could be) sees a body that
    /// never left the ground.
    [[nodiscard]] std::int32_t eyeZ() const noexcept {
        return feetZ_ + kEyeHeight - crouchOffsetQ8_ - landingDipOffsetQ8_;
    }
    /// How far the eye is currently ducked below standing height, Q8. 0
    /// upright, kCrouchEyeDropQ8 at the bottom of a full crouch, eased
    /// between. Exposed so a case can assert the EASE happened rather than
    /// only its endpoints.
    [[nodiscard]] std::int32_t crouchOffsetQ8() const noexcept { return crouchOffsetQ8_; }
    /// How far the eye is currently sunk from a landing that has not finished
    /// climbing back out, Q8. 0 between landings.
    [[nodiscard]] std::int32_t landingDipOffsetQ8() const noexcept {
        return landingDipOffsetQ8_;
    }
    /// True while a jump asked for too early -- mid-haul, mid-leap, mid-hop --
    /// is still waiting to fire the moment the body is next eligible. See
    /// kJumpBufferSteps.
    [[nodiscard]] bool jumpBuffered() const noexcept { return jumpBufferStepsLeft_ > 0; }

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

    /// FATIGUE BUILD: AGILITY'S RUNTIME READER, the legs' own multiplier.
    /// Q8 against a neutral 256, applied to whichever gait the step asked for
    /// -- every gait equally, so the walk/jog/sprint ratios (and the stealth
    /// balance asserted against them in human_scale.hpp) are preserved
    /// exactly. Set once at boot from the chargen sheet through
    /// agilitySpeedScaleQ8 (fatigue.hpp): 256 at the base-40 sheet, which is
    /// bit-for-bit the legs every earlier build had, up to 304 at the
    /// ceiling. SIM STATE and in the digest -- a multiplier the hash could
    /// not see would let two runs disagree about where a body is standing.
    void setSpeedScaleQ8(std::int32_t scaleQ8) noexcept {
        speedScaleQ8_ = scaleQ8 < 64 ? 64 : (scaleQ8 > 512 ? 512 : scaleQ8);
    }
    [[nodiscard]] std::int32_t speedScaleQ8() const noexcept { return speedScaleQ8_; }

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
    /// Advances one step of a standing jump, and lands it on the last one.
    void flyJumpStep() noexcept;
    /// Moves one axis of the velocity toward what the legs were asked for.
    /// Speeding up uses `accel`; slowing, stopping and reversing use `brake`.
    [[nodiscard]] static std::int32_t approach(std::int32_t have, std::int32_t want,
                                               std::int32_t accel,
                                               std::int32_t brake) noexcept;
    /// #77: THE CAMERA'S OWN RESPONSE CURVE. Moves `have` a quarter of the
    /// remaining distance to `target` and never gets stuck a unit short of it
    /// -- fast at first and gentler as it settles, the way a spring does and
    /// approach()'s straight ramp deliberately does not. Everything the eye
    /// does on top of the legs (crouchOffsetQ8_, landingDipOffsetQ8_) is this
    /// one curve pointed at a different target.
    [[nodiscard]] static std::int32_t easeToward(std::int32_t have, std::int32_t target) noexcept;
    /// Hauls onto the ledge lying in direction `dir`. mantle() is this with the
    /// facing snapped to the compass; the automatic path is this with the
    /// direction the legs were actually pushing.
    RoofResult mantleToward(const TileStep& dir) noexcept;
    /// Starts the kHaulSteps leg-lock, but only for a climb that happened.
    RoofResult startHaul(const RoofResult& climbed) noexcept;
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

    // --- #77: the jump, the haul and the climb nobody pressed a key for ------
    //
    // Integer and in the digest, for the same reason the leap's fields are: a
    // body caught mid-hop or mid-haul when a fingerprint is taken is a body two
    // runs have to agree about.
    std::int32_t jumpStepsLeft_ = 0;
    std::int32_t jumpStepsTotal_ = 0;
    /// What the legs are actually doing, Q8 per step, per axis.
    ///
    /// SIMULATION STATE, and integer, for exactly the reason the position is:
    /// two runs of the same input have to end up in the same place, and a
    /// velocity that lived in the client would be a second opinion about where
    /// the body is going. In the digest with everything else.
    std::int32_t velX_ = 0;
    std::int32_t velY_ = 0;
    /// Steps of the mantle's haul still to run. Non-zero locks the legs.
    std::int32_t haulStepsLeft_ = 0;
    /// The traversal the body took on its own, waiting to be read by whoever
    /// charges for one. See takeAutoMove.
    RoofResult autoMove_{};

    // --- #77: the eye's own two verbs ---------------------------------------
    //
    // Neither of these is feetZ, x or y -- see eyeZ() -- so nothing here can
    // move a hitbox, break a collision case, or shift where a scripted
    // capture's body ends up. Integer and in the digest anyway, for the same
    // reason the haul's own fields are: a fingerprint taken mid-ease is a
    // fingerprint two runs have to agree about.
    std::int32_t crouchOffsetQ8_ = 0;
    std::int32_t landingDipOffsetQ8_ = 0;
    /// A jump asked for while the body could not take it yet, waiting to
    /// fire. See kJumpBufferSteps.
    std::int32_t jumpBufferStepsLeft_ = 0;
    /// FATIGUE BUILD: see setSpeedScaleQ8. 256 is the legs as shipped.
    std::int32_t speedScaleQ8_ = 256;
};

}  // namespace granadad::sim
