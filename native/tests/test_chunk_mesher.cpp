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
#include <string>
#include <string_view>
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
#include "granadad/render3d/static_pieces.hpp"
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

// ---------------------------------------------------------------------------
// S LANE -- the Synty building kit placed from the tile map
// ---------------------------------------------------------------------------
//
// Four claims, each a case that goes red on its own: a corner tile gets the
// corner piece, a door gap gets the door frame, the placements are a pure
// function of the tile map and the catalogue (the real Docks placed twice
// hash the same), and the catalogue that ships is well-formed and dresses
// the district. The licensed glTF files are never needed: placement never
// opens one, and the frame test (test_render3d.cpp) proves the placeholder
// stands when they are absent.

namespace {

const StaticCatalogue& shippedCatalogue() {
    static const StaticCatalogue catalogue =
        StaticCatalogue::load(staticCataloguePath(content::contentDir()));
    return catalogue;
}

std::uint16_t materialId(std::string_view id) {
    const std::span<const std::string_view> ids = render::materialIds();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return 0;
}

std::size_t countRole(const std::vector<StaticPlacement>& placements, PieceRole role) {
    std::size_t n = 0;
    for (const StaticPlacement& p : placements) {
        if (p.role == role) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("the shipped piece catalogue loads and names a piece for every rule") {
    const StaticCatalogue& catalogue = shippedCatalogue();
    REQUIRE(catalogue.error().empty());
    for (const std::string& warning : catalogue.warnings()) {
        MESSAGE("catalogue warning: " << warning);
    }
    CHECK(catalogue.warnings().empty());
    REQUIRE_FALSE(catalogue.empty());
    for (const PieceRole role : {PieceRole::Wall, PieceRole::WallCorner, PieceRole::WallWindow,
                                 PieceRole::WallDoor, PieceRole::RoofEdge, PieceRole::FloorPlank,
                                 PieceRole::FloorCobble, PieceRole::FloorFlag, PieceRole::Water,
                                 PieceRole::PropBarrel, PieceRole::PropCrate, PieceRole::PropSack,
                                 PieceRole::LampWall, PieceRole::LampPost, PieceRole::Brazier,
                                 PieceRole::FloorFill, PieceRole::WallCap, PieceRole::Ceiling,
                                 PieceRole::WallTimber}) {
        const PieceSpec* spec = catalogue.piece(role);
        REQUIRE_MESSAGE(spec != nullptr, "no piece for role " << pieceRoleName(role));
        CHECK(spec->file.find(".gltf") != std::string::npos);
        CHECK(spec->file.find('/') != std::string::npos);
        CHECK(catalogue.pieceIndex(role) >= 0);
        CHECK(catalogue.pieces()[static_cast<std::size_t>(catalogue.pieceIndex(role))].role == role);
    }
    // The table is in role order, so piece indices agree on every machine.
    for (std::size_t i = 1; i < catalogue.pieces().size(); ++i) {
        CHECK(catalogue.pieces()[i - 1].role < catalogue.pieces()[i].role);
    }
    // The kit's own module: 2.5 m walls, 3 m storeys, the brick on -Z.
    CHECK(catalogue.piece(PieceRole::Wall)->width == doctest::Approx(2.5F));
    CHECK(catalogue.piece(PieceRole::Wall)->frontNegZ);
    CHECK(catalogue.piece(PieceRole::RoofEdge)->frontNegZ == false);
    // Materials: the Gull's granite is masonry with flags underfoot and a
    // flat fill where no flag fits, its timber storey is timber, dirt is a
    // flat fill and nothing on its walls, steel is nothing at all.
    REQUIRE(catalogue.materialByName("granite") != nullptr);
    CHECK(catalogue.materialByName("granite")->wallClass == WallClass::Masonry);
    CHECK(catalogue.materialByName("granite")->plasterOut);
    CHECK_FALSE(catalogue.materialByName("brick")->plasterOut);
    CHECK(catalogue.materialByName("granite")->floorRole == PieceRole::FloorFlag);
    CHECK(catalogue.materialByName("granite")->fillRole == PieceRole::FloorFill);
    CHECK(catalogue.materialByName("brick")->floorRole == PieceRole::FloorCobble);
    REQUIRE(catalogue.materialByName("oak") != nullptr);
    CHECK(catalogue.materialByName("oak")->wallClass == WallClass::Timber);
    CHECK(catalogue.materialByName("oak")->floorRole == PieceRole::FloorPlank);
    REQUIRE(catalogue.materialByName("dirt") != nullptr);
    CHECK(catalogue.materialByName("dirt")->wallClass == WallClass::None);
    CHECK(catalogue.materialByName("dirt")->fillRole == PieceRole::FloorFill);
    CHECK(catalogue.materialByName("steel") == nullptr);
    CHECK(catalogue.piece(PieceRole::FloorFill)->flipY);
    CHECK(catalogue.piece(PieceRole::FloorCobble)->minBlock == 2);
    CHECK(catalogue.material(materialId("granite")) == catalogue.materialByName("granite"));
    CHECK(catalogue.minBand() == 18);
    CHECK(catalogue.digest() != 0U);
    // The role names round-trip, and an unknown one is None.
    CHECK(pieceRoleFromName("wall_corner") == PieceRole::WallCorner);
    CHECK(pieceRoleName(PieceRole::WallDoor) == "wall_door");
    CHECK(pieceRoleFromName("gargoyle") == PieceRole::None);
    // A malformed document is an empty catalogue with the reason kept.
    const StaticCatalogue broken = StaticCatalogue::fromJson("[1, 2");
    CHECK(broken.empty());
    CHECK_FALSE(broken.error().empty());
    CHECK(placeStaticPieces(sim::TileQuery(docksWorld()), broken, {}).placements.empty());
}

TEST_CASE("a wall tile with two open neighbours places a corner piece") {
    // A brick house on a floor at level 19 (the quayside band, above the
    // catalogue's floor band), out of doors: a 6 x 6 ring of
    // WALL from (10, 10) to (15, 15) with FLOOR inside and a street of
    // FLOOR around it, nothing built above. Its four corner tiles are the
    // wall tiles with exactly two exposed neighbours -- and exactly those
    // four get the corner piece, brick side out, on the tile's corner.
    content::World world(content::Coords(1, 1, 3), content::LaneLayout::core());
    const std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
    const std::span<std::uint16_t> materials = world.shortLane(content::kMaterialLane);
    std::fill(forms.begin(), forms.end(), static_cast<std::uint8_t>(content::TileForm::Open));
    const sim::TileQuery tiles(world);
    const std::uint16_t brick = materialId("brick");
    const auto put = [&](std::int32_t x, std::int32_t y, std::int32_t z, content::TileForm form,
                         std::uint16_t material) {
        forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
        materials[tiles.index(x, y, z)] = material;
    };
    for (std::int32_t y = 6; y <= 19; ++y) {
        for (std::int32_t x = 6; x <= 19; ++x) {
            put(x, y, 18, content::TileForm::Wall, materialId("dirt"));
            put(x, y, 19, content::TileForm::Floor, materialId("dirt"));
        }
    }
    for (std::int32_t y = 10; y <= 15; ++y) {
        for (std::int32_t x = 10; x <= 15; ++x) {
            const bool ring = x == 10 || x == 15 || y == 10 || y == 15;
            put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor,
                ring ? brick : materialId("oak"));
        }
    }
    const StaticCatalogue& catalogue = shippedCatalogue();
    REQUIRE(catalogue.piece(PieceRole::WallCorner) != nullptr);
    const StaticPlacements placed = placeStaticPieces(tiles, catalogue, {});

    // Exactly four corners, one on each corner tile.
    CHECK(countRole(placed.placements, PieceRole::WallCorner) == 4);
    bool seen[4] = {false, false, false, false};
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::WallCorner) {
            continue;
        }
        CHECK(p.lightZ == 19);
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19)));
        // Full legs: every run is long enough for the kit's own 2.5 m.
        CHECK(p.instance.scale.x == doctest::Approx(1.0F));
        CHECK(p.instance.scale.z == doctest::Approx(1.0F));
        const bool nw = p.lightX == 10 && p.lightY == 10;
        const bool ne = p.lightX == 15 && p.lightY == 10;
        const bool se = p.lightX == 15 && p.lightY == 15;
        const bool sw = p.lightX == 10 && p.lightY == 15;
        CHECK((nw || ne || se || sw));
        // The piece's brick corner stands 0.225 proud of the tile's own
        // corner point, both ways -- the NE corner's piece origin is its
        // leg's length back along the north face, the others turned.
        if (ne) {
            seen[0] = true;
            CHECK(p.instance.yaw == doctest::Approx(0.0F));
            CHECK(p.instance.position.x == doctest::Approx(16.0F - 2.3875F));
            CHECK(p.instance.position.z == doctest::Approx(10.0F - 0.1125F));
        } else if (se) {
            seen[1] = true;
            CHECK(p.instance.yaw == doctest::Approx(3.14159265F / 2.0F));
        } else if (sw) {
            seen[2] = true;
            CHECK(p.instance.yaw == doctest::Approx(3.14159265F));
        } else if (nw) {
            seen[3] = true;
            CHECK(p.instance.yaw == doctest::Approx(3.0F * 3.14159265F / 2.0F));
        }
    }
    CHECK((seen[0] && seen[1] && seen[2] && seen[3]));

    // The straight runs: each outer face is 6 tiles; the corners take 2.5
    // less a hair at each end, so one stretched wall piece fills the rest
    // of each face. The inner faces (nothing above, so still out of doors
    // here) are 4 tiles with concave ends: two pieces each.
    const std::size_t walls = countRole(placed.placements, PieceRole::Wall) +
                              countRole(placed.placements, PieceRole::WallWindow);
    CHECK(walls == 4 + 4 * 2);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Wall || p.role == PieceRole::WallWindow) {
            CHECK(p.instance.scale.x > 0.4F);
            CHECK(p.instance.scale.x < 1.4F);
            // A centimetre under the storey, so the chunk's own wall head
            // never fights the piece's top.
            CHECK(p.instance.scale.y == doctest::Approx((render::kBandHeight - 0.01F) / 3.0057F));
        }
    }
    // No door: the ring is closed. No cobbles: the floors are dirt (the
    // flat fill, in rectangles around the house) and oak (planks -- the
    // 4 x 4 interior is one plank rectangle; nothing is left to the chunk).
    CHECK(countRole(placed.placements, PieceRole::WallDoor) == 0);
    CHECK(countRole(placed.placements, PieceRole::FloorCobble) == 0);
    CHECK(countRole(placed.placements, PieceRole::FloorPlank) == 1);
    CHECK(countRole(placed.placements, PieceRole::FloorFill) >= 4);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::FloorPlank) {
            CHECK(p.instance.scale.x == doctest::Approx(4.0F / 2.5F));
            CHECK(p.instance.scale.z == doctest::Approx(4.0F / 2.5F));
            CHECK(p.instance.position.x == doctest::Approx(11.0F));
            CHECK(p.instance.position.z == doctest::Approx(11.0F));
        }
        if (p.role == PieceRole::FloorFill) {
            // Mirrored upward, and never over the house.
            CHECK(p.instance.scale.y < 0.0F);
            CHECK((p.lightY2 < 10 || p.lightY > 15 || p.lightX2 < 10 || p.lightX > 15));
        }
    }
    // A cornice along every outdoor face at the roof line, a cap over the
    // ring's wall heads, and no ceiling anywhere (nothing is built above).
    CHECK(countRole(placed.placements, PieceRole::RoofEdge) >= 4);
    CHECK(countRole(placed.placements, PieceRole::WallCap) >= 1);
    CHECK(countRole(placed.placements, PieceRole::Ceiling) == 0);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallCap) {
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(20) + 0.004F));
            CHECK(p.instance.scale.y < 0.0F);
        }
    }
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::RoofEdge) {
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(20) - 0.2347F));
        }
    }
    // And the same world placed again is the same list, byte for byte.
    const StaticPlacements again = placeStaticPieces(tiles, catalogue, {});
    REQUIRE(again.placements.size() == placed.placements.size());
    for (std::size_t i = 0; i < placed.placements.size(); ++i) {
        CHECK(again.placements[i].role == placed.placements[i].role);
        CHECK(again.placements[i].instance.position.x == placed.placements[i].instance.position.x);
        CHECK(again.placements[i].instance.position.z == placed.placements[i].instance.position.z);
        CHECK(again.placements[i].instance.yaw == placed.placements[i].instance.yaw);
    }
}

TEST_CASE("a door tile places the door frame") {
    // The same house with a roof over it (a FLOOR storey above the ring's
    // interior and the ring itself) and a two-tile gap in its south wall:
    // the gap is walkable, open on both sides, roofed on one and not the
    // other, and the wall runs on past both jambs -- a door. It gets one
    // frame, fitted to the two tiles, brick side to the street. The gap
    // between two houses is not a door and gets none.
    content::World world(content::Coords(1, 1, 3), content::LaneLayout::core());
    const std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
    const std::span<std::uint16_t> materials = world.shortLane(content::kMaterialLane);
    std::fill(forms.begin(), forms.end(), static_cast<std::uint8_t>(content::TileForm::Open));
    const sim::TileQuery tiles(world);
    const std::uint16_t granite = materialId("granite");
    const auto put = [&](std::int32_t x, std::int32_t y, std::int32_t z, content::TileForm form,
                         std::uint16_t material) {
        forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
        materials[tiles.index(x, y, z)] = material;
    };
    for (std::int32_t y = 4; y <= 27; ++y) {
        for (std::int32_t x = 4; x <= 27; ++x) {
            put(x, y, 18, content::TileForm::Wall, materialId("dirt"));
            put(x, y, 19, content::TileForm::Floor, materialId("brick"));
        }
    }
    // House A: ring (8..15, 8..15), door at (11..12, 15).
    for (std::int32_t y = 8; y <= 15; ++y) {
        for (std::int32_t x = 8; x <= 15; ++x) {
            const bool ring = x == 8 || x == 15 || y == 8 || y == 15;
            put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor,
                ring ? granite : materialId("oak"));
            put(x, y, 20, content::TileForm::Floor, materialId("thatch"));
        }
    }
    put(11, 15, 19, content::TileForm::Floor, materialId("oak"));
    put(12, 15, 19, content::TileForm::Floor, materialId("oak"));
    // House B two tiles east of A, sharing A's rows: the alley mouth at
    // (16..17, 8) has walls either side and walls beyond them, but both
    // sides of it are open to the sky.
    for (std::int32_t y = 8; y <= 15; ++y) {
        for (std::int32_t x = 18; x <= 24; ++x) {
            const bool ring = x == 18 || x == 24 || y == 8 || y == 15;
            put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor,
                ring ? granite : materialId("oak"));
            put(x, y, 20, content::TileForm::Floor, materialId("thatch"));
        }
    }
    const StaticCatalogue& catalogue = shippedCatalogue();
    REQUIRE(catalogue.piece(PieceRole::WallDoor) != nullptr);
    const StaticPlacements placed = placeStaticPieces(tiles, catalogue, {});

    REQUIRE(countRole(placed.placements, PieceRole::WallDoor) == 1);
    CHECK(placed.stats.doorGaps == 1);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::WallDoor) {
            continue;
        }
        // The building is rendered stone, so the frame shows its plaster to
        // the street: yawed to face south, then half a turn, its module
        // running east from the gap's west edge, in the facade's own plane
        // (the wall's half thickness proud of the south face at z = 16).
        CHECK(p.instance.yaw == doctest::Approx(0.0F));
        CHECK(p.instance.position.x == doctest::Approx(11.0F));
        CHECK(p.instance.position.z == doctest::Approx(16.0F + 0.1125F));
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19)));
        // Fitted to the two-tile gap: 2 / 2.5.
        CHECK(p.instance.scale.x == doctest::Approx(0.8F));
        CHECK(p.lightX == 11);
        CHECK(p.lightY == 15);
    }
    // The roof over the house puts a ceiling under it -- over the rooms,
    // under the slab, lit as the room's underside -- and nothing over the
    // street.
    CHECK(countRole(placed.placements, PieceRole::Ceiling) >= 2);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Ceiling) {
            CHECK(p.instance.position.y ==
                  doctest::Approx(render::bandSurface(20) - render::kFloorSlab - 0.004F));
            CHECK(p.instance.scale.y > 0.0F);
            CHECK(p.lightZ == 19);
            CHECK(p.facing == doctest::Approx(render::kUndersideLift));
        }
    }
    // The house's interior is roofed now, so its inner faces wear plaster:
    // a wall piece on an indoor face is yawed half a turn from the brick
    // rule -- checked on the north wall's south face, which looks south
    // into the room and would face south (yaw pi) brick-out.
    bool plasterSeen = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Wall && p.lightY == 8 && p.lightX > 8 && p.lightX < 15 &&
            p.instance.position.z > 8.9F && p.instance.position.z < 9.2F) {
            plasterSeen = true;
            CHECK(p.instance.yaw == doctest::Approx(0.0F));
        }
    }
    CHECK(plasterSeen);
    // The window rule only fires out of doors, on a stretched piece: none
    // of the pieces inside the room are windows.
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallWindow) {
            CHECK((p.lightX == 8 || p.lightX == 15 || p.lightY == 8 || p.lightY == 15 ||
                   p.lightX == 18 || p.lightX == 24));
        }
    }
    // A lamp on the street beside the house's wall hangs a wall lamp on
    // that wall; a lamp in the open stands a post; a fire is a brazier.
    std::vector<render::Lamp> lamps(3);
    lamps[0].name = "lamp_house_door";
    lamps[0].x = 9;
    lamps[0].y = 16;
    lamps[0].z = 19;
    lamps[0].warmth = render::LampWarmth::Lantern;
    lamps[1].name = "lamp_square";
    lamps[1].x = 5;
    lamps[1].y = 5;
    lamps[1].z = 19;
    lamps[1].warmth = render::LampWarmth::Lantern;
    lamps[2].name = "brazier_square";
    lamps[2].x = 6;
    lamps[2].y = 5;
    lamps[2].z = 19;
    lamps[2].warmth = render::LampWarmth::Fire;
    const StaticPlacements lit = placeStaticPieces(tiles, catalogue, lamps);
    CHECK(countRole(lit.placements, PieceRole::LampWall) == 1);
    CHECK(countRole(lit.placements, PieceRole::LampPost) == 1);
    CHECK(countRole(lit.placements, PieceRole::Brazier) == 1);
    for (const StaticPlacement& p : lit.placements) {
        if (p.role == PieceRole::LampWall) {
            // On the door-side wall's south face (z = 16), hung out over
            // the lamp's own tile.
            CHECK(p.instance.position.z == doctest::Approx(16.0F + 0.323F));
            CHECK(p.instance.position.x == doctest::Approx(9.5F));
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + 2.1F));
        }
    }
    CHECK(lit.placements.size() == placed.placements.size() + 3);
}

TEST_CASE("placement is a deterministic function of the tile map") {
    // THE REAL DOCKS, dressed twice from two views of one world, through
    // two world scenes: the same placements in the same order, the same
    // description bytes, the same hash -- the S lane's determinism claim.
    // And every rule fires somewhere in the district: walls and corners,
    // windows, door frames, cornices, planks over the piers, cobbles on the
    // Tarwalk, flags on the quay, the harbour's water, props, the lamps.
    const sim::TileQuery tilesA(docksWorld());
    const sim::TileQuery tilesB(docksWorld());
    const StaticCatalogue& catalogue = shippedCatalogue();
    REQUIRE_FALSE(catalogue.empty());
    const std::vector<render::Lamp> lamps =
        render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    REQUIRE_FALSE(lamps.empty());

    const StaticPlacements first = placeStaticPieces(tilesA, catalogue, lamps);
    const StaticPlacements second = placeStaticPieces(tilesB, catalogue, lamps);
    REQUIRE(first.placements.size() == second.placements.size());
    CHECK(first.placements.size() > 1000);
    for (std::size_t i = 0; i < first.placements.size(); ++i) {
        const StaticInstance& a = first.placements[i].instance;
        const StaticInstance& b = second.placements[i].instance;
        REQUIRE(a.piece == b.piece);
        REQUIRE(a.role == b.role);
        REQUIRE(a.position.x == b.position.x);
        REQUIRE(a.position.y == b.position.y);
        REQUIRE(a.position.z == b.position.z);
        REQUIRE(a.yaw == b.yaw);
        REQUIRE(a.scale.x == b.scale.x);
        REQUIRE(a.scale.z == b.scale.z);
    }
    const StaticPlacementStats& stats = first.stats;
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Wall)] > 500);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallTimber)] > 100);
    // Timber stands the plank quad on its edge: a quarter turn about X.
    for (const StaticPlacement& p : first.placements) {
        if (p.role == PieceRole::WallTimber) {
            CHECK(p.instance.pitch == doctest::Approx(-3.14159265F / 2.0F));
            CHECK(p.instance.scale.z == doctest::Approx((render::kBandHeight - 0.01F) / 2.5F));
        } else {
            CHECK(p.instance.pitch == 0.0F);
        }
    }
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallCorner)] >= 4);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallWindow)] > 20);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallDoor)] >= 2);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::RoofEdge)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorPlank)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorCobble)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorFlag)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorFill)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallCap)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Ceiling)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Water)] > 5);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::PropBarrel)] +
              stats.byRole[static_cast<std::size_t>(PieceRole::PropCrate)] +
              stats.byRole[static_cast<std::size_t>(PieceRole::PropSack)] >
          20);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::LampWall)] +
              stats.byRole[static_cast<std::size_t>(PieceRole::LampPost)] +
              stats.byRole[static_cast<std::size_t>(PieceRole::Brazier)] ==
          lamps.size());
    // The Gilded Gull's door (tavern.hpp: x 153..154 on the y = 66
    // frontage) is one of the frames, brick to the Tarwalk (north).
    bool gullDoor = false;
    for (const StaticPlacement& p : first.placements) {
        if (p.role == PieceRole::WallDoor && p.lightX == 153 && p.lightY == 66 && p.lightZ == 19) {
            gullDoor = true;
            // Granite: plaster to the street, so the north-facing frame is
            // flipped, on the frontage plane, its half thickness proud.
            CHECK(p.instance.yaw == doctest::Approx(3.14159265F));
            CHECK(p.instance.position.z == doctest::Approx(66.0F - 0.1125F));
            CHECK(p.instance.scale.x == doctest::Approx(0.8F));
        }
    }
    CHECK(gullDoor);
    // Nothing is placed below the catalogue's floor band (a ceiling under a
    // slab on that band is lit one level lower; the strand's tide pools lie
    // one level under the harbour's surface), and everything is inside the
    // authored district.
    for (const StaticPlacement& p : first.placements) {
        const bool oneBelow = p.role == PieceRole::Ceiling || p.role == PieceRole::Water;
        CHECK(p.lightZ >= catalogue.minBand() - (oneBelow ? 1 : 0));
        CHECK(p.instance.position.x > 30.0F);
        CHECK(p.instance.position.x < 226.0F);
    }
    MESSAGE("Docks pieces: " << first.placements.size() << " placed -- " << stats.byRole[1]
                             << " walls, " << stats.byRole[2] << " corners, " << stats.byRole[3]
                             << " windows, " << stats.byRole[4] << " doors, " << stats.byRole[5]
                             << " cornices, " << stats.byRole[6] << " plank, " << stats.byRole[7]
                             << " cobble, " << stats.byRole[8] << " flag, " << stats.byRole[9]
                             << " water, " << stats.byRole[16] << " fills, " << stats.byRole[17]
                             << " caps, " << stats.byRole[18] << " ceilings, "
                             << stats.byRole[10] + stats.byRole[11] + stats.byRole[12]
                             << " props, " << stats.byRole[13] + stats.byRole[14] + stats.byRole[15]
                             << " lamps; " << stats.wallRuns << " wall runs, " << stats.doorGaps
                             << " door gaps");

    // Through the world scene: the description carries the table and the
    // pieces near the eye, lit, and hashes the same twice.
    const render::TileAtlas& atlas = proceduralAtlas();
    const render::Camera eye = spawnCamera();
    WorldSceneParams params;
    params.timeOfDaySeconds = 20 * 3600;
    WorldScene worldA(tilesA, atlas, nullptr, &catalogue, &lamps);
    WorldScene worldB(tilesB, atlas, nullptr, &catalogue, &lamps);
    SceneDescription sceneA;
    SceneDescription sceneB;
    worldA.refresh(sceneA, eye, 320.0F / 180.0F, params);
    worldB.refresh(sceneB, eye, 320.0F / 180.0F, params);
    CHECK(worldA.stats().piecesPlaced == first.placements.size());
    CHECK(worldA.stats().piecesInstanced > 100);
    CHECK(worldA.stats().piecesInstanced < worldA.stats().piecesPlaced);
    CHECK(worldA.stats().piecesRelit == 1);
    REQUIRE(sceneA.statics.size() == worldA.stats().piecesInstanced);
    REQUIRE(sceneA.pieces.size() == catalogue.pieces().size());
    CHECK(sceneHash(sceneA) == sceneHash(sceneB));
    // Every described piece is inside its reach.
    for (const StaticInstance& piece : sceneA.statics) {
        REQUIRE(piece.piece < sceneA.pieces.size());
        const float dx = piece.position.x - eye.x;
        const float dz = piece.position.z - eye.y;
        CHECK(dx * dx + dz * dz <= 96.0F * 96.0F + 1.0F);
    }
    // The same minute again relights nothing; the next hour relights once
    // and moves the hash.
    const std::uint64_t evening = sceneHash(sceneA);
    worldA.refresh(sceneA, eye, 320.0F / 180.0F, params);
    CHECK(worldA.stats().piecesRelit == 1);
    CHECK(sceneHash(sceneA) == evening);
    params.timeOfDaySeconds = 21 * 3600;
    worldA.refresh(sceneA, eye, 320.0F / 180.0F, params);
    CHECK(worldA.stats().piecesRelit == 2);
    CHECK(sceneHash(sceneA) != evening);
    // And without a catalogue the description carries no pieces at all.
    WorldScene bare(tilesA, atlas, nullptr);
    SceneDescription plain;
    bare.refresh(plain, eye, 320.0F / 180.0F, params);
    CHECK(plain.statics.empty());
    CHECK(plain.pieces.empty());
    CHECK(bare.stats().piecesPlaced == 0);
}
