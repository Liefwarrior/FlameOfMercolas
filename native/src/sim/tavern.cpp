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
     hourOfDay(7), hourOfDay(1), Activity::Working, 300},
    {"Gerta Saltcotte", "the Fair-Weight", ActorRole::Bartender, gull::kBartenderX,
     gull::kBartenderY, hourOfDay(10, 30), hourOfDay(2, 30), Activity::Working, 80},
    {"Ox Gullbane", "Slab-Fist", ActorRole::Bouncer, 153, 67, hourOfDay(11), hourOfDay(20),
     Activity::Watching, 20},
    {"Kled Tarbeck", "the Patient", ActorRole::Bouncer, 154, 75, hourOfDay(18), hourOfDay(3),
     Activity::Watching, 20},
    {"Father Maell", "of the Mission", ActorRole::PriestOfTheFlame, 149, 74, hourOfDay(19),
     hourOfDay(21, 30), Activity::Drinking, 8},
    {"Wisp", "Low-Tide", ActorRole::SkyrunnerContact, 158, 68, hourOfDay(22), hourOfDay(3),
     Activity::Drinking, 60},
}};

/// The patrons. Two thin hours at midday when the lunch trade is in, and then
/// the whole crowd from the dusk pay-out until the small hours -- the wage loop
/// DOCKS-GAZETTEER §4 describes: dawn muster, cargo work, dusk pay-out, tavern.
constexpr std::array<RosterEntry, 8> kPatrons = {{
    {"Bram Marrow", "the Steady", ActorRole::Patron, 149, 69, hourOfDay(12), hourOfDay(14),
     Activity::Drinking, kPatronPurse},
    {"Marta Coldquay", "Crane-Eye", ActorRole::Patron, 150, 69, hourOfDay(12), hourOfDay(14),
     Activity::Drinking, kPatronPurse},
    {"Tarn Wrenhale", "Two-Loads", ActorRole::Patron, 151, 70, hourOfDay(18), hourOfDay(1),
     Activity::Drinking, kPatronPurse},
    {"Sella Brinewall", "the Quiet", ActorRole::Patron, 154, 70, hourOfDay(18), hourOfDay(1),
     Activity::Drinking, kPatronPurse},
    {"Wick Hempson", "Rope-burned", ActorRole::Patron, 150, 70, hourOfDay(19), hourOfDay(2),
     Activity::Drinking, kPatronPurse},
    {"Hobbin Mastwright", "Salt-cracked", ActorRole::Patron, 150, 74, hourOfDay(19),
     hourOfDay(2), Activity::Drinking, kPatronPurse},
    {"Edda Pierpont", "the Broad", ActorRole::Patron, 156, 73, hourOfDay(20), hourOfDay(2),
     Activity::Drinking, kPatronPurse},
    {"Colm Tarbeck", "the Willing", ActorRole::Patron, 148, 72, hourOfDay(20), hourOfDay(1),
     Activity::Drinking, kPatronPurse},
}};

}  // namespace

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
      spellbook_(Spellbook::load(contentDir)),
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

void Tavern::setTimeOfDay(std::int32_t secondOfDay) noexcept {
    timeOfDay_ = ((secondOfDay % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
}

void Tavern::skipTo(std::int32_t secondOfDay) {
    setTimeOfDay(secondOfDay);
    // A night has gone by in one step, so the cellar has been restocked and the
    // room re-seated. Nothing in between is simulated and this is the one place
    // that is true -- see the header on what the tavern does not model.
    drinkStock_ = kOpeningStock;
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
    if (timeOfDay_ == gull::kOpensAt) {
        drinkStock_ = kOpeningStock;
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
            if (tick_ - warnedAtTick_ >= kGraceSeconds) {
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
        reportOffence(Offence::Brawled);
        return result;
    }

    Fighter victim = target->asFighter();
    result.blow = strike(playerWeapon_, victim, drawForPlayerAction());
    target->setHealth(victim.hp, victim.hpMax);
    target->setActivity(result.blow.downed ? Activity::Downed : Activity::Brawling);
    target->faceToward(playerX_, playerY_);
    reportOffence(Offence::Brawled);
    return result;
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
    if (drinkStock_ <= 0) {
        return ServiceResult::OutOfStock;
    }
    if (playerCoin_ < kDrinkPrice) {
        return ServiceResult::NoCoin;
    }
    playerCoin_ -= kDrinkPrice;
    bartender->giveCoin(kDrinkPrice);
    --drinkStock_;
    ++playerDrinks_;
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
    if (rentedRoom_ >= 0) {
        return ServiceResult::Served;  // already have one; the man does not charge twice
    }
    if (playerCoin_ < kRoomPrice) {
        return ServiceResult::NoCoin;
    }
    playerCoin_ -= kRoomPrice;
    innkeeper->giveCoin(kRoomPrice);
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
            result.line = spellbook_.loaded()
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
    return spellbook_.teachableAt("linkcraft", linkcraftLevel);
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
    sink.put_int(static_cast<std::uint32_t>(brawlers_.size()));
    for (const std::int32_t id : brawlers_) {
        sink.put_int(static_cast<std::uint32_t>(id));
    }
}

}  // namespace granadad::sim
