#pragma once

// An actor: somebody with a name, a job, a place to be at this hour, and a
// position the rest of the simulation can measure against.
//
// "actors", not souls, not entities. The word is the word everywhere.
//
// THE TWO CLOCKS, AGAIN
//
// The world ticks once a simulated second and an actor DECIDES on that beat:
// where the schedule says it should be, whether to serve a drink, whether to
// warn somebody. It MOVES on the 60 Hz movement-step clock the player's body
// uses, at an integer Q8 speed.
//
// That is a deliberate reading of the 2026-07-31 ruling, which says NPCs stay
// tile-stepped and are interpolated at draw time. They ARE tile-stepped: an
// actor picks whole tiles, one at a time, out of a breadth-first search, and
// never does sub-tile collision. What is different is WHERE the position
// between two tiles lives -- in the simulation as a Q8 integer, not
// reconstructed by the renderer from a pair of tiles and a frame time.
//
// It lives here because of the brawl. A bouncer's reach is kMeleeReach, a
// hundred and sixty units of two hundred and fifty-six, and the question "is he
// close enough to shove the player out of the door" has to be answered by the
// same arithmetic that answers "where is the player", or the renderer becomes
// the authority on who got hit. Interpolating in the renderer would put that
// number on the wrong side of the line the whole project is built around.
//
// NO FLOATS. Nothing in this file or its implementation may become one.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// Seconds in a day, so an hour of the clock is a number and not a conversion.
inline constexpr std::int32_t kSecondsPerDay = 86400;

[[nodiscard]] constexpr std::int32_t hourOfDay(std::int32_t hour, std::int32_t minute = 0) noexcept {
    return hour * 3600 + minute * 60;
}

// ---------------------------------------------------------------------------
// what an actor is
// ---------------------------------------------------------------------------

/// The roles S2 needs. Every one of them is a job somebody does in a room, not
/// a label: each has a post, hours, and something it does when the player walks
/// up to it.
enum class ActorRole : std::uint8_t {
    /// Drinks, talks, occupies a stool, and is the reason the room is loud.
    Patron = 0,
    /// Pours. Has stock, takes coin, runs out.
    Bartender = 1,
    /// Keeps the stair and the rooms above it. Rents one; the player sleeps.
    Innkeeper = 2,
    /// Keeps the peace, in that order: watch, warn, put out of the door.
    Bouncer = 3,
    /// Divine Light. Teaches, and crafts spells out of the authored raws.
    PriestOfTheFlame = 4,
    /// Present, and guarded. The S5 questline's way in.
    SkyrunnerContact = 5,
    /// S6. Not somebody. A rat on the skirting after the doors shut, which is
    /// the ward's own source of the one contraband the ward pays a bounty ON.
    ///
    /// It is an Actor and not a new kind of thing on purpose: it has a place to
    /// be at an hour, it moves on the movement clock, it can be hit and it can
    /// be on the floor -- every one of which the actor already does. What it is
    /// NOT is a person: it does not witness, it cannot be talked to, it is not
    /// counted in the room's noise, and hitting it is not an offence against
    /// the house. Every one of those exclusions is stated at its own call site.
    Vermin = 6,
};

[[nodiscard]] std::string_view actorRoleName(ActorRole role) noexcept;

/// What an actor is doing this second.
enum class Activity : std::uint8_t {
    /// Not in the room. Off shift, at home, asleep, at sea.
    Away = 0,
    /// On its way to where the schedule says it should be.
    Walking = 1,
    /// At its post, doing the job.
    Working = 2,
    /// At a table or a stool with something in front of it.
    Drinking = 3,
    /// A bouncer on the floor with nothing to do about it yet.
    Watching = 4,
    /// Closing on a troublemaker to say it once.
    Warning = 5,
    /// Hands on, shoving toward the door.
    Ejecting = 6,
    /// Swinging.
    Brawling = 7,
    /// On the floor. A brawl's natural end, and not a death.
    Downed = 8,
};

[[nodiscard]] std::string_view activityName(Activity activity) noexcept;

/// One block of somebody's day: be HERE, doing THIS, between these two seconds.
///
/// A block may wrap midnight (from > to), which is what a night shift is.
struct ScheduleBlock {
    std::int32_t fromSecond = 0;
    std::int32_t toSecond = 0;
    std::int32_t postX = 0;
    std::int32_t postY = 0;
    std::int32_t postBand = 0;
    Activity activity = Activity::Working;

    [[nodiscard]] bool covers(std::int32_t secondOfDay) const noexcept;
};

/// A whole day. Blocks are tried in order and the first that covers the second
/// wins, so an override block goes first and the standing shift goes last.
class Schedule {
public:
    void add(const ScheduleBlock& block) { blocks_.push_back(block); }

    [[nodiscard]] const std::vector<ScheduleBlock>& blocks() const noexcept { return blocks_; }

    /// The block covering `secondOfDay`, or nullptr for "off, and elsewhere".
    [[nodiscard]] const ScheduleBlock* at(std::int32_t secondOfDay) const noexcept;

private:
    std::vector<ScheduleBlock> blocks_;
};

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------

/// How fast an actor walks, Q8 per movement step. Slower than the player's 11:
/// somebody crossing a taproom with a tray is not jogging, and the player
/// overtaking the room reads correctly.
inline constexpr std::int32_t kActorWalkSpeed = 7;
/// A bouncer with a job to do. Faster than a patron and slower than a run.
inline constexpr std::int32_t kActorPurposefulSpeed = 13;

/// Health. Bouncers are hired for it.
inline constexpr std::int32_t kActorHealth = 24;
inline constexpr std::int32_t kBouncerHealth = 40;

/// Close enough to be at a post, Q8. Half a tile.
inline constexpr std::int32_t kAtPostRadius = 128;

// ---------------------------------------------------------------------------
// the actor
// ---------------------------------------------------------------------------

class Actor {
public:
    Actor(std::int32_t id, std::string name, std::string epithet, ActorRole role,
          std::int32_t tileX, std::int32_t tileY, std::int32_t band);

    // --- identity ----------------------------------------------------------
    [[nodiscard]] std::int32_t id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& epithet() const noexcept { return epithet_; }
    [[nodiscard]] ActorRole role() const noexcept { return role_; }

    // --- where it is -------------------------------------------------------
    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    [[nodiscard]] std::int32_t band() const noexcept { return band_; }
    [[nodiscard]] std::int32_t tileX() const noexcept { return q8_tile(x_); }
    [[nodiscard]] std::int32_t tileY() const noexcept { return q8_tile(y_); }
    [[nodiscard]] Angle facing() const noexcept { return facing_; }

    /// Chebyshev-ish Q8 distance to a point: the larger of the two axis gaps.
    /// Integer, and the same measure the brawl reach uses.
    [[nodiscard]] std::int32_t distanceTo(std::int32_t xQ8, std::int32_t yQ8) const noexcept;

    // --- what it is doing --------------------------------------------------
    [[nodiscard]] Activity activity() const noexcept { return activity_; }
    void setActivity(Activity activity) noexcept { activity_ = activity; }
    [[nodiscard]] bool present() const noexcept { return activity_ != Activity::Away; }

    [[nodiscard]] Schedule& schedule() noexcept { return schedule_; }
    [[nodiscard]] const Schedule& schedule() const noexcept { return schedule_; }

    // --- condition ---------------------------------------------------------
    [[nodiscard]] std::int32_t hp() const noexcept { return hp_; }
    [[nodiscard]] std::int32_t hpMax() const noexcept { return hpMax_; }
    void setHealth(std::int32_t hp, std::int32_t hpMax) noexcept;
    [[nodiscard]] Weapon weapon() const noexcept { return weapon_; }
    void setWeapon(Weapon weapon) noexcept { weapon_ = weapon; }
    [[nodiscard]] Intent intent() const noexcept { return intent_; }
    void setIntent(Intent intent) noexcept { intent_ = intent; }
    [[nodiscard]] Fighter asFighter() const noexcept;

    /// Coin, in the smallest unit there is. Bartenders take it; patrons run out.
    [[nodiscard]] std::int32_t coin() const noexcept { return coin_; }
    void setCoin(std::int32_t coin) noexcept { coin_ = coin; }
    [[nodiscard]] bool takeCoin(std::int32_t amount) noexcept;
    void giveCoin(std::int32_t amount) noexcept;

    // --- movement ----------------------------------------------------------

    /// Sets the tile the actor is heading for. Nothing happens until step().
    void setDestination(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept;
    [[nodiscard]] std::int32_t destinationX() const noexcept { return destX_; }
    [[nodiscard]] std::int32_t destinationY() const noexcept { return destY_; }
    [[nodiscard]] std::int32_t destinationBand() const noexcept { return destBand_; }
    [[nodiscard]] bool atDestination() const noexcept;

    /// One movement step of walking. `path` supplies the route; `speed` is Q8
    /// per step. Advances toward the next tile centre, and asks `path` for a
    /// fresh first step each time a tile centre is reached.
    void step(RegionPath& path, std::int32_t speed) noexcept;

    /// Puts the actor down at a tile immediately, with no walk. For a schedule
    /// block starting off-screen, and for tests.
    void placeAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) noexcept;

    /// Turns to look at a point.
    void faceToward(std::int32_t xQ8, std::int32_t yQ8) noexcept;
    void setFacing(Angle facing) noexcept { facing_ = facing & (kTurnFull - 1); }

    /// Displaces the actor by a Q8 impulse without pathing -- a shove. Refuses
    /// to leave a tile it cannot stand on, so a shove cannot put somebody in a
    /// wall or off a quay.
    void push(const TileQuery& tiles, std::int32_t dxQ8, std::int32_t dyQ8) noexcept;

    // --- hashing -----------------------------------------------------------

    /// Folds everything that IS this actor into the sink, in a fixed order.
    void hashInto(HashSink& sink) const;

private:
    std::int32_t id_;
    std::string name_;
    std::string epithet_;
    ActorRole role_;

    std::int32_t x_;
    std::int32_t y_;
    std::int32_t band_;
    Angle facing_ = 0;

    std::int32_t destX_;
    std::int32_t destY_;
    std::int32_t destBand_;
    /// The tile currently being walked to. Equal to the actor's own tile when
    /// there is nowhere to go.
    std::int32_t nextX_;
    std::int32_t nextY_;
    std::int32_t nextBand_;

    Activity activity_ = Activity::Away;
    Schedule schedule_;

    std::int32_t hp_ = kActorHealth;
    std::int32_t hpMax_ = kActorHealth;
    Weapon weapon_ = Weapon::Fists;
    Intent intent_ = Intent::Subdue;
    std::int32_t coin_ = 0;
};

}  // namespace granadad::sim
