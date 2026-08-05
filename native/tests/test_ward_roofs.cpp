// SOMEBODY LIVES ON THE ROOFS.
//
// The population round shipped six hundred and sixty-one people and said this
// out loud in its own header: "ward actors have no climb verb, so nobody is
// homed where they cannot walk; the Gullet's thieves keep ground-level
// condos." Read against the canon that is a bigger hole than it sounds.
//
//   * DOCKS-GAZETTEER section 2.5 puts Trojian housing on a wealth gradient
//     that runs from courtyard compounds up to ROOFTOP SLUMS -- tents and mud
//     huts on a walled deck, let by the house-owner beneath. That is where the
//     poorest of a ward live.
//   * The same section says rooftops are socially unseemly for every Trojian
//     except a presented Wielder, which is WHY the poor are up there and WHY
//     burglars use the deck as a highway. The ward's criminal faction is called
//     the SKYRUNNERS. K35 The Skyrunner's Roost is an authored site on the
//     Gullet's roof-slum deck.
//   * Section 2.6 counts 8,132 standable cells the walking rules cannot reach
//     and files their isolation as design "until the law/economy layers learn
//     to climb (S5+)".
//
// So the district had a poor tier with nobody in it and a criminal faction
// whose territory was empty. These cases are about the three claims that fixes:
// the roofs are inhabited, they are inhabited by the people canon puts there,
// and every one of those people can get down again.
//
// THE LAST ONE IS THE ONE TO WATCH. "Reachable by climbing" is a statement
// about getting UP. A bed that can be climbed to and not climbed from is a
// tenant standing on a deck until the day it starves, and nothing about a
// frame or a census would say so.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/ward_actors.hpp"

using namespace granadad;

namespace {

struct Docks {
    content::World world;
    sim::TileQuery tiles;
    explicit Docks()
        : world(content::loadWorldFile(content::bakedMap(sim::docks::kWorldName))), tiles(world) {}
};

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;  // "GRANADAD"

struct WardRun {
    Docks docks;
    sim::PhasedEngine engine;
    sim::WardPopulation* people = nullptr;

    explicit WardRun(std::int32_t startHour) : engine(kSeed, docks.world) {
        auto owned = std::make_unique<sim::WardPopulation>(
            docks.tiles, sim::hourOfDay(startHour), kSeed, content::contentDir());
        people = owned.get();
        engine.register_system(std::move(owned));
        engine.boot();
    }

    void run(std::int64_t ticks) {
        for (std::int64_t t = 0; t < ticks; ++t) {
            engine.tick();
        }
    }
};

/// Whether one step of a planned route is a move the district's own rules
/// actually permit. Asked of every hop of every roof route below, because a
/// router that invented a move would produce a beautiful path nobody could
/// walk -- and the sim commits exactly the cells the route names.
[[nodiscard]] bool legalHop(const sim::TileQuery& tiles, const sim::PathStep& from,
                            const sim::PathStep& to) {
    if (std::max(std::abs(from.x - to.x), std::abs(from.y - to.y)) != 1) {
        return false;
    }
    if (tiles.stepBand(from.x, from.y, from.band, to.x, to.y) == to.band) {
        return true;  // an ordinary step, including the one-level kerb
    }
    if (tiles.mantleBand(from.x, from.y, from.band, to.x, to.y) == to.band) {
        return true;  // a haul up a wall face
    }
    const std::int32_t landing =
        tiles.landingBand(to.x, to.y, from.band - 2, sim::kMaxPathDrop - 2);
    return landing != sim::TileQuery::kNoBand && landing == to.band;
}

/// The nearest piece of the ward's own WALKING ground to a roof bed: the
/// compound underneath it. Everything on the walking island is joined to
/// everything else on it, so a route to the nearest piece of it is a route to
/// the whole district.
[[nodiscard]] bool groundUnder(const sim::WardPopulation& people, const sim::PathStep& bed,
                               sim::PathStep& out) {
    for (std::int32_t r = 1; r <= 12; ++r) {
        for (std::int32_t db = 0; db >= -3; --db) {
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                for (std::int32_t dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    if (!people.onWalkingGround(bed.x + dx, bed.y + dy, bed.band + db)) {
                        continue;
                    }
                    out = sim::PathStep{bed.x + dx, bed.y + dy, bed.band + db};
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the roof slum has tenants, and they are the people canon puts up there") {
    WardRun run(8);
    const sim::WardCensus roll = run.people->census();

    // THE HEADLINE NUMBER. Before this pass it was zero, by construction: the
    // bake's snap came down off every deck to find the compound underneath.
    INFO("roof-homed: ", roll.roofHomed, " of ", roll.total);
    CHECK(roll.roofHomed > 0);

    // AND THEY ARE MORE THAN ONE TRADE. The gazetteer's rooftop tier is the
    // ward's poor, its children and the burglars whose whole identity is the
    // deck -- so a plane holding one household of one trade would be a bed on a
    // roof rather than a roof slum. Which trades exactly is a fact about which
    // huts the map put on a climb-only plane and which it put up a stair, so
    // the case counts distinct trades rather than naming them.
    std::int32_t trades = 0;
    for (std::size_t t = 0; t < sim::kWardTypeCount; ++t) {
        if (roll.roofHomedByType[t] > 0) {
            INFO(roll.roofHomedByType[t], " x ",
                 sim::wardTypeName(static_cast<sim::WardType>(t)), " sleep on a deck");
            ++trades;
        }
    }
    INFO("distinct trades on the decks: ", trades);
    CHECK(trades >= 2);

    // AND NOBODY WHO CANNOT CLIMB, which is the failure this would arrive as:
    // one serf drawn into a roof hut by the household mix is one body that
    // walks off to work and can never get home again.
    for (const sim::WardActor& actor : run.people->actors()) {
        if (!actor.homeOnTheRoof) {
            continue;
        }
        INFO("actor ", actor.id, " is a ", sim::wardTypeName(actor.type), " homed at ",
             actor.homeX, ',', actor.homeY, ",z", actor.homeBand);
        CHECK(sim::wardTypeClimbs(actor.type));
    }

    // AND THE REFUSALS ARE REPORTED RATHER THAN ASSERTED AWAY.
    //
    // A refusal is a hut the router could not prove a sound deck cell for, so
    // its household fell back to the compound underneath. THAT IS THE GUARD
    // WORKING, not a defect: the alternative is a bed on a plane its tenant
    // cannot leave, which is the one outcome this whole pass exists to prevent.
    // The Docks has ten authored roof huts and the map does not give a sound,
    // round-trippable deck cell for every one of them.
    //
    // So the number is printed and the CLAIM it would have guarded is proved
    // directly elsewhere: "a roof bed is a bed you can get out of" plans every
    // tenant's route home and back with the real router, and "nobody is homed
    // on ground they cannot reach" holds it for the whole roll. A build where
    // every hut was refused would fail the roofHomed check above, which is the
    // regression this actually needs to catch.
    INFO("roof huts: ", run.people->roofHomesOnStairs(), " on a stair-served deck, ",
         run.people->roofHomesRefused(), " refused for want of a sound deck cell");
}

TEST_CASE("a roof bed is a bed you can get out of, and it is proved both ways") {
    // THE STRANDING CASE. The component map says a deck can be reached from the
    // ward's ground BY CLIMBING; it says nothing whatever about coming back,
    // and the roof moves are not symmetric -- a wall you can mantle up is a
    // wall you may only be able to come down beside, and a drop has no inverse
    // at all. So every roof tenant's own route home and back is planned here
    // with the same router, in the same gait, that the simulation uses.
    WardRun run(8);
    sim::PathFinder finder(run.docks.tiles);
    std::vector<sim::PathStep> route;

    std::int32_t checked = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (!actor.homeOnTheRoof) {
            continue;
        }
        ++checked;
        const sim::PathStep bed{actor.homeX, actor.homeY, actor.homeBand};
        // NOT THE ACTOR'S OWN POST, and that matters. A thief's post IS its
        // bed, so routing bed-to-post for one of those would be a search from a
        // cell to itself -- true for free, and proving nothing about anything.
        // The reference is the ward's own walking ground under the deck.
        sim::PathStep ground{0, 0, 0};
        INFO("actor ", actor.id, ' ', sim::wardTypeName(actor.type), " bed ", bed.x, ',', bed.y,
             ",z", bed.band);
        REQUIRE(groundUnder(*run.people, bed, ground));
        INFO("ground under it at ", ground.x, ',', ground.y, ",z", ground.band);
        // Folded to one bool on purpose: doctest decomposes the expression
        // inside a CHECK into a binary comparison and static_asserts on
        // anything more complicated than that.
        const bool sameCell =
            bed.x == ground.x && bed.y == ground.y && bed.band == ground.band;
        REQUIRE_FALSE(sameCell);

        // DOWN, which is the direction nothing else in the build checks.
        REQUIRE(finder.find(bed, ground, 0, route, sim::Gait::Climb));
        sim::PathStep at = bed;
        for (const sim::PathStep& hop : route) {
            INFO("down-hop to ", hop.x, ',', hop.y, ",z", hop.band, " from ", at.x, ',', at.y,
                 ",z", at.band);
            CHECK(legalHop(run.docks.tiles, at, hop));
            at = hop;
        }
        // And up again.
        REQUIRE(finder.find(ground, bed, 0, route, sim::Gait::Climb));
        at = ground;
        for (const sim::PathStep& hop : route) {
            INFO("up-hop to ", hop.x, ',', hop.y, ",z", hop.band, " from ", at.x, ',', at.y, ",z",
                 at.band);
            CHECK(legalHop(run.docks.tiles, at, hop));
            at = hop;
        }
    }
    INFO("roof tenants whose round trip was planned");
    CHECK(checked > 0);
}

TEST_CASE("the Skyrunners live on a deck, not in a ground condo") {
    // DOCKS-GAZETTEER section 2.5: rooftops are the burglar's highway precisely
    // because they are socially unseemly. Section 3.1 files K35 The Skyrunner's
    // Roost as "a concealed nook on the Gullet Compound's rooftop-slum deck,
    // reached only through a crawl-gap, not a threshold".
    //
    // THE CLAIM IS ABOUT ALTITUDE AND NOT ABOUT WHICH VERB GETS YOU THERE, and
    // that distinction is the honest one: some of the ward's roof huts stand on
    // decks the S4 vertical pass re-connected by stair (section 2.6) and some
    // stand on the roof-slum plane, which is climb-only. A Skyrunner asleep in
    // a courtyard condo is the thing canon rules out, and the count that says
    // so is thieves bedded ABOVE the street against thieves bedded on it.
    WardRun run(2);
    std::int32_t onADeck = 0;
    std::int32_t onTheStreet = 0;
    std::int32_t climbOnly = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (actor.type != sim::WardType::Thief) {
            continue;
        }
        if (actor.homeBand >= sim::docks::kBandUpper) {
            ++onADeck;
            if (actor.homeOnTheRoof) {
                ++climbOnly;
            }
        } else if (actor.homeBand == sim::docks::kBandQuayside) {
            ++onTheStreet;
        }
    }
    INFO("thieves bedded on a deck: ", onADeck, " (", climbOnly,
         " of them reachable only by climbing) against ", onTheStreet, " at street level");
    CHECK(onADeck > 0);
    // And at least one of them keeps a bed the Watch could not follow him to,
    // which is the whole of why the roof-slums are outside the law.
    CHECK(climbOnly > 0);
}

TEST_CASE("climbing is a verb the poor have and the Watch does not") {
    // The exclusion is canon and not convenience -- section 2.5's social rule
    // is exactly why the roof-slums are outside the law -- so it is asserted
    // rather than left to a comment. A mutation that hands everybody the climb
    // turns this red.
    CHECK(sim::wardTypeClimbs(sim::WardType::Thief));
    CHECK(sim::wardTypeClimbs(sim::WardType::Urchin));
    CHECK(sim::wardTypeClimbs(sim::WardType::Wastrel));
    CHECK(sim::wardTypeClimbs(sim::WardType::Cat));
    CHECK(sim::wardTypeClimbs(sim::WardType::Stray));

    CHECK_FALSE(sim::wardTypeClimbs(sim::WardType::MilitiaWatch));
    CHECK_FALSE(sim::wardTypeClimbs(sim::WardType::Serf));
    CHECK_FALSE(sim::wardTypeClimbs(sim::WardType::Shopkeeper));
    CHECK_FALSE(sim::wardTypeClimbs(sim::WardType::PriestOfTheFlame));
}

TEST_CASE("a walker cannot reach the roof-slum plane, and a climber can") {
    // The two gaits, asked the same question about the same district. This is
    // the whole of the movement change stated as one comparison: the walking
    // rule refuses the deck (which is section 2.6's design, and why the plane
    // held zero bodies), and the climb reaches it.
    WardRun run(8);
    sim::PathFinder finder(run.docks.tiles);
    std::vector<sim::PathStep> route;

    std::int32_t decks = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (!actor.homeOnTheRoof) {
            continue;
        }
        const sim::PathStep deck{actor.homeX, actor.homeY, actor.homeBand};
        sim::PathStep ground{0, 0, 0};
        INFO("deck ", deck.x, ',', deck.y, ",z", deck.band);
        REQUIRE(groundUnder(*run.people, deck, ground));
        // The walking rule refuses it -- which is DOCKS-GAZETTEER 2.6's design
        // and the reason the plane held nobody -- and the climb does not.
        CHECK_FALSE(finder.find(ground, deck, 0, route, sim::Gait::Walk));
        CHECK(finder.find(ground, deck, 0, route, sim::Gait::Climb));
        ++decks;
    }
    CHECK(decks > 0);
}

TEST_CASE("a climb costs what a climb costs, and open ground is priced the same") {
    // TWO CLAIMS, AND THE SECOND IS THE ONE THAT PROTECTS SIX HUNDRED PEOPLE.
    //
    // A climb move is only ever offered for a neighbour the walking rule
    // already refused, so a Climb search over ground with no walls in it must
    // expand the same cells in the same order at the same costs -- otherwise
    // handing the ward's poor a climb verb would quietly re-route every walk
    // any of them ever takes. Same route, tile for tile, is how that is said.
    Docks docks;
    sim::PathFinder finder(docks.tiles);
    std::vector<sim::PathStep> walked;
    std::vector<sim::PathStep> climbed;

    // The Tarwalk, west to east along the open working spine.
    const sim::PathStep west{52, 65, sim::docks::kBandQuayside};
    const sim::PathStep east{128, 65, sim::docks::kBandQuayside};
    REQUIRE(finder.find(west, east, 7u, walked, sim::Gait::Walk));
    REQUIRE(finder.find(west, east, 7u, climbed, sim::Gait::Climb));
    REQUIRE(walked.size() == climbed.size());
    for (std::size_t i = 0; i < walked.size(); ++i) {
        INFO("step ", i);
        CHECK(walked[i].x == climbed[i].x);
        CHECK(walked[i].y == climbed[i].y);
        CHECK(walked[i].band == climbed[i].band);
    }

    // And the price is not nothing. Seven ordinary steps for a haul is what
    // makes a body walk round a warehouse it could get over -- a free climb
    // turns the roofs into the shortest path between any two points in the
    // ward, which is a district where nobody uses the roads.
    CHECK(sim::kMantleCost >= 7 * sim::kStepCostOrthogonal);
    CHECK(sim::kDropCostBase > sim::kStepCostDiagonal);
}

TEST_CASE("the roof empties when its tenants go out to work, and fills when they are back") {
    // THE RHYTHM, WHICH IS WHAT MAKES IT A HOME AND NOT A STORAGE SHELF.
    //
    // AND THE HOURS ARE THE POOR'S HOURS, not a townhouse's. The roof slum's
    // people are the ward's kerb and its bins: a wastrel's Streetlife window is
    // nine in the morning to ten at night and a child's Scavenge window is
    // seven at night to four in the morning. So the deck is at its emptiest in
    // the EVENING, when both of those are out working, and at its fullest at
    // breakfast, when both are asleep. Writing the case the other way round --
    // "full at night" -- would be asserting a middle-class day the authored job
    // windows do not describe.
    WardRun run(8);
    const std::int32_t breakfast = run.people->census().onRoofNow;
    run.people->skipToSecond(sim::hourOfDay(20));
    const std::int32_t evening = run.people->census().onRoofNow;

    INFO("bodies off the walking island at 08:00=", breakfast, " and at 20:00=", evening);
    CHECK(breakfast > evening);
    CHECK(breakfast > 0);
}

TEST_CASE("a roof tenant that went out to work climbs home again on its own legs") {
    // NOT A SETTLE. skipToSecond teleports the ward to where the hour says it
    // should be, which proves the schedule and proves nothing at all about
    // whether a body can make the journey. This one walks it.
    //
    // Nine in the evening: the wastrels are still on their kerbs and the
    // children are still on the bins. Twenty-two hundred is when RETURN_HOME's
    // night term outscores every job in the band, and from there the roof
    // tenants have to cross the ward and go up a wall to get to bed.
    WardRun run(21);
    std::vector<std::int32_t> outAtDusk;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (actor.homeOnTheRoof && !actor.atHome()) {
            outAtDusk.push_back(actor.id);
        }
    }
    INFO("roof tenants away from their beds at 21:00");
    REQUIRE_FALSE(outAtDusk.empty());
    const std::int32_t onTheDeckAtDusk = run.people->census().onRoofNow;

    // 21:00 to 22:30. An hour to finish the shift and half an hour to walk it,
    // and a body steps a tile a second.
    run.run(5400);

    // THE MEASURE IS BODIES OFF THE WALKING ISLAND, which is a thing that can
    // only have happened by climbing: nothing in this run teleports anybody,
    // and the only route onto a deck is up a wall.
    const std::int32_t onTheDeckAtBedtime = run.people->census().onRoofNow;
    std::int32_t cameHome = 0;
    for (const std::int32_t id : outAtDusk) {
        const sim::WardActor& actor = run.people->actors()[static_cast<std::size_t>(id)];
        if (actor.atHome()) {
            ++cameHome;
        }
    }
    INFO("bodies on a deck: ", onTheDeckAtDusk, " at 21:00 and ", onTheDeckAtBedtime,
         " at 22:30; of ", outAtDusk.size(), " tenants out at dusk, ", cameHome,
         " were in their own beds");
    CHECK(onTheDeckAtBedtime > onTheDeckAtDusk);
}

TEST_CASE("nobody is homed on ground they cannot reach, climber or not") {
    // The rule the population round already kept for walkers, restated now
    // that there are two kinds of body. A walker's bed is on the walking
    // island. A climber's bed is on the walking island or on the climb closure
    // hanging off it -- and never anywhere else, which is the -1 the component
    // map paints on a sealed cellar or a decorative deck with no way onto it.
    WardRun run(8);
    for (const sim::WardActor& actor : run.people->actors()) {
        INFO("actor ", actor.id, ' ', sim::wardTypeName(actor.type), " bed ", actor.homeX, ',',
             actor.homeY, ",z", actor.homeBand);
        CHECK(run.docks.tiles.standable(actor.homeX, actor.homeY, actor.homeBand));
        if (!sim::wardTypeClimbs(actor.type)) {
            CHECK_FALSE(actor.homeOnTheRoof);
        }
    }
}
