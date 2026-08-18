#include "granadad/sim/tavern.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

// TASK #81. Casebook{} default-constructs the unbound placeholder
// earnedLegend() hands to legendOf() for the row it never reads. See that
// method's own comment on tavern.hpp.
#include "granadad/sim/casebook.hpp"
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

/// ASCII upper case, for the 4x6 font and nothing else. The same one-line rule
/// the dialogue layer applies to an authored name; no programmer is renaming
/// anybody here.
[[nodiscard]] std::string upperCase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
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

/// TONIGHT'S RATS. Not people: no name pool was raided for them, they are not
/// among the Forty, and the roster entry they get is the smallest one that
/// still puts a body on a tile at an hour. They keep to the three corners of
/// the taproom the bar cannot see into and the fourth is under the stair.
///
/// DOCKS-GAZETTEER section 3 gives the ward rat-catchers at Kennel Row and a
/// dog yard to keep them in; the Watch pays by the scalp for what the terriers
/// cannot reach. This is where a player gets the one contraband on the list
/// that is not a crime to hold.
struct VerminPost {
    std::int32_t x;
    std::int32_t y;
};
constexpr std::array<VerminPost, 4> kVerminPosts = {{
    {gull::kFootprintX0 + 1, gull::kFootprintY1 - 1},
    {gull::kFootprintX1 - 1, gull::kFootprintY0 + 2},
    {gull::kHearthX0, gull::kHearthY - 1},
    {gull::kSnugX0, gull::kFootprintY1 - 2},
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

std::string_view counterRefusal(ServiceResult result, Goods goods) noexcept {
    const bool drink = goods == Goods::Drink;
    switch (result) {
        case ServiceResult::Served:
            return "";
        case ServiceResult::NobodyThere:
            return drink ? "THERE IS NOBODY BEHIND THE BAR."
                         : "THERE IS NOBODY AT THE STAIR TO ASK.";
        case ServiceResult::Closed:
            return "THE DOORS ARE SHUT.";
        case ServiceResult::NoCoin:
            return "YOUR PURSE WILL NOT COVER IT.";
        case ServiceResult::OutOfStock:
            // Gerta's own words for it, and the innkeeper's. Compare the
            // authored greetings in Tavern::talkToNearest.
            return drink ? "THE BARRELS ARE DRY UNTIL THE DOORS OPEN AGAIN."
                         : "EVERY BED IN THE HOUSE IS LET.";
        case ServiceResult::TooFar:
            return "TOO FAR OFF TO BE HEARD.";
        case ServiceResult::Barred:
            return "YOU HAVE BEEN PUT OUT OF THIS HOUSE.";
        case ServiceResult::Refused:
            return "NOT FOR YOU, NOT TONIGHT.";
    }
    return "";
}

std::string_view restRefusal(ServiceResult result) noexcept {
    switch (result) {
        case ServiceResult::Served:
            return "";
        case ServiceResult::NobodyThere:
            // Nothing to do with anybody being absent: sleep() answers this
            // when no room has been taken. The player is standing in a house
            // where none of the beds is theirs.
            return "NO BED HERE IS YOURS.";
        case ServiceResult::TooFar:
            return "NOT AT YOUR OWN BED-FOOT.";
        case ServiceResult::Closed:
            return "THE DOORS ARE SHUT.";
        case ServiceResult::NoCoin:
            return "YOUR PURSE WILL NOT COVER IT.";
        case ServiceResult::OutOfStock:
            return "EVERY BED IN THE HOUSE IS LET.";
        case ServiceResult::Barred:
            return "YOU HAVE BEEN PUT OUT OF THIS HOUSE.";
        case ServiceResult::Refused:
            return "NOT FOR YOU, NOT TONIGHT.";
    }
    return "";
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
      worldSeed_(worldSeed),
      timeOfDay_(((timeOfDaySeconds % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay) {
    startedAt_ = timeOfDay_;
    // The nemesis book reads the same registry the director does -- SHARED, not
    // a second load of the owner's file, so the two can never disagree about
    // which faction is index 2.
    nemesis_ = NemesisBook::load(contentDir, dialogue_.factionsShared());
    buildRoster();
    // Tonight's boat, and tonight's work. Both are posted before anybody has
    // taken a step, so a session that opens at ten at night opens on a board
    // that has been up since the doors did.
    // THE BOARD FIRST, THEN THE BOAT. The order is load-bearing since S7:
    // drawBaleGoods reads tonight's offers to decide what a hull landed, so a
    // boat drawn before the board was posted would be a boat that had nothing
    // to read.
    dialogue_.postContracts(dayNumber(), rng_.world_seed());
    drawBaleGoods();
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

    // And tonight's rats, appended AFTER every person, so an actor id is still
    // a stable index into a roster that has only ever grown at the end.
    for (const VerminPost& post : kVerminPosts) {
        Actor rat(nextId, "Rat", "on the skirting", ActorRole::Vermin, post.x, post.y,
                  gull::kGroundBand);
        ScheduleBlock block;
        block.fromSecond = kVerminFrom;
        block.toSecond = kVerminUntil;
        block.postX = post.x;
        block.postY = post.y;
        block.postBand = gull::kGroundBand;
        block.activity = Activity::Working;
        rat.schedule().add(block);
        rat.setHealth(kVerminHealth, kVerminHealth);
        actors_.push_back(std::move(rat));
        ++nextId;
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
    // HELD-EFFECTS BUILD: a hold is on the absolute clock, so the night this
    // jump skipped ran it out -- swept here, not paused, for the same honesty
    // heat cooling above claims. A quarter-hour tuning does not survive a
    // night in a rented bed.
    sweepHeldEffects();
}

// ---------------------------------------------------------------------------
// the player, as this room sees them
// ---------------------------------------------------------------------------

void Tavern::setPlayer(std::int32_t xQ8, std::int32_t yQ8, std::int32_t band) noexcept {
    if (!playerKnown_) {
        // The band the player STARTED on is not an arrival anywhere. Seeded on
        // the first sighting so a session that spawns on the lead does not
        // credit itself with a roof-run for standing still -- see
        // settleLanding, which is the only thing that raises this afterwards.
        highestBand_ = band;
    }
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
        // PEOPLE. A rat in the corner is not one of the fourteen, does not make
        // the room busier and is not somebody the frame is counting.
        if (actor.present() && actor.role() != ActorRole::Vermin) {
            ++count;
        }
    }
    return count;
}

std::int32_t Tavern::verminPresent() const noexcept {
    std::int32_t count = 0;
    for (const Actor& actor : actors_) {
        if (actor.present() && actor.role() == ActorRole::Vermin) {
            ++count;
        }
    }
    return count;
}

std::int32_t Tavern::verminFirstId() const noexcept {
    for (const Actor& actor : actors_) {
        if (actor.role() == ActorRole::Vermin) {
            return actor.id();
        }
    }
    return 0;
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
        // NEAREST PERSON. Everything that asks this -- talking, greeting, the
        // conversation surface -- means somebody, and a rat has nothing to say.
        // What DOES want a rat asks for one by name; see downedVerminInReach
        // and playerPunchNearest.
        if (actor.role() == ActorRole::Vermin) {
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
        // WHOSE BALE IS IT. With no job open for what is in it, the boat's own
        // buyer is waiting at the corner and the flat runner's fee is the pay
        // -- which is exactly what S5 did and what its cases still assert. With
        // a job open, the sack comes off your shoulder into your own and the
        // contract is what pays, because being paid twice for one bale would
        // make the snug a faucet with extra steps.
        bool ownBuyer = true;
        for (const Contract& row : dialogue_.contracts().contracts()) {
            if (row.live() && row.good == crimes.baleGood()) {
                ownBuyer = false;
                break;
            }
        }
        const std::int32_t pay = crimes.deliverBale(ownBuyer);
        playerCoin_ = wrap_add(playerCoin_, pay);
        dialogue_.setPlayerCoin(playerCoin_);
        dialogue_.noteCrime(Crime::Smuggle, seen);
        if (seen) {
            spreadWitness(kPlayerActorId, Deed::Robbed);
        }
    }
    wasInside_ = inside;
    // S9. One movement step of forgetting: the noise a probe or a snapped pick
    // made fades out over kNoiseFadeSteps. Done last so the delivery above is
    // judged against the noise that was in the air when it happened.
    stealth_.step();
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
    // A new day is new work. refresh() answers instantly on a day it has
    // already posted, so this is a comparison and not a rebuild.
    //
    // BEFORE THE RESTOCK, not after. The boat reads the board -- see
    // drawBaleGoods -- so the board has to be tonight's before the hull is.
    dialogue_.postContracts(dayNumber(), rng_.world_seed());
    // RADIANT BUILD: and new errands, when the room knows the district's own
    // people. The same comparison-not-rebuild contract; on the day the clock
    // turns, the board binds its nouns to wherever those bodies are actually
    // standing at that instant, which is the generator's whole design.
    if (wardPeople_ != nullptr) {
        dialogue_.postRadiant(dayNumber(), rng_.world_seed(), *wardPeople_);
    }
    if (timeOfDay_ == gull::kOpensAt) {
        drinkStock_ = kOpeningStock;
        balesInSnug_ = kBalesPerNight;
        drawBaleGoods();
        // A fresh night brings fresh vermin, exactly as it brings fresh
        // barrels. Both are what stop their trade being a faucet.
        scalpedVermin_ = 0;
        rentedRoom_ = -1;
    }
    // Somebody put on the floor comes round. A brawl is not a killing, so a
    // downed patron is a patron who gets up in a minute or two with a headache
    // and a quarter of their health -- and then walks back to their stool,
    // because the schedule is still theirs.
    for (Actor& actor : actors_) {
        if (actor.activity() != Activity::Downed) {
            continue;
        }
        if (actor.role() == ActorRole::Vermin) {
            // A rat on the floor stays on the floor. It is the one body in this
            // room that does not get up with a headache, because it is the one
            // body somebody is going to skin.
            continue;
        }
        actor.setHealth(actor.hp() + 1, actor.hpMax());
        if (actor.hp() * 4 >= actor.hpMax()) {
            actor.setActivity(Activity::Walking);
        }
    }

    applySchedules();
    tickBouncers();
    tickWatch();
    tickBrawl();
    // After the brawl on purpose: a scald's dose lands on the hp the second's
    // blows left behind, so the two cannot disagree about ordering between
    // runs.
    tickSpellwork();
    // HELD-EFFECTS BUILD: the holds run out on the same cadence the trickles
    // deliver on -- one sweep a second, and the sheet re-derives the moment
    // a row lapses.
    sweepHeldEffects();
    tickPatrons();
    tickVermin();
}

std::int32_t Tavern::dayNumber() const noexcept {
    // Monotonic across midnight AND across a night in a cell: elapsed_ counts
    // every simulated second this room has run including the ones a skip
    // jumped, and the wall clock's wrap is not in it. A deadline measured
    // against timeOfDay_ would be a deadline nobody could ever miss.
    return static_cast<std::int32_t>((static_cast<std::int64_t>(startedAt_) + elapsed_) /
                                     kSecondsPerDay);
}

void Tavern::skipHours(std::int32_t hours) {
    const std::int32_t forward = std::max(0, hours);
    // Whole days first, because skipTo can only ever carry the room round one
    // face of the clock and a sentence is measured in nights.
    elapsed_ += static_cast<std::int64_t>(forward / 24) * kSecondsPerDay;
    skipTo((timeOfDay_ + (forward % 24) * 3600) % kSecondsPerDay);
}

void Tavern::drawBaleGoods() noexcept {
    // A boat brings a boat's cargo: powder, spirit or bales. It does not bring
    // rats and it does not bring somebody's christening cup, so the draw is
    // over exactly the three the harbour actually lands.
    //
    // WHAT THE WARD ORDERED COMES FIRST. Tonight's board is already posted by
    // the time the doors open, and every offer on it names a good. The wanted
    // set is those goods, filtered to the three a hull carries; a bale is drawn
    // from that set when it is non-empty, and from all three when it is not.
    // Without this, S6's snug landed six units of one good chosen by a coin
    // that had never read the board -- and most of the board was undeliverable.
    std::array<Contraband, kBoatGoodCount> wanted{};
    std::int32_t wantedCount = 0;
    for (const Contract& row : dialogue_.contracts().contracts()) {
        if (row.state != ContractState::Offered && row.state != ContractState::Taken) {
            continue;
        }
        const std::int32_t index = static_cast<std::int32_t>(row.good);
        if (index < 1 || index > kBoatGoodCount) {
            // Scalps and pieces are not cargo. The ward's own rats supply the
            // one and somebody's strongbox supplies the other.
            continue;
        }
        bool seen = false;
        for (std::int32_t i = 0; i < wantedCount; ++i) {
            seen = seen || wanted[static_cast<std::size_t>(i)] == row.good;
        }
        if (!seen) {
            wanted[static_cast<std::size_t>(wantedCount)] = row.good;
            ++wantedCount;
        }
    }
    for (std::int32_t bale = 0; bale < kBalesPerNight; ++bale) {
        const std::uint64_t roll = rng_.draw(0xBA1EU, playerActionSeq_ + bale);
        if (wantedCount > 0) {
            baleGoods_[static_cast<std::size_t>(bale)] =
                wanted[static_cast<std::size_t>(roll % static_cast<std::uint64_t>(wantedCount))];
        } else {
            baleGoods_[static_cast<std::size_t>(bale)] =
                static_cast<Contraband>(1 + static_cast<std::int32_t>(roll % kBoatGoodCount));
        }
    }
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

        if (actor.role() == ActorRole::Vermin) {
            // A rat that has been skinned does not get up and is not replaced
            // until the boat, the barrels and the vermin are all restocked
            // together at opening. This is what stops the ward's bounty being
            // a coin faucet with whiskers.
            const std::int32_t bit = 1 << (actor.id() - verminFirstId());
            if ((scalpedVermin_ & bit) != 0) {
                actor.setActivity(Activity::Away);
                continue;
            }
        }

        const ScheduleBlock* block = actor.schedule().at(timeOfDay_);
        if (block != nullptr) {
            if (!actor.present()) {
                // Arriving: appear on the quay outside and walk in the door.
                actor.placeAt(gull::kStreetX + (actor.id() % 5) - 2, gull::kStreetY,
                              gull::kGroundBand);
                actor.setActivity(Activity::Walking);
            }
            // S8: A MAN WITH A GRUDGE STOPS KEEPING HIS OWN HOURS. Past
            // kHuntsAtGrudge the rota is still what puts him in the building,
            // and where he stands once he is in it is wherever the player is.
            // He is not attacking -- the brawl rules are unchanged and it still
            // takes a punch to start one -- he is simply there, every time you
            // turn round, which is what a nemesis is for.
            const Nemesis* rival = nemesis_.of(actor.id());
            if (rival != nullptr && rival->hunts() && playerKnown_ && playerInside() &&
                playerBand_ == block->postBand) {
                actor.setDestination(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
                actor.setActivity(actor.atDestination() ? Activity::Watching
                                                        : Activity::Walking);
                actor.faceToward(playerX_, playerY_);
                continue;
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
        if (barredUntilTick_ >= 0 && elapsed_ >= barredUntilTick_) {
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
                // OUT OF THE RAWS, NOT OUT OF THIS FILE. S6 shipped a
                // hardcoded English sentence here -- a spoken line composed by
                // whoever wrote the door policy -- in a project whose stated
                // discipline (contract.hpp) is that not one proper noun in a
                // system's output is chosen by a programmer. It is authored
                // now, in content/raws/barks/house_barks.json, and it rotates
                // on how many warnings this house has given, so a bouncer does
                // not say the same sentence twice in a night.
                lastWarning_ = responder->name() + ": " +
                               std::string(dialogue_.barks().line(
                                   dialogue_.barks().resolve({std::string("house.warning")}),
                                   warningsGiven_));
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
                // CHARGED AGAINST elapsed_ AND NOT tick_, since S8, and it
                // is the same correction S6 made to the Watch's heat for the
                // same reason: tick_ counts ticks that were RUN and elapsed_
                // counts simulated seconds INCLUDING the ones a skipTo jumped.
                // A player who was put out of the door and then slept a night
                // in a rented bed came back to a house that still had not
                // forgotten, because the clock the door policy read had not
                // moved. A night is a night.
                barredUntilTick_ = elapsed_ + kBarredSeconds;
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
    // A RAT IS A TARGET AND A PERSON IS NOT NECESSARILY ONE. nearestTo answers
    // with people, because everything else that asks it means somebody; a fist
    // wants whichever of the two is actually closer.
    const Actor* quarry = nearestVerminTo(playerX_, playerY_, kMeleeReach);
    if (quarry != nullptr &&
        (found == nullptr || quarry->distanceTo(playerX_, playerY_) <
                                 found->distanceTo(playerX_, playerY_))) {
        found = quarry;
    }
    if (found == nullptr) {
        return result;
    }
    if (found->role() == ActorRole::Vermin) {
        // Killing vermin under a captains' roof is not a brawl and the house
        // has no opinion about it: no offence is reported, nobody is told, and
        // the fight classifier is never asked. Every one of those omissions is
        // deliberate and this is where they are stated.
        Actor* rat = mutableActorById(found->id());
        if (rat == nullptr) {
            return result;
        }
        result.swung = true;
        result.targetId = rat->id();
        result.targetName = rat->name();
        Fighter prey = rat->asFighter();
        // FATIGUE BUILD: the swing is powered by the wind it was thrown on --
        // term read first, then the cost paid -- and MGT's reader rides the
        // damage. Same draw, same order; see strike()'s own header.
        const std::int32_t swingTerm = fatigue_.termQ8();
        fatigue_.drain(kPunchFatiguePoints * kFatiguePointFine);
        result.blow = strike(playerWeapon_, prey, drawForPlayerAction(),
                             meleeDamageBonus(effectiveAttributes().value(AttributeId::Might)),
                             swingTerm);
        rat->setHealth(prey.hp, prey.hpMax);
        rat->setActivity(result.blow.downed ? Activity::Downed : Activity::Walking);
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
    // FATIGUE BUILD: identical to the vermin swing above -- term first, cost
    // paid, MGT on the damage. The classify above already ruled this the
    // room's fight, so the cost lands only on a swing that was thrown.
    const std::int32_t swingTerm = fatigue_.termQ8();
    fatigue_.drain(kPunchFatiguePoints * kFatiguePointFine);
    result.blow = strike(playerWeapon_, victim, drawForPlayerAction(),
                         meleeDamageBonus(playerAttributes_.value(AttributeId::Might)),
                         swingTerm);
    target->setHealth(victim.hp, victim.hpMax);
    target->setActivity(result.blow.downed ? Activity::Downed : Activity::Brawling);
    target->faceToward(playerX_, playerY_);
    if (result.blow.downed) {
        // S8: THE REMATCH, and what winning one is worth. The grudge comes
        // down; the rung, the house, the toll and the charge do not. You can
        // beat him. You cannot un-found his guild.
        nemesis_.recordVictory(target->id());
    }
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
            std::int32_t hpAfter = playerFighter.hp;
            if (playerBlocking_) {
                // THE GUARD. Same roll, same draw stream -- blockedDamage is a
                // pure function arguing about what the landed blow is WORTH,
                // never a second chance at whether it landed. Scaled by
                // SHIELDWALL and never to zero (see brawl.hpp), and every blow
                // it softens trains the skill -- Morrowind's own rule, taking
                // a hit on raised arms is how a guard is learned.
                const std::int32_t kept =
                    blockedDamage(blow.damage, dialogue_.skills().level(kBlockSkill));
                hpAfter = std::max(0, fighters.front().hp - kept);
                blowsBlocked_ = wrap_add(blowsBlocked_, 1);
                dialogue_.skills().use(kBlockSkill);
            }
            playerHp_ = std::max(kPlayerBrawlFloor, hpAfter);
            // WHOSE FIST IT WAS. S8 turns being put down into a promotion for
            // whoever did it, so the room has to know which of the men swinging
            // at it landed the last one -- and it is the LAST, not the first,
            // because a man who joins in at the end and finishes you is who the
            // room saw standing over you.
            lastBlowBy_ = id;
        }
        actor->faceToward(playerX_, playerY_);
    }

    if (playerHp_ <= kPlayerBrawlFloor && !playerFloored_) {
        // Down. A brawl stops there -- see kPlayerBrawlFloor -- and what
        // follows is not more fighting, it is being carried out.
        applyDefeat(lastBlowBy_);
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
// S8 -- the nemesis
// ---------------------------------------------------------------------------

RiseWorld Tavern::riseWorld() noexcept {
    RiseWorld world;
    world.guilds = &dialogue_.standings();
    world.ledger = &dialogue_.ledger();
    world.roll = roll_;
    for (const Actor& actor : actors_) {
        if (!actor.present() || actor.role() == ActorRole::Vermin) {
            continue;
        }
        world.presentIds.push_back(actor.id());
        world.presentFactions.push_back(factionOf(actor));
    }
    return world;
}

void Tavern::applyDefeat(std::int32_t winnerId) {
    playerFloored_ = true;
    playerHp_ = kPlayerBrawlFloor;
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
    lastBlowBy_ = -1;

    const Actor* winner = actorById(winnerId);
    if (winner == nullptr || winner->role() == ActorRole::Vermin) {
        // Nobody in particular put you down -- a fall, a rat, the floor itself.
        // There is no rivalry with a staircase.
        defeatRelease_ = true;
        return;
    }

    Defeat defeat;
    defeat.actorId = winner->id();
    defeat.who = winner->name();
    defeat.epithet = winner->epithet();
    defeat.day = dayNumber();
    defeat.playerCoin = playerCoin_;
    // The job family the ROOM says he has, and his trade. The faction is
    // derived from the family through the owner's own factions.json inside the
    // book; nothing here tabulates it a second time.
    const std::size_t index = static_cast<std::size_t>(winner->id() - 1);
    const RosterEntry* entry = nullptr;
    if (index < kStaff.size()) {
        entry = &kStaff[index];
    } else if (index - kStaff.size() < kPatrons.size()) {
        entry = &kPatrons[index - kStaff.size()];
    }
    if (entry != nullptr) {
        defeat.jobPrefix = std::string(jobFamilyKey(entry->family));
        defeat.trade = entry->skillId;
    }
    // The Skyrunner contact presents as a wastrel and IS villain.skyrunner --
    // presented identity against true identity, exactly as factionOf reads it.
    if (winner->role() == ActorRole::SkyrunnerContact) {
        defeat.jobPrefix = "villain";
    }

    lastDefeat_ = nemesis_.recordDefeat(defeat, riseWorld());

    // WHAT THE ROOM OWNS, applied here and nowhere else: the purse he went
    // through, and what everybody is carrying afterwards.
    const std::int32_t taken = std::min(playerCoin_, lastDefeat_.coinTaken);
    playerCoin_ -= taken;
    lastDefeat_.coinTaken = taken;
    if (Actor* paid = mutableActorById(winnerId); paid != nullptr) {
        paid->giveCoin(taken);
    }
    // AND HE SAYS SOMETHING, out of the owner's own tables. The book picks
    // WHICH authored key applies -- it knows nothing about barks -- and the
    // room, which has the tables, resolves it. Nothing anywhere writes the
    // line, which is the rule the whole conversation layer is built on.
    const std::vector<std::string> chain{lastDefeat_.barkKey, std::string("nemesis.taunt")};
    const std::string_view key = dialogue_.barks().resolve(chain);
    if (!key.empty()) {
        lastDefeat_.taunt =
            std::string(dialogue_.barks().line(key, winner->id() + lastDefeat_.wins));
    }
    armRivals();
    // A rung changed hands in this room, so the room learns whose side people
    // are on -- the same call a player climbing a ladder makes.
    applyRivalHostility();
    defeatRelease_ = true;
}

void Tavern::armRivals() {
    for (Actor& actor : actors_) {
        const Nemesis* rival = nemesis_.of(actor.id());
        if (rival == nullptr) {
            continue;
        }
        // HE COMES PREPARED. Fists, then something off a table, then a blade --
        // and brawl.hpp's own rule then says a fight with him is no longer this
        // room's business. That is not decoration: the room refuses to resolve
        // it with fist rules, out loud, which is what a man who has beaten you
        // twice ought to feel like.
        actor.setWeapon(rival->weapon());
        actor.setIntent(rival->intent());
    }
}

void Tavern::attachPeople(const WardPopulation* people) {
    wardPeople_ = people;
    // Today's errands, up the moment the district is known -- not on the next
    // day turn. A session that attaches at construction gets a board bound to
    // the same instant the population itself was placed at.
    if (wardPeople_ != nullptr) {
        dialogue_.postRadiant(dayNumber(), rng_.world_seed(), *wardPeople_);
    }
}

void Tavern::concedeTo(std::int32_t actorId) {
    if (playerFloored_) {
        return;
    }
    applyDefeat(actorId);
}

bool Tavern::takeDefeatRelease() noexcept {
    const bool release = defeatRelease_;
    defeatRelease_ = false;
    return release;
}

void Tavern::reviveAfterDefeat() {
    // The hours are gone and so is a quarter of the purse. NOTHING the rival
    // gained comes back: he is still on the rung, his house is still founded,
    // its toll is still on the price of a mug and the roll still says who holds
    // the Gullet. Permanence is the point.
    skipHours(kBlackoutHours);
    playerHp_ = playerHpMax_;
    playerFloored_ = false;
    if (standing_ == Standing::BeingEjected || standing_ == Standing::BeingWarned) {
        standing_ = Standing::Welcome;
    }
    armRivals();
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

Legend Tavern::earnedLegend() const {
    return legendOf(dialogue_.crimes(), dialogue_.skills(), dialogue_.standings(),
                    dialogue_.contracts(), Casebook{});
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
    // S8: AND WHAT THE MAN WHO BEAT YOU TAKES OFF THE TOP. A rival who has
    // founded a trade house inside the guild behind this counter puts a
    // permanent cut on every price that guild quotes you -- see
    // NemesisBook::tollPercent. It is the same integer channel a rung already
    // moves, so a house founded over your body is legible as the price of a
    // mug going up and staying up.
    terms.guildPercent += nemesis_.tollPercent(factionOf(*bartender));
    // TASK #81: AND WHAT A CONTRACT PAID OFF BUYS BACK. THE TRADE is legend's
    // own name for a run of contracts taken and paid rather than left to
    // expire -- see legendOf's own comment. A rung on it is a discount here,
    // in the same channel and the same units as the guild's own rate, so
    // completing jobs for coin and completing them for standing are the same
    // ladder rather than two the player has to climb twice.
    terms.guildPercent += earnedLegend().pricePercent();
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
    terms.guildPercent += nemesis_.tollPercent(factionOf(*innkeeper));
    // TASK #81: see the matching note on drinkPriceForPlayer -- the same
    // Trade rung buys the same discount on a room.
    terms.guildPercent += earnedLegend().pricePercent();
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

ServiceResult Tavern::sleepReadiness() const noexcept {
    // sleep()'s own three checks, unmoved and unduplicated: sleepUntil calls
    // this before it touches the clock, so the two can never quietly disagree
    // about where a bed answers.
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
    return ServiceResult::Served;
}

ServiceResult Tavern::sleepUntil(std::int32_t hour) {
    const ServiceResult ready = sleepReadiness();
    if (ready != ServiceResult::Served) {
        return ready;
    }
    skipTo(hourOfDay(((hour % 24) + 24) % 24));
    // THE MEND, and the whole of the owner's SLEEP/WAIT distinction in one
    // line. A night in a paid bed restores the body the same total way
    // reviveAfterDefeat always has; WAIT (the render layer's verb) moves the
    // identical clock and touches no hit point. Skill buys neither: this is a
    // bed working, not a number being bought.
    playerHp_ = playerHpMax_;
    return ServiceResult::Served;
}

ServiceResult Tavern::sleep() { return sleepUntil(7); }

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
        case ActorRole::Vermin:
            // Unreachable: nearestTo answers with people. Handled anyway,
            // because the compiler asks and because "unreachable" is a claim
            // that stops being true the day somebody changes nearestTo.
            result.result = ServiceResult::NobodyThere;
            result.line.clear();
            break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// conversation
// ---------------------------------------------------------------------------

std::int32_t Tavern::rosterSkillOf(const Actor& actor) const noexcept {
    // The authored number for this body, out of the roster table, without
    // building a whole Speaker to read one integer. Watchman Cull's kit-keeping
    // is 25 because notables.json says he inventories seized cargo for a
    // living, and that is exactly the skill a search is fought with.
    const std::size_t index = static_cast<std::size_t>(actor.id() - 1);
    if (index < kStaff.size()) {
        return kStaff[index].skillLevel;
    }
    if (index - kStaff.size() < kPatrons.size()) {
        return kPatrons[index - kStaff.size()].skillLevel;
    }
    return 0;
}

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
    // S8. What he is to the PLAYER, which is a different fact from what he is
    // to the ward. Only ever non-zero for somebody who has had them on the
    // floor, which is nearly nobody.
    if (const Nemesis* rival = nemesis_.of(actor.id()); rival != nullptr) {
        speaker.rivalWins = rival->wins;
        speaker.rivalTitle = rival->title;
        if (const ChapterRaw* house = nemesis_.chapters().at(rival->chapter);
            house != nullptr) {
            speaker.rivalHouse = house->displayName;
        }
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
    //   4. S9 -- AND THEY ACTUALLY NOTICED. Light, sound, whether they were
    //      facing you and how much SKYRUNNING is behind your feet. Asked
    //      through the one rule witnessCount asks, so a deed cannot be
    //      remembered by somebody the heat never counted.
    if (!playerKnown_) {
        return;
    }
    for (const Actor& actor : actors_) {
        if (actor.id() == victimId) {
            continue;
        }
        if (!noticeBy(actor).seen) {
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
                // The refusal a person would say, not the enum's name for it.
                reply.line = std::string(
                    counterRefusal(served, drink ? Goods::Drink : Goods::Room));
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
    std::int32_t seen = 0;
    for (const Actor& actor : actors_) {
        if (actor.id() == exceptId) {
            continue;
        }
        // S9 ROUTES THIS THROUGH THE NOTICE RULE. The three clauses S3 fixed --
        // range, floor, line of sight -- are still every bit of it that they
        // were, and they now live in noticeBy() with light, sound, facing and
        // the skill beside them. One rule, two callers, and no way for a crime
        // to be witnessed by one path and missed by the other.
        if (noticeBy(actor).seen) {
            ++seen;
        }
    }
    return seen;
}

// ---------------------------------------------------------------------------
// S9: light, sound, and who is in a position to notice
// ---------------------------------------------------------------------------

void Tavern::setPlayerMotion(bool moving, bool running) noexcept {
    stealth_.setMotion(moving, running);
}

std::vector<SimLight> Tavern::simLights() const {
    // ONE LIST OF LAMPS, TWO CONSUMERS. houseLights() is what the renderer
    // draws and this is what the law weighs, and they are the same lights
    // derived from the same rule: a flame that is bright to the eye and dark to
    // a watchman would be the exact bug that having two fields invites.
    std::vector<SimLight> lights;
    for (const gull::HouseLight& light : houseLights()) {
        std::int32_t luminance = 0;
        switch (light.kind) {
            case gull::LightKind::Hearth:
                // A banked fire in a stone hearth throws the furthest of the
                // three, which is why the room going dark at three in the
                // morning is the burglar's hour.
                luminance = 22;
                break;
            case gull::LightKind::Lantern:
                luminance = 16;
                break;
            case gull::LightKind::Candle:
                luminance = 9;
                break;
        }
        lights.push_back(SimLight{light.x, light.y, light.band, luminance});
    }
    return lights;
}

std::int32_t Tavern::lightAt(std::int32_t tileX, std::int32_t tileY,
                             std::int32_t band) const noexcept {
    const bool indoors = gull::insideFootprint(tileX, tileY);
    return illuminationAt(simLights(), tileX, tileY, band, timeOfDay_, indoors);
}

std::int32_t Tavern::lightOnPlayer() const noexcept {
    if (!playerKnown_) {
        return 0;
    }
    return lightAt(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
}

Notice Tavern::noticeBy(const Actor& actor) const noexcept {
    NoticeInput in;
    in.stance = stealth_.stance();
    in.noise = stealth_.noise();
    in.roomNoise = noise();
    in.sneakLevel = dialogue_.skills().level(kRoofSkill);
    if (!playerKnown_) {
        in.oblivious = true;
        return noticeOf(in);
    }
    // The three refusals that are not a matter of degree. A rat is not a
    // witness, a man on the floor is not a witness, and somebody who is not in
    // the room at all is not a witness.
    if (!actor.present() || actor.activity() == Activity::Downed ||
        actor.role() == ActorRole::Vermin) {
        in.oblivious = true;
        return noticeOf(in);
    }
    // SAME FLOOR. A body on the guest floor is not in the taproom, whatever its
    // (x, y) says -- the S3 review's own finding, kept exactly.
    if (actor.band() != playerBand_) {
        in.oblivious = true;
        return noticeOf(in);
    }
    const std::int32_t dx = actor.x() - playerX_;
    const std::int32_t dy = actor.y() - playerY_;
    const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (distance > kWitnessRangeTiles * kSubOne) {
        in.oblivious = true;
        return noticeOf(in);
    }
    in.distanceQ8 = distance;
    // Arm's reach needs no sight line -- see kWitnessReachTiles on why the bar
    // counter is the reason that clause exists.
    in.lineOfSight = distance <= kWitnessReachTiles * kSubOne || tiles_ == nullptr ||
                     tiles_->lineOfSight(actor.tileX(), actor.tileY(), q8_tile(playerX_),
                                         q8_tile(playerY_), playerBand_);
    in.observerFacing = actor.facing();
    in.bearingToBody = bearingTo(actor.x(), actor.y(), playerX_, playerY_);
    // VERIFICATION GAP (S9): the rule reads the light on the PLAYER'S tile and
    // nothing else. An actor standing under a lantern is no easier for the
    // player to make out than one in the dark, because nothing in this build
    // asks -- there is no player-side perception at all, only the room's
    // perception of the player. The field is symmetric and would answer it;
    // what is missing is a caller.
    in.light = lightOnPlayer();
    // WHOSE JOB IS LOOKING. A bouncer on the floor, anybody already closing on
    // the player, and every watchman -- derived from the owner's own
    // factions.json through factionOf, so nobody wrote a second table.
    const Activity doing = actor.activity();
    in.alert = actor.role() == ActorRole::Bouncer || doing == Activity::Watching ||
               doing == Activity::Warning || doing == Activity::Ejecting ||
               doing == Activity::Brawling ||
               factionOf(actor) == dialogue_.factions().indexOf("watch");
    return noticeOf(in);
}

std::int32_t Tavern::watchersInReach() const noexcept {
    if (!playerKnown_) {
        return 0;
    }
    std::int32_t count = 0;
    for (const Actor& actor : actors_) {
        // THE SAME FOUR REFUSALS noticeBy applies before it weighs anything --
        // present, upright, not vermin, same floor, within the witness range.
        // Deliberately not factored into a shared helper: noticeBy returns a
        // Notice and this returns a count, and a helper that returned "is this
        // one oblivious" would be a third place the range could drift.
        if (!actor.present() || actor.activity() == Activity::Downed ||
            actor.role() == ActorRole::Vermin || actor.band() != playerBand_) {
            continue;
        }
        const std::int32_t dx = actor.x() - playerX_;
        const std::int32_t dy = actor.y() - playerY_;
        const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (distance > kWitnessRangeTiles * kSubOne) {
            continue;
        }
        ++count;
    }
    return count;
}

Notice Tavern::worstNotice() const noexcept {
    Notice worst;
    bool any = false;
    for (const Actor& actor : actors_) {
        const Notice one = noticeBy(actor);
        // The one who reads you best. Ties break on the earlier id, because
        // actors_ is never reordered -- so two runs cannot disagree about who
        // is looking hardest.
        //
        // S10 FIXED THE MARGIN THIS RANKS ON. It was `read - cover`, and
        // noticeOf decides with `read + noise > cover` -- so the NOISE the body
        // is making was left out of the comparison that picks whose opinion
        // counts. A body running through a dark room could be ranked behind a
        // sleeping man whose Notice reads a flat zero, and `hidden()` would
        // then answer off the sleeper while a bouncer four tiles away had
        // seen=true. Found by a case that expected an upright sprint to be
        // noticed and got HIDDEN back with `read=0 cover=20 noise=0` -- the
        // signature of an oblivious actor winning the ranking.
        const std::int32_t margin = one.read + one.noise - one.cover;
        if (!any || margin > worst.read + worst.noise - worst.cover) {
            worst = one;
            any = true;
        }
    }
    return worst;
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

// ---------------------------------------------------------------------------
// the sheet and the wind -- fatigue build
// ---------------------------------------------------------------------------

void Tavern::setPlayerAttributes(const AttributeBlock& attributes) noexcept {
    playerAttributes_ = attributes;
    // The boot seam refills on purpose: a body arrives at the Docks rested,
    // and this is called once beside setPlayerHealth, never mid-game.
    fatigue_.resetFor(playerAttributes_);
}

void Tavern::stepPlayerFatigue(bool moving, bool sprinting) noexcept {
    if (moving && sprinting) {
        // The sprinting step pays its drain and earns nothing back -- the
        // reference's own "regen while not draining" rule.
        fatigue_.drain(kSprintDrainFinePerStep);
        return;
    }
    // HELD-EFFECTS BUILD: the EFFECTIVE sheet, here and at every attribute
    // reader below -- a live tuning is felt exactly where the base sheet is,
    // and nowhere else. Identical to the base at an empty table.
    fatigue_.regen(fatigueRegenFinePerStep(
        effectiveAttributes().value(AttributeId::Vigor),
        dialogue_.skills().level(kGritSkill), moving));
}

void Tavern::chargePlayerJump() noexcept {
    // A standing jump is legs, not climbing: AGI still prices it, but no
    // amount of skyrunning makes hopping free -- the roofs teach walls.
    fatigue_.drain(verticalFatigueCostFine(
        kJumpFatiguePoints, effectiveAttributes().value(AttributeId::Agility), 0));
}

void Tavern::chargePlayerMantle() noexcept {
    fatigue_.drain(verticalFatigueCostFine(
        kMantleFatiguePoints, effectiveAttributes().value(AttributeId::Agility),
        dialogue_.skills().level(kRoofSkill)));
}

void Tavern::chargePlayerLeap() noexcept {
    fatigue_.drain(verticalFatigueCostFine(
        kLeapFatiguePoints, effectiveAttributes().value(AttributeId::Agility),
        dialogue_.skills().level(kRoofSkill)));
}

AttributeBlock Tavern::effectiveAttributes() const noexcept {
    // Base sheet plus every live tuning, per-attribute total clamped to the
    // spellforge limit BEFORE it is added -- "a live nudge is read by every
    // check in the game, so it is held to +/-2 however many rows stack"
    // (spells.json's own notes) -- and AttributeBlock::setValue holds the
    // floor/ceiling after. With no holds live this returns the base sheet
    // bit for bit, which is what keeps every pre-existing capture identical.
    AttributeBlock out = playerAttributes_;
    if (heldEffects_.empty()) {
        return out;
    }
    std::array<std::int32_t, kAttributeCount> delta{};
    for (const ActiveHold& hold : heldEffects_) {
        delta[static_cast<std::size_t>(hold.attribute)] += hold.magnitude;
    }
    for (std::size_t i = 0; i < kAttributeCount; ++i) {
        const AttributeId id = static_cast<AttributeId>(i);
        const std::int32_t clamped =
            std::clamp(delta[i], -kAttributeModifierLimit, kAttributeModifierLimit);
        if (clamped != 0) {
            out.setValue(id, playerAttributes_.value(id) + clamped);
        }
    }
    return out;
}

void Tavern::applyHeldEffects() noexcept {
    // The one derived consequence a hold has beyond the readers that consult
    // effectiveAttributes() live: the pool's ceiling is a function of the
    // sheet, so it re-derives here -- WITHOUT a refill (resizeFor's own
    // contract), or recasting a MGT tuning would be a wind faucet.
    fatigue_.resizeFor(effectiveAttributes());
}

void Tavern::sweepHeldEffects() {
    bool lapsed = false;
    for (std::size_t i = 0; i < heldEffects_.size();) {
        if (heldEffects_[i].expiresAt <= elapsed_) {
            heldEffects_.erase(heldEffects_.begin() + static_cast<std::ptrdiff_t>(i));
            lapsed = true;
        } else {
            ++i;
        }
    }
    if (lapsed) {
        applyHeldEffects();
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
    // S9. THE BOX IS LOCKED. S5's burglary was a keypress beside a container
    // that had no lid on it; CRACKSMANSHIP was read once, afterwards, to scale
    // what fell out. The lock has to be OPEN before a hand goes in, and getting
    // it open is beginPick() and probeLock() -- or forceLock(), and the noise.
    if ((openedLocks_ & bit) == 0) {
        out.result = ServiceResult::Refused;
        out.line = (jammedLocks_ & bit) != 0 ? "THE LOCK IS RUINED. FORCE IT OR LEAVE IT."
                                             : "IT IS LOCKED.";
        return out;
    }
    crackedBoxes_ |= bit;
    const std::int32_t craft = dialogue_.skills().level(kThieverySkill);
    // WHAT A HAND THE WARD HAS TAKEN STILL MANAGES. The only lasting
    // statistical penalty in this build, and it is canon's: DECISIONS.md says a
    // Skyrunner loses the hand on a first offence, so a cracksman who has been
    // through the Watch's yard is worth half of what he was.
    const std::int32_t hands = dialogue_.crimes().takePercent();
    out.coin = (kStrongboxCoin + craft / 4) * hands / 100;
    out.loot = std::max(1, (1 + craft / 20) * hands / 100);
    // AND WHAT A BOOT THROUGH THE LID COSTS. Forcing always works, and this is
    // the price of it: half the coin, because a box that has been stove in
    // spills, and because a cracksman who can pick should.
    if ((forcedLocks_ & bit) != 0) {
        out.coin = (out.coin * kForcedYieldPercent) / 100;
    }
    out.seen = witnessCount(kPlayerActorId) > 0;
    playerCoin_ = wrap_add(playerCoin_, out.coin);
    dialogue_.setPlayerCoin(playerCoin_);
    dialogue_.crimes().takeLoot(out.loot);
    // AND A PIECE WITH A NAME ON IT. The anonymous "loot" above is what a fence
    // buys by the handful and asks nothing about; this is the cup, the chart,
    // the signet -- the thing a recovery contract can ask for by name and a
    // watchman can hang on you. One a box, because there is one of it.
    const std::int32_t piece = dialogue_.crimes().stash().add(Contraband::Artifact, 1);
    // AND IT IS THE PIECE SOMEBODY ASKED FOR. S6 put an integer in the sack and
    // let the brief promise a christening cup; the object had no existence.
    // Now the box yields the object the job named, the job records that it has
    // it, and a recovery contract can only be settled with pieces lifted while
    // the player was actually carrying it. nullptr means nobody had asked --
    // then it is anonymous loot and a fence's problem.
    const Contract* wanted = piece > 0 ? dialogue_.contracts().recoverPiece() : nullptr;
    dialogue_.noteCrime(Crime::Burgle, out.seen);
    if (out.seen) {
        spreadWitness(kPlayerActorId, Deed::Robbed);
        reportOffence(Offence::Stole);
    }
    out.result = ServiceResult::Served;
    // out.loot is (1 + cracksmanship/20) scaled by what hands the player still
    // has, so it is 2 at level 20 and 3 at level 40 -- and until this pass a
    // cracksman good enough to take three of anything was told he had taken
    // "3 PIECE".
    out.line = "CRACKED IT - " + std::to_string(out.coin) + "C AND " +
               std::to_string(out.loot) + (out.loot == 1 ? " PIECE" : " PIECES") +
               (out.seen ? ", AND SEEN." : ".");
    if (wanted != nullptr && !wanted->thing.empty()) {
        // THE OWNER'S OWN WORDS, upper-cased for the 4x6 font and nothing else
        // done to them. No programmer named this object.
        std::string named;
        named.reserve(wanted->thing.size());
        for (const char c : wanted->thing) {
            named.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        out.line += " " + named + ".";
    } else if (piece > 0) {
        out.line += " SOMETHING WITH A NAME ON IT.";
    }
    return out;
}

// ---------------------------------------------------------------------------
// S9: the lock, the wire, and the boot
// ---------------------------------------------------------------------------

Lock Tavern::strongboxLock(std::int32_t room) noexcept {
    Lock lock;
    // THE ROOM INDEX IS THE LOCK'S IDENTITY, so the same seed builds the same
    // four locks every time and a player who has worked this box before knows
    // where its pins sit. Offset by one so room 0 is not lock id 0, which is
    // what an uninitialised Lock would be.
    lock.id = room + 1;
    lock.pins = kStrongboxPins;
    lock.wards = strongboxWards(room);
    return lock;
}

void Tavern::setPicks(std::int32_t picks) noexcept {
    picks_ = std::max(0, picks);
}

void Tavern::movePick(std::int32_t delta) noexcept {
    picking_.moveDepth(delta);
}

void Tavern::abandonPick() noexcept {
    picking_.abandon();
    pickingRoom_ = -1;
}

Tavern::PickResult Tavern::beginPick() {
    PickResult out;
    if (!playerKnown_ || playerBand_ != gull::kUpperBand) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO PICK.";
        return out;
    }
    const std::int32_t room = gull::roomAtStand(q8_tile(playerX_), q8_tile(playerY_));
    if (room < 0) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO PICK.";
        return out;
    }
    const std::int32_t bit = 1 << room;
    if (room == rentedRoom_) {
        // Your own box, and your own key. Refused for the same reason cracking
        // it is: renting a bed to burgle yourself is not a crime and must not
        // be a way to farm the skill either.
        out.result = ServiceResult::Refused;
        out.line = "THAT ONE IS YOURS.";
        return out;
    }
    if ((openedLocks_ & bit) != 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "IT IS ALREADY OPEN.";
        return out;
    }
    if ((jammedLocks_ & bit) != 0) {
        out.result = ServiceResult::Refused;
        out.feel = Feel::Jammed;
        out.line = "THE WARDS ARE RUINED. FORCE IT OR LEAVE IT.";
        return out;
    }
    if (picks_ <= 0) {
        out.result = ServiceResult::NoCoin;
        out.line = "NO WIRE LEFT.";
        return out;
    }
    picking_.begin(strongboxLock(room), worldSeed_, dialogue_.skills().level(kThieverySkill));
    pickingRoom_ = room;
    out.result = ServiceResult::Served;
    // picks_ is at least one here -- the arm above refuses an empty roll -- and
    // one is exactly the case a player hits on their last wire, which is when
    // this line matters most and when it used to read "1 PICKS".
    const std::int32_t pins = picking_.lock().pins;
    out.line = "WIRE IN. " + std::to_string(pins) + (pins == 1 ? " PIN, " : " PINS, ") +
               std::to_string(picks_) + (picks_ == 1 ? " PICK." : " PICKS.");
    return out;
}

Tavern::PickResult Tavern::probeLock() {
    PickResult out;
    if (!picking_.open() || pickingRoom_ < 0) {
        out.result = ServiceResult::NobodyThere;
        out.feel = Feel::Nothing;
        out.line = "NOTHING UNDER THE WIRE.";
        return out;
    }
    const std::int32_t room = pickingRoom_;
    const std::int32_t bit = 1 << room;
    const Feel feel = picking_.probe(picks_);
    out.feel = feel;
    out.result = ServiceResult::Served;

    // EVERY PROBE IS A SOUND, and a snapped pick is a louder one. It goes into
    // the same StealthState a footstep does, so the room judges a lock being
    // worked exactly the way it judges everything else.
    stealth_.makeNoise(feel == Feel::Broke || feel == Feel::Jammed ? kBreakNoise : kProbeNoise);

    // THE HANDS ARE CHARGED FOR THE ATTEMPT, not for the success. Morrowind's
    // own rule, and the one this project has been applying since S3: you get
    // better at locks by working locks, including the ones that beat you.
    dialogue_.skills().use(kThieverySkill);

    switch (feel) {
        case Feel::Set:
            out.line = "A PIN DROPS. " + std::to_string(picking_.pinsSet()) + "/" +
                       std::to_string(picking_.lock().pins) + ".";
            break;
        case Feel::TooShallow:
            out.line = "TOO SHALLOW.";
            break;
        case Feel::TooDeep:
            out.line = "TOO DEEP.";
            break;
        case Feel::NoFeel:
            out.line = "NOTHING. YOU CANNOT TELL WHERE.";
            break;
        case Feel::Broke:
            out.line = "THE PICK SNAPS. " + std::to_string(picks_) + " LEFT.";
            break;
        case Feel::Jammed:
            jammedLocks_ |= bit;
            pickingRoom_ = -1;
            out.line = "THE LAST PICK SNAPS OFF IN THE WARDS.";
            break;
        case Feel::Open:
            openedLocks_ |= bit;
            pickingRoom_ = -1;
            // A LOCK THAT COMES OPEN IS WORTH MORE THAN A PIN. The extra effort
            // is the whole of the level-up curve for this skill: a cracksman
            // rises by finishing, not by fiddling.
            dialogue_.skills().use(kThieverySkill, 3);
            out.opened = true;
            out.line = "IT GIVES. THE BOX IS OPEN.";
            break;
        default:
            out.line = "NOTHING UNDER THE WIRE.";
            break;
    }
    // WHO HEARD IT. Judged after the noise is in the air, which is why
    // makeNoise is above and not below: probing a lock in a house with people
    // still in it is a different act from probing one at four in the morning,
    // and it is the same rule that decides both.
    out.seen = witnessCount(kPlayerActorId) > 0;
    if (out.seen) {
        spreadWitness(kPlayerActorId, Deed::Robbed);
        reportOffence(Offence::Stole);
        out.line += " SEEN.";
    }
    return out;
}

Tavern::PickResult Tavern::forceLock() {
    PickResult out;
    if (!playerKnown_ || playerBand_ != gull::kUpperBand) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO FORCE.";
        return out;
    }
    const std::int32_t room = gull::roomAtStand(q8_tile(playerX_), q8_tile(playerY_));
    if (room < 0) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO FORCE.";
        return out;
    }
    if (room == rentedRoom_) {
        out.result = ServiceResult::Refused;
        out.line = "THAT ONE IS YOURS.";
        return out;
    }
    const std::int32_t bit = 1 << room;
    if ((openedLocks_ & bit) != 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "IT IS ALREADY OPEN.";
        return out;
    }
    picking_.abandon();
    pickingRoom_ = -1;
    openedLocks_ |= bit;
    forcedLocks_ |= bit;
    // THE LOUDEST THING IN THE BUILDING. Nothing about this is subtle and that
    // is the design: force is always available, always works, and is never the
    // quiet answer.
    stealth_.makeNoise(kForceNoise);
    out.result = ServiceResult::Served;
    out.feel = Feel::Forced;
    out.opened = true;
    out.line = "THE LID GOES. LOUDLY.";
    out.seen = witnessCount(kPlayerActorId) > 0;
    if (out.seen) {
        spreadWitness(kPlayerActorId, Deed::Robbed);
        reportOffence(Offence::Stole);
        out.line += " SEEN.";
    }
    return out;
}

// ---------------------------------------------------------------------------
// S9: the hand in the coat, and the wire that opens what it cannot reach
// ---------------------------------------------------------------------------

Tavern::StealResult Tavern::liftFrom() {
    StealResult out;
    if (!playerKnown_) {
        out.result = ServiceResult::TooFar;
        out.line = "NOBODY WITHIN REACH.";
        return out;
    }
    const Actor* mark = nearestTo(playerX_, playerY_, kLiftReachQ8);
    if (mark == nullptr) {
        out.result = ServiceResult::TooFar;
        out.line = "NOBODY WITHIN REACH.";
        return out;
    }
    if (mark->coin() <= 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = upperCase(mark->name()) + " HAS NOTHING ON THEM.";
        return out;
    }

    // TWO SKILLS AND ONE NUMBER. See the note on kLiftNoticePerPoint for why
    // stealth moves the mark's guard rather than deciding the lift outright.
    //
    // Deterministic, and no draw: a roll here would be a draw the twin-run gate
    // has to account for, which is the same reason the dialogue layer's own
    // PickPocket topic has never rolled one either.
    const Notice notice = noticeBy(*mark);
    const std::int32_t craft = dialogue_.skills().level(kThieverySkill);
    // Their STREETWISE, out of the roster, through the same accessor the
    // dialogue layer's own PickPocket topic reads it with. One number, one
    // source, and no second table to let drift.
    const std::int32_t wits = speakerFor(*mark).awareness;
    const std::int32_t swing = std::clamp((notice.read - notice.cover) / kLiftNoticePerPoint,
                                          -kLiftStealthSwing, kLiftStealthSwing);
    const std::int32_t guard = std::max(0, wits + swing);
    const bool caught = craft < guard;

    // THE APPROACH IS CHARGED WHETHER OR NOT THE HAND WAS. You learn to move
    // quietly by moving quietly at somebody, and being caught teaches more than
    // most things do.
    dialogue_.skills().use(kRoofSkill);

    if (caught) {
        out.result = ServiceResult::Refused;
        out.seen = true;
        out.line = upperCase(mark->name()) +
                   (notice.seen ? " WAS WATCHING YOUR HANDS." : " FEELS THE HAND AND TURNS.");
        dialogue_.ledger().witness(mark->id(), Deed::Robbed);
        dialogue_.noteCrime(Crime::Lift, true);
        spreadWitness(mark->id(), Deed::Robbed);
        reportOffence(Offence::Stole);
        return out;
    }

    const std::int32_t markId = mark->id();
    const std::int32_t purse = mark->coin();
    const std::int32_t lifted = std::min(purse, 1 + craft / 8 + purse / 4);
    if (Actor* lighter = mutableActorById(markId); lighter != nullptr) {
        lighter->setCoin(purse - lifted);
    }
    playerCoin_ = wrap_add(playerCoin_, lifted);
    dialogue_.setPlayerCoin(playerCoin_);
    // A purse carries something that is not coin, and that something is what a
    // fence is for.
    dialogue_.crimes().takeLoot(kLiftPieces);
    out.result = ServiceResult::Served;
    out.coin = lifted;
    out.loot = kLiftPieces;
    // WHO ELSE SAW IT. The mark did not; that is what "not caught" means. The
    // rest of the room is a separate question and it is the room's own rule
    // that answers it.
    out.seen = witnessCount(markId) > 0;
    dialogue_.noteCrime(Crime::Lift, out.seen);
    if (out.seen) {
        spreadWitness(markId, Deed::Robbed);
        reportOffence(Offence::Stole);
    }
    out.line = "LIFTED " + std::to_string(lifted) + "C OFF " + upperCase(mark->name()) +
               (out.seen ? ", AND SEEN." : ".");
    return out;
}

Tavern::StealResult Tavern::buyPicks() {
    StealResult out;
    if (!playerKnown_) {
        out.result = ServiceResult::TooFar;
        out.line = "NOBODY HERE SELLS WIRE.";
        return out;
    }
    const Actor* contact = nullptr;
    for (const Actor& actor : actors_) {
        if (actor.role() != ActorRole::SkyrunnerContact || !actor.present()) {
            continue;
        }
        if (actor.distanceTo(playerX_, playerY_) <= kReachQ8) {
            contact = &actor;
            break;
        }
    }
    if (contact == nullptr) {
        out.result = ServiceResult::TooFar;
        out.line = "NOBODY HERE SELLS WIRE.";
        return out;
    }
    const std::int32_t roofs = dialogue_.factions().indexOf("skyrunners");
    if (!dialogue_.standings().isMember(roofs)) {
        // Nobody sells a stranger picks, for the same reason nobody hands one a
        // bale. The roofs' first rung is what a lock is actually gated behind.
        out.result = ServiceResult::Refused;
        out.line = "HE SELLS WIRE TO HIS OWN.";
        return out;
    }
    const std::int32_t price = kPickPrice * kPicksPerSet;
    if (playerCoin_ < price) {
        out.result = ServiceResult::NoCoin;
        out.line = "NOT FOR WHAT YOU ARE CARRYING.";
        return out;
    }
    playerCoin_ = wrap_add(playerCoin_, -price);
    dialogue_.setPlayerCoin(playerCoin_);
    // TASK #81: MORE WIRE FOR THE SAME COIN, off THE WIRE. The price above is
    // what kPicksPerSet is worth and does not move; a rung on the ward's
    // opinion of your hands is a few extra picks in the set the same coin
    // buys, exactly as legendOf's own comment on picksPerSetBonus promises.
    const std::int32_t given = kPicksPerSet + earnedLegend().picksPerSetBonus();
    picks_ += given;
    out.result = ServiceResult::Served;
    out.coin = -price;
    out.line = std::to_string(given) + " PICKS FOR " + std::to_string(price) + "C. " +
               std::to_string(picks_) + " IN THE ROLL.";
    return out;
}

// ---------------------------------------------------------------------------
// S6: the Watch, and the one thing S5's warrant could not do
// ---------------------------------------------------------------------------

bool Tavern::canSeePlayer(const Actor& actor) const noexcept {
    // S9. THE WATCHMAN IS SUBJECT TO THE SAME RULE AS EVERYBODY ELSE, and this
    // is the most consequential place it applies: watchStance()'s own header
    // note calls the Closing beat "THE WINDOW: out of the door, out of his
    // sight, and it is over". Until now the only way through that window was
    // distance. It is now also DARK and QUIET and DOWN ON YOUR HAUNCHES, which
    // is what a window is supposed to be.
    //
    // kWatchSightTiles and kWitnessRangeTiles are both 8 and a static_assert
    // below holds them together, so routing the Watch through the room's own
    // notice rule changes the RANGE not at all -- only what happens inside it.
    static_assert(kWatchSightTiles == kWitnessRangeTiles,
                  "the Watch and the room must agree how far a person can be seen, or "
                  "canSeePlayer and witnessCount are two rules pretending to be one");
    return noticeBy(actor).seen;
}

Actor* Tavern::watchmanWatchingPlayer() noexcept {
    const std::int32_t garrison = dialogue_.factions().indexOf("watch");
    if (garrison < 0) {
        return nullptr;
    }
    Actor* best = nullptr;
    std::int32_t bestDistance = 0;
    for (Actor& actor : actors_) {
        // WHO IS A WATCHMAN IS DERIVED, not tabulated: the room knows an
        // actor's job family and the owner's own factions.json says which
        // faction claims that family's jobs. Nobody wrote a second table.
        if (factionOf(actor) != garrison || !canSeePlayer(actor)) {
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

void Tavern::tickWatch() {
    CrimeLedger& crimes = dialogue_.crimes();
    // S6 SHIPPED THE OPPOSITE OF THIS AND IT WAS AN EXPLOIT. The condemned
    // branch used to return here -- stance idle, no watchman, no notice, no
    // arrest -- which made the ward's HARSHEST sentence its SAFEST state: two
    // Skyrunner arrests bought the rest of the game at zero risk, and the only
    // residual cost (kMaimedTakePercent) had already been paid at the first.
    //
    // A condemned man is not invisible. He is the one face in the ward every
    // watchman already has. There is no early return any more; what
    // condemnation changes is kCondemnedRecognisePermille, below, and it
    // changes it in the direction the fiction says.

    if (watchStance_ == WatchStance::Closing) {
        Actor* officer = watchmanId_ < 0 ? nullptr : mutableActorById(watchmanId_);
        if (officer == nullptr || !officer->present() ||
            officer->activity() == Activity::Downed || !canSeePlayer(*officer)) {
            // OUT OF HIS SIGHT IS OUT OF IT. This is the whole counterplay and
            // the reason the roofs are worth having: a man who gets through the
            // door with a bale on his shoulder has got away with it, and the
            // ward's own eight tiles of sight are what decide that.
            watchStance_ = WatchStance::Idle;
            watchmanId_ = -1;
            watchCause_ = WatchCause::None;
            if (officer != nullptr && officer->activity() == Activity::Warning) {
                officer->setActivity(Activity::Watching);
            }
            return;
        }
        if (tick_ - noticedAtTick_ > kWatchClosingSeconds) {
            // He gave it up. Twelve seconds of a man walking away is twelve
            // seconds of an off-duty watchman deciding his drink is getting
            // warm -- and it is the counterplay stated as a number rather than
            // implied by a sight test.
            watchStance_ = WatchStance::Idle;
            watchmanId_ = -1;
            watchCause_ = WatchCause::None;
            officer->setActivity(Activity::Watching);
            return;
        }
        officer->setActivity(Activity::Warning);
        officer->faceToward(playerX_, playerY_);
        if (officer->distanceTo(playerX_, playerY_) > kMeleeReach) {
            officer->setDestination(q8_tile(playerX_), q8_tile(playerY_), playerBand_);
            return;
        }
        applyArrest(*officer);
        return;
    }

    // Idle. He glances up every few seconds; he is off shift with a drink in
    // his hand, not frisking the room.
    if (tick_ % kWatchLookSeconds != 0) {
        return;
    }
    Actor* officer = watchmanWatchingPlayer();
    if (officer == nullptr) {
        return;
    }
    const Stash& sack = crimes.stash();
    const std::int32_t permille =
        noticePermille(sack.illicitWeight(), dialogue_.skills().level(kHaggleSkill),
                       rosterSkillOf(*officer));
    const bool noticed = passes(rng_.draw(static_cast<std::uint64_t>(officer->id()) ^ 0x5741U,
                                          static_cast<std::int32_t>(tick_ % 4096)),
                                permille);
    // A WARRANT IS NOT A BEACON. He has to connect the face to the paper, and
    // that is its own roll -- see kRecognisePermille on why paper alone being
    // instant cause would make "wanted" mean "the game is over".
    //
    // A CONDEMNED FACE IS. There is no paper to connect any more: the ward
    // passed sentence on this man in public and every watchman in it was told
    // who he was. He is recognised at kCondemnedRecognisePermille whether or
    // not a warrant is out, which is what makes the rope a punishment rather
    // than the amnesty S6 shipped.
    const std::int32_t recognisePermille =
        crimes.condemned() ? kCondemnedRecognisePermille
                           : (crimes.warrant() ? kRecognisePermille : 0);
    const bool recognised =
        recognisePermille > 0 &&
        passes(rng_.draw(static_cast<std::uint64_t>(officer->id()) ^ 0x57415252U,
                         static_cast<std::int32_t>(tick_ % 4096)),
               recognisePermille);
    const WatchCause cause = watchCause(recognised, noticed, sack.illicitUnits());
    if (cause == WatchCause::None) {
        return;
    }
    watchStance_ = WatchStance::Closing;
    watchmanId_ = officer->id();
    watchCause_ = cause;
    noticedAtTick_ = tick_;
    lastDemand_ = officer->name() + ": " +
                  std::string(dialogue_.barks().line(
                      dialogue_.barks().resolve({std::string("watch.demand")}),
                      static_cast<std::int32_t>(tick_ / kWatchLookSeconds)));
    officer->setActivity(Activity::Warning);
}

void Tavern::applyArrest(Actor& officer) {
    CrimeLedger& crimes = dialogue_.crimes();
    const Stash before = crimes.stash();
    const std::int32_t roofs = dialogue_.factions().indexOf("skyrunners");
    const bool skyrunner = dialogue_.standings().isMember(roofs);

    const CrimeLedger::ArrestOutcome outcome =
        crimes.arrest(skyrunner, playerCoin_, drawForPlayerAction());
    playerCoin_ = std::max(0, playerCoin_ - outcome.fine);
    dialogue_.setPlayerCoin(playerCoin_);
    // A job whose goods are in the impound is a job you have lost. THIS is what
    // makes an arrest cost more than a night: the coin was never the point.
    const std::int32_t lost = dialogue_.contracts().seizeFor(before);

    lastArrest_ = ArrestReport{};
    lastArrest_.happened = true;
    lastArrest_.sentence = outcome.sentence;
    lastArrest_.cause = watchCause_;
    lastArrest_.unitsSeized = outcome.unitsSeized;
    lastArrest_.fine = outcome.fine;
    lastArrest_.heldHours = outcome.heldHours;
    lastArrest_.contractsLost = lost;
    lastArrest_.officer = officer.name();

    const char* table = "watch.fined";
    switch (outcome.sentence) {
        case Sentence::Held:
            table = "watch.held";
            break;
        case Sentence::Maimed:
            table = "watch.maimed";
            break;
        case Sentence::Condemned:
            table = "watch.condemned";
            break;
        default:
            break;
    }
    lastArrest_.line = officer.name() + ": " +
                       std::string(dialogue_.barks().line(
                           dialogue_.barks().resolve({std::string(table)}),
                           crimes.arrests() + outcome.unitsSeized));

    if (outcome.heldHours > 0) {
        // The night goes by in the cell and nothing in it is simulated -- the
        // same honest jump sleeping in a rented bed already makes.
        skipHours(outcome.heldHours);
    }
    // Whatever the house was minding is somebody else's problem now.
    standing_ = Standing::Welcome;
    brawlers_.clear();
    respondingBouncerId_ = -1;
    watchStance_ = WatchStance::Idle;
    watchmanId_ = -1;
    watchCause_ = WatchCause::None;
    officer.setActivity(Activity::Watching);
    // And the body is turned loose on the Tarwalk. The room does not own it, so
    // it asks -- see takeArrestRelease.
    arrestRelease_ = true;
}

const Actor* Tavern::respondingWatchman() const noexcept {
    return watchmanId_ < 0 ? nullptr : actorById(watchmanId_);
}

bool Tavern::takeArrestRelease() noexcept {
    const bool pending = arrestRelease_;
    arrestRelease_ = false;
    return pending;
}

// ---------------------------------------------------------------------------
// S6: the vermin, and the knife
// ---------------------------------------------------------------------------

void Tavern::tickVermin() {
    for (Actor& actor : actors_) {
        if (actor.role() != ActorRole::Vermin || !actor.present() ||
            actor.activity() == Activity::Downed) {
            continue;
        }
        if (!actor.atDestination()) {
            continue;
        }
        // A rat does not hold a post; it works along a skirting. One tile at a
        // time, inside the walls, on the room's own draw -- which is why two
        // runs of one seed put every rat in the same corner.
        const std::uint64_t roll = rng_.draw(static_cast<std::uint64_t>(actor.id()), 0);
        const std::int32_t dx = static_cast<std::int32_t>(roll % 3U) - 1;
        const std::int32_t dy = static_cast<std::int32_t>((roll / 3U) % 3U) - 1;
        const std::int32_t toX = actor.tileX() + dx;
        const std::int32_t toY = actor.tileY() + dy;
        if (!gull::insideFootprint(toX, toY) || tiles_ == nullptr ||
            !tiles_->standable(toX, toY, gull::kGroundBand)) {
            continue;
        }
        actor.setDestination(toX, toY, gull::kGroundBand);
        actor.setActivity(Activity::Walking);
    }
}

const Actor* Tavern::nearestVerminTo(std::int32_t xQ8, std::int32_t yQ8,
                                     std::int32_t reachQ8) const noexcept {
    const Actor* best = nullptr;
    std::int32_t bestDistance = reachQ8 + 1;
    for (const Actor& actor : actors_) {
        if (actor.role() != ActorRole::Vermin || !actor.present() ||
            actor.activity() == Activity::Downed) {
            continue;
        }
        const std::int32_t distance = actor.distanceTo(xQ8, yQ8);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &actor;
        }
    }
    return best;
}

Actor* Tavern::downedVerminInReach() noexcept {
    if (!playerKnown_) {
        return nullptr;
    }
    Actor* best = nullptr;
    std::int32_t bestDistance = kReachQ8 + 1;
    for (Actor& actor : actors_) {
        if (actor.role() != ActorRole::Vermin || actor.activity() != Activity::Downed ||
            actor.band() != playerBand_) {
            continue;
        }
        const std::int32_t distance = actor.distanceTo(playerX_, playerY_);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &actor;
        }
    }
    return best;
}

Tavern::StealResult Tavern::takeScalp() {
    StealResult out;
    Actor* quarry = downedVerminInReach();
    if (quarry == nullptr) {
        out.result = ServiceResult::TooFar;
        out.line = "NOTHING HERE TO SKIN.";
        return out;
    }
    const std::int32_t bit = 1 << (quarry->id() - verminFirstId());
    if ((scalpedVermin_ & bit) != 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "ALREADY TAKEN.";
        return out;
    }
    Stash& sack = dialogue_.crimes().stash();
    if (sack.add(Contraband::Scalp, 1) <= 0) {
        out.result = ServiceResult::OutOfStock;
        out.line = "YOU CANNOT CARRY ANOTHER THING.";
        return out;
    }
    scalpedVermin_ |= bit;
    quarry->setActivity(Activity::Away);
    // A knife beside a carcass is FIELDCRAFT, which is the same skill the Java
    // build's own cull verb charges for the same act.
    dialogue_.skills().use(contrabandSkill(Contraband::Scalp), 1);
    out.result = ServiceResult::Served;
    out.loot = 1;
    // NOT A CRIME, and stated rather than implied: the ward pays for these.
    // Nothing goes through noteCrime, no heat is raised and no witness matters.
    out.line = "ONE SCALP. THE WARD PAYS FOR THESE.";
    return out;
}

Tavern::LandingResult Tavern::settleLanding(const RoofResult& move, std::int32_t fellBands,
                                            std::int32_t landedBand, std::int32_t landedX,
                                            std::int32_t landedY) {
    LandingResult out;
    // Every climb, leap and fall is a use of the craft it takes.
    dialogue_.skills().use(kRoofSkill, move.tiles > 1 ? 2 : 1);

    // WHAT THE FALL WAS, IN METRES, BEFORE ANYTHING IS CHARGED FOR IT.
    //
    // #77 REPLACED A STAIRCASE WITH GRAVITY. What was here was
    // `(fellBands - safeBands) * 24`: a flat slab of hit points per storey past
    // a threshold, so a fall was worth nothing at all right up to a line and
    // then worth a fixed lump. It had no height in it anywhere -- change the
    // storey height and the number would not move, which is exactly what
    // happened when the storey tripled and the only fix was to double the
    // literal.
    //
    // The model now is the one the body experiences. You are hurt by the speed
    // you arrive at, v = sqrt(2gh), and by the square of how much of it your legs
    // cannot absorb. Every input is a length in millimetres and the only chosen
    // number in the whole chain is one divisor. See sim/human_scale.hpp.
    out.fellMm = fellBands * kMillimetresPerBand;

    // WHAT SOFTENS IT. Two things, and they are additive because they are both
    // simply "how far you can fall before it starts costing".
    //
    //   the roofs' teaching -- a journeyman skyrunner and a guild that has shown
    //   you where to put your feet are worth kTaughtLandingMm each, and
    //   safeDropBands is still the one place that rule lives.
    //
    //   what is underneath -- water or deep mud. Real, and the reason people
    //   survive going off quays.
    const std::int32_t roofs = dialogue_.factions().indexOf("skyrunners");
    const std::int32_t safeBands = safeDropBands(dialogue_.skills().level(kRoofSkill),
                                                 dialogue_.standings().unlocked(roofs, "roof"));
    std::int32_t cushionMm = kFreeFallMm + (safeBands - kSafeDropBands) * kTaughtLandingMm;
    if (tiles_ != nullptr && landedX != INT32_MIN && landedY != INT32_MIN &&
        tiles_->fluidDepth(landedX, landedY, landedBand) > 0) {
        out.softLanding = true;
        cushionMm += kSoftLandingMm;
    }

    if (out.fellMm > 0) {
        // VERIFICATION GAP (S5, still open in S6 and in #77): fall damage lands
        // on the TAVERN'S copy of the player's hit points, because that is the
        // only place hit points exist in this build -- so a body that falls off
        // a roof three streets away is hurt by the Gilded Gull's bookkeeping. It
        // is the right number in the wrong owner, and it moves when the player
        // has a body of their own rather than a room that keeps score for them.
        //
        // It still floors at the brawl floor like everything else in this build:
        // nothing kills the player yet. What changed is that the NUMBER is now
        // honest about what happened -- a three-storey fall bills 109 of a
        // hundred hit points -- so the day the player can die, the roofs will
        // kill them without this line being retuned.
        out.hurt = fallInjury(out.fellMm, cushionMm);
        if (out.hurt > 0) {
            injurePlayer(out.hurt);
        }
    }

    // A ROOF-RUN IS AN ARRIVAL, not a step. Counted the first time the body
    // gets higher than it has ever been, so a player pacing about on the lead
    // does not farm the guild's regard by walking in circles.
    if (landedBand > highestBand_) {
        highestBand_ = landedBand;
        if (landedBand >= gull::kRoofBand) {
            // Nobody looks up: a roof-run is witnessed by nobody in this build,
            // which is the whole social point of the roofs and is stated here
            // rather than implied.
            dialogue_.noteCrime(Crime::RoofRun, false);
            out.roofRun = true;
        }
    }
    // The two body verbs the questline counts by name. They are not crimes and
    // do not raise heat, so they go to the tally directly.
    if (move.ok()) {
        out.counted = move.tiles > 1 ? "leaps" : "climbs";
        dialogue_.noteTally(out.counted);
    }
    return out;
}

Tavern::StealResult Tavern::handleBale() {
    StealResult out;
    CrimeLedger& crimes = dialogue_.crimes();
    if (crimes.carryingBale()) {
        crimes.dropBale();
        // CLAMPED, and it has to be. The snug restocks to kBalesPerNight when
        // the doors open, and a player who was holding a bale across that
        // moment would otherwise put down a fourth one and index off the end
        // of the stack the next time they picked it up.
        balesInSnug_ = std::min(kBalesPerNight, balesInSnug_ + 1);
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
    // S6 CLOSES THE S5 GAP. A bale had no weight, no contents and no owner --
    // it was a flag with a name. It has a KIND and a COUNT now, both decided by
    // the boat that landed it, and what comes out of it at the threshold is
    // what a contract can want and a watchman can find.
    //
    // S7: PER BALE, not per night. The snug empties from the top of the stack,
    // so three bales can be three different goods and a player with two jobs
    // on the board can fill both from one night's hull.
    const Contraband good = baleGoods_[static_cast<std::size_t>(balesInSnug_)];
    crimes.takeBale(good, kBaleUnits);
    out.result = ServiceResult::Served;
    out.loot = kBaleUnits;
    // "N IN IT", not "N OF IT": contrabandLabel is plural for three of the five
    // goods -- SCALPS, PIECES -- and "A BALE OF SCALPS - 3 OF IT" does not
    // agree with any of them.
    out.line = "A BALE OF " + std::string(contrabandLabel(good)) + " - " +
               std::to_string(kBaleUnits) + " IN IT.";
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
// the cast
// ---------------------------------------------------------------------------

const Spell* Tavern::equippedSpell() const noexcept {
    const Grimoire& book = dialogue_.grimoire();
    if (!equippedSpellId_.empty()) {
        return book.find(equippedSpellId_);
    }
    // Nothing picked: the first known crafting is the default, so the first
    // spell a priest hands over is READY without a menu trip. Grimoire order
    // (ascending id) on both runs, so the default cannot disagree either.
    return book.spells().empty() ? nullptr : &book.spells().front();
}

bool Tavern::equipSpellAt(std::int32_t index) {
    const std::vector<Spell>& spells = dialogue_.grimoire().spells();
    if (index < 0 || index >= static_cast<std::int32_t>(spells.size())) {
        return false;
    }
    equippedSpellId_ = spells[static_cast<std::size_t>(index)].id;
    return true;
}

bool Tavern::bindSpellToSlot(std::int32_t slot, std::string_view spellId) {
    if (slot < 0 || slot >= kQuickSlotCount) {
        return false;
    }
    // Only what the grimoire actually knows. A slot holding an id the hand
    // could not equip would be a promise the number row cannot keep.
    if (dialogue_.grimoire().find(spellId) == nullptr) {
        return false;
    }
    quickSlotIds_[static_cast<std::size_t>(slot)] = std::string(spellId);
    return true;
}

bool Tavern::clearSlot(std::int32_t slot) {
    if (slot < 0 || slot >= kQuickSlotCount) {
        return false;
    }
    quickSlotIds_[static_cast<std::size_t>(slot)].clear();
    return true;
}

const Spell* Tavern::slotSpell(std::int32_t slot) const noexcept {
    if (slot < 0 || slot >= kQuickSlotCount) {
        return nullptr;
    }
    const std::string& id = quickSlotIds_[static_cast<std::size_t>(slot)];
    if (id.empty()) {
        return nullptr;
    }
    return dialogue_.grimoire().find(id);
}

bool Tavern::equipSlot(std::int32_t slot) {
    const Spell* spell = slotSpell(slot);
    if (spell == nullptr) {
        return false;
    }
    // THROUGH equipSpellAt, deliberately: the grimoire index is looked up
    // fresh (id order can have shifted since the bind) and the one equip
    // path S13 built keeps its one caller-side contract. This is the caller
    // that verb was waiting for.
    const std::vector<Spell>& spells = dialogue_.grimoire().spells();
    for (std::size_t i = 0; i < spells.size(); ++i) {
        if (spells[i].id == spell->id) {
            return equipSpellAt(static_cast<std::int32_t>(i));
        }
    }
    return false;
}

Tavern::CastResult Tavern::playerCastEquipped() {
    CastResult out;
    const Spell* spell = equippedSpell();
    if (spell == nullptr) {
        // THE COMMON STATE. The grimoire is empty at spawn; the refusal names
        // where casting starts rather than shrugging.
        out.line = "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.";
        return out;
    }
    // Materialise the default pick, so the hash and the HUD agree about what
    // the hand is holding from the first cast on.
    if (equippedSpellId_.empty()) {
        equippedSpellId_ = spell->id;
    }
    if (elapsed_ < castCoolUntil_) {
        out.line = "THE LINK IS STILL COOLING -- " +
                   std::to_string(castCoolUntil_ - elapsed_) + "S.";
        return out;
    }
    // THE S13 REFUSAL BOUNDARY, MOVED. A held-effects engine exists now
    // (heldEffects_, laid below), so a WHILE_ACTIVE tuning on the player's
    // OWN body resolves: the delta flows through effectiveAttributes() into
    // every runtime reader the fatigue build crossed. What still cannot
    // resolve is refused, out loud, BEFORE anything is spent or drawn --
    // resolving a row nothing reads while charging a cooldown and a skill-use
    // would be a lie told with a success toast, the same contract as S13.
    //
    // VERIFICATION GAP (S15): TEMPERATURE STILL HAS NO READER. The baked
    // map's temperature lane is bake-time data (content/world.hpp) and no
    // live model -- cold, wet, a fire's reach -- reads a held warmth off a
    // body, so the three authored warmth rows keep an honest refusal until a
    // temperature model lands; inventing one was ruled out of this pass.
    // Two narrower gaps share the sentence: a tuning laid on ANOTHER body
    // (sap_the_step) refuses because roster and ward actors carry no
    // attribute sheet (fatigue.hpp's player-scoped line -- giving them one
    // moves the ward's daily-life determinism), and a FORGED tuning refuses
    // because ForgeBench has no param field yet, so the row cannot say which
    // string it tunes and the cast will not guess a limb.
    for (const SpellComponent& component : spell->components) {
        switch (effectKindOf(component.effect)) {
            case EffectKind::Vitality:
                break;
            case EffectKind::Temperature:
                out.line = "A BODY CAN HOLD NOW -- BUT NOTHING IN THIS WARD "
                           "READS ITS HEAT YET.";
                return out;
            case EffectKind::Attribute:
                if (targetShapeOf(spell->target) != TargetShape::Self) {
                    out.line = "NO SHEET ON THEM TO TUNE. ONLY YOUR OWN TUNING HOLDS.";
                    return out;
                }
                if (!attributeFromRaw(component.param).has_value()) {
                    out.line = "THE BENCH NAMED NO STRING TO TUNE. "
                               "THE CRAFT IS AHEAD OF THE FORGE.";
                    return out;
                }
                break;
            case EffectKind::Unknown:
                out.line = "NOTHING HOLDS " +
                           std::string(effectKindWord(effectKindOf(component.effect))) +
                           " YET. THE CRAFT IS AHEAD OF THE HANDS.";
                return out;
        }
    }
    const TargetShape shape = targetShapeOf(spell->target);
    if (shape == TargetShape::Ranged || shape == TargetShape::Unknown) {
        // Canon gates the unbridged link behind the gift, no authored row
        // teaches it, and this build has no way to pick a body across a room
        // -- so the refusal is canon's own, not a missing feature dressed up.
        out.line = "THE LINK NEEDS A BRIDGE. AN ARM, A BLADE -- A TOUCH.";
        return out;
    }
    Actor* touched = nullptr;
    if (shape == TargetShape::Touch) {
        const Actor* found = nearestTo(playerX_, playerY_, kMeleeReach);
        if (found == nullptr) {
            out.line = "NOBODY IN REACH TO LINK.";
            return out;
        }
        touched = mutableActorById(found->id());
        if (touched == nullptr) {
            out.line = "NOBODY IN REACH TO LINK.";
            return out;
        }
        out.targetId = touched->id();
    }
    bool harms = false;
    for (const SpellComponent& component : spell->components) {
        if (component.magnitude < 0) {
            harms = true;
        }
    }
    if (touched != nullptr && harms) {
        // A SCALD IS AN ASSAULT, whatever the hand was holding: the same
        // consequences a punch carries, in the same order playerPunchNearest
        // applies them -- join the fight, classify it BEFORE the harm lands,
        // and refuse the room's own resolution when steel is out.
        if (std::find(brawlers_.begin(), brawlers_.end(), touched->id()) == brawlers_.end()) {
            brawlers_.push_back(touched->id());
            std::sort(brawlers_.begin(), brawlers_.end());
        }
        const std::vector<Fighter> fighters = currentFight();
        out.fight = classifyFight(fighters);
        if (!resolvesInWorld(out.fight)) {
            escalation_ = out.fight;
            noteEscalation(touched->id());
            reportOffence(Offence::Brawled);
            out.line = "STEEL IS OUT. THIS IS NOT THE ROOM'S FIGHT ANY MORE.";
            return out;
        }
    }
    // THE CHECK, and the ONE draw a cast costs -- taken only after every
    // refusal above has passed, so a refused press leaves the draw stream
    // exactly where it found it.
    //
    // FATIGUE BUILD, three readers, all neutral at the shipped baseline. WIT
    // is percentage points on the check ((WIT-40)/5 -- the mind's runtime
    // reader, fatigue.hpp); the FatigueTerm scales the whole chance DOWN from
    // a full pool's x1 (term/kFatigueTermFullQ8, so a full pool computes the
    // exact shipped number and an empty one x0.6 of it -- state degrading an
    // outcome, never buying one past the ceiling the clamp still holds); and
    // the body's share of working the link is paid in wind BEFORE the roll,
    // slip or open -- effort spent is spent. Same single draw as ever.
    // HELD-EFFECTS BUILD: the EFFECTIVE sheet, so a held Clear the Head is
    // felt on the very next link -- "the one crafting that feeds itself",
    // spells.json's own provenance. Read ONCE, here, before this cast lays
    // or refreshes anything: the mind that opens this link is the mind that
    // was held when the press landed, and the hold this cast itself lays
    // pays out from the next read on, cooldown included.
    const std::int32_t level = dialogue_.skills().level(spell->skill);
    const std::int32_t difficulty = spellDifficulty(*spell);
    const std::int32_t wit = effectiveAttributes().value(AttributeId::Wit);
    const std::int32_t castTerm = fatigue_.termQ8();
    fatigue_.drain(kCastFatiguePoints * kFatiguePointFine);
    const std::int32_t rawChance = kCastBasePercent + kCastPercentPerLevel * level -
                                   kCastPercentPerDifficulty * difficulty +
                                   castWitBonusPercent(wit);
    const std::int32_t chance =
        std::clamp(static_cast<std::int32_t>(
                       (static_cast<std::int64_t>(rawChance) * castTerm) /
                       kFatigueTermFullQ8),
                   kCastFloorPercent, kCastCeilPercent);
    const std::uint64_t roll = drawForPlayerAction();
    if (static_cast<std::int32_t>(roll % 100U) >= chance) {
        // WIT is the magicka-analog and RECOVERY is where it is spent -- both
        // cooldowns scale by (340-WIT)/300, exactly x1 at the base sheet.
        castCoolUntil_ = elapsed_ + witScaledCooldown(kFizzleCooldownTicks, wit);
        // NO SKILL CHARGE on a slip: linkcraft is learned by links that open
        // -- the same rule the teaching precedent set -- and a fizzle-farm
        // in a quiet corner should train nothing.
        out.line = "THE LINK SLIPS.";
        return out;
    }
    out.cast = true;
    castCoolUntil_ = elapsed_ + witScaledCooldown(spell->cooldownTicks, wit);
    dialogue_.skills().use(spell->skill);
    // HELD-EFFECTS BUILD: RECAST REFRESHES, PER CRAFTING. Every row this
    // spell laid last time comes off before its new rows go on, so one
    // crafting is one entry however often it is recast -- replaced whole,
    // clock reset, never stacked against itself. Erasing keeps insertion
    // order for everything else, so the table stays deterministic.
    bool laysHold = false;
    for (const SpellComponent& component : spell->components) {
        laysHold = laysHold || effectModeOf(component.mode) == EffectMode::WhileActive;
    }
    if (laysHold) {
        for (std::size_t i = 0; i < heldEffects_.size();) {
            if (heldEffects_[i].spellId == spell->id) {
                heldEffects_.erase(heldEffects_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    for (const SpellComponent& component : spell->components) {
        switch (effectModeOf(component.mode)) {
            case EffectMode::Instant:
                applySpellDose(out.targetId, component.magnitude);
                break;
            case EffectMode::OverTime: {
                // Every dose the cost model priced, delivered on the cadence
                // it priced them at. The first lands one period in: a trickle
                // is a trickle, not a blow with a tail.
                SpellTrickle trickle;
                trickle.targetId = out.targetId;
                trickle.magnitude = component.magnitude;
                trickle.dosesLeft =
                    std::max(1, component.durationTicks / kOverTimePeriodTicks);
                trickle.cadenceLeft = kOverTimePeriodTicks;
                trickles_.push_back(trickle);
                break;
            }
            case EffectMode::WhileActive: {
                // The vet above proved this is a SELF tuning with a named
                // string, so the row is layable as authored. Absolute expiry
                // on the room's own clock, castCoolUntil_'s exact shape.
                ActiveHold hold;
                hold.spellId = spell->id;
                hold.attribute = *attributeFromRaw(component.param);
                hold.magnitude = component.magnitude;
                hold.expiresAt = elapsed_ + component.durationTicks;
                heldEffects_.push_back(std::move(hold));
                break;
            }
            case EffectMode::Unknown:
                // Unreachable: the component vet above refused every axis and
                // shape the loader's pairing table would have refused too.
                break;
        }
    }
    if (laysHold) {
        applyHeldEffects();
    }
    if (touched != nullptr && harms) {
        touched->setActivity(Activity::Brawling);
        touched->faceToward(playerX_, playerY_);
        dialogue_.ledger().record(touched->id(), Deed::Struck);
        spreadWitness(touched->id(), Deed::Struck);
        if (talkingToId_ == touched->id()) {
            endConversation();
        }
        reportOffence(Offence::Brawled);
    }
    out.line = upperCase(spell->displayName) +
               (touched != nullptr ? " -- ON " + upperCase(touched->name()) : " -- HELD.");
    return out;
}

void Tavern::applySpellDose(std::int32_t targetId, std::int32_t magnitude) {
    if (magnitude == 0) {
        return;
    }
    if (targetId < 0) {
        // The player. The vitality floor holds here too, and it does NOT set
        // playerFloored_: no crafting on the public shelf can put a body on
        // the ground -- the floor is structural, spells.json's own words.
        if (magnitude > 0) {
            playerHp_ = std::min(playerHpMax_, playerHp_ + magnitude);
        } else {
            playerHp_ = std::max(kVitalityFloor, playerHp_ + magnitude);
        }
        return;
    }
    Actor* actor = mutableActorById(targetId);
    if (actor == nullptr || !actor->present()) {
        return;
    }
    const std::int32_t hp =
        std::clamp(actor->hp() + magnitude, kVitalityFloor, actor->hpMax());
    // Never Downed by a dose, for the same structural reason as the floor.
    actor->setHealth(hp, actor->hpMax());
}

void Tavern::tickSpellwork() {
    // Index loop with in-place erase, in insertion order: deterministic, and a
    // dose can add no new trickle so the shape is simple.
    for (std::size_t i = 0; i < trickles_.size();) {
        SpellTrickle& trickle = trickles_[i];
        bool alive = true;
        if (trickle.targetId >= 0) {
            const Actor* actor = actorById(trickle.targetId);
            if (actor == nullptr || !actor->present()) {
                // The body left the room and the bridge went with it.
                alive = false;
            }
        }
        if (alive && --trickle.cadenceLeft <= 0) {
            applySpellDose(trickle.targetId, trickle.magnitude);
            trickle.cadenceLeft = kOverTimePeriodTicks;
            --trickle.dosesLeft;
            alive = trickle.dosesLeft > 0;
        }
        if (alive) {
            ++i;
        } else {
            trickles_.erase(trickles_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
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
    // S6: the highest the player has been. It decides whether the next landing
    // is a roof-run or a lap of the lead, so it is state and not a readout.
    sink.put_int(static_cast<std::uint32_t>(highestBand_));
    // S6: tonight's cargo, which rats are gone, and where a watchman is in the
    // business of taking you. All of it decides what happens next, so all of it
    // is state the twin-run gate compares.
    for (const Contraband good : baleGoods_) {
        sink.put_byte(static_cast<std::uint32_t>(good));
    }
    sink.put_int(static_cast<std::uint32_t>(scalpedVermin_));
    // S9: how the body is carrying itself, what it is doing to the air, and
    // every lock in the building. All of it decides whether a crime is
    // witnessed or an act is possible at all, so all of it is state the
    // twin-run gate compares -- a jammed lock in particular is permanent world
    // change and a hash that could not see it would not protect it.
    stealth_.hashInto(sink);
    picking_.hashInto(sink);
    sink.put_int(static_cast<std::uint32_t>(pickingRoom_));
    sink.put_int(static_cast<std::uint32_t>(picks_));
    sink.put_int(static_cast<std::uint32_t>(openedLocks_));
    sink.put_int(static_cast<std::uint32_t>(jammedLocks_));
    sink.put_int(static_cast<std::uint32_t>(forcedLocks_));
    sink.put_byte(static_cast<std::uint32_t>(watchStance_));
    sink.put_byte(static_cast<std::uint32_t>(watchCause_));
    sink.put_int(static_cast<std::uint32_t>(watchmanId_));
    sink.put_long(static_cast<std::uint64_t>(noticedAtTick_));
    sink.put_int(static_cast<std::uint32_t>(startedAt_));
    sink.put_byte(arrestRelease_ ? 1U : 0U);
    sink.put_byte(wasInside_ ? 1U : 0U);
    sink.put_long(static_cast<std::uint64_t>(elapsed_));
    // S8: everybody who has ever put the player on the floor, and everything
    // the ward gave them for it. A rung, a founded house and a charge on the
    // roll are the most permanent state this build has; state the twin-run gate
    // cannot see is state the gate does not protect.
    sink.put_int(static_cast<std::uint32_t>(lastBlowBy_));
    sink.put_byte(defeatRelease_ ? 1U : 0U);
    // FIRST-PERSON COMBAT (S13). The held guard and its tally, the equipped
    // crafting, the cast recovery clock, and every trickle still delivering.
    // All of it decides what the next blow or the next second does, so all of
    // it is state the twin-run gate compares. DELIBERATE STRUCTURE CHANGE to
    // the live Tavern hash, stated here rather than discovered: the pinned
    // codec goldens (test_world_hash.cpp) hash fixed byte specs, not this
    // struct, so nothing is re-derived -- the twin-run and cross-toolchain
    // gates compare live runs of THIS shape against itself.
    sink.put_byte(playerBlocking_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(blowsBlocked_));
    sink.put_int(static_cast<std::uint32_t>(equippedSpellId_.size()));
    for (const char character : equippedSpellId_) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
    }
    sink.put_long(static_cast<std::uint64_t>(castCoolUntil_));
    sink.put_int(static_cast<std::uint32_t>(trickles_.size()));
    for (const SpellTrickle& trickle : trickles_) {
        sink.put_int(static_cast<std::uint32_t>(trickle.targetId));
        sink.put_int(static_cast<std::uint32_t>(trickle.magnitude));
        sink.put_int(static_cast<std::uint32_t>(trickle.dosesLeft));
        sink.put_int(static_cast<std::uint32_t>(trickle.cadenceLeft));
    }
    // SPELLS BUILD: the quick bar's ten slot contents, right beside the
    // equipped crafting they exist to re-point. Which crafting a number key
    // readies decides what the next cast does, so it is state the twin-run
    // gate compares. DELIBERATE STRUCTURE CHANGE to the live Tavern hash,
    // exactly the S13 shape above: the pinned codec goldens hash fixed byte
    // specs, not this struct, and the live gates compare THIS shape against
    // itself.
    for (const std::string& slotId : quickSlotIds_) {
        sink.put_int(static_cast<std::uint32_t>(slotId.size()));
        for (const char character : slotId) {
            sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
        }
    }
    // FATIGUE BUILD: the sheet and the wind. The four attributes decide what
    // every punch, gait, climb cost and cast is worth from this run on, and
    // the pool (current, max, winded) decides whether the next sprint step is
    // even taken -- state the twin-run gate compares or does not protect.
    // DELIBERATE STRUCTURE CHANGE to the live Tavern hash, the S13 shape: the
    // pinned codec goldens (test_world_hash.cpp) hash fixed byte specs, not
    // this struct, the baked-map world hash never reaches this code, and the
    // live twin-run/cross-toolchain gates compare THIS shape against itself.
    playerAttributes_.hashInto(sink);
    fatigue_.hashInto(sink);
    // HELD-EFFECTS BUILD: every live hold -- which crafting laid it, which
    // string it tunes, by how much, and the tick it lapses on. A live tuning
    // is read by every attribute reader in the game, so two runs that
    // disagreed about one would be two different games. DELIBERATE STRUCTURE
    // CHANGE to the live Tavern hash, the S13 shape: the pinned codec goldens
    // (test_world_hash.cpp) hash fixed byte specs, not this struct, the
    // baked-map world hash never reaches this code, and the live twin-run and
    // cross-toolchain gates compare THIS shape against itself.
    sink.put_int(static_cast<std::uint32_t>(heldEffects_.size()));
    for (const ActiveHold& hold : heldEffects_) {
        sink.put_int(static_cast<std::uint32_t>(hold.spellId.size()));
        for (const char character : hold.spellId) {
            sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
        }
        sink.put_byte(static_cast<std::uint32_t>(hold.attribute));
        sink.put_int(static_cast<std::uint32_t>(hold.magnitude));
        sink.put_long(static_cast<std::uint64_t>(hold.expiresAt));
    }
    nemesis_.hashInto(sink);
    dialogue_.hashInto(sink);
}

}  // namespace granadad::sim
