#include "granadad/sim/tavern.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "granadad/sim/fixed.hpp"

namespace granadad::sim {

namespace {

/// Whether `second` falls in [from, until), where the pair may wrap midnight.
[[nodiscard]] bool withinDayWindow(std::int32_t second, std::int32_t from,
                                   std::int32_t until) noexcept {
    if (from <= until) {
        return second >= from && second < until;
    }
    return second >= from || second < until;
}

/// Diagonal shoves would otherwise be sqrt(2) stronger than straight ones. Same
/// 46341/65536 the body's movement uses.
[[nodiscard]] std::int32_t diagonalScaled(std::int32_t impulse) noexcept {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(impulse) * 46341) >> 16);
}

/// How long a patron nurses one drink, in simulated seconds.
constexpr std::int32_t kPatronDrinkEverySeconds = 240;
/// Coin a patron arrives with. A day's wage, near enough, and it runs out.
constexpr std::int32_t kPatronPurse = 14;

/// One line of roster, so the cast reads as a table rather than as a hundred
/// lines of constructor calls.
struct RosterEntry {
    const char* name;
    const char* epithet;
    ActorRole role;
    std::int32_t postX;
    std::int32_t postY;
    std::int32_t fromSecond;
    std::int32_t toSecond;
    Activity activity;
    std::int32_t coin;
    // --- S3: who they are socially -----------------------------------------
    /// A content/raws/names/notables.json id, or "". Only three of this roster
    /// are among the Forty: Master Venn, who owns the house; Father Maell, who
    /// drinks in it; and Captain Ivo Wake, whose ship is on the fishbone pier
    /// and whose trade this house is named for. The other twelve are Venn's
    /// hired staff and the ward's dockers, and the raws never named them --
    /// which is why they speak from the generic tables and not from a personal
    /// one somebody would have had to invent.
    const char* notableId;
    /// Which greet.<family>.* set of tables they answer from.
    JobFamily family;
    /// The trade they will talk shop about, out of the authored mastery lines.
    const char* skillId;
    std::int32_t skillLevel;
    /// Their streetwise: what a haggle is fought with, and what notices a hand
    /// in a purse.
    std::int32_t streetwise;
    /// S4. The faction this one SPEAKS FOR, or "". Belonging to a faction is
    /// derived from the job family through the owner's own factions.json
    /// (see Tavern::factionOf) -- every patron in this room is a dockhand and
    /// nobody had to write that down. Recruiting is a different fact, and it is
    /// a job somebody has: four of this cast can put your name on a roll and
    /// the rest are members who cannot.
    ///
    /// VERIFICATION GAP (S4): the WATCH has no recruiter here, so it cannot be
    /// joined IN PLAY -- the garrison is K34 and the watchpost K21, and this
    /// build simulates one room. Its ladder, its rivalry and its influence all
    /// work and are proved through the sim API, and the case that puts a
    /// Skyrunner and a Watch member in the same taproom joins the Watch through
    /// the ledger rather than through a conversation. What is missing is a
    /// door, not a mechanism.
    const char* recruits;
};

// THE CAST OF THE GILDED GULL.
//
// Master Venn and Father Maell are canon by name in
// content/raws/names/notables.json. The other four staff are Venn's "six hired
// staff", which that same file says he has; their names are drawn from the
// authored pools in content/raws/names/names.json -- shopkeeper given names and
// surnames for the bartender, militia-watch given names and shopkeeper
// epithets for the two bouncers, wastrel given names and epithets for the
// Skyrunner contact -- so nobody here is named out of nowhere.
//
// Father Maell taking an evening hour in a captains' house is not a licence
// with the canon: DOCKS-GAZETTEER §3 has him writing three unanswered letters
// to the monastery about "something wrong on the water" before the first body
// came up, and the Gilded Gull is where the men who were on the water drink.
constexpr std::array<RosterEntry, 6> kStaff = {{
    {"Master Venn", "landlord of the Gilded Gull", ActorRole::Innkeeper, 158, 76,
     hourOfDay(7), hourOfDay(1), Activity::Working, 300,
     // notables.json: streetwise 30, and "each certain the other hears more".
     "venn", JobFamily::Trade, "streetwise", 30, 30, "merchants"},
    {"Gerta Saltcotte", "the Fair-Weight", ActorRole::Bartender, gull::kBartenderX,
     gull::kBartenderY, hourOfDay(10, 30), hourOfDay(2, 30), Activity::Working, 80,
     "", JobFamily::Trade, "streetwise", 18, 18, ""},
    {"Ox Gullbane", "Slab-Fist", ActorRole::Bouncer, 153, 67, hourOfDay(11), hourOfDay(20),
     Activity::Watching, 20, "", JobFamily::Serf, "kit_keeping", 12, 10, ""},
    {"Kled Tarbeck", "the Patient", ActorRole::Bouncer, 154, 75, hourOfDay(18), hourOfDay(3),
     Activity::Watching, 20, "", JobFamily::Serf, "streetwise", 14, 14, ""},
    {"Father Maell", "of the Mission", ActorRole::PriestOfTheFlame, 149, 74, hourOfDay(19),
     hourOfDay(21, 30), Activity::Drinking, 8,
     // notables.json: channeling 40. A master, and the tables have master lines.
     // He speaks for the Mission because DOCKS-GAZETTEER section 3 says the
     // Mission is his: soup, bunks, and a disciple always awake.
     "maell", JobFamily::Clergy, "channeling", 40, 8, "temple"},
    // S5 RENAMES THIS ONE, and it is a correction rather than a flourish. S2
    // invented "Wisp Low-Tide" out of the authored wastrel name pools because
    // the Skyrunners had no line to hang anything on. They do now, and the
    // owner named him first: content/raws/names/notables.json carries `finch`,
    // Finch, "the quiet tenant", sited at LAIR_SKYRUNNER -- and ranks.json's
    // own note says the Skyrunners' first rung is called Tenant BECAUSE of that
    // epithet. He also has a personal bark table and an authored micro-history
    // with Gullet Mag, neither of which an invented name can ever reach.
    {"Finch", "the quiet tenant", ActorRole::SkyrunnerContact, 158, 68, hourOfDay(22),
     hourOfDay(3), Activity::Drinking, 60, "finch", JobFamily::Wastrel, "skyrunning", 30, 26,
     "skyrunners"},
}};

/// The patrons. Two thin hours at midday when the lunch trade is in, and then
/// the whole crowd from the dusk pay-out until the small hours -- the wage loop
/// DOCKS-GAZETTEER §4 describes: dawn muster, cargo work, dusk pay-out, tavern.
///
/// S3 adds ONE, and only one, and he is canon: Captain Ivo Wake, master of the
/// Kestrel on the fishbone pier. The Gull is the CAPTAINS' tavern
/// (DOCKS-GAZETTEER §3); Wake "was at sea the night of the killings and can
/// prove it, which interests him more than it should", which is a man who wants
/// to be asked. He is here so at least one of the Forty is a person you can
/// walk up to, rather than a row in a JSON file.
constexpr std::array<RosterEntry, 10> kPatrons = {{
    {"Bram Marrow", "the Steady", ActorRole::Patron, 149, 69, hourOfDay(12), hourOfDay(14),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "fieldcraft", 11, 8, ""},
    {"Marta Coldquay", "Crane-Eye", ActorRole::Patron, 150, 69, hourOfDay(12), hourOfDay(14),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "fieldcraft", 22, 9, ""},
    {"Tarn Wrenhale", "Two-Loads", ActorRole::Patron, 151, 70, hourOfDay(18), hourOfDay(1),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "fieldcraft", 15, 7, ""},
    {"Sella Brinewall", "the Quiet", ActorRole::Patron, 154, 70, hourOfDay(18), hourOfDay(1),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "streetwise", 12, 12, ""},
    {"Wick Hempson", "Rope-burned", ActorRole::Patron, 150, 70, hourOfDay(19), hourOfDay(2),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "kit_keeping", 24, 6, ""},
    {"Hobbin Mastwright", "Salt-cracked", ActorRole::Patron, 150, 74, hourOfDay(19),
     hourOfDay(2), Activity::Drinking, kPatronPurse, "", JobFamily::Maritime, "seacraft", 26, 9,
     ""},
    {"Edda Pierpont", "the Broad", ActorRole::Patron, 156, 73, hourOfDay(20), hourOfDay(2),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "fishing", 19, 11, ""},
    {"Colm Tarbeck", "the Willing", ActorRole::Patron, 148, 72, hourOfDay(20), hourOfDay(1),
     Activity::Drinking, kPatronPurse, "", JobFamily::Serf, "fieldcraft", 8, 5, ""},
    // S5 puts the ward's LAW in the room, and it closes two things at once.
    //
    // The S4 review's finding: one of the five factions could not be joined in
    // play, because the Watch had no recruiter anywhere a player could stand.
    // Watchman Cull is canon -- notables.json, `cull`, the impound keeper at
    // K02 -- and the impound yard is a short walk from the Gull's door. A
    // watchman having a drink after his shift in the captains' tavern is the
    // least strained way to put the garrison within reach of a conversation.
    //
    // And it makes the mirror VISIBLE. enemyPresence() has counted rivals since
    // S4 with nothing in the room to count; now the Skyrunners' contact and the
    // Watch's recruiter drink in the same taproom between ten and one, and a
    // player who has signed one roll walks into a room with the other in it.
    // He is also the pair of eyes a bale has to get past.
    {"Watchman Cull", "the impound keeper", ActorRole::Patron, 152, 68, hourOfDay(21),
     hourOfDay(1), Activity::Drinking, 30,
     // notables.json: the impound keeper, and the kit-keeping mastery tables
     // have adept lines for a man who inventories seized cargo for a living.
     "cull", JobFamily::Watch, "kit_keeping", 25, 20, "watch"},
    {"Captain Ivo Wake", "of the Kestrel", ActorRole::Patron, 155, 73, hourOfDay(19),
     hourOfDay(1), Activity::Drinking, 45,
     // notables.json: seacraft 35, which the mastery tables have adept lines for.
     // He speaks for the gang: maritime.sailor is a dockhands job in the
     // owner's own factions.json, and a captain is who a docker signs with.
     "wake", JobFamily::Maritime, "seacraft", 35, 16, "dockhands"},
}};

}  // namespace

namespace gull {

std::vector<TilePos> taproomTables(const TileQuery& tiles) {
    std::vector<TilePos> tables;
    // Ascending y then x: the map's own order, so two machines walking this
    // produce the same list and the renderer's candle order is stable.
    for (std::int32_t y = kFootprintY0 + 1; y <= kFootprintY1 - 1; ++y) {
        for (std::int32_t x = kFootprintX0 + 1; x <= kFootprintX1 - 1; ++x) {
            if (!tiles.solid(x, y, kGroundBand)) {
                continue;
            }
            // The bar counter is not a table -- the bartender stands behind it
            // and nobody puts a candle on a working counter.
            if (y == kBarY && x >= kBarX0 && x <= kBarX1) {
                continue;
            }
            // Nor is the hearth, which is masonry and already on fire.
            if (y == kHearthY && x >= kHearthX0 && x <= kHearthX1) {
                continue;
            }
            // Nor the partition the snug stands behind.
            if (x == kSnugX0 - 1) {
                continue;
            }
            tables.push_back(TilePos{x, y});
        }
    }
    return tables;
}

std::vector<TilePos> lanternTiles() {
    return {
        // Over the threshold, one for each door leaf: the first thing a crew
        // off a ship sees.
        TilePos{kDoorX0, kDoorY + 1},
        TilePos{kDoorX1, kDoorY + 1},
        // And over the counter, on the customers' side of it.
        TilePos{(kBarX0 + kBarX1) / 2, kBarY - 1},
    };
}

}  // namespace gull

std::string_view serviceResultName(ServiceResult result) noexcept {
    switch (result) {
        case ServiceResult::Served:
            return "served";
        case ServiceResult::NobodyThere:
            return "nobody there";
        case ServiceResult::Closed:
            return "closed";
        case ServiceResult::NoCoin:
            return "no coin";
        case ServiceResult::OutOfStock:
            return "out of stock";
        case ServiceResult::TooFar:
            return "too far";
        case ServiceResult::Barred:
            return "barred";
        case ServiceResult::Refused:
            return "refused";
    }
    return "?";
}

std::string_view offenceName(Offence offence) noexcept {
    switch (offence) {
        case Offence::Brawled:
            return "brawled";
        case Offence::Stole:
            return "stole";
        case Offence::RefusedToLeave:
            return "refused to leave";
    }
    return "?";
}

std::string_view standingName(Standing standing) noexcept {
    switch (standing) {
        case Standing::Welcome:
            return "welcome";
        case Standing::BeingWarned:
            return "being warned";
        case Standing::Warned:
            return "warned";
        case Standing::BeingEjected:
            return "being ejected";
        case Standing::Barred:
            return "barred";
    }
    return "?";
}

// ---------------------------------------------------------------------------

Tavern::Tavern(const TileQuery& tiles, std::int32_t timeOfDaySeconds, std::uint64_t worldSeed,
               std::filesystem::path contentDir)
    : id_(SystemId::of("tavern.gilded_gull", "GULL")),
      tiles_(&tiles),
      path_(tiles, gull::kRegion),
      dialogue_(DialogueDirector::load(contentDir)),
      rng_(worldSeed, id_.salt()),
      timeOfDay_(((timeOfDaySeconds % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay) {
    buildRoster();
    applySchedules();
    // Everybody whose shift has already started is AT their post, not walking
    // in from the street: a session that opens at eight in the evening opens on
    // a room that has been busy for two hours.
    for (Actor& actor : actors_) {
        if (actor.present()) {
            actor.placeAt(actor.destinationX(), actor.destinationY(), actor.destinationBand());
        }
    }
    applySchedules();
}

void Tavern::buildRoster() {
    actors_.clear();
    actors_.reserve(kStaff.size() + kPatrons.size());
    std::int32_t nextId = 1;  // 0 is the player, always.

    const auto add = [this, &nextId](const RosterEntry& entry) {
        // Everyone starts off shift, out on the quay, and walks in when their
        // hours begin. Spread along the apron so an arriving crowd is a crowd
        // and not a stack.
        const std::int32_t arriveX = gull::kStreetX + (nextId % 5) - 2;
        Actor actor(nextId, entry.name, entry.epithet, entry.role, arriveX, gull::kStreetY,
                    gull::kGroundBand);
        ScheduleBlock block;
        block.fromSecond = entry.fromSecond;
        block.toSecond = entry.toSecond;
        block.postX = entry.postX;
        block.postY = entry.postY;
        block.postBand = gull::kGroundBand;
        block.activity = entry.activity;
        actor.schedule().add(block);
        actor.setCoin(entry.coin);
        actors_.push_back(std::move(actor));
        ++nextId;
    };

    for (const RosterEntry& entry : kStaff) {
        add(entry);
    }
    for (const RosterEntry& entry : kPatrons) {
        add(entry);
    }
}

// ---------------------------------------------------------------------------
// the clock
// ---------------------------------------------------------------------------

bool Tavern::isOpen() const noexcept {
    return withinDayWindow(timeOfDay_, gull::kOpensAt, gull::kClosesAt);
}

bool Tavern::fireLit() const noexcept {
    return withinDayWindow(timeOfDay_, gull::kFireLitFrom, gull::kFireLitUntil);
}

std::vector<gull::HouseLight> Tavern::houseLights() const {
    std::vector<gull::HouseLight> lights;
    if (fireLit()) {
        // GUARDED, and the guard is the point: `for (x = X0; x <= X1; ++x)` with
        // X0 past X1 is an EMPTY loop, so an inverted hearth range would put the
        // fire out with nothing red anywhere. The S2 review found exactly that.
        if (gull::kHearthX0 <= gull::kHearthX1) {
            for (std::int32_t x = gull::kHearthX0; x <= gull::kHearthX1; ++x) {
                lights.push_back(
                    gull::HouseLight{x, gull::kHearthY, gull::kGroundBand, gull::LightKind::Hearth});
            }
        }
    }
    if (isOpen()) {
        for (const gull::TilePos& table : gull::taproomTables(*tiles_)) {
            lights.push_back(gull::HouseLight{table.x, table.y, gull::kGroundBand,
                                              gull::LightKind::Candle});
        }
        for (const gull::TilePos& hook : gull::lanternTiles()) {
            lights.push_back(
                gull::HouseLight{hook.x, hook.y, gull::kGroundBand, gull::LightKind::Lantern});
        }
    }
    return lights;
}

void Tavern::setTimeOfDay(std::int32_t secondOfDay) noexcept {
    timeOfDay_ = ((secondOfDay % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
}

// VERIFICATION GAP (S2): skipTo FAKES the hours it skips. Nothing that would
// have happened in them happens -- no drinks poured, no wages spent, no patron
// walking home. The cellar is restocked and everybody is teleported to wherever
// their rota says they should be. A night asleep is therefore not the same
// world as a night watched, and no test asserts it should be.
//
// S3 note: what it deliberately does NOT touch is the social ledger. Sleeping
// a night does not make anybody forget you robbed them.
void Tavern::skipTo(std::int32_t secondOfDay) {
    // How long the jump actually was, forward round the clock face. Heat cools
    // for every one of those seconds: a night asleep IS a night the ward had to
    // forget in, and that is the one thing skipTo has always been allowed to
    // fake honestly.
    const std::int32_t wrapped =
        ((secondOfDay % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    const std::int32_t forward =
        wrapped >= timeOfDay_ ? wrapped - timeOfDay_ : kSecondsPerDay - timeOfDay_ + wrapped;
    elapsed_ += forward;
    dialogue_.crimes().cool(elapsed_);
    setTimeOfDay(secondOfDay);
    // A night has gone by in one step, so the cellar has been restocked and the
    // room re-seated. Nothing in between is simulated and this is the one place
    // that is true -- see the header on what the tavern does not model.
    drinkStock_ = kOpeningStock;
    balesInSnug_ = kBalesPerNight;
    applySchedules();
    for (Actor& actor : actors_) {
        if (actor.present()) {
            actor.placeAt(actor.destinationX(), actor.destinationY(), actor.destinationBand());
        } else {
            actor.placeAt(gull::kStreetX + (actor.id() % 5) - 2, gull::kStreetY,
                          gull::kGroundBand);
        }
    }
    applySchedules();
}

// ---------------------------------------------------------------------------
// the player, as this room sees them
// ---------------------------------------------------------------------------

void Tavern::setPlayer(std::int32_t xQ8, std::int32_t yQ8, std::int32_t band) noexcept {
    playerX_ = xQ8;
    playerY_ = yQ8;
    playerBand_ = band;
    playerKnown_ = true;
}

void Tavern::setPlayerCombat(Weapon weapon, Intent intent) noexcept {
    playerWeapon_ = weapon;
    playerIntent_ = intent;
}

std::int32_t Tavern::takePlayerShoveX() noexcept {
    return std::exchange(shoveX_, 0);
}
std::int32_t Tavern::takePlayerShoveY() noexcept {
    return std::exchange(shoveY_, 0);
}

bool Tavern::playerInside() const noexcept {
    return playerKnown_ && gull::insideFootprint(q8_tile(playerX_), q8_tile(playerY_));
}

// ---------------------------------------------------------------------------
// who is in the room
// ---------------------------------------------------------------------------

const Actor* Tavern::actorById(std::int32_t id) const noexcept {
    for (const Actor& actor : actors_) {
        if (actor.id() == id) {
            return &actor;
        }
    }
    return nullptr;
}

Actor* Tavern::mutableActorById(std::int32_t id) noexcept {
    for (Actor& actor : actors_) {
        if (actor.id() == id) {
            return &actor;
        }
    }
    return nullptr;
}

std::int32_t Tavern::presentCount() const noexcept {
    std::int32_t count = 0;
    for (const Actor& actor : actors_) {
        if (actor.present()) {
            ++count;
        }
    }
    return count;
}

std::int32_t Tavern::patronCount() const noexcept {
    std::int32_t count = 0;
    for (const Actor& actor : actors_) {
        if (actor.present() && actor.role() == ActorRole::Patron) {
            ++count;
        }
    }
    return count;
}

std::int32_t Tavern::noise() const noexcept {
    // Staff make a room busy; patrons make it LOUD. An empty room at dawn is
    // silent and pay night is a wall of it.
    const std::int32_t level = presentCount() * 5 + patronCount() * 7;
    return std::min(100, level);
}

const Actor* Tavern::nearestTo(std::int32_t xQ8, std::int32_t yQ8,
                               std::int32_t reachQ8) const noexcept {
    const Actor* best = nullptr;
    std::int32_t bestDistance = reachQ8 + 1;
    for (const Actor& actor : actors_) {
        if (!actor.present() || actor.activity() == Activity::Downed) {
            continue;
        }
        const std::int32_t distance = actor.distanceTo(xQ8, yQ8);
        // Ties break on the lower id, so "nearest" is a total order and two
        // runs cannot disagree about who answered.
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &actor;
        }
    }
    return best;
}

Actor* Tavern::findRole(ActorRole role, bool presentOnly) noexcept {
    for (Actor& actor : actors_) {
        if (actor.role() == role && (!presentOnly || actor.present())) {
            return &actor;
        }
    }
    return nullptr;
}

const Actor* Tavern::findRole(ActorRole role, bool presentOnly) const noexcept {
    for (const Actor& actor : actors_) {
        if (actor.role() == role && (!presentOnly || actor.present())) {
            return &actor;
        }
    }
    return nullptr;
}

std::int32_t Tavern::speedFor(const Actor& actor) const noexcept {
    switch (actor.activity()) {
        case Activity::Warning:
        case Activity::Ejecting:
        case Activity::Brawling:
            return kActorPurposefulSpeed;
        default:
            return kActorWalkSpeed;
    }
}

// ---------------------------------------------------------------------------
// the two clocks
// ---------------------------------------------------------------------------

void Tavern::stepMovement() {
    for (Actor& actor : actors_) {
        if (!actor.present() || actor.activity() == Activity::Downed) {
            continue;
        }
        actor.step(path_, speedFor(actor));
    }

    // THE RUN LANDS AT THE THRESHOLD, and it is a transition rather than a
    // state: a bale that is out of the house is out, and standing in the street
    // holding one is not a second run. Checked every movement step rather than
    // every second, because a body crosses a doorway in a third of one and a
    // tick would miss it.
    const bool inside = playerInside();
    CrimeLedger& crimes = dialogue_.crimes();
    if (wasInside_ && !inside && crimes.carryingBale()) {
        const bool seen = witnessCount(kPlayerActorId) > 0;
        const std::int32_t pay = crimes.deliverBale();
        playerCoin_ = wrap_add(playerCoin_, pay);
        dialogue_.setPlayerCoin(playerCoin_);
        dialogue_.noteCrime(Crime::Smuggle, seen);
        if (seen) {
            spreadWitness(kPlayerActorId, Deed::Robbed);
        }
    }
    wasInside_ = inside;
}

void Tavern::tick(const TickContext& context) {
    // Rebound rather than replaced: the engine's source and ours derive from
    // the same (seed, salt, tick) chain, so this keeps the two in step and
    // keeps the tavern usable outside an engine.
    rng_.begin_tick(static_cast<std::uint64_t>(context.tick()));
    tick_ = context.tick();
    advanceSecond();
}

void Tavern::advanceSecond() {
    timeOfDay_ = (timeOfDay_ + 1) % kSecondsPerDay;
    ++elapsed_;
    // The ward forgets, slowly. Charged against elapsed_ and not against the
    // clock on the wall, because the clock wraps at midnight and a memory that
    // wrapped with it would hand the Watch a clean sheet every night.
    dialogue_.crimes().cool(elapsed_);
    if (timeOfDay_ == gull::kOpensAt) {
        drinkStock_ = kOpeningStock;
        balesInSnug_ = kBalesPerNight;
        rentedRoom_ = -1;
        stockedOnDay_ = tick_;
    }
    // Somebody put on the floor comes round. A brawl is not a killing, so a
    // downed patron is a patron who gets up in a minute or two with a headache
    // and a quarter of their health -- and then walks back to their stool,
    // because the schedule is still theirs.
    for (Actor& actor : actors_) {
        if (actor.activity() != Activity::Downed) {
            continue;
        }
        actor.setHealth(actor.hp() + 1, actor.hpMax());
        if (actor.hp() * 4 >= actor.hpMax()) {
            actor.setActivity(Activity::Walking);
        }
    }

    applySchedules();
    tickBouncers();
    tickBrawl();
    tickPatrons();
}

void Tavern::applySchedules() {
    for (Actor& actor : actors_) {
        // Trouble outranks the rota. A bouncer mid-ejection does not clock off,
        // and somebody on the floor is not walking anywhere.
        switch (actor.activity()) {
            case Activity::Warning:
            case Activity::Ejecting:
            case Activity::Brawling:
            case Activity::Downed:
                continue;
            default:
                break;
        }

        const ScheduleBlock* block = actor.schedule().at(timeOfDay_);
        if (block != nullptr) {
            if (!actor.present()) {
                // Arriving: appear on the quay outside and walk in the door.
                actor.placeAt(gull::kStreetX + (actor.id() % 5) - 2, gull::kStreetY,
                              gull::kGroundBand);
                actor.setActivity(Activity::Walking);
            }
            actor.setDestination(block->postX, block->postY, block->postBand);
            actor.setActivity(actor.atDestination() ? block->activity : Activity::Walking);
            if (actor.atDestination() && actor.role() == ActorRole::Bartender) {
                // Behind the bar means facing across it.
                actor.setFacing(kFacingNorth);
            }
            continue;
        }

        if (!actor.present()) {
            continue;
        }
        // Off shift: out of the door and away. Away means gone from the room,
        // not standing in the street all night.
        actor.setDestination(gull::kStreetX, gull::kStreetY, gull::kGroundBand);
        if (!gull::insideFootprint(actor.tileX(), actor.tileY()) && actor.atDestination()) {
            actor.setActivity(Activity::Away);
        } else {
            actor.setActivity(Activity::Walking);
        }
    }
}

// ---------------------------------------------------------------------------
// the door policy
// ---------------------------------------------------------------------------

Actor* Tavern::onDutyBouncerNearestPlayer() noexcept {
    Actor* best = nullptr;
    std::int32_t bestDistance = 0;
    for (Actor& actor : actors_) {
        if (actor.role() != ActorRole::Bouncer || !actor.present() ||
            actor.activity() == Activity::Downed) {
            continue;
        }
        const std::int32_t distance = actor.distanceTo(playerX_, playerY_);
        if (best == nullptr || distance < bestDistance) {
            best = &actor;
            bestDistance = distance;
        }
    }
    return best;
}

const Actor* Tavern::respondingBouncer() const noexcept {
    return respondingBouncerId_ < 0 ? nullptr : actorById(respondingBouncerId_);
}

void Tavern::reportOffence(Offence offence) {
    ++offences_;
    switch (standing_) {
        case Standing::Welcome:
            standing_ = Standing::BeingWarned;
            respondingBouncerId_ = -1;
            break;
        case Standing::BeingWarned:
        case Standing::Warned:
            // Told once. There is no third position between "warned" and
            // "out", and refusing to leave is the same answer as swinging
            // again -- which is the whole of the door policy.
            standing_ = Standing::BeingEjected;
            break;
        case Standing::BeingEjected:
        case Standing::Barred:
            break;
    }
    if (offence == Offence::RefusedToLeave && standing_ == Standing::Warned) {
        standing_ = Standing::BeingEjected;
    }
}

void Tavern::tickBouncers() {
    if (standing_ == Standing::Barred) {
        if (barredUntilTick_ >= 0 && tick_ >= barredUntilTick_) {
            standing_ = Standing::Welcome;
            offences_ = 0;
            barredUntilTick_ = -1;
            respondingBouncerId_ = -1;
        }
        return;
    }
    if (standing_ == Standing::Welcome) {
        return;
    }

    Actor* responder = respondingBouncerId_ < 0 ? nullptr : mutableActorById(respondingBouncerId_);
    if (responder == nullptr || !responder->present() ||
        responder->activity() == Activity::Downed) {
        responder = onDutyBouncerNearestPlayer();
        respondingBouncerId_ = responder == nullptr ? -1 : responder->id();
    }
    if (responder == nullptr) {
        // Nobody on the door. The house minds, and can do nothing about it
        // until somebody comes on shift -- which is what a rota is for.
        return;
    }

    switch (standing_) {
        case Standing::BeingWarned: {
            responder->setActivity(Activity::Warning);
            responder->faceToward(playerX_, playerY_);
            if (responder->distanceTo(playerX_, playerY_) > kWarnRadius) {
                responder->setDestination(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
            } else {
                // Close enough to be heard. Stop THERE rather than walking into
                // the man you are talking to.
                responder->setDestination(responder->tileX(), responder->tileY(),
                                          responder->band());
                standing_ = Standing::Warned;
                warnedAtTick_ = tick_;
                ++warningsGiven_;
                lastWarning_ = responder->name() +
                               ": that is your one. Out of this house, or I put you out.";
            }
            break;
        }
        case Standing::Warned: {
            if (!playerInside()) {
                // Took the warning. The house lets it go.
                standing_ = Standing::Welcome;
                responder->setActivity(Activity::Watching);
                respondingBouncerId_ = -1;
                break;
            }
            // Still Warning, not Watching: the schedule skips an actor in a
            // trouble activity, and a bouncer who wandered back to his post
            // during the grace would have to cross the room again.
            responder->setActivity(Activity::Warning);
            responder->setDestination(responder->tileX(), responder->tileY(),
                                      responder->band());
            responder->faceToward(playerX_, playerY_);
            // The rope is the WARD'S, not a constant: a district whose Watch
            // has lost its grip is a district whose houses stop waiting.
            if (tick_ - warnedAtTick_ >= graceSecondsForPlayer()) {
                // Still here. That is the refusal, and it needs no separate
                // report from the client.
                standing_ = Standing::BeingEjected;
            }
            break;
        }
        case Standing::BeingEjected: {
            responder->setActivity(Activity::Ejecting);
            responder->setDestination(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
            responder->faceToward(playerX_, playerY_);
            if (!playerInside()) {
                standing_ = Standing::Barred;
                ++timesEjected_;
                barredUntilTick_ = tick_ + kBarredSeconds;
                responder->setActivity(Activity::Watching);
                respondingBouncerId_ = -1;
                brawlers_.clear();
                break;
            }
            if (responder->distanceTo(playerX_, playerY_) > kMeleeReach) {
                break;
            }
            // Hands on. The shove goes along the route OUT, not along the line
            // from the bouncer, so somebody being put out of a taproom goes
            // through the door rather than into the far wall.
            const PathStep next = path_.firstStepToward(
                PathStep{q8_tile(playerX_), q8_tile(playerY_), playerBand_},
                PathStep{gull::kStreetX, gull::kStreetY, gull::kGroundBand});
            const std::int32_t dxT = next.x - q8_tile(playerX_);
            const std::int32_t dyT = next.y - q8_tile(playerY_);
            if (dxT == 0 && dyT == 0) {
                break;
            }
            const std::int32_t impulse = (dxT != 0 && dyT != 0)
                                             ? diagonalScaled(kEjectionShove)
                                             : kEjectionShove;
            shoveX_ = wrap_add(shoveX_, dxT * impulse);
            shoveY_ = wrap_add(shoveY_, dyT * impulse);
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// the fight
// ---------------------------------------------------------------------------

std::vector<Fighter> Tavern::currentFight() const {
    std::vector<Fighter> fighters;
    Fighter player;
    player.actorId = kPlayerActorId;
    player.weapon = playerWeapon_;
    player.intent = playerIntent_;
    player.hp = playerHp_;
    player.hpMax = playerHpMax_;
    fighters.push_back(player);
    for (const std::int32_t id : brawlers_) {
        if (const Actor* actor = actorById(id); actor != nullptr) {
            fighters.push_back(actor->asFighter());
        }
    }
    return fighters;
}

std::uint64_t Tavern::drawForPlayerAction() noexcept {
    playerActionSeq_ = wrap_add(playerActionSeq_, 1);
    return rng_.draw(static_cast<std::uint64_t>(kPlayerActorId) + 0x9E37U, playerActionSeq_);
}

Tavern::PunchResult Tavern::playerPunchNearest() {
    PunchResult result;
    const Actor* found = nearestTo(playerX_, playerY_, kMeleeReach);
    if (found == nullptr) {
        return result;
    }
    Actor* target = mutableActorById(found->id());
    if (target == nullptr) {
        return result;
    }
    result.swung = true;
    result.targetId = target->id();
    result.targetName = target->name();

    if (std::find(brawlers_.begin(), brawlers_.end(), target->id()) == brawlers_.end()) {
        brawlers_.push_back(target->id());
        std::sort(brawlers_.begin(), brawlers_.end());
    }

    // THE RULE, at the only moment it matters: before the blow lands.
    const std::vector<Fighter> fighters = currentFight();
    result.fight = classifyFight(fighters);
    if (!resolvesInWorld(result.fight)) {
        // Not this room's fight. The world stops resolving it and says so; the
        // client takes it to the dedicated combat screen.
        //
        // The house still minds, and the two things are separate: where the
        // FIGHT is resolved is a rendering-and-rules question, and whether the
        // bouncers come over is a door-policy question. Drawing a blade in the
        // Gilded Gull answers both.
        escalation_ = result.fight;
        // THE CONSUMER escalated() did not have in S2. A drawn blade in a
        // captains' house is not a private matter: the man it is pointed at
        // remembers it above everything else in this list, and every soul who
        // could see it remembers it nearly as hard. Their greetings change, the
        // ward's opinion drops, and the bar stops serving you.
        noteEscalation(target->id());
        reportOffence(Offence::Brawled);
        return result;
    }

    Fighter victim = target->asFighter();
    result.blow = strike(playerWeapon_, victim, drawForPlayerAction());
    target->setHealth(victim.hp, victim.hpMax);
    target->setActivity(result.blow.downed ? Activity::Downed : Activity::Brawling);
    target->faceToward(playerX_, playerY_);
    dialogue_.ledger().record(target->id(), Deed::Struck);
    spreadWitness(target->id(), Deed::Struck);
    // Somebody you just hit is not somebody you are still talking to.
    if (talkingToId_ == target->id()) {
        endConversation();
    }
    reportOffence(Offence::Brawled);
    return result;
}

void Tavern::noteEscalation(std::int32_t targetId) {
    if (escalationSeen_) {
        return;
    }
    escalationSeen_ = true;
    if (targetId >= 0) {
        dialogue_.ledger().record(targetId, Deed::DrewSteel);
    }
    spreadWitness(targetId, Deed::DrewSteel);
    endConversation();
}

void Tavern::tickBrawl() {
    if (brawlers_.empty()) {
        return;
    }
    // Re-classify every second: a fight that was a brawl a moment ago stops
    // being one the instant somebody draws, escalates, or is beaten past the
    // bloodied line.
    const std::vector<Fighter> fighters = currentFight();
    const FightClass fight = classifyFight(fighters);
    if (!resolvesInWorld(fight)) {
        escalation_ = fight;
        noteEscalation(brawlers_.empty() ? -1 : brawlers_.front());
        return;
    }

    std::int32_t drawIndex = 0;
    for (const std::int32_t id : brawlers_) {
        Actor* actor = mutableActorById(id);
        if (actor == nullptr || !actor->present()) {
            continue;
        }
        if (actor->activity() == Activity::Downed) {
            continue;
        }
        if (actor->distanceTo(playerX_, playerY_) > kMeleeReach) {
            // Out of reach: close, rather than swing at air.
            actor->setDestination(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
            actor->setActivity(Activity::Brawling);
            ++drawIndex;
            continue;
        }
        const std::uint64_t roll = rng_.draw(static_cast<std::uint64_t>(id), drawIndex++);
        Fighter playerFighter = fighters.front();
        const Blow blow = strike(actor->weapon(), playerFighter, roll);
        if (blow.landed) {
            playerHp_ = std::max(kPlayerBrawlFloor, playerFighter.hp);
        }
        actor->faceToward(playerX_, playerY_);
    }

    if (playerHp_ <= kPlayerBrawlFloor && !playerFloored_) {
        // Down. A brawl stops there -- see kPlayerBrawlFloor -- and what
        // follows is not more fighting, it is being carried out.
        playerFloored_ = true;
        if (standing_ != Standing::Barred) {
            standing_ = Standing::BeingEjected;
        }
        for (const std::int32_t id : brawlers_) {
            if (Actor* actor = mutableActorById(id);
                actor != nullptr && actor->activity() == Activity::Brawling) {
                actor->setActivity(Activity::Walking);
            }
        }
        brawlers_.clear();
        return;
    }

    // A fight nobody is left standing for is over.
    bool anyoneUp = false;
    for (const std::int32_t id : brawlers_) {
        const Actor* actor = actorById(id);
        if (actor != nullptr && actor->present() && actor->activity() != Activity::Downed) {
            anyoneUp = true;
        }
    }
    if (!anyoneUp || !playerInside()) {
        for (const std::int32_t id : brawlers_) {
            if (Actor* actor = mutableActorById(id);
                actor != nullptr && actor->activity() == Activity::Brawling) {
                actor->setActivity(Activity::Walking);
            }
        }
        brawlers_.clear();
    }
}

// ---------------------------------------------------------------------------
// trade
// ---------------------------------------------------------------------------

void Tavern::tickPatrons() {
    if (drinkStock_ <= 0) {
        return;
    }
    Actor* bartender = findRole(ActorRole::Bartender, true);
    if (bartender == nullptr) {
        return;
    }
    for (Actor& actor : actors_) {
        if (!actor.present() || actor.activity() != Activity::Drinking) {
            continue;
        }
        // Staggered by id so the whole room does not order on the same second.
        if ((timeOfDay_ + actor.id() * 17) % kPatronDrinkEverySeconds != 0) {
            continue;
        }
        if (drinkStock_ <= 0 || !actor.takeCoin(kDrinkPrice)) {
            continue;
        }
        bartender->giveCoin(kDrinkPrice);
        --drinkStock_;
    }
}

std::int32_t Tavern::drinkPriceForPlayer() const {
    if (negotiatedDrink_ >= 0) {
        return negotiatedDrink_;
    }
    const Actor* bartender = findRole(ActorRole::Bartender, false);
    if (bartender == nullptr) {
        return kDrinkPrice;
    }
    HaggleTerms terms;
    terms.basePrice = kDrinkPrice;
    terms.attitude = dialogue_.ledger().attitudeOf(bartender->id());
    terms.playerSkill = dialogue_.skills().level(kHaggleSkill);
    terms.merchantSkill = kStaff[1].streetwise;
    terms.goods = Goods::Drink;
    // S4: and what the guild behind the counter is worth to this buyer. THIS IS
    // THE TRADE HALF of "factions have real impact on the world" -- a mug of
    // ale costs a different number of coin because of a roll you are on and a
    // ladder you climbed, with no dialogue open and nobody haggling.
    terms.guildPercent = guildPricePercent(dialogue_.standings(), factionOf(*bartender));
    return askingPrice(terms);
}

std::int32_t Tavern::roomPriceForPlayer() const {
    if (negotiatedRoom_ >= 0) {
        return negotiatedRoom_;
    }
    const Actor* innkeeper = findRole(ActorRole::Innkeeper, false);
    if (innkeeper == nullptr) {
        return kRoomPrice;
    }
    HaggleTerms terms;
    terms.basePrice = kRoomPrice;
    terms.attitude = dialogue_.ledger().attitudeOf(innkeeper->id());
    terms.playerSkill = dialogue_.skills().level(kHaggleSkill);
    terms.merchantSkill = kStaff[0].streetwise;
    terms.goods = Goods::Room;
    terms.guildPercent = guildPricePercent(dialogue_.standings(), factionOf(*innkeeper));
    return askingPrice(terms);
}

ServiceResult Tavern::buyDrink() {
    if (standing_ == Standing::Barred) {
        return ServiceResult::Barred;
    }
    if (!isOpen()) {
        return ServiceResult::Closed;
    }
    Actor* bartender = findRole(ActorRole::Bartender, true);
    if (bartender == nullptr) {
        return ServiceResult::NobodyThere;
    }
    if (!playerKnown_ || bartender->distanceTo(playerX_, playerY_) > 2 * kSubOne) {
        return ServiceResult::TooFar;
    }
    // The behaviour change the whole social layer exists to produce. A
    // bartender who has caught you with a hand in his purse does not pour.
    if (dialogue_.ledger().attitudeOf(bartender->id()) == Attitude::Hostile) {
        return ServiceResult::Refused;
    }
    if (drinkStock_ <= 0) {
        return ServiceResult::OutOfStock;
    }
    const std::int32_t price = drinkPriceForPlayer();
    if (playerCoin_ < price) {
        return ServiceResult::NoCoin;
    }
    playerCoin_ -= price;
    bartender->giveCoin(price);
    --drinkStock_;
    ++playerDrinks_;
    negotiatedDrink_ = -1;
    bartender->faceToward(playerX_, playerY_);
    return ServiceResult::Served;
}

ServiceResult Tavern::rentRoom() {
    if (standing_ == Standing::Barred) {
        return ServiceResult::Barred;
    }
    Actor* innkeeper = findRole(ActorRole::Innkeeper, true);
    if (innkeeper == nullptr) {
        return ServiceResult::NobodyThere;
    }
    if (!playerKnown_ || innkeeper->distanceTo(playerX_, playerY_) > 2 * kSubOne) {
        return ServiceResult::TooFar;
    }
    if (dialogue_.ledger().attitudeOf(innkeeper->id()) == Attitude::Hostile) {
        return ServiceResult::Refused;
    }
    if (rentedRoom_ >= 0) {
        return ServiceResult::Served;  // already have one; the man does not charge twice
    }
    const std::int32_t price = roomPriceForPlayer();
    if (playerCoin_ < price) {
        return ServiceResult::NoCoin;
    }
    playerCoin_ -= price;
    innkeeper->giveCoin(price);
    negotiatedRoom_ = -1;
    // The rooms are let in order. Four of them, and the last three are for the
    // captains this house is named for -- so the player gets the first free one.
    rentedRoom_ = 0;
    innkeeper->faceToward(playerX_, playerY_);
    return ServiceResult::Served;
}

ServiceResult Tavern::sleep() {
    if (rentedRoom_ < 0) {
        return ServiceResult::NobodyThere;
    }
    const gull::GuestRoom& room = gull::kRooms[rentedRoom_];
    if (!playerKnown_ || playerBand_ != gull::kUpperBand) {
        return ServiceResult::TooFar;
    }
    const std::int32_t distance =
        std::max(wrap_abs(wrap_sub(q8_tile_centre(room.standX), playerX_)),
                 wrap_abs(wrap_sub(q8_tile_centre(room.standY), playerY_)));
    if (distance > 2 * kSubOne) {
        return ServiceResult::TooFar;
    }
    skipTo(hourOfDay(7));
    return ServiceResult::Served;
}

// ---------------------------------------------------------------------------
// talking
// ---------------------------------------------------------------------------

TalkResult Tavern::talkToNearest() {
    TalkResult result;
    const Actor* actor = nearestTo(playerX_, playerY_, 2 * kSubOne);
    if (actor == nullptr) {
        return result;
    }
    result.speaker = actor->name();
    result.result = ServiceResult::Served;
    switch (actor->role()) {
        case ActorRole::Bartender:
            result.line = drinkStock_ > 0 ? "Ale is two. Wine is more than you have."
                                          : "Barrels are dry until the doors open again.";
            break;
        case ActorRole::Innkeeper:
            result.line = rentedRoom_ >= 0
                              ? "Your room is up the stair. Mind the fourth tread."
                              : "A bed is twelve, and it comes with the door bolted.";
            break;
        case ActorRole::Bouncer:
            result.line = standing_ == Standing::Welcome
                              ? "Keep your hands where the room can see them."
                              : "I have said my piece.";
            break;
        case ActorRole::PriestOfTheFlame:
            result.line = spellbook().loaded()
                              ? "The Flame is taught, not given. Sit, and I will show you one."
                              : "I have nothing to teach tonight.";
            break;
        case ActorRole::SkyrunnerContact:
            // Present, and guarded. S5's questline is the thing that changes
            // this answer; until then the refusal IS the content.
            result.result = ServiceResult::Refused;
            result.line = "You have the wrong table, and I have the wrong face for questions.";
            break;
        case ActorRole::Patron:
            result.line = "Whole ward is drinking on a dead man's tide.";
            break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// conversation
// ---------------------------------------------------------------------------

Speaker Tavern::speakerFor(const Actor& actor) const {
    Speaker speaker;
    speaker.actorId = actor.id();
    speaker.name = actor.name();
    speaker.epithet = actor.epithet();
    speaker.purse = actor.coin();
    // Ids are assigned 1..N in roster order and actors_ is never reordered, so
    // the roster row IS the actor. Guarded anyway: an id that fell outside the
    // table would otherwise read off the end of it.
    const std::size_t index = static_cast<std::size_t>(actor.id() - 1);
    const RosterEntry* entry = nullptr;
    if (index < kStaff.size()) {
        entry = &kStaff[index];
    } else if (index - kStaff.size() < kPatrons.size()) {
        entry = &kPatrons[index - kStaff.size()];
    }
    if (entry != nullptr) {
        speaker.notableId = entry->notableId;
        speaker.family = entry->family;
        speaker.skillId = entry->skillId;
        speaker.skillLevel = entry->skillLevel;
        speaker.haggleSkill = entry->streetwise;
        speaker.awareness = entry->streetwise;
        speaker.recruitsFor = entry->recruits;
    }
    if (const Faction* faction = dialogue_.factions().at(factionOf(actor)); faction != nullptr) {
        speaker.factionId = faction->id;
    }
    // The one man in the room who teaches, and the one whose ladder can open a
    // workbench. Which craftings and which rung are the raws' business, not
    // his role's -- see DialogueDirector::choose.
    speaker.teaches = actor.role() == ActorRole::PriestOfTheFlame;
    // S5. The one body in the room who buys what is not yours to sell. Being a
    // Skyrunner is not the test and is not enough: a cutpurse is not a fence,
    // and the ROLE is what says which of the two is in front of you.
    speaker.buysStolen = actor.role() == ActorRole::SkyrunnerContact;
    // And who can be leaned on. Not the two men paid to throw people out, not
    // the law, and not your own fence -- leaning on the fence is how you stop
    // having one.
    speaker.leanable = actor.role() != ActorRole::Bouncer &&
                       actor.role() != ActorRole::SkyrunnerContact &&
                       speaker.family != JobFamily::Watch;
    switch (actor.role()) {
        case ActorRole::Bartender:
            speaker.trades = true;
            speaker.goods = Goods::Drink;
            speaker.basePrice = kDrinkPrice;
            break;
        case ActorRole::Innkeeper:
            speaker.trades = true;
            speaker.goods = Goods::Room;
            speaker.basePrice = kRoomPrice;
            break;
        default:
            break;
    }
    // A mood override outranks the greeting: somebody on the floor of a taproom
    // has something else to say, and the raws already wrote it.
    if (actor.activity() == Activity::Downed) {
        speaker.moodKey = "mood.downed";
    } else if (standing_ == Standing::Warned || standing_ == Standing::BeingEjected) {
        // Being walked to the door is a state the whole room can see.
        if (actor.role() == ActorRole::Bouncer) {
            speaker.moodKey = "mood.harried";
        }
    }
    return speaker;
}

bool Tavern::talkTo() {
    const Actor* actor = nearestTo(playerX_, playerY_, 2 * kSubOne);
    if (actor == nullptr) {
        dialogue_.close();
        talkingToId_ = -1;
        return false;
    }
    dialogue_.setPlayerCoin(playerCoin_);
    if (!dialogue_.open(speakerFor(*actor), timeOfDay_)) {
        talkingToId_ = -1;
        return false;
    }
    talkingToId_ = actor->id();
    if (Actor* turning = mutableActorById(actor->id()); turning != nullptr) {
        // They look at you while you talk to them. Cheap, and it is the whole
        // difference between a person and a prop.
        turning->faceToward(playerX_, playerY_);
    }
    return true;
}

void Tavern::spreadWitness(std::int32_t victimId, Deed deed) {
    // THREE CONDITIONS, and S3 only had one of them.
    //
    // The S3 review found that widening kWitnessRangeTiles from 8 to a hundred
    // thousand kept the whole gate green, and that the filter was 2D: an actor
    // asleep in a guest room directly above the taproom "saw" a robbery through
    // the floor, and an actor in the snug behind the partition wall saw one
    // through masonry. So the rule is now stated in full and every clause of it
    // has a case that goes red when it is removed:
    //
    //   1. WITHIN RANGE   eight tiles, and a test stands somebody just outside
    //                     it and requires that they remember nothing.
    //   2. SAME FLOOR     a band comparison. A body on the guest floor is not
    //                     in the taproom, whatever its (x, y) says.
    //   3. LINE OF SIGHT  asked of the tiles, through the one function that
    //                     answers "does this block a ray".
    if (!playerKnown_) {
        return;
    }
    const std::int32_t range = kWitnessRangeTiles * kSubOne;
    const std::int32_t playerTileX = q8_tile(playerX_);
    const std::int32_t playerTileY = q8_tile(playerY_);
    for (const Actor& actor : actors_) {
        if (!actor.present() || actor.id() == victimId) {
            continue;
        }
        if (actor.band() != playerBand_) {
            continue;
        }
        const std::int32_t distance = actor.distanceTo(playerX_, playerY_);
        if (distance > range) {
            continue;
        }
        // Arm's reach needs no sight line -- see kWitnessReachTiles on why the
        // bar counter is the reason that clause exists.
        if (distance > kWitnessReachTiles * kSubOne && tiles_ != nullptr &&
            !tiles_->lineOfSight(actor.tileX(), actor.tileY(), playerTileX, playerTileY,
                                 playerBand_)) {
            continue;
        }
        dialogue_.ledger().witness(actor.id(), deed);
    }
}

// ---------------------------------------------------------------------------
// the guilds
// ---------------------------------------------------------------------------

std::int32_t Tavern::factionOf(const Actor& actor) const noexcept {
    // The Skyrunner contact is asked by ROLE and everybody else by job family,
    // and that is not so much a special case as the only place the two differ:
    // Wisp presents as a wastrel (wastrel.streetlife is deliberately
    // unaffiliated in the owner's raws) and IS villain.skyrunner. Presented
    // identity versus true identity, which this project has a ruling about.
    if (actor.role() == ActorRole::SkyrunnerContact) {
        return dialogue_.factions().factionForJobPrefix("villain");
    }
    const std::size_t index = static_cast<std::size_t>(actor.id() - 1);
    const RosterEntry* entry = nullptr;
    if (index < kStaff.size()) {
        entry = &kStaff[index];
    } else if (index - kStaff.size() < kPatrons.size()) {
        entry = &kPatrons[index - kStaff.size()];
    }
    if (entry == nullptr) {
        return -1;
    }
    return dialogue_.factions().factionForJobPrefix(jobFamilyKey(entry->family));
}

std::int32_t Tavern::enemyPresence() const noexcept {
    const FactionLedger& standings = dialogue_.standings();
    std::int32_t count = 0;
    for (const Actor& actor : actors_) {
        if (!actor.present()) {
            continue;
        }
        const std::int32_t theirs = factionOf(actor);
        if (theirs < 0) {
            continue;
        }
        for (const std::int32_t rival : dialogue_.factions().rivalsOf(theirs)) {
            if (standings.rank(rival) > 0) {
                ++count;
                break;
            }
        }
    }
    return count;
}

void Tavern::applyRivalHostility() {
    FactionLedger& standings = dialogue_.standings();
    for (const Actor& actor : actors_) {
        if (!actor.present()) {
            continue;
        }
        const std::int32_t theirs = factionOf(actor);
        if (theirs < 0) {
            continue;
        }
        for (const std::int32_t rival : dialogue_.factions().rivalsOf(theirs)) {
            if (standings.rank(rival) <= 0) {
                continue;
            }
            // seed(), not record(): this is the standing an authored
            // relationship implies, which is exactly what that call exists for.
            // Nobody did anything to anybody -- you put on the other colours.
            dialogue_.ledger().seed(actor.id(), kHostileAtOrBelow);
            break;
        }
    }
}

std::int32_t Tavern::graceSecondsForPlayer() const noexcept {
    const FactionLedger& standings = dialogue_.standings();
    const std::int32_t watch = dialogue_.factions().indexOf("watch");
    const std::int32_t roofs = dialogue_.factions().indexOf("skyrunners");
    if (watch < 0 || roofs < 0) {
        return kGraceSeconds;
    }
    // Divided by five so the whole 0..100 influence scale is worth twenty
    // seconds of rope either way, and clamped so a house never gives none and
    // never gives all night.
    std::int32_t swing = (standings.influence(watch) - standings.influence(roofs)) / 5;
    // S5. `grace` is the Watch's second rung, and until now it was a token with
    // no reader -- the S4 review found it, and found the trap in it too: the
    // swing above reads INFLUENCE, the ward's balance of power, which has
    // nothing to do with the token that happens to share the word. Now the
    // token means what it says. A house gives a watchman longer to finish his
    // drink, because the house would rather not explain itself later.
    if (standings.unlocked(watch, "grace")) {
        swing += 8;
    }
    // And it gives a wanted man none of it: heat is what the ward has HEARD,
    // and a bouncer hears everything.
    if (dialogue_.crimes().warrant()) {
        swing -= 10;
    }
    return std::clamp(kGraceSeconds + swing, kGraceSecondsFloor, kGraceSecondsCeiling);
}

void Tavern::applyReply(Reply& reply) {
    if (!reply.ok) {
        return;
    }
    switch (reply.kind) {
        case TopicKind::Buy: {
            // The intent came from the dialogue layer; the counter resolves it,
            // through exactly the same buyDrink()/rentRoom() that the S2 verbs
            // used. One purchase path, not two.
            const bool drink = dialogue_.speaker().goods == Goods::Drink;
            const std::int32_t before = playerCoin_;
            const ServiceResult served = drink ? buyDrink() : rentRoom();
            if (served == ServiceResult::Served) {
                reply.line = "PAID " + std::to_string(before - playerCoin_) + "C.";
            } else {
                reply.line = std::string(serviceResultName(served));
                reply.ok = false;
            }
            dialogue_.setPlayerCoin(playerCoin_);
            reply.coinDelta = 0;
            break;
        }
        case TopicKind::Trade:
            if (reply.coinDelta < 0) {
                // A struck price is an AGREEMENT, not a payment: no coin moves
                // here. It is remembered, and the purchase that follows across
                // the counter -- buyDrink(), rentRoom() -- charges it.
                if (dialogue_.speaker().goods == Goods::Drink) {
                    negotiatedDrink_ = -reply.coinDelta;
                } else {
                    negotiatedRoom_ = -reply.coinDelta;
                }
                reply.coinDelta = 0;
            }
            break;
        case TopicKind::BuyDrinkFor:
            if (reply.coinDelta < 0) {
                // The drink comes off the bar like any other. Dry barrels do not
                // undo the gesture: you offered, and the room saw you offer.
                if (drinkStock_ > 0) {
                    --drinkStock_;
                    if (Actor* bartender = findRole(ActorRole::Bartender, true);
                        bartender != nullptr) {
                        bartender->giveCoin(-reply.coinDelta);
                    }
                }
                spreadWitness(talkingToId_, Deed::BoughtDrink);
            }
            break;
        case TopicKind::PickPocket:
            if (reply.offence) {
                // Caught. Everybody who could see it remembers, the ward hears,
                // and the house sends somebody over.
                spreadWitness(talkingToId_, Deed::Robbed);
                reportOffence(Offence::Stole);
            } else if (reply.coinDelta > 0) {
                // The coin came off a real purse and the world says so.
                if (Actor* victim = mutableActorById(talkingToId_); victim != nullptr) {
                    // Discarded on purpose: the lift was already capped at what
                    // is in the purse, and a refusal here would mean the purse
                    // moved between the two, which nothing can do.
                    (void)victim->takeCoin(reply.coinDelta);
                }
            }
            break;
        default:
            break;
    }
    if (reply.coinDelta != 0) {
        playerCoin_ = std::max(0, wrap_add(playerCoin_, reply.coinDelta));
        dialogue_.setPlayerCoin(playerCoin_);
    }
    if (reply.ranked) {
        // The moment the room learns whose side you are on. Every rival present
        // is seeded hostile -- see applyRivalHostility, and note that it is the
        // ROOM applying it and not the dialogue layer, which still has no idea
        // there is a room.
        applyRivalHostility();
    }
    if (reply.criminal) {
        // WHO SAW IT is the room's business and only the room's: the dialogue
        // layer still does not know there is a room. A caught hand is seen by
        // the person whose wrist it is, whatever else is true of the taproom.
        const bool seen = reply.offence || witnessCount(talkingToId_) > 0;
        if (reply.kind == TopicKind::Lean && reply.ok) {
            spreadWitness(talkingToId_, Deed::Robbed);
        }
        dialogue_.noteCrime(reply.crime, seen);
    }
    if (reply.closes) {
        talkingToId_ = -1;
    }
}

std::int32_t Tavern::witnessCount(std::int32_t exceptId) const noexcept {
    if (!playerKnown_) {
        return 0;
    }
    const std::int32_t playerTileX = q8_tile(playerX_);
    const std::int32_t playerTileY = q8_tile(playerY_);
    std::int32_t seen = 0;
    for (const Actor& actor : actors_) {
        if (!actor.present() || actor.id() == exceptId ||
            actor.activity() == Activity::Downed) {
            continue;
        }
        if (actor.band() != playerBand_) {
            continue;
        }
        const std::int32_t dx = actor.x() - playerX_;
        const std::int32_t dy = actor.y() - playerY_;
        const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (distance > kWitnessRangeTiles * kSubOne) {
            continue;
        }
        if (distance > kWitnessReachTiles * kSubOne && tiles_ != nullptr &&
            !tiles_->lineOfSight(actor.tileX(), actor.tileY(), playerTileX, playerTileY,
                                 playerBand_)) {
            continue;
        }
        ++seen;
    }
    return seen;
}

void Tavern::injurePlayer(std::int32_t amount) {
    if (amount <= 0) {
        return;
    }
    playerHp_ = std::max(kPlayerBrawlFloor, playerHp_ - amount);
    if (playerHp_ <= kPlayerBrawlFloor) {
        playerFloored_ = true;
    }
}

Tavern::StealResult Tavern::crackStrongbox() {
    StealResult out;
    if (!playerKnown_ || playerBand_ != gull::kUpperBand) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO OPEN.";
        return out;
    }
    const std::int32_t room = gull::roomAtStand(q8_tile(playerX_), q8_tile(playerY_));
    if (room < 0) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO OPEN.";
        return out;
    }
    if (room == rentedRoom_) {
        // Your own box. Not a crime, and the room says so rather than letting a
        // player farm the tally by renting a bed and robbing themselves.
        out.result = ServiceResult::Refused;
        out.line = "THAT ONE IS YOURS.";
        return out;
    }
    const std::int32_t bit = 1 << room;
    if ((crackedBoxes_ & bit) != 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "ALREADY EMPTY.";
        return out;
    }
    crackedBoxes_ |= bit;
    const std::int32_t craft = dialogue_.skills().level(kThieverySkill);
    out.coin = kStrongboxCoin + craft / 4;
    out.loot = 1 + craft / 20;
    out.seen = witnessCount(kPlayerActorId) > 0;
    playerCoin_ = wrap_add(playerCoin_, out.coin);
    dialogue_.setPlayerCoin(playerCoin_);
    dialogue_.crimes().takeLoot(out.loot);
    dialogue_.noteCrime(Crime::Burgle, out.seen);
    if (out.seen) {
        spreadWitness(kPlayerActorId, Deed::Robbed);
        reportOffence(Offence::Stole);
    }
    out.result = ServiceResult::Served;
    out.line = "CRACKED IT - " + std::to_string(out.coin) + "C AND " +
               std::to_string(out.loot) + " PIECE" + (out.seen ? ", AND SEEN." : ".");
    return out;
}

Tavern::StealResult Tavern::handleBale() {
    StealResult out;
    CrimeLedger& crimes = dialogue_.crimes();
    if (crimes.carryingBale()) {
        crimes.dropBale();
        ++balesInSnug_;
        out.result = ServiceResult::Served;
        out.line = "PUT IT DOWN.";
        return out;
    }
    if (!playerKnown_ || playerBand_ != gull::kGroundBand) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO CARRY.";
        return out;
    }
    const std::int32_t dx = q8_tile_centre(gull::kBaleX) - playerX_;
    const std::int32_t dy = q8_tile_centre(gull::kBaleY) - playerY_;
    if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > kReachQ8) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO CARRY.";
        return out;
    }
    const std::int32_t roofs = dialogue_.factions().indexOf("skyrunners");
    if (!dialogue_.standings().isMember(roofs)) {
        // Nobody hands a stranger a bale.
        out.result = ServiceResult::Refused;
        out.line = "IT IS NOT YOURS TO PICK UP.";
        return out;
    }
    if (balesInSnug_ <= 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "THE SNUG IS EMPTY TONIGHT.";
        return out;
    }
    --balesInSnug_;
    // VERIFICATION GAP (S5): a bale is a BOOLEAN, not an item. There is no
    // inventory in this build, so what is being carried has no weight, no
    // contents, no owner and cannot be dropped anywhere but where it was picked
    // up. Everything downstream of it -- the run, the pay, the tally, the heat
    // -- is real; the object is a flag with a name.
    crimes.takeBale();
    out.result = ServiceResult::Served;
    out.line = "THE BALE IS HEAVIER THAN IT LOOKS.";
    return out;
}

Reply Tavern::chooseTopic(std::size_t index) {
    dialogue_.setPlayerCoin(playerCoin_);
    Reply reply = dialogue_.choose(index);
    applyReply(reply);
    return reply;
}

Reply Tavern::offerPrice(std::int32_t coins) {
    dialogue_.setPlayerCoin(playerCoin_);
    Reply reply = dialogue_.offerPrice(coins);
    applyReply(reply);
    return reply;
}

Reply Tavern::takeAskingPrice() {
    dialogue_.setPlayerCoin(playerCoin_);
    Reply reply = dialogue_.takeAsking();
    applyReply(reply);
    return reply;
}

Reply Tavern::commitForge() {
    dialogue_.setPlayerCoin(playerCoin_);
    Reply reply = dialogue_.commitForge();
    applyReply(reply);
    return reply;
}

Reply Tavern::endForge() {
    Reply reply = dialogue_.endForge();
    applyReply(reply);
    return reply;
}

void Tavern::endConversation() {
    dialogue_.close();
    talkingToId_ = -1;
}

std::vector<const Spell*> Tavern::priestTeaches(std::int32_t linkcraftLevel) const {
    const Actor* priest = findRole(ActorRole::PriestOfTheFlame, true);
    if (priest == nullptr) {
        return {};
    }
    // LINKCRAFT, not channeling, and the difference is the owner's not mine.
    // notables.json gives Father Maell channeling 40 -- what HE does. All
    // eleven spells in content/raws/spells/spells.json are cast with linkcraft,
    // the Simple Magic seed skill, so linkcraft is what he has to hand over.
    // Asking the raws which skill they need, rather than assuming the teacher's,
    // is the difference between teaching from canon and teaching from a guess.
    return spellbook().teachableAt(kCraftingSkill, linkcraftLevel);
}

// ---------------------------------------------------------------------------
// hashing
// ---------------------------------------------------------------------------

void Tavern::hash_into(HashSink& sink) const {
    // Dense index order, which is id order: actors_ is never reordered.
    sink.put_int(static_cast<std::uint32_t>(actors_.size()));
    for (const Actor& actor : actors_) {
        actor.hashInto(sink);
    }
    sink.put_int(static_cast<std::uint32_t>(timeOfDay_));
    sink.put_int(static_cast<std::uint32_t>(playerX_));
    sink.put_int(static_cast<std::uint32_t>(playerY_));
    sink.put_int(static_cast<std::uint32_t>(playerBand_));
    sink.put_byte(playerKnown_ ? 1U : 0U);
    sink.put_byte(static_cast<std::uint32_t>(playerWeapon_));
    sink.put_byte(static_cast<std::uint32_t>(playerIntent_));
    sink.put_int(static_cast<std::uint32_t>(playerHp_));
    sink.put_byte(playerFloored_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(playerCoin_));
    sink.put_int(static_cast<std::uint32_t>(playerDrinks_));
    sink.put_int(static_cast<std::uint32_t>(playerActionSeq_));
    sink.put_int(static_cast<std::uint32_t>(shoveX_));
    sink.put_int(static_cast<std::uint32_t>(shoveY_));
    sink.put_int(static_cast<std::uint32_t>(drinkStock_));
    sink.put_int(static_cast<std::uint32_t>(rentedRoom_));
    sink.put_byte(static_cast<std::uint32_t>(standing_));
    sink.put_int(static_cast<std::uint32_t>(offences_));
    sink.put_int(static_cast<std::uint32_t>(warningsGiven_));
    sink.put_int(static_cast<std::uint32_t>(timesEjected_));
    sink.put_long(static_cast<std::uint64_t>(warnedAtTick_));
    sink.put_long(static_cast<std::uint64_t>(barredUntilTick_));
    sink.put_int(static_cast<std::uint32_t>(respondingBouncerId_));
    sink.put_byte(static_cast<std::uint32_t>(escalation_));
    sink.put_byte(escalationSeen_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(brawlers_.size()));
    for (const std::int32_t id : brawlers_) {
        sink.put_int(static_cast<std::uint32_t>(id));
    }
    // S3: everything anybody remembers about the player, the skills they have
    // earned, and whatever price is on the table. All of it is simulation
    // state, so all of it is in the hash -- a relationship the twin-run gate
    // could not see would be a relationship the gate does not protect.
    sink.put_int(static_cast<std::uint32_t>(negotiatedDrink_));
    sink.put_int(static_cast<std::uint32_t>(negotiatedRoom_));
    sink.put_int(static_cast<std::uint32_t>(talkingToId_));
    // S5: which boxes have been emptied, whether the body was inside the walls
    // on the last step, and how long this room has been running.
    sink.put_int(static_cast<std::uint32_t>(crackedBoxes_));
    sink.put_int(static_cast<std::uint32_t>(balesInSnug_));
    sink.put_byte(wasInside_ ? 1U : 0U);
    sink.put_long(static_cast<std::uint64_t>(elapsed_));
    dialogue_.hashInto(sink);
}

}  // namespace granadad::sim
