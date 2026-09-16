#pragma once

// THE WARD, WITH PEOPLE IN IT.
//
// What this is, and what it is not.
//
// sim/actor.hpp is one actor: a name, a post, an hour, a Q8 position. The
// Tavern owns seventeen of them and they are the only bodies in the district.
// compound.hpp owns the ward's HOUSEHOLDS -- and says so out loud in its own
// header: "there is no walking here... no actor in the Gilded Gull is one of
// these households". So the ward has a roll of who owns what, a taproom with
// people in it, and between the two a district of eight thousand walkable tiles
// with nobody on them. The owner played it and said so: "there were no people,
// even at night there should be people like guards urchins thieves taverns".
//
// This is the population. It is the ONE thing the Java actor package has that
// this build did not: a district full of bodies with needs, homes, jobs and a
// daily rhythm.
//
// IT EXTENDS, IT DOES NOT COMPETE. The Gilded Gull's staff and patrons are the
// Tavern's and are spawned by nobody here -- Tavern::actors() is still the only
// place they live, and WardPopulation refuses to put a home, an anchor or a
// wander target inside the Gull's footprint. The ward's own drinkers keep the
// other two houses, K04 The Bilge and K05 The Lantern Room, which the Tavern
// system has never staffed. Session merges the two rosters at draw time and the
// report prints both, so "the ward's roll" is one number with two sources
// rather than two populations arguing over the same street.
//
// NO FLOATS. Positions are whole tiles, needs are integers, decay is an
// integer accumulator. The renderer interpolates between the tile an actor was
// on last tick and the tile it is on now; that interpolation is the renderer's
// and never comes back.
//
// -------------------------------------------------------------------------
// THE CLOCK, AND WHY EVERY JAVA NUMBER IS MULTIPLIED
// -------------------------------------------------------------------------
// The Java's day is 24,000 ticks. This engine's day is 86,400 -- one tick is
// one second and the clock on the wall is a real clock. So every rate ported
// from content/raws/actors/*.json is rescaled by 24000/86400 at load, once, in
// one function, and every duration is stated in SECONDS here rather than in
// Java ticks. A decay of 1000 per kilotick in the raws drains a reserve by
// 24,000 a day; it drains it by 24,000 a day here too, and the number in this
// header is 278 because a day is longer.
//
// Getting that wrong is not a balance issue, it is a starving ward: at the raw
// numbers unscaled a serf would burn 86,400 points of hunger a day against a
// 10,000 reserve and three meals.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/watch.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// who is in the ward
// ---------------------------------------------------------------------------

/// The kinds of body the district holds.
///
/// SIXTEEN, and the count is a readability decision rather than a taxonomy.
/// The owner's complaint named "guards urchins thieves taverns"; a type is
/// justified when a player can tell it from its neighbours across a dark
/// street. Behaviour comes from the JOB, not from here -- a type carries a
/// look, a walking speed, a leash and one row of the owner's need raws.
enum class WardType : std::uint8_t {
    /// The ward's labour: warehouse crews, rope-walk hands, gutting sheds.
    Serf = 0,
    /// Keeps a counter. One per establishment, and the ward's small money.
    Shopkeeper = 1,
    /// Crews the three hulls on the Long Quay and sleeps aboard.
    Sailor = 2,
    /// Works the strand, the fingers and the dawn auction.
    Fisher = 3,
    /// Moves goods between the stands on a fixed round.
    Carter = 4,
    /// The state's only ambient voice: post, beat, and a night roster.
    MilitiaWatch = 5,
    /// The ward's poor. Not a criminal -- a person with no work today.
    Wastrel = 6,
    /// A street child. Small, fast, sleeps rough, works the bins.
    Urchin = 7,
    /// Cutpurse, robber, roof-runner. Comes out when the Watch thins.
    Thief = 8,
    /// Father Maell's office: exactly one, at the Mission.
    PriestOfTheFlame = 9,
    /// The white garb the ward distrusts, and the almshouse that runs on it.
    DiscipleOfTheFlame = 10,
    /// Rat-catchers, drovers, the impound yard's dog handler.
    AnimalKeeper = 11,
    /// Owned: a keeper's dog, and the goats in the Gallows Row pen.
    Dog = 12,
    /// Unowned, and nobody feeds it. The quay's own stray.
    ///
    /// NOT A GULL, and the correction is worth recording: the Java package's
    /// `feral` type is a bird in its comments and a FERAL DOG in the owner's
    /// own art (content/art/sprites, `actor_feral_dog_0`, tagged
    /// actor/beast/vermin, which is exactly what the index's `feral` query
    /// asks for). The art is canon and the comment was not.
    Stray = 13,
    /// Eight of them, and they keep the mice down where the Watch does not.
    Cat = 14,
    /// Prey. Bins, dens, and the bottom of the ward's one food chain.
    Mouse = 15,
};

inline constexpr std::size_t kWardTypeCount = 16;

[[nodiscard]] std::string_view wardTypeName(WardType type) noexcept;

/// The file under content/raws/actors this type reads its needs out of.
///
/// SEVERAL TYPES SHARE A FILE and that is deliberate: a sailor and a rope-walk
/// hand have the same appetite and the same bedtime, and inventing a
/// sailor.json nobody authored would be this build writing its own canon. All
/// eleven authored files are read and none is unread, which a case pins in both
/// directions.
[[nodiscard]] std::string_view wardTypeRawsId(WardType type) noexcept;

/// Whether this type is a person. Beasts are not counted in the ward's roll of
/// SOULS, are not fed by the food economy, and are not what a player means when
/// they say the street is empty.
[[nodiscard]] constexpr bool isPerson(WardType type) noexcept {
    return type < WardType::Dog;
}

/// WHETHER THIS KIND OF BODY WILL GO UP A WALL.
///
/// #80, and it is the one thing the population round said it did not have:
/// "ward actors have no climb verb, so nobody is homed where they cannot walk;
/// the Gullet's thieves keep ground-level condos." The faction is called the
/// SKYRUNNERS. Their territory was empty.
///
/// NOT EVERYBODY, AND THE EXCLUSION IS CANON RATHER THAN CONVENIENCE.
/// DOCKS-GAZETTEER section 2.5 rules that rooftops are unseemly for every
/// Trojian except a presented Wielder -- which is exactly WHY the poor live on
/// them and why burglars use the roof-slum deck as a highway. Section 2.6 keeps
/// the roof-slums "outside the law" on the same rule. So the list is the ward's
/// poor and the ward's beasts, and a watchman in a coat of plates does not go
/// up the Gullet's wall after a cutpurse. That is not a gap; it is the reason
/// the Gullet is the Gullet.
///
/// A shopkeeper, a priest, a sailor and the Watch all walk. They always did.
[[nodiscard]] constexpr bool wardTypeClimbs(WardType type) noexcept {
    return type == WardType::Wastrel || type == WardType::Urchin ||
           type == WardType::Thief || type == WardType::Cat || type == WardType::Stray;
}

/// The ward's predators, and its one prey.
///
/// #80. The Java build's `feral` row is captioned "Harbor Gull" and this build
/// wears it on the Stray, because the owner's own art for the `feral` query is
/// a feral DOG (actor_feral_dog_0, tagged actor/beast/vermin) and the art is
/// canon where a comment is not -- the note on WardType::Stray records that
/// correction. There is no bird in content/art/sprites to hang a seventeenth
/// type on, so the gull's BEHAVIOUR (the long leash of feral.json, radius 24,
/// and the hunt) lands on the body the district actually draws.
[[nodiscard]] constexpr bool isPredator(WardType type) noexcept {
    return type == WardType::Cat || type == WardType::Stray;
}

[[nodiscard]] constexpr bool isPrey(WardType type) noexcept {
    return type == WardType::Mouse;
}

/// WHO STANDS THEIR GROUND WHEN THE STREET GOES BAD, instead of running.
///
/// STREET SENSES (9a completion). The gazetteer's own crowd ladder splits the
/// district in two when a fright crosses it: "Serfs flee -> Shopkeepers
/// bucket-chain -> ... -> Priest walks in" (DOCKS-GAZETTEER section 4.2's fire
/// scenario, and the deference table's textures -- a serf "may flee", a
/// shopkeeper brings the ledger out, the priest has claims ON the ward). The
/// roadmap's reaction table put it plainly: "Serfs and wastrels flee,
/// shopkeepers and priests cower." So a shopkeeper, a priest and a disciple
/// COWER -- frightened but standing, facing the trouble -- where a serf, a
/// sailor, a fisher, a carter, a wastrel, an urchin or a thief FLEE. The Watch
/// is neither: it holds by exemption (never alarmed), which is a different rule
/// (9b's, the street Watch's Respond) and not this one.
[[nodiscard]] constexpr bool wardTypeCowers(WardType type) noexcept {
    return type == WardType::Shopkeeper || type == WardType::PriestOfTheFlame ||
           type == WardType::DiscipleOfTheFlame;
}

/// WHO SWINGS BACK WHEN STRUCK, instead of routing.
///
/// STREET SENSES leg (b). The lane brief's own line: "a struck body that is
/// not a professional routs; a Thief or a Sailor swings back through the same
/// brawl rules; no second ladder." A sailor off a Long Quay hull and a
/// cutpurse who works the dark are the ward's two trades that answer a fist
/// with a fist. Everybody else who is struck panics and breaks away (the leg
/// (a) flee plan, deeper); the Watch is neither -- it holds, and what it does
/// instead is leg (c)'s.
[[nodiscard]] constexpr bool wardTypeFightsBack(WardType type) noexcept {
    // STREET SENSES leg (c): and the Watch. A blow on a watchman makes him a
    // brawler (D6, the Gull's own rule for a blow on the closing Cull) -- he
    // fights, he does not arrest, and his blows can kill (the client lands
    // them with Intent::Kill, Lethal by B2). Killing him is murder like
    // anybody else's.
    return type == WardType::Sailor || type == WardType::Thief || type == WardType::MilitiaWatch;
}

// ---------------------------------------------------------------------------
// what somebody needs
// ---------------------------------------------------------------------------

/// Five, in Maslow order, and the order is the array order.
///
/// RESERVE SEMANTICS: high is satisfied, 0 is desperate, 10,000 is full. Every
/// comparison in this file reads "below" as "wants", which is the opposite of
/// the intuition people bring to the word "hunger" and is worth saying once.
enum class Need : std::uint8_t {
    Hunger = 0,
    Rest = 1,
    Coin = 2,
    Safety = 3,
    Duty = 4,
};

inline constexpr std::size_t kNeedCount = 5;

inline constexpr std::int32_t kNeedMax = 10000;
inline constexpr std::int32_t kNeedLow = 3000;
inline constexpr std::int32_t kNeedCritical = 1000;

/// THE HYSTERESIS, and it is the single most important number here.
///
/// A need policy keeps winning until the reserve climbs back to this, not
/// merely until it stops being LOW. Without it one recovery tick nudges the
/// reserve above kNeedLow, the need policy's score drops to zero, the job wins
/// the same tick and walks the actor straight back out of the room it came in
/// to eat in -- forever, in front of the player.
inline constexpr std::int32_t kNeedRecovered = 6000;

/// One need's row of the owner's raws, already rescaled to this engine's day.
///
/// BOTH RATES ARE PER KILOTICK. The raws express recovery per tick and decay
/// per kilotick; carrying two units into the engine is how a rescale gets
/// applied to one of them and not the other. See the header note on the clock.
struct NeedConfig {
    std::int32_t start = kNeedMax;
    std::int32_t decayPerKilotick = 0;
    std::int32_t recoverPerKilotick = 0;
    std::int32_t lowBonus = 0;
    std::int32_t critBonus = 0;
};

/// Everything a type is, loaded once from content/raws/actors.
struct WardTypeStats {
    NeedConfig needs[kNeedCount];
    /// Ticks between tile steps. 1 is a working walk: one tile a second, and a
    /// tile is about 0.9 m.
    std::int32_t speedTicksPerStep = 1;
    /// How far from its anchor a body will let itself be drawn.
    std::int32_t leashRadius = 30;
    std::int32_t fleePriority = 950;
    std::int32_t seekFoodPriority = 305;
    std::int32_t returnHomePriority = 305;
    std::int32_t returnHomeRhythmBonus = 80;
    std::int32_t loiterPriority = 10;
};

/// The eleven authored rows, indexed by WardType. Loads content/raws/actors and
/// falls back to a compiled copy when the tree is absent, exactly like every
/// other raws loader in this build -- a content edit must not stop the game
/// booting.
class WardTypeTable {
public:
    [[nodiscard]] static WardTypeTable load(const std::filesystem::path& contentDir);

    [[nodiscard]] const WardTypeStats& operator[](WardType type) const noexcept {
        return rows_[static_cast<std::size_t>(type)];
    }
    /// True when the authored files were actually found and read.
    [[nodiscard]] bool fromAuthoredRaws() const noexcept { return fromRaws_; }
    /// How many of the eleven files were read. Pinned by a case, because a
    /// loader that is silent about a missing file lets a whole suite pass
    /// against a fallback.
    [[nodiscard]] std::int32_t filesRead() const noexcept { return filesRead_; }

private:
    WardTypeStats rows_[kWardTypeCount];
    bool fromRaws_ = false;
    std::int32_t filesRead_ = 0;
};

// ---------------------------------------------------------------------------
// what somebody does
// ---------------------------------------------------------------------------

/// A job is a LEAF: a shape of work plus the hours it is done in. Two jobs with
/// the same shape and different hours are two jobs, because the hours are the
/// whole of what makes a district look different at two in the morning.
enum class WardJob : std::uint8_t {
    /// No work. Beasts that only wander, and the newly unemployed.
    None = 0,
    /// Stand at a post and do the work. Warehouses, sheds, counters, stalls.
    Anchor = 1,
    /// Walk a fixed loop of waypoints. The carters' round.
    Rounds = 2,
    /// Beat a route or a corner, six in the morning until six at night.
    Patrol = 3,
    /// THE SAME SHAPE, THE OTHER TWELVE HOURS, and it is a separate leaf on
    /// purpose. A night shift is a job whose only difference from a day job is
    /// its hours, and the flag that lets it out-argue bedtime has to belong to
    /// the job rather than to the actor -- see JobParams::worksThroughTheNight
    /// for why "any job whose window is open" would silently roster the ward's
    /// thieves as well.
    NightWatch = 4,
    /// Work the courtyard's own ground.
    Farm = 5,
    /// The strand, the fingers, the dawn auction.
    Fish = 6,
    /// Nowhere to be: the commons, the kerb, whoever is passing.
    Streetlife = 7,
    /// The bins, in the order a child who knows the ward walks them.
    Scavenge = 8,
    /// The other trade. Comes out when the Watch is thin and works the dark.
    Thieving = 9,
    /// Drift, and dwell. Beasts, at every hour there is.
    Wander = 10,
};

inline constexpr std::size_t kWardJobCount = 11;

[[nodiscard]] std::string_view wardJobName(WardJob job) noexcept;

/// THE JOB BAND IS 100..299 AND THE CEILING IS LOAD-BEARING ARITHMETIC.
///
/// RETURN_HOME is priced at 305. A job that could reach 305 would out-score it
/// and the actor would never go to bed. So `priority + rhythmBonus` is refused
/// above 299 at construction rather than clamped, because a clamp is a bug that
/// balances itself and a refusal is a bug that stops the build.
inline constexpr std::int32_t kJobPriorityMin = 100;
inline constexpr std::int32_t kJobPriorityMax = 299;

/// One bound job. Immutable, one per leaf, shared by every actor doing it: all
/// per-actor progress lives on the actor.
struct JobParams {
    WardJob shape = WardJob::None;
    std::int32_t priority = 100;
    /// Seconds since midnight, half-open [from, to). A window with from > to
    /// wraps midnight, which is what a night shift is.
    std::int32_t windowFromSecond = 0;
    std::int32_t windowToSecond = kSecondsPerDay;
    std::int32_t rhythmBonus = 0;
    /// Ticks of standing at the post that make one unit of work.
    std::int32_t workTicksPerUnit = 30;
    /// DUTY restored per completed unit.
    std::int32_t dutyPerUnit = 400;

    /// TRUE ONLY FOR THE NIGHT ROSTER, and it has to be opt-in per job.
    ///
    /// RETURN_HOME's night term prices going to bed at 385, above the entire
    /// job band. A rostered guard away from its bunk at two in the morning is
    /// therefore dragged home every tick and shoved back out the next -- the
    /// oscillation this flag exists to stop. It is not "any job whose window is
    /// open", because the thief's window is open at two as well and exempting
    /// the thief would silently rewrite the ward's nocturnal economy.
    bool worksThroughTheNight = false;

    [[nodiscard]] bool inWindow(std::int32_t secondOfDay) const noexcept;
};

/// The bound table, one row per leaf. Constructed once and validated: every
/// row's priority is inside the band and no row can out-score bedtime.
[[nodiscard]] const JobParams& wardJobParams(WardJob job) noexcept;

// ---------------------------------------------------------------------------
// which policy is acting
// ---------------------------------------------------------------------------

/// What an actor decided to do this tick. HASHED, because it is what the actor
/// DID -- and because a divergence isolated to a decision that produced no
/// movement would otherwise slip past the twin-run gate entirely.
enum class WardPolicy : std::uint8_t {
    Dead = 0,
    Flee = 1,
    SeekFood = 2,
    ReturnHome = 3,
    Pursue = 4,
    Loiter = 5,
    /// #80. THE BEAST FOOD CHANNEL, and it is appended rather than inserted
    /// because the ordinal is hashed: putting it anywhere else would renumber
    /// every policy the world hash has ever recorded.
    ///
    /// SEEK_FOOD is structurally unusable by a beast -- no larder, no coin, no
    /// stall will serve a cat -- which is why isPerson() gates it. Before this
    /// the ward's cats and strays therefore had no food channel at all beyond
    /// the den nibble their wander leg pays, and the mice were a population
    /// nothing ate. This is the Java build's BeastHuntPolicy, ported.
    Hunt = 6,
    /// STREET SENSES (9a completion). Frightened, but STANDING -- the shape the
    /// gazetteer's crowd ladder gives a shopkeeper and a priest ("Shopkeepers
    /// bucket-chain -> ... -> Priest walks in", DOCKS-GAZETTEER section 4.1 /
    /// 4.2's fire scenario), against the serf's and the wastrel's FLEE. Same
    /// gate as Flee (Safety under kNeedCritical), the OTHER response: face the
    /// fright and hold, rather than run from it. APPENDED, not inserted -- the
    /// ordinal is a hashed byte (hash_into's put_byte(policy)), so putting it
    /// anywhere but the end would renumber every policy the world hash has ever
    /// recorded, exactly the Hunt precedent above.
    Cower = 7,
    /// STREET SENSES leg (b). FIGHTING BACK: a struck sailor or thief closing
    /// on the player and swinging, under the brawl rules, until his clock runs
    /// out or he is floored. Wins over everything while it runs -- a man in a
    /// fight is not hungry. APPENDED for the same reason Cower was.
    Brawl = 8,
    /// STREET SENSES leg (c). THE WATCH CLOSING: a watchman who SAW cause --
    /// steel up, a blow, a killing -- walking the player down for
    /// kWatchClosingSeconds, the halt in his mouth, the arrest at reach. Below
    /// Brawl (a blow on him makes him a brawler, and he fights rather than
    /// arrests) and above everything else. APPENDED.
    Close = 9,
};

inline constexpr std::size_t kWardPolicyCount = 10;

[[nodiscard]] std::string_view wardPolicyName(WardPolicy policy) noexcept;

// ---------------------------------------------------------------------------
// one occupancy index
// ---------------------------------------------------------------------------

/// ONE BODY PER TILE. The owner's rule, verbatim: "no more stacking actors,
/// only one per square."
inline constexpr std::int32_t kMaxOccupantsPerCell = 1;

/// packed cell -> how many bodies are standing on it.
///
/// An open-addressing primitive map and NOT a dense array, because a packed
/// cell key spans the whole district times sixteen bands -- three quarters of a
/// million entries -- while at most a few hundred cells are ever occupied.
/// Two parallel primitive arrays, a Fibonacci hash, a power-of-two mask, and
/// BACKWARD-SHIFT deletion (Knuth 6.4 algorithm R) so there are no tombstones
/// and the table never depends on the order things were removed in.
class OccupancyIndex {
public:
    OccupancyIndex() { reset(64); }

    void clear() noexcept;
    [[nodiscard]] std::int32_t at(std::uint32_t cell) const noexcept;

    /// WHO is standing there, or -1.
    ///
    /// The index carries the occupant and not only the count, and it is worth
    /// saying why: the shove needs to know who it is shoving, and looking that
    /// up by walking the roster is O(N) inside a step that already happens once
    /// per body per tick. Six hundred and seventy-eight bodies is four hundred
    /// and sixty thousand comparisons a tick, which turns a build somebody runs
    /// into a build somebody starts and goes away from. Under the one-per-cell
    /// cap there is exactly one occupant, so this is a field and not a list.
    [[nodiscard]] std::int32_t occupantAt(std::uint32_t cell) const noexcept;

    void add(std::uint32_t cell, std::int32_t actorId);
    void remove(std::uint32_t cell) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    void reset(std::size_t capacity);
    void grow();
    [[nodiscard]] std::size_t slotOf(std::uint32_t cell) const noexcept;

    std::vector<std::uint32_t> keys_;
    std::vector<std::int16_t> counts_;
    std::vector<std::int32_t> owners_;
    std::size_t mask_ = 0;
    std::size_t count_ = 0;
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;
};

// ---------------------------------------------------------------------------
// one actor
// ---------------------------------------------------------------------------

/// A body in the ward.
///
/// EVERY LATCH IS AN ABSOLUTE TICK, never a countdown. A countdown has to be
/// decremented by somebody every tick, which means a body that is skipped for
/// any reason silently keeps its latch forever; an absolute deadline is correct
/// whether or not anybody looked at it.
struct WardActor {
    std::int32_t id = 0;
    WardType type = WardType::Serf;
    WardJob job = WardJob::None;

    /// Where the body is, in whole tiles.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    /// Where it was at the end of last tick. The renderer interpolates between
    /// the two and never writes either.
    std::int32_t prevX = 0;
    std::int32_t prevY = 0;
    std::int32_t prevBand = 0;
    Angle facing = 0;

    std::int16_t needs[kNeedCount] = {0, 0, 0, 0, 0};
    std::int32_t needAccum[kNeedCount] = {0, 0, 0, 0, 0};

    /// The bed. Every actor has one; a wastrel's is a doorway.
    std::int32_t homeX = 0;
    std::int32_t homeY = 0;
    std::int32_t homeBand = 0;
    /// The post. Where the job is done.
    std::int32_t anchorX = 0;
    std::int32_t anchorY = 0;
    std::int32_t anchorBand = 0;

    /// Where the body is walking, right now.
    std::int32_t targetX = 0;
    std::int32_t targetY = 0;
    std::int32_t targetBand = 0;

    /// Which leg of a round or a beat is being walked.
    std::int16_t leg = 0;
    /// Ticks of work banked toward the next unit, or ticks spent failing to
    /// make ground. Both budgets ride this one scalar; see the note on
    /// kPatrolBlockedYieldAttempts.
    std::int32_t goalWorkTicks = 0;
    /// The closest this leg has come to its target. Stored as distance + 1, so
    /// zero means "no mark yet".
    std::int32_t legMark = 0;

    std::int32_t moveAccumTicks = 0;
    /// The tick a failed route search may be retried on.
    std::int64_t routeRetryUntil = 0;
    /// And the tick a failed LEG DRAW may be retried on, which is a different
    /// failure with the same shape.
    ///
    /// advanceLeg asks for a corner up to eight times and every ask is a
    /// snapToStandable over seventy-five cells. A body that cannot be given a
    /// leg at all -- a thief standing on its own roof anchor, whose drawn
    /// corners all land on ground the snap refuses -- was asking that question
    /// six hundred times a second, every second, forever. Measured: the case
    /// that ticks the ward for twenty minutes at one in the morning went from
    /// seconds to fifty of them.
    std::int64_t legRetryUntil = 0;
    std::int64_t lastPushTick = -1000000;
    /// The tick the body first read zero hunger, or -1.
    std::int64_t starvingSince = -1;

    /// Meals in the sack, and the small money.
    std::int32_t rations = 0;
    std::int32_t coin = 0;

    /// --- #80: the hunt lock ------------------------------------------------
    ///
    /// Four scalars, and every one of them is a bug the Java build paid for.

    /// The prey this predator has committed to, or -1. A LOCK and not a
    /// preference: a hunt is never abandoned mid-chase for a nearer mouse,
    /// which is what stops two predators trading one prey back and forth.
    std::int32_t huntTarget = -1;
    /// Ticks spent under the current lock. THE BUDGET COUNTS TOTAL TICKS and
    /// deliberately not only blocked ones -- the second futility class the Java
    /// soak found was a gull ORBITING a mouse in an enclosed pocket, committing
    /// a step every tick and never reaching contact, which a blocked-tick
    /// counter reads as a healthy chase forever.
    std::int32_t huntTicks = 0;
    /// The tick a predator may acquire a NEW lock on. Without this the very
    /// next sense cadence re-locks the same doomed prey ten ticks later and the
    /// wander never gets a window wide enough to change the situation -- which
    /// starves the beast through a policy that could never feed it.
    std::int64_t huntBackoffUntil = 0;
    /// For PREY: the tick this body stands up again, or -1 for a body that is
    /// up. A caught mouse is not killed, it is taken off the board -- see
    /// kPreyReviveSeconds for why the revive is a population abstraction and
    /// not a resurrection.
    std::int64_t downedUntil = -1;

    /// TRUE WHEN THIS BODY'S BED IS NOT ON THE WARD'S WALKING ISLAND -- a roof
    /// hut, reached by climbing and by nothing else. Baked once, never written
    /// again, and it is how "somebody actually lives on the roofs" is a number
    /// rather than a claim.
    bool homeOnTheRoof = false;

    WardPolicy policy = WardPolicy::Loiter;
    bool dead = false;

    /// --- STREET SENSES leg (b): the combat sheet ---------------------------
    ///
    /// THE MINIMAL SHEET, and every field is a hashed one: hit points, the
    /// brawl floor (downedUntil, REUSED -- the prey's revive latch is the same
    /// absolute-tick shape a struck man's floor needs), death by violence, and
    /// the two scalars a body that swings back keeps. kActorHealth for
    /// everybody, the Gull's own number; a docker and a Gull patron are the
    /// same twenty-four points under the same strike(). Beasts carry the sheet
    /// and are never on the ray (persons only in v1, said out loud).
    std::int16_t hp = static_cast<std::int16_t>(kActorHealth);
    /// DEAD BY VIOLENCE. Sets `dead` as well (the tile is freed, the policy is
    /// Dead, the body never ticks again -- starvation's own machinery), and
    /// this bit says WHY, so the census and the murder law can tell a killing
    /// from a famine. Never cleared: a corpse is a Downed that never stands.
    bool slain = false;
    /// A body that FIGHTS BACK keeps swinging until this absolute tick (0 = not
    /// fighting). Set on a blow landing on a wardTypeFightsBack type; cleared
    /// by the clock, a floor, or a death.
    std::int64_t fightUntil = 0;
    /// Its own monotonic swing-draw sequence: ONE draw per street swing, keyed
    /// on the actor (context.draw(actorId, swingSeq)), exactly the shape the
    /// Gull's npcSwingSeq_ keeps -- order-independent across bodies, no shared
    /// index, no new stream.
    std::int32_t swingSeq = 0;

    /// --- STREET SENSES leg (c): the Watch's eyes ---------------------------
    ///
    /// For a MilitiaWatch body only, and every field an absolute tick or a
    /// byte, hashed. The Gull's tickWatch keeps the same three facts on the
    /// room (noticedAtTick_, watchCause_, the stance); here they live on the
    /// watchman, since there are thirteen of him.
    ///
    /// THE 12 s GIVE-UP LATCH: he is Closing while this is ahead of the clock
    /// (kWatchClosingSeconds from the cause he saw, refreshed by a cause seen
    /// again); out of his sight is out of it at once (actClose). 0 = not
    /// closing.
    std::int64_t closingUntil = 0;
    /// WHY: 0 none, else 1 + AlarmSeverity (Steel 1, Blow 2, Kill 3). A blow
    /// or a killing is arrested at reach; steel alone is a DEMAND with a
    /// grace.
    std::uint8_t closeCause = 0;
    /// THE SHEATHE GRACE (D5, kSheatheGraceSeconds): a blade seen up buys the
    /// player this long to put it away. Past it with the blade still out and
    /// it is an Offence (heat, no arrest by itself), fired once. 0 = none.
    std::int64_t sheatheBy = 0;

    /// DERIVED AND NOT HASHED: the cached route and where along it the body is.
    /// Reproducible from (position, target, world) by construction -- the
    /// search is pure -- so hashing it would only pin an optimisation.
    std::vector<PathStep> route;
    std::int32_t routeIndex = 0;
    std::int32_t routeTargetX = -1;
    std::int32_t routeTargetY = -1;
    std::int32_t routeTargetBand = -1;

    [[nodiscard]] std::int32_t need(Need which) const noexcept {
        return needs[static_cast<std::size_t>(which)];
    }
    /// Whether this body is on the board at all: not starved, and not a mouse
    /// currently in a predator's stomach.
    ///
    /// A DOWNED BODY HOLDS NO TILE, exactly like a corpse -- the same rule and
    /// for the same reason. A caught mouse that kept its square would seal a
    /// doorway or a den mouth for three hours of ward time with nothing on it
    /// for anyone to see, which is the one way being eaten could go on hurting
    /// a street after the mouse is gone.
    [[nodiscard]] bool visible() const noexcept { return !dead && downedUntil < 0; }
    /// STREET SENSES leg (b). A PERSON LYING ON THE STREET -- struck down on
    /// the brawl floor, or slain -- who is off the board (holds no tile, sees
    /// nothing, is nobody's target) and is DRAWN FLAT where he fell. The one
    /// predicate the renderer reads beside visible(); a caught mouse is off
    /// the board and NOT drawn (it is in a stomach), and a starved body is
    /// gone the way it always was.
    [[nodiscard]] bool floored() const noexcept {
        return isPerson(type) && (slain || (!dead && downedUntil >= 0));
    }
    /// Bloodied at or under a quarter of the sheet, the Gull's own line.
    [[nodiscard]] bool bloodied() const noexcept {
        return isBloodied(static_cast<std::int32_t>(hp), kActorHealth);
    }
    /// Home is a ROOM, not a bed, and the difference is load-bearing.
    ///
    /// A household is one to five people and one home cell, and only one body
    /// can stand on a cell. Asking for the exact tile would mean that in a
    /// family of five, four of them never recover a point of rest, never reach
    /// the larder, and spend every night of their lives one tile from their own
    /// door with RETURN_HOME winning at 385 -- a ward permanently walking home
    /// and never arriving. One tile out, on the same storey, is the room.
    [[nodiscard]] bool atHome() const noexcept {
        return band == homeBand && (x - homeX <= 1) && (homeX - x <= 1) && (y - homeY <= 1) &&
               (homeY - y <= 1);
    }
};

// ---------------------------------------------------------------------------
// who somebody is
// ---------------------------------------------------------------------------

/// A name, and whether the owner's raws already wrote it.
///
/// #79. THE WARD HAD NO NAMES, and that is why `E` reached fourteen people.
/// A body with a job, a bed and a shift is still not somebody you can talk to
/// until it has a name to put over the conversation -- and the district already
/// had six hundred and sixty-one of the first and none of the second.
///
/// KEPT OUT OF WardActor ON PURPOSE. Everything in that struct is read by a
/// policy every tick and is folded into the world hash; three std::strings per
/// body would sit in the hot loop being copied by nothing. An identity is baked
/// once, never changes, and is read only when somebody is spoken to.
struct WardIdentity {
    /// A content/raws/names/notables.json id, or empty. Twenty-nine of the
    /// Forty land here: the roster already claims a keeper for their authored
    /// site, so Crell IS the body standing in the Weighhouse rather than a
    /// second Crell invented beside him.
    std::string notableId;
    /// What goes over the conversation. Drawn from the authored pools in
    /// content/raws/names/names.json for everybody the raws never named.
    std::string name;
    std::string epithet;
};

// ---------------------------------------------------------------------------
// the ward's own numbers
// ---------------------------------------------------------------------------

/// A meal. One ration, and it is most of a reserve.
inline constexpr std::int32_t kEatRestore = 8000;
/// Rest recovered per kilotick standing on the home cell. Eight hours refills a
/// reserve from empty with room to spare, which is what makes one night enough.
inline constexpr std::int32_t kRestRecoveredPerKilotickAtHome = 420;
/// Hunger at zero for this long and the body is dead. Three days, and it is
/// deliberately long: starving is a slow visible failure of the food economy,
/// not a punishment for one missed meal.
inline constexpr std::int64_t kStarvationGraceSeconds = 3 * kSecondsPerDay;

/// A shove's own clock. Ten Java ticks is thirty-six seconds.
inline constexpr std::int32_t kPushCooldownTicks = 36;
/// And the pushee's, deliberately much shorter than the pusher's. At the full
/// cooldown a saturated room has collective supremacy over one trapped body:
/// six parked residents each shove once per cooldown and reset the victim's
/// clock on every bounce, so it never banks the quiet ticks to shove its own
/// way out.
inline constexpr std::int32_t kPusheeStaggerTicks = 7;

/// Refused step ATTEMPTS before a beat gives up on its corner and picks
/// another. Attempts and not ticks: at a speed of one step per two ticks a
/// stillness counter charges the cadence's own no-move beat.
inline constexpr std::int32_t kPatrolBlockedYieldAttempts = 20;
/// Steps that LAND without closing on the target before the same thing happens.
/// A live-lock is not a dead-lock and needs its own clock: two guards meeting
/// head-on in a one-wide connector each replan through the other every tick and
/// shuffle forever, and a stillness counter reads that as healthy.
inline constexpr std::int32_t kPatrolNoProgressYieldSteps = 100;
/// So a refused attempt costs five units and a landed-but-no-closer step one.
inline constexpr std::int32_t kPatrolStallWeight =
    kPatrolNoProgressYieldSteps / kPatrolBlockedYieldAttempts;

/// Chebyshev distance from a post that still counts as being at it.
///
/// UNDER ONE-PER-SQUARE A CREW CANNOT SHARE A POST. Ten hands at a single-cell
/// anchor means one of them works and nine ring it all shift, complete no
/// units, earn no duty and churn shoves. Working within two of the post counts,
/// and a body already in reach stops walking.
inline constexpr std::int32_t kWorkReach = 2;

/// Ticks a failed route search waits before being tried again. A body that
/// cannot get somewhere must not re-run an expensive failed search every tick.
inline constexpr std::int32_t kRouteRetryCooldownTicks = 300;

/// And ticks a failed LEG DRAW waits. Shorter than the route cooldown because
/// the draws are keyed on the tick, so a minute later is a genuinely different
/// question rather than the same one asked again. See WardActor::legRetryUntil
/// for the fifty seconds this is worth.
inline constexpr std::int32_t kLegRetryCooldownTicks = 60;

// ---------------------------------------------------------------------------
// #80: the food chain
// ---------------------------------------------------------------------------
//
// Ported from the Java build's BeastHuntPolicy, whose javadoc is a list of the
// bugs it exists to prevent. Every number below carries the one it closes.
//
// AND IT IS CHEAP ON PURPOSE. Six hundred and seventy-eight bodies already tick
// every second; an all-pairs predator scan would be half a million comparisons
// a tick. There are two reasons this is not that. The prey are a CONTIGUOUS ID
// RANGE -- the mice are spawned last, after every person and every other beast,
// and section 6 of the roster says so out loud -- so a probe walks thirty-two
// ids and not six hundred and seventy-eight. And a probe only runs at all for a
// predator that is hungry, unlocked, off backoff and standing on a sense
// boundary. The measured worst case is thirteen predators times thirty-two mice
// once every ten ticks: forty-two comparisons a tick, against the ward's own
// six hundred and seventy-eight policy evaluations.

/// Sense-probe cadence. A predator acquires only on ticks divisible by this.
inline constexpr std::int32_t kSensePeriodTicks = 10;
/// Same-band Chebyshev radius a predator can smell a mouse at.
inline constexpr std::int32_t kSenseRadius = 24;
/// Contact distance. ADJACENCY AND NOT THE CELL, because one-per-square means
/// the predator can never stand where the prey is standing.
inline constexpr std::int32_t kContactRadius = 1;
/// A locked prey that got this far away is gone. A defensive bound on a stale
/// lock, never the ordinary way a chase ends.
inline constexpr std::int32_t kLoseRadius = 2 * kSenseRadius;
/// Ticks under one lock before the chase is declared FUTILE and dropped. A real
/// chase closes from the sense radius in about fifty; a chase that has run this
/// long is one of the Java soak's two futility classes -- the chokepoint freeze
/// (the route exists, its first hop is plugged by parked bodies, so the
/// predator "chases" in place) or the untouchable-prey orbit (the prey sits in
/// an enclosed pocket, steps keep committing, and contact never lands).
inline constexpr std::int32_t kChaseBudgetTicks = 100;
/// And no new lock for this long afterwards, so the wander gets a window wide
/// enough to walk the beast somewhere else. Without it the next sense cadence
/// re-locks the same doomed mouse ten ticks later, forever.
inline constexpr std::int32_t kHuntBackoffTicks = 500;
/// How long a caught mouse is off the board.
///
/// AN EIGHTH OF A DAY, which is the Java's 3,000 of a 24,000-tick day carried
/// across to this engine's 86,400. The revive is a POPULATION abstraction and
/// not a resurrection: the mouse that stands up in the den is a fresh mouse out
/// of the den, which is why it stands up hungry-free and why the den, rather
/// than the individual, is what the ecology is about.
inline constexpr std::int64_t kPreyReviveSeconds = kSecondsPerDay / 8;
/// THE MOST A SCAVENGED SCRAP WILL EVER PUT IN A PREDATOR, and this one
/// constant is what makes the food chain load-bearing rather than decorative.
///
/// The wander leg pays a DEN NIBBLE on every arrival (actPursue) and a leg is
/// three tiles, so a beast arrives every few seconds and the nibble is worth
/// hundreds of points a tick against a decay of a quarter of one. Measured
/// against this engine's clock rather than the Java's, that is a cat which is
/// permanently full -- and a permanently full cat never hunts, so a hunt shipped
/// beside it would be dead code that a soak could not tell from a working
/// ecology. THE MICE WERE SAFE BECAUSE THE CATS WERE NEVER HUNGRY.
///
/// So a predator's scrap tops it up only to HERE, which is under kNeedLow. A
/// predator that cannot reach prey therefore hovers permanently hungry and
/// permanently looking, and never starves; a predator that catches something
/// goes to full and is out of the hunt for the seven hours that takes to drain.
/// The catch is the only thing that FEEDS a predator, and the scrap is the only
/// thing that keeps one alive when there is nothing to catch.
///
/// PREY AND LIVESTOCK ARE UNCHANGED: a mouse lives on bin scraps and a goat on
/// the pen, and both still fill right up. The bottom of a food chain is not
/// supposed to be hungry.
inline constexpr std::int32_t kScavengeCeiling = 2500;
static_assert(kScavengeCeiling < kNeedLow,
              "a scrap that lifted a predator out of the hunger band would switch the hunt off");

/// Within this of a live lock a mouse knows about it and runs. Its SAFETY is
/// driven to nothing, its own FLEE fires at 950 next tick, and it recovers over
/// about a hundred and fifty ticks -- so a mouse runs while it is being chased
/// and settles when it is not.
inline constexpr std::int32_t kPreyPanicRadius = 6;

// ---------------------------------------------------------------------------
// STREET PANIC (feel/build, 9a): the crowd reacts to violence it can see
// ---------------------------------------------------------------------------
//
// Eli, 2026-09-10: "I'd expect the watch and crowd to react appropriately to
// the violence they're witnessing." WardPolicy::Flee was built, hashed and
// DEAD for people: every authored row starts Safety at 9,000 or 10,000 with a
// decay of 0, and until this build the only thing that ever lowered it was a
// cat closing on a mouse (actHunt, kPreyPanicRadius above). alarm() is the
// trigger the channel was waiting for, and everything under it is the needs
// machinery that already existed: FLEE fires at the raws' own priority, the
// reserve climbs back, and NOTHING NEW IS HASHED -- needs[3] and needAccum[3]
// have been in hash_into since the port.
//
// WHO PANICS: the three-clause witness rule the taproom's spreadWitness keeps,
// applied draw-free -- SAME BAND (somebody one floor up is not on the street),
// WITHIN RANGE (Chebyshev, the metric witnessesAround already uses) and LINE
// OF SIGHT through TileQuery::lineOfSight (the Gull is roofed and walled, so a
// killing at the bar reaches the street only through the door). People only,
// as witnessesAround counts them -- and NOT THE WATCH. A watchman who runs from
// a drawn knife is the wrong feel; what he does instead (Respond, 9b) sequences
// after the justice build, and until then he holds, as the Gull's bouncers hold
// the door and never rout (Tavern::isProfessional, the same rule).
//
// THE TUNING IS CODE, NOT RAWS. The raws' own Safety recovery is 2-4 a tick in
// Java ticks, which this engine's day rescales to about half a point a second:
// a serf driven to nothing would run for half an hour. That rate is right for a
// mouse (mouse.json says 25 and the chase is tuned to it) and wrong for a
// street, and COMBAT-ACTION-SPEC.md section 1 forbids content edits -- so the
// panic rate is a constant here, and moves to the raws when the actor files
// are next opened (DECISIONS.md, "Oblivion feel: street panic").

/// How bad what they saw was. Picks the radius the alarm carries and how far
/// the reserve is driven down: a blade makes the nearby give you room for
/// half a minute; a killing empties the street for the better part of two.
enum class AlarmSeverity : std::uint8_t {
    /// Steel in a raised hand, or the hands up over a floored man.
    Steel = 0,
    /// A blow landed on somebody under lethal rules.
    Blow = 1,
    /// A killing. A body that will not get up.
    Kill = 2,
};

/// How far each severity carries, in tiles. Six is across a street; twelve is
/// a stretch of it; twenty-four is the predators' own sense radius, the
/// furthest anybody in this district notices anything.
inline constexpr std::int32_t kAlarmRadiusSteel = 6;
inline constexpr std::int32_t kAlarmRadiusBlow = 12;
inline constexpr std::int32_t kAlarmRadiusKill = 24;

/// Where the reserve is driven to, per severity. Every one is under
/// kNeedCritical, so FLEE fires next tick for everybody alarmed; the depth is
/// how long they run before the gate lets them stop.
inline constexpr std::int32_t kPanicSafetySteel = 700;
inline constexpr std::int32_t kPanicSafetyBlow = 250;
inline constexpr std::int32_t kPanicSafetyKill = 0;

/// Extra Safety a FRIGHTENED person recovers per tick -- one whose reserve is
/// under kNeedCritical, which is to say one FLEE is driving -- on top of the
/// raws' own rate. Nine a second puts a serf driven to nothing back over the
/// gate in about 105 seconds (from 700 in ~31, from 250 in ~78). A person who
/// is not frightened recovers at the raws' rate exactly as before, so the
/// no-player gate run's arithmetic is untouched by construction; beasts are
/// never alarmed and keep their own rates (the mouse's chase depends on it).
inline constexpr std::int32_t kPanicRecoverPerTick = 9;

static_assert(kPanicSafetySteel < kNeedCritical && kPanicSafetyBlow < kNeedCritical &&
                  kPanicSafetyKill < kNeedCritical,
              "an alarm that does not cross the FLEE gate frightens nobody");
static_assert(kPanicSafetyKill <= kPanicSafetyBlow && kPanicSafetyBlow <= kPanicSafetySteel,
              "a killing must frighten a street at least as long as a blow, and a blow as "
              "long as a blade");
static_assert(kAlarmRadiusSteel <= kAlarmRadiusBlow && kAlarmRadiusBlow <= kAlarmRadiusKill,
              "a killing must carry at least as far as a blow, and a blow as far as a blade");

[[nodiscard]] constexpr std::int32_t alarmRadius(AlarmSeverity severity) noexcept {
    switch (severity) {
        case AlarmSeverity::Steel: return kAlarmRadiusSteel;
        case AlarmSeverity::Blow: return kAlarmRadiusBlow;
        case AlarmSeverity::Kill: return kAlarmRadiusKill;
    }
    return kAlarmRadiusKill;
}

[[nodiscard]] constexpr std::int32_t alarmFloor(AlarmSeverity severity) noexcept {
    switch (severity) {
        case AlarmSeverity::Steel: return kPanicSafetySteel;
        case AlarmSeverity::Blow: return kPanicSafetyBlow;
        case AlarmSeverity::Kill: return kPanicSafetyKill;
    }
    return kPanicSafetyKill;
}

[[nodiscard]] std::string_view alarmSeverityName(AlarmSeverity severity) noexcept;

// ---------------------------------------------------------------------------
// STREET SENSES leg (b): a body to hit
// ---------------------------------------------------------------------------
//
// The gap analysis' first finding, verbatim: "THE STREET HAS NO BODY TO HIT.
// A swing on the Tarwalk hits nobody because the sightline raycast walks the
// 17-body tavern roster only." Leg (b) puts the district's people on the ray
// and gives them the Gull's own sheet -- kActorHealth, strike(), classifyFight,
// the Subdue floor and the Lethal rules -- through the SAME roll the swing
// already owns. Nothing here draws: the sightline is the S9 draw-free
// projection, the blow's roll is the Tavern's drawForPlayerAction, and a body
// that swings back draws exactly once per swing on its own key, the Gull's
// npcSwingSeq_ shape. The tavern roster is never touched (one person, one
// roster -- SHIP-NOTE's promotion alternative is refused).

/// Seconds a struck man lies on the brawl floor before he stands. The Gull's
/// downed rule is +1 hp/s and stand at a quarter: six seconds from nothing to
/// six points, so the street gets the same six.
inline constexpr std::int64_t kStreetFloorSeconds = 6;
/// What he stands up with: a quarter of the sheet, the Gull's own line.
inline constexpr std::int32_t kStreetStandHp = kActorHealth / 4;
/// Seconds a body that FIGHTS BACK keeps swinging after the last blow it took.
/// Twenty is a fight, not a grudge: long enough to close and land, short enough
/// that a player who walks off is not followed across the district.
inline constexpr std::int64_t kStreetFightSeconds = 20;
/// Where a STRUCK non-fighter's Safety is driven: the Kill floor -- the lane
/// brief's "routs ... with a longer panic". A bystander who saw the blow runs
/// ~78 s (kPanicSafetyBlow); the man who took it runs ~105 s.
inline constexpr std::int32_t kStruckPanicFloor = kPanicSafetyKill;
/// Reach for a street swing at the player, in tiles: adjacent. The ward walks
/// in whole tiles, so the Gull's kMeleeReach (a tile and a quarter, Q8) is one
/// tile of Chebyshev here; the client re-checks the Q8 reach before the blow
/// lands (a swing thrown at a player who stepped back whiffs, the Gull's rule).
inline constexpr std::int32_t kStreetReachTiles = 1;

static_assert(kStreetStandHp > 0, "a man cannot stand up dead");
static_assert(kStruckPanicFloor <= kPanicSafetyBlow,
              "the man who took the blow must run at least as long as the man who saw it");

/// One blow a fighting-back body threw AT THE PLAYER this tick: who, and the
/// ONE roll he drew for it. The client resolves it on the player's sheet
/// through the Gull's own player-side rules (Tavern::takeStreetBlow) -- the
/// population never sees the player's hit points, and the Gull never sees
/// the ward's draw stream.
struct StreetBlow {
    std::int32_t attackerId = -1;
    std::uint64_t roll = 0;
};

// ---------------------------------------------------------------------------
// STREET SENSES leg (c): the Watch on the beats gets eyes
// ---------------------------------------------------------------------------
//
// The gap analysis' third finding: "THE STREET WATCH IS SCENERY. Thirteen
// MilitiaWatch WardActors by day and seven by night walk beats with no eyes."
// Leg (c) gives them the Gull's own Watch, on the street's terms: a watchman
// who SEES cause (the same three-clause notice rule, at kWatchSightTiles --
// the number the Gull's canSeePlayer and the room's witnessCount are held to
// by static_assert) goes Closing, walks the player down with the halt in his
// mouth, gives it up at kWatchClosingSeconds or the moment the player is out
// of his sight (the Gull's own two rules, the roofs' whole counterplay), and
// ARRESTS AT REACH through the ONE seam the Gull's Cull uses (the client
// lands it: Tavern::arrestByStreetWatch -> CrimeLedger::charge /
// seizeAtArrest / openHearing, TAKEN TO THE MISSION, the hearing). Steel seen
// up is a DEMAND first (SHEATHE IT, kSheatheGraceSeconds), and only an
// Offence (kSheatheOffenceHeat, no arrest by itself) if it stays out -- D5.
// Deference is absolute: a presented Wielder is never closed on (D6). Nothing
// here draws.

/// Seconds a watchman gives a raised blade before it goes on the paper. D5's
/// "6 s sheathe grace".
inline constexpr std::int64_t kSheatheGraceSeconds = 6;
/// What ignoring it costs: D5's "heat +10 for ignoring it; an unsheathed blade
/// with no blow is an Offence". Heat, not paper -- ten is a sixth of a
/// warrant, and the Watch heard it.
inline constexpr std::int32_t kSheatheOffenceHeat = 10;
/// Reach for a street arrest, in tiles: adjacent, the same tile-reach a street
/// swing has (the ward walks in whole tiles).
inline constexpr std::int32_t kStreetArrestReachTiles = kStreetReachTiles;

static_assert(kSheatheGraceSeconds < kWatchClosingSeconds,
              "a blade must be given its grace before the chase it started runs out");

/// What a closing watchman did this tick, for the client to say and to land.
enum class WatchEventKind : std::uint8_t {
    /// He started Closing on a blow or a killing: the halt line.
    Halt = 0,
    /// He started Closing on steel: the SHEATHE IT demand, the grace running.
    Sheathe = 1,
    /// The grace ran out with the blade still up: an Offence, heat, no arrest.
    Offence = 2,
    /// He reached the player with a blow or a killing behind it: the arrest,
    /// landed by the client through the Gull's own seam.
    Arrest = 3,
};

struct WatchEvent {
    std::int32_t watchmanId = -1;
    WatchEventKind kind = WatchEventKind::Halt;
    /// The cause he closed on, 1 + AlarmSeverity, for the arrest's own record.
    std::uint8_t cause = 0;
};

// ---------------------------------------------------------------------------
// the system
// ---------------------------------------------------------------------------

/// What the ward looks like right now, for the report and for the acceptance.
struct WardCensus {
    std::int32_t total = 0;
    std::int32_t people = 0;
    std::int32_t beasts = 0;
    std::int32_t alive = 0;
    std::int32_t starved = 0;
    /// STREET SENSES leg (b). Dead by violence (not counted in `starved`), and
    /// persons lying on the brawl floor right now (alive, off the board).
    std::int32_t slain = 0;
    std::int32_t downed = 0;
    std::int32_t byType[kWardTypeCount] = {};
    std::int32_t byPolicy[kWardPolicyCount] = {};
    /// The labouring trades, and how many of them the ward failed to feed.
    /// THE BALANCE BAR IS WRITTEN AGAINST THESE TWO and not against the whole
    /// roll: the Java build's own bar was serf starvation at or below 5%, and a
    /// roll that included beasts would dilute it with animals the food economy
    /// was never asked to feed.
    std::int32_t serfs = 0;
    std::int32_t serfsStarved = 0;
    /// Bodies whose hunger is under kNeedLow right now.
    std::int32_t hungry = 0;
    /// Bodies standing within kWorkReach of their own post.
    std::int32_t atPost = 0;
    /// Bodies standing on their own home cell.
    std::int32_t atHome = 0;

    /// #80. WHO LIVES ON THE ROOFS, and where they are standing right now.
    ///
    /// `roofHomed` counts beds that are not on the ward's walking island --
    /// a hut you get to by climbing and by nothing else -- broken out by type,
    /// because "the roof slum is populated" and "the Skyrunners live in their
    /// own territory" are two different claims and the second one is the whole
    /// point of the faction. `onRoofNow` is how many bodies are actually up
    /// there at this instant, which is the number a night frame shows.
    std::int32_t roofHomed = 0;
    std::int32_t roofHomedByType[kWardTypeCount] = {};
    std::int32_t onRoofNow = 0;

    /// #80. The food chain, counted. `prey` is every mouse on the roll,
    /// `preyUp` is how many of them are on the board right now, and the gap is
    /// what has been eaten and has not yet come back out of the den. A prey
    /// count that only ever equals the roll is not an ecology.
    std::int32_t prey = 0;
    std::int32_t preyUp = 0;
};

/// The food and coin ledger. EXACT, and it is a gate rather than a report.
///
/// minted - consumed must equal what is held, at every tick, per kind. A
/// simulation that can quietly create or destroy a loaf will balance its own
/// economy by accident and the balance will mean nothing.
struct WardLedger {
    std::int64_t foodMinted = 0;
    std::int64_t foodEaten = 0;
    std::int64_t coinMinted = 0;
    std::int64_t coinSunk = 0;
};

/// The district's population, as a registered simulation system.
class WardPopulation final : public SimulationSystem {
public:
    /// Bakes the roster over `tiles` and starts the clock at `startSecond`.
    WardPopulation(const TileQuery& tiles, std::int32_t startSecond, std::uint64_t worldSeed,
                   const std::filesystem::path& contentDir);

    [[nodiscard]] const SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] TickPhase phase() const noexcept override { return TickPhase::Actors; }
    void tick(const TickContext& context) override;
    void hash_into(HashSink& sink) const override;

    [[nodiscard]] const std::vector<WardActor>& actors() const noexcept { return actors_; }
    /// Parallel to actors(), in the same index order, which IS id order. Baked
    /// once and never written again.
    [[nodiscard]] const std::vector<WardIdentity>& identities() const noexcept {
        return identities_;
    }
    /// Who this body is, or an empty identity for an id nobody baked. Answering
    /// with a blank rather than throwing is deliberate: a missing names.json
    /// must leave the ward mute, not stop the game.
    [[nodiscard]] const WardIdentity& identity(std::int32_t actorId) const noexcept;
    /// The body standing within `reachTiles` of (x, y) on `band`, nearest
    /// first, or nullptr. What pressing E on a street corner asks.
    ///
    /// CHEBYSHEV AND SAME-BAND, which is the same reach rule the taproom uses
    /// (Tavern::nearestTo) and the same band rule its witness filter learned the
    /// hard way: somebody asleep on the floor above is not somebody you are
    /// standing next to, whatever their (x, y) says.
    [[nodiscard]] const WardActor* nearestTo(std::int32_t x, std::int32_t y, std::int32_t band,
                                             std::int32_t reachTiles) const noexcept;
    /// The body with this id, or nullptr. Ids are dense and assigned in bake
    /// order, so this is an index check and not a search.
    [[nodiscard]] const WardActor* byId(std::int32_t actorId) const noexcept;
    /// Takes coin off somebody. Answers what actually came out, which is never
    /// more than they had.
    [[nodiscard]] std::int32_t takeCoinFrom(std::int32_t actorId, std::int32_t coin) noexcept;
    /// Turns a body to look at a point. They look at you while you talk to
    /// them, which is the whole difference between a person and a prop.
    void faceToward(std::int32_t actorId, std::int32_t x, std::int32_t y) noexcept;
    /// How many OTHER living people stand within `reachTiles` on the same band.
    /// What "did anybody see that" is asked of on a street with no walls in it.
    [[nodiscard]] std::int32_t witnessesAround(std::int32_t actorId,
                                               std::int32_t reachTiles) const noexcept;

    /// STREET PANIC. Frightens every person who can SEE (x, y) on `band` from
    /// within `radiusTiles` -- same band, Chebyshev range, line of sight --
    /// by driving their Safety down to alarmFloor(severity), so FLEE fires
    /// next tick and the street scatters; kPanicRecoverPerTick brings them
    /// back. A reserve already lower is left where it is. Answers how many
    /// saw it. Draw-free, and touches no field the hash does not already
    /// cover. The Watch is exempt (it holds); beasts are not asked.
    ///
    /// The caller passes the radius as well as the severity so a case can
    /// probe the edge of the rule directly; the client passes
    /// alarmRadius(severity), which is the three tiers.
    std::int32_t alarm(std::int32_t x, std::int32_t y, std::int32_t band,
                       std::int32_t radiusTiles, AlarmSeverity severity) noexcept;

    /// STREET SENSES (9a completion). WHERE THE PLAYER IS, in whole tiles,
    /// pushed by the client every step the way Tavern::setPlayer is -- so a
    /// frightened body FLEES AWAY FROM HIM rather than in a drawn direction
    /// (actFlee reads it), and the street Watch (9b) has somewhere to close on.
    /// HASHED, because a policy reads it: a scalar behaviour depends on that the
    /// hash does not cover is a divergence nothing would ever see (hash_into's
    /// own standing note). A run that never calls this -- the gate's own
    /// population workload before its assault leg, a session that never syncs --
    /// leaves the player UNKNOWN, and every body then flees exactly as 9a's
    /// drawn step did, so the no-player arithmetic is untouched by construction.
    void setPlayer(std::int32_t x, std::int32_t y, std::int32_t band) noexcept;
    /// Whether a player position has been pushed at all. Off until the first
    /// setPlayer; the flee-away vector needs a FROM before it means anything.
    [[nodiscard]] bool playerKnown() const noexcept { return playerKnown_; }

    // --- STREET SENSES leg (b): a body to hit ------------------------------

    /// THE SAME RAY, THIS ROSTER. The first PERSON the crosshair passes through
    /// among the district's people, by the identical integer projection
    /// Tavern::sightlineTarget casts (COMBAT-ACTION-SPEC.md section 2.1):
    /// along = (fx*dx + fy*dy) >> 16, perp = (-fy*dx + fx*dy) >> 16, on the line
    /// iff 0 < along <= kMeleeReach and |perp| <= kBodyHalfWidth, the smallest
    /// `along` wins, ties to the lower id. A body's Q8 position is its tile's
    /// centre (it walks in whole tiles). Standing persons only: a floored body
    /// is not a target, a beast is not on the street's ray in v1. Draw-free.
    /// `alongOut` (optional) receives the winner's along so the client can pick
    /// the nearer of this roster and the Gull's -- one rule, two rosters.
    [[nodiscard]] const WardActor* sightlineTarget(std::int32_t playerXQ8,
                                                   std::int32_t playerYQ8, std::int32_t band,
                                                   Angle yaw,
                                                   std::int64_t* alongOut = nullptr) const noexcept;

    /// WHO SAW IT. Standing persons -- the Watch included, `exceptId` excluded
    /// -- who can see (x, y) on `band` from within `radiusTiles` by the three
    /// clauses alarm() keeps (same band, Chebyshev range, line of sight). The
    /// murder law's own N SAW IT for a street killing, counted BEFORE the body
    /// drops, exactly as Tavern::slayActor counts the room. Draw-free, const.
    [[nodiscard]] std::int32_t witnessesInSight(std::int32_t x, std::int32_t y,
                                                std::int32_t band, std::int32_t radiusTiles,
                                                std::int32_t exceptId) const noexcept;

    /// A BLOW LANDED on a street body -- resolved by the caller on the Gull's
    /// own strike() (the sheet handed over as a Fighter, the roll the swing
    /// already owned), and applied here: `hpAfter` is what strike() left, `blow`
    /// what it did, `lethal` the class it landed under. Downed under BRAWL is
    /// the floor (downedUntil, kStreetFloorSeconds, stands at kStreetStandHp);
    /// downed under LETHAL is death (slain + dead, the tile freed, never
    /// stands) -- except a crowned Evictor blow, which only ever puts a man
    /// out. A struck fighter-back type starts swinging (fightUntil); anybody
    /// else struck is driven to kStruckPanicFloor and routs through the leg
    /// (a) flee plan. And the CROWD is alarmed at the tile -- Kill for a
    /// killing, Blow for a blow. Answers whether the body died. The tavern
    /// roster is never touched.
    bool applyStreetBlow(std::int32_t actorId, std::int32_t hpAfter, const Blow& blow,
                         bool lethal) noexcept;

    /// The blows fighting-back bodies threw AT THE PLAYER this tick (one draw
    /// each, on the thrower's own key), read-and-clear. A MAILBOX, not state:
    /// produced inside tick() and consumed by the client the same step (or
    /// drained by the gate's driver), so it is deliberately not hashed -- no
    /// ward behaviour ever reads it. The client resolves each on the player's
    /// sheet through Tavern::takeStreetBlow after its own reach check.
    [[nodiscard]] std::vector<StreetBlow> takeStreetBlows();

    // --- STREET SENSES leg (c): the Watch --------------------------------------

    /// DEFERENCE (D6, absolute). Pushed by the client beside setPlayer from
    /// Tavern::playerPresentsAsWielder(): while true no watchman is ever given
    /// cause, and one already Closing stands down. HASHED -- it decides what
    /// the Watch does.
    void setPlayerPresentsAsWielder(bool presents) noexcept;
    [[nodiscard]] bool playerPresentsAsWielder() const noexcept { return playerWielder_; }
    /// A HOUSE'S OWN BRAWL IS NOT STREET BUSINESS (found chasing
    /// test_scripted_lines.cpp's nemesis line). Pushed by the client beside
    /// setPlayer from Tavern::playerInside(): the Gull's ground floor and the
    /// open Tarwalk share a band (both 19) and alarm()'s own line-of-sight
    /// deliberately crosses an open door -- the owner's rule, "a killing at
    /// the bar reaches the Tarwalk only through the door" -- so the ordinary
    /// crowd still panics at a fight it heard through the door exactly as leg
    /// (a)/(b) shipped. The WATCH is a different question: a sanctioned house
    /// brawl is Watchman Cull's jurisdiction, through the Gull's own separate
    /// watch (WatchCause, violenceInView), never a beat cop's on the strength
    /// of what leaked past the threshold. While true no watchman is ever given
    /// cause. HASHED -- it decides what the Watch does, same as the deference
    /// flag beside it.
    void setPlayerIndoors(bool indoors) noexcept;
    [[nodiscard]] bool playerIndoors() const noexcept { return playerIndoors_; }
    /// What the Watch did this tick -- halts, demands, offences and arrests at
    /// reach -- read-and-clear, the blows' own mailbox rule (unhashed; the
    /// client says the lines and lands the arrest through the Gull's seam; the
    /// gate's driver drains and counts).
    [[nodiscard]] std::vector<WatchEvent> takeWatchEvents();
    /// Whether this watchman is Closing right now (the latch ahead of the
    /// clock). For the HUD's mark and the cases.
    [[nodiscard]] bool watchmanClosing(std::int32_t actorId) const noexcept;
    /// The three-clause notice rule asked of ONE body about the pushed player:
    /// standing, same band, within kWatchSightTiles, line of sight. The same
    /// question the Gull's canSeePlayer asks of its roster, on this one.
    [[nodiscard]] bool canSeePlayer(const WardActor& actor) const noexcept;

    [[nodiscard]] std::int32_t secondOfDay() const noexcept { return secondOfDay_; }
    [[nodiscard]] std::int64_t currentTick() const noexcept { return tick_; }

    /// The roll, recounted. Cheap, and derived rather than maintained: a
    /// counter that is incremented in six places is a counter that is wrong in
    /// one of them.
    [[nodiscard]] WardCensus census() const;
    [[nodiscard]] const WardLedger& ledger() const noexcept { return ledger_; }
    /// Food held, right now, everywhere: sacks, larders and stalls. The right
    /// hand side of the conservation identity.
    [[nodiscard]] std::int64_t foodHeld() const noexcept;

    /// How many bodies are inside `place` on `band` at this instant. The
    /// acceptance is written in these terms -- a place, an hour and a reason --
    /// so it is a method and not a loop in a test.
    [[nodiscard]] std::int32_t countIn(std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                       std::int32_t y1, std::int32_t band) const noexcept;
    /// The same, restricted to one type.
    [[nodiscard]] std::int32_t countIn(std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                       std::int32_t y1, std::int32_t band,
                                       WardType type) const noexcept;

    /// The watchmen who work the dark, by id.
    [[nodiscard]] const std::vector<std::int32_t>& nightRoster() const noexcept {
        return nightRoster_;
    }

    /// #80. Catches since the roster was baked, and chases abandoned as futile.
    ///
    /// BOTH, and the second is the interesting one. A hunt that always succeeds
    /// is a hunt with no geometry in it; a futile count that climbs without
    /// bound is the chokepoint freeze the Java build spent a sprint finding.
    /// Counted rather than asserted, so a case can watch the ratio.
    [[nodiscard]] std::int64_t catches() const noexcept { return catches_; }
    [[nodiscard]] std::int64_t futileChases() const noexcept { return futileChases_; }
    /// Roof homes the bake REFUSED because the climb was one-way: up but never
    /// back down. Zero is the expected answer and it is a number rather than an
    /// assumption -- a body homed on a deck it cannot descend from is the exact
    /// failure this pass was warned about.
    [[nodiscard]] std::int32_t roofHomesRefused() const noexcept { return roofRefused_; }
    /// Roof huts standing on a deck the ward can WALK onto -- reached by an
    /// authored stair rather than by a climb.
    ///
    /// THE DISTRICT HAS BOTH KINDS AND THEY ARE COUNTED APART. Section 2.6's S4
    /// vertical pass re-connected the Gullet's roof decks to their own condo,
    /// so several of these huts have had tenants since the roster was written
    /// and this round leaves them exactly where they were. The gap this round
    /// closes is the roof-slum PLANE -- world z22, 1,706 standable cells and,
    /// before it, no bodies at all. Reporting the two as one number would let
    /// a stair-served hut stand in as evidence for a climb nobody made.
    [[nodiscard]] std::int32_t roofHomesOnStairs() const noexcept { return roofOnStairs_; }
    /// The id range the mice occupy. Half-open, and CONTIGUOUS: section 6 of
    /// the roster spawns them last, after every person and every other beast.
    /// Exposed because "the predator scan is bounded by the prey count and not
    /// by the roll" is a structural claim a case should be able to read.
    [[nodiscard]] std::int32_t preyFirst() const noexcept { return preyFirst_; }
    [[nodiscard]] std::int32_t preyEnd() const noexcept { return preyEnd_; }
    /// Whether this cell is on the ward's own WALKING island -- the ground the
    /// district lives on, painted once at the bake from the player's own spawn.
    /// A cell that is standable and answers false is either somewhere you have
    /// to climb to (a roof deck) or somewhere nobody can reach at all.
    ///
    /// Exposed because the difference between the two is the whole of this
    /// pass, and a case that cannot ask the question can only assert around it.
    [[nodiscard]] bool onWalkingGround(std::int32_t x, std::int32_t y,
                                       std::int32_t band) const noexcept {
        return componentAt(x, y, band) == mainComponent_;
    }

    /// Shoves recorded since the roster was baked, and the worst pile-up seen:
    /// the most bodies ever standing within one tile of each other. Both are
    /// how "no guard pile-ups" stops being an opinion.
    [[nodiscard]] std::int64_t shoves() const noexcept { return shoves_; }
    [[nodiscard]] std::int32_t worstJam() const noexcept { return worstJam_; }
    /// Times a watchman has laid hands on another watchman. ZERO, always: the
    /// etiquette gate refuses it before any draw is made, which is what makes
    /// the beat's own corner yield the resolution mechanism instead of two
    /// watchmen wrestling in a doorway for the rest of the night. It is counted
    /// rather than asserted so that deleting the gate turns a case red.
    [[nodiscard]] std::int64_t watchOnWatchShoves() const noexcept { return watchShoves_; }

    /// Jumps the clock without simulating the gap -- what a capture at a named
    /// hour does, and what sleeping a night in a rented bed does.
    ///
    /// It also SETTLES the ward: every body is put where that hour's schedule
    /// says it should be, with no walk. Without that a capture at two in the
    /// morning photographs a district standing exactly where the bake left it,
    /// because a skip simulates none of the seconds it jumps -- and the frame
    /// that is supposed to prove the Watch is out would show a Watch still in
    /// bed. The taproom has done this since S2 (Actor::placeAt, "for a schedule
    /// block starting off-screen"); this is the same move for six hundred more
    /// people.
    void skipToSecond(std::int32_t second);

    /// Puts everybody where this hour says they should be, immediately. Called
    /// at boot and across every skip. Deterministic: ascending id, a fixed
    /// spiral for the free cell, and no draw.
    void settleToSchedule();

    /// The ward's own report line, for the gate and for --selftest.
    [[nodiscard]] std::string reportLine() const;

private:
    struct Home {
        std::int32_t x = 0, y = 0, band = 0;
        /// Meals in the larder. Restocked once a day out of the courtyards and
        /// the market, and drawn down by whoever sleeps here.
        std::int32_t larder = 0;
        /// How many people sleep here. The restock is sized off it, because a
        /// flat larder starves a household of five and wastes one of one -- and
        /// a food economy that starves by household size is a food economy
        /// whose balance number means nothing.
        std::int32_t residents = 0;
    };
    struct Route {
        std::int32_t first = 0;
        std::int32_t count = 0;
    };

    void bakeRoster(const std::filesystem::path& contentDir);
    /// Turns the notable bindings the roster made into names, and gives
    /// everybody else one out of the authored pools. Runs LAST, after every
    /// post is claimed, because claiming a post changes what somebody IS: the
    /// spare hand who ends up keeping the Slop-Chest is a shopkeeper by the
    /// time the ward opens and must be named out of the shopkeepers' pool.
    void bakeIdentities(const std::filesystem::path& contentDir);
    void tickActor(WardActor& actor, const TickContext& context);
    void decayNeeds(WardActor& actor);
    bool auditStarvation(WardActor& actor);
    [[nodiscard]] WardPolicy selectPolicy(const WardActor& actor) const;
    void actSeekFood(WardActor& actor);
    void actReturnHome(WardActor& actor);
    void actPursue(WardActor& actor, const TickContext& context);
    void actLoiter(WardActor& actor, const TickContext& context);
    void actFlee(WardActor& actor, const TickContext& context);
    /// One DRAWN orthogonal step -- the 9a panic step, and the loiter shuffle.
    /// Shared so the loiter shuffle stays direction-blind while actFlee's own
    /// away-vector (STREET SENSES) is the frightened body's alone: a loiterer
    /// near a known player mills, it does not back away from him.
    void oneDrawnStep(WardActor& actor, const TickContext& context);
    /// STREET SENSES (9a completion). Frightened and STANDING: face the fright
    /// (the pushed player, when known) and hold the tile. Draw-free, moves
    /// nobody -- the whole difference between a shopkeeper and a serf when the
    /// street goes bad. See wardTypeCowers.
    void actCower(WardActor& actor);
    /// STREET SENSES leg (b). FIGHTING BACK: close on the pushed player (a
    /// route step toward his tile) and, in reach, throw ONE blow -- one draw on
    /// this body's own key and sequence, posted to the mailbox for the client
    /// to land on the player's sheet. Ends when the clock runs out.
    void actBrawl(WardActor& actor, const TickContext& context);
    /// STREET SENSES leg (b). A struck man gets up: a quarter of the sheet, on
    /// his own tile if it is free, else the first free standable neighbour in
    /// the fixed order, else he lies a little longer (the den-full rule).
    /// Answers whether he stood.
    bool standUp(WardActor& actor);
    /// STREET SENSES leg (c). THE WATCH CLOSING: out of sight is out of it;
    /// else face him, close (the route step), and at reach arrest (a blow or a
    /// killing behind it) or hold the demand (steel), firing the Offence once
    /// when the grace runs out. Draw-free.
    void actClose(WardActor& actor);
    void actHunt(WardActor& actor);

    /// The throttled prey probe: an ascending scan of the MICE ONLY -- see
    /// preyFirst_ -- on the same band, up and not downed, nearest by Chebyshev
    /// with the lower id breaking ties. Pure, draw-free, and identical between
    /// the scoring call and the acting one, which is what stops the two
    /// disagreeing about whether there was anything to hunt.
    [[nodiscard]] std::int32_t senseNearestPrey(const WardActor& predator) const noexcept;
    /// What a live or acquirable hunt is worth: the raws' own seekFood pricing,
    /// so a starving beast outranks a scared one exactly the way a starving
    /// person does, and no new raws field is invented for it.
    [[nodiscard]] std::int32_t huntScore(const WardActor& predator) const noexcept;
    void dropHuntLock(WardActor& predator) const noexcept;
    /// Stands a caught mouse back up, in its own den, on the first free
    /// standable cell of a fixed spiral. Answers false when the den is full,
    /// and the caller then leaves it down a little longer rather than stacking.
    bool revivePrey(WardActor& prey);

    /// One tile toward `target`, along a cached route. Answers whether the body
    /// moved.
    bool stepToward(WardActor& actor, std::int32_t tx, std::int32_t ty, std::int32_t tband);
    bool tryEnter(WardActor& actor, std::int32_t nx, std::int32_t ny, std::int32_t nband);
    bool tryPush(WardActor& pusher, std::int32_t cx, std::int32_t cy, std::int32_t cband,
                 const TickContext& context);
    void rebuildOccupancy();
    void runDailyProvision();
    void chargeStall(WardActor& actor, bool moved, bool closer);
    /// Picks the next corner of a beat, or the next waypoint of a round.
    void advanceLeg(WardActor& actor, const TickContext& context);

    [[nodiscard]] std::uint32_t cellKey(std::int32_t x, std::int32_t y,
                                        std::int32_t band) const noexcept;
    /// The nearest standable cell to (x, y, band), searched outward in a fixed
    /// order. An authored anchor is a marker on a map and a marker can sit on a
    /// counter; a body has to stand somewhere real.
    ///
    /// `wantWalkable` additionally requires the cell to be in the district's
    /// MAIN walking component -- see walkComponent_ for why that matters more
    /// than it sounds.
    [[nodiscard]] bool snapToStandable(std::int32_t& x, std::int32_t& y, std::int32_t& band,
                                       std::int32_t radius, bool wantWalkable = true) const;

    /// Paints every standable cell with the id of the walk component it is in.
    /// Run once at the bake, before anybody is placed.
    void mapWalkComponents();
    [[nodiscard]] std::int16_t componentAt(std::int32_t x, std::int32_t y,
                                           std::int32_t band) const noexcept;
    /// Whether a body that can climb could get to this cell at all: the walking
    /// island, or anything the climb closure reaches off it.
    [[nodiscard]] bool climbReaches(std::int32_t x, std::int32_t y,
                                    std::int32_t band) const noexcept {
        return componentAt(x, y, band) >= 0;
    }
    /// THE ROUND TRIP, AND IT IS THE GUARD THE WHOLE FEATURE RESTS ON.
    ///
    /// kClimbIsland says a cell can be reached from the ward's ground BY
    /// CLIMBING. It does NOT say a body up there can get back down, and the
    /// difference is a roof full of tenants who will stand on it until they
    /// starve. So every roof bed is proved both ways with the real router in
    /// the real gait before anybody is put in it, and a bed that fails is
    /// refused and counted (roofHomesRefused).
    [[nodiscard]] bool roofBedIsSound(std::int32_t x, std::int32_t y, std::int32_t band) const;
    /// A climb route from a to b AND from b to a. Both, because the roof moves
    /// are not symmetric: a wall you can mantle up is a wall you may only be
    /// able to come down beside, and a drop is a move with no inverse at all.
    [[nodiscard]] bool climbRoundTrip(const PathStep& a, const PathStep& b) const;

    SystemId id_;
    const TileQuery* tiles_;
    std::uint64_t worldSeed_;
    std::int32_t secondOfDay_ = 0;
    /// Seconds the ward's clock stands AHEAD of the engine's tick count. Kept
    /// apart from secondOfDay_ so the clock is DERIVED from the tick rather
    /// than incremented -- an incremented clock drifts the first time a tick is
    /// skipped, and skipToSecond exists precisely to skip them.
    ///
    /// MONOTONE, and never reduced modulo a day. Skipping to an hour earlier
    /// than the current one means the NEXT such hour, so a skip always moves
    /// time forward -- which is what makes the day number this is divided into
    /// advance across a slept night and the larders restock because of it.
    std::int64_t clockOffset_ = 0;
    std::int64_t tick_ = 0;
    std::int64_t lastProvisionDay_ = -1;

    /// STREET SENSES (9a completion). The player's tile, pushed by setPlayer and
    /// read by actFlee for its away-vector (and by the street Watch, 9b). Whole
    /// tiles, integer, hashed. Unknown until the first push -- see setPlayer.
    std::int32_t playerX_ = 0;
    std::int32_t playerY_ = 0;
    std::int32_t playerBand_ = 0;
    bool playerKnown_ = false;

    /// STREET SENSES leg (b). The mailbox of blows thrown at the player this
    /// tick -- see takeStreetBlows. Not hashed, by design (its own note).
    std::vector<StreetBlow> pendingBlows_;
    /// STREET SENSES leg (c). The Watch's own mailbox (takeWatchEvents), the
    /// same rule; and the deference flag, HASHED (the Watch reads it).
    std::vector<WatchEvent> pendingWatch_;
    bool playerWielder_ = false;
    /// A house's own brawl is not street business -- see setPlayerIndoors.
    /// HASHED, the deference flag's own reason.
    bool playerIndoors_ = false;

    WardTypeTable types_;
    std::vector<WardActor> actors_;
    /// Parallel to actors_. See WardIdentity on why it is not a member of it.
    std::vector<WardIdentity> identities_;
    std::vector<Home> homes_;
    /// Which home each actor sleeps in, or -1 for somebody sleeping rough.
    std::vector<std::int32_t> homeOf_;
    /// The waypoint pool every round and beat indexes into.
    std::vector<PathStep> waypoints_;
    std::vector<Route> routes_;
    /// Which route each actor walks, or -1.
    std::vector<std::int32_t> routeOf_;
    /// Where food is sold. The victualler stands of DOCKS-GAZETTEER's market
    /// row, and the only place somebody with coin and an empty larder can eat.
    std::vector<PathStep> marketStalls_;
    /// The ids of the watchmen who work the dark. Held so "the night roster is
    /// exactly seven and they are on the street at two" is something a case can
    /// read rather than something a comment claims.
    std::vector<std::int32_t> nightRoster_;
    /// The market's own stock, drawn on by anybody with coin and no larder.
    std::int32_t marketStock_ = 0;

    /// WHICH ISLAND OF THE DISTRICT EACH CELL IS ON, or -1 for a cell nobody
    /// can stand on. Painted once at the bake by a flood fill over
    /// TileQuery::stepBand -- the walking rule, exactly, with no roof moves.
    ///
    /// IT EXISTS FOR SPEED AND IT PAYS FOR ITSELF IN CORRECTNESS. An A* that
    /// CANNOT succeed is the most expensive search there is: it burns the whole
    /// four-thousand-node budget before answering no, and it answers no again
    /// every time the retry cooldown lapses, forever. Six hundred bodies each
    /// asking one impossible question is not a slow simulation, it is a stopped
    /// one -- and the Docks has genuinely unreachable ground in it, because
    /// DOCKS-GAZETTEER section 2.6 files the roof-slum planes' isolation as
    /// design.
    ///
    /// So the question is asked ONCE, at the bake, and a body is never homed or
    /// posted anywhere it cannot walk to. What the map cannot reach on foot,
    /// nobody in the ward lives on.
    /// #80. AND A SECOND LABEL ON THE SAME MAP: kClimbIsland, for a cell the
    /// ward can only reach by hauling itself up a wall. The roof decks and the
    /// roof-slum planes are that, and they are two thirds of the district's
    /// standable ground -- docks.hpp's kReachableWithRoofMoves against
    /// kReachableFromSpawn is the arithmetic.
    ///
    /// It is painted by CONTINUING the walk flood rather than by running a
    /// second one: the walking pass leaves its whole frontier in a vector, and
    /// the climb pass re-walks it offering only the moves the walking rule
    /// already refused. So the extra cost is four probes per already-known cell
    /// plus the new ground, once per Session, and never a second full scan.
    std::vector<std::int16_t> walkComponent_;
    /// The component the district's own spawn is in: the one the ward lives on.
    std::int16_t mainComponent_ = -1;

    OccupancyIndex occupancy_;
    mutable PathFinder finder_;
    WardLedger ledger_;
    std::int64_t shoves_ = 0;
    std::int64_t watchShoves_ = 0;
    std::int64_t catches_ = 0;
    std::int64_t futileChases_ = 0;
    std::int32_t worstJam_ = 0;
    std::int32_t starved_ = 0;
    std::int32_t roofRefused_ = 0;
    std::int32_t roofOnStairs_ = 0;
    /// The mice's contiguous id range, recorded at the bake. See preyFirst().
    std::int32_t preyFirst_ = 0;
    std::int32_t preyEnd_ = 0;
};

/// The two labels walkComponent_ carries. A cell is on the ward's own walking
/// ground, or it is somewhere the ward can only climb to, or it is nowhere.
inline constexpr std::int16_t kWalkIsland = 0;
inline constexpr std::int16_t kClimbIsland = 1;

/// The hour the ward's own acceptance is written against, and the places it
/// names. Exposed so a test and a capture script agree on what "the Tarwalk at
/// eight" means without either of them owning the numbers.
namespace wardplaces {

/// The working spine, quayside, in the reach the day trades walk.
inline constexpr std::int32_t kTarwalkX0 = 32;
inline constexpr std::int32_t kTarwalkY0 = 60;
inline constexpr std::int32_t kTarwalkX1 = 195;
inline constexpr std::int32_t kTarwalkY1 = 67;
/// The lower-middle road the Watch's night beat runs.
inline constexpr std::int32_t kRopewyndX0 = 36;
inline constexpr std::int32_t kRopewyndY0 = 90;
inline constexpr std::int32_t kRopewyndX1 = 209;
inline constexpr std::int32_t kRopewyndY1 = 99;
/// The whole of the quayside band. "The district is not empty" is asked of
/// this, not of a frame.
inline constexpr std::int32_t kDistrictX0 = 32;
inline constexpr std::int32_t kDistrictY0 = 32;
inline constexpr std::int32_t kDistrictX1 = 223;
inline constexpr std::int32_t kDistrictY1 = 159;

}  // namespace wardplaces

}  // namespace granadad::sim
