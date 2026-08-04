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

#include "granadad/sim/angle.hpp"
#include "granadad/sim/tile_query.hpp"

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

/// Eye height above the surface the body stands on, Q8.
inline constexpr std::int32_t kEyeHeight = 205;

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
/// Q8. A band change is a whole tile of height; at 16 per step that is a
/// sixteenth of a second of smoothing — enough that a kerb does not snap, not
/// enough to feel like an elevator.
inline constexpr std::int32_t kEyeEaseRate = 16;

/// Longest single-axis displacement resolved without substepping, Q8. Anything
/// larger is split, so tunnelling is impossible by construction rather than by
/// the speed constants happening to be small.
inline constexpr std::int32_t kMaxStepQ8 = 64;

/// Pitch is clamped just short of straight up and straight down. Exactly
/// vertical is legal geometry and a degenerate camera basis, and nothing in the
/// game needs it.
inline constexpr Angle kMaxPitch = kTurnQuarter - 512;

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

    const TileQuery* tiles_;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t band_ = 0;
    std::int32_t feetZ_ = 0;
    Angle yaw_ = 0;
    Angle pitch_ = 0;
    std::int64_t steps_ = 0;
    bool spawnedLegally_ = false;
};

}  // namespace granadad::sim
