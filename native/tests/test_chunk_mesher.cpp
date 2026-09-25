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
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/door_jambs.hpp"
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
    for (std::size_t r = 1; r < kPieceRoleCount; ++r) {
        const auto role = static_cast<PieceRole>(r);
        const PieceSpec* spec = catalogue.piece(role);
        REQUIRE_MESSAGE(spec != nullptr, "no piece for role " << pieceRoleName(role));
        CHECK(spec->file.find(".gltf") != std::string::npos);
        CHECK(spec->file.find('/') != std::string::npos);
        CHECK(catalogue.pieceIndex(role) >= 0);
        CHECK(catalogue.pieces()[static_cast<std::size_t>(catalogue.pieceIndex(role))].role == role);
        CHECK(catalogue.variantCount(role) >= 1);
    }
    // The table is in role order, variants in file order, so piece indices
    // agree on every machine.
    for (std::size_t i = 1; i < catalogue.pieces().size(); ++i) {
        const PieceSpec& a = catalogue.pieces()[i - 1];
        const PieceSpec& b = catalogue.pieces()[i];
        CHECK((a.role < b.role || (a.role == b.role && a.variant + 1 == b.variant)));
    }
    // Variants: the cobbles alternate two blocks, the chimneys three stacks,
    // and each variant is its own row at its own index.
    CHECK(catalogue.variantCount(PieceRole::FloorCobble) == 2);
    CHECK(catalogue.variantCount(PieceRole::Chimney) == 3);
    CHECK(catalogue.variantCount(PieceRole::RoofTile) == 3);
    CHECK(catalogue.pieceIndex(PieceRole::Chimney, 2) == catalogue.pieceIndex(PieceRole::Chimney, 0) + 2);
    CHECK(catalogue.piece(PieceRole::Chimney, 1)->file != catalogue.piece(PieceRole::Chimney, 0)->file);
    CHECK(catalogue.piece(PieceRole::Chimney, 3) == nullptr);
    // The knobs: the rhythm, the chimney rate over masonry, the furniture.
    CHECK(catalogue.knobs().windowEvery == 2);
    CHECK(catalogue.knobs().chimneyEvery == 12);
    CHECK(catalogue.knobs().tableEvery > 0);
    CHECK(catalogue.knobs().boatEvery > 0);
    // The hull's lean is a knob, and the shipped one is plumb (a leaning
    // quad opens a wedge of atlas at every corner of a hull).
    CHECK(catalogue.knobs().hullFlareDegrees >= 0.0F);
    // The door frame knows its opening, so the inside plaster is cut round it.
    CHECK(catalogue.piece(PieceRole::WallDoor)->cutX == doctest::Approx(0.17F));
    CHECK(catalogue.piece(PieceRole::WallDoor)->cutY == doctest::Approx(2.029F));
    CHECK(catalogue.piece(PieceRole::Flame)->twoSided);
    CHECK(catalogue.piece(PieceRole::Flame)->selfLit);
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
    CHECK(catalogue.materialByName("granite")->floorRole == PieceRole::FloorCobble);
    CHECK(catalogue.materialByName("granite")->fillRole == PieceRole::FloorFill);
    CHECK(catalogue.materialByName("brick")->floorRole == PieceRole::FloorCobble);
    REQUIRE(catalogue.materialByName("oak") != nullptr);
    CHECK(catalogue.materialByName("oak")->wallClass == WallClass::Timber);
    CHECK(catalogue.materialByName("oak")->floorRole == PieceRole::FloorPlank);
    REQUIRE(catalogue.materialByName("dirt") != nullptr);
    CHECK(catalogue.materialByName("dirt")->wallClass == WallClass::None);
    CHECK(catalogue.materialByName("dirt")->fillRole == PieceRole::FloorFill);
    // Steel is dressed now (the quay's undressed patch); chromatis is not.
    REQUIRE(catalogue.materialByName("steel") != nullptr);
    CHECK(catalogue.materialByName("steel")->wallClass == WallClass::Masonry);
    CHECK(catalogue.materialByName("chromatis") == nullptr);
    // Slab sides: planks on timber, tinted plaster on stone; thatch is the
    // roofing material and its planes get the roof rules.
    CHECK(catalogue.materialByName("oak")->lipRole == PieceRole::LipPlank);
    CHECK(catalogue.materialByName("granite")->lipRole == PieceRole::LipStone);
    CHECK(catalogue.materialByName("thatch")->roof);
    CHECK_FALSE(catalogue.materialByName("granite")->roof);
    // A roof plane wears the flagstone piece as slates over a dark fill,
    // never floorboards.
    CHECK(catalogue.materialByName("thatch")->floorRole == PieceRole::FloorFlag);
    CHECK(catalogue.materialByName("thatch")->fillRole == PieceRole::FloorFill);
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
            // Mirrored upward, lit at its four corner points, and never
            // over the house: its footprint (origin plus the fitted extent)
            // stays outside the ring.
            CHECK(p.instance.scale.y < 0.0F);
            CHECK(p.bilinear);
            const float x1 = p.instance.position.x + p.instance.scale.x * 2.5F;
            const float z1 = p.instance.position.z + p.instance.scale.z * 2.5F;
            CHECK((z1 <= 10.01F || p.instance.position.z >= 15.99F || x1 <= 10.01F ||
                   p.instance.position.x >= 15.99F));
        }
    }
    // A cornice along every outdoor face at the roof line, a cap over the
    // ring's wall heads, and no ceiling anywhere (nothing is built above,
    // and the ring stands on solid ground -- a wall over the substrate is
    // nobody's ceiling).
    CHECK(countRole(placed.placements, PieceRole::RoofEdge) >= 4);
    CHECK(countRole(placed.placements, PieceRole::WallCap) >= 1);
    CHECK(countRole(placed.placements, PieceRole::Ceiling) == 0);
    // Windows keep a rhythm now, not a coin flip: every second piece along
    // a run, never the first -- so the six-tile faces (one stretched piece
    // between the corner legs) carry none and the four-tile inner faces
    // (two plaster pieces each) carry exactly one each. And every window
    // knows the cell behind it.
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallWindow) {
            CHECK(p.hasInside);
            CHECK(p.insideZ == 19);
        }
    }
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
        // Lit from jamb to jamb (the south face runs west): its A end is the
        // cut between the east jamb (13) and the gap cell beside it.
        CHECK(p.lightX == 13);
        CHECK(p.endAX == 12);
        CHECK(p.lightY == 15);
    }
    // The roof over the house puts a ceiling under it -- over the rooms,
    // under the slab, lit as the room's underside -- and nothing over the
    // street. Joists cross it on the odd grid lines.
    CHECK(countRole(placed.placements, PieceRole::Ceiling) >= 2);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Ceiling) {
            CHECK(p.instance.position.y ==
                  doctest::Approx(render::bandSurface(20) - render::kFloorSlab - 0.004F));
            CHECK(p.instance.scale.y > 0.0F);
            CHECK(p.lightZ == 19);
            CHECK(p.facing == doctest::Approx(render::kUndersideLift));
            CHECK(p.bilinear);
        }
        if (p.role == PieceRole::Joist) {
            CHECK(p.instance.position.y < render::bandSurface(20) - render::kFloorSlab);
            CHECK(p.instance.position.y > render::bandSurface(20) - render::kFloorSlab - 0.4F);
            CHECK((static_cast<int>(std::lround(p.instance.position.z)) & 1) == 1);
        }
    }
    CHECK(countRole(placed.placements, PieceRole::Joist) >= 4);
    // The frame on a rendered building was turned to show its plaster to
    // the street, so its brick faces the room: the thin plaster stands
    // behind the header and both jambs, cut round the opening, a hair
    // inside the facade plane. Two leaves hang open on the jambs.
    // Three behind the frame (the header and both jambs) and two returns
    // over the frame's ends in the reveal planes, each over the wall's own
    // thickness less a hair each way (the frame's centre plane stands half
    // the wall's thickness off the facade at z = 16; its trim is thicker
    // but stops short of the ends) so no edge of it stands proud as a
    // hairline.
    CHECK(countRole(placed.placements, PieceRole::DoorInside) == 5);
    const float frameFace = 16.0F + catalogue.piece(PieceRole::Wall)->thickness;
    std::size_t returns = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::DoorInside && p.instance.yaw != doctest::Approx(3.14159265F)) {
            ++returns;
            // The quad runs along its local +X, which a yaw turns to
            // (cos, sin) in the scene's XZ: its two ends in z.
            const float len = std::fabs(p.instance.scale.x) * 2.5F;
            const float zA = p.instance.position.z;
            const float zB = zA + std::sin(p.instance.yaw) * len;
            CHECK(std::max(zA, zB) < frameFace - 0.002F);
            CHECK(std::max(zA, zB) > frameFace - 0.02F);
        }
    }
    CHECK(returns == 2);
    std::size_t behind = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::DoorInside && p.instance.yaw == doctest::Approx(3.14159265F)) {
            ++behind;
            CHECK(p.instance.position.z < 16.0F);
            CHECK(p.instance.position.z > 15.9F);
        }
    }
    CHECK(behind == 3);
    CHECK(countRole(placed.placements, PieceRole::DoorLeaf) == 2);
    // Neither leaf is mirrored (a mirrored piece draws both-sided and its
    // back faces showed as a red seam); the far one is turned a half turn
    // from the near one instead, hung from its far end.
    {
        std::vector<const StaticPlacement*> leaves;
        for (const StaticPlacement& p : placed.placements) {
            if (p.role == PieceRole::DoorLeaf) {
                leaves.push_back(&p);
            }
        }
        REQUIRE(leaves.size() == 2);
        for (const StaticPlacement* p : leaves) {
            CHECK(p->instance.scale.z > 0.0F);
            CHECK(p->mode == kDrawShaded);
        }
        const float turned = std::fabs(leaves[0]->instance.yaw - leaves[1]->instance.yaw);
        CHECK(std::fabs(turned - 3.14159265F) < 0.01F);
    }
    // The house's interior is roofed now, so its inner faces wear plaster:
    // the thin one-sided plaster quad, turned half a turn so its face
    // looks into the room -- checked on the north wall's south face, which
    // looks south into the room (the quad's +Z is its face; yawed to face
    // south it would sit at pi, and turned to show that face it is at 0).
    bool plasterSeen = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallPlaster && p.lightY == 8 && p.lightX > 8 && p.lightX < 15 &&
            p.instance.position.z > 9.1F && p.instance.position.z < 9.3F) {
            plasterSeen = true;
            CHECK(p.instance.yaw == doctest::Approx(0.0F));
        }
    }
    CHECK(plasterSeen);
    // And no kit wall stands on an indoor face: its other side is brick.
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Wall) {
            CHECK((p.lightX == 8 || p.lightX == 15 || p.lightY == 8 || p.lightY == 15 || p.lightX == 18 ||
                   p.lightX == 24));
        }
    }
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
    CHECK(countRole(lit.placements, PieceRole::LampBracket) == 1);
    CHECK(countRole(lit.placements, PieceRole::LampPost) == 1);
    CHECK(countRole(lit.placements, PieceRole::Brazier) == 1);
    CHECK(countRole(lit.placements, PieceRole::Ember) == 1);
    // Every lamp but the post (its glass is its own) carries ONE flame
    // quad, drawn as its own light and turned to the eye by the world
    // scene (a billboard about its centre): never a crossed pair, whose
    // edge-on quad was a bright bar at arm's length.
    CHECK(countRole(lit.placements, PieceRole::Flame) == 2);
    for (const StaticPlacement& p : lit.placements) {
        if (p.role == PieceRole::LampWall) {
            // On the door-side wall's south face (z = 16), hung from its
            // bracket out over the lamp's own tile, at head height and
            // above.
            const PieceSpec* lantern = catalogue.piece(PieceRole::LampWall);
            CHECK(p.instance.position.z == doctest::Approx(16.0F + lantern->standoff));
            CHECK(p.instance.position.x == doctest::Approx(9.5F));
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + lantern->lift));
            CHECK(p.selfLit);
        }
        if (p.role == PieceRole::Flame) {
            CHECK(p.selfLit);
            CHECK(p.instance.scale.z < 0.0F);
            // A halo: its alpha falls off over the quad's own extent.
            CHECK(p.mode == kDrawHalo);
            CHECK(p.instance.mode == kDrawHalo);
            CHECK(p.instance.gradientTo == doctest::Approx(catalogue.piece(PieceRole::Flame)->width));
            CHECK(p.instance.gradientToZ == doctest::Approx(catalogue.piece(PieceRole::Flame)->height));
            // A billboard, anchored on the flame's centre: the quad's
            // origin is half its width and height off the anchor.
            CHECK(p.billboard);
            CHECK(p.anchor.y == doctest::Approx(p.instance.position.y +
                                                0.5F * catalogue.piece(PieceRole::Flame)->height *
                                                    p.instance.scale.y));
        }
        // A lamp is its own light and stays flat; a thing with volume (the
        // door leaves) is shaded by its normals.
        if (p.role == PieceRole::LampWall || p.role == PieceRole::Brazier || p.role == PieceRole::Ember) {
            CHECK(p.mode == kDrawPlain);
        }
        if (p.role == PieceRole::DoorLeaf) {
            CHECK(p.mode == kDrawShaded);
        }
        // The brazier's tray and flame sit inside its cage at the stand's
        // own scale.
        if (p.role == PieceRole::Ember || (p.role == PieceRole::Flame && p.lightX == 6 && p.lightY == 5)) {
            const PieceSpec* stand = catalogue.piece(PieceRole::Brazier);
            CHECK(p.instance.position.y - render::bandSurface(19) < 1.9F * stand->scale);
            CHECK(p.instance.position.y - render::bandSurface(19) > 1.0F * stand->scale);
        }
    }
    CHECK(lit.placements.size() == placed.placements.size() + 3 + 3 + 1);
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
    // Brick out of doors on the brick buildings; the thin plaster quad on
    // every rendered and every indoor face.
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Wall)] > 100);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallPlaster)] > 2000);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallTimber)] > 100);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Chimney)] > 3);
    // Timber stands the plank quad on its edge: a quarter turn about X. A
    // hull's boards stand ACROSS -- a quarter turn about Z -- and lean
    // outward from it by the catalogue's flare. A lip is the slab's own
    // height; a parapet the coping's. Nothing else pitches or rolls.
    const float flare = catalogue.knobs().hullFlareDegrees * 3.14159265F / 180.0F;
    std::size_t hulls = 0;
    for (const StaticPlacement& p : first.placements) {
        const bool upright = p.role == PieceRole::WallTimber || p.role == PieceRole::LipPlank ||
                             p.role == PieceRole::LipStone || p.role == PieceRole::Parapet ||
                             p.role == PieceRole::Hull || p.role == PieceRole::QuayWall;
        if (p.role == PieceRole::Hull) {
            ++hulls;
            CHECK(p.instance.pitch == 0.0F);
            CHECK(p.instance.roll == doctest::Approx(3.14159265F / 2.0F + flare));
            CHECK(p.alongZ);
        } else {
            CHECK(p.instance.roll == 0.0F);
        }
        if (p.role == PieceRole::WallTimber) {
            CHECK(p.instance.pitch == doctest::Approx(-3.14159265F / 2.0F));
            if (p.instance.scale.z > 0.5F) {
                CHECK(p.instance.scale.z == doctest::Approx((render::kBandHeight - 0.01F) / 2.5F));
            }
        } else if (p.role == PieceRole::LipPlank || p.role == PieceRole::LipStone) {
            CHECK(p.instance.pitch == doctest::Approx(-3.14159265F / 2.0F));
            CHECK(p.instance.scale.z == doctest::Approx((render::kFloorSlab - 0.01F) / 2.5F));
        } else if (!upright) {
            CHECK(p.instance.pitch == 0.0F);
        }
    }
    // The moored hulls lean: their boards, and a gunwale beam along the
    // open top of each.
    CHECK(hulls > 10);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Gunwale)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::LipPlank)] > 20);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::LipStone)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Rowboat)] > 3);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Crane)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Parapet)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::RoofTile)] > 20);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::DoorInside)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::DoorLeaf)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Joist)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Table)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Fireplace)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Pillar)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::Post)] > 0);
    // Two jambs on every door in the district (gates keep their own posts),
    // plus the Tarwalk's two pairs before the Gull, its hitching post and
    // the yard's rail grid as street posts; the taproom's tables and the
    // lone piers stay pillars.
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::DoorPost)] > 4);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::QuayWall)] > 0);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::RoofFlag)] > 0);
    // The quay wall wears the stone piece, never the cornice: no cornice on
    // the harbour band, and every stone face there has the water before it.
    for (const StaticPlacement& p : first.placements) {
        if (p.role == PieceRole::RoofEdge) {
            CHECK(p.lightZ > catalogue.minBand());
        }
        if (p.role == PieceRole::QuayWall) {
            CHECK(p.lightZ == catalogue.minBand());
            CHECK(p.instance.pitch < 0.0F);
        }
    }
    // The chimneys stand over masonry only, and they vary.
    bool chimneyVariants[3] = {false, false, false};
    std::size_t chimneys = 0;
    for (const StaticPlacement& p : first.placements) {
        if (p.role == PieceRole::Chimney) {
            const std::int32_t x = static_cast<std::int32_t>(std::floor(p.instance.position.x));
            const std::int32_t y = static_cast<std::int32_t>(std::floor(p.instance.position.z));
            CHECK(tilesA.form(x, y, p.lightZ - 1) == content::TileForm::Wall);
            const MaterialRule* below = catalogue.material(tilesA.material(x, y, p.lightZ - 1));
            REQUIRE(below != nullptr);
            CHECK(below->wallClass == WallClass::Masonry);
            const std::size_t v = static_cast<std::size_t>(p.instance.piece) -
                                  static_cast<std::size_t>(catalogue.pieceIndex(PieceRole::Chimney));
            REQUIRE(v < 3);
            chimneyVariants[v] = true;
            ++chimneys;
        }
    }
    MESSAGE("Docks chimneys: " << chimneys);
    CHECK((chimneyVariants[0] ? 1 : 0) + (chimneyVariants[1] ? 1 : 0) + (chimneyVariants[2] ? 1 : 0) >= 2);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallCorner)] >= 4);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallWindow)] > 20);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::WallDoor)] >= 2);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::RoofEdge)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorPlank)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorCobble)] > 50);
    CHECK(stats.byRole[static_cast<std::size_t>(PieceRole::FloorFlag)] >= 0);
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
        // (Lit from jamb to jamb: its A end is the cut between the west
        // jamb 152 and the gap cell 153, the north face running east.)
        if (p.role == PieceRole::WallDoor && p.lightX == 152 && p.endAX == 153 && p.lightY == 66 &&
            p.lightZ == 19) {
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
    // one level under the harbour's surface; a joist hangs in the room
    // under its ceiling), and everything is inside the authored district.
    for (const StaticPlacement& p : first.placements) {
        const bool oneBelow =
            p.role == PieceRole::Ceiling || p.role == PieceRole::Water || p.role == PieceRole::Joist;
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
    // The frustum cull: of the pieces inside their reach, only those in
    // front of the eye and inside its cone are described.
    CHECK(worldA.stats().piecesInReach > worldA.stats().piecesInstanced);
    CHECK(worldA.stats().piecesInstanced * 3 > worldA.stats().piecesInReach);
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

// ---------------------------------------------------------------------------
// S LANE, SECOND PASS -- the critic's fourteen, each with a case
// ---------------------------------------------------------------------------

namespace {

/// A rendered stone house on the quayside band: a granite ring from (8, 8)
/// to (15, 15) on an oak floor, a two-tile door in its south wall, a roof
/// of thatch over all of it at z = 20 -- and, over the taproom, a one-cell
/// oak partition standing on the roof band where a floor would be, so one
/// ceiling cell is the underside of a WALL. A trudgeon post stands inside
/// (a table, in the sim's own terms) and another on the street; a two-cell
/// granite block stands free inside the room (a hearth).
struct HouseWorld {
    content::World world{content::Coords(1, 1, 3), content::LaneLayout::core()};
    std::span<std::uint8_t> forms = world.byteLane(content::kFormLane);
    std::span<std::uint16_t> materials = world.shortLane(content::kMaterialLane);
    sim::TileQuery tiles{world};

    HouseWorld() {
        std::fill(forms.begin(), forms.end(), static_cast<std::uint8_t>(content::TileForm::Open));
        for (std::int32_t y = 4; y <= 27; ++y) {
            for (std::int32_t x = 4; x <= 27; ++x) {
                put(x, y, 18, content::TileForm::Wall, materialId("dirt"));
                put(x, y, 19, content::TileForm::Floor, materialId("brick"));
            }
        }
        for (std::int32_t y = 8; y <= 15; ++y) {
            for (std::int32_t x = 8; x <= 15; ++x) {
                const bool ring = x == 8 || x == 15 || y == 8 || y == 15;
                put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor,
                    ring ? materialId("granite") : materialId("oak"));
                put(x, y, 20, content::TileForm::Floor, materialId("thatch"));
            }
        }
        put(11, 15, 19, content::TileForm::Floor, materialId("oak"));
        put(12, 15, 19, content::TileForm::Floor, materialId("oak"));
        // The partition over the room's (13, 10): a WALL on the roof band.
        put(13, 10, 20, content::TileForm::Wall, materialId("oak"));
        // A post inside (the sim's table) and one on the street.
        put(13, 12, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
        put(20, 20, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
        // The hearth: two granite cells against nothing, two rows in from
        // the north wall, the room to their south.
        put(10, 10, 19, content::TileForm::Wall, materialId("granite"));
        put(11, 10, 19, content::TileForm::Wall, materialId("granite"));
    }

    void put(std::int32_t x, std::int32_t y, std::int32_t z, content::TileForm form,
             std::uint16_t material) {
        forms[tiles.index(x, y, z)] = static_cast<std::uint8_t>(form);
        materials[tiles.index(x, y, z)] = material;
    }
};

const StaticPlacement* findRole(const std::vector<StaticPlacement>& placements, PieceRole role) {
    for (const StaticPlacement& p : placements) {
        if (p.role == role) {
            return &p;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("a wall cell over an open cell gets a ceiling") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    // The partition's underside: a ceiling quad over the taproom cell
    // (13, 10), lit as the room's underside and in the room's ceiling tint
    // (the thatch floor beside it), not the oak wall's own -- and at the
    // SLAB PLANE the floor ceilings round it hang at, so the room's ceiling
    // is one plane and no slab side shows round the partition.
    bool underside = false;
    // The slab over the room's pillar (13, 12) and over its hearth (10..11,
    // 10) is the room's ceiling too: a ceiling quad covers each.
    bool overPillar = false;
    bool overHearth = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::Ceiling) {
            continue;
        }
        CHECK(p.lightZ == 19);
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(20) - render::kFloorSlab - 0.004F));
        const float x0 = p.instance.position.x;
        const float z0 = p.instance.position.z;
        const auto covers = [&](float cx, float cz) {
            // A block's extent is its scale over the piece's own 2.5 m.
            const float w = p.instance.scale.x * 2.5F;
            const float h = p.instance.scale.z * 2.5F;
            return cx > std::min(x0, x0 + w) && cx < std::max(x0, x0 + w) && cz > std::min(z0, z0 + h) &&
                   cz < std::max(z0, z0 + h);
        };
        if (x0 == doctest::Approx(13.0F) && z0 == doctest::Approx(10.0F) && std::fabs(p.instance.scale.x) < 0.5F) {
            underside = true;
            CHECK(p.facing == doctest::Approx(render::kUndersideLift));
            const Rgba8 thatch = catalogue.materialByName("thatch")->ceilingTint;
            CHECK(p.instance.tint.r == thatch.r);
            CHECK(p.instance.tint.g == thatch.g);
            CHECK(p.instance.tint.b == thatch.b);
        }
        overPillar = overPillar || covers(13.5F, 12.5F);
        overHearth = overHearth || (covers(10.5F, 10.5F) && covers(11.5F, 10.5F));
    }
    CHECK(underside);
    CHECK(overPillar);
    CHECK(overHearth);
}

TEST_CASE("a door frame and the wall beside it share one light at the jamb") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    // The house's door is the two oak cells (11..12, 15) in the south ring
    // wall. The frame is lit from jamb to jamb; at each edge its value is
    // the point half way between the jamb cell and the gap cell beside it,
    // and the plaster piece that ends at that jamb is lit at the same
    // point -- so no step of light at the jamb.
    const StaticPlacement* frame = findRole(placed.placements, PieceRole::WallDoor);
    REQUIRE(frame != nullptr);
    CHECK(frame->gradient);
    // The south face runs west (a-order -x): the frame's A end is the cut
    // between the jamb at x = 13 and the gap cell 12, its B end the cut
    // between the gap cell 11 and the jamb at 10 (each pair in a-order).
    CHECK(frame->lightX == 13);
    CHECK(frame->endAX == 12);
    CHECK(frame->endAT == doctest::Approx(0.5F));
    CHECK(frame->lightX2 == 11);
    CHECK(frame->endBX == 10);
    CHECK(frame->endBT == doctest::Approx(0.5F));
    bool east = false;
    bool west = false;
    std::size_t doorBayWindows = 0;
    for (const StaticPlacement& p : placed.placements) {
        const bool southFace = p.lightZ == 19 && p.lightY == 15 && p.instance.position.z > 15.9F;
        if (!southFace) {
            continue;
        }
        if ((p.role == PieceRole::WallPlaster || p.role == PieceRole::Wall || p.role == PieceRole::WallWindow) &&
            p.gradient) {
            if (p.lightX2 == 13 && p.endBX == 12) {
                east = true;
                CHECK(p.endBT == doctest::Approx(0.5F));
            }
            if (p.lightX == 11 && p.endAX == 10) {
                west = true;
                CHECK(p.endAT == doctest::Approx(0.5F));
            }
        }
        // The window rhythm starts on the door's own bay: the two cells
        // beside each jamb carry a window, looking into the room's cell
        // behind the bay's middle.
        if (p.role == PieceRole::WallWindow && p.hasInside && p.insideY == 14 &&
            (p.insideX == 9 || p.insideX == 10 || p.insideX == 13 || p.insideX == 14)) {
            ++doorBayWindows;
        }
    }
    CHECK(east);
    CHECK(west);
    CHECK(doorBayWindows == 2);
}

TEST_CASE("the roof finish keys on the building top, whatever its tile says") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // The house's top re-tiled in brick, as a terrace over the room.
    for (std::int32_t y = 8; y <= 15; ++y) {
        for (std::int32_t x = 8; x <= 15; ++x) {
            house.put(x, y, 20, content::TileForm::Floor, materialId("brick"));
        }
    }
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    const RuleKnobs& knobs = catalogue.knobs();
    std::size_t flags = 0;
    std::size_t roofLips = 0;
    std::size_t battens = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.lightZ != 20) {
            continue;
        }
        // No street cobbles in the sky: the brick floor's own piece never
        // lands on the top. Nothing climbs to this top, so it is LEAD: the
        // dark fill and the batten seams, and no flag (paving is for a
        // deck a stair arrives on).
        CHECK(p.role != PieceRole::FloorCobble);
        if (p.role == PieceRole::RoofFlag) {
            ++flags;
        }
        if (p.role == PieceRole::RoofBatten) {
            ++battens;
            // Thin, in the roof tint (a shade over the fill: a lead roll
            // catches the light), on the surface, along a north-south
            // module line inside the plane (never its edge).
            CHECK(p.instance.tint.r == knobs.roofTint.r);
            CHECK(p.instance.tint.b == knobs.roofTint.b);
            CHECK(p.instance.scale.x < 0.5F);
            CHECK(p.instance.position.y > render::bandSurface(20));
            CHECK(p.instance.position.y < render::bandSurface(20) + 0.1F);
            const float line = p.instance.position.x;
            CHECK(std::fabs(line - std::round(line)) < 0.001F);
            CHECK(line > 8.0F);
            CHECK(line < 16.0F);
        }
        if (p.role == PieceRole::FloorFill) {
            CHECK(p.instance.tint.r == knobs.roofFillTint.r);
            CHECK(p.instance.tint.b == knobs.roofFillTint.b);
        }
        // The roof's edge is the roof's own dark, not the brick's red lip.
        if (p.role == PieceRole::LipStone) {
            ++roofLips;
            CHECK(p.instance.tint.r == knobs.roofFillTint.r);
            CHECK(p.instance.tint.g == knobs.roofFillTint.g);
        }
    }
    CHECK(flags == 0);
    // Seven interior lines (x = 9..15) over the 8 x 8 top, one batten run
    // each: the whole plane is lead with seams every metre.
    CHECK(battens == 7);
    CHECK(roofLips > 0);
    // A HATCH: a stair in the room under the terrace's middle makes the
    // roof round it a deck, and the deck keeps its flags (a whole 3 x 3
    // block at the flag's own module, in the roof tint) with no batten
    // across it; the rest of the top stays lead.
    house.put(12, 12, 19, content::TileForm::Stair, materialId("oak"));
    const StaticPlacements decked = placeStaticPieces(house.tiles, catalogue, {});
    std::size_t deckFlags = 0;
    std::size_t deckBattens = 0;
    for (const StaticPlacement& p : decked.placements) {
        if (p.lightZ != 20) {
            continue;
        }
        if (p.role == PieceRole::RoofFlag) {
            ++deckFlags;
            CHECK(std::fabs(p.instance.scale.x) == doctest::Approx(3.0F / 2.9125F).epsilon(0.01F));
            CHECK(p.instance.tint.r == knobs.roofTint.r);
            CHECK(p.instance.tint.b == knobs.roofTint.b);
            CHECK(std::abs(p.lightX - 12) <= 3);
            CHECK(std::abs(p.lightY - 12) <= 3);
        }
        if (p.role == PieceRole::RoofBatten) {
            ++deckBattens;
        }
    }
    CHECK(deckFlags >= 1);
    CHECK(deckFlags <= 4);
    CHECK(deckBattens > 0);
    CHECK(deckBattens < battens + 8);
    // The street stays a street: cobbles on the brick at 19, no roof flag.
    bool cobbles = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.lightZ == 19 && p.role == PieceRole::RoofFlag) {
            CHECK(false);
        }
        cobbles = cobbles || (p.lightZ == 19 && p.role == PieceRole::FloorCobble);
    }
    CHECK(cobbles);
}

TEST_CASE("a lit room's window is warm at night") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    std::vector<render::Lamp> lamps(1);
    lamps[0].name = "lamp_house_bar";
    lamps[0].x = 12;
    lamps[0].y = 11;
    lamps[0].z = 19;
    lamps[0].luminance = 26;  // reaches the far bay by the door
    lamps[0].warmth = render::LampWarmth::Lantern;
    const render::LampGlow glow = render::LampGlow::build(house.tiles, lamps);
    REQUIRE(glow.litCellCount() > 0);
    WorldScene world(house.tiles, proceduralAtlas(), &glow, &catalogue, &lamps);
    render::Camera eye;
    eye.x = 11.5F;
    eye.y = 20.5F;
    eye.z = render::bandSurface(19) + 1.6F;
    eye.yaw = 0.0F;  // north, at the house's south face
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    WorldSceneParams params;
    SceneDescription scene;
    const Rgba8 warm = catalogue.knobs().litPane;
    const auto panes = [&](int hour, std::size_t& windows, std::size_t& lit) {
        params.timeOfDaySeconds = hour * 3600;
        world.refresh(scene, eye, 16.0F / 9.0F, params);
        windows = 0;
        lit = 0;
        for (const StaticInstance& piece : scene.statics) {
            if (piece.role != static_cast<std::uint8_t>(PieceRole::WallWindow)) {
                continue;
            }
            ++windows;
            if (piece.pane.r == warm.r && piece.pane.g == warm.g && piece.pane.b == warm.b) {
                ++lit;
            }
        }
    };
    std::size_t windows = 0;
    std::size_t lit = 0;
    // At noon the panes are dark whatever the lamp does; at nine at night
    // the room glows and every window the eye sees is warm.
    panes(12, windows, lit);
    CHECK(windows >= 1);
    CHECK(lit == 0);
    // Every window on the ring knows the room behind it.
    std::size_t placedWindows = 0;
    for (const StaticPlacement& p : world.placements()) {
        if (p.role == PieceRole::WallWindow) {
            ++placedWindows;
            CHECK(p.hasInside);
            CHECK(house.tiles.form(p.insideX, p.insideY, p.insideZ) == content::TileForm::Floor);
        }
    }
    CHECK(placedWindows >= 2);
    panes(21, windows, lit);
    CHECK(windows >= 1);
    CHECK(lit == windows);
    // And with the lamp out, only the windows the tile hash keeps a candle
    // behind (two in three, a roofed room behind them) stay warm: the
    // others go dark.
    const std::vector<render::Lamp> none;
    const render::LampGlow dark = render::LampGlow::build(house.tiles, none);
    WorldScene unlit(house.tiles, proceduralAtlas(), &dark, &catalogue, &none);
    SceneDescription sceneUnlit;
    params.timeOfDaySeconds = 21 * 3600;
    unlit.refresh(sceneUnlit, eye, 16.0F / 9.0F, params);
    std::size_t seen = 0;
    std::size_t homely = 0;
    for (const StaticPlacement& p : unlit.placements()) {
        if (p.role == PieceRole::WallWindow) {
            CHECK(p.hasInside);
            homely += p.homely ? 1 : 0;
        }
    }
    for (const StaticInstance& piece : sceneUnlit.statics) {
        if (piece.role == static_cast<std::uint8_t>(PieceRole::WallWindow)) {
            ++seen;
        }
    }
    CHECK(seen >= 1);
    CHECK(homely <= placedWindows);
    std::size_t warmSeen = 0;
    for (const StaticInstance& piece : sceneUnlit.statics) {
        if (piece.role == static_cast<std::uint8_t>(PieceRole::WallWindow) && piece.pane.r == warm.r &&
            piece.pane.g == warm.g && piece.pane.b == warm.b) {
            ++warmSeen;
        }
    }
    CHECK(warmSeen <= homely);
}

TEST_CASE("a timber post is a pillar, plaster indoors and timber out, and a pile beside the water") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // A third post against the house's east wall (a pilaster: one wall
    // neighbour, of another class), and a fourth on the street with the
    // harbour dug beside it.
    house.put(16, 11, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(24, 24, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(25, 24, 19, content::TileForm::Open, materialId("dirt"));
    house.world.shortLane(content::kFluidLane)[house.tiles.index(25, 24, 19)] = 3;
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    // Indoors: one pillar on the cell, fitted to it, in the catalogue's own
    // plaster colour, and no board on any of its four faces. Out of doors
    // (the street post, the pilaster, the partition on the roof band): the
    // same pillar in the material's timber, and no boards either.
    const PieceSpec* spec = catalogue.piece(PieceRole::Pillar);
    REQUIRE(spec != nullptr);
    const Rgba8 timber = catalogue.materialByName("trudgeon_wood")->topTint;
    std::size_t pillars = 0;
    bool indoor = false;
    bool street = false;
    bool pilaster = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Pillar) {
            ++pillars;
            CHECK(p.instance.scale.x > 2.0F);
            CHECK(p.mode == kDrawShaded);
            if (p.lightX == 13 && p.lightY == 12) {
                indoor = true;
                CHECK(p.instance.position.x == doctest::Approx(13.5F));
                CHECK(p.instance.position.z == doctest::Approx(12.5F));
                CHECK(p.instance.tint.r == spec->tint.r);
                CHECK(p.instance.tint.g == spec->tint.g);
            }
            if (p.lightX == 20 && p.lightY == 20) {
                // Timber: the material's top tint, lifted (a post in the
                // light), over the pillar's own.
                street = true;
                CHECK(p.instance.tint.r < spec->tint.r);
                CHECK(p.instance.tint.r > static_cast<std::uint8_t>((spec->tint.r * timber.r + 127) / 255));
                CHECK(p.instance.tint.b < p.instance.tint.r);
            }
            pilaster = pilaster || (p.lightX == 16 && p.lightY == 11);
        }
        if (p.role == PieceRole::WallTimber) {
            const bool onPillar = (p.lightX == 13 && p.lightY == 12) || (p.lightX == 20 && p.lightY == 20) ||
                                  (p.lightX == 16 && p.lightY == 11);
            CHECK_FALSE(onPillar);
        }
    }
    CHECK(pillars == 4);
    CHECK(indoor);
    CHECK(street);
    CHECK(pilaster);
    // Beside the water: no pillar; the boards stay, tarred (darker than the
    // material's own tint), and ONE pile stands through the cell's centre,
    // on its foot, its head over the core. Under a deck (the water dug
    // beside the roofed cell (26, 24)) the pile stops a hair short of it.
    CHECK(countRole(placed.placements, PieceRole::Post) == 1);
    const StaticPlacement* pile = findRole(placed.placements, PieceRole::Post);
    REQUIRE(pile != nullptr);
    CHECK(pile->lightX == 24);
    CHECK(pile->lightY == 24);
    CHECK(pile->instance.position.x == doctest::Approx(24.5F));
    CHECK(pile->instance.position.z == doctest::Approx(24.5F));
    CHECK(pile->instance.position.y == doctest::Approx(render::bandSurface(19)));
    const PieceSpec* beam = catalogue.piece(PieceRole::Post);
    REQUIRE(beam != nullptr);
    CHECK(pile->instance.scale.y * beam->height > render::kBandHeight + 0.5F);
    house.put(26, 24, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(26, 24, 20, content::TileForm::Floor, materialId("trudgeon_wood"));
    const StaticPlacements decked = placeStaticPieces(house.tiles, catalogue, {});
    CHECK(countRole(decked.placements, PieceRole::Post) == 2);
    for (const StaticPlacement& p : decked.placements) {
        if (p.role == PieceRole::Post && p.lightX == 26) {
            CHECK(p.instance.scale.y * beam->height < render::kBandHeight);
        }
        if (p.role == PieceRole::Pillar) {
            const bool underDeck = p.lightX == 26 && p.lightY == 24;
            CHECK_FALSE(underDeck);
        }
    }
    std::size_t tarred = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallTimber && p.lightX == 24 && p.lightY == 24) {
            const Rgba8 own = catalogue.materialByName("trudgeon_wood")->tint;
            CHECK(p.instance.tint.r < own.r);
            CHECK(p.instance.tint.g < own.g);
            ++tarred;
        }
        if (p.role == PieceRole::Pillar) {
            const bool onPile = p.lightX == 24 && p.lightY == 24;
            CHECK_FALSE(onPile);
        }
    }
    CHECK(tarred == 4);
}

TEST_CASE("a lantern's room gets tables and a hearth") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // No lantern: no tables. Then a lantern in the room.
    const StaticPlacements bare = placeStaticPieces(house.tiles, catalogue, {});
    CHECK(countRole(bare.placements, PieceRole::Table) == 0);
    std::vector<render::Lamp> lamps(1);
    lamps[0].name = "lamp_house_bar";
    lamps[0].x = 12;
    lamps[0].y = 11;
    lamps[0].z = 19;
    lamps[0].luminance = 18;
    lamps[0].warmth = render::LampWarmth::Lantern;
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, lamps);
    CHECK(countRole(placed.placements, PieceRole::Table) >= 1);
    CHECK(countRole(placed.placements, PieceRole::Bench) >= 2);
    CHECK(countRole(placed.placements, PieceRole::Mug) >= 1);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Table) {
            // Inside the room, two from every wall, on the floor.
            CHECK(p.instance.position.x > 9.9F);
            CHECK(p.instance.position.x < 14.1F);
            CHECK(p.instance.position.z > 10.9F);
            CHECK(p.instance.position.z < 14.1F);
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19)));
        }
    }
    // Stools stand at the indoor pillar (the sim's own table), never at
    // the post on the street.
    CHECK(countRole(placed.placements, PieceRole::Stool) >= 1);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Stool) {
            CHECK(std::fabs(p.instance.position.x - 13.5F) < 1.6F);
            CHECK(std::fabs(p.instance.position.z - 12.5F) < 1.6F);
        }
    }
    // The hearth: the free-standing pair at (10..11, 10) wears the
    // fireplace on its south face, where the room is (one row of floor
    // and then the wall to its north), with a fire in it. The block is the
    // rule; it needs no lantern.
    CHECK(countRole(placed.placements, PieceRole::Fireplace) == 1);
    CHECK(countRole(bare.placements, PieceRole::Fireplace) == 1);
    const StaticPlacement* hearth = findRole(placed.placements, PieceRole::Fireplace);
    REQUIRE(hearth != nullptr);
    // Its footprint is centred on the pair: the origin sits its own local
    // centre (turned to face south, so along -X) back from x = 11.
    const PieceSpec* fireplace = catalogue.piece(PieceRole::Fireplace);
    CHECK(hearth->instance.position.x ==
          doctest::Approx(11.0F - 0.5F * (fireplace->minX + fireplace->maxX) * fireplace->scale));
    CHECK(hearth->instance.position.z > 11.0F);
    CHECK(hearth->instance.position.z < 12.5F);
    CHECK(hearth->instance.yaw == doctest::Approx(0.0F));
    CHECK(countRole(placed.placements, PieceRole::Flame) >= 1 + 1);
}

TEST_CASE("a roof plane is flat, dressed, and chimneyed over masonry only") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // A second house of oak walls east of the first, thatch over it.
    for (std::int32_t y = 8; y <= 15; ++y) {
        for (std::int32_t x = 18; x <= 25; ++x) {
            const bool ring = x == 18 || x == 25 || y == 8 || y == 15;
            house.put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor, materialId("oak"));
            house.put(x, y, 20, content::TileForm::Floor, materialId("thatch"));
        }
    }
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    // The thatch planes wear the flat fill (tar), never floorboards, and an
    // upstand runs round every edge: brick over the granite ring, boards
    // over the oak one.
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::FloorPlank) {
            CHECK(p.lightZ != 20);
        }
    }
    std::size_t brickUpstands = 0;
    std::size_t boardUpstands = 0;
    const auto parapetPiece = static_cast<std::uint16_t>(catalogue.pieceIndex(PieceRole::Parapet));
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Parapet) {
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(20)));
            if (p.instance.piece == parapetPiece) {
                ++brickUpstands;
                CHECK(p.instance.position.x < 17.0F);
            } else {
                ++boardUpstands;
                CHECK(p.instance.position.x > 17.0F);
            }
        }
    }
    CHECK(brickUpstands == 4);
    CHECK(boardUpstands == 4);
    // Chimneys, if any, stand over the granite ring and never the oak.
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Chimney) {
            CHECK(p.instance.position.x < 17.0F);
        }
    }
    // Nothing pitched over either plane: every piece on the roof band is
    // flat, an upstand, a lip, or the boards of the lone partition.
    for (const StaticPlacement& p : placed.placements) {
        if (p.lightZ == 20 && p.role != PieceRole::Parapet && p.role != PieceRole::LipStone &&
            p.role != PieceRole::WallTimber) {
            CHECK(p.instance.pitch == 0.0F);
        }
    }
}

TEST_CASE("DIAG: the cells round the Gull's door and the hulls, printed") {
    const sim::TileQuery tiles(docksWorld());
    const StaticCatalogue& catalogue = shippedCatalogue();
    const StaticPlacements placed = placeStaticPieces(tiles, catalogue, {});
    const auto formName = [](content::TileForm f) {
        switch (f) {
            case content::TileForm::Wall: return "W";
            case content::TileForm::Floor: return "F";
            case content::TileForm::Open: return ".";
            case content::TileForm::Void: return "#";
            case content::TileForm::Ramp: return "R";
            case content::TileForm::Stair: return "S";
            default: return "?";
        }
    };
    const std::span<const std::string_view> ids = render::materialIds();
    for (std::int32_t z = 19; z <= 21; ++z) {
        std::string grid = "z=" + std::to_string(z) + "\n";
        for (std::int32_t y = 56; y <= 68; ++y) {
            grid += std::to_string(y) + ": ";
            for (std::int32_t x = 142; x <= 164; ++x) {
                grid += formName(tiles.form(x, y, z));
                const std::uint16_t m = tiles.material(x, y, z);
                const std::string mat = m < ids.size() ? std::string(ids[m]) : "?";
                grid += mat.empty() ? '?' : mat[0];
                grid += cellRoofed(tiles, x, y, z) ? '^' : ' ';
                grid += ' ';
            }
            grid += "\n";
        }
        MESSAGE(grid);
    }
    // The timber cells on the street band round the door: what each is.
    for (std::int32_t y = 56; y <= 68; ++y) {
        for (std::int32_t x = 142; x <= 164; ++x) {
            if (tiles.form(x, y, 19) != content::TileForm::Wall) {
                continue;
            }
            const std::uint16_t m = tiles.material(x, y, 19);
            const MaterialRule* r = catalogue.material(m);
            if (r == nullptr || r->wallClass != WallClass::Timber) {
                continue;
            }
            int walls = 0;
            for (int s = 0; s < 4; ++s) {
                const std::int32_t nx = x + (s == 1 ? 1 : (s == 3 ? -1 : 0));
                const std::int32_t ny = y + (s == 2 ? 1 : (s == 0 ? -1 : 0));
                walls += tiles.form(nx, ny, 19) == content::TileForm::Wall ? 1 : 0;
            }
            std::string roles;
            for (const StaticPlacement& p : placed.placements) {
                if (p.lightX == x && p.lightY == y && p.lightZ == 19) {
                    roles += std::string(pieceRoleName(p.role)) + " ";
                }
            }
            MESSAGE("timber cell (" << x << "," << y << ") material " << (m < ids.size() ? ids[m] : "?")
                                     << " wallNeighbours=" << walls << " above=" << formName(tiles.form(x, y, 20))
                                     << " roles: " << roles);
        }
    }
    // Every hull face: its cell, side, and what stands behind it.
    int hullFaces = 0;
    int hullRoomBehind = 0;
    std::string hullNote;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::Hull) {
            continue;
        }
        ++hullFaces;
        // The face's normal from the yaw: local -Z out... the piece is
        // across; use the light cell and the position to find the side.
        const float cx = static_cast<float>(p.lightX) + 0.5F;
        const float cz = static_cast<float>(p.lightY) + 0.5F;
        const float dx = p.instance.position.x - cx;
        const float dz = p.instance.position.z - cz;
        int sx = 0, sy = 0;
        if (std::fabs(dx) > std::fabs(dz)) {
            sx = dx > 0 ? 1 : -1;
        } else {
            sy = dz > 0 ? 1 : -1;
        }
        const std::int32_t bx = p.lightX - sx;
        const std::int32_t by = p.lightY - sy;
        const bool room = (tiles.form(bx, by, p.lightZ) == content::TileForm::Floor) && cellRoofed(tiles, bx, by, p.lightZ);
        if (room) {
            ++hullRoomBehind;
            if (hullNote.size() < 1500) {
                hullNote += "(" + std::to_string(p.lightX) + "," + std::to_string(p.lightY) + "," +
                            std::to_string(p.lightZ) + ") behind=" + formName(tiles.form(bx, by, p.lightZ)) +
                            (cellRoofed(tiles, bx, by, p.lightZ) ? "^" : "") + "; ";
            }
        }
    }
    MESSAGE("hull faces " << hullFaces << ", with a roofed floor behind " << hullRoomBehind << ": " << hullNote);
    // Every timber wall cell beside the harbour on the street band: what its
    // eight neighbours are (form + roofed), so the hull rule can be read
    // off the real map rather than guessed.
    std::string beside;
    int besideCount = 0;
    for (std::int32_t y = 36; y <= 60 && beside.size() < 6000; ++y) {
        for (std::int32_t x = 120; x <= 160; ++x) {
            for (std::int32_t z = 18; z <= 20; ++z) {
                if (tiles.form(x, y, z) != content::TileForm::Wall) {
                    continue;
                }
                const MaterialRule* r = catalogue.material(tiles.material(x, y, z));
                if (r == nullptr || r->wallClass != WallClass::Timber) {
                    continue;
                }
                bool water = false;
                for (int sx = -1; sx <= 1 && !water; ++sx) {
                    for (int sy = -1; sy <= 1 && !water; ++sy) {
                        if ((sx == 0) == (sy == 0)) {
                            continue;
                        }
                        water = (tiles.form(x + sx, y + sy, z) == content::TileForm::Open &&
                                 tiles.fluidDepth(x + sx, y + sy, z) > 0) ||
                                (tiles.form(x + sx, y + sy, z - 1) == content::TileForm::Open &&
                                 tiles.fluidDepth(x + sx, y + sy, z - 1) > 0);
                    }
                }
                if (!water) {
                    continue;
                }
                ++besideCount;
                const std::uint16_t mBelow = tiles.material(x, y, z - 1);
                const std::uint16_t mAbove = tiles.material(x, y, z + 1);
                beside += "(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ") below=" +
                          formName(tiles.form(x, y, z - 1)) + (mBelow < ids.size() ? std::string(ids[mBelow]) : "?") +
                          " above=" + formName(tiles.form(x, y, z + 1)) +
                          (mAbove < ids.size() ? std::string(ids[mAbove]) : "?") + " [";
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            beside += "* ";
                            continue;
                        }
                        beside += formName(tiles.form(x + dx, y + dy, z));
                        beside += cellRoofed(tiles, x + dx, y + dy, z) ? "^" : " ";
                    }
                    beside += "|";
                }
                beside += "] ";
            }
        }
    }
    MESSAGE("timber walls beside water on 18..20 in x120..160 y36..60: " << besideCount << " -- " << beside);
    // Roof cells reachable by a stair or ramp beneath / beside.
    int roofFlags = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::RoofFlag) {
            ++roofFlags;
        }
    }
    int stairsUnderRoof = 0;
    for (std::int32_t z = 19; z < tiles.sizeZ(); ++z) {
        for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
            for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
                const content::TileForm f = tiles.form(x, y, z);
                if (f != content::TileForm::Stair && f != content::TileForm::Ramp) {
                    continue;
                }
                if (tiles.form(x, y, z + 1) == content::TileForm::Open) {
                    ++stairsUnderRoof;
                }
            }
        }
    }
    MESSAGE("roof flag blocks " << roofFlags << ", stairs/ramps with sky over them " << stairsUnderRoof);
}

TEST_CASE("a timber wall with a roofed room behind it is a building, not a hull") {
    // THE HULL EXCLUSION. Two timber walls beside the harbour: one closes a
    // roofed shed (a floor behind it with a slab over it) and wears the
    // upright boards of any building; the other stands before open air --
    // a moored hull -- and wears the tarred boards across, with the
    // gunwale beam along its open top.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // Water along x = 4..5 at rows 18..25, one band down and at 19.
    for (std::int32_t y = 18; y <= 25; ++y) {
        for (std::int32_t x = 4; x <= 5; ++x) {
            house.put(x, y, 19, content::TileForm::Open, materialId("dirt"));
            house.world.shortLane(content::kFluidLane)[house.tiles.index(x, y, 19)] = 3;
        }
    }
    // The shed: a timber ring at x 6..8, rows 18..20, an oak floor inside,
    // a plank slab over the whole of it.
    for (std::int32_t y = 18; y <= 20; ++y) {
        for (std::int32_t x = 6; x <= 8; ++x) {
            const bool ring = x == 6 || x == 8 || y == 18 || y == 20;
            house.put(x, y, 19, ring ? content::TileForm::Wall : content::TileForm::Floor,
                      ring ? materialId("trudgeon_wood") : materialId("oak"));
            house.put(x, y, 20, content::TileForm::Floor, materialId("trudgeon_wood"));
        }
    }
    // The hull: a timber run at x = 6, rows 23..25, the way a moored ship
    // is built -- its lower boards at 18 over the water with a roofed hold
    // of air behind them, the deck a floor at 19 over that, and the gunwale
    // at 19 standing on the boards with the open deck behind it.
    for (std::int32_t y = 23; y <= 25; ++y) {
        house.put(6, y, 18, content::TileForm::Wall, materialId("trudgeon_wood"));
        house.put(7, y, 18, content::TileForm::Open, materialId("dirt"));
        house.put(6, y, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
        house.put(7, y, 19, content::TileForm::Floor, materialId("trudgeon_wood"));
        for (std::int32_t x = 4; x <= 5; ++x) {
            house.put(x, y, 18, content::TileForm::Open, materialId("dirt"));
            house.world.shortLane(content::kFluidLane)[house.tiles.index(x, y, 18)] = 3;
        }
    }
    // And a fenced yard on piles: a timber wall at x = 6, rows 26..27, over
    // the water (nothing under it) with an open floor behind it and no
    // hull under it -- a building's wall, not a ship's.
    for (std::int32_t y = 26; y <= 27; ++y) {
        house.put(6, y, 18, content::TileForm::Open, materialId("dirt"));
        house.put(6, y, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
        house.put(7, y, 19, content::TileForm::Floor, materialId("trudgeon_wood"));
        house.put(4, y, 19, content::TileForm::Open, materialId("dirt"));
        house.put(5, y, 19, content::TileForm::Open, materialId("dirt"));
        house.world.shortLane(content::kFluidLane)[house.tiles.index(4, y, 19)] = 3;
        house.world.shortLane(content::kFluidLane)[house.tiles.index(5, y, 19)] = 3;
    }
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    std::size_t shedHull = 0;
    std::size_t shedBoards = 0;
    std::size_t hullBoards = 0;
    std::size_t gunwaleBoards = 0;
    std::size_t yardHull = 0;
    std::size_t yardBoards = 0;
    for (const StaticPlacement& p : placed.placements) {
        const bool shedWest = p.lightX == 6 && p.lightY >= 18 && p.lightY <= 20 && p.lightZ == 19;
        const bool hullWest = p.lightX == 6 && p.lightY >= 23 && p.lightY <= 25 && p.lightZ == 18;
        const bool gunwaleWest = p.lightX == 6 && p.lightY >= 23 && p.lightY <= 25 && p.lightZ == 19;
        const bool yardWest = p.lightX == 6 && p.lightY >= 26 && p.lightY <= 27 && p.lightZ == 19;
        if (p.role == PieceRole::Hull) {
            shedHull += shedWest ? 1 : 0;
            hullBoards += hullWest ? 1 : 0;
            gunwaleBoards += gunwaleWest ? 1 : 0;
            yardHull += yardWest ? 1 : 0;
        }
        if (p.role == PieceRole::WallTimber && p.instance.position.x < 6.5F) {
            shedBoards += shedWest ? 1 : 0;
            yardBoards += yardWest ? 1 : 0;
        }
    }
    CHECK(shedHull == 0);
    CHECK(shedBoards > 0);
    CHECK(hullBoards > 0);
    CHECK(gunwaleBoards > 0);
    CHECK(yardHull == 0);
    CHECK(yardBoards > 0);
}

TEST_CASE("a timber post beside a door gap hangs the shop sign") {
    // THE POST'S JOB. Two lone timber cells flank a door gap on the street:
    // the one before the gap along its line carries the shop sign on its
    // street face, its bracket out over the street at sign height; the
    // other stays a plain pier. A post beside no gap hangs nothing.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // The house's own door: cells (11, 15) and (12, 15) are already the
    // gap in the south wall (HouseWorld). Posts on the street a row south
    // of the jambs, at x = 10 and x = 13, row 16 -- lone timber cells.
    house.put(10, 16, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(13, 16, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    const StaticPlacements without = placeStaticPieces(house.tiles, catalogue, {});
    // Beside no gap (the gap's cells are (11..12, 15); the posts touch
    // (11, 16) and (12, 16), street): no sign.
    CHECK(countRole(without.placements, PieceRole::ShopSign) == 0);
    // Now posts directly beside the gap: the jamb cells themselves are
    // granite; put lone timber posts in the street cells flanking the gap
    // row's mouth by making a two-wide timber pair the gap's own sides --
    // simplest: move the gap. A gap between two timber posts on the street:
    // a wall line along row 22 with a two-cell gap at x 12..13, a roofed
    // room to its north (rows 19..21 floored and roofed).
    for (std::int32_t y = 19; y <= 21; ++y) {
        for (std::int32_t x = 9; x <= 16; ++x) {
            house.put(x, y, 19, content::TileForm::Floor, materialId("oak"));
            house.put(x, y, 20, content::TileForm::Floor, materialId("thatch"));
        }
    }
    for (std::int32_t x = 9; x <= 16; ++x) {
        house.put(x, 22, 19, content::TileForm::Wall, materialId("granite"));
        house.put(x, 22, 20, content::TileForm::Floor, materialId("thatch"));
    }
    house.put(12, 22, 19, content::TileForm::Floor, materialId("brick"));
    house.put(13, 22, 19, content::TileForm::Floor, materialId("brick"));
    house.put(12, 22, 20, content::TileForm::Open, materialId("thatch"));
    house.put(13, 22, 20, content::TileForm::Open, materialId("thatch"));
    // The timber posts ARE the jambs: (11, 22) and (14, 22) timber, lone
    // in class terms (their granite neighbours are another class), and
    // out in the open (nothing over them).
    house.put(11, 22, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(14, 22, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(11, 22, 20, content::TileForm::Open, materialId("thatch"));
    house.put(14, 22, 20, content::TileForm::Open, materialId("thatch"));
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    REQUIRE(placed.stats.doorGaps >= 1);
    std::size_t signs = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::ShopSign) {
            continue;
        }
        ++signs;
        // On the post before the gap (x = 11), on its south (street) face,
        // out over the street, at sign height, half the kit's size.
        CHECK(p.lightX == 11);
        CHECK(p.lightY == 22);
        CHECK(p.instance.position.z > 23.0F);
        CHECK(p.instance.position.x == doctest::Approx(11.5F));
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + 2.35F));
        CHECK(p.instance.scale.x == doctest::Approx(0.55F));
        CHECK(p.mode == kDrawShaded);
        // The bracket's +X points south, out from the face: a clockwise
        // yaw of a quarter turn.
        CHECK(p.instance.yaw == doctest::Approx(3.14159265F * 0.5F));
    }
    CHECK(signs == 1);
    // The jambs are DOOR POSTS, not pillars: the strapped timber post
    // fitted to each cell, the sign hung off the same face as before.
    std::size_t doorPosts = 0;
    for (const StaticPlacement& p : placed.placements) {
        const bool jamb = p.lightY == 22 && (p.lightX == 11 || p.lightX == 14);
        if (p.role == PieceRole::DoorPost && jamb) {
            ++doorPosts;
        }
        if (p.role == PieceRole::Pillar) {
            CHECK_FALSE(jamb);
        }
    }
    CHECK(doorPosts == 2);
}

TEST_CASE("a timber post with a job on the street is the strapped door post, a lone pier the pillar") {
    // THE JAMB IS TIMBER. The Gull's door posts read as metre-square
    // concrete columns: a lone timber cell out of doors was the kit's
    // concrete pillar in a timber tint whatever stood beside it. Now a post
    // within two cells of a door gap (the jamb, the hitching post against
    // the wall beside the door) wears the kit's strapped timber post
    // instead, fitted to the cell as the pillar was -- the sim's cell is
    // still a metre square and the chunk box inside it is drawn whatever
    // the catalogue says, so nothing thinner would hide it -- the storey
    // tall, in the material's own tint. A post with no door near it and
    // no partner stays the pillar; a post beside the water stays the pile.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const PieceSpec* spec = catalogue.piece(PieceRole::DoorPost);
    REQUIRE(spec != nullptr);
    const PieceSpec* pillar = catalogue.piece(PieceRole::Pillar);
    REQUIRE(pillar != nullptr);
    // The house's door is (11..12, 15), the street south of it. A post
    // diagonal to the gap's mouth at (13, 16) -- a hitching post at the
    // corner of the frontage -- and one three cells out at (12, 18): the
    // first is a door post, the second is out of reach and a pillar, like
    // HouseWorld's own street post at (20, 20). A pile at (24, 24).
    house.put(13, 16, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(12, 18, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(24, 24, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(25, 24, 19, content::TileForm::Open, materialId("dirt"));
    house.world.shortLane(content::kFluidLane)[house.tiles.index(25, 24, 19)] = 3;
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    const Rgba8 timber = catalogue.materialByName("trudgeon_wood")->tint;
    std::size_t doorPosts = 0;
    std::size_t jambs = 0;
    bool farPillar = false;
    bool streetPillar = false;
    for (const StaticPlacement& p : placed.placements) {
        // The house's own door at (11..12, 15) wears the two JAMBS on its
        // flank cells -- a different dress of the same piece, slim and cut
        // to the head. They are not this rule and are counted apart.
        if (p.role == PieceRole::DoorPost && p.lightY == 15) {
            ++jambs;
            CHECK((p.lightX == 10 || p.lightX == 13));
            continue;
        }
        if (p.role == PieceRole::DoorPost) {
            ++doorPosts;
            CHECK(p.lightX == 13);
            CHECK(p.lightY == 16);
            CHECK(p.lightZ == 19);
            CHECK(p.mode == kDrawShaded);
            CHECK(p.instance.position.x == doctest::Approx(13.5F));
            CHECK(p.instance.position.z == doctest::Approx(16.5F));
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + spec->lift));
            // Fitted to the cell: its section the cell's width plus the
            // catalogue's hair, its height the storey less a centimetre.
            CHECK(p.instance.scale.x * spec->width == doctest::Approx(1.0F + 2.0F * spec->thickness));
            CHECK(p.instance.scale.z == doctest::Approx(p.instance.scale.x));
            CHECK(p.instance.scale.y * spec->height == doctest::Approx(render::kBandHeight - 0.01F));
            CHECK(p.instance.pitch == 0.0F);
            CHECK(p.instance.roll == 0.0F);
            // The material's own tint over the piece's.
            CHECK(p.instance.tint.r == static_cast<std::uint8_t>((spec->tint.r * timber.r + 127) / 255));
            CHECK(p.instance.tint.g == static_cast<std::uint8_t>((spec->tint.g * timber.g + 127) / 255));
            CHECK(p.instance.tint.b == static_cast<std::uint8_t>((spec->tint.b * timber.b + 127) / 255));
        }
        if (p.role == PieceRole::Pillar) {
            CHECK_FALSE((p.lightX == 13 && p.lightY == 16));
            CHECK_FALSE((p.lightX == 24 && p.lightY == 24));
            farPillar = farPillar || (p.lightX == 12 && p.lightY == 18);
            streetPillar = streetPillar || (p.lightX == 20 && p.lightY == 20);
        }
        if (p.role == PieceRole::Post) {
            CHECK(p.lightX == 24);
            CHECK(p.lightY == 24);
        }
    }
    CHECK(doorPosts == 1);
    CHECK(jambs == 2);
    CHECK(farPillar);
    CHECK(streetPillar);
    CHECK(countRole(placed.placements, PieceRole::Post) == 1);
    // No sign on it: the sign wants the post on the gap's own line, four-
    // adjacent to a gap cell, and this one is diagonal to the mouth.
    CHECK(countRole(placed.placements, PieceRole::ShopSign) == 0);
    // Under a roof the same cell is a plastered pier again, whatever door
    // stands near it. The door's own two jambs are untouched: they are
    // cells of the frontage, not street furniture.
    house.put(13, 16, 20, content::TileForm::Floor, materialId("thatch"));
    const StaticPlacements roofed = placeStaticPieces(house.tiles, catalogue, {});
    CHECK(countRole(roofed.placements, PieceRole::DoorPost) == 2);
    for (const StaticPlacement& p : roofed.placements) {
        if (p.role == PieceRole::DoorPost) {
            CHECK(p.lightY == 15);
        }
    }
    bool pier = false;
    for (const StaticPlacement& p : roofed.placements) {
        if (p.role == PieceRole::Pillar && p.lightX == 13 && p.lightY == 16) {
            pier = true;
            CHECK(p.instance.tint.r == pillar->tint.r);
        }
    }
    CHECK(pier);
}

TEST_CASE("on the real Docks every jamb cell carries one beam and has lost its ground storey") {
    // THE DISTRICT, not a world built to contain the case. Whatever the map
    // says, every cell the jamb rule names carries exactly one slim beam,
    // and the mesher has taken that cell's box out under the head -- the
    // Gull's door, the Eel-Pots', every door in the ward, without a
    // coordinate hard-coded here.
    const sim::TileQuery tiles(docksWorld());
    const StaticCatalogue& catalogue = shippedCatalogue();
    const PieceSpec* spec = catalogue.piece(PieceRole::DoorPost);
    REQUIRE(spec != nullptr);
    const StaticPlacements placed = placeStaticPieces(tiles, catalogue, {});
    std::set<std::int64_t> seen;
    std::size_t jambBeams = 0;
    std::int32_t jambX = -1;
    std::int32_t jambY = -1;
    std::int32_t jambZ = -1;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::DoorPost || !doorPostAt(tiles, p.lightX, p.lightY, p.lightZ)) {
            continue;
        }
        ++jambBeams;
        // One beam per cell, never two stacked in the one place.
        const std::int64_t key = (static_cast<std::int64_t>(p.lightZ) << 40) |
                                 (static_cast<std::int64_t>(p.lightY) << 20) |
                                 static_cast<std::int64_t>(p.lightX);
        CHECK(seen.insert(key).second);
        // Slim, and cut to the head: the two facts the critic asked for.
        CHECK(spec->width * p.instance.scale.x > 0.2F);
        CHECK(spec->width * p.instance.scale.x < 0.4F);
        CHECK(p.instance.scale.y * spec->height == doctest::Approx(kDoorHeadHeight));
        if (jambZ < 0) {
            jambX = p.lightX;
            jambY = p.lightY;
            jambZ = p.lightZ;
        }
    }
    // The ward is full of doors and every one of them has two of these.
    CHECK(jambBeams >= 6);
    REQUIRE(jambZ >= 0);
    // And the mesh agrees, on the first jamb cell the rules found.
    const render::TileAtlas& atlas = proceduralAtlas();
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(atlas);
    const ChunkGeometry geometry =
        buildChunkGeometry(tiles, atlas, materials, chunkOf(jambX, jambY), 0);
    REQUIRE_FALSE(geometry.truncated);
    const float headY = render::bandSurface(jambZ) + kDoorHeadHeight;
    std::size_t faces = 0;
    for (const ChunkFace& face : geometry.faces) {
        if (face.x != jambX || face.y != jambY || face.z != jambZ) {
            continue;
        }
        ++faces;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            CHECK(geometry.positions[(face.firstVertex + corner) * 3 + 1] >= headY - 0.001F);
        }
    }
    // The lintel course is still meshed: the frontage is not open over the
    // door, it just has no block under the beam.
    CHECK(faces > 0);
}

TEST_CASE("a door wears two jambs, on the cells that flank it, slim and cut to the head") {
    // THE JAMBS. The critic's line on the first pass: metre-square,
    // storey-tall boxes that frame no door. They stood on lone timber cells
    // NEAR a door, fitted to the cell because the chunk box was still in
    // it. Now the rule names the two cells the door rule already demands --
    // the wall hard against the left of the opening and the wall hard
    // against its right -- and the beam stands there at its OWN section,
    // from the ground to the head, with a beam laid across the two heads.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const PieceSpec* spec = catalogue.piece(PieceRole::DoorPost);
    REQUIRE(spec != nullptr);
    const PieceSpec* wallSpec = catalogue.piece(PieceRole::Wall);
    REQUIRE(wallSpec != nullptr);
    const PieceSpec* lintel = catalogue.piece(PieceRole::Joist);
    REQUIRE(lintel != nullptr);
    // The frontage's finish: the wall piece's standoff plus half its
    // thickness out from the cell's own boundary plane.
    const float proud =
        (wallSpec->standoffSet ? wallSpec->standoff : wallSpec->thickness * 0.5F) +
        wallSpec->thickness * 0.5F;
    // The door rule itself, read on the two cells and on nothing else along
    // that wall.
    CHECK(doorPostAt(house.tiles, 10, 15, 19));
    CHECK(doorPostAt(house.tiles, 13, 15, 19));
    CHECK_FALSE(doorPostAt(house.tiles, 9, 15, 19));
    CHECK_FALSE(doorPostAt(house.tiles, 14, 15, 19));
    CHECK_FALSE(doorPostAt(house.tiles, 8, 15, 19));
    // The gap cells themselves are floor, not wall: never jambs.
    CHECK_FALSE(doorPostAt(house.tiles, 11, 15, 19));
    CHECK_FALSE(doorPostAt(house.tiles, 12, 15, 19));

    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    REQUIRE(placed.stats.doorGaps == 1);
    // Two, one per flank cell, and no third.
    CHECK(countRole(placed.placements, PieceRole::DoorPost) == 2);
    std::size_t west = 0;
    std::size_t east = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::DoorPost) {
            continue;
        }
        CHECK(p.lightY == 15);
        CHECK(p.lightZ == 19);
        CHECK(p.mode == kDrawShaded);
        // SLIM: the piece at its own section, a quarter of a metre across,
        // not the metre the cell is. This is the whole point.
        CHECK(p.instance.scale.x == doctest::Approx(spec->scale));
        CHECK(p.instance.scale.z == doctest::Approx(spec->scale));
        const float across = spec->width * p.instance.scale.x;
        CHECK(across > 0.2F);
        CHECK(across < 0.4F);
        // On the ground, cut off at the head: no cap block over the lintel.
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + spec->lift));
        CHECK(p.instance.scale.y * spec->height == doctest::Approx(kDoorHeadHeight));
        // Standing on the door's own plane (the south face of row 15), its
        // section straddling the frontage's finish.
        CHECK(p.instance.position.z == doctest::Approx(16.0F + proud));
        // Hard against the edge of the opening, inside its own cell.
        if (p.lightX == 10) {
            ++west;
            CHECK(p.instance.position.x > 10.5F);
            CHECK(p.instance.position.x < 11.0F);
        }
        if (p.lightX == 13) {
            ++east;
            CHECK(p.instance.position.x > 13.0F);
            CHECK(p.instance.position.x < 13.5F);
        }
    }
    CHECK(west == 1);
    CHECK(east == 1);
    // And the head beam across the two of them, its underside on the head.
    const float half = 0.5F * (lintel->maxX - lintel->minX) * lintel->scale;
    std::size_t heads = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Lintel && p.lightZ == 19 &&
            p.instance.position.z == doctest::Approx(16.0F + proud)) {
            ++heads;
            CHECK(p.instance.position.y ==
                  doctest::Approx(render::bandSurface(19) + kDoorHeadHeight + half + lintel->lift));
        }
    }
    CHECK(heads == 1);
}

TEST_CASE("the mesher leaves the jamb cells' ground storey out and meshes the wall over it") {
    // WHAT MAKES THE BEAM READ. A quarter-metre post inside a metre-square
    // box is a decal on a block, so the mesher takes the box out of the two
    // jamb cells from the ground to the head -- and ONLY to the head: the
    // lintel course over the opening still meshes, so the frontage has no
    // hole in it. The neighbouring wall then shows its own reveal down the
    // whole opening, and the cell under the jamb shows its top: the
    // threshold the beam stands on. Nothing is left see-through.
    HouseWorld house;
    const render::TileAtlas& atlas = proceduralAtlas();
    const ChunkMaterials materials = ChunkMaterials::fromAtlas(atlas);
    const ChunkGeometry geometry = buildChunkGeometry(house.tiles, atlas, materials,
                                                      chunkOf(10, 15), 0);
    const float floorY = render::bandSurface(19);
    const float headY = floorY + kDoorHeadHeight;
    const auto span = [&geometry](const ChunkFace& face, float& lowest, float& highest) {
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const float y = geometry.positions[(face.firstVertex + corner) * 3 + 1];
            lowest = std::min(lowest, y);
            highest = std::max(highest, y);
        }
    };
    std::size_t jambFaces = 0;
    std::size_t plainFaces = 0;
    float plainLowest = 1.0e9F;
    bool jambSoffit = false;
    bool revealFace = false;
    bool threshold = false;
    for (const ChunkFace& face : geometry.faces) {
        float lowest = 1.0e9F;
        float highest = -1.0e9F;
        span(face, lowest, highest);
        const bool jamb = face.z == 19 && face.y == 15 && (face.x == 10 || face.x == 13);
        if (jamb) {
            ++jambFaces;
            // Not one quad of this cell dips under the head.
            CHECK(lowest >= headY - 0.001F);
            CHECK(highest <= floorY + render::kBandHeight + 0.001F);
            if (face.dir == FaceDir::Bottom) {
                jambSoffit = true;
                CHECK(lowest == doctest::Approx(headY));
            }
        }
        // The wall cell next along the run still reaches the ground, and
        // shows the face it used to hide behind the jamb's box.
        if (face.z == 19 && face.y == 15 && face.x == 9) {
            ++plainFaces;
            plainLowest = std::min(plainLowest, lowest);
            if (face.dir == FaceDir::East) {
                revealFace = true;
                CHECK(lowest == doctest::Approx(floorY));
                CHECK(highest == doctest::Approx(floorY + render::kBandHeight));
            }
        }
        // The substrate under the jamb is no longer covered: its top face
        // is the threshold.
        if (face.z == 18 && face.y == 15 && face.x == 10 && face.dir == FaceDir::Top) {
            threshold = true;
            CHECK(lowest == doctest::Approx(floorY));
        }
    }
    // The lintel course is still there -- the frontage is not open above
    // the door.
    CHECK(jambFaces > 0);
    CHECK(jambSoffit);
    CHECK(plainFaces > 0);
    CHECK(plainLowest == doctest::Approx(floorY));
    CHECK(revealFace);
    CHECK(threshold);
    // And the mesh is still a pure function of the tiles.
    const ChunkGeometry again = buildChunkGeometry(house.tiles, atlas, materials,
                                                   chunkOf(10, 15), 0);
    CHECK(geometry.positions == again.positions);
    CHECK(geometry.indices == again.indices);
}

TEST_CASE("the one-wide cobble leftovers along a frontage wear setts") {
    // THE STRIP. A cobbled street three cells wide along a wall: the 2 x 2
    // blocks take two of the rows and the third, against the frontage,
    // was the flat fill alone. Now every such cell wears the flag piece
    // fitted to the one cell, its stones a third of their size -- a sett
    // strip -- over the fill, in the material's floor tint, turned by
    // hash; and never on a roof.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    // Everything on the street band is brick already (HouseWorld); a
    // three-wide brick street x 20..22 over rows 4..27 is the default. Fence
    // it: walls at x = 19 and x = 23 so the cobble merge is bounded to it.
    for (std::int32_t y = 4; y <= 27; ++y) {
        house.put(19, y, 19, content::TileForm::Wall, materialId("granite"));
        house.put(23, y, 19, content::TileForm::Wall, materialId("granite"));
        for (std::int32_t x = 24; x <= 27; ++x) {
            house.put(x, y, 19, content::TileForm::Floor, materialId("dirt"));
        }
    }
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    std::size_t strips = 0;
    std::size_t cobbles = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.lightZ != 19 || p.lightX < 20 || p.lightX > 22) {
            continue;
        }
        if (p.role == PieceRole::FloorCobble) {
            ++cobbles;
        }
        if (p.role == PieceRole::FloorStrip) {
            ++strips;
            const PieceSpec* spec = catalogue.piece(PieceRole::FloorStrip);
            REQUIRE(spec != nullptr);
            // One cell: the 3 m piece at a third.
            CHECK(std::fabs(p.instance.scale.x) == doctest::Approx(1.0F / (spec->maxX - spec->minX)).epsilon(0.01F));
            CHECK(std::fabs(p.instance.scale.z) == doctest::Approx(1.0F / (spec->maxZ - spec->minZ)).epsilon(0.01F));
            const MaterialRule* brick = catalogue.materialByName("brick");
            REQUIRE(brick != nullptr);
            CHECK(p.instance.tint.r == brick->floorTint.r);
            CHECK(p.bilinear);
        }
    }
    CHECK(cobbles > 0);
    // Three wide, twenty-four long: the 2 x 2 merge leaves one row of
    // singles (or more where the merge could not pair), every one dressed.
    CHECK(strips >= 12);
    // No strip in the sky, and none where a block already stands: a cell
    // carries a cobble block or a strip, never both.
    std::set<std::pair<std::int32_t, std::int32_t>> stripCells;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::FloorStrip) {
            CHECK(p.lightZ == 19);
            // A turned piece's first corner point is any of the cell's
            // four; the cell is the least corner.
            stripCells.emplace(std::min(p.lightX, p.endBX), std::min(p.lightY, p.endBY));
        }
    }
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::FloorCobble && p.lightZ == 19) {
            const std::int32_t x0 = std::min(p.lightX, p.endBX);
            const std::int32_t x1 = std::max(p.lightX, p.endBX);
            const std::int32_t y0 = std::min(p.lightY, p.endBY);
            const std::int32_t y1 = std::max(p.lightY, p.endBY);
            for (std::int32_t y = y0; y < y1; ++y) {
                for (std::int32_t x = x0; x < x1; ++x) {
                    CHECK(stripCells.count({x, y}) == 0);
                }
            }
        }
    }
}

TEST_CASE("a flame's halo faces the eye from wherever the eye is") {
    // THE BILLBOARD, through the world scene: the same lantern described
    // from four eyes turns its one quad toward each -- yawed to an eye
    // along the street, PITCHED to an eye under the lamp or on the roof
    // over it -- about the same centre, so a body walking past, standing
    // under it or looking down on it never sees it edge-on. A yaw alone
    // left the quad plumb, a bar to an eye beneath it: the placement
    // critic's "edge at arm's length". And the description stays a pure
    // function of the eye: the same eye twice is the same turn and the
    // same hash.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    std::vector<render::Lamp> lamps(1);
    lamps[0].name = "lamp_street";
    lamps[0].x = 20;
    lamps[0].y = 19;
    lamps[0].z = 19;
    lamps[0].warmth = render::LampWarmth::Lantern;
    // A wall to hang it on, north of the lamp's tile.
    house.put(20, 18, 19, content::TileForm::Wall, materialId("granite"));
    const render::LampGlow glow = render::LampGlow::build(house.tiles, lamps);
    WorldScene scene(house.tiles, proceduralAtlas(), &glow, &catalogue, &lamps);
    WorldSceneParams params;
    params.timeOfDaySeconds = 21 * 3600;
    const auto describe = [&](float ex, float ey, float height, float look) {
        render::Camera eye;
        eye.x = ex;
        eye.y = ey;
        eye.z = render::bandSurface(19) + height;
        eye.yaw = std::atan2(20.5F - ex, -(19.5F - ey));
        eye.pitch = look;
        eye.hfovTan = 1.0F;
        SceneDescription out;
        scene.refresh(out, eye, 16.0F / 9.0F, params);
        return out;
    };
    const SceneDescription fromSouth = describe(20.5F, 23.5F, 1.7F, 0.0F);
    const SceneDescription fromEast = describe(24.5F, 19.5F, 1.7F, 0.0F);
    const SceneDescription fromSouthAgain = describe(20.5F, 23.5F, 1.7F, 0.0F);
    // Under the lamp, a hand's breadth off plumb, looking up: the lantern
    // hangs 0.42 out from the wall's face over the lamp's own tile, so
    // the eye stands a tenth east of it and a hair north.
    const SceneDescription fromBelow = describe(20.6F, 19.5F, 1.7F, 1.2F);
    // Two storeys up and a tile south, looking down: the roof's edge.
    const SceneDescription fromAbove = describe(20.5F, 20.5F, 7.7F, -1.2F);
    CHECK(sceneHash(fromSouth) == sceneHash(fromSouthAgain));
    // THE LANTERN'S flame -- the house's hearth across the street has one
    // too, placed first -- by the lamp cell it is lit from; its anchor is
    // the flame's own point, in the lantern's glass, over the eye of a
    // body on the street and under one on the roof.
    const PieceSpec* flame = catalogue.piece(PieceRole::Flame);
    REQUIRE(flame != nullptr);
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, lamps);
    const StaticPlacement* hung = nullptr;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Flame && p.lightX == 20 && p.lightY == 19) {
            hung = &p;
        }
    }
    REQUIRE(hung != nullptr);
    REQUIRE(hung->billboard);
    const Vec3 anchor = hung->anchor;
    CHECK(anchor.y > render::bandSurface(19) + 1.7F);
    CHECK(anchor.y < render::bandSurface(19) + 2.5F);
    // In a description: the halo whose origin lies within a metre of that
    // anchor (the hearth's is ten tiles off).
    const auto flameOf = [&](const SceneDescription& d) -> const StaticInstance* {
        for (const StaticInstance& at : d.statics) {
            const float dx = at.position.x - anchor.x;
            const float dy = at.position.y - anchor.y;
            const float dz = at.position.z - anchor.z;
            if (at.mode == kDrawHalo && dx * dx + dy * dy + dz * dz < 1.0F) {
                return &at;
            }
        }
        return nullptr;
    };
    const StaticInstance* south = flameOf(fromSouth);
    const StaticInstance* east = flameOf(fromEast);
    const StaticInstance* below = flameOf(fromBelow);
    const StaticInstance* above = flameOf(fromAbove);
    REQUIRE(south != nullptr);
    REQUIRE(east != nullptr);
    REQUIRE(below != nullptr);
    REQUIRE(above != nullptr);
    // The centre: the origin is half a width along the quad's own +X and
    // half a height along its own +Y from it -- the +Y tipped by the pitch
    // to (0, cos, sin) and both turned by the yaw, the adapter's own order.
    // From every eye it sits ON THAT EYE'S OWN RAY through the flame,
    // floated toward the eye by no more than half the quad's height and
    // never past the eye: the same pixel as the flame from wherever it is
    // looked at, its plane clear of the lantern's body.
    const auto centreOf = [flame](const StaticInstance& at) {
        const float w = flame->width * at.scale.x;
        const float h = flame->height * at.scale.y;
        const float c = std::cos(at.yaw);
        const float s = std::sin(at.yaw);
        const float cp = std::cos(at.pitch);
        const float sp = std::sin(at.pitch);
        return Vec3{at.position.x + 0.5F * w * c - 0.5F * h * sp * s, at.position.y + 0.5F * h * cp,
                    at.position.z + 0.5F * w * s + 0.5F * h * sp * c};
    };
    const auto onTheRay = [&](const StaticInstance& at, float ex, float ey, float ez) {
        const Vec3 centre = centreOf(at);
        const float h = flame->height * at.scale.y;
        // From the anchor: toward the eye, and how far.
        const float rx = ex - anchor.x, ry = ey - anchor.y, rz = ez - anchor.z;
        const float reach = std::sqrt(rx * rx + ry * ry + rz * rz);
        const float fx = centre.x - anchor.x, fy = centre.y - anchor.y, fz = centre.z - anchor.z;
        const float floated = std::sqrt(fx * fx + fy * fy + fz * fz);
        const float along = (fx * rx + fy * ry + fz * rz) / reach;
        CHECK(floated > 0.05F);
        CHECK(floated < 0.5F * h + 0.01F);
        CHECK(floated < 0.5F * reach);
        // On the ray: the whole float lies along it.
        CHECK(along > 0.999F * floated);
        return centre;
    };
    const float street = render::bandSurface(19) + 1.7F;
    const Vec3 a = onTheRay(*south, 20.5F, street, 23.5F);
    const Vec3 b = onTheRay(*east, 24.5F, street, 19.5F);
    const Vec3 u = onTheRay(*below, 20.6F, street, 19.5F);
    const Vec3 o = onTheRay(*above, 20.5F, render::bandSurface(19) + 7.7F, 20.5F);
    // And from each eye the quad's normal -- local +Z, which the pitch
    // takes to (0, -sin, cos) and the yaw to (-sin cos, -sin, cos cos) --
    // lies along the line from the centre to that eye IN THREE
    // DIMENSIONS: never edge-on, wherever the eye is, under it included.
    const auto faces = [](const StaticInstance& at, const Vec3& centre, float ex, float ey, float ez) {
        const float dx = ex - centre.x;
        const float dy = ey - centre.y;
        const float dz = ez - centre.z;
        const float nx = -std::sin(at.yaw) * std::cos(at.pitch);
        const float ny = -std::sin(at.pitch);
        const float nz = std::cos(at.yaw) * std::cos(at.pitch);
        const float along = nx * dx + ny * dy + nz * dz;
        return std::fabs(along) > 0.99F * std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    CHECK(faces(*south, a, 20.5F, street, 23.5F));
    CHECK(faces(*east, b, 24.5F, street, 19.5F));
    CHECK(faces(*below, u, 20.6F, street, 19.5F));
    CHECK(faces(*above, o, 20.5F, render::bandSurface(19) + 7.7F, 20.5F));
    CHECK_FALSE(faces(*south, a, 24.5F, street, 19.5F));
    CHECK_FALSE(faces(*south, a, 20.6F, street, 19.5F));
    CHECK(south->yaw != east->yaw);
    // Along the street the quad stands nearly plumb (a few degrees up to
    // a lamp over head height); under the lamp it tips hard toward the
    // ground, and on the roof it tips the other way, toward the sky.
    CHECK(std::fabs(south->pitch) < 0.2F);
    CHECK(below->pitch > 1.0F);
    CHECK(above->pitch < -0.8F);
}

TEST_CASE("a pair of lone posts two cells apart on a street carries a hitching rail") {
    // THE PAIR'S JOB. HouseWorld's street post at (20, 20) gets a twin at
    // (22, 20): one rail from the first's east face to the second's west
    // face at hip height, in the beam piece at its catalogue section, lit
    // by the cell between. A post with no twin (the pilaster, the indoor
    // table) carries none, and a pair under a roof carries none.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    house.put(22, 20, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    // And a pair down a column at x = 25: rows 22 and 24.
    house.put(25, 22, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    house.put(25, 24, 19, content::TileForm::Wall, materialId("trudgeon_wood"));
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    const PieceSpec* rail = catalogue.piece(PieceRole::PostRail);
    REQUIRE(rail != nullptr);
    std::size_t rails = 0;
    bool acrossRow = false;
    bool downColumn = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::PostRail) {
            continue;
        }
        ++rails;
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + 1.05F + rail->lift));
        CHECK(p.instance.scale.x == doctest::Approx(rail->scale));
        // One metre long: from face to face across the cell between.
        CHECK(p.instance.scale.z == doctest::Approx(1.0F / 5.0F));
        if (p.lightX == 21 && p.lightY == 20) {
            acrossRow = true;
            CHECK(p.instance.position.x == doctest::Approx(21.0F));
            CHECK(p.instance.position.z == doctest::Approx(20.5F));
        }
        if (p.lightX == 25 && p.lightY == 23) {
            downColumn = true;
            CHECK(p.instance.position.x == doctest::Approx(25.5F));
            CHECK(p.instance.position.z == doctest::Approx(23.0F));
        }
    }
    CHECK(rails == 2);
    CHECK(acrossRow);
    CHECK(downColumn);
    // Both halves of each pair wear the strapped post fitted to their cell
    // (the frame's timber, read from either end); the indoor table and the
    // partition on the roof band are the pillars left. Plus the house's own
    // door, whose two flank cells wear the slim jamb dress of the same
    // piece: four street posts and two jambs.
    CHECK(countRole(placed.placements, PieceRole::DoorPost) == 6);
    CHECK(countRole(placed.placements, PieceRole::Pillar) == 2);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::DoorPost) {
            const bool rowPair = p.lightY == 20 && (p.lightX == 20 || p.lightX == 22);
            const bool columnPair = p.lightX == 25 && (p.lightY == 22 || p.lightY == 24);
            const bool jamb = p.lightY == 15 && (p.lightX == 10 || p.lightX == 13);
            CHECK((rowPair || columnPair || jamb));
        }
    }
    // And a board in each frame, hung from the first post into the gap.
    std::size_t boards = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::ShopSign) {
            continue;
        }
        ++boards;
        CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19) + 2.35F));
        const bool rowFrame = p.lightX == 20 && p.lightY == 20;
        const bool columnFrame = p.lightX == 25 && p.lightY == 22;
        CHECK((rowFrame || columnFrame));
        if (rowFrame) {
            CHECK(p.instance.position.x > 21.0F);
            CHECK(p.instance.yaw == doctest::Approx(0.0F));
        }
        if (columnFrame) {
            CHECK(p.instance.position.z > 23.0F);
            CHECK(p.instance.yaw == doctest::Approx(3.14159265F * 0.5F));
        }
    }
    CHECK(boards == 2);
}

// ---------------------------------------------------------------------------
// THE LIGHT LAW -- the ward after dark
// ---------------------------------------------------------------------------

namespace {

/// A lot whose draws come out as asked: the candle and the owl are read as
/// `lot % 100` and `(lot >> 8) % 100` over the whole word, so the low
/// sixteen bits are searched for the pair once the bedtime and rising bytes
/// (16..23, 24..31) are set.
std::uint32_t lotOf(int candle, int owl, std::uint32_t bed, std::uint32_t rise) {
    const std::uint32_t top = (rise << 24) | (bed << 16);
    for (std::uint32_t low = 0; low < 65536U; ++low) {
        const std::uint32_t lot = top | low;
        if (static_cast<int>(lot % 100U) == candle && static_cast<int>((lot >> 8) % 100U) == owl) {
            return lot;
        }
    }
    return top;
}

bool windowPane(PieceRole role) {
    return role == PieceRole::WallWindow || role == PieceRole::PaneTimber;
}

bool warmPane(const StaticInstance& piece, const Rgba8& warm) {
    return windowPane(static_cast<PieceRole>(piece.role)) && piece.pane.r == warm.r &&
           piece.pane.g == warm.g && piece.pane.b == warm.b;
}

}  // namespace

TEST_CASE("the light law answers the hour, and a house's windows draw one lot") {
    // The rule alone, on lots built by hand, with the shipped hours.
    RuleKnobs knobs;
    knobs.houseCandlePercent = 85;
    knobs.houseOwlPercent = 10;
    knobs.houseBedtimeFrom = 21.5F;
    knobs.houseBedtimeTo = 26.5F;
    knobs.houseRisingFrom = 4.0F;
    knobs.houseRisingTo = 6.5F;
    knobs.storeLampPercent = 15;
    // A plain household: a candle, no owl, bedtime at the window's middle
    // (midnight), rising at the middle of its own (a quarter past five).
    const std::uint32_t plain = lotOf(0, 50, 128U, 128U);
    REQUIRE(plain % 100U == 0U);
    REQUIRE((plain >> 8) % 100U == 50U);
    CHECK(paneGlows(HouseKind::Household, plain, 20.0F, knobs));
    CHECK(paneGlows(HouseKind::Household, plain, 23.5F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Household, plain, 0.5F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Household, plain, 3.0F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Household, plain, 5.0F, knobs));
    CHECK(paneGlows(HouseKind::Household, plain, 5.5F, knobs));
    // The earliest bedtime and the latest: half past nine, half past two.
    CHECK(paneGlows(HouseKind::Household, lotOf(0, 50, 0U, 128U), 21.4F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Household, lotOf(0, 50, 0U, 128U), 21.6F, knobs));
    CHECK(paneGlows(HouseKind::Household, lotOf(0, 50, 255U, 128U), 2.0F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Household, lotOf(0, 50, 255U, 128U), 3.0F, knobs));
    // No candle: dark whatever the hour. An owl: up whatever the hour.
    CHECK_FALSE(paneGlows(HouseKind::Household, lotOf(90, 50, 128U, 128U), 20.0F, knobs));
    CHECK(paneGlows(HouseKind::Household, lotOf(0, 5, 128U, 128U), 3.0F, knobs));
    // The named houses, whatever their lot: lit is lit, dark is a
    // watchman's lamp in one window in so many, none is none.
    CHECK(paneGlows(HouseKind::Lit, lotOf(90, 50, 0U, 0U), 3.0F, knobs));
    CHECK(paneGlows(HouseKind::Dark, lotOf(10, 50, 0U, 0U), 3.0F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::Dark, lotOf(20, 50, 0U, 0U), 22.0F, knobs));
    CHECK_FALSE(paneGlows(HouseKind::None, lotOf(0, 5, 0U, 0U), 22.0F, knobs));
    // The defaults are the old rule: two panes in three, up all night.
    const RuleKnobs old{};
    CHECK(paneGlows(HouseKind::Household, lotOf(0, 50, 128U, 128U), 3.0F, old));
    CHECK(paneGlows(HouseKind::Household, lotOf(66, 50, 128U, 128U), 3.0F, old));
    CHECK_FALSE(paneGlows(HouseKind::Household, lotOf(67, 50, 128U, 128U), 22.0F, old));

    // On the house world every window on the ring looks into the one room,
    // so every pane carries the one lot -- the household's, since no sign
    // of the Docks stands here.
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    std::size_t panes = 0;
    std::uint32_t lot = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (!windowPane(p.role)) {
            continue;
        }
        REQUIRE(p.hasInside);
        CHECK(p.homely);
        CHECK(p.house == HouseKind::Household);
        CHECK(p.houseLot != 0U);
        if (panes == 0) {
            lot = p.houseLot;
        }
        CHECK(p.houseLot == lot);
        ++panes;
    }
    CHECK(panes >= 2);
}

TEST_CASE("the light law on the Docks: up at ten, abed at three, and the Gull never sleeps") {
    const sim::TileQuery tilesA(docksWorld());
    const sim::TileQuery tilesB(docksWorld());
    const StaticCatalogue& catalogue = shippedCatalogue();
    const RuleKnobs& knobs = catalogue.knobs();
    REQUIRE(knobs.houseBedtimeTo > knobs.houseBedtimeFrom);
    REQUIRE_FALSE(knobs.litAllNight.empty());
    REQUIRE_FALSE(knobs.keptDark.empty());
    const std::vector<render::Lamp> lamps =
        render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    const StaticPlacements first = placeStaticPieces(tilesA, catalogue, lamps);
    const StaticPlacements second = placeStaticPieces(tilesB, catalogue, lamps);
    REQUIRE(first.placements.size() == second.placements.size());

    // Every pane knows its house the same way twice, and a timber frame
    // carries a pane of its own.
    std::size_t households = 0;
    std::size_t litHouses = 0;
    std::size_t darkHouses = 0;
    std::size_t yards = 0;
    for (std::size_t i = 0; i < first.placements.size(); ++i) {
        const StaticPlacement& p = first.placements[i];
        const StaticPlacement& q = second.placements[i];
        REQUIRE(p.role == q.role);
        if (!windowPane(p.role)) {
            continue;
        }
        REQUIRE(p.hasInside);
        REQUIRE(p.house == q.house);
        REQUIRE(p.houseLot == q.houseLot);
        REQUIRE(p.insideX == q.insideX);
        REQUIRE(p.insideY == q.insideY);
        REQUIRE(p.insideZ == q.insideZ);
        CHECK(p.homely == (p.house != HouseKind::None));
        switch (p.house) {
            case HouseKind::None: ++yards; break;
            case HouseKind::Household: ++households; break;
            case HouseKind::Lit: ++litHouses; break;
            case HouseKind::Dark: ++darkHouses; break;
        }
    }
    const std::size_t timberPanes = countRole(first.placements, PieceRole::PaneTimber);
    CHECK(timberPanes > 10);
    CHECK(timberPanes == countRole(first.placements, PieceRole::WindowTimber));
    CHECK(households >= 40);
    CHECK(litHouses >= 4);
    CHECK(darkHouses >= 4);
    MESSAGE("Docks panes: " << households << " household, " << litHouses << " lit all night, "
                            << darkHouses << " kept dark, " << yards << " on no room");

    // The law's own count at an hour (a lamp's glow is on top of this).
    const auto upAt = [&](float hour, HouseKind kind) {
        std::size_t n = 0;
        for (const StaticPlacement& p : first.placements) {
            if (windowPane(p.role) && p.house == kind && paneGlows(p.house, p.houseLot, hour, knobs)) {
                ++n;
            }
        }
        return n;
    };
    const std::size_t up20 = upAt(20.0F, HouseKind::Household);
    const std::size_t up22 = upAt(22.0F, HouseKind::Household);
    const std::size_t up3 = upAt(3.0F, HouseKind::Household);
    MESSAGE("Docks households up: " << up20 << " at eight, " << up22 << " at ten, " << up3 << " at three");
    // At eight nobody has gone to bed; at ten most are up; at three only
    // the owls.
    CHECK(up20 >= up22);
    CHECK(up22 >= 20);
    CHECK(up22 * 3 > households);
    CHECK(up3 * 2 < up22);
    CHECK(up3 * 3 < households);
    // A watchman's lamp in a few of the stores' windows, never most.
    CHECK(upAt(3.0F, HouseKind::Dark) * 2 < darkHouses);
    CHECK(upAt(3.0F, HouseKind::Lit) == litHouses);
    CHECK(upAt(22.0F, HouseKind::None) == 0);

    // The Gilded Gull (its sign's footprint, tavern.hpp: x 146..160, y
    // 66..79, its oak storey over its granite one) keeps every pane lit at
    // both hours; the King's Bond across the Tarwalk is kept dark.
    std::size_t gull = 0;
    std::size_t bond = 0;
    for (const StaticPlacement& p : first.placements) {
        if (!windowPane(p.role) || p.insideZ < 19 || p.insideZ > 21) {
            continue;
        }
        if (p.insideX >= 146 && p.insideX <= 160 && p.insideY >= 66 && p.insideY <= 79) {
            CHECK(p.house == HouseKind::Lit);
            CHECK(paneGlows(p.house, p.houseLot, 22.0F, knobs));
            CHECK(paneGlows(p.house, p.houseLot, 3.0F, knobs));
            ++gull;
        }
        if (p.insideX >= 114 && p.insideX <= 130 && p.insideY >= 66 && p.insideY <= 78) {
            CHECK(p.house == HouseKind::Dark);
            ++bond;
        }
    }
    CHECK(gull >= 2);
    CHECK(bond >= 1);

    // Through the world scene, from the Tarwalk before the Gull's frontage
    // (frame 16's vantage), no baked lamps: the panes the eye sees are warm
    // by the law alone, the same twice, fewer at three than at ten, none at
    // noon.
    const render::TileAtlas& atlas = proceduralAtlas();
    render::Camera eye;
    eye.x = 150.5F;
    eye.y = 63.5F;
    eye.z = render::bandSurface(19) + static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 150.0F * 3.14159265358979323846F / 180.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    WorldScene worldA(tilesA, atlas, nullptr, &catalogue, &lamps);
    WorldScene worldB(tilesB, atlas, nullptr, &catalogue, &lamps);
    WorldSceneParams params;
    params.timeOfDaySeconds = 22 * 3600;
    SceneDescription sceneA;
    SceneDescription sceneB;
    worldA.refresh(sceneA, eye, 16.0F / 9.0F, params);
    worldB.refresh(sceneB, eye, 16.0F / 9.0F, params);
    CHECK(sceneHash(sceneA) == sceneHash(sceneB));
    const Rgba8 warm = knobs.litPane;
    const auto warmSeen = [&warm](const SceneDescription& scene) {
        std::size_t n = 0;
        for (const StaticInstance& piece : scene.statics) {
            n += warmPane(piece, warm) ? 1 : 0;
        }
        return n;
    };
    const std::size_t warm22 = warmSeen(sceneA);
    CHECK(warm22 >= 2);
    params.timeOfDaySeconds = 3 * 3600;
    worldA.refresh(sceneA, eye, 16.0F / 9.0F, params);
    worldB.refresh(sceneB, eye, 16.0F / 9.0F, params);
    CHECK(sceneHash(sceneA) == sceneHash(sceneB));
    const std::size_t warm3 = warmSeen(sceneA);
    MESSAGE("Docks panes warm from the Gull's frontage: " << warm22 << " at ten, " << warm3 << " at three");
    CHECK(warm3 >= 1);
    CHECK(warm3 <= warm22);
    params.timeOfDaySeconds = 12 * 3600;
    worldA.refresh(sceneA, eye, 16.0F / 9.0F, params);
    CHECK(warmSeen(sceneA) == 0);
}

