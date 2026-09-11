// THE CHUNK MESHER, ON THE REAL DOCKS AND ON A WORLD BUILT TO CONTAIN A CASE.
//
// Three claims the 3D world rests on, each a case that goes red on its own:
//
//   * the mesh is a DETERMINISTIC FUNCTION OF THE TILE MAP -- the district
//     meshed twice from two TileQuery views of one world is the same bytes,
//     chunk for chunk, and the two SceneDescriptions hash the same. That is
//     the 3D analogue of "the same world loads identically twice", and it is
//     what lets a scene hash stand in for a frame hash.
//   * a WALL emits wall quads and a FLOOR in a street does not -- the fact
//     that keeps the building you walk into and the building you see the
//     same building (the renderer plan's second risk).
//   * the light recolours without moving a vertex, and the world scene
//     instances only what is near the eye.
//
// The procedural atlas throughout: no art tree, no licensed asset.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    return world;
}

const render::TileAtlas& proceduralAtlas() {
    static const render::TileAtlas atlas = render::TileAtlas::procedural();
    return atlas;
}

/// The authored spawn as a render camera: the quayside, looking 265
/// degrees (west, a shade south), eye at the body's height.
render::Camera spawnCamera() {
    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) +
            static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * 3.14159265358979323846F / 180.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    return eye;
}

}  // namespace

TEST_CASE("the Docks mesh is a deterministic function of the tile map") {
    const sim::TileQuery tilesA(docksWorld());
    const sim::TileQuery tilesB(docksWorld());
    const render::TileAtlas& atlas = proceduralAtlas();
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(atlas);
    REQUIRE(materials.tileCount() == atlas.tileCount());
    REQUIRE(materials.texture().id == kChunkAtlasTextureId);
    REQUIRE(materials.texture().pixels.size() ==
            static_cast<std::size_t>(materials.texture().width) *
                static_cast<std::size_t>(materials.texture().height) * 4U);

    ChunkLighting noon;
    noon.timeOfDaySeconds = 12 * 3600;

    SceneDescription a;
    SceneDescription b;
    std::size_t withGeometry = 0;
    std::size_t triangles = 0;
    std::size_t largest = 0;
    for (std::int32_t cy = 0; cy < kChunksDown; ++cy) {
        for (std::int32_t cx = 0; cx < kChunksAcross; ++cx) {
            const ChunkKey key{cx, cy};
            MeshData first = meshChunk(tilesA, atlas, materials, key, 0, noon);
            MeshData second = meshChunk(tilesB, atlas, materials, key, 0, noon);
            REQUIRE(first.id == chunkMeshId(key));
            REQUIRE(first.version == chunkVersion(0, noon));
            REQUIRE(first.vertexCount() <= 65535);
            REQUIRE(first.colours.size() == first.vertexCount() * 4);
            REQUIRE(first.texcoords.size() == first.vertexCount() * 2);
            CHECK(first.positions == second.positions);
            CHECK(first.texcoords == second.texcoords);
            CHECK(first.colours == second.colours);
            CHECK(first.indices == second.indices);
            // Every index names a vertex that exists.
            for (const std::uint16_t index : first.indices) {
                REQUIRE(index < first.vertexCount());
            }
            if (first.triangleCount() > 0) {
                ++withGeometry;
                triangles += first.triangleCount();
                largest = std::max(largest, first.vertexCount());
            }
            a.putMesh(std::move(first));
            b.putMesh(std::move(second));
        }
    }
    CHECK(sceneHash(a) == sceneHash(b));
    // The 32-tile VOID border is two chunks wide on every side: 12 x 8
    // authored chunks carry geometry and the rest carry none.
    CHECK(withGeometry >= 60);
    CHECK(withGeometry <= 96);
    CHECK(meshChunk(tilesA, atlas, materials, ChunkKey{0, 0}, 0, noon).triangleCount() == 0);
    CHECK(meshChunk(tilesA, atlas, materials, ChunkKey{1, 1}, 0, noon).triangleCount() == 0);
    // A district's worth of faces, and the guard never fired.
    CHECK(triangles > 20000);
    CHECK(largest < 65535);
    MESSAGE("Docks: " << withGeometry << " chunks with geometry, " << triangles
                      << " triangles, largest chunk " << largest << " vertices");

    // The clock moves the colours and the version, and nothing else.
    ChunkLighting dusk;
    dusk.timeOfDaySeconds = 20 * 3600;
    const ChunkKey spawn = chunkOf(sim::docks::kSpawnTileX, sim::docks::kSpawnTileY);
    const MeshData atNoon = meshChunk(tilesA, atlas, materials, spawn, 0, noon);
    const MeshData atDusk = meshChunk(tilesA, atlas, materials, spawn, 0, dusk);
    REQUIRE(atNoon.triangleCount() > 0);
    CHECK(atNoon.positions == atDusk.positions);
    CHECK(atNoon.texcoords == atDusk.texcoords);
    CHECK(atNoon.indices == atDusk.indices);
    CHECK(atNoon.colours != atDusk.colours);
    CHECK(atNoon.version != atDusk.version);
    // Committed dark: the same face is darker at eight in the evening.
    std::size_t noonSum = 0;
    std::size_t duskSum = 0;
    for (std::size_t i = 0; i < atNoon.colours.size(); ++i) {
        if ((i & 3U) == 3U) {
            continue;
        }
        noonSum += atNoon.colours[i];
        duskSum += atDusk.colours[i];
    }
    CHECK(duskSum < noonSum);
}

TEST_CASE("a wall tile produces wall quads and a floor tile does not") {
    // One chunk each way: 32 x 32 x 16 tiles, all OPEN to begin with.
    content::World world(content::Coords(1, 1, 2), content::LaneLayout::core());
    const std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
    std::fill(forms.begin(), forms.end(), static_cast<std::uint8_t>(content::TileForm::Open));
    const sim::TileQuery tiles(world);
    REQUIRE(tiles.sizeX() == 32);
    REQUIRE(tiles.sizeZ() == 16);
    const auto put = [&](std::int32_t x, std::int32_t y, std::int32_t z,
                         content::TileForm form) {
        forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
    };

    // A street: a 5 x 5 patch of FLOOR at level 5 around (20, 20)...
    for (std::int32_t y = 18; y <= 22; ++y) {
        for (std::int32_t x = 18; x <= 22; ++x) {
            put(x, y, 5, content::TileForm::Floor);
        }
    }
    // ...and a wall standing on another patch, with a second wall to its
    // south so the two share a boundary.
    for (std::int32_t y = 8; y <= 12; ++y) {
        for (std::int32_t x = 8; x <= 12; ++x) {
            put(x, y, 5, content::TileForm::Floor);
        }
    }
    put(10, 10, 5, content::TileForm::Wall);
    put(10, 11, 5, content::TileForm::Wall);

    // The wall: four sides, a top. The floors beside it hang BELOW its own
    // level's surface (a slab under the walking surface), so they cover none
    // of the storey it rises through.
    const std::uint8_t lone = cellFaceMask(tiles, 10, 10, 5);
    CHECK((lone & kFaceTopBit) != 0U);
    CHECK((lone & kFaceNorthBit) != 0U);
    CHECK((lone & kFaceEastBit) != 0U);
    CHECK((lone & kFaceWestBit) != 0U);
    // ...except the face it shares with its neighbour, which neither draws.
    CHECK((lone & kFaceSouthBit) == 0U);
    CHECK((cellFaceMask(tiles, 10, 11, 5) & kFaceNorthBit) == 0U);
    CHECK((cellFaceMask(tiles, 10, 11, 5) & kFaceSouthBit) != 0U);

    // The floor in the middle of the street: a top, an underside over the
    // air below, and NO wall quads.
    const std::uint8_t street = cellFaceMask(tiles, 20, 20, 5);
    CHECK((street & kFaceTopBit) != 0U);
    CHECK((street & kFaceBottomBit) != 0U);
    CHECK((street & kFaceSideBits) == 0U);
    // The street's edge does show its slab's side, thin as it is.
    CHECK((cellFaceMask(tiles, 18, 20, 5) & kFaceWestBit) != 0U);
    // Air draws nothing.
    CHECK(cellFaceMask(tiles, 20, 20, 6) == 0U);

    // And in the mesh itself: the wall's sides are the storey, three tiles
    // tall from its level's surface to the next one's, wearing side art; the
    // floor's top wears floor art at the walking surface.
    const render::TileAtlas& atlas = proceduralAtlas();
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(atlas);
    // The wall stands in chunk (0, 0) and the street in chunk (1, 1): two
    // geometries, one face list to scan.
    const ChunkGeometry wallChunk = buildChunkGeometry(tiles, atlas, materials, ChunkKey{0, 0}, 0);
    const ChunkGeometry streetChunk =
        buildChunkGeometry(tiles, atlas, materials, ChunkKey{1, 1}, 0);
    CHECK_FALSE(wallChunk.truncated);
    CHECK_FALSE(streetChunk.truncated);
    CHECK(wallChunk.key.cx == 0);
    CHECK(streetChunk.key.cy == 1);
    std::size_t wallSides = 0;
    std::size_t floorSides = 0;
    bool floorTopSeen = false;
    const auto scan = [&](const ChunkGeometry& geometry) {
        for (const ChunkFace& face : geometry.faces) {
            const bool side = face.dir != FaceDir::Top && face.dir != FaceDir::Bottom;
            if (face.x == 10 && face.y == 10 && face.z == 5 && side) {
                ++wallSides;
                CHECK(face.kind == render::FaceKind::Side);
                float lowest = 1.0e9F;
                float highest = -1.0e9F;
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const float y = geometry.positions[(face.firstVertex + corner) * 3 + 1];
                    lowest = std::min(lowest, y);
                    highest = std::max(highest, y);
                }
                CHECK(lowest == doctest::Approx(render::bandSurface(5)));
                CHECK(highest == doctest::Approx(render::bandSurface(6)));
            }
            if (face.x == 20 && face.y == 20 && face.z == 5) {
                if (side) {
                    ++floorSides;
                } else if (face.dir == FaceDir::Top) {
                    floorTopSeen = true;
                    CHECK(face.kind == render::FaceKind::FloorTop);
                    CHECK(geometry.positions[face.firstVertex * 3 + 1] ==
                          doctest::Approx(render::bandSurface(5)));
                }
            }
        }
        // Every quad is four vertices and two triangles, counted the same way.
        CHECK(geometry.vertexCount() == geometry.faces.size() * 4);
        CHECK(geometry.triangleCount() == geometry.faces.size() * 2);
    };
    scan(wallChunk);
    scan(streetChunk);
    CHECK(wallSides == 3);
    CHECK(floorSides == 0);
    CHECK(floorTopSeen);
}

TEST_CASE("a chunk's version moves with the dynamic lamps as well as the clock") {
    std::vector<render::Lamp> none;
    CHECK(dynamicLampKey(none) == 0U);
    render::Lamp hearth;
    hearth.name = "gull_hearth";
    hearth.x = 150;
    hearth.y = 70;
    hearth.z = 19;
    hearth.luminance = 20;
    hearth.warmth = render::LampWarmth::Fire;
    std::vector<render::Lamp> lit{hearth};
    std::vector<render::Lamp> litAgain{hearth};
    CHECK(dynamicLampKey(lit) != 0U);
    CHECK(dynamicLampKey(lit) == dynamicLampKey(litAgain));
    render::Lamp banked = hearth;
    banked.luminance = 6;
    CHECK(dynamicLampKey(std::vector<render::Lamp>{banked}) != dynamicLampKey(lit));

    ChunkLighting dark;
    dark.timeOfDaySeconds = 3 * 3600;
    ChunkLighting withHearth = dark;
    withHearth.lampKey = dynamicLampKey(lit);
    CHECK(chunkVersion(0, dark) != chunkVersion(0, withHearth));
    CHECK(chunkVersion(0, withHearth) != 0U);
    CHECK(chunkVersion(1, withHearth) != chunkVersion(0, withHearth));
}

TEST_CASE("the world scene instances the chunks near the eye and recolours on the minute") {
    const sim::TileQuery tiles(docksWorld());
    const render::TileAtlas& atlas = proceduralAtlas();
    const render::Camera eye = spawnCamera();
    WorldSceneParams params;
    params.timeOfDaySeconds = 12 * 3600;

    WorldScene world(tiles, atlas, nullptr);
    SceneDescription scene;
    world.refresh(scene, eye, 320.0F / 180.0F, params);
    const WorldSceneStats& stats = world.stats();
    CHECK(stats.chunksBuilt == static_cast<std::size_t>(kChunksAcross * kChunksDown));
    CHECK(stats.chunksWithGeometry >= 60);
    CHECK_FALSE(stats.anyTruncated);
    // Near the spawn: some chunks, not the whole district.
    CHECK(stats.chunksInstanced >= 4);
    CHECK(stats.chunksInstanced < stats.chunksWithGeometry);
    // The sky first, then the chunks, all textured with the one atlas.
    REQUIRE(scene.instances.size() == stats.chunksInstanced + 1);
    CHECK(scene.instances[0].meshId == kSkyMeshId);
    for (std::size_t i = 1; i < scene.instances.size(); ++i) {
        CHECK(scene.instances[i].textureId == kChunkAtlasTextureId);
        CHECK(scene.instances[i].meshId >= kChunkMeshIdBase);
        CHECK(scene.instances[i].meshId < kActorMeshIdBase);
    }
    REQUIRE(scene.textures.size() == 1);
    CHECK(scene.findMesh(kSkyMeshId) != nullptr);
    // The camera is the body's: the eye where the spawn stands, up is up.
    CHECK(scene.camera.position.x == doctest::Approx(eye.x));
    CHECK(scene.camera.position.y == doctest::Approx(eye.z));
    CHECK(scene.camera.position.z == doctest::Approx(eye.y));
    CHECK(scene.camera.up.y == doctest::Approx(1.0F));

    // Built again from scratch: the same description, byte for byte.
    WorldScene again(tiles, atlas, nullptr);
    SceneDescription twice;
    again.refresh(twice, eye, 320.0F / 180.0F, params);
    CHECK(sceneHash(scene) == sceneHash(twice));

    // The same minute again: nothing recolours. The next minute: every
    // chunk with geometry recolours, the sky too, and no vertex moves.
    const std::size_t recoloured = stats.meshesRecoloured;
    world.refresh(scene, eye, 320.0F / 180.0F, params);
    CHECK(stats.meshesRecoloured == recoloured);
    const std::uint64_t sameMinute = sceneHash(scene);
    params.timeOfDaySeconds += 30;
    world.refresh(scene, eye, 320.0F / 180.0F, params);
    CHECK(stats.meshesRecoloured == recoloured);
    CHECK(sceneHash(scene) == sameMinute);
    const ChunkKey spawnChunk = chunkOf(sim::docks::kSpawnTileX, sim::docks::kSpawnTileY);
    const std::vector<float> before = scene.findMesh(chunkMeshId(spawnChunk))->positions;
    const std::vector<std::uint8_t> beforeColours =
        scene.findMesh(chunkMeshId(spawnChunk))->colours;
    params.timeOfDaySeconds = 20 * 3600;
    world.refresh(scene, eye, 320.0F / 180.0F, params);
    CHECK(stats.meshesRecoloured == recoloured + stats.chunksWithGeometry);
    CHECK(scene.findMesh(chunkMeshId(spawnChunk))->positions == before);
    CHECK(scene.findMesh(chunkMeshId(spawnChunk))->colours != beforeColours);
    CHECK(sceneHash(scene) != sameMinute);

    // The sky dome clears the far plane with its lid on and is bigger than
    // the district: no vertex farther than 512 from the eye, none nearer
    // than the farthest authored tile could be.
    const MeshData* sky = scene.findMesh(kSkyMeshId);
    REQUIRE(sky != nullptr);
    for (std::size_t i = 0; i + 2 < sky->positions.size(); i += 3) {
        const float d = std::sqrt(sky->positions[i] * sky->positions[i] +
                                  sky->positions[i + 1] * sky->positions[i + 1] +
                                  sky->positions[i + 2] * sky->positions[i + 2]);
        REQUIRE(d < 512.0F);
        REQUIRE(d >= kSkyHeight - 1.0F);
    }
}
