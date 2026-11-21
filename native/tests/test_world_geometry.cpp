// PHASE 4 -- WORLD GEOMETRY AND MATERIALS (docs/design/OBLIVION-PLAN.md section
// 6.5), on worlds built to contain each case and on the real Docks.
//
// Claims, each a case that goes red on its own:
//   * a face wears world-metre UVs and a surface of the atlas: a storey is
//     1.5 repeats of a two-metre surface, never one tile stretched over it;
//   * a house gets a hipped roof with eaves; a pier deck over water and a
//     street over earth get none; and the roof steps aside when the eye is on
//     the deck (the flat deck is the sim's and the Skyrunners' own);
//   * a ramp is a wedge, a stair is steps, a wall on a street has a plinth and
//     a cornice, a door has its posts, a quay has coping and bollards;
//   * the far geometry is the body and the roofs only, flat in colour, and far
//     chunks switch with hysteresis;
//   * the district stays a deterministic function of the tile map, and the
//     body of every chunk is exactly the faces it always was.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render/scene_types.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/render3d/world_surfaces.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

const content::World& docks() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    return world;
}

const render::TileAtlas& tileAtlas() {
    static const render::TileAtlas atlas = render::TileAtlas::procedural();
    return atlas;
}

std::uint16_t materialId(std::string_view id) {
    const auto ids = render::materialIds();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return 0;
}

/// A 32 x 32 world, all open, that a case fills in.
struct Stage {
    content::World world{content::Coords(1, 1, 2), content::LaneLayout::core()};
    std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
    std::span<std::uint16_t> materials = world.shortLane(content::kMaterialLane);
    std::span<std::uint16_t> fluid = world.shortLane(content::kFluidLane);

    Stage() { std::fill(forms.begin(), forms.end(), static_cast<std::uint8_t>(content::TileForm::Open)); }

    void put(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y, std::int32_t z,
             content::TileForm form, std::string_view material) {
        forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
        materials[tiles.index(x, y, z)] = materialId(material);
    }
};

ChunkGeometry build(const sim::TileQuery& tiles, ChunkKey key, Detail detail = Detail::Near) {
    static const ChunkMaterials materials = ChunkMaterials::fromAtlas(tileAtlas());
    return buildChunkGeometry(tiles, tileAtlas(), materials, key, 0, detail);
}

float maxY(const SurfaceArrays& a) {
    float m = -1.0e9F;
    for (std::size_t i = 1; i < a.positions.size(); i += 3) {
        m = std::max(m, a.positions[i]);
    }
    return m;
}
float minY(const SurfaceArrays& a) {
    float m = 1.0e9F;
    for (std::size_t i = 1; i < a.positions.size(); i += 3) {
        m = std::min(m, a.positions[i]);
    }
    return m;
}

/// A brick house, 6 x 5 tiles, on a floor at level 5: walls round the edge at
/// level 5, a room of floor inside, and a thatch-free reman roof deck over all
/// of it at level 6 with sky over that.
void house(Stage& stage, const sim::TileQuery& tiles, std::int32_t x0, std::int32_t y0) {
    for (std::int32_t y = y0 - 1; y <= y0 + 5; ++y) {
        for (std::int32_t x = x0 - 1; x <= x0 + 6; ++x) {
            stage.put(tiles, x, y, 5, content::TileForm::Floor, "granite");
        }
    }
    for (std::int32_t y = y0; y < y0 + 5; ++y) {
        for (std::int32_t x = x0; x < x0 + 6; ++x) {
            const bool edge = x == x0 || x == x0 + 5 || y == y0 || y == y0 + 4;
            if (edge) {
                stage.put(tiles, x, y, 5, content::TileForm::Wall, "brick");
            }
            stage.put(tiles, x, y, 6, content::TileForm::Floor, "reman_concrete");
        }
    }
}

}  // namespace

TEST_CASE("a face wears world-metre UVs on a surface of the atlas, not one tile stretched over the storey") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    stage.put(tiles, 10, 10, 5, content::TileForm::Wall, "granite");
    const ChunkGeometry geometry = build(tiles, ChunkKey{0, 0});
    REQUIRE(geometry.faces.size() >= 5);
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(tileAtlas());
    const std::array<float, 2> ashlar = materials.surfaces().origin(Surface::GraniteAshlar);
    bool sawSide = false;
    for (const ChunkFace& face : geometry.faces) {
        if (face.x != 10 || face.y != 10 || face.dir != FaceDir::South) {
            continue;
        }
        sawSide = true;
        float vLow = 1.0e9F;
        float vHigh = -1.0e9F;
        float uLow = 1.0e9F;
        float uHigh = -1.0e9F;
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t v = face.firstVertex + c;
            vLow = std::min(vLow, geometry.texcoords[v * 2 + 1]);
            vHigh = std::max(vHigh, geometry.texcoords[v * 2 + 1]);
            uLow = std::min(uLow, geometry.texcoords[v * 2]);
            uHigh = std::max(uHigh, geometry.texcoords[v * 2]);
            CHECK(geometry.texcoords2[v * 2] == doctest::Approx(ashlar[0]));
            CHECK(geometry.texcoords2[v * 2 + 1] == doctest::Approx(ashlar[1]));
        }
        // A tile is a metre; a surface repeats every two; a storey is three.
        CHECK(uHigh - uLow == doctest::Approx(1.0F / static_cast<float>(kSurfaceMetres)));
        CHECK(vHigh - vLow == doctest::Approx(render::kBandHeight / static_cast<float>(kSurfaceMetres)));
    }
    CHECK(sawSide);
    // The same wall one tile along continues the same surface: u carries on.
    stage.put(tiles, 11, 10, 5, content::TileForm::Wall, "granite");
    const ChunkGeometry two = build(tiles, ChunkKey{0, 0});
    float lastEnd = -1.0F;
    float nextStart = -1.0F;
    for (const ChunkFace& face : two.faces) {
        if (face.y == 10 && face.z == 5 && face.dir == FaceDir::South) {
            const float u0 = std::min({two.texcoords[face.firstVertex * 2], two.texcoords[(face.firstVertex + 1) * 2]});
            const float u1 = std::max({two.texcoords[face.firstVertex * 2], two.texcoords[(face.firstVertex + 1) * 2]});
            if (face.x == 10) {
                lastEnd = u1;
            } else if (face.x == 11) {
                nextStart = u0;
            }
        }
    }
    CHECK(lastEnd == doctest::Approx(nextStart));
}

TEST_CASE("a house gets a hipped roof with eaves, and a pier deck and a street get none") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    house(stage, tiles, 8, 8);
    // A street: a wide floor at level 5 over earth at level 4, nothing above.
    for (std::int32_t y = 20; y < 26; ++y) {
        for (std::int32_t x = 4; x < 28; ++x) {
            stage.put(tiles, x, y, 4, content::TileForm::Wall, "dirt");
            stage.put(tiles, x, y, 5, content::TileForm::Floor, "granite");
        }
    }
    // A pier: timber deck at level 5 over water at level 4.
    for (std::int32_t y = 27; y < 29; ++y) {
        for (std::int32_t x = 4; x < 12; ++x) {
            stage.put(tiles, x, y, 5, content::TileForm::Floor, "oak");
            stage.fluid[tiles.index(x, y, 4)] = 7;
        }
    }
    const ChunkGeometry geometry = build(tiles, ChunkKey{0, 0});
    const ChunkGeometry southChunk = build(tiles, ChunkKey{0, 1});
    CHECK_FALSE(geometry.truncated);
    // Exactly the house's roof, on the level of its deck.
    REQUIRE(geometry.roofs.size() == 1);
    const RoofLayer& roof = geometry.roofs.front();
    CHECK(roof.level == 6);
    CHECK(roof.deckY == doctest::Approx(render::bandSurface(6)));
    CHECK(roof.triangleCount() > 60);
    CHECK(southChunk.roofs.empty());
    // It rises from the eaves (a little under the deck) to a ridge above it,
    // and no further than a storey over it.
    CHECK(minY(roof) < roof.deckY);
    CHECK(maxY(roof) > roof.deckY + 1.0F);
    CHECK(maxY(roof) < roof.deckY + 4.0F);
    // The eaves stand proud of the house: a vertex past its wall line.
    float minX = 1.0e9F;
    for (std::size_t i = 0; i < roof.positions.size(); i += 3) {
        minX = std::min(minX, roof.positions[i]);
    }
    CHECK(minX < 8.0F - 0.2F);
    // Its colour is its light: every vertex names the deck cell it was cut from.
    for (std::size_t v = 0; v < roof.vertexCount(); ++v) {
        CHECK(roof.cells[v * 3 + 2] == 6);
    }
    // The body is what it always was: the roof is not in it.
    CHECK(maxY(geometry) <= render::bandSurface(6) + 0.001F);
}

TEST_CASE("the roof steps aside when the eye stands on its deck") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    house(stage, tiles, 8, 8);
    WorldScene world(tiles, tileAtlas(), nullptr);
    WorldSceneParams params;
    params.timeOfDaySeconds = 12 * 3600;
    SceneDescription scene;
    render::Camera eye;
    eye.x = 20.5F;
    eye.y = 12.5F;
    eye.z = render::bandSurface(5) + 1.7F;  // in the street at level 5
    eye.hfovTan = 1.0F;
    world.refresh(scene, eye, 1.7F, params);
    CHECK(world.stats().roofsInstanced == 1);
    // On the deck: the eye is a person's height over level 6's surface.
    eye.x = 10.5F;
    eye.y = 10.5F;
    eye.z = render::bandSurface(6) + 1.7F;
    world.refresh(scene, eye, 1.7F, params);
    CHECK(world.stats().roofsInstanced == 0);
    // Roofs off: none, at the street too.
    params.pitchedRoofs = false;
    eye.z = render::bandSurface(5) + 1.7F;
    world.refresh(scene, eye, 1.7F, params);
    CHECK(world.stats().roofsInstanced == 0);
}

TEST_CASE("a ramp is a wedge against its terrace, and a stair is steps") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    // A street at level 5, a terrace of earth wall at level 5 beyond it with a
    // floor at level 6 over it, and a ramp between them.
    for (std::int32_t x = 6; x < 12; ++x) {
        stage.put(tiles, x, 10, 5, content::TileForm::Floor, "granite");
        stage.put(tiles, x, 12, 5, content::TileForm::Wall, "dirt");
        stage.put(tiles, x, 12, 6, content::TileForm::Floor, "granite");
    }
    stage.put(tiles, 8, 11, 5, content::TileForm::Ramp, "dirt");
    // A stair against a landing to the east.
    stage.put(tiles, 20, 20, 5, content::TileForm::Stair, "oak");
    stage.put(tiles, 21, 20, 6, content::TileForm::Floor, "oak");
    const ChunkGeometry geometry = build(tiles, ChunkKey{0, 0});
    const ChunkGeometry stairChunk = build(tiles, ChunkKey{1, 1});
    const float deck = render::bandSurface(5);
    // The wedge's high edge is over the deck by the rise and its low edge on it.
    REQUIRE(geometry.extra.triangleCount() >= 4);
    CHECK(maxY(geometry.extra) == doctest::Approx(deck + 1.2F));
    // The wedge's top is a slope, not a flat or a wall.
    bool slope = false;
    for (std::size_t v = 0; v < geometry.extra.vertexCount(); ++v) {
        const float ny = geometry.extra.normals[v * 3 + 1];
        if (ny > 0.2F && ny < 0.99F) {
            slope = true;
        }
    }
    CHECK(slope);
    // The slab under it is still the flat walking surface the sim means.
    bool flatTop = false;
    for (const ChunkFace& face : geometry.faces) {
        if (face.x == 8 && face.y == 11 && face.z == 5 && face.dir == FaceDir::Top) {
            flatTop = true;
            CHECK(geometry.positions[face.firstVertex * 3 + 1] == doctest::Approx(deck));
        }
    }
    CHECK(flatTop);
    // The stair: six steps, each a box, climbing to the same rise.
    REQUIRE(stairChunk.extra.triangleCount() >= 6 * 2 * 3);
    CHECK(maxY(stairChunk.extra) == doctest::Approx(deck + 1.2F));
    std::set<int> heights;
    for (std::size_t i = 1; i < stairChunk.extra.positions.size(); i += 3) {
        heights.insert(static_cast<int>(std::lround((stairChunk.extra.positions[i] - deck) * 100.0F)));
    }
    CHECK(heights.size() >= 7);
}

TEST_CASE("a wall on a street has a plinth and a cornice, and none where it stands in a room") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    house(stage, tiles, 8, 8);
    const ChunkGeometry geometry = build(tiles, ChunkKey{0, 0});
    const float foot = render::bandSurface(5);
    const float head = foot + render::kBandHeight;
    // Plinth boxes: quads between the foot and half a metre up, proud of the
    // brick face; cornice boxes: quads at the wall head.
    std::size_t plinthQuads = 0;
    std::size_t corniceQuads = 0;
    for (std::size_t q = 0; q + 3 < geometry.extra.vertexCount(); q += 4) {
        const float y0 = geometry.extra.positions[q * 3 + 1];
        const float y1 = geometry.extra.positions[(q + 2) * 3 + 1];
        const float low = std::min(y0, y1);
        const float high = std::max(y0, y1);
        if (low >= foot - 0.001F && high <= foot + 0.52F && high - low > 0.3F) {
            ++plinthQuads;
        }
        if (high <= head + 0.001F && high >= head - 0.001F && high - low > 0.1F && high - low < 0.4F) {
            ++corniceQuads;
        }
    }
    CHECK(plinthQuads >= 14);
    CHECK(corniceQuads >= 14);
}

TEST_CASE("a door has its timber posts, a ceiling its beams, a quay its coping and bollards") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    // A wall line along x at y=10, level 5, a two-cell door gap at x=14..15,
    // street to the north (open), a roofed room to the south.
    for (std::int32_t x = 9; x <= 20; ++x) {
        for (std::int32_t y = 9; y <= 14; ++y) {
            stage.put(tiles, x, y, 4, content::TileForm::Wall, "dirt");  // ground under it all
        }
    }
    for (std::int32_t x = 10; x < 20; ++x) {
        stage.put(tiles, x, 10, 5, content::TileForm::Wall, "brick");
        stage.put(tiles, x, 9, 5, content::TileForm::Floor, "granite");
        for (std::int32_t y = 11; y < 14; ++y) {
            stage.put(tiles, x, y, 5, content::TileForm::Floor, "oak");
            stage.put(tiles, x, y, 6, content::TileForm::Floor, "oak");
        }
        stage.put(tiles, x, 14, 5, content::TileForm::Wall, "brick");
    }
    for (std::int32_t x : {14, 15}) {
        stage.forms[tiles.index(x, 10, 5)] = static_cast<std::uint8_t>(content::TileForm::Floor);
    }
    stage.put(tiles, 9, 10, 5, content::TileForm::Wall, "brick");
    stage.put(tiles, 20, 10, 5, content::TileForm::Wall, "brick");
    stage.put(tiles, 9, 9, 5, content::TileForm::Floor, "granite");
    stage.put(tiles, 20, 9, 5, content::TileForm::Floor, "granite");
    const ChunkGeometry house = build(tiles, ChunkKey{0, 0});
    // Posts: boxes the height of the door's head.
    std::size_t posts = 0;
    for (std::size_t q = 0; q + 3 < house.extra.vertexCount(); q += 4) {
        const float a = house.extra.positions[q * 3 + 1];
        const float b = house.extra.positions[(q + 2) * 3 + 1];
        if (std::abs(std::abs(a - b) - 2.03F) < 0.01F) {
            ++posts;
        }
    }
    CHECK(posts >= 4);
    // Beams: under the level-6 slab over the room, every second line.
    std::size_t beams = 0;
    for (std::size_t q = 0; q + 3 < house.extra.vertexCount(); q += 4) {
        const float n = house.extra.normals[q * 3 + 1];
        const float y = house.extra.positions[q * 3 + 1];
        if (n < -0.9F && y < render::bandSurface(6) - 0.2F && y > render::bandSurface(6) - 0.4F) {
            ++beams;
        }
    }
    CHECK(beams >= 6);

    // A quay: a stone deck at level 5 over water at level 4.
    Stage quay;
    const sim::TileQuery quayTiles(quay.world);
    for (std::int32_t x = 2; x < 30; ++x) {
        quay.put(quayTiles, x, 10, 5, content::TileForm::Floor, "granite");
        quay.put(quayTiles, x, 12, 4, content::TileForm::Open, "granite");
        quay.fluid[quayTiles.index(x, 11, 4)] = 7;
        quay.fluid[quayTiles.index(x, 12, 4)] = 7;
    }
    const ChunkGeometry water = build(quayTiles, ChunkKey{0, 0});
    const ChunkGeometry waterEast = build(quayTiles, ChunkKey{1, 0});
    CHECK(water.extra.triangleCount() + waterEast.extra.triangleCount() > 40);
    // A bollard is half a metre of iron over the deck.
    const float deck = render::bandSurface(5);
    bool bollard = false;
    for (const SurfaceArrays* a : {&water.extra, &waterEast.extra}) {
        if (maxY(*a) > deck + 0.55F && maxY(*a) < deck + 0.7F) {
            bollard = true;
        }
    }
    CHECK(bollard);
}

TEST_CASE("the far geometry is the body and the roofs, flat in colour; chunks switch with hysteresis") {
    Stage stage;
    const sim::TileQuery tiles(stage.world);
    house(stage, tiles, 8, 8);
    const ChunkGeometry near = build(tiles, ChunkKey{0, 0}, Detail::Near);
    const ChunkGeometry far = build(tiles, ChunkKey{0, 0}, Detail::Far);
    CHECK(far.faces.size() == near.faces.size());
    CHECK(far.extra.vertexCount() == 0);
    CHECK(far.roofs.size() == near.roofs.size());
    CHECK(far.totalTriangles() < near.totalTriangles());
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(tileAtlas());
    const std::array<float, 2> white = materials.surfaces().origin(Surface::White);
    for (std::size_t v = 0; v < far.vertexCount(); ++v) {
        CHECK(far.texcoords2[v * 2] == doctest::Approx(white[0]));
    }
    ChunkLighting noon;
    CHECK(colourChunk(far, noon).id == farChunkMeshId(ChunkKey{0, 0}));
    CHECK(colourChunk(near, noon).id == chunkMeshId(ChunkKey{0, 0}));
    CHECK(colourChunk(far, noon).version != colourChunk(near, noon).version);

    // The scene: the house is near with the eye beside it and far from across
    // the world; it keeps its level between the two distances.
    WorldScene world(tiles, tileAtlas(), nullptr);
    WorldSceneParams params;
    params.timeOfDaySeconds = 12 * 3600;
    params.chunkDistance = 400.0F;
    params.farDistance = 12.0F;
    params.nearDistance = 6.0F;
    SceneDescription scene;
    render::Camera eye;
    eye.hfovTan = 1.0F;
    eye.z = render::bandSurface(5) + 1.7F;
    const auto farAt = [&](float x) {
        eye.x = x;
        eye.y = 11.0F;
        world.refresh(scene, eye, 1.7F, params);
        return world.stats().chunksFar;
    };
    // The house and its eaves reach x = 14.4: a chunk's distance is to that box.
    CHECK(farAt(10.0F) == 0);   // inside it
    CHECK(farAt(24.0F) == 0);   // 9.6 off: between the thresholds, keeps near
    CHECK(farAt(28.0F) == 1);   // 13.6 off: past farDistance
    CHECK(scene.findMesh(farChunkMeshId(ChunkKey{0, 0})) != nullptr);
    CHECK(scene.findMesh(chunkMeshId(ChunkKey{0, 0})) == nullptr);
    CHECK(farAt(22.0F) == 1);   // 7.6 off: between the thresholds, keeps far
    CHECK(farAt(18.0F) == 0);   // 3.6 off: within nearDistance, near again
    CHECK(scene.findMesh(chunkMeshId(ChunkKey{0, 0})) != nullptr);
    CHECK(scene.findMesh(farChunkMeshId(ChunkKey{0, 0})) == nullptr);
}

TEST_CASE("the Docks: every chunk meshes with its detail, the roofs are the buildings', and nothing truncates") {
    const sim::TileQuery tiles(docks());
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(tileAtlas());
    std::size_t roofLayers = 0;
    std::size_t roofTriangles = 0;
    std::size_t extraTriangles = 0;
    std::size_t bodyTriangles = 0;
    std::size_t largest = 0;
    for (std::int32_t cy = 0; cy < kChunksDown; ++cy) {
        for (std::int32_t cx = 0; cx < kChunksAcross; ++cx) {
            const ChunkGeometry g = buildChunkGeometry(tiles, tileAtlas(), materials, ChunkKey{cx, cy}, 0);
            CHECK_FALSE(g.truncated);
            bodyTriangles += g.triangleCount();
            extraTriangles += g.extra.triangleCount();
            largest = std::max(largest, g.vertexCount() + g.extra.vertexCount());
            for (const RoofLayer& roof : g.roofs) {
                ++roofLayers;
                roofTriangles += roof.triangleCount();
                CHECK(roof.vertexCount() <= 65535);
                // A roof sits on its deck: no lower than the eaves' drop, no
                // higher than a storey over it.
                CHECK(minY(roof) >= roof.deckY - 1.0F);
                CHECK(maxY(roof) <= roof.deckY + 4.0F);
                for (std::size_t v = 0; v < roof.vertexCount(); ++v) {
                    REQUIRE(roof.cells[v * 3 + 2] == roof.level);
                }
            }
        }
    }
    MESSAGE("Docks: body " << bodyTriangles << " triangles, detail " << extraTriangles << ", roofs "
                           << roofTriangles << " over " << roofLayers << " layers; largest chunk "
                           << largest << " vertices");
    CHECK(roofLayers >= 20);
    CHECK(roofTriangles > 2000);
    CHECK(extraTriangles > 3000);
    CHECK(largest < 65535);
}

TEST_CASE("the skyline ring climbs to the Inner Wall in the south and is low sea in the north") {
    constexpr float kPi = 3.14159265358979323846F;
    const float south = skylineHeight(kPi * 0.5F);
    const float north = skylineHeight(kPi * 1.5F);
    CHECK(south > 25.0F);
    CHECK(north < 12.0F);
    // The palace is a tall thin spire: far above its own surroundings.
    const float palaceAngle = kPi * 0.5F - 0.95F;
    CHECK(skylineHeight(palaceAngle) > skylineHeight(palaceAngle + 0.1F) + 25.0F);
    const MeshData ring = buildSkyline(12 * 3600, render::Weather{});
    CHECK(ring.id == kSkylineMeshId);
    CHECK(ring.vertexCount() == 720);
    for (std::size_t i = 0; i + 2 < ring.positions.size(); i += 3) {
        const float d = std::sqrt(ring.positions[i] * ring.positions[i] + ring.positions[i + 2] * ring.positions[i + 2]);
        REQUIRE(d == doctest::Approx(kSkylineRadius).epsilon(0.001));
    }
    // Hazed by the hour: darker at night.
    const MeshData night = buildSkyline(0, render::Weather{});
    long dayLight = 0;
    long nightLight = 0;
    for (std::size_t i = 0; i < ring.colours.size(); ++i) {
        if ((i & 3U) != 3U) {
            dayLight += ring.colours[i];
            nightLight += night.colours[i];
        }
    }
    CHECK(nightLight < dayLight);
}
