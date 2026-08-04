// The movement/sight questions, asked of the owner's real baked Docks.
//
// Nothing here is synthetic. Every expected number was read out of
// content/maps/baked/docks_surface.trojsav, which is why these cases can catch
// a walkability rule that is self-consistent and wrong about the actual map.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const content::World& docksWorld() {
    // One load for the whole file. The reader is proven stateless by
    // content's own "the same world loads identically twice", so sharing it
    // costs nothing and saves ~1.5M tiles of decode per case.
    static const content::World world =
        content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

}  // namespace

TEST_CASE("Q8 sub-tile coordinates floor rather than truncate") {
    CHECK(q8_of_tile(0) == 0);
    CHECK(q8_of_tile(1) == 256);
    CHECK(q8_tile(0) == 0);
    CHECK(q8_tile(255) == 0);
    CHECK(q8_tile(256) == 1);
    CHECK(q8_tile(-1) == -1);    // NOT 0: tile -1 owns -256..-1
    CHECK(q8_tile(-256) == -1);
    CHECK(q8_tile(-257) == -2);
    CHECK(q8_sub(-1) == 255);
    CHECK(q8_tile_centre(7) == 7 * 256 + 128);
    // The invariant the whole scheme rests on.
    for (std::int32_t p = -1000; p <= 1000; ++p) {
        REQUIRE(q8_tile(p) * 256 + q8_sub(p) == p);
    }
}

TEST_CASE("the query reports the docks' real dimensions") {
    const TileQuery tiles(docksWorld());
    CHECK(tiles.sizeX() == 256);
    CHECK(tiles.sizeY() == 192);
    CHECK(tiles.sizeZ() == 32);
}

TEST_CASE("out of the world reads as VOID and blocks") {
    const TileQuery tiles(docksWorld());
    CHECK(tiles.form(-1, 0, 19) == content::TileForm::Void);
    CHECK(tiles.form(0, -1, 19) == content::TileForm::Void);
    CHECK(tiles.form(0, 0, -1) == content::TileForm::Void);
    CHECK(tiles.form(tiles.sizeX(), 0, 19) == content::TileForm::Void);
    CHECK(tiles.form(0, tiles.sizeY(), 19) == content::TileForm::Void);
    CHECK(tiles.form(0, 0, tiles.sizeZ()) == content::TileForm::Void);
    CHECK(tiles.solid(-1, 0, 19));
    CHECK_FALSE(tiles.walkable(-1, 0, 19));
    CHECK(tiles.fluidDepth(-1, 0, 19) == 0);
}

TEST_CASE("indexing agrees with the world's own lane addressing") {
    const TileQuery tiles(docksWorld());
    const content::World& world = docksWorld();
    // Walk a diagonal through the interior and check the two paths to a tile
    // agree. A bit-math slip here would move the whole map by a chunk.
    for (std::int32_t i = 0; i < 128; ++i) {
        const std::int32_t x = 32 + i;
        const std::int32_t y = 32 + i;
        const std::int32_t z = 8 + (i % 24);
        const std::int32_t chunkIndex = world.coords().chunkIndexOf(x >> 5, y >> 5, z >> 3);
        const std::int32_t localIdx = content::Coords::localIdxOf(x & 31, y & 31, z & 7);
        REQUIRE(tiles.index(x, y, z) ==
                content::World::tileIndex(static_cast<std::size_t>(chunkIndex),
                                          static_cast<std::size_t>(localIdx)));
        REQUIRE(tiles.form(x, y, z) == world.form(tiles.index(x, y, z)));
    }
}

TEST_CASE("OPEN is not walkable, which is the rule everyone gets wrong first") {
    const TileQuery tiles(docksWorld());
    // Straight above the harbour: OPEN air over deep water. Treating OPEN as
    // ground would let the player stroll out over the water at quay height.
    REQUIRE(tiles.form(120, 36, 19) == content::TileForm::Open);
    CHECK_FALSE(tiles.walkable(120, 36, 19));
    CHECK_FALSE(tiles.solid(120, 36, 19));  // and it does not block sight either
}

TEST_CASE("deep water blocks a walkable form") {
    const TileQuery tiles(docksWorld());
    // The harbour proper: 7,382 cells at depth 7.
    std::size_t deep = 0;
    std::size_t deepAndWalkableForm = 0;
    for (std::int32_t z = 0; z < tiles.sizeZ(); ++z) {
        for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
            for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
                if (tiles.fluidDepth(x, y, z) >= kBlockingFluidDepth) {
                    ++deep;
                    const content::TileForm f = tiles.form(x, y, z);
                    if (f == content::TileForm::Floor || f == content::TileForm::Ramp ||
                        f == content::TileForm::Stair) {
                        ++deepAndWalkableForm;
                        REQUIRE_FALSE(tiles.walkable(x, y, z));
                    }
                }
            }
        }
    }
    // depth 4 (8 cells) + depth 7 (7,382 cells) from the baked FLUID lane.
    CHECK(deep == 7390);
    // And the rule is not vacuous: some of those really are floor tiles that
    // have gone under.
    CHECK(deepAndWalkableForm > 0);
}

TEST_CASE("headroom refuses the world's own ceiling") {
    // THE S2 REVIEW'S FIRST FINDING, closed. It mutated headroom() to
    // `return true` and the entire 228-test gate stayed green, because the
    // shipped Docks contains no cell that is walkable with VOID directly above
    // it -- so over that map the rule and `true` really are the same function
    // and no count re-derived from those bytes can tell them apart.
    //
    // Both halves are asserted here. First the rule, on a world built to
    // contain the case; then the fact about the district, out loud, so the day
    // the map gains such a cell this comment stops being true noisily.

    SUBCASE("a floor under the void border is not somewhere a body can be") {
        // Three chunks each way: 96 x 96 x 24 tiles, all OPEN to begin with.
        content::World world(content::Coords(3, 3, 3), content::LaneLayout::core());
        const std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
        std::fill(forms.begin(), forms.end(),
                  static_cast<std::uint8_t>(content::TileForm::Open));
        const TileQuery tiles(world);
        REQUIRE(tiles.sizeX() == 96);
        REQUIRE(tiles.sizeZ() == 24);

        const auto put = [&](std::int32_t x, std::int32_t y, std::int32_t z,
                             content::TileForm form) {
            forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
        };

        // A floor with air above it: standable, obviously.
        put(40, 40, 10, content::TileForm::Floor);
        put(40, 40, 11, content::TileForm::Open);
        REQUIRE(tiles.walkable(40, 40, 10));
        CHECK(tiles.headroom(40, 40, 10));
        CHECK(tiles.standable(40, 40, 10));

        // A floor with a WALL above it: still standable. This is the S2
        // correction -- a wall at z+1 starts a whole tile above the feet, which
        // is what sits over every doorway in a two-storey building, and refusing
        // it sealed every interior in the district.
        put(41, 40, 10, content::TileForm::Floor);
        put(41, 40, 11, content::TileForm::Wall);
        REQUIRE(tiles.walkable(41, 40, 10));
        CHECK(tiles.headroom(41, 40, 10));
        CHECK(tiles.standable(41, 40, 10));

        // A floor with VOID above it: walkable, and NOT standable. This is the
        // case the shipped map does not have, and the case `return true` cannot
        // survive.
        put(42, 40, 10, content::TileForm::Floor);
        put(42, 40, 11, content::TileForm::Void);
        REQUIRE(tiles.walkable(42, 40, 10));
        CHECK_FALSE(tiles.headroom(42, 40, 10));
        CHECK_FALSE(tiles.standable(42, 40, 10));

        // ...and the top of the world is the same case by another route: out of
        // bounds reads as VOID, so nothing stands on the last level.
        put(43, 40, tiles.sizeZ() - 1, content::TileForm::Floor);
        REQUIRE(tiles.walkable(43, 40, tiles.sizeZ() - 1));
        CHECK_FALSE(tiles.standable(43, 40, tiles.sizeZ() - 1));

        // And the rule reaches the movement model, not just the query: a step
        // onto that cell is refused at every band.
        CHECK(tiles.stepBand(41, 40, 10, 42, 40) == TileQuery::kNoBand);
        CHECK(tiles.stepBand(40, 40, 10, 41, 40) == 10);
    }

    SUBCASE("the shipped Docks has no such cell, which is why the counts cannot see it") {
        const TileQuery tiles(docksWorld());
        std::size_t walkableUnderVoid = 0;
        for (std::int32_t z = 0; z < tiles.sizeZ(); ++z) {
            for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
                for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
                    if (tiles.walkable(x, y, z) && !tiles.headroom(x, y, z)) {
                        ++walkableUnderVoid;
                    }
                }
            }
        }
        // Zero, today. If a re-bake ever authors ground up against the border
        // ring this goes red, and the note in tile_query.hpp needs rewriting
        // rather than quietly becoming false.
        CHECK(walkableUnderVoid == 0);
    }
}

TEST_CASE("the three walk bands are where the gazetteer says they are") {
    const TileQuery tiles(docksWorld());
    std::map<std::int32_t, std::size_t> standableByBand;
    for (std::int32_t z = 0; z < tiles.sizeZ(); ++z) {
        for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
            for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
                if (tiles.standable(x, y, z)) {
                    ++standableByBand[z];
                }
            }
        }
    }
    // Quayside is by far the largest plane, and each band up is smaller --
    // the district climbs and narrows, exactly as authored.
    CHECK(standableByBand[docks::kBandQuayside] > standableByBand[docks::kBandMidSlope]);
    CHECK(standableByBand[docks::kBandMidSlope] > standableByBand[docks::kBandUpper]);
    CHECK(standableByBand[docks::kBandQuayside] == docks::kStandableOnQuayside);
    CHECK(standableByBand[docks::kBandMidSlope] == docks::kStandableOnMidSlope);
    CHECK(standableByBand[docks::kBandUpper] == docks::kStandableOnUpper);
    // Nothing stands above the roofline.
    CHECK(standableByBand[23] == 0);
}

TEST_CASE("a step up needs a ramp or a stair, and mostly there is not one") {
    const TileQuery tiles(docksWorld());

    // For every place where the neighbouring column's only footing is one level
    // UP -- a kerb, a wall top, a warehouse floor above -- ask whether the rule
    // lets the body climb it. If the climb clause were dropped, everything in
    // the district would become a staircase; if ramps stopped counting, nothing
    // would.
    std::size_t allowed = 0;
    std::size_t refused = 0;
    for (std::int32_t z = 18; z <= 21; ++z) {
        for (std::int32_t y = 1; y < tiles.sizeY() - 1; ++y) {
            for (std::int32_t x = 1; x < tiles.sizeX() - 1; ++x) {
                if (!tiles.standable(x, y, z)) {
                    continue;
                }
                const std::int32_t nx[4] = {x + 1, x - 1, x, x};
                const std::int32_t ny[4] = {y, y, y + 1, y - 1};
                for (int d = 0; d < 4; ++d) {
                    if (tiles.standable(nx[d], ny[d], z) || tiles.standable(nx[d], ny[d], z - 1)) {
                        continue;  // level ground or a drop; not a climb at all
                    }
                    if (!tiles.standable(nx[d], ny[d], z + 1)) {
                        continue;  // nothing up there either
                    }
                    if (tiles.stepBand(x, y, z, nx[d], ny[d]) == z + 1) {
                        ++allowed;
                        REQUIRE((tiles.climbable(x, y, z) ||
                                 tiles.climbable(nx[d], ny[d], z + 1)));
                    } else {
                        ++refused;
                    }
                }
            }
        }
    }
    // S1: 110 / 5,035, before the headroom correction made doorway lintels
    // standable (tile_query.hpp). Both sides moved and the ratio did not.
    CHECK(allowed == 109);
    CHECK(refused == 4849);
    // The point: the overwhelming majority of one-level rises in this district
    // are walls, and the body cannot walk up them.
    CHECK(refused > allowed * 40);
}

TEST_CASE("the whole district is reachable from the spawn, all three bands") {
    const TileQuery tiles(docksWorld());
    REQUIRE(tiles.standable(docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand));

    // A flood fill over exactly the movement rule -- four-way steps, each
    // resolved by stepBand. This is the acceptance claim "the three walk bands
    // are reachable via ramps and stairs", counted rather than asserted.
    struct Cell {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
    };
    const std::size_t span =
        static_cast<std::size_t>(tiles.sizeX()) * static_cast<std::size_t>(tiles.sizeY()) *
        static_cast<std::size_t>(tiles.sizeZ());
    std::vector<char> seen(span, 0);
    const auto slot = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        return (static_cast<std::size_t>(z) * static_cast<std::size_t>(tiles.sizeY()) +
                static_cast<std::size_t>(y)) *
                   static_cast<std::size_t>(tiles.sizeX()) +
               static_cast<std::size_t>(x);
    };

    std::vector<Cell> frontier;
    frontier.push_back({docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand});
    seen[slot(docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand)] = 1;
    std::map<std::int32_t, std::int32_t> byBand;
    std::int32_t total = 0;

    while (!frontier.empty()) {
        const Cell cell = frontier.back();
        frontier.pop_back();
        ++total;
        ++byBand[cell.z];
        const std::int32_t nx[4] = {cell.x + 1, cell.x - 1, cell.x, cell.x};
        const std::int32_t ny[4] = {cell.y, cell.y, cell.y + 1, cell.y - 1};
        for (int d = 0; d < 4; ++d) {
            const std::int32_t nz = tiles.stepBand(cell.x, cell.y, cell.z, nx[d], ny[d]);
            if (nz == TileQuery::kNoBand) {
                continue;
            }
            if (seen[slot(nx[d], ny[d], nz)] != 0) {
                continue;
            }
            seen[slot(nx[d], ny[d], nz)] = 1;
            frontier.push_back({nx[d], ny[d], nz});
        }
    }

    CHECK(total == docks::kReachableFromSpawn);
    CHECK(byBand[docks::kBandQuayside] == docks::kReachableOnQuayside);
    CHECK(byBand[docks::kBandMidSlope] == docks::kReachableOnMidSlope);
    CHECK(byBand[docks::kBandUpper] == docks::kReachableOnUpper);
    CHECK(byBand[docks::kBandQuayside - 1] == docks::kReachableBelowQuay);
    // All three named bands, from one spawn, with no teleporting.
    CHECK(byBand.size() == 4);
}

TEST_CASE("stepBand prefers level ground, then a drop, then a climb") {
    const TileQuery tiles(docksWorld());
    // Flat ground on Tarwalk: neighbouring quayside tiles keep the band.
    REQUIRE(tiles.standable(143, 61, 19));
    REQUIRE(tiles.standable(144, 61, 19));
    CHECK(tiles.stepBand(143, 61, 19, 144, 61) == 19);
    // Into the warehouse wall behind: nothing to stand on at any legal band.
    REQUIRE(tiles.solid(143, 66, 19));
    CHECK(tiles.stepBand(143, 65, 19, 143, 66) == TileQuery::kNoBand);
}
