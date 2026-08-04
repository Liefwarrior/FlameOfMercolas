#pragma once

// The Gilded Gull: a room in the Docks with people in it who have jobs.
//
// K03 in DOCKS-GAZETTEER.md section 3 -- "Captains' tavern: charts on the
// walls, factors doing deals, the good wine", run by Master Venn, who is canon
// in content/raws/names/notables.json along with his six hired staff. It is the
// district's grandest house and the only one with rentable rooms authored
// above it, which is why it and not the Bilge or the Lantern Room is the
// tavern that gets staffed first.
//
// EVERY COORDINATE IN HERE WAS READ OUT OF THE BAKED WORLD, not out of the
// generator that produced it, and test_tavern.cpp re-reads all of them from
// content/maps/baked/docks_surface.trojsav on every build. A tavern whose bar
// is one tile from where the map says it is would put the bartender inside a
// wall on the day somebody re-bakes the district.
//
// WHAT IS SIMULATED
//
//   * six staff and eight patrons, each an Actor with a name, a post, hours,
//     and something it does when the player walks up to it;
//   * an hour of the clock, which decides who is in the room, whether the fire
//     is lit, and how loud it is;
//   * coin: the bartender has stock and takes payment, the innkeeper rents one
//     of the four rooms above the stair and the player sleeps in it;
//   * trouble: brawling, stealing or refusing to leave gets you warned by a
//     bouncer and then physically put out of the door.
//
// WHAT IS NOT, and is not pretended to be: hunger, rest, wages, relationships,
// crime beyond this room, or anything the district does outside these walls.
// The actor simulation ARCHITECTURE.md marks NOT BUILT stays not built; this is
// one building's worth of it, done properly.
//
// NO FLOATS.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/actor.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/spellbook.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the building, in world tiles
// ---------------------------------------------------------------------------

/// The Gilded Gull's authored geometry. World tiles: the baked world carries a
/// one-chunk VOID border, so authored local (lx, ly, lz) is (lx+32, ly+32,
/// lz+8). Everything here is the WORLD number.
namespace gull {

/// Ground floor -- the taproom -- and the guest floor above it.
inline constexpr std::int32_t kGroundBand = 19;
inline constexpr std::int32_t kUpperBand = 20;

/// Footprint including the walls. 15 x 14, the district's largest interior
/// after the Ropewalk.
inline constexpr std::int32_t kFootprintX0 = 146;
inline constexpr std::int32_t kFootprintY0 = 66;
inline constexpr std::int32_t kFootprintX1 = 160;
inline constexpr std::int32_t kFootprintY1 = 79;

/// The two door tiles in the north wall, on the Tarwalk frontage.
inline constexpr std::int32_t kDoorX0 = 153;
inline constexpr std::int32_t kDoorX1 = 154;
inline constexpr std::int32_t kDoorY = 66;

/// Where a body ends up when it is put out: the quay apron, two tiles clear of
/// the threshold, which is far enough that walking straight back in is a
/// decision rather than an accident.
inline constexpr std::int32_t kStreetX = 153;
inline constexpr std::int32_t kStreetY = 63;

/// The bar counter: a seven-tile run of granite across the middle of the room.
inline constexpr std::int32_t kBarX0 = 149;
inline constexpr std::int32_t kBarX1 = 155;
inline constexpr std::int32_t kBarY = 71;
/// Behind it, where the bartender stands.
inline constexpr std::int32_t kBartenderX = 152;
inline constexpr std::int32_t kBartenderY = 72;

/// The hearth in the south wall, and the tile in front of it the fire lights
/// the room from.
inline constexpr std::int32_t kHearthX0 = 149;
inline constexpr std::int32_t kHearthX1 = 150;
inline constexpr std::int32_t kHearthY = 77;

/// The stair to the guest rooms, and the snug it stands in behind the
/// partition wall at x=157.
inline constexpr std::int32_t kStairX = 159;
inline constexpr std::int32_t kStairY = 77;
inline constexpr std::int32_t kSnugX0 = 158;
inline constexpr std::int32_t kSnugX1 = 159;

/// The four rentable rooms above, one bed each. Authored as CLOTH blocks in
/// content/maps/src/docks_surface.tmx; the tile a sleeper stands on is the one
/// beside the bed.
inline constexpr std::int32_t kRoomCount = 4;
struct GuestRoom {
    std::int32_t bedX;
    std::int32_t bedY;
    /// Where a body stands to use the bed.
    std::int32_t standX;
    std::int32_t standY;
};
inline constexpr GuestRoom kRooms[kRoomCount] = {
    {148, 68, 149, 68},  // north-west
    {156, 68, 155, 68},  // north-east
    {148, 76, 149, 76},  // south-west
    {156, 76, 155, 76},  // south-east
};

/// Everything a pathfinder inside the Gull may touch: the building, both
/// floors, and enough of the Tarwalk in front of it to put somebody out on.
inline constexpr TileBox kRegion{kFootprintX0 - 1, kStreetY - 2, kGroundBand,
                                 kFootprintX1 + 1, kFootprintY1 + 1, kUpperBand};

/// Whether a tile is inside the walls, on either floor.
[[nodiscard]] constexpr bool insideFootprint(std::int32_t tileX, std::int32_t tileY) noexcept {
    return tileX >= kFootprintX0 && tileX <= kFootprintX1 && tileY >= kFootprintY0 &&
           tileY <= kFootprintY1;
}

// --- the hours --------------------------------------------------------------

/// Doors open at eleven and shut at two. A captains' house keeps late hours
/// because ships come in on the tide, not on the clock.
inline constexpr std::int32_t kOpensAt = hourOfDay(11);
inline constexpr std::int32_t kClosesAt = hourOfDay(2);

/// The fire is banked, not lit, outside these. Lit an hour before the doors so
/// the room is warm when the first crew arrives.
inline constexpr std::int32_t kFireLitFrom = hourOfDay(10);
inline constexpr std::int32_t kFireLitUntil = hourOfDay(3);

}  // namespace gull

// ---------------------------------------------------------------------------
// trade
// ---------------------------------------------------------------------------

/// What a drink and a bed cost, in the smallest coin there is.
inline constexpr std::int32_t kDrinkPrice = 2;
inline constexpr std::int32_t kRoomPrice = 12;
/// Barrels in the cellar at open. A tavern that never runs dry has no economy.
inline constexpr std::int32_t kOpeningStock = 60;
/// What the player arrives with. S2 is where the purse starts existing.
inline constexpr std::int32_t kPlayerStartingCoin = 40;

/// The answer to any request made across the bar or at the stair.
enum class ServiceResult : std::uint8_t {
    Served = 0,
    /// Nobody is behind the bar, or at the stair, or in the room at all.
    NobodyThere = 1,
    /// The doors are shut.
    Closed = 2,
    /// Not enough coin.
    NoCoin = 3,
    /// The barrels are dry, or every room is let.
    OutOfStock = 4,
    /// You are not close enough to ask.
    TooFar = 5,
    /// You have been put out of this house, and it has not forgotten.
    Barred = 6,
    /// Asked, and refused. The Skyrunner's whole personality.
    Refused = 7,
};

[[nodiscard]] std::string_view serviceResultName(ServiceResult result) noexcept;

/// What the player did that the house minds.
enum class Offence : std::uint8_t {
    /// Threw a punch under this roof.
    Brawled = 0,
    /// Took something.
    Stole = 1,
    /// Was told to go, and did not.
    RefusedToLeave = 2,
};

[[nodiscard]] std::string_view offenceName(Offence offence) noexcept;

/// How the house is currently regarding the player.
enum class Standing : std::uint8_t {
    /// Nothing has happened.
    Welcome = 0,
    /// A bouncer is on the way over to say it once.
    BeingWarned = 1,
    /// Said. The clock is running.
    Warned = 2,
    /// Hands on.
    BeingEjected = 3,
    /// Out, and not welcome back for a while.
    Barred = 4,
};

[[nodiscard]] std::string_view standingName(Standing standing) noexcept;

// --- the numbers behind the door policy -------------------------------------

/// How close a bouncer has to get before the warning counts as given, Q8.
inline constexpr std::int32_t kWarnRadius = 2 * kSubOne;
/// Seconds between being warned and being handled. Long enough to finish a
/// drink and leave; short enough that "he warned me" is not a licence.
inline constexpr std::int32_t kGraceSeconds = 20;
/// Seconds you stay barred after being put out.
inline constexpr std::int32_t kBarredSeconds = 300;
/// Q8 a single shove moves somebody. Five-eighths of a tile, so being frog-
/// marched from the bar to the street is about fifteen seconds of it.
inline constexpr std::int32_t kEjectionShove = 160;

/// The reserved actor id the player answers to inside this room. Zero, so no
/// staff or patron can ever collide with it.
inline constexpr std::int32_t kPlayerActorId = 0;

/// A BRAWL NEVER KILLS. That is what makes it a brawl, and it is why the door
/// policy can be enforced with fists at all: the worst a taproom fight does to
/// the player is put them on the floor and then out of it. Hit points stop
/// here, the fight ends, and the bouncers carry on with the ejection.
///
/// The player being knocked properly unconscious -- and everything that follows
/// from it -- belongs with the dedicated combat screen, which S2 does not have.
inline constexpr std::int32_t kPlayerBrawlFloor = 1;

// ---------------------------------------------------------------------------
// the tavern
// ---------------------------------------------------------------------------

/// What a conversation produced.
struct TalkResult {
    ServiceResult result = ServiceResult::NobodyThere;
    /// Who answered, or empty.
    std::string speaker;
    /// What they said.
    std::string line;
};

class Tavern final : public SimulationSystem {
public:
    /// `tiles` and the world behind it must outlive the tavern. `contentDir` is
    /// only used to read the spell raws the priest teaches from; an empty path
    /// or a missing file leaves him with nothing to teach and changes nothing
    /// else.
    ///
    /// The tavern carries its OWN CounterRandomSource rather than only using
    /// the one the engine hands it per tick. Every draw it makes is still the
    /// same pure (seed, salt, tick, key, index) chain -- the source holds no
    /// stream position -- but a player punching somebody happens between ticks,
    /// outside any TickContext, and a fight whose damage roll came from
    /// somewhere unhashed would be a hole in the twin-run gate. The draw index
    /// for those is a counter that IS hashed.
    Tavern(const TileQuery& tiles, std::int32_t timeOfDaySeconds, std::uint64_t worldSeed,
           std::filesystem::path contentDir = {});

    // --- SimulationSystem ---------------------------------------------------

    [[nodiscard]] const SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] TickPhase phase() const noexcept override { return TickPhase::Actors; }
    /// One simulated second: the clock moves, schedules fire, drinks are
    /// poured, bouncers decide, and a fight resolves one exchange.
    void tick(const TickContext& context) override;
    void hash_into(HashSink& sink) const override;

    // --- the movement clock -------------------------------------------------

    /// One movement step for every actor. Called kStepsPerSecond times between
    /// ticks, by whoever owns the loop.
    void stepMovement();

    /// Tells the room where the player's body is, and what is in its hands.
    /// Q8, the same units the body uses -- see actor.hpp on why this is not
    /// reconstructed at draw time.
    void setPlayer(std::int32_t xQ8, std::int32_t yQ8, std::int32_t band) noexcept;
    void setPlayerCombat(Weapon weapon, Intent intent) noexcept;

    /// A shove the room wants applied to the player's body, in Q8, or (0,0).
    /// Read and CLEARED by the caller, which owns the body -- the tavern never
    /// holds a pointer to it.
    [[nodiscard]] std::int32_t takePlayerShoveX() noexcept;
    [[nodiscard]] std::int32_t takePlayerShoveY() noexcept;

    // --- the clock ----------------------------------------------------------

    [[nodiscard]] std::int32_t timeOfDay() const noexcept { return timeOfDay_; }
    void setTimeOfDay(std::int32_t secondOfDay) noexcept;
    /// Moves the clock forward and re-seats everybody, without running the
    /// intervening seconds. What sleeping in a rented room does.
    void skipTo(std::int32_t secondOfDay);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool fireLit() const noexcept;

    // --- who is in the room -------------------------------------------------

    [[nodiscard]] const std::vector<Actor>& actors() const noexcept { return actors_; }
    [[nodiscard]] const Actor* actorById(std::int32_t id) const noexcept;
    [[nodiscard]] std::int32_t presentCount() const noexcept;
    [[nodiscard]] std::int32_t patronCount() const noexcept;
    /// 0 (empty) to 100 (pay night). What the room SOUNDS like.
    [[nodiscard]] std::int32_t noise() const noexcept;

    /// The nearest present actor to the player, within `reachQ8`, or nullptr.
    [[nodiscard]] const Actor* nearestTo(std::int32_t xQ8, std::int32_t yQ8,
                                         std::int32_t reachQ8) const noexcept;

    [[nodiscard]] bool playerInside() const noexcept;

    // --- trade --------------------------------------------------------------

    [[nodiscard]] std::int32_t playerCoin() const noexcept { return playerCoin_; }
    void setPlayerCoin(std::int32_t coin) noexcept { playerCoin_ = coin; }
    [[nodiscard]] std::int32_t drinkStock() const noexcept { return drinkStock_; }
    [[nodiscard]] std::int32_t drinksPlayerHasHad() const noexcept { return playerDrinks_; }

    /// Buys a drink across the bar. The player must be within reach of the
    /// bartender, who must be on shift, with stock, and be paid.
    ServiceResult buyDrink();

    /// Rents one of the four rooms above the stair from the innkeeper.
    [[nodiscard]] std::int32_t rentedRoom() const noexcept { return rentedRoom_; }
    ServiceResult rentRoom();

    /// Sleeps until seven in the morning. Requires a rented room and a body in
    /// it. Returns Served and moves the clock; the caller moves the body.
    ServiceResult sleep();

    /// Talks to whoever is nearest. Every role answers differently, and the
    /// Skyrunner contact answers by not answering.
    [[nodiscard]] TalkResult talkToNearest();

    /// What the priest of the Flame will teach a student at this level of
    /// LINKCRAFT -- the skill the eleven authored spells are actually cast
    /// with -- straight out of the raws. Empty when he is not in the room or
    /// the raws were not found.
    [[nodiscard]] std::vector<const Spell*> priestTeaches(std::int32_t linkcraftLevel) const;
    [[nodiscard]] const Spellbook& spellbook() const noexcept { return spellbook_; }

    // --- trouble ------------------------------------------------------------

    [[nodiscard]] Standing playerStanding() const noexcept { return standing_; }
    /// The player's hit points as this room has been keeping them. Floors at
    /// kPlayerBrawlFloor -- see the constant.
    [[nodiscard]] std::int32_t playerHp() const noexcept { return playerHp_; }
    /// True once a brawl has taken the player to the floor.
    [[nodiscard]] bool playerFloored() const noexcept { return playerFloored_; }
    [[nodiscard]] std::int32_t warningsGiven() const noexcept { return warningsGiven_; }
    [[nodiscard]] std::int32_t timesEjected() const noexcept { return timesEjected_; }
    /// What the bouncer said, or empty. Cleared when a new one is issued.
    [[nodiscard]] const std::string& lastWarning() const noexcept { return lastWarning_; }
    /// The bouncer currently dealing with the player, or nullptr.
    [[nodiscard]] const Actor* respondingBouncer() const noexcept;

    /// Tells the house the player did something. Idempotent within a second.
    void reportOffence(Offence offence);

    /// The player throws a punch at whoever is in reach. Resolves IN WORLD when
    /// classifyFight says brawl, and refuses -- raising escalation() -- when it
    /// says lethal, because that is the combat screen's fight and not this
    /// room's.
    struct PunchResult {
        bool swung = false;
        FightClass fight = FightClass::Brawl;
        Blow blow;
        std::int32_t targetId = -1;
        std::string targetName;
    };
    PunchResult playerPunchNearest();

    /// Set when a fight in this room stopped being the world's business. The
    /// client routes this to the dedicated first-person combat screen; until
    /// that screen exists (S3+) the tavern simply stops resolving the fight and
    /// says so, rather than quietly resolving a knife fight with fist rules.
    [[nodiscard]] FightClass escalation() const noexcept { return escalation_; }
    [[nodiscard]] bool escalated() const noexcept { return escalation_ == FightClass::Lethal; }
    void clearEscalation() noexcept { escalation_ = FightClass::Brawl; }

    /// The fight as the rule sees it right now: the player plus everybody
    /// currently swinging.
    [[nodiscard]] std::vector<Fighter> currentFight() const;

private:
    void buildRoster();
    void applySchedules();
    void tickBouncers();
    void tickBrawl();
    void tickPatrons();
    void advanceSecond();
    [[nodiscard]] Actor* findRole(ActorRole role, bool presentOnly) noexcept;
    [[nodiscard]] const Actor* findRole(ActorRole role, bool presentOnly) const noexcept;
    [[nodiscard]] Actor* mutableActorById(std::int32_t id) noexcept;
    [[nodiscard]] Actor* onDutyBouncerNearestPlayer() noexcept;
    [[nodiscard]] std::int32_t speedFor(const Actor& actor) const noexcept;
    /// A draw for an action the player took between ticks. Bumps and hashes the
    /// sequence counter, so replaying the same actions reproduces the same
    /// rolls.
    [[nodiscard]] std::uint64_t drawForPlayerAction() noexcept;

    SystemId id_;
    const TileQuery* tiles_;
    RegionPath path_;
    Spellbook spellbook_;
    CounterRandomSource rng_;
    std::int32_t playerActionSeq_ = 0;

    std::vector<Actor> actors_;
    std::int32_t timeOfDay_;
    std::int64_t tick_ = 0;

    // the player, as this room sees them
    std::int32_t playerX_ = 0;
    std::int32_t playerY_ = 0;
    std::int32_t playerBand_ = gull::kGroundBand;
    bool playerKnown_ = false;
    Weapon playerWeapon_ = Weapon::Fists;
    Intent playerIntent_ = Intent::Subdue;
    std::int32_t playerHp_ = 100;
    std::int32_t playerHpMax_ = 100;
    bool playerFloored_ = false;
    std::int32_t playerCoin_ = kPlayerStartingCoin;
    std::int32_t playerDrinks_ = 0;
    std::int32_t shoveX_ = 0;
    std::int32_t shoveY_ = 0;

    // trade
    std::int32_t drinkStock_ = kOpeningStock;
    std::int32_t rentedRoom_ = -1;
    std::int64_t stockedOnDay_ = -1;

    // trouble
    Standing standing_ = Standing::Welcome;
    std::int32_t offences_ = 0;
    std::int32_t warningsGiven_ = 0;
    std::int32_t timesEjected_ = 0;
    std::int64_t warnedAtTick_ = -1;
    std::int64_t barredUntilTick_ = -1;
    std::int32_t respondingBouncerId_ = -1;
    std::string lastWarning_;
    FightClass escalation_ = FightClass::Brawl;
    /// Ids currently swinging at the player, in ascending order.
    std::vector<std::int32_t> brawlers_;
};

}  // namespace granadad::sim
