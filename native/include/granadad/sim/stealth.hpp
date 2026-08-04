#pragma once

// Being unseen: light, sound, sight line, and the one rule that weighs them.
//
// WHY THIS EXISTS AND WHY IT IS SIM AND NOT RENDER
//
// render/lighting.hpp opens with "there is no stealth system", and it was
// right: light lived entirely on the far side of the line, in floats, computed
// once at load, and read by nothing that could change the world. Meanwhile the
// only thing in this build that decided whether anybody SAW you was
// Tavern::witnessCount -- eight tiles, same floor, a sight line, and nothing
// else. Standing in the middle of a lit taproom at noon and crouching in the
// dark of the snug at four in the morning were, to the simulation, the same
// act.
//
// So the light the ward is actually showing you moves across the line. Not the
// renderer's field -- that stays float, stays presentation, and is untouched --
// but a SECOND, INTEGER field over the same authored sources, in the same
// shape, owned by the simulation and hashed with it. Two fields over one set of
// lamps is a real cost and it is the honest one: the alternative is either
// floats in state or a renderer the simulation reads, and this project has a
// ruling about both.
//
// THE FOUR THINGS THAT MATTER, and every one of them has a case that goes red
// when it is deleted:
//
//   LIGHT     what is falling on the tile you are standing on. A lamp pool
//             gives you away; the hearth going out at three in the morning is
//             the best cover in the room.
//   SOUND     what you are doing to the air. Standing still is silent, walking
//             is not, running is loud, and a lock being probed is louder than
//             either. Against it: the ROOM'S own noise, which is a number the
//             tavern has been simulating since S2 and nothing has ever read.
//             A full house covers a hand in a purse.
//   SIGHT     distance, the floor you are on, whether masonry is in the way --
//             the three clauses S3 already had -- plus whether they are
//             actually LOOKING at you, which the eight-point facing every actor
//             has been hashing since S2 can finally answer.
//   SKILL     SKYRUNNING, whose own row in content/raws/skills/skills.json
//             says it covers "sneak, pickpocket, takedown, jumps, climbs,
//             rooftop runs". The Morrowind steer applied: you get better at not
//             being seen by not being seen.
//
// NO FLOATS. NO ROLLS. The notice rule is a comparison between two integers,
// deterministic in both directions, for the same reason the pickpocket check
// has always been one: a draw taken inside a const query would be a hole in
// the twin-run gate.

#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// light, in integers
// ---------------------------------------------------------------------------

/// The scale everything on this page is quoted in. 0 is pitch dark and 100 is
/// standing in the flame.
inline constexpr std::int32_t kLightMax = 100;

/// One light the SIMULATION knows about. `luminance` is the authored 0..31 of
/// content/maps/baked/docks_surface.lamps.json -- the same number the renderer
/// reads, so a lamp cannot be bright to the eye and dark to the law.
struct SimLight {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    std::int32_t luminance = 0;
};

/// Radius of a light of this luminance, in SIXTEENTHS of a tile.
///
/// The renderer's own curve, in integers: radius 4 + (lum-8)/12 tiles clamped
/// to 3.5..5.5. 56 and 88 sixteenths are those two bounds exactly.
[[nodiscard]] std::int32_t lightRadiusQ4(std::int32_t luminance) noexcept;

/// Peak brightness of a light of this luminance, on the 0..kLightMax scale.
/// The renderer's 0.55 + 0.45*lum/26, clamped.
[[nodiscard]] std::int32_t lightPeak(std::int32_t luminance) noexcept;

/// What one light puts on one tile. Zero outside its radius, on another band,
/// or for a luminance of zero.
///
/// Falloff is the renderer's P*(1-(d/R)^2)^2 evaluated in integers. It does NOT
/// ask the tiles whether masonry is in the way: a lamp on the far side of a
/// wall is a real defect and it is a SMALL one, because every light this build
/// actually uses is either inside the room with you or outside it on a band you
/// are not on. Marked rather than hidden -- see the note on ambientLight.
///
/// VERIFICATION GAP (S9): light does not cast shadows. A body standing behind
/// the bar counter is lit by the lantern over it as though the granite were
/// glass. The sight rule DOES respect masonry (TileQuery::lineOfSight, already
/// used by witnessCount since S3), so what leaks is the brightness term and not
/// the seeing term.
[[nodiscard]] std::int32_t glowFrom(const SimLight& light, std::int32_t x, std::int32_t y,
                                    std::int32_t band) noexcept;

/// Every light in the list, combined by saturating union -- the renderer's own
/// rule, so two lanterns over one table do not add to 200.
[[nodiscard]] std::int32_t glowAt(const std::vector<SimLight>& lights, std::int32_t x,
                                  std::int32_t y, std::int32_t band) noexcept;

/// What the SKY is worth at this second of the day, on the 0..kLightMax scale.
///
/// Committed dark, per the visual target: noon here is overcast harbour light
/// and midnight is near black. A triangle rather than a sine -- integer, exact,
/// and nobody can see the difference between the two through a 4x6 font.
[[nodiscard]] std::int32_t ambientLight(std::int32_t secondOfDay) noexcept;

/// How much of the sky reaches a tile INSIDE a building, as a percentage. The
/// Gilded Gull has small windows and a lead roof; a quarter of the daylight is
/// the honest answer and it is stated once, here, rather than by whoever
/// happens to be asking.
inline constexpr std::int32_t kIndoorSkyPercent = 25;

/// Sky plus every flame, saturating. This is THE illumination query.
[[nodiscard]] std::int32_t illuminationAt(const std::vector<SimLight>& lights, std::int32_t x,
                                          std::int32_t y, std::int32_t band,
                                          std::int32_t secondOfDay, bool indoors) noexcept;

// ---------------------------------------------------------------------------
// sound, and how the body is carrying itself
// ---------------------------------------------------------------------------

/// How the body is standing. Crouching is the whole of the stealth verb: it
/// halves your speed and it is worth more than twenty levels of skill.
enum class Stance : std::uint8_t {
    Upright = 0,
    Crouched = 1,
};

[[nodiscard]] std::string_view stanceName(Stance stance) noexcept;

/// Noise, on the same 0..100 scale the tavern already quotes its room noise in.
inline constexpr std::int32_t kNoiseMax = 100;

/// What MOVING costs you, per stance. Standing still is silent in both.
inline constexpr std::int32_t kNoiseWalking = 30;
inline constexpr std::int32_t kNoiseRunning = 55;
inline constexpr std::int32_t kNoiseCrouchWalking = 9;

/// What crouching does to the body's speed, as a percentage of the walk. Half:
/// a player who wants to be unseen pays for it in the one currency a
/// first-person game has, which is time.
inline constexpr std::int32_t kCrouchSpeedPercent = 50;

/// Movement steps an ACT'S noise takes to fade to nothing. Two seconds at
/// kStepsPerSecond: long enough that a probe and the next probe overlap, short
/// enough that a player who stops and waits is quiet again.
inline constexpr std::int32_t kNoiseFadeSteps = 120;

/// What the body is doing to the air right now, and what it is doing to the
/// light. All integer, all hashed, and it belongs to the simulation because
/// every one of these decides whether a crime was witnessed.
class StealthState {
public:
    [[nodiscard]] Stance stance() const noexcept { return stance_; }
    void setStance(Stance stance) noexcept { stance_ = stance; }
    void toggleStance() noexcept {
        stance_ = stance_ == Stance::Upright ? Stance::Crouched : Stance::Upright;
    }

    /// Told once a movement step by whoever owns the body.
    void setMotion(bool moving, bool running) noexcept;

    /// An ACT made a noise: a pick in a lock, a box forced, a window put in.
    /// Louder than anything already fading takes over; nothing ever stacks.
    void makeNoise(std::int32_t amount) noexcept;

    /// One movement step of forgetting.
    void step() noexcept;

    /// The loudest thing the body is doing: how it is moving, or what it just
    /// did, whichever carries further.
    [[nodiscard]] std::int32_t noise() const noexcept;
    /// The act half alone, before it has faded. For a HUD that wants to say
    /// "that was loud" rather than "you are walking".
    [[nodiscard]] std::int32_t actNoise() const noexcept;

    void hashInto(HashSink& sink) const;

private:
    Stance stance_ = Stance::Upright;
    bool moving_ = false;
    bool running_ = false;
    std::int32_t act_ = 0;
    std::int32_t actSteps_ = 0;
};

// ---------------------------------------------------------------------------
// the notice rule
// ---------------------------------------------------------------------------

/// What one observer can make of the player, and what the player is doing
/// about it. `seen` is the whole answer; the two halves are exposed because a
/// number a player cannot see the working of is a number they cannot play
/// against.
struct Notice {
    /// How well this observer reads the body. Distance, light, facing, alert.
    std::int32_t read = 0;
    /// What the body is doing about it. Stance, skill, the room's own din.
    std::int32_t cover = 0;
    /// What the body is doing to give itself away.
    std::int32_t noise = 0;
    /// read + noise > cover.
    bool seen = false;
};

/// Everything the rule weighs, gathered by whoever owns the room.
struct NoticeInput {
    /// Manhattan distance, Q8, exactly as the room already measures it.
    std::int32_t distanceQ8 = 0;
    /// True when nothing solid is between them. The caller asks the tiles.
    bool lineOfSight = true;
    /// The observer's facing, and the bearing from them to the body.
    Angle observerFacing = 0;
    Angle bearingToBody = 0;
    /// 0..kLightMax on the body's own tile.
    std::int32_t light = 0;
    /// 0..kNoiseMax the body is making.
    std::int32_t noise = 0;
    /// 0..kNoiseMax the ROOM is making, which is cover.
    std::int32_t roomNoise = 0;
    /// The body's SKYRUNNING.
    std::int32_t sneakLevel = 0;
    Stance stance = Stance::Upright;
    /// True for somebody whose job is looking: a bouncer on the floor, a
    /// watchman on a stool, anybody already closing on you.
    bool alert = false;
    /// True for somebody who is not going to notice anything: on the floor,
    /// or a rat.
    bool oblivious = false;
};

// --- the numbers ------------------------------------------------------------
//
// Every one of them is on this page and nowhere else, so the rule can be read
// in one sitting and mutated in one place.

/// What an observer reads off a body standing on top of them in the dark.
inline constexpr std::int32_t kNoticeBase = 60;
/// And what a tile of distance takes off it. Eight tiles -- the range S3 fixed
/// and every witness case since has been written against -- costs 56 of the 60.
inline constexpr std::int32_t kNoticePerTile = 7;
/// The most that being lit can add.
inline constexpr std::int32_t kNoticeLightWeight = 40;
/// Added when the body is inside the observer's forward arc.
inline constexpr std::int32_t kNoticeFacing = 12;
/// And how wide that arc is, either side of their nose. A quarter turn each
/// way: a person notices things in front of them, not behind their ears.
inline constexpr Angle kNoticeArc = kTurnQuarter;
/// Added for somebody whose job is watching the room.
///
/// DELIBERATELY SMALLER THAN THE LIGHT TERM. A bouncer on the floor is paying
/// attention; he is not carrying a lantern. Set high enough that an alert man
/// sees through a dark room, this number would make every clause below it
/// decorative -- a crouched, silent, skilled body eight tiles away in a house
/// with its lamps out would still be made out, and there would be no game in
/// any of it.
inline constexpr std::int32_t kNoticeAlert = 15;
/// Taken off when masonry is in the way but they are still within earshot.
/// NOT a refusal: you can be HEARD through a wall, which is why a lock probed
/// in a shut room is still a risk.
inline constexpr std::int32_t kNoticeBlindPenalty = 45;

/// What a body gets for nothing at all.
inline constexpr std::int32_t kCoverBase = 20;
/// And for going down on its haunches. The single biggest thing on this page
/// that a player can DO, which is the point: crouching is the stealth verb and
/// it costs half your speed.
inline constexpr std::int32_t kCoverCrouch = 26;
/// Per level of SKYRUNNING, and the ceiling on it. A master of the roofs is
/// worth more than crouching and less than crouching in the dark.
inline constexpr std::int32_t kCoverPerSneakLevel = 1;
inline constexpr std::int32_t kCoverSneakCap = 30;
/// What a full house is worth: room noise of 100 buys this much cover.
inline constexpr std::int32_t kCoverRoomNoiseWeight = 20;
/// How much of the body's own noise reaches the rule. Halved, because noise
/// tells somebody that SOMETHING happened and light tells them it was you.
inline constexpr std::int32_t kNoiseHalvedPercent = 50;

/// THE RULE. One function, no state, no draw, and every clause of it is a
/// constant on this page.
[[nodiscard]] Notice noticeOf(const NoticeInput& input) noexcept;

/// The bearing from (fromX, fromY) to (toX, toY) as a BAM, on the compass
/// angle.hpp fixes: 0 is north, which is -Y.
///
/// Integer, table-free and exact to the eighth of a turn, which is all the
/// forward-arc test needs: the four axis cases and the four diagonals, chosen
/// by sign and by which component is larger. Nothing here calls atan2 and
/// nothing here is a float.
[[nodiscard]] Angle bearingTo(std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
                              std::int32_t toY) noexcept;

/// Whether `bearing` falls inside `arc` either side of `facing`. Wrap-safe.
[[nodiscard]] bool withinArc(Angle facing, Angle bearing, Angle arc) noexcept;

}  // namespace granadad::sim
