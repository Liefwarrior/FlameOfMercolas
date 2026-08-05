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
    /// A harbour gull. The district's own predator, and unowned.
    Gull = 13,
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
};

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
    void add(std::uint32_t cell);
    void remove(std::uint32_t cell) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    void reset(std::size_t capacity);
    void grow();
    [[nodiscard]] std::size_t slotOf(std::uint32_t cell) const noexcept;

    std::vector<std::uint32_t> keys_;
    std::vector<std::int16_t> counts_;
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
    std::int64_t lastPushTick = -1000000;
    /// The tick the body first read zero hunger, or -1.
    std::int64_t starvingSince = -1;

    /// Meals in the sack, and the small money.
    std::int32_t rations = 0;
    std::int32_t coin = 0;

    WardPolicy policy = WardPolicy::Loiter;
    bool dead = false;

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
    [[nodiscard]] bool atHome() const noexcept {
        return x == homeX && y == homeY && band == homeBand;
    }
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
    std::int32_t byType[kWardTypeCount] = {};
    std::int32_t byPolicy[6] = {};
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

    /// Shoves recorded since the roster was baked, and the worst pile-up seen:
    /// the most bodies ever standing within one tile of each other. Both are
    /// how "no guard pile-ups" stops being an opinion.
    [[nodiscard]] std::int64_t shoves() const noexcept { return shoves_; }
    [[nodiscard]] std::int32_t worstJam() const noexcept { return worstJam_; }

    /// Jumps the clock without simulating the gap -- what a capture at a named
    /// hour does. Needs decay and larders restock across the jump so the ward
    /// the shutter sees is the ward that hour would really have.
    void skipToSecond(std::int32_t second);

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
    void tickActor(WardActor& actor, const TickContext& context);
    void decayNeeds(WardActor& actor);
    bool auditStarvation(WardActor& actor);
    [[nodiscard]] WardPolicy selectPolicy(const WardActor& actor) const;
    void actSeekFood(WardActor& actor);
    void actReturnHome(WardActor& actor);
    void actPursue(WardActor& actor, const TickContext& context);
    void actLoiter(WardActor& actor, const TickContext& context);
    void actFlee(WardActor& actor, const TickContext& context);

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
    [[nodiscard]] bool snapToStandable(std::int32_t& x, std::int32_t& y, std::int32_t& band,
                                       std::int32_t radius) const;

    SystemId id_;
    const TileQuery* tiles_;
    std::uint64_t worldSeed_;
    std::int32_t secondOfDay_ = 0;
    /// The second of the day the engine's tick zero corresponds to. Kept apart
    /// from secondOfDay_ so the clock is derived from the tick rather than
    /// incremented -- an incremented clock drifts the moment a tick is skipped,
    /// and skipToSecond exists precisely to skip ticks.
    std::int32_t secondsAtBoot_ = 0;
    std::int64_t tick_ = 0;
    std::int64_t lastProvisionDay_ = -1;

    WardTypeTable types_;
    std::vector<WardActor> actors_;
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

    OccupancyIndex occupancy_;
    mutable PathFinder finder_;
    WardLedger ledger_;
    std::int64_t shoves_ = 0;
    std::int32_t worstJam_ = 0;
    std::int32_t starved_ = 0;
};

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
