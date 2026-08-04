// The Gilded Gull, asserted against the map it stands on and the rules it runs.
//
// Three kinds of case in here and they are deliberately separate:
//
//   MAP      every coordinate the tavern hardcodes, re-read from
//            content/maps/baked/docks_surface.trojsav. If the district is
//            re-baked and the bar moves a tile, this goes red here rather than
//            putting a bartender inside a wall at run time.
//   RULE     classifyFight, on its own, as a table. The brawl/lethal line is
//            the sprint's one binding design ruling and it is checked without a
//            world anywhere near it.
//   ROOM     the whole thing running: a real PhasedEngine, a real PlayerBody
//            on the real Docks, sixty movement steps to the tick, and a
//            troublemaker who ends up in the street.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

const TileQuery& docksTiles() {
    static const TileQuery tiles(docksWorld());
    return tiles;
}

/// A tavern, an engine, a body, and the loop that drives all three. This is the
/// same shape the client's loop has: sixty movement steps to one simulated
/// second, the tavern told where the body is on every one of them, and the
/// shove it asks for applied to the body that owns its own collision.
class Room {
public:
    explicit Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
                  std::uint64_t seed = 0x4752414E41444144ull)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(seed, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand,
                                             kFacingSouth)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, seed,
                                               content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] PlayerBody& body() noexcept { return *body_; }
    [[nodiscard]] const TileQuery& tiles() const noexcept { return *tiles_; }
    [[nodiscard]] PhasedEngine& engine() noexcept { return *engine_; }

    /// Runs `seconds` of simulated time with the player standing still.
    void run(int seconds) { runWith(seconds, MoveInput{}); }

    /// Runs a second at a time until `done` or `limitSeconds` have passed.
    /// Returns how many seconds it took, or -1 if it never happened -- so a
    /// case can assert BOTH that a thing occurred and that it did not take all
    /// night, without hard-coding how long the walk across a taproom is.
    template <typename Predicate>
    int runUntil(Predicate done, int limitSeconds) {
        for (int second = 0; second < limitSeconds; ++second) {
            if (done()) {
                return second;
            }
            run(1);
        }
        return done() ? limitSeconds : -1;
    }

    void runWith(int seconds, const MoveInput& input) {
        for (int second = 0; second < seconds; ++second) {
            for (int step = 0; step < kStepsPerSecond; ++step) {
                tavern_->setPlayer(body_->x(), body_->y(), body_->band());
                tavern_->stepMovement();
                const std::int32_t shoveX = tavern_->takePlayerShoveX();
                const std::int32_t shoveY = tavern_->takePlayerShoveY();
                if (shoveX != 0 || shoveY != 0) {
                    body_->push(shoveX, shoveY);
                }
                body_->step(input);
            }
            tavern_->setPlayer(body_->x(), body_->y(), body_->band());
            engine_->tick();
        }
    }

private:
    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
};

}  // namespace

// ===========================================================================
// MAP -- every coordinate, re-read from the baked bytes
// ===========================================================================

TEST_CASE("the Gilded Gull is where the tavern says it is") {
    const TileQuery& tiles = docksTiles();
    using content::TileForm;

    // EVERY RANGE IS GUARDED BEFORE IT IS WALKED. `for (x = X0; x <= X1; ++x)`
    // with X0 past X1 is an EMPTY loop, so an inverted pair makes the whole
    // check below vacuous and green. The S2 review pushed kHearthX0 past
    // kHearthX1 and nothing anywhere went red -- while the renderer, which had
    // the same loop, silently stopped drawing the fire. These four lines are
    // the fix, and they are why the scalar pins below are worth anything.
    REQUIRE(gull::kFootprintX0 < gull::kFootprintX1);
    REQUIRE(gull::kFootprintY0 < gull::kFootprintY1);
    REQUIRE(gull::kBarX0 < gull::kBarX1);
    REQUIRE(gull::kHearthX0 <= gull::kHearthX1);
    REQUIRE(gull::kSnugX0 <= gull::kSnugX1);
    REQUIRE(gull::kDoorX0 < gull::kDoorX1);

    // The shell. Walls all the way round the footprint except at the door.
    for (std::int32_t x = gull::kFootprintX0; x <= gull::kFootprintX1; ++x) {
        const bool isDoor = x == gull::kDoorX0 || x == gull::kDoorX1;
        const TileForm north = tiles.form(x, gull::kFootprintY0, gull::kGroundBand);
        const TileForm south = tiles.form(x, gull::kFootprintY1, gull::kGroundBand);
        if (isDoor) {
            REQUIRE(north == TileForm::Floor);
        } else {
            REQUIRE(north == TileForm::Wall);
        }
        REQUIRE(south == TileForm::Wall);
    }
    for (std::int32_t y = gull::kFootprintY0; y <= gull::kFootprintY1; ++y) {
        REQUIRE(tiles.form(gull::kFootprintX0, y, gull::kGroundBand) == TileForm::Wall);
        REQUIRE(tiles.form(gull::kFootprintX1, y, gull::kGroundBand) == TileForm::Wall);
    }

    // Both door tiles can be walked through, from the street and from inside.
    CHECK(tiles.standable(gull::kDoorX0, gull::kDoorY, gull::kGroundBand));
    CHECK(tiles.standable(gull::kDoorX1, gull::kDoorY, gull::kGroundBand));
    CHECK(tiles.standable(gull::kStreetX, gull::kStreetY, gull::kGroundBand));

    // The bar counter is a solid run and the bartender's tile is beside it.
    for (std::int32_t x = gull::kBarX0; x <= gull::kBarX1; ++x) {
        REQUIRE(tiles.form(x, gull::kBarY, gull::kGroundBand) == TileForm::Wall);
    }
    CHECK(tiles.standable(gull::kBartenderX, gull::kBartenderY, gull::kGroundBand));
    // ...and the counter is BETWEEN the bartender and the room, which is what
    // stops a customer reaching over it. Directly load-bearing: the brawl reach
    // is a tile and a quarter and the counter is exactly two tiles wide.
    CHECK(gull::kBartenderY == gull::kBarY + 1);
    CHECK(tiles.solid(gull::kBartenderX, gull::kBarY, gull::kGroundBand));

    // The hearth is masonry in the south wall.
    for (std::int32_t x = gull::kHearthX0; x <= gull::kHearthX1; ++x) {
        REQUIRE(tiles.form(x, gull::kHearthY, gull::kGroundBand) == TileForm::Wall);
        REQUIRE(tiles.standable(x, gull::kHearthY - 1, gull::kGroundBand));
    }

    // The stair to the rooms, both ends of it.
    CHECK(tiles.form(gull::kStairX, gull::kStairY, gull::kGroundBand) == TileForm::Stair);
    CHECK(tiles.form(gull::kStairX, gull::kStairY, gull::kUpperBand) == TileForm::Stair);
    CHECK(tiles.climbable(gull::kStairX, gull::kStairY, gull::kGroundBand));

    // Four rooms, four beds, four places to stand beside one.
    for (const gull::GuestRoom& room : gull::kRooms) {
        REQUIRE(tiles.solid(room.bedX, room.bedY, gull::kUpperBand));
        REQUIRE(tiles.standable(room.standX, room.standY, gull::kUpperBand));
    }
}

TEST_CASE("the Gull's lights come out of the baked bytes, not out of the renderer") {
    // THE S2 REVIEW'S SEVENTH FINDING. session.cpp hardcoded four table tiles
    // and three lantern tiles with no derivation and no test -- the exact thing
    // tavern.hpp's own header rule forbids. The furniture is now READ from the
    // world, and this is what says the reading is right.
    const TileQuery& tiles = docksTiles();
    const std::vector<gull::TilePos> tables = gull::taproomTables(tiles);

    INFO("derived ", tables.size(), " table tiles");
    for (const gull::TilePos& table : tables) {
        INFO("table at ", table.x, ",", table.y);
        // Inside the walls...
        REQUIRE(table.x > gull::kFootprintX0);
        REQUIRE(table.x < gull::kFootprintX1);
        REQUIRE(table.y > gull::kFootprintY0);
        REQUIRE(table.y < gull::kFootprintY1);
        // ...solid in the baked bytes, because that is what furniture is...
        REQUIRE(tiles.solid(table.x, table.y, gull::kGroundBand));
        // ...and not the bar, not the hearth, not the partition.
        REQUIRE_FALSE((table.y == gull::kBarY && table.x >= gull::kBarX0 &&
                       table.x <= gull::kBarX1));
        REQUIRE_FALSE((table.y == gull::kHearthY && table.x >= gull::kHearthX0 &&
                       table.x <= gull::kHearthX1));
        REQUIRE(table.x != gull::kSnugX0 - 1);
        // A table nobody can reach is scenery. Every one has somewhere to sit.
        bool reachable = false;
        const std::int32_t nx[4] = {table.x + 1, table.x - 1, table.x, table.x};
        const std::int32_t ny[4] = {table.y, table.y, table.y + 1, table.y - 1};
        for (int d = 0; d < 4; ++d) {
            reachable = reachable || tiles.standable(nx[d], ny[d], gull::kGroundBand);
        }
        REQUIRE(reachable);
    }
    // Ascending by (y, x): the map's order, so the candle list is the same on
    // every machine.
    for (std::size_t i = 1; i < tables.size(); ++i) {
        REQUIRE((tables[i - 1].y < tables[i].y ||
                 (tables[i - 1].y == tables[i].y && tables[i - 1].x < tables[i].x)));
    }
    // Pinned, so a re-bake that moves the furniture is red here rather than a
    // room that quietly goes dark.
    CHECK(tables.size() == gull::kTableCount);

    // The lanterns hang off the door and the bar, both of which are re-read
    // from the baked bytes above.
    const std::vector<gull::TilePos> lanterns = gull::lanternTiles();
    REQUIRE(lanterns.size() == 3);
    for (const gull::TilePos& hook : lanterns) {
        INFO("lantern at ", hook.x, ",", hook.y);
        REQUIRE(gull::insideFootprint(hook.x, hook.y));
        // A lantern hangs over somewhere you can walk, or it is inside a wall.
        REQUIRE(tiles.standable(hook.x, hook.y, gull::kGroundBand));
    }
}

TEST_CASE("the house shows its lights when the house is open, and not otherwise") {
    SUBCASE("shut and cold at five in the morning") {
        Room room(hourOfDay(5), gull::kStreetX, gull::kStreetY);
        REQUIRE_FALSE(room.tavern().isOpen());
        REQUIRE_FALSE(room.tavern().fireLit());
        CHECK(room.tavern().houseLights().empty());
    }
    SUBCASE("lit and open at nine at night") {
        Room room(hourOfDay(21), gull::kBartenderX, gull::kBarY - 1);
        REQUIRE(room.tavern().isOpen());
        REQUIRE(room.tavern().fireLit());
        const std::vector<gull::HouseLight> lights = room.tavern().houseLights();
        std::size_t hearth = 0;
        std::size_t candles = 0;
        std::size_t lanterns = 0;
        for (const gull::HouseLight& light : lights) {
            REQUIRE(light.band == gull::kGroundBand);
            REQUIRE(gull::insideFootprint(light.x, light.y));
            switch (light.kind) {
                case gull::LightKind::Hearth:
                    ++hearth;
                    break;
                case gull::LightKind::Candle:
                    ++candles;
                    break;
                case gull::LightKind::Lantern:
                    ++lanterns;
                    break;
            }
        }
        // Two hearth cells, a candle on every derived table, three lanterns.
        // Non-zero on all three, so an inverted range that empties one of the
        // loops is red HERE and not only in a screenshot nobody looks at.
        CHECK(hearth == 2);
        CHECK(candles == gull::kTableCount);
        CHECK(lanterns == 3);
    }
    SUBCASE("the fire is banked before the doors open, and it is the only light") {
        Room room(hourOfDay(10, 30), gull::kStreetX, gull::kStreetY);
        REQUIRE(room.tavern().fireLit());
        REQUIRE_FALSE(room.tavern().isOpen());
        const std::vector<gull::HouseLight> lights = room.tavern().houseLights();
        REQUIRE_FALSE(lights.empty());
        for (const gull::HouseLight& light : lights) {
            REQUIRE(light.kind == gull::LightKind::Hearth);
        }
    }
}

TEST_CASE("every post in the roster is a tile somebody can stand on") {
    // The failure this prevents is silent: an actor whose post is inside a wall
    // never arrives, never reaches Working, and stands in the doorway forever.
    Tavern tavern(docksTiles(), hourOfDay(20), 1, content::contentDir());
    REQUIRE(tavern.actors().size() == 16);
    for (const Actor& actor : tavern.actors()) {
        for (const ScheduleBlock& block : actor.schedule().blocks()) {
            INFO("post of ", actor.name());
            REQUIRE(docksTiles().standable(block.postX, block.postY, block.postBand));
            REQUIRE(gull::insideFootprint(block.postX, block.postY));
        }
    }
}

TEST_CASE("there is a route from the quay to the bar, and it goes through the door") {
    RegionPath path(docksTiles(), gull::kRegion);
    std::vector<PathStep> route;
    REQUIRE(path.find(PathStep{gull::kStreetX, gull::kStreetY, gull::kGroundBand},
                      PathStep{gull::kBartenderX, gull::kBartenderY, gull::kGroundBand},
                      route));
    CHECK_FALSE(route.empty());
    CHECK(route.back().x == gull::kBartenderX);
    CHECK(route.back().y == gull::kBartenderY);

    // The only way in is the door, so the route has to use one of its two tiles.
    const bool throughDoor =
        std::any_of(route.begin(), route.end(), [](const PathStep& step) {
            return step.y == gull::kDoorY &&
                   (step.x == gull::kDoorX0 || step.x == gull::kDoorX1);
        });
    CHECK(throughDoor);

    // Every step of it is somewhere a body can be, and consecutive steps are
    // never more than one tile apart.
    PathStep previous{gull::kStreetX, gull::kStreetY, gull::kGroundBand};
    for (const PathStep& step : route) {
        REQUIRE(docksTiles().standable(step.x, step.y, step.band));
        REQUIRE(std::max(std::abs(step.x - previous.x), std::abs(step.y - previous.y)) == 1);
        previous = step;
    }
}

TEST_CASE("a route does not slip through the corner where two walls meet") {
    // The snug's partition runs down x=157 with a gap at y=69 and y=70. Going
    // from (156,71) to (157,70) is one diagonal step -- straight through the
    // join between the partition and the tile south of the gap -- and it must
    // not be taken. An actor that cuts that corner reads as walking through the
    // wall, which is the one thing a room full of walls must never do.
    const TileQuery& tiles = docksTiles();
    REQUIRE(tiles.standable(156, 71, gull::kGroundBand));
    REQUIRE(tiles.standable(157, 70, gull::kGroundBand));
    REQUIRE(tiles.standable(156, 70, gull::kGroundBand));
    REQUIRE(tiles.solid(157, 71, gull::kGroundBand));  // the wall being cut

    RegionPath path(tiles, gull::kRegion);
    std::vector<PathStep> route;
    REQUIRE(path.find(PathStep{156, 71, gull::kGroundBand},
                      PathStep{157, 70, gull::kGroundBand}, route));
    // Two steps, round the corner, not one through it.
    CHECK(route.size() == 2);
    CHECK(route.front().x == 156);
    CHECK(route.front().y == 70);
}

TEST_CASE("a route into a wall is refused rather than approximated") {
    RegionPath path(docksTiles(), gull::kRegion);
    std::vector<PathStep> route;
    // The middle of the bar counter: masonry.
    CHECK_FALSE(path.find(PathStep{gull::kStreetX, gull::kStreetY, gull::kGroundBand},
                          PathStep{gull::kBartenderX, gull::kBarY, gull::kGroundBand}, route));
    CHECK(route.empty());
    // Somewhere outside the box entirely.
    CHECK_FALSE(path.find(PathStep{gull::kStreetX, gull::kStreetY, gull::kGroundBand},
                          PathStep{10, 10, gull::kGroundBand}, route));
}

// ===========================================================================
// RULE -- the brawl / lethal line, with no world in sight
// ===========================================================================

TEST_CASE("a fist fight is a brawl and a knife fight is not") {
    const auto twoFighters = [](Weapon aWeapon, Intent aIntent, std::int32_t aHp, Weapon bWeapon,
                                Intent bIntent, std::int32_t bHp) {
        return std::vector<Fighter>{Fighter{1, aWeapon, aIntent, aHp, 40},
                                    Fighter{2, bWeapon, bIntent, bHp, 40}};
    };

    // B1 -- nothing edged is out.
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Subdue, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Brawl);
    CHECK(classifyFight(twoFighters(Weapon::Improvised, Intent::Subdue, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Brawl);
    // A bouncer's cudgel keeps it in the world. That is the entire reason
    // bouncers carry one.
    CHECK(classifyFight(twoFighters(Weapon::Blunt, Intent::Subdue, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Brawl);
    CHECK(classifyFight(twoFighters(Weapon::Edged, Intent::Subdue, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Lethal);
    // ...from either side of it.
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Subdue, 40, Weapon::Edged,
                                    Intent::Subdue, 40)) == FightClass::Lethal);

    // B2 -- nobody means to kill.
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Harm, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Brawl);
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Kill, 40, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Lethal);

    // B3 -- nobody is being finished. A bloodied man among people who only mean
    // to put him out is still a bar fight; the same man with somebody meaning
    // him harm is not.
    REQUIRE(isBloodied(9, 40));
    REQUIRE_FALSE(isBloodied(11, 40));
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Subdue, 40, Weapon::Fists,
                                    Intent::Subdue, 9)) == FightClass::Brawl);
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Harm, 40, Weapon::Fists,
                                    Intent::Subdue, 9)) == FightClass::Lethal);
    // The bloodied man may be the one meaning harm, too.
    CHECK(classifyFight(twoFighters(Weapon::Fists, Intent::Harm, 9, Weapon::Fists,
                                    Intent::Subdue, 40)) == FightClass::Lethal);

    // A fight with nobody to fight is not an escalation.
    CHECK(classifyFight({}) == FightClass::Brawl);
    const std::vector<Fighter> alone{Fighter{1, Weapon::Edged, Intent::Kill, 40, 40}};
    CHECK(classifyFight(alone) == FightClass::Brawl);

    // And the polarity has exactly one definition.
    CHECK(resolvesInWorld(FightClass::Brawl));
    CHECK_FALSE(resolvesInWorld(FightClass::Lethal));
}

TEST_CASE("a blow takes hit points off and eventually puts somebody down") {
    Fighter target{1, Weapon::Fists, Intent::Subdue, 24, 24};
    std::int32_t landed = 0;
    std::int32_t missed = 0;
    bool sawBloodied = false;
    for (std::uint64_t roll = 0; roll < 64 && !isDowned(target.hp); ++roll) {
        const Blow blow = strike(Weapon::Fists, target, roll * 0x9E3779B97F4A7C15ull);
        if (blow.landed) {
            ++landed;
            CHECK(blow.damage >= baseDamage(Weapon::Fists));
        } else {
            ++missed;
        }
        sawBloodied = sawBloodied || blow.bloodied;
    }
    CHECK(landed > 0);
    CHECK(missed > 0);  // a fight that never whiffs reads as a spreadsheet
    CHECK(sawBloodied);
    CHECK(isDowned(target.hp));
    CHECK(target.hp == 0);  // down, and never below it
}

// ===========================================================================
// ROOM -- hours, trade, and the door policy
// ===========================================================================

TEST_CASE("the room is loud at night and empty at dawn") {
    Tavern night(docksTiles(), hourOfDay(22), 1, content::contentDir());
    Tavern dawn(docksTiles(), hourOfDay(5), 1, content::contentDir());

    CHECK(night.isOpen());
    CHECK_FALSE(dawn.isOpen());
    CHECK(night.fireLit());
    CHECK_FALSE(dawn.fireLit());

    CHECK(night.patronCount() > 0);
    CHECK(dawn.patronCount() == 0);
    CHECK(dawn.presentCount() == 0);
    CHECK(night.noise() > dawn.noise());
    CHECK(dawn.noise() == 0);

    // Every named role the sprint asked for is actually in the room at ten.
    bool bartender = false;
    bool innkeeper = false;
    bool skyrunner = false;
    std::int32_t bouncers = 0;
    for (const Actor& actor : night.actors()) {
        if (!actor.present()) {
            continue;
        }
        bartender = bartender || actor.role() == ActorRole::Bartender;
        innkeeper = innkeeper || actor.role() == ActorRole::Innkeeper;
        skyrunner = skyrunner || actor.role() == ActorRole::SkyrunnerContact;
        bouncers += actor.role() == ActorRole::Bouncer ? 1 : 0;
    }
    CHECK(bartender);
    CHECK(innkeeper);
    CHECK(skyrunner);
    CHECK(bouncers >= 1);

    // The priest keeps an evening hour, not a night one.
    Tavern evening(docksTiles(), hourOfDay(19, 30), 1, content::contentDir());
    CHECK(evening.priestTeaches(3).size() > 0);
    Tavern noon(docksTiles(), hourOfDay(12), 1, content::contentDir());
    CHECK(noon.priestTeaches(3).empty());

    // Both bouncers are on for the loud hours, and one alone covers the day.
    const auto onDuty = [](const Tavern& tavern) {
        std::int32_t count = 0;
        for (const Actor& actor : tavern.actors()) {
            count += (actor.present() && actor.role() == ActorRole::Bouncer) ? 1 : 0;
        }
        return count;
    };
    Tavern lunch(docksTiles(), hourOfDay(13), 1, content::contentDir());
    CHECK(onDuty(lunch) == 1);
    CHECK(onDuty(evening) == 2);
}

TEST_CASE("patrons arrive and leave, and the room fills between the two") {
    Room room(hourOfDay(17, 59), gull::kStreetX, gull::kStreetY);
    const std::int32_t before = room.tavern().patronCount();
    // Two minutes past six: the pay-out crowd is walking in off the quay.
    room.run(150);
    const std::int32_t after = room.tavern().patronCount();
    CHECK(after > before);

    // And they are actually IN the room, not standing in the street.
    std::int32_t inside = 0;
    for (const Actor& actor : room.tavern().actors()) {
        if (actor.present() && actor.role() == ActorRole::Patron &&
            gull::insideFootprint(actor.tileX(), actor.tileY())) {
            ++inside;
        }
    }
    CHECK(inside > 0);
}

TEST_CASE("the bartender sells a drink, takes the coin, and runs out") {
    // At the counter, on the customer side of it.
    Room room(hourOfDay(20), gull::kBartenderX, gull::kBarY - 1);
    room.run(1);
    Tavern& tavern = room.tavern();

    const Actor* bartender = nullptr;
    for (const Actor& actor : tavern.actors()) {
        if (actor.role() == ActorRole::Bartender) {
            bartender = &actor;
        }
    }
    REQUIRE(bartender != nullptr);
    REQUIRE(bartender->present());

    const std::int32_t purse = tavern.playerCoin();
    const std::int32_t stock = tavern.drinkStock();
    const std::int32_t tillBefore = bartender->coin();

    REQUIRE(tavern.buyDrink() == ServiceResult::Served);
    CHECK(tavern.playerCoin() == purse - kDrinkPrice);
    CHECK(tavern.drinkStock() == stock - 1);
    CHECK(bartender->coin() == tillBefore + kDrinkPrice);
    CHECK(tavern.drinksPlayerHasHad() == 1);

    // Buy until the purse is empty, and then be refused for the right reason.
    while (tavern.playerCoin() >= kDrinkPrice) {
        REQUIRE(tavern.buyDrink() == ServiceResult::Served);
    }
    CHECK(tavern.buyDrink() == ServiceResult::NoCoin);

    // Coin is not created: what the player spent is what the till gained.
    CHECK(bartender->coin() - tillBefore == purse - tavern.playerCoin());
}

TEST_CASE("nobody is served across the room, or through a shut door") {
    // Standing at the door is not standing at the bar.
    Room far(hourOfDay(20), gull::kDoorX0, gull::kDoorY);
    far.run(1);
    CHECK(far.tavern().buyDrink() == ServiceResult::TooFar);

    // Four in the morning: the doors are shut and the barrels are somebody
    // else's problem.
    Room shut(hourOfDay(4), gull::kBartenderX, gull::kBarY - 1);
    shut.run(1);
    CHECK(shut.tavern().buyDrink() == ServiceResult::Closed);
}

TEST_CASE("the innkeeper rents a room and the player sleeps until morning") {
    // At the stair, in the snug, where Master Venn keeps his post.
    Room room(hourOfDay(22), 158, 75);
    room.run(1);
    Tavern& tavern = room.tavern();

    const std::int32_t purse = tavern.playerCoin();
    // S3: kRoomPrice is what a bed is WORTH. What you pay is what the landlord
    // thinks of you plus what your streetwise is worth against his, so the
    // charge is asked for rather than assumed -- and a stranger with no trade
    // skill pays a little over the odds, which is the whole point.
    const std::int32_t asked = tavern.roomPriceForPlayer();
    CHECK(asked >= kRoomPrice);
    REQUIRE(tavern.rentRoom() == ServiceResult::Served);
    CHECK(tavern.playerCoin() == purse - asked);
    REQUIRE(tavern.rentedRoom() >= 0);
    REQUIRE(tavern.rentedRoom() < gull::kRoomCount);

    // Not from the taproom floor: you have to go up.
    CHECK(tavern.sleep() == ServiceResult::TooFar);

    const gull::GuestRoom& let = gull::kRooms[tavern.rentedRoom()];
    tavern.setPlayer(q8_tile_centre(let.standX), q8_tile_centre(let.standY), gull::kUpperBand);
    REQUIRE(tavern.sleep() == ServiceResult::Served);
    CHECK(tavern.timeOfDay() == hourOfDay(7));
    // Morning: the fire is banked, the doors are shut, and the room is empty.
    CHECK_FALSE(tavern.isOpen());
    CHECK(tavern.patronCount() == 0);
}

TEST_CASE("the Skyrunner contact is present and says nothing worth having") {
    Room room(hourOfDay(23), 158, 69);
    room.run(2);
    const TalkResult talk = room.tavern().talkToNearest();
    CHECK(talk.speaker == "Finch");
    // Present, and guarded. S5's questline is what changes this answer.
    CHECK(talk.result == ServiceResult::Refused);
    CHECK_FALSE(talk.line.empty());
}

TEST_CASE("the priest teaches out of the authored raws, not out of a table here") {
    Tavern tavern(docksTiles(), hourOfDay(20), 1, content::contentDir());
    REQUIRE(tavern.spellbook().loaded());
    // The eleven spells the owner wrote, and every one of them modular: a list
    // of components rather than a baked outcome. That shape is what S4's
    // spellcrafting composes.
    CHECK(tavern.spellbook().size() == 11);
    const Spell* warm = tavern.spellbook().find("warm_the_hands");
    REQUIRE(warm != nullptr);
    CHECK(warm->displayName == "Warm the Hands");
    CHECK(warm->skill == "linkcraft");
    REQUIRE(warm->components.size() == 1);
    CHECK(warm->components.front().effect == "TEMPERATURE");
    CHECK(warm->components.front().magnitude == 15);

    // He teaches what the student can hold and no more.
    const std::vector<const Spell*> novice = tavern.priestTeaches(0);
    const std::vector<const Spell*> adept = tavern.priestTeaches(3);
    CHECK(adept.size() >= novice.size());
    for (const Spell* spell : novice) {
        CHECK(spell->skill == "linkcraft");
        CHECK(spell->minLevel <= 0);
    }
}

// ===========================================================================
// ROOM -- the door policy, which is the sprint's acceptance case
// ===========================================================================

TEST_CASE("a brawler gets warned and then physically put out of the door") {
    // Seven in the evening: both bouncers on, the pay-night crowd in, and the
    // player standing at the counter beside Tarn Wrenhale.
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();

    REQUIRE(tavern.playerInside());
    REQUIRE(tavern.playerStanding() == Standing::Welcome);
    REQUIRE(tavern.timesEjected() == 0);

    // --- the punch ---------------------------------------------------------
    const Tavern::PunchResult punch = tavern.playerPunchNearest();
    REQUIRE(punch.swung);
    CHECK_FALSE(punch.targetName.empty());
    // Fists, and nobody means more than to win, so this is the world's fight.
    CHECK(punch.fight == FightClass::Brawl);
    CHECK(punch.blow.landed);
    CHECK(punch.blow.damage > 0);
    CHECK_FALSE(tavern.escalated());
    // The house noticed.
    CHECK(tavern.playerStanding() == Standing::BeingWarned);

    // --- the warning -------------------------------------------------------
    // A bouncer has to cross the room to give it, so it does not happen this
    // second -- but it does happen, and quickly.
    CHECK(tavern.playerStanding() == Standing::BeingWarned);
    const int toWarning = room.runUntil(
        [&tavern] { return tavern.playerStanding() == Standing::Warned; }, 30);
    CHECK(toWarning >= 0);
    CHECK(toWarning < 30);
    CHECK(tavern.warningsGiven() == 1);
    CHECK(tavern.playerStanding() == Standing::Warned);
    CHECK_FALSE(tavern.lastWarning().empty());
    REQUIRE(tavern.respondingBouncer() != nullptr);
    CHECK(tavern.respondingBouncer()->role() == ActorRole::Bouncer);
    // It was said to the player's face, not shouted across the room.
    CHECK(tavern.respondingBouncer()->distanceTo(room.body().x(), room.body().y()) <=
          kWarnRadius);
    // Still inside, and still in one piece.
    CHECK(tavern.playerInside());
    CHECK(tavern.timesEjected() == 0);

    // --- refusing to leave -------------------------------------------------
    // The player does nothing at all: no input, no movement, no second punch.
    // Staying put past the grace IS the refusal, and it is what the door policy
    // acts on. The grace is REAL: two seconds short of it, nobody has touched
    // anybody.
    const std::int32_t startY = room.body().tileY();
    room.run(kGraceSeconds - 2);
    CHECK(tavern.playerStanding() == Standing::Warned);
    CHECK(tavern.playerInside());
    CHECK(tavern.timesEjected() == 0);
    CHECK(room.body().tileY() == startY);  // not shoved an inch yet

    room.run(3);
    CHECK(tavern.playerStanding() == Standing::BeingEjected);

    // --- the ejection ------------------------------------------------------
    const int toStreet = room.runUntil([&tavern] { return !tavern.playerInside(); }, 90);
    CHECK(toStreet >= 0);
    CHECK(toStreet < 90);
    room.run(1);  // one more tick for the house to register it

    CHECK_FALSE(tavern.playerInside());
    CHECK(tavern.timesEjected() == 1);
    CHECK(tavern.playerStanding() == Standing::Barred);
    // Out through the DOOR, on the street side of the north wall, not through a
    // wall and not into the cellar.
    CHECK(room.body().tileY() < gull::kFootprintY0);
    CHECK(room.body().tileY() < startY);
    CHECK(room.body().band() == gull::kGroundBand);
    CHECK(room.tiles().standable(room.body().tileX(), room.body().tileY(),
                                 room.body().band()));
    // The whole thing happened in the world. No transition was ever raised.
    CHECK_FALSE(tavern.escalated());
    CHECK(tavern.escalation() == FightClass::Brawl);

    // --- and the house remembers -------------------------------------------
    CHECK(tavern.buyDrink() == ServiceResult::Barred);
    CHECK(tavern.rentRoom() == ServiceResult::Barred);
}

TEST_CASE("a second offence skips the grace and goes straight to hands on") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();

    REQUIRE(tavern.playerPunchNearest().swung);
    REQUIRE(room.runUntil([&tavern] { return tavern.playerStanding() == Standing::Warned; },
                          30) >= 0);
    const std::int32_t warnings = tavern.warningsGiven();

    // Told once. There is no second warning.
    tavern.reportOffence(Offence::Stole);
    CHECK(tavern.playerStanding() == Standing::BeingEjected);
    room.run(1);
    CHECK(tavern.warningsGiven() == warnings);

    room.run(80);
    CHECK(tavern.timesEjected() == 1);
    CHECK_FALSE(tavern.playerInside());
}

TEST_CASE("taking the warning and leaving ends it with no hands on anybody") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.playerPunchNearest().swung);
    REQUIRE(room.runUntil([&tavern] { return tavern.playerStanding() == Standing::Warned; },
                          30) >= 0);

    // Walk out of the door under your own power. North-north-east: the wall is
    // solid at x=152 and the door is at 153 and 154, so the body slides along
    // the front and out through the gap -- which is the collision rule doing
    // exactly what it is for.
    room.body().setYaw(angle_from_degrees(25));
    MoveInput out;
    out.forward = 1;
    out.run = true;
    for (int second = 0; second < 20 && tavern.playerInside(); ++second) {
        room.runWith(1, out);
    }

    CHECK_FALSE(tavern.playerInside());
    room.run(1);
    CHECK(tavern.playerStanding() == Standing::Welcome);
    CHECK(tavern.timesEjected() == 0);
    // Not barred: the house takes yes for an answer.
    CHECK(tavern.buyDrink() != ServiceResult::Barred);
}

TEST_CASE("a drawn blade stops being the world's fight") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();

    tavern.setPlayerCombat(Weapon::Edged, Intent::Harm);
    const Tavern::PunchResult swing = tavern.playerPunchNearest();
    REQUIRE(swing.swung);
    CHECK(swing.fight == FightClass::Lethal);
    // The world REFUSED to resolve it. No blow landed, nobody lost a hit point
    // in here, and the escalation is raised for the client to route to the
    // dedicated combat screen.
    CHECK_FALSE(swing.blow.landed);
    CHECK(swing.blow.damage == 0);
    CHECK(tavern.escalated());
    CHECK(tavern.escalation() == FightClass::Lethal);
    const Actor* target = tavern.actorById(swing.targetId);
    REQUIRE(target != nullptr);
    CHECK(target->hp() == target->hpMax());

    // The house still minds. Where the fight is resolved and whether the
    // bouncers come over are two different questions.
    CHECK(tavern.playerStanding() == Standing::BeingWarned);
}

TEST_CASE("a brawl that starts with fists escalates the moment a knife comes out") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();

    REQUIRE(tavern.playerPunchNearest().fight == FightClass::Brawl);
    room.run(2);
    REQUIRE_FALSE(tavern.escalated());
    CHECK(tavern.currentFight().size() >= 2);

    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    room.run(1);
    CHECK(tavern.escalated());
}

TEST_CASE("a brawl never kills the player, it puts them on the floor") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    room.run(2);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.playerPunchNearest().swung);

    const std::int32_t startHp = tavern.playerHp();
    room.run(120);
    // It hurt, in the world, with fists.
    CHECK(tavern.playerHp() < startHp);
    CHECK(tavern.playerHp() >= kPlayerBrawlFloor);
    CHECK_FALSE(tavern.escalated());
}

// ===========================================================================
// ROOM -- determinism
// ===========================================================================

TEST_CASE("the same seed runs the tavern to the same hash twice") {
    // The property the whole project rests on, applied to the first system that
    // has actors in it. Two independent rooms, the same seed, the same scripted
    // trouble, and their section hashes must agree bit for bit.
    const auto runOne = [](std::uint64_t seed) {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1, seed);
        room.run(2);
        room.tavern().playerPunchNearest();
        room.run(45);
        WorldHasher hasher;
        room.tavern().hash_into(hasher.section_sink(room.tavern().id()));
        return hasher.section_hash(room.tavern().id());
    };

    const std::uint64_t first = runOne(0x4752414E41444144ull);
    const std::uint64_t second = runOne(0x4752414E41444144ull);
    CHECK(first == second);

    // ...and a different seed is a different night, or the hash is not reading
    // anything the RNG touched.
    const std::uint64_t other = runOne(0x0123456789ABCDEFull);
    CHECK(other != first);
}

TEST_CASE("the tavern's state is actually in the hash it produces") {
    // A hash that ignores half the state is a gate that goes green through a
    // divergence. Each of these changes ONE thing and requires the number to
    // move.
    const auto hashOf = [](const Tavern& tavern) {
        WorldHasher hasher;
        tavern.hash_into(hasher.section_sink(tavern.id()));
        return hasher.section_hash(tavern.id());
    };

    Tavern base(docksTiles(), hourOfDay(20), 7, content::contentDir());
    const std::uint64_t start = hashOf(base);

    Tavern later(docksTiles(), hourOfDay(20), 7, content::contentDir());
    later.setTimeOfDay(hourOfDay(21));
    CHECK(hashOf(later) != start);

    Tavern moved(docksTiles(), hourOfDay(20), 7, content::contentDir());
    moved.setPlayer(q8_tile_centre(gull::kBartenderX), q8_tile_centre(gull::kBarY - 1),
                    gull::kGroundBand);
    CHECK(hashOf(moved) != start);

    Tavern poorer(docksTiles(), hourOfDay(20), 7, content::contentDir());
    poorer.setPlayerCoin(kPlayerStartingCoin - 1);
    CHECK(hashOf(poorer) != start);

    Tavern armed(docksTiles(), hourOfDay(20), 7, content::contentDir());
    armed.setPlayerCombat(Weapon::Edged, Intent::Kill);
    CHECK(hashOf(armed) != start);

    Tavern troubled(docksTiles(), hourOfDay(20), 7, content::contentDir());
    troubled.reportOffence(Offence::Stole);
    CHECK(hashOf(troubled) != start);
}

// ===========================================================================
// WITNESS -- the S3 review's first finding, closed from both sides
// ===========================================================================

TEST_CASE("a deed carries eight tiles and no further") {
    // S3 asserted only that the radius was greater than zero: the review
    // widened kWitnessRangeTiles to a hundred thousand tiles and the whole
    // 270-test gate stayed green. Both directions are pinned now.
    Room bar(hourOfDay(20), gull::kBartenderX, gull::kBarY - 1);
    const Actor* bartender = nullptr;
    for (const Actor& actor : bar.tavern().actors()) {
        if (actor.role() == ActorRole::Bartender && actor.present()) {
            bartender = &actor;
        }
    }
    REQUIRE(bartender != nullptr);
    bar.tavern().setPlayer(q8_tile_centre(bartender->tileX()),
                           q8_tile_centre(bartender->tileY() - 1), gull::kGroundBand);
    bar.tavern().spreadWitness(-1, Deed::Robbed);
    // Somebody a tile away, with nothing in between, remembers it.
    CHECK(bar.tavern().dialogue().ledger().knows(bartender->id()));

    // And from the far corner of the room, EVERY body outside the range must
    // have no memory of it at all. This is the case that goes red when the
    // radius is widened.
    Room corner(hourOfDay(20), gull::kBartenderX, gull::kBarY - 1);
    const std::int32_t fromX = gull::kFootprintX0 + 1;
    const std::int32_t fromY = gull::kFootprintY0 + 1;
    corner.tavern().setPlayer(q8_tile_centre(fromX), q8_tile_centre(fromY), gull::kGroundBand);
    corner.tavern().spreadWitness(-1, Deed::Robbed);
    bool anyBeyond = false;
    for (const Actor& actor : corner.tavern().actors()) {
        if (!actor.present()) {
            continue;
        }
        const std::int32_t tiles =
            std::max(std::abs(actor.tileX() - fromX), std::abs(actor.tileY() - fromY));
        if (tiles > Tavern::kWitnessRangeTiles) {
            anyBeyond = true;
            CAPTURE(actor.name());
            CHECK_FALSE(corner.tavern().dialogue().ledger().knows(actor.id()));
        }
    }
    // Not vacuous: somebody really was out of range.
    CHECK(anyBeyond);
}

TEST_CASE("a robbery on the guest floor is not witnessed by the taproom below") {
    // The second half of the S3 finding: spreadWitness filtered on (x, y) only,
    // so a sleeper in a room directly above the bar witnessed a robbery through
    // the floor. Bodies are on the ground band; the player goes upstairs.
    Room upstairs(hourOfDay(20), gull::kBartenderX, gull::kBarY - 1);
    REQUIRE(upstairs.tavern().presentCount() > 0);
    const gull::GuestRoom& bed = gull::kRooms[0];
    upstairs.tavern().setPlayer(q8_tile_centre(bed.standX), q8_tile_centre(bed.standY),
                                gull::kUpperBand);
    upstairs.tavern().spreadWitness(-1, Deed::Robbed);
    for (const Actor& actor : upstairs.tavern().actors()) {
        CAPTURE(actor.name());
        CHECK_FALSE(upstairs.tavern().dialogue().ledger().knows(actor.id()));
    }
    // The same deed on the same tiles one floor down IS seen, so the case above
    // is about the floor and not about the corner of the map.
    Room downstairs(hourOfDay(20), gull::kBartenderX, gull::kBarY - 1);
    downstairs.tavern().setPlayer(q8_tile_centre(bed.standX), q8_tile_centre(bed.standY),
                                  gull::kGroundBand);
    downstairs.tavern().spreadWitness(-1, Deed::Robbed);
    std::int32_t seen = 0;
    for (const Actor& actor : downstairs.tavern().actors()) {
        if (downstairs.tavern().dialogue().ledger().knows(actor.id())) {
            ++seen;
        }
    }
    CHECK(seen > 0);
}

TEST_CASE("nobody witnesses anything through a wall") {
    // The third clause. Standing on the quay outside the north wall, the room
    // is full of people within eight tiles of the player and NONE of them can
    // see the street through masonry -- except through the two door tiles, and
    // the case picks a spot that is not in line with either.
    Room outside(hourOfDay(20), gull::kStreetX, gull::kStreetY);
    const std::int32_t standX = gull::kFootprintX0 + 2;
    const std::int32_t standY = gull::kFootprintY0 - 2;
    REQUIRE(standX != gull::kDoorX0);
    REQUIRE(standX != gull::kDoorX1);
    outside.tavern().setPlayer(q8_tile_centre(standX), q8_tile_centre(standY),
                               gull::kGroundBand);
    bool anyoneClose = false;
    for (const Actor& actor : outside.tavern().actors()) {
        if (!actor.present() || actor.band() != gull::kGroundBand) {
            continue;
        }
        const std::int32_t tiles =
            std::max(std::abs(actor.tileX() - standX), std::abs(actor.tileY() - standY));
        if (tiles <= Tavern::kWitnessRangeTiles && gull::insideFootprint(actor.tileX(),
                                                                        actor.tileY())) {
            anyoneClose = true;
        }
    }
    REQUIRE(anyoneClose);
    outside.tavern().spreadWitness(-1, Deed::Robbed);
    for (const Actor& actor : outside.tavern().actors()) {
        if (!gull::insideFootprint(actor.tileX(), actor.tileY())) {
            continue;
        }
        CAPTURE(actor.name());
        CHECK_FALSE(outside.tavern().dialogue().ledger().knows(actor.id()));
    }
}
