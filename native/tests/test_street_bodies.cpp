// THE STREET HAS A BODY TO HIT.
//
// STREET SENSES leg (b). The gap analysis' first finding: "a swing on the
// Tarwalk hits nobody because the sightline raycast walks the 17-body tavern
// roster only; WardActor carries no hp, no downed-by-a-blow, no
// dead-by-violence state." This leg gives the district's people the Gull's
// own sheet and puts them on the Gull's own ray, through the Gull's own
// strike(), classifyFight, floor and murder law -- one rule, two rosters, and
// nobody promoted onto the roster he is not on.
//
// WHAT THESE CASES CLAIM, and how each is kept honest:
//
//   * the street's ray IS the Gull's ray: WardPopulation::sightlineTarget
//     agrees with the spec's along/perp projection COMPUTED HERE from the
//     same yaw, so the sim is checked against the rule and not against itself;
//   * a struck docker goes down on the brawl floor and gets up at a quarter,
//     off the board while he lies there and on it again when he stands;
//   * a struck serf routs (the leg (a) plan, deeper), a struck sailor swings
//     back (the Brawl policy, one draw per swing on his own key), and the
//     Watch does neither;
//   * a landed street blow scatters the crowd in sight (the leg (a) alarm,
//     from the leg (b) cause);
//   * a blow thrown at the player lands on his sheet through the Gull's own
//     player-side rules (the guard softens it, the hard band is the roll's);
//   * a street kill is MURDER with the witnesses counted, through the client's
//     own attack path -- the real verbs, the real ray, the real ledger;
//   * and the population twin-runs byte-identical under all of it.
//
// EVERY CASE TAKES A privateWard() or its own Session: every one mutates.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/ward_voice.hpp"
#include "granadad/sim/world_hash.hpp"
#include "support/ward_fixture.hpp"

using namespace granadad;
using granadad::testfix::privateWard;
using granadad::testfix::sharedTiles;
using granadad::testfix::WardRun;

namespace {

/// Four in the afternoon: the day trades on the quay, the hour the gate's own
/// violence leg and the panic frames use.
constexpr std::int32_t kHour = 16;
constexpr std::int64_t kSettleTicks = 30;

std::uint64_t digestOf(const sim::WardPopulation& people) {
    sim::HashSink sink(0x5354524545544244ull);  // "STREETBD"
    people.hash_into(sink);
    return sink.finished();
}

/// The first standing person of `type` (or any non-Watch person when `any`)
/// on walking ground with a free standable orthogonal neighbour to stand the
/// player on. Returns the actor id and the stand tile; -1 when none.
struct Stand {
    std::int32_t id = -1;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
};

Stand findStand(const sim::WardPopulation& people, sim::WardType type, bool any) {
    static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
    static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
    for (const sim::WardActor& actor : people.actors()) {
        if (!actor.visible() || !sim::isPerson(actor.type) ||
            actor.type == sim::WardType::MilitiaWatch) {
            continue;
        }
        if (!any && actor.type != type) {
            continue;
        }
        if (!people.onWalkingGround(actor.x, actor.y, actor.band)) {
            continue;
        }
        // Clear of the Gilded Gull, runStreetLine's own exclusion: a stand at
        // the taproom's wall could put a patron on the room's ray through it,
        // and the pick would go to the roster instead of the street.
        if (actor.x >= sim::gull::kFootprintX0 - 4 && actor.x <= sim::gull::kFootprintX1 + 4 &&
            actor.y >= sim::gull::kFootprintY0 - 6 && actor.y <= sim::gull::kFootprintY1 + 4) {
            continue;
        }
        for (int n = 0; n < 4; ++n) {
            const std::int32_t sx = actor.x + dx[n];
            const std::int32_t sy = actor.y + dy[n];
            if (!sharedTiles().standable(sx, sy, actor.band)) {
                continue;
            }
            if (people.nearestTo(sx, sy, actor.band, 0) != nullptr) {
                continue;  // somebody is standing there
            }
            return Stand{actor.id, sx, sy, actor.band};
        }
    }
    return Stand{};
}

/// A yaw that looks from (fromX, fromY) straight at (toX, toY): the four-point
/// facing, which is exact for an orthogonal neighbour.
sim::Angle lookAt(std::int32_t fromX, std::int32_t fromY, std::int32_t toX, std::int32_t toY) {
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    if (std::abs(dx) >= std::abs(dy)) {
        return dx > 0 ? sim::kFacingEast : sim::kFacingWest;
    }
    return dy > 0 ? sim::kFacingSouth : sim::kFacingNorth;
}

/// A landed, not-hard, not-crowned roll for strike(): the low three bits set
/// (no whiff), the fatigue band (bits 32-37) clear, the variance ((roll>>3)%3)
/// 0, and the NPC hard band (bits 48-55) at 0xFF, well past kNpcHardBand256
/// -- so a street blow read through Tavern::takeStreetBlow is a plain swing.
/// Used where a case wants a blow that definitely lands for its base damage.
constexpr std::uint64_t kLandingRoll = 0x00FF000000000001ull;
static_assert(((kLandingRoll >> sim::kNpcHardRollShift) & 0xFFU) >= sim::kNpcHardBand256,
              "the landing roll must not read as a hard swing");
static_assert((kLandingRoll & 7U) != 0, "the landing roll must not whiff");
static_assert(((kLandingRoll >> 3) % 3) == 0, "the landing roll must carry no variance");

/// One fist through the population's own door, the gate driver's shape:
/// strike() on the sheet, then applyStreetBlow. Returns the blow.
sim::Blow fist(sim::WardPopulation& people, std::int32_t id, std::uint64_t roll, bool lethal) {
    const sim::WardActor& actor = *people.byId(id);
    sim::Fighter sheet;
    sheet.actorId = id;
    sheet.weapon = sim::Weapon::Fists;
    sheet.intent = sim::Intent::Subdue;
    sheet.hp = actor.hp;
    sheet.hpMax = sim::kActorHealth;
    const sim::Blow blow = sim::strike(sim::Weapon::Fists, sheet, roll);
    (void)people.applyStreetBlow(id, sheet.hp, blow, lethal);
    return blow;
}

render::SessionConfig streetAt(int hour, std::int32_t x, std::int32_t y, std::int32_t band) {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = band;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

// ---------------------------------------------------------------------------
// the same ray
// ---------------------------------------------------------------------------

TEST_CASE("the street's ray is the Gull's ray: the same integer projection") {
    // A player stood a tile off a docker, looking straight at him, and the
    // spec's own projection (COMBAT-ACTION-SPEC.md 2.1) computed here from the
    // same numbers: the target the sim names must be the one the rule names,
    // at the along the rule gives.
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand stand = findStand(people, sim::WardType::Serf, true);
    REQUIRE(stand.id >= 0);
    const sim::WardActor& body = *people.byId(stand.id);

    const sim::Angle yaw = lookAt(stand.x, stand.y, body.x, body.y);
    const std::int32_t px = sim::q8_tile_centre(stand.x);
    const std::int32_t py = sim::q8_tile_centre(stand.y);

    // The rule, written a second time.
    const std::int64_t fx = sim::forward_x_q16(yaw);
    const std::int64_t fy = sim::forward_y_q16(yaw);
    const sim::WardActor* expected = nullptr;
    std::int64_t expectedAlong = static_cast<std::int64_t>(sim::kMeleeReach) + 1;
    for (const sim::WardActor& actor : people.actors()) {
        if (!actor.visible() || !sim::isPerson(actor.type) || actor.band != stand.band) {
            continue;
        }
        const std::int64_t dx = static_cast<std::int64_t>(sim::q8_tile_centre(actor.x)) - px;
        const std::int64_t dy = static_cast<std::int64_t>(sim::q8_tile_centre(actor.y)) - py;
        const std::int64_t along = (fx * dx + fy * dy) >> 16;
        if (along <= 0 || along > sim::kMeleeReach) {
            continue;
        }
        const std::int64_t perp = (-fy * dx + fx * dy) >> 16;
        if (perp > sim::kBodyHalfWidth || perp < -sim::kBodyHalfWidth) {
            continue;
        }
        if (along < expectedAlong) {
            expectedAlong = along;
            expected = &actor;
        }
    }
    REQUIRE(expected != nullptr);

    std::int64_t along = -1;
    const sim::WardActor* found = people.sightlineTarget(px, py, stand.band, yaw, &along);
    REQUIRE(found != nullptr);
    CHECK(found->id == expected->id);
    CHECK(along == expectedAlong);
    // An orthogonal neighbour is one tile down the ray, inside reach (a
    // cardinal facing is exact in the Q16 table, but the claim is the
    // agreement above, not the table).
    CHECK(along >= sim::kSubOne - 1);
    CHECK(along <= sim::kSubOne);
    // Turned away, the line is empty.
    std::int64_t none = 0;
    const sim::Angle away = static_cast<sim::Angle>(yaw + 2 * sim::kTurnQuarter);
    CHECK(people.sightlineTarget(px, py, stand.band, away, &none) == nullptr);
    CHECK(none == -1);
    // And a floored body is off the ray: put him down, ask again.
    (void)fist(people, found->id, kLandingRoll, false);
    while (people.byId(found->id)->visible()) {
        (void)fist(people, found->id, kLandingRoll, false);
    }
    const sim::WardActor* after = people.sightlineTarget(px, py, stand.band, yaw, nullptr);
    CHECK((after == nullptr || after->id != found->id));
}

// ---------------------------------------------------------------------------
// down, and up
// ---------------------------------------------------------------------------

TEST_CASE("a struck docker goes down and gets up") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand stand = findStand(people, sim::WardType::Serf, false);
    REQUIRE_MESSAGE(stand.id >= 0, "no serf on walking ground at sixteen");
    const std::int32_t id = stand.id;
    people.setPlayer(stand.x, stand.y, stand.band);

    CHECK(people.byId(id)->hp == sim::kActorHealth);
    CHECK_FALSE(people.byId(id)->floored());
    // Fists until he drops: 3-5 a blow against 24, brawl rules, nobody dies.
    int blows = 0;
    while (people.byId(id)->visible() && blows < 20) {
        (void)fist(people, id, kLandingRoll, false);
        ++blows;
    }
    const sim::WardActor& body = *people.byId(id);
    REQUIRE(body.downedUntil >= 0);
    CHECK(blows >= 5);
    CHECK(body.hp == 0);
    CHECK(body.floored());
    CHECK_FALSE(body.visible());
    CHECK_FALSE(body.slain);
    CHECK_FALSE(body.dead);
    CHECK(body.downedUntil == people.currentTick() + sim::kStreetFloorSeconds);
    // Off the board: not a target, not a witness, not somebody you can talk to.
    CHECK(people.nearestTo(body.x, body.y, body.band, 0) == nullptr);
    CHECK(people.census().downed >= 1);

    // He lies his six seconds...
    own->run(sim::kStreetFloorSeconds - 1);
    CHECK_FALSE(people.byId(id)->visible());
    // ...and STANDS, at a quarter, on the board again.
    own->run(2);
    CHECK(people.byId(id)->visible());
    CHECK(people.byId(id)->downedUntil < 0);
    CHECK(people.byId(id)->hp == sim::kStreetStandHp);
    CHECK(people.byId(id)->bloodied());
    CHECK_FALSE(people.byId(id)->floored());
    CHECK(people.census().downed == 0);
    // And he is running: struck, he routs through the leg (a) plan.
    CHECK(people.byId(id)->need(sim::Need::Safety) < sim::kNeedCritical);
    CHECK(people.byId(id)->policy == sim::WardPolicy::Flee);
}

TEST_CASE("a struck serf routs deeper than a bystander, a struck sailor swings back") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();

    // THE SERF: one landed fist, standing after it -- Safety driven to the
    // struck floor (deeper than the Blow floor a bystander gets), and Flee.
    const Stand serf = findStand(people, sim::WardType::Serf, false);
    REQUIRE(serf.id >= 0);
    people.setPlayer(serf.x, serf.y, serf.band);
    const sim::Blow blow = fist(people, serf.id, kLandingRoll, false);
    REQUIRE(blow.landed);
    REQUIRE_FALSE(blow.downed);
    CHECK(people.byId(serf.id)->hp == sim::kActorHealth - blow.damage);
    CHECK(people.byId(serf.id)->need(sim::Need::Safety) == sim::kStruckPanicFloor);
    CHECK(people.byId(serf.id)->fightUntil == 0);
    own->run(1);
    CHECK(people.byId(serf.id)->policy == sim::WardPolicy::Flee);

    // THE SAILOR: the same fist, and he FIGHTS BACK -- the clock set, the
    // Brawl policy the next tick, and ONE blow in the mailbox with his own id
    // on it, drawn on his own key (the player is on the tile beside him).
    const Stand sailor = findStand(people, sim::WardType::Sailor, false);
    REQUIRE_MESSAGE(sailor.id >= 0, "no sailor on walking ground at sixteen");
    people.setPlayer(sailor.x, sailor.y, sailor.band);
    const sim::Blow onSailor = fist(people, sailor.id, kLandingRoll, false);
    REQUIRE(onSailor.landed);
    CHECK(people.byId(sailor.id)->fightUntil == people.currentTick() + sim::kStreetFightSeconds);
    const std::int32_t seqBefore = people.byId(sailor.id)->swingSeq;
    own->run(1);
    CHECK(people.byId(sailor.id)->policy == sim::WardPolicy::Brawl);
    CHECK(people.byId(sailor.id)->swingSeq == seqBefore + 1);
    const std::vector<sim::StreetBlow> thrown = people.takeStreetBlows();
    bool swung = false;
    for (const sim::StreetBlow& b : thrown) {
        if (b.attackerId == sailor.id) {
            swung = true;
        }
    }
    CHECK(swung);
    // Read-and-clear: the mailbox is empty until the next tick.
    CHECK(people.takeStreetBlows().empty());
    // And when his clock runs out he is done: no Brawl, and -- frightened by
    // the blow he took -- he breaks off through the flee plan.
    own->run(sim::kStreetFightSeconds + 1);
    CHECK(people.byId(sailor.id)->policy != sim::WardPolicy::Brawl);
}

TEST_CASE("a landed street blow scatters the crowd in sight") {
    // The leg (a) alarm, from the leg (b) cause: a fist on a docker in front
    // of the fish market frightens everybody who could see it at the Blow
    // radius, by the three clauses -- computed here independently -- and
    // nobody who could not.
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand stand = findStand(people, sim::WardType::Serf, false);
    REQUIRE(stand.id >= 0);
    const sim::WardActor& victim = *people.byId(stand.id);
    const std::int32_t vx = victim.x;
    const std::int32_t vy = victim.y;
    const std::int32_t vband = victim.band;
    people.setPlayer(stand.x, stand.y, stand.band);

    std::vector<std::int32_t> seen;
    std::vector<std::int32_t> unseen;
    for (const sim::WardActor& actor : people.actors()) {
        if (actor.id == stand.id || !actor.visible() || !sim::isPerson(actor.type) ||
            actor.type == sim::WardType::MilitiaWatch) {
            continue;
        }
        const bool sameBand = actor.band == vband;
        const bool inRange =
            std::max(std::abs(actor.x - vx), std::abs(actor.y - vy)) <= sim::kAlarmRadiusBlow;
        const bool los = (actor.x == vx && actor.y == vy) ||
                         sharedTiles().lineOfSight(actor.x, actor.y, vx, vy, vband);
        if (sameBand && inRange && los) {
            seen.push_back(actor.id);
        } else {
            unseen.push_back(actor.id);
        }
        REQUIRE(actor.need(sim::Need::Safety) >= sim::kNeedCritical);
    }
    REQUIRE(seen.size() >= 2);

    const sim::Blow blow = fist(people, stand.id, kLandingRoll, false);
    REQUIRE(blow.landed);
    for (const std::int32_t id : seen) {
        INFO("saw it: ", id);
        CHECK(people.byId(id)->need(sim::Need::Safety) == sim::kPanicSafetyBlow);
    }
    for (const std::int32_t id : unseen) {
        INFO("did not: ", id);
        CHECK(people.byId(id)->need(sim::Need::Safety) >= sim::kNeedCritical);
    }
    // The man who took it is deeper than the men who saw it.
    CHECK(people.byId(stand.id)->need(sim::Need::Safety) < sim::kPanicSafetyBlow);
}

// ---------------------------------------------------------------------------
// the street swings back, on the player's own sheet
// ---------------------------------------------------------------------------

TEST_CASE("a blow thrown at the player lands through the Gull's own player-side rules") {
    render::Session session(streetAt(kHour, sim::docks::kSpawnTileX, sim::docks::kSpawnTileY,
                                     sim::docks::kSpawnBand));
    sim::Tavern& tavern = session.tavern();
    sim::Fighter sailor;
    sailor.actorId = sim::kWardSpeakerIdBase + 7;
    sailor.weapon = sim::Weapon::Fists;
    sailor.intent = sim::Intent::Subdue;
    sailor.hp = sim::kActorHealth;
    sailor.hpMax = sim::kActorHealth;

    // Guard down: a landing, not-hard fist for its base three.
    const std::int32_t before = tavern.playerHp();
    REQUIRE(tavern.takeStreetBlow(sailor, kLandingRoll));
    CHECK(tavern.playerHp() == before - sim::baseDamage(sim::Weapon::Fists));
    // A blow caught puts the hands up (raise rule 3).
    CHECK(tavern.playerHandsUp());
    // A whiff (the low three bits clear) lands nothing and costs nothing.
    const std::int32_t mid = tavern.playerHp();
    CHECK_FALSE(tavern.takeStreetBlow(sailor, 0xFF00000000000000ull));
    CHECK(tavern.playerHp() == mid);
    // Guard up: blockedDamage at shieldwall zero keeps sixty percent, floor one
    // -- three becomes one -- and the catch is counted.
    tavern.setPlayerBlocking(true);
    const std::int32_t blocked = tavern.blowsBlocked();
    REQUIRE(tavern.takeStreetBlow(sailor, kLandingRoll));
    CHECK(tavern.playerHp() == mid - sim::blockedDamage(sim::baseDamage(sim::Weapon::Fists), 0));
    CHECK(tavern.blowsBlocked() == blocked + 1);
    tavern.setPlayerBlocking(false);
    // The HARD band is the roll's own bits 48-55 (kNpcHardBand256): a roll
    // under the band doubles the rolled damage.
    const std::int32_t low = tavern.playerHp();
    REQUIRE(tavern.takeStreetBlow(sailor, 0x0000000000000001ull));
    CHECK(tavern.playerHp() == low - 2 * sim::baseDamage(sim::Weapon::Fists));
    // A brawl never kills the player: fists on fists floors at
    // kPlayerBrawlFloor and the defeat seam takes it, with no roster winner.
    // The player's sheet is the chargen's, not the docker's twenty-four, so
    // the loop runs to the floor and is bounded only against a broken floor.
    for (int n = 0; n < 200 && !tavern.playerFloored(); ++n) {
        (void)tavern.takeStreetBlow(sailor, kLandingRoll);
    }
    CHECK(tavern.playerFloored());
    CHECK(tavern.playerHp() == sim::kPlayerBrawlFloor);
    CHECK(tavern.takeDefeatRelease());
    CHECK(tavern.nemesis().worst() == nullptr);
}

// ---------------------------------------------------------------------------
// murder, through the client's own attack path
// ---------------------------------------------------------------------------

TEST_CASE("a street kill is murder with the witnesses counted") {
    // The real verbs: a session on the Tarwalk at sixteen, stood a tile off a
    // docker and looking at him, steel in hand meaning it (Lethal by B1 and
    // B2), and the Attack key down and up until the body is a corpse. Then the
    // ledger: WANTED FOR BLOOD, the witnesses the street counted before the
    // body dropped, and the murder heat.
    const std::unique_ptr<WardRun> twin = privateWard(kHour);
    twin->run(kSettleTicks);
    const Stand stand = findStand(twin->people(), sim::WardType::Serf, false);
    REQUIRE(stand.id >= 0);

    render::Session session(streetAt(kHour, stand.x, stand.y, stand.band));
    // The session's ward is the same bake (same seed, same hour); settle it the
    // same thirty ticks so the docker is where the fixture found him.
    session.stepMany(sim::MoveInput{}, static_cast<int>(kSettleTicks) * sim::kStepsPerSecond);
    const std::int32_t id = stand.id;
    REQUIRE(session.people().byId(id)->visible());

    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    REQUIRE_FALSE(session.tavern().dialogue().crimes().murderer());
    const std::int32_t heatBefore = session.tavern().dialogue().crimes().heat();

    std::int32_t lastWitnesses = -1;
    bool killed = false;
    for (int tap = 0; tap < 12 && !killed; ++tap) {
        const sim::WardActor& body = *session.people().byId(id);
        if (!body.visible()) {
            break;  // floored by a crowned blow, or gone
        }
        // Re-stand beside him and look at him: he may have moved a tile
        // between taps. Placement is the harness's; the swing is the game's.
        std::int32_t sx = body.x + 1;
        std::int32_t sy = body.y;
        static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
        static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
        bool stood = false;
        for (int n = 0; n < 4 && !stood; ++n) {
            sx = body.x + dx[n];
            sy = body.y + dy[n];
            if (sharedTiles().standable(sx, sy, body.band) &&
                session.people().nearestTo(sx, sy, body.band, 0) == nullptr) {
                stood = true;
            }
        }
        REQUIRE(stood);
        session.placeBodyAt(sx, sy, body.band);
        session.body().setYaw(lookAt(sx, sy, body.x, body.y));
        session.stepMany(sim::MoveInput{}, 1);
        lastWitnesses = session.people().witnessesInSight(body.x, body.y, body.band,
                                                          sim::kWatchSightTiles, id);
        // A HARD swing: hold past the threshold, release. Edged doubled is
        // 22-26 against 24.
        session.attackDown();
        session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
        session.attackUp();
        killed = session.people().byId(id)->slain;
        session.stepMany(sim::MoveInput{}, sim::kHardSwingRecoverySteps + 1);
    }
    REQUIRE_MESSAGE(killed, "twelve hard swings with steel did not kill a docker");

    const sim::WardActor& corpse = *session.people().byId(id);
    CHECK(corpse.slain);
    CHECK(corpse.dead);
    CHECK(corpse.floored());
    CHECK_FALSE(corpse.visible());
    CHECK(corpse.policy == sim::WardPolicy::Dead);
    CHECK(session.people().census().slain == 1);
    CHECK(session.people().census().starved == 0);

    const sim::CrimeLedger& crimes = session.tavern().dialogue().crimes();
    REQUIRE(lastWitnesses >= 0);
    if (lastWitnesses > 0) {
        // Seen: WANTED FOR BLOOD, N SAW IT on the record, instant paper.
        CHECK(crimes.murderer());
        CHECK(crimes.slewWitnesses() == lastWitnesses);
        CHECK(crimes.heat() >= heatBefore + sim::kMurderHeat);
        CHECK(crimes.warrant());
    } else {
        // Nobody could see it: the ward heard nothing.
        CHECK_FALSE(crimes.murderer());
        CHECK(crimes.heat() == heatBefore);
    }
    // The tavern roster was never touched: nobody in the Gull died.
    for (const sim::Actor& actor : session.tavern().actors()) {
        CHECK(actor.activity() != sim::Activity::Dead);
    }
    // And the crowd that saw it is frightened (Kill severity at the tile, the
    // widest and longest): every non-Watch person who could see the corpse's
    // tile at the Kill radius is under the gate. Checked on Safety, which the
    // alarm wrote at once, rather than on the policy the next tick picks.
    std::int32_t frightened = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (actor.id == id || !actor.visible() || !sim::isPerson(actor.type) ||
            actor.type == sim::WardType::MilitiaWatch || actor.band != corpse.band) {
            continue;
        }
        if (std::max(std::abs(actor.x - corpse.x), std::abs(actor.y - corpse.y)) >
            sim::kAlarmRadiusKill) {
            continue;
        }
        if ((actor.x != corpse.x || actor.y != corpse.y) &&
            !sharedTiles().lineOfSight(actor.x, actor.y, corpse.x, corpse.y, corpse.band)) {
            continue;
        }
        INFO("in sight of the killing: ", actor.id);
        CHECK(actor.need(sim::Need::Safety) < sim::kNeedCritical);
        ++frightened;
    }
    CHECK(frightened >= 1);
}

// ---------------------------------------------------------------------------
// the twin run
// ---------------------------------------------------------------------------

TEST_CASE("the population twin-run stays byte-identical run-to-run under street blows") {
    const std::unique_ptr<WardRun> a = privateWard(kHour);
    const std::unique_ptr<WardRun> b = privateWard(kHour);
    const std::unique_ptr<WardRun> quiet = privateWard(kHour);
    a->run(kSettleTicks);
    b->run(kSettleTicks);
    quiet->run(kSettleTicks);
    REQUIRE(digestOf(a->people()) == digestOf(b->people()));

    const Stand serf = findStand(a->people(), sim::WardType::Serf, false);
    const Stand sailor = findStand(a->people(), sim::WardType::Sailor, false);
    REQUIRE(serf.id >= 0);
    REQUIRE(sailor.id >= 0);
    for (WardRun* run : {a.get(), b.get()}) {
        sim::WardPopulation& people = run->people();
        people.setPlayer(serf.x, serf.y, serf.band);
        for (int n = 0; n < 8; ++n) {
            const std::uint64_t roll = kLandingRoll ^ (static_cast<std::uint64_t>(n) << 8);
            (void)fist(people, serf.id, roll, false);
        }
        people.setPlayer(sailor.x, sailor.y, sailor.band);
        (void)fist(people, sailor.id, kLandingRoll, false);
    }
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));

    // Through the floor, the stand-up, the fight and its end.
    a->run(40);
    b->run(40);
    quiet->run(40);
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(a->people().reportLine() == b->people().reportLine());
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));
    CHECK(a->people().byId(serf.id)->visible());
    CHECK(a->people().byId(serf.id)->hp == sim::kStreetStandHp);
}
