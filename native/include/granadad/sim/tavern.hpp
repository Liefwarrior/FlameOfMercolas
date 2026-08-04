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
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/engine.hpp"
// The roof moves' own vocabulary: a landing is charged HERE, in the room that
// owns the player's hit points, so RoofResult and safeDropBands are part of
// this header's interface. See Tavern::settleLanding.
#include "granadad/sim/player.hpp"
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
/// And the lead over both of them: fifteen by fourteen of flat roof, authored
/// since S1, standable since S1, and unreachable until S5 taught the body to
/// climb. It is the Skyrunners' front door to the ward's grandest house.
inline constexpr std::int32_t kRoofBand = 21;

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

/// S5. A guest with anything worth keeping keeps it at the foot of the bed, so
/// the cell a strongbox stands on IS the bed cell -- authored as a solid CLOTH
/// block, which makes it furniture a body cannot walk into and can reach across
/// from the one tile beside it. Four rooms, four boxes, and no new geometry:
/// the burglary happens on tiles the map already has.
///
/// A box in a room you RENTED is your own, and cracking your own box is not a
/// crime; the room refuses to call it one.
[[nodiscard]] constexpr std::int32_t roomAtStand(std::int32_t tileX,
                                                 std::int32_t tileY) noexcept {
    for (std::int32_t i = 0; i < kRoomCount; ++i) {
        if (kRooms[i].standX == tileX && kRooms[i].standY == tileY) {
            return i;
        }
    }
    return -1;
}

/// Where a bale of contraband sits between the boat and the buyer: the snug
/// behind the partition, which is the one corner of the taproom the bar cannot
/// see into. It is why the Skyrunners' contact drinks there.
inline constexpr std::int32_t kBaleX = 158;
inline constexpr std::int32_t kBaleY = 70;

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

// --- the lights, DERIVED ----------------------------------------------------
//
// S2 hardcoded four table tiles and three lantern tiles in the renderer, with
// no derivation and no test -- the exact thing the header rule above forbids,
// and the S2 review caught it. They are computed here now, out of the baked
// bytes and out of coordinates that are themselves re-read from the baked bytes
// on every build.

/// A tile, when only a tile is meant.
struct TilePos {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// What kind of flame a house light is. The renderer decides what that looks
/// like; the simulation decides where they are and when they burn.
enum class LightKind : std::uint8_t {
    /// The fire in the south wall.
    Hearth = 0,
    /// A candle standing on a piece of furniture.
    Candle = 1,
    /// A lantern hanging from the ceiling.
    Lantern = 2,
};

struct HouseLight {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = kGroundBand;
    LightKind kind = LightKind::Candle;
};

/// The taproom's FURNITURE, read out of the world: every solid cell inside the
/// walls on the ground floor that is not the bar counter, not the hearth and
/// not the snug partition. Those are the tables, and a table is where a candle
/// stands. Ascending by (y, x), so the list is the map's and not the caller's.
[[nodiscard]] std::vector<TilePos> taproomTables(const TileQuery& tiles);

/// How many the baked Docks actually has. Pinned so a re-bake that moves the
/// furniture is a red test rather than a room that quietly goes dark.
inline constexpr std::size_t kTableCount = 4;

/// Where the three hanging lanterns are, derived from the door and the bar --
/// both of which test_tavern.cpp re-reads from the baked bytes. A captains'
/// house lights its threshold and its counter; nothing else needs a rule.
[[nodiscard]] std::vector<TilePos> lanternTiles();

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

/// What a drink and a bed cost, in the smallest coin there is. THE ODDS, not
/// the price: what the player actually pays comes out of drinkPriceForPlayer()
/// and moves with how the bartender feels about them and how well they haggle.
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
/// What the ward's own balance of power can move that to. A house never gives
/// no rope at all and never gives all night -- see graceSecondsForPlayer().
inline constexpr std::int32_t kGraceSecondsFloor = 6;
inline constexpr std::int32_t kGraceSecondsCeiling = 40;
/// Seconds you stay barred after being put out.
inline constexpr std::int32_t kBarredSeconds = 300;
/// Q8 a single shove moves somebody. Five-eighths of a tile, so being frog-
/// marched from the bar to the street is about fifteen seconds of it.
inline constexpr std::int32_t kEjectionShove = 160;

/// The reserved actor id the player answers to inside this room. Zero, so no
/// staff or patron can ever collide with it.
inline constexpr std::int32_t kPlayerActorId = 0;

/// How close a body has to be to a strongbox or a bale to put hands on it, Q8.
inline constexpr std::int32_t kReachQ8 = 2 * kSubOne;

/// Bales in the snug per night. A boat brings what a boat brings.
inline constexpr std::int32_t kBalesPerNight = 2;
/// And how many UNITS are in one. S5's bale was a bool; a bale has contents
/// now, and what is in it is what a contract can want and a watchman can find.
inline constexpr std::int32_t kBaleUnits = 3;

/// Rats on the skirting after the doors shut, per night. Four, because the
/// ward's smallest bounty asks for two and its largest for four -- a night's
/// work is a night's work, and a fifth rat would make a bounty a formality.
inline constexpr std::int32_t kVerminPerNight = 4;
/// What a rat has. One clean punch.
inline constexpr std::int32_t kVerminHealth = 4;
/// The hours the taproom is quiet enough for them. From an hour before the
/// doors shut until they open again.
inline constexpr std::int32_t kVerminFrom = hourOfDay(1);
inline constexpr std::int32_t kVerminUntil = hourOfDay(11);

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

    /// Every flame the house is showing right now: the hearth while the fire is
    /// lit, a candle on every derived table and the hanging lanterns while the
    /// doors are open, nothing at all when the house is dark. The SIMULATION
    /// owns this, not the renderer -- see the derivation note on gull::.
    [[nodiscard]] std::vector<gull::HouseLight> houseLights() const;

    // --- who is in the room -------------------------------------------------

    [[nodiscard]] const std::vector<Actor>& actors() const noexcept { return actors_; }
    [[nodiscard]] const Actor* actorById(std::int32_t id) const noexcept;
    /// PEOPLE in the room. A rat is not one of the fourteen.
    [[nodiscard]] std::int32_t presentCount() const noexcept;
    /// And rats, counted apart for the same reason.
    [[nodiscard]] std::int32_t verminPresent() const noexcept;
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

    /// What the bar and the stair will charge the player RIGHT NOW.
    ///
    /// kDrinkPrice and kRoomPrice are what the goods are worth. What you pay is
    /// what the person behind the counter thinks of you, plus what your
    /// streetwise is worth against theirs, plus whatever you last argued them
    /// down to. This is the single most legible place standing shows up: a warm
    /// bartender charges less than a cold one for the same mug, every time,
    /// with no dialogue open.
    [[nodiscard]] std::int32_t drinkPriceForPlayer() const;
    [[nodiscard]] std::int32_t roomPriceForPlayer() const;
    /// The price last settled by haggling, or -1. Cleared when it is used, so
    /// one argument buys one drink.
    [[nodiscard]] std::int32_t negotiatedDrinkPrice() const noexcept {
        return negotiatedDrink_;
    }
    [[nodiscard]] std::int32_t negotiatedRoomPrice() const noexcept { return negotiatedRoom_; }

    /// Buys a drink across the bar. The player must be within reach of the
    /// bartender, who must be on shift, with stock, and be paid.
    ///
    /// Returns Refused when the bartender is HOSTILE. That is the one refusal
    /// in the trade path, and it is deliberate: DOCKS-GAZETTEER section 5.3
    /// forbids refusing INFORMATION, and says nothing about a landlord who
    /// caught you with a hand in his till.
    ServiceResult buyDrink();

    /// Rents one of the four rooms above the stair from the innkeeper.
    [[nodiscard]] std::int32_t rentedRoom() const noexcept { return rentedRoom_; }
    ServiceResult rentRoom();

    /// Sleeps until seven in the morning. Requires a rented room and a body in
    /// it. Returns Served and moves the clock; the caller moves the body.
    ServiceResult sleep();

    /// Talks to whoever is nearest. Every role answers differently, and the
    /// Skyrunner contact answers by not answering.
    ///
    /// KEPT because the roles' service answers are real -- "barrels are dry
    /// until the doors open again" is the bartender's own stock talking. It is
    /// no longer the whole of a conversation: talkTo() is.
    [[nodiscard]] TalkResult talkToNearest();

    // --- conversation -------------------------------------------------------
    //
    // The Gull owns the ward's social state for now, because the Gull is the
    // only room with people in it. When the district's other houses are staffed
    // the director moves up a level and every one of them borrows the same one;
    // nothing below depends on it living here.

    [[nodiscard]] DialogueDirector& dialogue() noexcept { return dialogue_; }
    [[nodiscard]] const DialogueDirector& dialogue() const noexcept { return dialogue_; }

    /// Describes an actor to the dialogue layer: who they are, which of the
    /// Forty they are (if any), what they will talk shop about, what they sell.
    [[nodiscard]] Speaker speakerFor(const Actor& actor) const;

    /// Opens a conversation with whoever is in reach. False when nobody is.
    bool talkTo();
    /// Picks a topic. The reply's coin and offences are applied HERE, so the
    /// dialogue layer never has to know what a tavern is.
    Reply chooseTopic(std::size_t index);
    /// Haggling moves, legal only while a haggle is open.
    Reply offerPrice(std::int32_t coins);
    Reply takeAskingPrice();
    /// Composing moves, legal only while a workbench is open. Routed through
    /// the room for the same reason the haggle is: the reply may move coin,
    /// standing or a rung, and the room is what applies those.
    Reply commitForge();
    Reply endForge();
    void endConversation();

    // --- S5: the crimes that are an ACT and not a conversation --------------
    //
    // Leaning on somebody and selling stolen property happen across a table and
    // live in the dialogue layer. These happen in the room, so the room owns
    // them -- and each reports through the same DialogueDirector::noteCrime the
    // topics do, so a tally, a heat and a faction number can never be moved by
    // one path and missed by the other.

    /// What an act in the room produced.
    struct StealResult {
        ServiceResult result = ServiceResult::NobodyThere;
        /// Coin taken.
        std::int32_t coin = 0;
        /// Pieces of property taken -- the thing a fence exists to buy.
        std::int32_t loot = 0;
        /// True when somebody present could see it happen.
        bool seen = false;
        /// A short report for the message line.
        std::string line;
    };

    /// Cracks the strongbox at the foot of the bed in the guest room the body
    /// is standing in. Refused for a room the player rented, for a box already
    /// emptied, and from the wrong floor.
    StealResult crackStrongbox();
    /// Which of the four boxes have been emptied, as a bitmask. Hashed.
    [[nodiscard]] std::int32_t crackedBoxes() const noexcept { return crackedBoxes_; }

    /// What a landing off the roofs cost.
    struct LandingResult {
        /// Hit points the fall took, or 0.
        std::int32_t hurt = 0;
        /// True when this landing was the first arrival somewhere new and high,
        /// and therefore a roof-run the ward could have minded.
        bool roofRun = false;
        /// The counted verb the questline was told about: "climbs" or "leaps".
        std::string_view counted;
    };

    /// Charges a roof landing: the craft it takes, the fall it cost, the
    /// roof-run it was, and the counted verb a questline stage might want.
    ///
    /// MOVED HERE IN S6, out of render::Session, and it is a correction rather
    /// than a tidy-up. The S5 review's third finding was that this glue -- which
    /// owns fall damage, a skill use, a crime and two questline tallies -- had
    /// no test that could go red, because it lived in the client layer and the
    /// simulation suite could not reach it: test_crime.cpp had to hand-call
    /// noteTally() and noteCrime() to walk the Skyrunner line past its roof
    /// beats, which proved the questline and proved nothing about the landing.
    ///
    /// It is simulation state -- hit points, a tally, heat, two faction numbers
    /// -- so it belongs in the room that owns them. Session now hands the room
    /// the fall the body reported and draws whatever comes back.
    LandingResult settleLanding(const RoofResult& move, std::int32_t fellBands,
                                std::int32_t landedBand);
    /// The highest band the player has ever stood on in this room's memory. A
    /// roof-run is counted once per ARRIVAL somewhere new and high rather than
    /// once per step, and this is what tells the two apart.
    [[nodiscard]] std::int32_t highestBandReached() const noexcept { return highestBand_; }

    /// Skins the downed vermin within reach. THE CULL VERB, and it is the Java
    /// build's own rule read across: stand beside a body, spend the act, and
    /// there is a scalp in your hand. A rat that has been skinned does not get
    /// up again, which is what a per-night count of them is FOR -- without it
    /// the ward's bounty is a coin faucet with whiskers.
    ///
    /// Refused for a rat still on its feet: this build has no killing, and a
    /// brawl is what puts a thing on the floor.
    StealResult takeScalp();
    /// Which of tonight's vermin have been skinned, as a bitmask. Hashed.
    [[nodiscard]] std::int32_t scalpedVermin() const noexcept { return scalpedVermin_; }

    // --- S6: the Watch --------------------------------------------------------

    /// Where a watchman is in the business of taking you.
    enum class WatchStance : std::uint8_t {
        /// Having a drink.
        Idle = 0,
        /// Has seen something and is crossing the room about it. THIS IS THE
        /// WINDOW: out of the door, out of his sight, and it is over.
        Closing = 1,
        /// Hands on. Resolved in the same second it is reached.
        Taken = 2,
    };

    /// What an arrest came to.
    struct ArrestReport {
        bool happened = false;
        Sentence sentence = Sentence::None;
        WatchCause cause = WatchCause::None;
        std::int32_t unitsSeized = 0;
        std::int32_t fine = 0;
        std::int32_t heldHours = 0;
        /// Contracts that died with the goods.
        std::int32_t contractsLost = 0;
        std::string officer;
        std::string line;
    };

    [[nodiscard]] WatchStance watchStance() const noexcept { return watchStance_; }
    [[nodiscard]] WatchCause watchInterest() const noexcept { return watchCause_; }
    /// The watchman currently interested in the player, or nullptr.
    [[nodiscard]] const Actor* respondingWatchman() const noexcept;
    /// What the last watchman to look at you said, or empty.
    [[nodiscard]] const std::string& lastDemand() const noexcept { return lastDemand_; }
    /// The last arrest, whether or not it has been read.
    [[nodiscard]] const ArrestReport& lastArrest() const noexcept { return lastArrest_; }
    /// True once, after an arrest, so whoever owns the body can put it on the
    /// street where the impound turns people loose. Read and CLEARED.
    [[nodiscard]] bool takeArrestRelease() noexcept;

    /// Which day of the world this is. Monotonic across midnight and across a
    /// night in a cell, because a deadline that wrapped with the wall clock
    /// would be a deadline nobody could miss.
    [[nodiscard]] std::int32_t dayNumber() const noexcept;

    /// Moves the clock on by whole hours, days included. What a sentence does.
    void skipHours(std::int32_t hours);

    /// Takes the bale in the snug, or puts it back down. The roofs do not hand
    /// a bale to a stranger, so it wants membership; carrying it OUT of the
    /// house past somebody who would mind is the run, and the room notices that
    /// on its own -- see stepMovement.
    StealResult handleBale();

    /// Everybody present who could see it remembers that they saw it. This is
    /// what makes a robbery in a full taproom different from one in an empty
    /// one, and it is what moves the ward's own opinion.
    void spreadWitness(std::int32_t victimId, Deed deed);

    /// How many present actors, other than `exceptId`, can actually see the
    /// player right now. Same range and same sight rule spreadWitness uses,
    /// exposed because a CRIME needs the count rather than the side effect:
    /// heat is what the Watch heard, and nobody heard an empty room.
    [[nodiscard]] std::int32_t witnessCount(std::int32_t exceptId) const noexcept;
    /// How far across a room a deed carries, in tiles.
    static constexpr std::int32_t kWitnessRangeTiles = 8;
    /// Inside this, you do not need a sight line: you are in arm's reach and
    /// there is nothing between you but air.
    ///
    /// This is not a softening of the line-of-sight rule, it is what keeps the
    /// rule from being absurd. The Gull's bar counter is authored as SOLID
    /// masonry -- correctly, a body cannot walk through it -- so a strict ray
    /// makes the bartender blind to the hand in her own till, one tile away
    /// across her own bar. A counter is waist high; a wall is not; the tile
    /// lanes cannot tell them apart, and two tiles of grace is the honest way
    /// to say so until they can.
    static constexpr std::int32_t kWitnessReachTiles = 2;

    /// What the priest of the Flame will teach a student at this level of
    /// LINKCRAFT -- the skill the eleven authored spells are actually cast
    /// with -- straight out of the raws. Empty when he is not in the room or
    /// the raws were not found.
    [[nodiscard]] std::vector<const Spell*> priestTeaches(std::int32_t linkcraftLevel) const;
    /// ONE spell shelf, owned by the dialogue layer. The tavern used to load a
    /// second copy of the same eleven rows; two loaders reading one file is two
    /// chances to disagree about it.
    [[nodiscard]] const Spellbook& spellbook() const noexcept { return dialogue_.spellbook(); }

    // --- the guilds ---------------------------------------------------------

    /// Which faction claims an actor, or -1. DERIVED, not tabulated: the room
    /// knows the actor's job family, and the owner's own factions.json says
    /// which faction claims that family's jobs. Nobody wrote a second table and
    /// so nobody can let one drift.
    [[nodiscard]] std::int32_t factionOf(const Actor& actor) const noexcept;

    /// How many people in this room right now belong to a faction that is the
    /// declared rival of one the player holds rank in.
    ///
    /// THIS IS THE "ENEMY PRESENCE" HALF of the faction requirement, and it is
    /// a count of BODIES rather than a mood: a Watch runner who walks into the
    /// Gull at midnight is standing in a room with a Skyrunner in it, and the
    /// Skyrunner knows what the arm-band means.
    ///
    /// VERIFICATION GAP (S4): the COUNT has no consumer outside the test suite.
    /// What the room actually does with a rival present is seed them hostile
    /// (applyRivalHostility), which is real and proved; a number that says how
    /// many is a readout waiting for the thing that reads it.
    [[nodiscard]] std::int32_t enemyPresence() const noexcept;

    /// Seeds every present rival's opinion of the player to HOSTILE. Called
    /// whenever the player's standing on a ladder changes, because that is the
    /// moment the room learns whose side they are on.
    ///
    /// VERIFICATION GAP (S4): it fires on a RANK CHANGE and not on arrival. A
    /// rival who walks in an hour after you signed the Watch's roll greets you
    /// as a stranger until something else moves your standing. The right fix is
    /// a check when an actor becomes present, and it wants a schedule hook this
    /// room does not have yet.
    void applyRivalHostility();

    /// How many seconds of rope the house gives a warned player RIGHT NOW.
    ///
    /// THIS IS THE OTHER HALF, and it is the one that is visible from inside
    /// this room: when the Watch's influence over the ward falls and the roofs'
    /// rises, the houses stop waiting for the Watch and handle it themselves.
    /// A guild's weight in the district is not a number on a sheet -- it is how
    /// long a bouncer lets you finish your drink.
    [[nodiscard]] std::int32_t graceSecondsForPlayer() const noexcept;

    // --- trouble ------------------------------------------------------------

    [[nodiscard]] Standing playerStanding() const noexcept { return standing_; }
    /// The player's hit points as this room has been keeping them. Floors at
    /// kPlayerBrawlFloor -- see the constant.
    [[nodiscard]] std::int32_t playerHp() const noexcept { return playerHp_; }
    /// Hurts the player by `amount`, floored exactly the way a brawl is. S5's
    /// one caller is a landing off a roof that was higher than the legs allow,
    /// and it lives here because this is where the player's hit points live.
    void injurePlayer(std::int32_t amount);
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
    void clearEscalation() noexcept {
        escalation_ = FightClass::Brawl;
        escalationSeen_ = false;
    }

    /// The fight as the rule sees it right now: the player plus everybody
    /// currently swinging.
    [[nodiscard]] std::vector<Fighter> currentFight() const;

private:
    void buildRoster();
    void applySchedules();
    void tickBouncers();
    void tickWatch();
    void tickVermin();
    /// The watchman who can see the player right now, or nullptr.
    [[nodiscard]] Actor* watchmanWatchingPlayer() noexcept;
    /// True when this actor can see the player: present, same band, in range,
    /// and with a line to them. The same rule witnessCount uses, asked about
    /// one person.
    [[nodiscard]] bool canSeePlayer(const Actor& actor) const noexcept;
    /// The Watch puts its hands on you. One call site.
    void applyArrest(Actor& officer);
    /// The nearest downed vermin within reach, or nullptr.
    [[nodiscard]] Actor* downedVerminInReach() noexcept;
    /// The nearest rat on its feet within reach, or nullptr.
    [[nodiscard]] const Actor* nearestVerminTo(std::int32_t xQ8, std::int32_t yQ8,
                                               std::int32_t reachQ8) const noexcept;
    /// The actor id of the first rat. Roster order: every person, then every
    /// rat, so this plus an index is a rat's id and the bitmask has a home.
    [[nodiscard]] std::int32_t verminFirstId() const noexcept;
    /// What tonight's boat brought. Drawn once a night, so both bales in the
    /// snug are the same cargo -- because they came off the same hull.
    [[nodiscard]] Contraband drawBaleGood() noexcept;
    /// The authored skill level of a roster body, without building a Speaker.
    [[nodiscard]] std::int32_t rosterSkillOf(const Actor& actor) const noexcept;
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

    void applyReply(Reply& reply);
    /// What a drawn blade does to a room full of people, exactly once.
    void noteEscalation(std::int32_t targetId);

    SystemId id_;
    const TileQuery* tiles_;
    RegionPath path_;
    DialogueDirector dialogue_;
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
    /// -1 when nothing has been argued down. Set by a struck haggle and spent
    /// by the purchase that follows it.
    std::int32_t negotiatedDrink_ = -1;
    std::int32_t negotiatedRoom_ = -1;
    /// Which actor the open conversation is with, or -1.
    std::int32_t talkingToId_ = -1;
    /// Set once when a blade is drawn, so the room reacts to it exactly once
    /// rather than every second the classifier keeps saying "lethal".
    bool escalationSeen_ = false;

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

    // --- S5 -----------------------------------------------------------------
    /// How many bales are still in the snug tonight. Restocked when the doors
    /// open, exactly like the cellar -- WITHOUT it, the run is a coin faucet: a
    /// bale delivered leaves the snug empty of nothing, so a player can carry
    /// the same bale out of the same door until the guild loves them. A boat
    /// brings what a boat brings.
    std::int32_t balesInSnug_ = kBalesPerNight;
    /// And what is in them tonight.
    Contraband baleGood_ = Contraband::Moonshine;
    /// Bit i is set once room i's strongbox has been emptied.
    std::int32_t crackedBoxes_ = 0;
    /// The highest band the player has stood on. See settleLanding.
    std::int32_t highestBand_ = 0;
    // --- S6 -----------------------------------------------------------------
    /// Bit i is set once tonight's i-th rat has been skinned.
    std::int32_t scalpedVermin_ = 0;
    WatchStance watchStance_ = WatchStance::Idle;
    WatchCause watchCause_ = WatchCause::None;
    std::int32_t watchmanId_ = -1;
    std::int64_t noticedAtTick_ = -1;
    std::string lastDemand_;
    ArrestReport lastArrest_;
    bool arrestRelease_ = false;
    /// The second-of-day this room was constructed at, so dayNumber() can be
    /// monotonic without the wall clock's midnight in it.
    std::int32_t startedAt_ = 0;
    /// Whether the player was inside the walls on the previous movement step.
    /// A bale is DELIVERED by crossing the threshold with it, and a crossing is
    /// a transition and not a state.
    bool wasInside_ = false;
    /// Simulated seconds this room has been running, INCLUDING the ones a
    /// skipTo jumped. The clock on the wall wraps at midnight and the Watch's
    /// memory does not, so heat is charged against this and not timeOfDay_.
    std::int64_t elapsed_ = 0;
};

}  // namespace granadad::sim
