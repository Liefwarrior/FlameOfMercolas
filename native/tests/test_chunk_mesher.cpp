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
#include "granadad/render/lighting.hpp"
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
    CHECK(catalogue.knobs().hullFlareDegrees > 0.0F);
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
        CHECK(p.lightX == 11);
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
    // over the frame's ends in the reveal planes.
    CHECK(countRole(placed.placements, PieceRole::DoorInside) == 5);
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
    // Every lamp but the post (its glass is its own) carries a crossed pair
    // of flame quads, drawn as their own light.
    CHECK(countRole(lit.placements, PieceRole::Flame) == 4);
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
        }
    }
    CHECK(lit.placements.size() == placed.placements.size() + 4 + 4 + 1);
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
                             p.role == PieceRole::Hull;
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
    // The partition's underside: a ceiling quad at the wall's own foot
    // (the roof band's surface), over the taproom cell (13, 10), lit as the
    // room's underside and in the room's ceiling tint (the thatch floor
    // beside it), not the oak wall's own.
    bool underside = false;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role != PieceRole::Ceiling) {
            continue;
        }
        CHECK(p.lightZ == 19);
        if (p.instance.position.y == doctest::Approx(render::bandSurface(20) - 0.004F)) {
            underside = true;
            CHECK(p.instance.position.x == doctest::Approx(13.0F));
            CHECK(p.instance.position.z == doctest::Approx(10.0F));
            CHECK(p.facing == doctest::Approx(render::kUndersideLift));
            const Rgba8 thatch = catalogue.materialByName("thatch")->ceilingTint;
            CHECK(p.instance.tint.r == thatch.r);
            CHECK(p.instance.tint.g == thatch.g);
            CHECK(p.instance.tint.b == thatch.b);
        } else {
            // The slab ceilings hang under the roof slab as before.
            CHECK(p.instance.position.y ==
                  doctest::Approx(render::bandSurface(20) - render::kFloorSlab - 0.004F));
        }
    }
    CHECK(underside);
}

TEST_CASE("a lit room's window is warm at night") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    std::vector<render::Lamp> lamps(1);
    lamps[0].name = "lamp_house_bar";
    lamps[0].x = 12;
    lamps[0].y = 11;
    lamps[0].z = 19;
    lamps[0].luminance = 18;
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

TEST_CASE("a lone timber cell is a pillar indoors and a bundle of piles out of doors") {
    HouseWorld house;
    const StaticCatalogue& catalogue = shippedCatalogue();
    const StaticPlacements placed = placeStaticPieces(house.tiles, catalogue, {});
    // Indoors: one pillar on the cell, fitted to it, and no board on any of
    // its four faces.
    CHECK(countRole(placed.placements, PieceRole::Pillar) == 1);
    const StaticPlacement* pillar = findRole(placed.placements, PieceRole::Pillar);
    REQUIRE(pillar != nullptr);
    CHECK(pillar->instance.position.x == doctest::Approx(13.5F));
    CHECK(pillar->instance.position.z == doctest::Approx(12.5F));
    CHECK(pillar->instance.scale.x > 2.0F);
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::WallTimber) {
            const bool onIndoorPost = p.instance.position.x > 12.9F && p.instance.position.x < 14.1F &&
                                      p.instance.position.z > 11.9F && p.instance.position.z < 13.1F;
            CHECK_FALSE(onIndoorPost);
        }
    }
    // Out of doors: four piles round the cell's corners, and the boards
    // between them tarred (darker than the material's own tint). (The
    // partition on the roof band is a lone timber cell too, and gets its
    // own four.)
    CHECK(countRole(placed.placements, PieceRole::Post) == 8);
    std::size_t tarred = 0;
    for (const StaticPlacement& p : placed.placements) {
        if (p.role == PieceRole::Post && p.lightZ == 19) {
            CHECK(std::fabs(p.instance.position.x - 20.5F) > 0.5F);
            CHECK(std::fabs(p.instance.position.z - 20.5F) > 0.5F);
            CHECK(std::fabs(p.instance.position.x - 20.5F) < 0.7F);
            CHECK(p.instance.position.y == doctest::Approx(render::bandSurface(19)));
        }
        if (p.role == PieceRole::WallTimber && p.lightX == 20 && p.lightY == 20) {
            const Rgba8 own = catalogue.materialByName("trudgeon_wood")->tint;
            CHECK(p.instance.tint.r < own.r);
            CHECK(p.instance.tint.g < own.g);
            ++tarred;
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
    CHECK(countRole(placed.placements, PieceRole::Flame) >= 2 + 2);
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
