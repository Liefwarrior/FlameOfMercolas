#include "granadad/sim/ward_actors.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "granadad/content/ascii.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine_error.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

std::string_view wardTypeName(WardType type) noexcept {
    switch (type) {
        case WardType::Serf: return "serf";
        case WardType::Shopkeeper: return "shopkeeper";
        case WardType::Sailor: return "sailor";
        case WardType::Fisher: return "fisher";
        case WardType::Carter: return "carter";
        case WardType::MilitiaWatch: return "watch";
        case WardType::Wastrel: return "wastrel";
        case WardType::Urchin: return "urchin";
        case WardType::Thief: return "thief";
        case WardType::PriestOfTheFlame: return "priest";
        case WardType::DiscipleOfTheFlame: return "disciple";
        case WardType::AnimalKeeper: return "keeper";
        case WardType::Dog: return "dog";
        case WardType::Stray: return "stray";
        case WardType::Cat: return "cat";
        case WardType::Mouse: return "mouse";
    }
    return "unknown";
}

std::string_view wardTypeRawsId(WardType type) noexcept {
    switch (type) {
        // Four trades share the labourer's row. A sailor and a rope-walk hand
        // eat the same and sleep the same; inventing raws nobody authored would
        // be this build writing its own canon.
        case WardType::Serf:
        case WardType::Sailor:
        case WardType::Fisher:
        case WardType::Carter: return "serf";
        case WardType::Shopkeeper: return "shopkeeper";
        case WardType::MilitiaWatch: return "militia_watch";
        // The ward's poor, its children and its thieves all live on the
        // wastrel's row: the same thin margin, and the same reason they are out
        // after dark.
        case WardType::Wastrel:
        case WardType::Urchin:
        case WardType::Thief: return "wastrel";
        case WardType::PriestOfTheFlame: return "priest_of_the_flame";
        case WardType::DiscipleOfTheFlame: return "disciple_of_the_flame";
        case WardType::AnimalKeeper: return "animal_keeper";
        case WardType::Dog: return "animal";
        case WardType::Stray: return "feral";
        case WardType::Cat: return "cat";
        case WardType::Mouse: return "mouse";
    }
    return "serf";
}

std::string_view wardJobName(WardJob job) noexcept {
    switch (job) {
        case WardJob::None: return "none";
        case WardJob::Anchor: return "anchor";
        case WardJob::Rounds: return "rounds";
        case WardJob::Patrol: return "patrol";
        case WardJob::NightWatch: return "nightwatch";
        case WardJob::Farm: return "farm";
        case WardJob::Fish: return "fish";
        case WardJob::Streetlife: return "streetlife";
        case WardJob::Scavenge: return "scavenge";
        case WardJob::Thieving: return "thieving";
        case WardJob::Wander: return "wander";
    }
    return "none";
}

std::string_view wardPolicyName(WardPolicy policy) noexcept {
    switch (policy) {
        case WardPolicy::Dead: return "dead";
        case WardPolicy::Flee: return "flee";
        case WardPolicy::SeekFood: return "seek-food";
        case WardPolicy::ReturnHome: return "return-home";
        case WardPolicy::Pursue: return "pursue";
        case WardPolicy::Loiter: return "loiter";
        case WardPolicy::Hunt: return "hunt";
    }
    return "loiter";
}

// ---------------------------------------------------------------------------
// the job table
// ---------------------------------------------------------------------------

bool JobParams::inWindow(std::int32_t secondOfDay) const noexcept {
    if (windowFromSecond == windowToSecond) {
        return false;  // a zero-width window is "never", and says so.
    }
    if (windowFromSecond < windowToSecond) {
        return secondOfDay >= windowFromSecond && secondOfDay < windowToSecond;
    }
    // Wrapped: a night shift.
    return secondOfDay >= windowFromSecond || secondOfDay < windowToSecond;
}

namespace {

/// THE HOURS THE DISTRICT KEEPS.
///
/// Every one of these is a claim a player can check by standing in the street
/// at that hour, which is exactly why they are hours of a real clock and not
/// the Java's stylised 24,000-tick day. The acceptance names three of them: the
/// Tarwalk is walked at eight because the day trades start at seven; the Watch
/// is on the Ropewynd at two because the night roster runs six to six; the
/// bins are worked at ten at night because that is when a child can work them
/// without being moved on.
constexpr JobParams makeJob(WardJob shape, std::int32_t priority, std::int32_t fromHour,
                            std::int32_t toHour, std::int32_t rhythmBonus,
                            std::int32_t workTicks, std::int32_t duty,
                            bool throughTheNight = false) {
    JobParams p;
    p.shape = shape;
    p.priority = priority;
    p.windowFromSecond = hourOfDay(fromHour);
    p.windowToSecond = hourOfDay(toHour) % kSecondsPerDay;
    p.rhythmBonus = rhythmBonus;
    p.workTicksPerUnit = workTicks;
    p.dutyPerUnit = duty;
    p.worksThroughTheNight = throughTheNight;
    return p;
}

/// The round-the-clock window, which makeJob's [0,0) cannot express -- a
/// zero-width window is deliberately "never" so a mistyped job fails loudly
/// rather than quietly working every hour there is.
constexpr JobParams allHours(JobParams p) {
    p.windowFromSecond = 0;
    p.windowToSecond = kSecondsPerDay;
    p.worksThroughTheNight = true;
    return p;
}

constexpr std::array<JobParams, kWardJobCount> kJobs = {
    /* None       */ makeJob(WardJob::None, 100, 0, 0, 0, 60, 0),
    /* Anchor     */ makeJob(WardJob::Anchor, 200, 7, 18, 60, 45, 500),
    /* Rounds     */ makeJob(WardJob::Rounds, 190, 7, 18, 60, 30, 400),
    /* Patrol     */ makeJob(WardJob::Patrol, 210, 6, 18, 80, 30, 500),
    /* NightWatch */ makeJob(WardJob::NightWatch, 210, 18, 6, 80, 30, 500, true),
    /* Farm       */ makeJob(WardJob::Farm, 195, 6, 17, 60, 60, 450),
    /* Fish       */ makeJob(WardJob::Fish, 200, 5, 15, 70, 45, 450),
    /* Streetlife */ makeJob(WardJob::Streetlife, 140, 9, 22, 40, 90, 200),
    /* Scavenge   */ makeJob(WardJob::Scavenge, 160, 19, 4, 60, 40, 250),
    /* Thieving   */ makeJob(WardJob::Thieving, 180, 22, 5, 60, 40, 300),
    /* Wander     */ allHours(makeJob(WardJob::Wander, 120, 0, 1, 0, 50, 150)),
};

static_assert(kJobs[1].priority + kJobs[1].rhythmBonus <= kJobPriorityMax,
              "a job that outscores going to bed is a ward that never sleeps");
static_assert(kJobs[3].priority + kJobs[3].rhythmBonus <= kJobPriorityMax, "");
static_assert(kJobs[4].priority + kJobs[4].rhythmBonus <= kJobPriorityMax, "");
static_assert(kJobs[4].worksThroughTheNight, "the night roster has to survive bedtime");
static_assert(!kJobs[9].worksThroughTheNight,
              "a thief keeping a night window is not a thief on a roster");

}  // namespace

const JobParams& wardJobParams(WardJob job) noexcept {
    return kJobs[static_cast<std::size_t>(job)];
}

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

namespace {

/// The rescale. One place, and every rate goes through it.
///
/// The raws are authored against a 24,000-tick day; this engine's day is
/// 86,400 ticks. A rate per kilotick therefore has to shrink by the same
/// factor, or a serf burns three and a half times its own reserve every day and
/// the ward starves inside a week.
[[nodiscard]] std::int32_t rescalePerKilotick(std::int32_t javaPerKilotick) noexcept {
    // 24000 / 86400 == 5 / 18, exactly, in integers.
    return (javaPerKilotick * 5 + 9) / 18;
}

[[nodiscard]] NeedConfig defaultNeed(std::int32_t start, std::int32_t decay,
                                     std::int32_t recover, std::int32_t low,
                                     std::int32_t crit) noexcept {
    NeedConfig c;
    c.start = start;
    c.decayPerKilotick = rescalePerKilotick(decay);
    c.recoverPerKilotick = rescalePerKilotick(recover * 1000);
    c.lowBonus = low;
    c.critBonus = crit;
    return c;
}

/// The compiled fallback. Deliberately a copy of the shipped serf row: a
/// missing raws tree must not stop the game booting, and the game booting with
/// nobody able to eat would be worse than refusing.
[[nodiscard]] WardTypeStats fallbackRow(WardType type) {
    WardTypeStats s;
    s.needs[0] = defaultNeed(8000, 1000, 0, 500, 1000);
    s.needs[1] = defaultNeed(9000, 600, 0, 250, 500);
    s.needs[2] = defaultNeed(6000, 500, 0, 200, 400);
    s.needs[3] = defaultNeed(10000, 0, 2, 300, 700);
    s.needs[4] = defaultNeed(8000, 600, 0, 200, 350);
    s.speedTicksPerStep = 1;
    s.leashRadius = 30;
    if (!isPerson(type)) {
        s.leashRadius = 14;
    }
    return s;
}

const char* const kNeedKeys[kNeedCount] = {"hunger", "rest", "coin", "safety", "duty"};

/// Meals somebody carries. Two is a working day's margin and no more: an actor
/// who could carry a week's food would never visit a larder or a stall, and the
/// whole food economy would be a number that never moved.
constexpr std::int32_t kCarryRations = 2;

}  // namespace

WardTypeTable WardTypeTable::load(const std::filesystem::path& contentDir) {
    WardTypeTable table;
    for (std::size_t i = 0; i < kWardTypeCount; ++i) {
        table.rows_[i] = fallbackRow(static_cast<WardType>(i));
    }
    const std::filesystem::path dir = contentDir / "raws" / "actors";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return table;
    }
    // The eleven authored ids, read once each and applied to every type that
    // names them. Sorted so the read order is a fact rather than a filesystem
    // accident.
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < kWardTypeCount; ++i) {
        const std::string id(wardTypeRawsId(static_cast<WardType>(i)));
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        const std::filesystem::path file = dir / (id + ".json");
        std::ifstream in(file);
        if (!in) {
            continue;
        }
        nlohmann::json doc;
        try {
            in >> doc;
        } catch (const nlohmann::json::exception&) {
            continue;  // a half-saved raw must not stop the ward booting
        }
        ++table.filesRead_;
        WardTypeStats row = fallbackRow(WardType::Serf);
        if (doc.contains("needs")) {
            const auto& needs = doc["needs"];
            for (std::size_t n = 0; n < kNeedCount; ++n) {
                if (!needs.contains(kNeedKeys[n])) {
                    continue;
                }
                const auto& block = needs[kNeedKeys[n]];
                row.needs[n] = defaultNeed(block.value("start", 8000),
                                           block.value("decayPerKilotick", 0),
                                           block.value("recoverPerTick", 0),
                                           block.value("lowBonus", 0),
                                           block.value("critBonus", 0));
            }
        }
        row.leashRadius = doc.value("leashRadius", row.leashRadius);
        if (doc.contains("flee")) {
            row.fleePriority = doc["flee"].value("priority", row.fleePriority);
        }
        if (doc.contains("seekFood")) {
            row.seekFoodPriority = doc["seekFood"].value("priority", row.seekFoodPriority);
        }
        if (doc.contains("returnHome")) {
            row.returnHomePriority = doc["returnHome"].value("priority", row.returnHomePriority);
            row.returnHomeRhythmBonus =
                doc["returnHome"].value("rhythmBonus", row.returnHomeRhythmBonus);
        }
        if (doc.contains("loiter")) {
            row.loiterPriority = doc["loiter"].value("priority", row.loiterPriority);
        }
        for (std::size_t i = 0; i < kWardTypeCount; ++i) {
            if (wardTypeRawsId(static_cast<WardType>(i)) == id) {
                const std::int32_t speed = table.rows_[i].speedTicksPerStep;
                table.rows_[i] = row;
                table.rows_[i].speedTicksPerStep = speed;
            }
        }
    }
    table.fromRaws_ = table.filesRead_ > 0;

    // Speed is this build's, not the raws': the Java's speedTicksPerStep is
    // against a 3.6-second tick and a body moving one tile every seven seconds
    // is a body a player watches not walking. One tile a second is 0.9 m/s,
    // which is a working walk; the Watch beats it faster and the beasts faster
    // again.
    table.rows_[static_cast<std::size_t>(WardType::MilitiaWatch)].speedTicksPerStep = 1;
    table.rows_[static_cast<std::size_t>(WardType::Urchin)].speedTicksPerStep = 1;
    table.rows_[static_cast<std::size_t>(WardType::Cat)].speedTicksPerStep = 1;
    table.rows_[static_cast<std::size_t>(WardType::Mouse)].speedTicksPerStep = 1;
    table.rows_[static_cast<std::size_t>(WardType::Stray)].speedTicksPerStep = 1;
    table.rows_[static_cast<std::size_t>(WardType::Dog)].speedTicksPerStep = 2;
    table.rows_[static_cast<std::size_t>(WardType::PriestOfTheFlame)].speedTicksPerStep = 2;
    return table;
}

// ---------------------------------------------------------------------------
// occupancy
// ---------------------------------------------------------------------------

void OccupancyIndex::reset(std::size_t capacity) {
    std::size_t power = 16;
    while (power < capacity * 2) {
        power *= 2;
    }
    keys_.assign(power, kEmpty);
    counts_.assign(power, 0);
    owners_.assign(power, -1);
    mask_ = power - 1;
    count_ = 0;
}

void OccupancyIndex::clear() noexcept {
    std::fill(keys_.begin(), keys_.end(), kEmpty);
    std::fill(counts_.begin(), counts_.end(), 0);
    std::fill(owners_.begin(), owners_.end(), -1);
    count_ = 0;
}

std::size_t OccupancyIndex::slotOf(std::uint32_t cell) const noexcept {
    // Fibonacci hash: the packed cell key is dense and highly structured, and a
    // plain mask over it puts a whole street in one bucket run.
    std::size_t slot = (static_cast<std::size_t>(cell * 0x9E3779B1u) >> 8) & mask_;
    while (keys_[slot] != kEmpty && keys_[slot] != cell) {
        slot = (slot + 1) & mask_;
    }
    return slot;
}

std::int32_t OccupancyIndex::at(std::uint32_t cell) const noexcept {
    const std::size_t slot = slotOf(cell);
    return keys_[slot] == cell ? counts_[slot] : 0;
}

std::int32_t OccupancyIndex::occupantAt(std::uint32_t cell) const noexcept {
    const std::size_t slot = slotOf(cell);
    return keys_[slot] == cell ? owners_[slot] : -1;
}

void OccupancyIndex::grow() {
    const std::vector<std::uint32_t> oldKeys = keys_;
    const std::vector<std::int16_t> oldCounts = counts_;
    const std::vector<std::int32_t> oldOwners = owners_;
    reset(oldKeys.size());
    for (std::size_t i = 0; i < oldKeys.size(); ++i) {
        if (oldKeys[i] == kEmpty) {
            continue;
        }
        const std::size_t slot = slotOf(oldKeys[i]);
        keys_[slot] = oldKeys[i];
        counts_[slot] = oldCounts[i];
        owners_[slot] = oldOwners[i];
        ++count_;
    }
}

void OccupancyIndex::add(std::uint32_t cell, std::int32_t actorId) {
    if ((count_ + 1) * 4 > keys_.size() * 3) {
        grow();
    }
    const std::size_t slot = slotOf(cell);
    if (keys_[slot] == kEmpty) {
        keys_[slot] = cell;
        counts_[slot] = 0;
        owners_[slot] = -1;
        ++count_;
    }
    ++counts_[slot];
    // The LOWEST id present, so two bodies arriving on one cell in one tick
    // resolve the same way twice. Under the one-per-cell cap this is always the
    // only occupant; the min is what keeps it correct if the cap is ever
    // raised, and costs a comparison.
    if (owners_[slot] < 0 || actorId < owners_[slot]) {
        owners_[slot] = actorId;
    }
}

void OccupancyIndex::remove(std::uint32_t cell) noexcept {
    std::size_t slot = slotOf(cell);
    if (keys_[slot] != cell) {
        return;
    }
    if (--counts_[slot] > 0) {
        return;
    }
    // BACKWARD-SHIFT DELETION, Knuth 6.4 algorithm R. No tombstones, so the
    // table's shape never depends on the order things were removed in -- which
    // is exactly the kind of history-dependence that makes two runs of the same
    // seed disagree.
    keys_[slot] = kEmpty;
    counts_[slot] = 0;
    owners_[slot] = -1;
    --count_;
    std::size_t hole = slot;
    std::size_t probe = (slot + 1) & mask_;
    while (keys_[probe] != kEmpty) {
        const std::size_t ideal =
            (static_cast<std::size_t>(keys_[probe] * 0x9E3779B1u) >> 8) & mask_;
        const std::size_t distFromIdeal = (probe - ideal) & mask_;
        const std::size_t distFromHole = (probe - hole) & mask_;
        if (distFromIdeal >= distFromHole) {
            keys_[hole] = keys_[probe];
            counts_[hole] = counts_[probe];
            owners_[hole] = owners_[probe];
            keys_[probe] = kEmpty;
            counts_[probe] = 0;
            owners_[probe] = -1;
            hole = probe;
        }
        probe = (probe + 1) & mask_;
    }
}

// ---------------------------------------------------------------------------
// the system
// ---------------------------------------------------------------------------

WardPopulation::WardPopulation(const TileQuery& tiles, std::int32_t startSecond,
                               std::uint64_t worldSeed,
                               const std::filesystem::path& contentDir)
    : id_(SystemId::of("population", "POPU")),
      tiles_(&tiles),
      worldSeed_(worldSeed),
      secondOfDay_(((startSecond % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay),
      types_(WardTypeTable::load(contentDir)),
      finder_(tiles) {
    // The clock is DERIVED from the tick and never incremented: an incremented
    // clock drifts the first time a tick is skipped, and skipToSecond exists
    // precisely to skip them.
    clockOffset_ = secondOfDay_;
    // THE MAP OF WHAT CAN BE WALKED TO, FIRST. Every home, post and waypoint
    // the bake places is checked against it, so the ward never contains a body
    // whose own bed it cannot reach on foot.
    mapWalkComponents();
    bakeRoster(contentDir);
    rebuildOccupancy();
    runDailyProvision();
    // AND THE WARD OPENS AT THE HOUR IT WAS ASKED FOR. A session that boots at
    // eight in the morning has to boot into a district already at work, not
    // into six hundred people in bed who will spend the next twenty minutes of
    // real time walking there in front of the player.
    settleToSchedule();
}

std::uint32_t WardPopulation::cellKey(std::int32_t x, std::int32_t y,
                                      std::int32_t band) const noexcept {
    return static_cast<std::uint32_t>((band * tiles_->sizeY() + y) * tiles_->sizeX() + x);
}

std::int16_t WardPopulation::componentAt(std::int32_t x, std::int32_t y,
                                         std::int32_t band) const noexcept {
    if (!tiles_->inBounds(x, y, band) || walkComponent_.empty()) {
        return -1;
    }
    return walkComponent_[static_cast<std::size_t>(cellKey(x, y, band))];
}

void WardPopulation::mapWalkComponents() {
    const std::size_t cells = static_cast<std::size_t>(tiles_->sizeX()) *
                              static_cast<std::size_t>(tiles_->sizeY()) *
                              static_cast<std::size_t>(tiles_->sizeZ());
    walkComponent_.assign(cells, -1);

    // ONE FLOOD, FROM THE DISTRICT'S OWN SPAWN, AND NOTHING ELSE.
    //
    // Labelling every island would mean asking standable() of all one and a
    // half million cells of the world -- and this runs once per Session, of
    // which the test suite builds a couple of hundred. The only question the
    // ward ever asks is "is this the ground the ward lives on", so the only
    // answer computed is that one: component 0 is what the player can walk to
    // from where the player arrives, and everything else is -1, which reads as
    // "not our ground" whether it is a roof plane, a sealed cellar or a wall.
    std::vector<std::int32_t> frontier;
    frontier.reserve(32768);
    const std::int32_t seed = static_cast<std::int32_t>(
        cellKey(docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand));
    if (!tiles_->standable(docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand)) {
        mainComponent_ = -1;
        return;
    }
    mainComponent_ = kWalkIsland;
    walkComponent_[static_cast<std::size_t>(seed)] = kWalkIsland;
    frontier.push_back(seed);
    static constexpr std::int32_t dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    static constexpr std::int32_t dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const std::int32_t key = frontier[head];
        const std::int32_t cx = key % tiles_->sizeX();
        const std::int32_t cy = (key / tiles_->sizeX()) % tiles_->sizeY();
        const std::int32_t cz = key / (tiles_->sizeX() * tiles_->sizeY());
        for (int n = 0; n < 8; ++n) {
            const std::int32_t nx = cx + dx[n];
            const std::int32_t ny = cy + dy[n];
            const std::int32_t nz = tiles_->stepBand(cx, cy, cz, nx, ny);
            if (nz == TileQuery::kNoBand) {
                continue;
            }
            // The same no-corner-cut rule the search uses, or the map would
            // promise routes the router refuses to plan.
            if (n >= 4 && (tiles_->stepBand(cx, cy, cz, nx, cy) == TileQuery::kNoBand ||
                           tiles_->stepBand(cx, cy, cz, cx, ny) == TileQuery::kNoBand)) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(cellKey(nx, ny, nz));
            if (walkComponent_[at] >= 0) {
                continue;
            }
            walkComponent_[at] = kWalkIsland;
            frontier.push_back(static_cast<std::int32_t>(at));
        }
    }

    // --- #80: and then the same flood, with the climb ------------------------
    //
    // THE FRONTIER IS RE-WALKED, NOT REBUILT. Every walking cell is already in
    // that vector in a fixed order, so the climb pass starts at index 0 with
    // the array it needs and expands it with BOTH kinds of move.
    //
    // BOTH, and it has to be both. The first draft offered only the four climb
    // moves, on the reasoning that the walk moves had already been taken -- and
    // that is true of the cells that were already there and false of every cell
    // the climb pass ADDS. You mantle onto a deck and then you WALK along it;
    // without the walking half a roof would be labelled one cell at a time, and
    // only where a wall face happened to stand under each of them. The decks
    // would have come out almost empty and the roof slum would have stayed
    // empty for a second, subtler reason than the one this pass is fixing.
    //
    // The walk probes over the original cells are all no-ops -- their answers
    // are already labelled -- so what they cost is one stepBand call apiece,
    // once per Session, and what they buy is the closure actually closing.
    //
    // WHY A SECOND LABEL AND NOT A SECOND ARRAY. The only question the ward
    // ever asks of this map is which of three answers a cell has -- our ground,
    // ground we would have to climb to, or nowhere -- and one int16 already
    // holds three answers. A parallel array would be another three megabytes
    // per Session for a bit of information.
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const std::int32_t key = frontier[head];
        const std::int32_t cx = key % tiles_->sizeX();
        const std::int32_t cy = (key / tiles_->sizeX()) % tiles_->sizeY();
        const std::int32_t cz = key / (tiles_->sizeX() * tiles_->sizeY());
        for (int n = 0; n < 8; ++n) {
            const std::int32_t nx = cx + dx[n];
            const std::int32_t ny = cy + dy[n];
            std::int32_t nz = tiles_->stepBand(cx, cy, cz, nx, ny);
            if (nz != TileQuery::kNoBand) {
                // The same no-corner-cut rule the search uses.
                if (n >= 4 && (tiles_->stepBand(cx, cy, cz, nx, cy) == TileQuery::kNoBand ||
                               tiles_->stepBand(cx, cy, cz, cx, ny) == TileQuery::kNoBand)) {
                    continue;
                }
            } else if (n < 4) {
                // NO DIAGONAL CLIMBS, exactly as PathFinder::resolveMove
                // refuses them -- the map must not promise a move the router
                // will not plan.
                nz = tiles_->mantleBand(cx, cy, cz, nx, ny);
                if (nz == TileQuery::kNoBand) {
                    nz = tiles_->landingBand(nx, ny, cz - 2, kMaxPathDrop - 2);
                }
            }
            if (nz == TileQuery::kNoBand || !tiles_->inBounds(nx, ny, nz)) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(cellKey(nx, ny, nz));
            if (walkComponent_[at] >= 0) {
                continue;
            }
            walkComponent_[at] = kClimbIsland;
            frontier.push_back(static_cast<std::int32_t>(at));
        }
    }
}

bool WardPopulation::roofBedIsSound(std::int32_t x, std::int32_t y, std::int32_t band) const {
    if (!tiles_->standable(x, y, band) || !climbReaches(x, y, band)) {
        return false;
    }
    // The ward's own ground, as near this bed as it gets: the compound
    // underneath it. Everything on kWalkIsland is connected to everything else
    // on kWalkIsland by construction, so proving the round trip to the nearest
    // piece of it proves the round trip to the whole district.
    std::int32_t gx = 0;
    std::int32_t gy = 0;
    std::int32_t gb = 0;
    bool found = false;
    for (std::int32_t r = 1; r <= 12 && !found; ++r) {
        for (std::int32_t db = 0; db >= -3 && !found; --db) {
            for (std::int32_t dy = -r; dy <= r && !found; ++dy) {
                for (std::int32_t dx = -r; dx <= r && !found; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    if (componentAt(x + dx, y + dy, band + db) != mainComponent_) {
                        continue;
                    }
                    gx = x + dx;
                    gy = y + dy;
                    gb = band + db;
                    found = true;
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    // BOTH WAYS, with the real router in the real gait. Up is what the flood
    // already promised; DOWN is the one nothing else checks, and a bed that
    // fails it is a tenant standing on a deck for the rest of the game.
    return climbRoundTrip(PathStep{gx, gy, gb}, PathStep{x, y, band});
}

bool WardPopulation::climbRoundTrip(const PathStep& a, const PathStep& b) const {
    std::vector<PathStep> scratch;
    if (!finder_.find(a, b, 0, scratch, Gait::Climb)) {
        return false;
    }
    return finder_.find(b, a, 0, scratch, Gait::Climb);
}

bool WardPopulation::snapToStandable(std::int32_t& x, std::int32_t& y, std::int32_t& band,
                                     std::int32_t radius, bool wantWalkable) const {
    const auto ok = [&](std::int32_t cx, std::int32_t cy, std::int32_t cz) {
        if (!tiles_->standable(cx, cy, cz)) {
            return false;
        }
        return !wantWalkable || mainComponent_ < 0 || componentAt(cx, cy, cz) == mainComponent_;
    };
    if (ok(x, y, band)) {
        return true;
    }
    // A fixed spiral: same ring order, same band order, every time. An authored
    // marker can sit on a counter or inside a rack; a body has to stand
    // somewhere real, and WHICH somewhere has to be a fact about the map rather
    // than about iteration order.
    for (std::int32_t r = 1; r <= radius; ++r) {
        // Same band first, then one below, then one above: a marker on a
        // counter belongs to the room it is in, and only after that to the
        // storey under or over it.
        for (const std::int32_t dband : {0, -1, 1}) {
            const std::int32_t bz = band + dband;
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                for (std::int32_t dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    if (ok(x + dx, y + dy, bz)) {
                        x += dx;
                        y += dy;
                        band = bz;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void WardPopulation::rebuildOccupancy() {
    // A FULL CLEAR AND RE-ADD, and it runs at the bake and after every settle
    // rather than every tick: every position change in between goes through
    // tryEnter, tryPush or a death, and each of those keeps the index straight
    // itself. Rebuilding six hundred and seventy-eight entries a second to
    // re-derive something already correct is work nobody reads.
    //
    // The DEAD are never counted, here or anywhere, so a corpse vacates its
    // tile rather than blocking a street for the rest of the game.
    occupancy_.clear();
    for (const WardActor& actor : actors_) {
        // #80: and a body that has been eaten holds no square either. visible()
        // is the one place "on the board" is answered; see its comment.
        if (actor.visible()) {
            occupancy_.add(cellKey(actor.x, actor.y, actor.band), actor.id);
        }
    }
}

// --- needs -----------------------------------------------------------------

void WardPopulation::decayNeeds(WardActor& actor) {
    const WardTypeStats& stats = types_[actor.type];
    for (std::size_t n = 0; n < kNeedCount; ++n) {
        const NeedConfig& cfg = stats.needs[n];
        std::int32_t accum = actor.needAccum[n] + cfg.decayPerKilotick;
        std::int32_t value = actor.needs[n];
        while (accum >= 1000) {
            accum -= 1000;
            value = std::max(0, value - 1);
        }
        // Recovery rides the same accumulator in the opposite direction, so
        // neither drifts: every thousandth of a point is accounted for and
        // nothing here is a float.
        std::int32_t recover = cfg.recoverPerKilotick;
        if (n == static_cast<std::size_t>(Need::Rest) && actor.atHome()) {
            recover += kRestRecoveredPerKilotickAtHome;
        }
        accum -= recover;
        while (accum < 0) {
            accum += 1000;
            value = std::min(kNeedMax, value + 1);
        }
        actor.needAccum[n] = accum;
        actor.needs[n] = static_cast<std::int16_t>(value);
    }
}

bool WardPopulation::auditStarvation(WardActor& actor) {
    if (actor.need(Need::Hunger) > 0) {
        actor.starvingSince = -1;
        return false;
    }
    if (actor.starvingSince < 0) {
        actor.starvingSince = tick_;
        return false;
    }
    if (tick_ - actor.starvingSince < kStarvationGraceSeconds) {
        return false;
    }
    actor.dead = true;
    actor.policy = WardPolicy::Dead;
    // AND THE TILE IS FREE. A corpse that kept its square would block a street,
    // a doorway or somebody's own bed for the rest of the game -- which is the
    // one way a death can go on hurting a district that has already lost the
    // person.
    occupancy_.remove(cellKey(actor.x, actor.y, actor.band));
    ++starved_;
    return true;
}

// --- policy selection ------------------------------------------------------

WardPolicy WardPopulation::selectPolicy(const WardActor& actor) const {
    const WardTypeStats& stats = types_[actor.type];
    const std::int32_t hunger = actor.need(Need::Hunger);
    const std::int32_t rest = actor.need(Need::Rest);
    const std::int32_t safety = actor.need(Need::Safety);

    std::int32_t bestScore = 0;
    WardPolicy best = WardPolicy::Loiter;
    // Ties are broken by the EARLIER policy in this order, which is why every
    // comparison below is strictly greater.
    const auto offer = [&](std::int32_t score, WardPolicy policy) {
        if (score > bestScore) {
            bestScore = score;
            best = policy;
        }
    };

    // FLEE is a hard gate, not a curve: below CRITICAL and nothing else
    // matters, above it and it is not a consideration at all.
    if (safety < kNeedCritical) {
        offer(stats.fleePriority, WardPolicy::Flee);
    }

    // SEEK_FOOD, with the hysteresis: once it has won it keeps winning until
    // the reserve is RECOVERED, not merely until it stops being LOW.
    if (isPerson(actor.type)) {
        const bool wants = hunger < kNeedLow ||
                           (actor.policy == WardPolicy::SeekFood && hunger < kNeedRecovered);
        if (wants) {
            const std::int32_t bonus = hunger < kNeedCritical
                                           ? stats.needs[0].critBonus
                                           : stats.needs[0].lowBonus;
            offer(stats.seekFoodPriority + bonus, WardPolicy::SeekFood);
        }
    }

    // RETURN_HOME. The night term is what empties a street at the right hour,
    // and it is why the roster's own flag has to exist.
    {
        const JobParams& params = wardJobParams(actor.job);
        const bool nightShift = params.worksThroughTheNight && params.inWindow(secondOfDay_);
        const bool night = secondOfDay_ >= hourOfDay(22) || secondOfDay_ < hourOfDay(6);
        std::int32_t score = 0;
        if (!nightShift) {
            const bool wants = rest < kNeedLow ||
                               (actor.policy == WardPolicy::ReturnHome && rest < kNeedRecovered);
            if (wants || night) {
                score = stats.returnHomePriority;
                if (rest < kNeedCritical) {
                    score += stats.needs[1].critBonus;
                } else if (rest < kNeedLow) {
                    score += stats.needs[1].lowBonus;
                }
                if (night) {
                    score += stats.returnHomeRhythmBonus;
                }
            }
            // AT HOME AT NIGHT IT SCORES NOTHING, and this clause is the whole
            // of the off-shift fix. Without it a guard standing on its own bunk
            // at two in the morning has RETURN_HOME at zero, its job at 220,
            // gets dragged one cell out -- and then RETURN_HOME's night branch
            // fires at 385 and drags it back, forever, all night, every night.
            // The other half of the fix is pursueOffShiftHome, in actPursue.
            if (actor.atHome() && rest >= kNeedRecovered) {
                score = 0;
            }
        }
        offer(score, WardPolicy::ReturnHome);
    }

    // The job.
    if (actor.job != WardJob::None) {
        const JobParams& params = wardJobParams(actor.job);
        offer(params.priority + (params.inWindow(secondOfDay_) ? params.rhythmBonus : 0),
              WardPolicy::Pursue);
    }

    // #80. THE BEAST FOOD CHANNEL, and it is acquire-gated on purpose.
    //
    // The trap this shape exists to avoid is the one the Java build documented
    // and then hit anyway: a policy that scores on the HUNGER band alone and
    // cannot actually feed the body pins it forever -- a hungry beast standing
    // still, "seeking food" it has no way to obtain. So HUNT is worth nothing
    // at all unless a lock is already held or the throttled probe found
    // something real, and a hungry cat with no reachable mouse simply keeps
    // wandering. That is a structural guarantee and not a tuning choice.
    if (isPredator(actor.type)) {
        offer(huntScore(actor), WardPolicy::Hunt);
    }

    offer(stats.loiterPriority, WardPolicy::Loiter);
    return best;
}

// --- movement --------------------------------------------------------------

bool WardPopulation::tryEnter(WardActor& actor, std::int32_t nx, std::int32_t ny,
                              std::int32_t nband) {
    if (occupancy_.at(cellKey(nx, ny, nband)) >= kMaxOccupantsPerCell) {
        return false;
    }
    occupancy_.remove(cellKey(actor.x, actor.y, actor.band));
    actor.x = nx;
    actor.y = ny;
    actor.band = nband;
    occupancy_.add(cellKey(nx, ny, nband), actor.id);
    return true;
}

bool WardPopulation::tryPush(WardActor& pusher, std::int32_t cx, std::int32_t cy,
                             std::int32_t cband, const TickContext& context) {
    if (tick_ - pusher.lastPushTick < kPushCooldownTicks) {
        return false;
    }
    // The lowest-id living body on the cell, straight out of the index. Walking
    // the roster to find it would be O(N) inside a step every body takes every
    // tick -- see OccupancyIndex::occupantAt for the number that makes.
    const std::int32_t occupantId = occupancy_.occupantAt(cellKey(cx, cy, cband));
    if (occupantId < 0 || occupantId == pusher.id ||
        occupantId >= static_cast<std::int32_t>(actors_.size())) {
        return false;
    }
    WardActor* occupant = &actors_[static_cast<std::size_t>(occupantId)];
    if (occupant->dead) {
        return false;
    }
    // A GUARD DOES NOT SHOVE A GUARD ON DUTY. Checked before any draw, so a
    // refusal costs no draw index and burns no cooldown -- and it is what makes
    // the beat's own corner yield the resolution mechanism instead of two
    // watchmen wrestling in a doorway for the rest of the night.
    if (pusher.type == WardType::MilitiaWatch && occupant->type == WardType::MilitiaWatch) {
        return false;
    }
    // Counted AFTER the gate, so it can only ever be non-zero if the gate above
    // stops working. A rule nobody can watch break is a rule nobody knows they
    // broke.
    if (pusher.type == WardType::MilitiaWatch && occupant->type == WardType::MilitiaWatch) {
        ++watchShoves_;
    }
    // A CONTEST, not a right of way. Losing burns no cooldown: the blocked step
    // retries next tick, so a deadlock's dissolution is delayed a tick or two
    // and never defeated.
    const std::uint64_t roll = context.draw(cellKey(cx, cy, cband), pusher.id & 0xFFFF);
    const std::int32_t odds = pusher.type == WardType::MilitiaWatch ? 800
                              : isPerson(pusher.type)              ? 550
                                                                   : 350;
    if (!passes(roll, odds)) {
        return false;
    }

    // Somewhere for the pushee to go: the first adjacent same-band cell in the
    // fixed order that is standable and EMPTY. Only a free cell qualifies, so
    // the one-per-square cap holds and no push chain can mechanically start.
    static constexpr std::int32_t dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    static constexpr std::int32_t dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    for (int n = 0; n < 8; ++n) {
        const std::int32_t tx = cx + dx[n];
        const std::int32_t ty = cy + dy[n];
        const std::int32_t tz = tiles_->stepBand(cx, cy, cband, tx, ty);
        if (tz == TileQuery::kNoBand) {
            continue;
        }
        if (occupancy_.at(cellKey(tx, ty, tz)) != 0) {
            continue;
        }
        occupancy_.remove(cellKey(cx, cy, cband));
        occupant->x = tx;
        occupant->y = ty;
        occupant->band = tz;
        occupancy_.add(cellKey(tx, ty, tz), occupant->id);
        occupant->route.clear();
        occupant->routeTargetX = -1;
        // The stagger, encoded by BACK-DATING the pushee's own clock rather
        // than by carrying a second one. One persisted scalar, two roles.
        occupant->lastPushTick =
            std::max(occupant->lastPushTick, tick_ - (kPushCooldownTicks - kPusheeStaggerTicks));
        pusher.lastPushTick = tick_;
        ++shoves_;
        return true;
    }

    // THE SQUEEZE-PAST. Under one-per-square two bodies meeting head-on in a
    // one-wide passage are unresolvable by displacement alone -- there is no
    // free cell for either of them -- and they park there for the rest of the
    // day, incidentally sealing whatever is behind them into a dead end. So
    // they exchange cells: a full shove, logged, and it burns the cooldown.
    const std::uint32_t here = cellKey(pusher.x, pusher.y, pusher.band);
    const std::uint32_t there = cellKey(cx, cy, cband);
    occupancy_.remove(here);
    occupancy_.remove(there);
    occupant->x = pusher.x;
    occupant->y = pusher.y;
    occupant->band = pusher.band;
    pusher.x = cx;
    pusher.y = cy;
    pusher.band = cband;
    occupancy_.add(here, occupant->id);
    occupancy_.add(there, pusher.id);
    occupant->route.clear();
    occupant->routeTargetX = -1;
    occupant->lastPushTick =
        std::max(occupant->lastPushTick, tick_ - (kPushCooldownTicks - kPusheeStaggerTicks));
    pusher.lastPushTick = tick_;
    ++shoves_;
    return true;
}

namespace {

[[nodiscard]] std::int32_t chebyshev(std::int32_t ax, std::int32_t ay, std::int32_t bx,
                                     std::int32_t by) noexcept {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

/// The eight-point facing of a step. BAM 0 is north, which is -Y, and it
/// increases clockwise -- the same convention the player's own yaw uses, which
/// is what lets the renderer draw a face on a ward actor with the code it
/// already had for a tavern one.
[[nodiscard]] Angle facingFromDelta(std::int32_t dx, std::int32_t dy) noexcept {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    if (dx == 0) {
        return dy < 0 ? kFacingNorth : kFacingSouth;
    }
    if (dy == 0) {
        return dx > 0 ? kFacingEast : kFacingWest;
    }
    if (dx > 0) {
        return dy < 0 ? kTurnQuarter / 2 : kTurnQuarter + kTurnQuarter / 2;
    }
    return dy < 0 ? kFacingWest + kTurnQuarter / 2 : kFacingSouth + kTurnQuarter / 2;
}

/// How far a leg still has to go, WITH the climb counted. A cross-band route
/// that has just reached the foot of the ramp has made real progress, and a
/// flat Chebyshev cannot see it.
[[nodiscard]] std::int32_t legDistance(const WardActor& actor, std::int32_t tx, std::int32_t ty,
                                       std::int32_t tband) noexcept {
    return chebyshev(actor.x, actor.y, tx, ty) + std::abs(actor.band - tband) * 4;
}

}  // namespace

bool WardPopulation::stepToward(WardActor& actor, std::int32_t tx, std::int32_t ty,
                                std::int32_t tband) {
    if (actor.x == tx && actor.y == ty && actor.band == tband) {
        return false;
    }
    const WardTypeStats& stats = types_[actor.type];
    if (++actor.moveAccumTicks < stats.speedTicksPerStep) {
        return false;
    }
    actor.moveAccumTicks = 0;

    const bool sameTarget = actor.routeTargetX == tx && actor.routeTargetY == ty &&
                            actor.routeTargetBand == tband;
    bool needsPlan = !sameTarget || actor.routeIndex >= static_cast<std::int32_t>(actor.route.size());
    if (!needsPlan) {
        // An adjacency guard: after a shove or a load the cached next hop may
        // no longer touch the body, and following it would be a teleport.
        //
        // #80. THE VERTICAL SLACK IS THE GAIT'S. A walker's next hop is always
        // within one band, and holding a climber to that would tear up the
        // route on the tile before every planned drop -- the body would replan,
        // get the same route, and replan again, standing on the ledge.
        const PathStep& next = actor.route[static_cast<std::size_t>(actor.routeIndex)];
        const std::int32_t vertical = wardTypeClimbs(actor.type) ? kMaxPathDrop : 1;
        if (chebyshev(actor.x, actor.y, next.x, next.y) != 1 ||
            std::abs(actor.band - next.band) > vertical) {
            needsPlan = true;
        }
    }
    if (needsPlan) {
        if (tick_ < actor.routeRetryUntil) {
            return false;
        }
        // A SEARCH THAT CANNOT SUCCEED IS NEVER RUN. It is the most expensive
        // search there is -- it burns the whole node budget before answering no
        // -- and the component map already knows the answer. See
        // walkComponent_ for the arithmetic of six hundred bodies each asking
        // one impossible question every retry cooldown, forever.
        //
        // #80. A CLIMBER ASKS THE SAME QUESTION OF A BIGGER MAP. For a walker
        // the two cells must be on the same island, which is what it always
        // was. For a body that can haul itself up a wall the walking island and
        // the climb closure are one place, so the only impossible destination
        // is one the ward cannot reach at all -- and the guard has to loosen by
        // exactly that much and no more, or a thief on the roof-slum deck is
        // refused the search that would take him home.
        const bool climbs = wardTypeClimbs(actor.type);
        const bool routable =
            climbs ? climbReaches(tx, ty, tband) && climbReaches(actor.x, actor.y, actor.band)
                   : componentAt(tx, ty, tband) == componentAt(actor.x, actor.y, actor.band);
        if (!routable) {
            actor.routeRetryUntil = tick_ + kRouteRetryCooldownTicks;
            return false;
        }
        // AND THE CLIMB IS ONLY ASKED FOR WHEN THE CLIMB IS THE POINT.
        //
        // A hundred of the ward's bodies can climb, and almost every walk any
        // of them takes is street to street. Running those in the climbing gait
        // would pay for four extra geometry probes on every refused neighbour
        // of every search, for a route that is going to come out along the road
        // anyway -- so a body on the ward's own ground, walking to somewhere
        // else on the ward's own ground, walks exactly the way it always did
        // and costs exactly what it always cost. The gait is raised only when
        // one end of the journey is off the walking island, which is the only
        // time a wall has to be got over.
        const bool crossesTheClimb =
            climbs && (componentAt(tx, ty, tband) != mainComponent_ ||
                       componentAt(actor.x, actor.y, actor.band) != mainComponent_);
        // salt is id + 1 because zero means NO JITTER and actor id zero is a
        // real actor standing on a real street.
        const bool ok = finder_.find(PathStep{actor.x, actor.y, actor.band},
                                     PathStep{tx, ty, tband},
                                     static_cast<std::uint32_t>(actor.id) + 1u, actor.route,
                                     crossesTheClimb ? Gait::Climb : Gait::Walk);
        actor.routeIndex = 0;
        actor.routeTargetX = tx;
        actor.routeTargetY = ty;
        actor.routeTargetBand = tband;
        if (!ok || actor.route.empty()) {
            actor.route.clear();
            actor.routeRetryUntil = tick_ + kRouteRetryCooldownTicks;
            return false;
        }
    }
    const PathStep next = actor.route[static_cast<std::size_t>(actor.routeIndex)];
    if (!tryEnter(actor, next.x, next.y, next.band)) {
        return false;
    }
    ++actor.routeIndex;
    actor.facing = facingFromDelta(next.x - actor.prevX, next.y - actor.prevY);
    return true;
}

void WardPopulation::chargeStall(WardActor& actor, bool moved, bool closer) {
    if (moved && closer) {
        actor.goalWorkTicks = 0;
        return;
    }
    // A refused attempt costs five and a landed-but-no-closer step costs one.
    // And a tick the speed cadence swallowed costs NOTHING: charging that at
    // the wedge rate caps real detour tolerance at a third of what the constant
    // says, no matter what the constant says.
    if (!moved && actor.moveAccumTicks != 0) {
        return;
    }
    actor.goalWorkTicks += moved ? 1 : kPatrolStallWeight;
}

// --- the four verbs --------------------------------------------------------

void WardPopulation::actSeekFood(WardActor& actor) {
    // 1. Eat what is in the sack.
    if (actor.rations > 0) {
        --actor.rations;
        ledger_.foodEaten += 1;
        actor.needs[0] = static_cast<std::int16_t>(
            std::min(kNeedMax, actor.need(Need::Hunger) + kEatRestore));
        actor.starvingSince = -1;
        return;
    }
    const std::int32_t home = actor.id < static_cast<std::int32_t>(homeOf_.size())
                                  ? homeOf_[static_cast<std::size_t>(actor.id)]
                                  : -1;
    // 2. Take from the larder, standing in the room it is in.
    if (home >= 0 && actor.atHome() && homes_[static_cast<std::size_t>(home)].larder > 0) {
        // WardActor::atHome is the ROOM and not the bed -- see its comment.
        // Anything stricter starves four fifths of every household.
        --homes_[static_cast<std::size_t>(home)].larder;
        ++actor.rations;
        return;
    }
    // 3. Buy at a stall, standing at one. The coin goes back into the ledger's
    // sunk column rather than vanishing, which is what makes the coin identity
    // as exact as the food one.
    if (marketStock_ > 0 && actor.coin > 0) {
        for (const PathStep& stall : marketStalls_) {
            if (actor.band == stall.band && chebyshev(actor.x, actor.y, stall.x, stall.y) <= 2) {
                --marketStock_;
                --actor.coin;
                ++ledger_.coinSunk;
                ++actor.rations;
                return;
            }
        }
    }
    // 4. Walk to whichever is nearer and can actually feed you: a larder with
    // something in it, or the nearest stall with stock.
    std::int32_t bestX = actor.homeX;
    std::int32_t bestY = actor.homeY;
    std::int32_t bestBand = actor.homeBand;
    std::int32_t best = home >= 0 && homes_[static_cast<std::size_t>(home)].larder > 0
                            ? legDistance(actor, actor.homeX, actor.homeY, actor.homeBand)
                            : 1 << 20;
    if (marketStock_ > 0) {
        for (const PathStep& stall : marketStalls_) {
            const std::int32_t d = legDistance(actor, stall.x, stall.y, stall.band);
            if (d < best) {
                best = d;
                bestX = stall.x;
                bestY = stall.y;
                bestBand = stall.band;
            }
        }
    }
    stepToward(actor, bestX, bestY, bestBand);
}

void WardPopulation::actReturnHome(WardActor& actor) {
    if (actor.atHome()) {
        // HOME, AND PACKING FOR THE MORNING. Rations are banked here and not in
        // SEEK_FOOD, because SEEK_FOOD only ever runs when somebody is already
        // hungry -- so a body that only ever topped up while hungry would eat
        // every loaf the instant it picked one up and walk out to work with an
        // empty sack, every day, forever.
        const std::int32_t home = actor.id < static_cast<std::int32_t>(homeOf_.size())
                                      ? homeOf_[static_cast<std::size_t>(actor.id)]
                                      : -1;
        if (home >= 0 && actor.rations < kCarryRations &&
            homes_[static_cast<std::size_t>(home)].larder > 1) {
            --homes_[static_cast<std::size_t>(home)].larder;
            ++actor.rations;
        }
        return;
    }
    stepToward(actor, actor.homeX, actor.homeY, actor.homeBand);
}

void WardPopulation::actFlee(WardActor& actor, const TickContext& context) {
    // One drawn orthogonal step, leash ignored. Panic is not a plan.
    static constexpr std::int32_t dx[4] = {-1, 1, 0, 0};
    static constexpr std::int32_t dy[4] = {0, 0, -1, 1};
    const std::uint64_t roll = context.draw(cellKey(actor.x, actor.y, actor.band), 7);
    const int start = static_cast<int>(roll & 3u);
    for (int i = 0; i < 4; ++i) {
        const int n = (start + i) & 3;
        const std::int32_t nx = actor.x + dx[n];
        const std::int32_t ny = actor.y + dy[n];
        const std::int32_t nz = tiles_->stepBand(actor.x, actor.y, actor.band, nx, ny);
        if (nz != TileQuery::kNoBand && tryEnter(actor, nx, ny, nz)) {
            actor.route.clear();
            actor.routeTargetX = -1;
            return;
        }
    }
}

void WardPopulation::actLoiter(WardActor& actor, const TickContext& context) {
    // Standing about is standing about. One step in eight is a shuffle, so a
    // street of loiterers reads as alive rather than as a row of statues, and
    // seven in eight are still -- which is what keeps loitering cheap.
    if ((context.draw(static_cast<std::uint64_t>(actor.id), 3) & 7u) != 0) {
        return;
    }
    actFlee(actor, context);
}

// --- #80: the hunt ---------------------------------------------------------

std::int32_t WardPopulation::huntScore(const WardActor& predator) const noexcept {
    const WardTypeStats& stats = types_[predator.type];
    const std::int32_t hunger = predator.need(Need::Hunger);
    // The raws' OWN seekFood pricing, reused rather than re-invented: a starving
    // beast outranks a scared one at exactly the point a starving person does.
    // Cat and stray both come out at 655 hungry and 1005 desperate, against
    // FLEE's 950 and the wander job's 120.
    const std::int32_t priced =
        stats.seekFoodPriority +
        (hunger < kNeedCritical ? stats.needs[0].critBonus : stats.needs[0].lowBonus);
    // A LIVE LOCK IS NEVER ABANDONED for a scoring reason. A predator that
    // dropped its chase the moment its hunger ticked back over the band would
    // spend its life starting hunts.
    if (predator.huntTarget >= 0) {
        return priced;
    }
    if (hunger >= kNeedLow) {
        return 0;
    }
    if (tick_ < predator.huntBackoffUntil) {
        return 0;  // the futile-chase backoff: the wander gets a real window
    }
    if (tick_ % kSensePeriodTicks != 0) {
        return 0;  // between sense boundaries nothing is acquired
    }
    return senseNearestPrey(predator) >= 0 ? priced : 0;
}

std::int32_t WardPopulation::senseNearestPrey(const WardActor& predator) const noexcept {
    std::int32_t best = -1;
    std::int32_t bestDistance = kSenseRadius + 1;
    // THE MICE AND NOBODY ELSE. preyFirst_..preyEnd_ is the contiguous range
    // section 6 of the roster spawns last; this loop is the entire cost of the
    // ward's food chain and it is thirty-two iterations.
    for (std::int32_t id = preyFirst_; id < preyEnd_; ++id) {
        const WardActor& prey = actors_[static_cast<std::size_t>(id)];
        if (!prey.visible() || prey.band != predator.band) {
            continue;
        }
        const std::int32_t d = chebyshev(predator.x, predator.y, prey.x, prey.y);
        // STRICTLY NEARER, so a tie goes to the lower id on every machine and
        // two mice equidistant from one cat never swap between runs.
        if (d < bestDistance) {
            bestDistance = d;
            best = id;
        }
    }
    return best;
}

void WardPopulation::dropHuntLock(WardActor& predator) const noexcept {
    predator.huntTarget = -1;
    predator.huntTicks = 0;
}

void WardPopulation::actHunt(WardActor& predator) {
    if (predator.huntTarget < 0) {
        // Byte-identical to the probe huntScore() ran, so the two can never
        // disagree about whether there was anything to hunt.
        predator.huntTarget = senseNearestPrey(predator);
        predator.huntTicks = 0;
        if (predator.huntTarget < 0) {
            return;  // defensive: only reachable if the two probes diverged
        }
    }
    if (predator.huntTarget < preyFirst_ || predator.huntTarget >= preyEnd_) {
        dropHuntLock(predator);  // defensive: a lock can only ever hold a mouse
        return;
    }
    WardActor& prey = actors_[static_cast<std::size_t>(predator.huntTarget)];
    if (!prey.visible() || prey.band != predator.band ||
        chebyshev(predator.x, predator.y, prey.x, prey.y) > kLoseRadius) {
        // Somebody else got it, or the lock went stale. Close and re-sense.
        dropHuntLock(predator);
        return;
    }
    const std::int32_t distance = chebyshev(predator.x, predator.y, prey.x, prey.y);
    if (distance <= kContactRadius) {
        // THE CATCH. The mouse goes off the board with a revive countdown and
        // the predator eats.
        //
        // NO FOOD ITEM IS MINTED OR SUNK and foodEaten is deliberately NOT
        // touched: that counter is the ward's LOAF ledger and its conservation
        // identity is a gate. A cat eating a rat is not a loaf leaving a
        // larder, and pretending it was would put the identity out by one every
        // time anything in the district ate anything.
        prey.downedUntil = tick_ + kPreyReviveSeconds;
        prey.needs[0] = static_cast<std::int16_t>(kNeedMax);
        prey.starvingSince = -1;
        prey.route.clear();
        prey.routeTargetX = -1;
        prey.policy = WardPolicy::Loiter;
        // AND THE TILE IS FREE, exactly as a corpse frees its own -- see
        // WardActor::visible for why a body in a stomach must not hold a cell.
        occupancy_.remove(cellKey(prey.x, prey.y, prey.band));
        predator.needs[0] = static_cast<std::int16_t>(
            std::min(kNeedMax, predator.need(Need::Hunger) + kEatRestore));
        predator.starvingSince = -1;
        ++catches_;
        dropHuntLock(predator);
        return;
    }
    // THE PREY KNOWS. Driving SAFETY to nothing makes the mouse's own FLEE
    // score 950 next tick, above its wander job, and safety recovers over about
    // a hundred and fifty ticks -- so a mouse runs while it is being chased and
    // settles when it is not, without a second policy or a second need.
    if (distance <= kPreyPanicRadius) {
        prey.needs[3] = 0;
    }
    if (++predator.huntTicks > kChaseBudgetTicks) {
        // FUTILE. Both of the Java soak's futility classes end here: the
        // chokepoint freeze, where the route exists but its first hop is
        // plugged by bodies that will not move, and the untouchable-prey orbit,
        // where steps keep committing and contact never lands. The budget
        // counts TOTAL ticks under the lock and deliberately not only blocked
        // ones, because the orbit never blocks.
        predator.targetBand = 0;  // and the wander draws a fresh leg elsewhere
        predator.huntBackoffUntil = tick_ + kHuntBackoffTicks;
        ++futileChases_;
        dropHuntLock(predator);
        return;
    }
    // The chase itself. Leash-ignoring, exactly like SEEK_FOOD's walk: a hunt
    // legitimately ranges past the roost.
    //
    // THE AIM IS STICKY WITHIN TWO TILES, and that is a cost decision with a
    // number behind it. stepToward replans the moment the target cell changes,
    // and a mouse moves every tick -- so aiming at its exact cell would run a
    // fresh A* per predator per tick for the whole chase, which is the ward's
    // most expensive routine fired at its highest rate. Holding the old aim
    // while the prey stays within two tiles of it reuses the cached route and
    // costs nothing in accuracy: contact is adjacency, and a route that ends
    // two tiles from a mouse ends adjacent to it.
    std::int32_t aimX = prey.x;
    std::int32_t aimY = prey.y;
    std::int32_t aimBand = prey.band;
    if (predator.routeTargetBand == aimBand &&
        predator.routeIndex < static_cast<std::int32_t>(predator.route.size()) &&
        chebyshev(predator.routeTargetX, predator.routeTargetY, aimX, aimY) <= 2) {
        aimX = predator.routeTargetX;
        aimY = predator.routeTargetY;
    }
    stepToward(predator, aimX, aimY, aimBand);
    if (predator.route.empty() && tick_ < predator.routeRetryUntil) {
        // The router said there is no way there. A bounded abandon, backed by
        // the search cooldown that is already in the actor.
        dropHuntLock(predator);
    }
}

bool WardPopulation::revivePrey(WardActor& prey) {
    // A FIXED SPIRAL OUT OF THE DEN, ascending, first free standable cell wins.
    // The mouse that stands up is a fresh mouse out of the den rather than the
    // one that was eaten -- see kPreyReviveSeconds -- so it comes back where
    // the den is and not where it died.
    for (std::int32_t r = 0; r <= 4; ++r) {
        for (std::int32_t dy = -r; dy <= r; ++dy) {
            for (std::int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) {
                    continue;
                }
                const std::int32_t nx = prey.anchorX + dx;
                const std::int32_t ny = prey.anchorY + dy;
                const std::int32_t nb = prey.anchorBand;
                if (!tiles_->standable(nx, ny, nb) || occupancy_.at(cellKey(nx, ny, nb)) != 0) {
                    continue;
                }
                prey.x = nx;
                prey.y = ny;
                prey.band = nb;
                prey.prevX = nx;
                prey.prevY = ny;
                prey.prevBand = nb;
                prey.downedUntil = -1;
                prey.route.clear();
                prey.routeTargetX = -1;
                prey.targetBand = 0;
                prey.moveAccumTicks = 0;
                prey.goalWorkTicks = 0;
                prey.legMark = 0;
                occupancy_.add(cellKey(nx, ny, nb), prey.id);
                return true;
            }
        }
    }
    return false;
}

void WardPopulation::advanceLeg(WardActor& actor, const TickContext& context) {
    const std::int32_t route = actor.id < static_cast<std::int32_t>(routeOf_.size())
                                   ? routeOf_[static_cast<std::size_t>(actor.id)]
                                   : -1;
    actor.goalWorkTicks = 0;
    actor.legMark = 0;
    if (route >= 0) {
        const Route& r = routes_[static_cast<std::size_t>(route)];
        actor.leg = static_cast<std::int16_t>((actor.leg + 1) % std::max(1, r.count));
        const PathStep& wp = waypoints_[static_cast<std::size_t>(r.first + actor.leg)];
        actor.targetX = wp.x;
        actor.targetY = wp.y;
        actor.targetBand = wp.band;
        return;
    }
    // No authored route: a corner near the post, drawn and then snapped. The
    // radius shrinks on each retry so a beat whose corner is unreachable does
    // not simply keep asking for the same one.
    const WardTypeStats& stats = types_[actor.type];
    const std::int32_t reach = std::max(3, stats.leashRadius);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint64_t roll =
            context.draw(static_cast<std::uint64_t>(actor.id), 100 + attempt);
        const std::int32_t span = std::max(2, reach - attempt * (reach / 10));
        std::int32_t cx =
            actor.anchorX + static_cast<std::int32_t>(roll % static_cast<std::uint64_t>(2 * span + 1)) - span;
        std::int32_t cy =
            actor.anchorY +
            static_cast<std::int32_t>((roll >> 20) % static_cast<std::uint64_t>(2 * span + 1)) - span;
        std::int32_t cb = actor.anchorBand;
        // AT LEAST A FEW TILES AWAY, and it is not fussiness. A leg is paid on
        // ARRIVAL, so a drawn corner that lands next to where the body already
        // stands is paid the same tick it is picked, and the tick after, and
        // the tick after that -- a beast dwelling in place would refill its own
        // hunger every second and never die of anything.
        bool ok = snapToStandable(cx, cy, cb, 2);
        if (!ok && wardTypeClimbs(actor.type)) {
            // #80. A CLIMBER'S BEAT IS THE DECK AS WELL AS THE STREET.
            //
            // The snap above insists on the ward's WALKING island, which is the
            // right answer for a body that can only walk and the wrong one for
            // a Skyrunner: DOCKS-GAZETTEER section 2.5 has burglars using the
            // roof-slum layer as a highway, "fleeing across compound roofs
            // after breaking in through a ceiling". A thief whose corners were
            // all dragged down to the street would sleep on a roof it never
            // worked. So a climber gets a second ask that will take a deck --
            // and still refuses a cell the ward cannot reach at all, which is
            // what the component check is for.
            cx = actor.anchorX +
                 static_cast<std::int32_t>(roll % static_cast<std::uint64_t>(2 * span + 1)) - span;
            cy = actor.anchorY +
                 static_cast<std::int32_t>((roll >> 20) % static_cast<std::uint64_t>(2 * span + 1)) -
                 span;
            cb = actor.anchorBand;
            ok = snapToStandable(cx, cy, cb, 2, /*wantWalkable=*/false) &&
                 componentAt(cx, cy, cb) >= 0;
        }
        if (ok && legDistance(actor, cx, cy, cb) >= 3) {
            actor.targetX = cx;
            actor.targetY = cy;
            actor.targetBand = cb;
            return;
        }
    }
    // THE LAST RESORT, AND IT REFUSES A LEG THAT GOES NOWHERE.
    //
    // A leg is paid ON ARRIVAL, and arrival is adjacency -- so a leg whose
    // target is the cell the body is already standing on is paid the tick it is
    // picked, and the tick after, and forever. That is not a theory: a thief's
    // post IS its own bed, and a settle inside the thieving window puts it
    // exactly there with no leg yet. Eight failed draws would then have handed
    // it the anchor it was standing on and minted it two royals a second for
    // the rest of the game.
    //
    // So the fallback is taken only when the anchor is somewhere else. When it
    // is not, the leg stays UNSET (band zero is the sentinel; band zero is the
    // world's own VOID border and nobody can stand on it) and actPursue does
    // nothing this tick. The draws are keyed on the tick, so next tick asks a
    // different question -- which is what makes waiting a fix rather than a
    // deadlock.
    if (legDistance(actor, actor.anchorX, actor.anchorY, actor.anchorBand) >= 3) {
        actor.targetX = actor.anchorX;
        actor.targetY = actor.anchorY;
        actor.targetBand = actor.anchorBand;
        return;
    }
    actor.targetX = 0;
    actor.targetY = 0;
    actor.targetBand = 0;
}

void WardPopulation::actPursue(WardActor& actor, const TickContext& context) {
    const JobParams& params = wardJobParams(actor.job);

    if (!params.inWindow(secondOfDay_)) {
        // OFF SHIFT, AND IT GOES HOME LIKE EVERY OTHER TRADE.
        //
        // Three properties here are not incidental. It CLEARS the goal target
        // rather than parking the bed in it -- the first draft cached the home
        // cell as this leg's corner, and at the dawn tick the beat found itself
        // already standing on its "corner" and awarded a full unit of duty and
        // a full unit of skill for having slept. It walks with the leash
        // ignored, because a home routinely sits outside the post's leash. And
        // goalWorkTicks keeps its honest meaning across the boundary: zeroed at
        // home or when ground was made, incremented on a genuinely failed step,
        // never zeroed unconditionally -- which would launder a body stuck all
        // night out of the stall metric.
        // CLEARED, and this line is the whole of the dawn free-duty fix.
        //
        // The first draft of this parked the home cell in the goal target, and
        // that one line paid a guard for sleeping: at the dawn tick the in-window
        // branch below sees a target already set, SKIPS advanceLeg, adopts the
        // bed as this leg's waypoint, finds itself standing on it, and awards a
        // full unit of duty. A body that spent the night in bed came on shift
        // with a night's work already banked. targetBand == 0 is the sentinel
        // for "no leg yet" -- band zero is the world's own VOID border and
        // nobody can stand on it.
        actor.targetX = 0;
        actor.targetY = 0;
        actor.targetBand = 0;
        actor.legMark = 0;
        if (actor.atHome()) {
            actor.goalWorkTicks = 0;
            return;
        }
        // The walk home is CROSS-BAND and ignores the post's leash, because a
        // home routinely sits outside the leash of the shed somebody works in.
        const bool moved = stepToward(actor, actor.homeX, actor.homeY, actor.homeBand);
        actor.goalWorkTicks = moved ? 0 : actor.goalWorkTicks + 1;
        return;
    }

    const bool anchored = params.shape == WardJob::Anchor || params.shape == WardJob::Farm ||
                          params.shape == WardJob::Fish;
    if (anchored) {
        if (legDistance(actor, actor.anchorX, actor.anchorY, actor.anchorBand) <= kWorkReach &&
            actor.band == actor.anchorBand) {
            // In reach: STOP WALKING and work. Ten hands cannot share one tile,
            // and a crew that rings its own post all shift completes no units,
            // earns no duty and churns shoves.
            if (++actor.goalWorkTicks >= params.workTicksPerUnit) {
                actor.goalWorkTicks = 0;
                actor.needs[4] = static_cast<std::int16_t>(
                    std::min(kNeedMax, actor.need(Need::Duty) + params.dutyPerUnit));
                actor.coin += 1;
                ledger_.coinMinted += 1;
            }
            return;
        }
        stepToward(actor, actor.anchorX, actor.anchorY, actor.anchorBand);
        return;
    }

    // Everything else walks a leg to a target and takes the unit on ARRIVAL.
    if (actor.targetBand == 0) {
        advanceLeg(actor, context);
        if (actor.targetBand == 0) {
            // No leg to be had from here this tick -- see advanceLeg's last
            // resort. Stand still and ask again next tick, on a different draw.
            return;
        }
    }
    const std::int32_t before = legDistance(actor, actor.targetX, actor.targetY, actor.targetBand);
    // ARRIVAL IS ADJACENCY, not the cell itself. Under one-per-square the
    // waypoint an actor is walking to is routinely occupied by whoever got
    // there first, and requiring the cell would park a whole beat behind one
    // body standing on a corner.
    if (before <= 1) {
        actor.needs[4] = static_cast<std::int16_t>(
            std::min(kNeedMax, actor.need(Need::Duty) + params.dutyPerUnit));
        if (params.shape == WardJob::Wander) {
            // THE DEN NIBBLE. A beast feeds where it dwells and touches no
            // item: the bin scraps a mouse lives on are not the wastrel's
            // scavenge margin, and minting a loaf for a mouse would put the
            // ward's food ledger out by one every time a rat ate.
            //
            // #80 CLOSED THE GAP THIS NOTE USED TO RECORD. The nibble is what
            // feeds the MOUSE, and it is all a mouse ever had; what a cat or a
            // stray had was the same nibble and nothing else, which is why the
            // district's mice were a population nothing ate. The predator's own
            // channel is WardPolicy::Hunt, with the sense probe, the chase
            // budget, the futile-chase backoff and the prey revive the Java
            // build spent a sprint arriving at -- all four, because a
            // half-ported hunt with no backoff is the exact bug it found (a
            // gull pinned eight thousand ticks against a plugged alcove).
            //
            // The nibble stays for the prey and for the dogs, whose wander is
            // their whole life and who hunt nothing. FOR A PREDATOR IT IS A
            // SCRAP AND NOT A MEAL -- see kScavengeCeiling for why that one
            // clamp is the difference between a food chain and a hunt nothing
            // ever triggers.
            const std::int32_t ceiling = isPredator(actor.type) ? kScavengeCeiling : kNeedMax;
            actor.needs[0] = static_cast<std::int16_t>(std::max(
                static_cast<std::int32_t>(actor.needs[0]),
                std::min(ceiling, actor.need(Need::Hunger) + 1500)));
            actor.starvingSince = -1;
        }
        if (params.shape == WardJob::Scavenge || params.shape == WardJob::Thieving) {
            // The bins pay in food and the dark pays in coin, which is the
            // whole economic difference between a hungry child and a thief.
            if (params.shape == WardJob::Scavenge) {
                ++actor.rations;
                ++ledger_.foodMinted;
            } else {
                actor.coin += 2;
                ledger_.coinMinted += 2;
            }
        }
        advanceLeg(actor, context);
        return;
    }
    const bool moved = stepToward(actor, actor.targetX, actor.targetY, actor.targetBand);
    const std::int32_t after = legDistance(actor, actor.targetX, actor.targetY, actor.targetBand);
    // THE HIGH-WATER MARK. A step that beats the closest this leg has ever come
    // zeroes the stall clock; anything else is stall, including a step that
    // lands somewhere new and no nearer. Stored as distance + 1 so zero can
    // mean "no mark yet".
    const bool closer = actor.legMark == 0 || after + 1 < actor.legMark;
    if (closer) {
        actor.legMark = after + 1;
    }
    chargeStall(actor, moved, closer);
    if (actor.goalWorkTicks >= kPatrolNoProgressYieldSteps + (actor.id % 19 - 9) * kPatrolStallWeight) {
        // DE-PHASED BY ID, and denominated in the attempt weight. Two bodies
        // wedged against each other start their stall clocks on the same tick;
        // a shared budget fires for both, both turn round, both meet again --
        // a polite deadlock. Putting ids one apart a fifth of an attempt apart
        // rounds to nothing and does not break it, so the spread is in units of
        // whole attempts and CENTRED on the band, which keeps the mean wait
        // where it was.
        advanceLeg(actor, context);
    }
}

// --- the tick --------------------------------------------------------------

void WardPopulation::tickActor(WardActor& actor, const TickContext& context) {
    actor.prevX = actor.x;
    actor.prevY = actor.y;
    actor.prevBand = actor.band;
    decayNeeds(actor);
    if (auditStarvation(actor)) {
        return;
    }
    const WardPolicy policy = selectPolicy(actor);
    actor.policy = policy;
    switch (policy) {
        case WardPolicy::Flee: actFlee(actor, context); break;
        case WardPolicy::SeekFood: actSeekFood(actor); break;
        case WardPolicy::ReturnHome: actReturnHome(actor); break;
        case WardPolicy::Pursue: actPursue(actor, context); break;
        case WardPolicy::Loiter: actLoiter(actor, context); break;
        case WardPolicy::Hunt: actHunt(actor); break;
        case WardPolicy::Dead: break;
    }
    // A HUNT THAT STOPPED BEING THE PLAN LETS GO OF ITS PREY. Without this a
    // predator that is scared off or dragged home keeps its lock, and the mouse
    // it was chasing is invisible to every other predator in the ward for as
    // long as the lock survives -- a hunt nobody is running that blocks the
    // hunts that would.
    if (policy != WardPolicy::Hunt && actor.huntTarget >= 0) {
        dropHuntLock(actor);
    }
    // A blocked step is a shove, and only a shove: a body that could not move
    // because somebody is standing there, and had somewhere legal to be.
    if (actor.x == actor.prevX && actor.y == actor.prevY && actor.band == actor.prevBand &&
        !actor.route.empty() && actor.routeIndex < static_cast<std::int32_t>(actor.route.size())) {
        const PathStep& next = actor.route[static_cast<std::size_t>(actor.routeIndex)];
        if (occupancy_.at(cellKey(next.x, next.y, next.band)) >= kMaxOccupantsPerCell) {
            tryPush(actor, next.x, next.y, next.band, context);
        }
    }
}

void WardPopulation::tick(const TickContext& context) {
    tick_ = context.tick();
    secondOfDay_ = static_cast<std::int32_t>((tick_ + clockOffset_) % kSecondsPerDay);
    runDailyProvision();
    for (WardActor& actor : actors_) {
        if (actor.dead) {
            continue;
        }
        // #80. A BODY THAT HAS BEEN EATEN DOES NOT TICK, and it comes back at
        // an ABSOLUTE tick rather than off a countdown -- the same rule every
        // other latch in this file keeps, and for the same reason: a countdown
        // somebody forgets to decrement is a mouse that never stands up again.
        if (actor.downedUntil >= 0) {
            if (tick_ < actor.downedUntil) {
                continue;
            }
            if (!revivePrey(actor)) {
                // The den is full. Wait rather than stack: one body per square
                // is the owner's rule and a revive is not an exception to it.
                actor.downedUntil = tick_ + kSensePeriodTicks;
                continue;
            }
        }
        tickActor(actor, context);
    }
    // The worst pile-up seen, measured rather than asserted: the most bodies
    // ever standing inside one tile of each other. "No guard pile-ups" is a
    // number or it is an opinion.
    if ((context.tick() % 240) == 0) {
        std::int32_t worst = 0;
        for (const WardActor& actor : actors_) {
            if (!actor.visible() || actor.type != WardType::MilitiaWatch) {
                continue;
            }
            std::int32_t near = 0;
            for (const WardActor& other : actors_) {
                if (other.visible() && other.type == WardType::MilitiaWatch &&
                    other.band == actor.band && chebyshev(actor.x, actor.y, other.x, other.y) <= 1) {
                    ++near;
                }
            }
            worst = std::max(worst, near);
        }
        worstJam_ = std::max(worstJam_, worst);
    }
}

void WardPopulation::skipToSecond(std::int32_t second) {
    const std::int32_t wanted = ((second % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    if (wanted == secondOfDay_) {
        return;  // the clocks already agree, which is every ordinary step
    }
    // FORWARD, ALWAYS. Asking for an hour earlier than the current one means
    // the next such hour, which is what "skip to two in the morning" means when
    // it is three in the afternoon -- and it is what makes the day number
    // advance, the larders restock and a slept night cost the ward a day's
    // food. Winding the clock backwards would be a save-load, not a skip.
    clockOffset_ += (wanted - secondOfDay_ + kSecondsPerDay) % kSecondsPerDay;
    secondOfDay_ = wanted;
    runDailyProvision();
    settleToSchedule();
}

void WardPopulation::settleToSchedule() {
    // ASCENDING ID, and the occupancy index is rebuilt as we go, so whoever has
    // the lower id gets the tile and the next body takes the next free one.
    // That is a rule and not an accident, which is what makes the settled ward
    // the same ward twice from the same seed.
    occupancy_.clear();
    for (WardActor& actor : actors_) {
        // A settle puts everybody where the hour says they should be. Nobody
        // means nobody: not the starved, and not a mouse still in a stomach --
        // placing a downed body would hand it a tile it is not entitled to and
        // then revivePrey would hand it a second one.
        if (!actor.visible()) {
            continue;
        }
        const JobParams& params = wardJobParams(actor.job);
        const bool working = actor.job != WardJob::None && params.inWindow(secondOfDay_);
        std::int32_t wx = working ? actor.anchorX : actor.homeX;
        std::int32_t wy = working ? actor.anchorY : actor.homeY;
        std::int32_t wb = working ? actor.anchorBand : actor.homeBand;
        // A BEAT IS A ROUTE AND NOT A POST. Settling every watchman onto the
        // patrol post it started from puts the whole Tarwalk beat in one heap
        // at one end of the Tarwalk, which is the opposite of what a beat is
        // for. Each of them starts on ITS OWN leg -- the legs were staggered by
        // id at the bake for exactly this -- so a settled district has the
        // Watch spread along the road rather than standing on each other.
        const std::int32_t route = actor.id < static_cast<std::int32_t>(routeOf_.size())
                                       ? routeOf_[static_cast<std::size_t>(actor.id)]
                                       : -1;
        if (working && route >= 0) {
            const Route& r = routes_[static_cast<std::size_t>(route)];
            if (r.count > 0) {
                const PathStep& wp =
                    waypoints_[static_cast<std::size_t>(r.first + (actor.leg % r.count))];
                wx = wp.x;
                wy = wp.y;
                wb = wp.band;
            }
        }
        // The first free standable cell out from where they belong. A crew of
        // ten sharing one post spreads over the shed's own floor rather than
        // stacking, which is the same thing kWorkReach buys them while the
        // simulation is actually running.
        bool placed = false;
        for (std::int32_t r = 0; r <= 6 && !placed; ++r) {
            for (std::int32_t dy = -r; dy <= r && !placed; ++dy) {
                for (std::int32_t dx = -r; dx <= r && !placed; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    if (!tiles_->standable(wx + dx, wy + dy, wb)) {
                        continue;
                    }
                    // #80: AND SOMEWHERE THE WARD CAN ACTUALLY REACH. A settle
                    // that put a body on a standable cell nobody can get to
                    // would strand it exactly as surely as a bed on a sealed
                    // deck -- and on a roof deck, where a hut's own tile has
                    // unreachable neighbours a tile away, that stopped being
                    // hypothetical. -1 is "nowhere"; both islands are fine.
                    if (componentAt(wx + dx, wy + dy, wb) < 0) {
                        continue;
                    }
                    if (occupancy_.at(cellKey(wx + dx, wy + dy, wb)) != 0) {
                        continue;
                    }
                    actor.x = wx + dx;
                    actor.y = wy + dy;
                    actor.band = wb;
                    placed = true;
                }
            }
        }
        if (!placed) {
            actor.x = wx;
            actor.y = wy;
            actor.band = wb;
        }
        actor.prevX = actor.x;
        actor.prevY = actor.y;
        actor.prevBand = actor.band;
        actor.moveAccumTicks = 0;
        actor.goalWorkTicks = 0;
        actor.legMark = 0;
        actor.routeRetryUntil = 0;
        actor.route.clear();
        actor.routeTargetX = -1;
        actor.routeTargetY = -1;
        actor.routeTargetBand = -1;
        actor.targetBand = 0;
        occupancy_.add(cellKey(actor.x, actor.y, actor.band), actor.id);
    }
}

// --- the food economy ------------------------------------------------------

void WardPopulation::runDailyProvision() {
    const std::int64_t day = (tick_ + clockOffset_) / kSecondsPerDay;
    if (day == lastProvisionDay_) {
        return;
    }
    lastProvisionDay_ = day;
    // The courtyards and the fishing grounds feed the ward once a day. MINTED,
    // and counted: every loaf that enters the district is on the left-hand side
    // of the conservation identity and every loaf eaten is on the right.
    //
    // THE SIZE OF THE RESTOCK IS THE BALANCE KNOB. A reserve drains 24,000
    // points a day and a meal is 8,000, so three meals a day per resident is
    // break-even and anything less is a slow, visible, correct famine. Four is
    // a ward that feeds itself with a thin margin, which is what a working
    // district looks like -- see the ward soak, which fails the build if the
    // margin turns out to be either a famine or a glut.
    for (Home& home : homes_) {
        const std::int32_t want = 4 * std::max(1, home.residents);
        if (home.larder < want) {
            ledger_.foodMinted += want - home.larder;
            home.larder = want;
        }
    }
    const std::int32_t stallWant = 400;
    if (marketStock_ < stallWant) {
        ledger_.foodMinted += stallWant - marketStock_;
        marketStock_ = stallWant;
    }
}

std::int64_t WardPopulation::foodHeld() const noexcept {
    std::int64_t held = marketStock_;
    for (const Home& home : homes_) {
        held += home.larder;
    }
    for (const WardActor& actor : actors_) {
        held += actor.rations;
    }
    return held;
}

// --- being spoken to -------------------------------------------------------
//
// #79. Everything in this block exists so that pressing E on a street corner
// reaches the body standing on it. None of it decides anything: no policy reads
// a name, and nothing here is in the tick loop.

namespace {

/// The empty answer for an id nobody baked. Static so the accessor can return a
/// reference and callers never have to null-check a name.
const WardIdentity& nobody() {
    static const WardIdentity kNobody;
    return kNobody;
}

}  // namespace

const WardIdentity& WardPopulation::identity(std::int32_t actorId) const noexcept {
    if (actorId < 0 || static_cast<std::size_t>(actorId) >= identities_.size()) {
        return nobody();
    }
    return identities_[static_cast<std::size_t>(actorId)];
}

const WardActor* WardPopulation::byId(std::int32_t actorId) const noexcept {
    // Ids are assigned 0..N-1 in bake order and actors_ is never reordered, so
    // the id IS the index. Guarded anyway: an id from outside would otherwise
    // read off the end of the roster.
    if (actorId < 0 || static_cast<std::size_t>(actorId) >= actors_.size()) {
        return nullptr;
    }
    return &actors_[static_cast<std::size_t>(actorId)];
}

const WardActor* WardPopulation::nearestTo(std::int32_t x, std::int32_t y, std::int32_t band,
                                           std::int32_t reachTiles) const noexcept {
    const WardActor* best = nullptr;
    std::int32_t bestDistance = reachTiles + 1;
    for (const WardActor& actor : actors_) {
        if (!actor.visible() || actor.band != band) {
            continue;
        }
        const std::int32_t dx = actor.x - x;
        const std::int32_t dy = actor.y - y;
        const std::int32_t distance = std::max(std::abs(dx), std::abs(dy));
        // STRICTLY NEARER, so a tie resolves to the lower id on every machine
        // and two bodies one tile apart never swap which one you are talking to
        // between one run and the next.
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &actor;
        }
    }
    return best;
}

std::int32_t WardPopulation::takeCoinFrom(std::int32_t actorId, std::int32_t coin) noexcept {
    if (actorId < 0 || static_cast<std::size_t>(actorId) >= actors_.size() || coin <= 0) {
        return 0;
    }
    WardActor& actor = actors_[static_cast<std::size_t>(actorId)];
    const std::int32_t taken = std::min(coin, actor.coin);
    actor.coin -= taken;
    // AND IT IS SUNK, because it has left the ward.
    //
    // The ledger's identity is `coinMinted - coinSunk == what the ward's purses
    // hold`, checked every three hundred ticks, and it is a GATE rather than a
    // report: an economy that can quietly create or destroy a royal balances
    // itself by accident. The player's purse is not one of the ward's -- it
    // belongs to the Tavern and is hashed there -- so a hand in a dockhand's
    // pocket moves coin ACROSS that boundary, and a robbery that did not say so
    // would break the identity the first time anybody committed one.
    ledger_.coinSunk += taken;
    return taken;
}

void WardPopulation::faceToward(std::int32_t actorId, std::int32_t x, std::int32_t y) noexcept {
    if (actorId < 0 || static_cast<std::size_t>(actorId) >= actors_.size()) {
        return;
    }
    WardActor& actor = actors_[static_cast<std::size_t>(actorId)];
    const std::int32_t dx = x - actor.x;
    const std::int32_t dy = y - actor.y;
    if (dx == 0 && dy == 0) {
        return;
    }
    // The four-point facing the renderer draws from, picked by which axis is
    // further. Integer only: a body's heading is simulation state and an
    // atan2 here would put a double in the middle of it.
    if (std::abs(dx) >= std::abs(dy)) {
        actor.facing = dx > 0 ? kFacingEast : kFacingWest;
    } else {
        actor.facing = dy > 0 ? kFacingSouth : kFacingNorth;
    }
}

std::int32_t WardPopulation::witnessesAround(std::int32_t actorId,
                                             std::int32_t reachTiles) const noexcept {
    const WardActor* victim = byId(actorId);
    if (victim == nullptr) {
        return 0;
    }
    std::int32_t seen = 0;
    for (const WardActor& actor : actors_) {
        if (actor.id == actorId || actor.dead || !isPerson(actor.type)) {
            continue;
        }
        if (actor.band != victim->band) {
            // The taproom's witness filter learned this the hard way: a body
            // asleep one floor up is not in the room, whatever its (x, y) says.
            continue;
        }
        const std::int32_t dx = actor.x - victim->x;
        const std::int32_t dy = actor.y - victim->y;
        if (std::max(std::abs(dx), std::abs(dy)) <= reachTiles) {
            ++seen;
        }
    }
    return seen;
}

// --- reporting -------------------------------------------------------------

WardCensus WardPopulation::census() const {
    WardCensus out;
    for (const WardActor& actor : actors_) {
        ++out.total;
        ++out.byType[static_cast<std::size_t>(actor.type)];
        if (isPerson(actor.type)) {
            ++out.people;
        } else {
            ++out.beasts;
        }
        const bool labouring = actor.type == WardType::Serf || actor.type == WardType::Sailor ||
                               actor.type == WardType::Fisher || actor.type == WardType::Carter;
        if (labouring) {
            ++out.serfs;
        }
        // #80. THE ROOF ROLL IS COUNTED OFF THE BED AND NOT OFF THE BAND, so it
        // counts the same people at eight in the morning when they are all down
        // in the street as it does at midnight when they are all up there.
        // `onRoofNow` is the other half and is a fact about this instant.
        if (actor.homeOnTheRoof) {
            ++out.roofHomed;
            ++out.roofHomedByType[static_cast<std::size_t>(actor.type)];
        }
        if (isPrey(actor.type)) {
            ++out.prey;
        }
        if (actor.dead) {
            ++out.starved;
            if (labouring) {
                ++out.serfsStarved;
            }
            continue;
        }
        ++out.alive;
        if (actor.downedUntil >= 0) {
            // Caught, and not yet back out of the den. Alive, on the roll, and
            // not on the board.
            continue;
        }
        if (isPrey(actor.type)) {
            ++out.preyUp;
        }
        if (componentAt(actor.x, actor.y, actor.band) != mainComponent_) {
            ++out.onRoofNow;
        }
        ++out.byPolicy[static_cast<std::size_t>(actor.policy)];
        if (actor.need(Need::Hunger) < kNeedLow) {
            ++out.hungry;
        }
        if (actor.band == actor.anchorBand &&
            chebyshev(actor.x, actor.y, actor.anchorX, actor.anchorY) <= kWorkReach) {
            ++out.atPost;
        }
        if (actor.atHome()) {
            ++out.atHome;
        }
    }
    return out;
}

std::int32_t WardPopulation::countIn(std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                     std::int32_t y1, std::int32_t band) const noexcept {
    std::int32_t n = 0;
    for (const WardActor& actor : actors_) {
        if (actor.visible() && actor.band == band && actor.x >= x0 && actor.x <= x1 &&
            actor.y >= y0 && actor.y <= y1) {
            ++n;
        }
    }
    return n;
}

std::int32_t WardPopulation::countIn(std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                     std::int32_t y1, std::int32_t band,
                                     WardType type) const noexcept {
    std::int32_t n = 0;
    for (const WardActor& actor : actors_) {
        if (actor.visible() && actor.type == type && actor.band == band && actor.x >= x0 &&
            actor.x <= x1 && actor.y >= y0 && actor.y <= y1) {
            ++n;
        }
    }
    return n;
}

std::string WardPopulation::reportLine() const {
    const WardCensus roll = census();
    std::string out = "ward[roll=" + content::dec(static_cast<std::uint64_t>(roll.total));
    out += " alive=" + content::dec(static_cast<std::uint64_t>(roll.alive));
    out += " starved=" + content::dec(static_cast<std::uint64_t>(roll.starved));
    out += " atpost=" + content::dec(static_cast<std::uint64_t>(roll.atPost));
    out += " athome=" + content::dec(static_cast<std::uint64_t>(roll.atHome));
    out += " hungry=" + content::dec(static_cast<std::uint64_t>(roll.hungry));
    out += " shoves=" + content::dec(static_cast<std::uint64_t>(shoves_));
    out += " food=" + content::dec(static_cast<std::uint64_t>(foodHeld()));
    // #80. THE TWO NEW FACTS, printed where every gate and every --selftest can
    // read them. `roof` is how many beds are on a deck and how many bodies are
    // standing on one right now; `mice` is how many of the prey are on the
    // board out of the roll, and `ate` is how many times the ward's cats and
    // strays have actually caught one. A mouse count that only ever reads 32/32
    // with ate=0 is a district with no food chain in it, and this line is where
    // that would be visible without running a test.
    out += " roof=" + content::dec(static_cast<std::uint64_t>(roll.roofHomed)) + "/" +
           content::dec(static_cast<std::uint64_t>(roll.onRoofNow));
    out += " mice=" + content::dec(static_cast<std::uint64_t>(roll.preyUp)) + "/" +
           content::dec(static_cast<std::uint64_t>(roll.prey));
    out += " ate=" + content::dec(static_cast<std::uint64_t>(catches_));
    out += " hour=" + content::dec(static_cast<std::uint64_t>(secondOfDay_ / 3600)) + "]";
    return out;
}

// --- the hash --------------------------------------------------------------

void WardPopulation::hash_into(HashSink& sink) const {
    // ASCENDING ID, and everything a policy READS is folded.
    //
    // The rule the Java build learned the hard way: a scalar a policy reads and
    // the hash does not cover is a divergence that never shows up. So position,
    // needs, home, post, target, leg, the stall clock, the mark, the sack, the
    // coin and the policy itself all go in. The route cache does not, because
    // it is a pure function of the three things above it that do.
    sink.put_int(static_cast<std::uint32_t>(actors_.size()));
    sink.put_int(static_cast<std::uint32_t>(secondOfDay_));
    // The clock's own offset and the day the larders were last filled. Both are
    // read by behaviour -- the offset decides every window and the day decides
    // whether anybody eats tomorrow -- so both are hashed. The rule this
    // codebase learned the hard way is that a scalar a policy READS and the
    // hash does not cover is a divergence nothing will ever see.
    sink.put_long(static_cast<std::uint64_t>(clockOffset_));
    sink.put_long(static_cast<std::uint64_t>(lastProvisionDay_));
    for (const WardActor& actor : actors_) {
        sink.put_byte(static_cast<std::uint32_t>(actor.type));
        sink.put_byte(static_cast<std::uint32_t>(actor.job));
        sink.put_byte(static_cast<std::uint32_t>(actor.policy));
        sink.put_byte(actor.dead ? 1u : 0u);
        sink.put_int(static_cast<std::uint32_t>(actor.x));
        sink.put_int(static_cast<std::uint32_t>(actor.y));
        sink.put_int(static_cast<std::uint32_t>(actor.band));
        sink.put_short(static_cast<std::uint32_t>(actor.facing));
        for (std::size_t n = 0; n < kNeedCount; ++n) {
            sink.put_short(static_cast<std::uint32_t>(actor.needs[n]));
            sink.put_int(static_cast<std::uint32_t>(actor.needAccum[n]));
        }
        sink.put_int(static_cast<std::uint32_t>(actor.homeX));
        sink.put_int(static_cast<std::uint32_t>(actor.homeY));
        sink.put_int(static_cast<std::uint32_t>(actor.homeBand));
        sink.put_int(static_cast<std::uint32_t>(actor.anchorX));
        sink.put_int(static_cast<std::uint32_t>(actor.anchorY));
        sink.put_int(static_cast<std::uint32_t>(actor.anchorBand));
        sink.put_int(static_cast<std::uint32_t>(actor.targetX));
        sink.put_int(static_cast<std::uint32_t>(actor.targetY));
        sink.put_int(static_cast<std::uint32_t>(actor.targetBand));
        sink.put_short(static_cast<std::uint32_t>(actor.leg));
        sink.put_int(static_cast<std::uint32_t>(actor.goalWorkTicks));
        sink.put_int(static_cast<std::uint32_t>(actor.legMark));
        sink.put_int(static_cast<std::uint32_t>(actor.moveAccumTicks));
        sink.put_int(static_cast<std::uint32_t>(actor.rations));
        sink.put_int(static_cast<std::uint32_t>(actor.coin));
        sink.put_long(static_cast<std::uint64_t>(actor.lastPushTick));
        sink.put_long(static_cast<std::uint64_t>(actor.starvingSince));
        // #80. THE HUNT LOCK AND THE ROOF BED. Every one of these is read by a
        // policy -- the lock decides whether a predator chases or wanders, the
        // budget decides when it gives up, the backoff decides when it may
        // start again, the countdown decides whether a mouse is on the board at
        // all, and the roof flag is what a bake put in a hut. The rule this
        // codebase learned the hard way is that a scalar a policy READS and the
        // hash does not cover is a divergence nothing will ever see.
        sink.put_int(static_cast<std::uint32_t>(actor.huntTarget));
        sink.put_int(static_cast<std::uint32_t>(actor.huntTicks));
        sink.put_long(static_cast<std::uint64_t>(actor.huntBackoffUntil));
        sink.put_long(static_cast<std::uint64_t>(actor.downedUntil));
        sink.put_byte(actor.homeOnTheRoof ? 1u : 0u);
    }
    for (const Home& home : homes_) {
        sink.put_int(static_cast<std::uint32_t>(home.larder));
    }
    sink.put_int(static_cast<std::uint32_t>(marketStock_));
    sink.put_long(static_cast<std::uint64_t>(ledger_.foodMinted));
    sink.put_long(static_cast<std::uint64_t>(ledger_.foodEaten));
    sink.put_long(static_cast<std::uint64_t>(ledger_.coinMinted));
    sink.put_long(static_cast<std::uint64_t>(ledger_.coinSunk));
    sink.put_long(static_cast<std::uint64_t>(shoves_));
    sink.put_long(static_cast<std::uint64_t>(catches_));
    sink.put_long(static_cast<std::uint64_t>(futileChases_));
}

}  // namespace granadad::sim
