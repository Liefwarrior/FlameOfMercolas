// The acceptance suite: load the owner's real baked worlds and assert facts
// about them that could only hold if the whole read path is correct.
//
// Every expected number in here was read out of the actual .trojsav bytes.

#include "granadad/content/world_reader.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "fixtures.hpp"
#include "granadad/content/chunk_codec.hpp"

using namespace granadad::content;
namespace fx = granadad::content::testing;

namespace {

/// Counts distinct values in a lane. std::map, not unordered_map — this is test
/// code, but the ordering discipline is the same everywhere in this project.
template <typename T>
std::map<T, std::size_t> histogram(std::span<const T> lane) {
    std::map<T, std::size_t> counts;
    for (const T value : lane) {
        ++counts[value];
    }
    return counts;
}

}  // namespace

TEST_CASE("META decodes to the right dimensions and the canonical lane set") {
    struct Case {
        const fx::BakedWorldFacts& facts;
    };
    for (const fx::BakedWorldFacts& facts :
         {fx::kTavernFixture, fx::kDocksSurface, fx::kCompoundBlock}) {
        CAPTURE(facts.name);
        TrojSav save = TrojSav::readFile(fx::bakedMap(facts.name));
        const WorldMeta meta = readMetaSection(save.section(sections::kMeta));

        CHECK(meta.coords.chunksX() == facts.chunksX);
        CHECK(meta.coords.chunksY() == facts.chunksY);
        CHECK(meta.coords.chunksZ() == facts.chunksZ);
        CHECK(meta.coords.chunkCount() == facts.chunkCount);
        CHECK(meta.siteCount == 0);

        // Exactly the seven core lanes, in registry order, with their widths.
        REQUIRE(meta.lanes.count() == kCoreLaneCount);
        const std::array<std::pair<std::string_view, int>, kCoreLaneCount> expected{{
            {lane_names::kMaterial, 2},
            {lane_names::kForm, 1},
            {lane_names::kFlags, 1},
            {lane_names::kTemperature, 2},
            {lane_names::kFluid, 2},
            {lane_names::kLight, 2},
            {lane_names::kOpacity, 1},
        }};
        for (std::size_t i = 0; i < kCoreLaneCount; ++i) {
            CAPTURE(i);
            CHECK(meta.lanes.byIndex(i).index == i);
            CHECK(meta.lanes.byIndex(i).name == expected[i].first);
            CHECK(meta.lanes.byIndex(i).bytesPerTile == expected[i].second);
        }
    }
}

TEST_CASE("tavern_fixture loads completely") {
    const fx::BakedWorldFacts& facts = fx::kTavernFixture;
    const World world = loadWorldFile(fx::bakedMap(facts.name));

    // --- shape -------------------------------------------------------------
    CHECK(world.coords().chunksX() == 4);
    CHECK(world.coords().chunksY() == 3);
    CHECK(world.coords().chunksZ() == 3);
    CHECK(world.chunkCount() == 36);
    // Every chunk decoded into a dense store of exactly 8192 cells.
    CHECK(world.tileCount() == 36u * 8192u);
    CHECK(world.tileCount() == 294912u);
    CHECK(world.lanes().count() == kCoreLaneCount);

    // 4x3x3 with a 1-chunk VOID border leaves a 2x1x1 interior.
    std::size_t interior = 0;
    for (std::int32_t i = 0; i < world.coords().chunkCount(); ++i) {
        if (!world.coords().isVoidBorder(i)) {
            ++interior;
        }
    }
    CHECK(interior == 2);

    // --- FORM lane ---------------------------------------------------------
    const auto forms = histogram(world.byteLane(kFormLane));
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Void)) == 278528);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Open)) == 12894);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Floor)) == 1845);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Wall)) == 1641);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Stair)) == 4);
    CHECK(forms.count(static_cast<std::uint8_t>(TileForm::Ramp)) == 0);
    // Not all one value, and nothing outside the declared enum.
    CHECK(forms.size() == 5);
    for (const auto& [value, count] : forms) {
        CHECK(value < kTileFormCount);
    }
    // The VOID count is exactly the border: 36 - 2 interior chunks.
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Void)) == 34u * 8192u);

    // --- MATERIAL lane -----------------------------------------------------
    // Only meaningful where FORM is neither VOID nor OPEN.
    std::map<std::uint16_t, std::size_t> materials;
    const std::span<const std::uint8_t> formLane = world.byteLane(kFormLane);
    const std::span<const std::uint16_t> materialLane = world.shortLane(kMaterialLane);
    for (std::size_t t = 0; t < world.tileCount(); ++t) {
        const TileForm form = static_cast<TileForm>(formLane[t]);
        if (form != TileForm::Void && form != TileForm::Open) {
            ++materials[materialLane[t]];
        }
    }
    CHECK(materials.at(fx::kMaterialBrick) == 288);
    CHECK(materials.at(fx::kMaterialDirt) == 830);
    CHECK(materials.at(fx::kMaterialGranite) == 1650);
    CHECK(materials.at(fx::kMaterialOak) == 491);
    CHECK(materials.at(fx::kMaterialThatch) == 202);
    CHECK(materials.size() == 7);
    // A real district, not a blank slab: several materials, all inside the
    // registry, and granite dominant as a docks build should be.
    CHECK(materials.size() > 1);
    for (const auto& [id, count] : materials) {
        CHECK(id < fx::kMaterialCount);
    }

    // --- FLAGS lane --------------------------------------------------------
    const auto flags = histogram(world.byteLane(kFlagsLane));
    CHECK(flags.at(0) == 14743);
    CHECK(flags.at(flag_bits::kBlocksMove | flag_bits::kBlocksLight) == 280169);
    for (const auto& [value, count] : flags) {
        CHECK((value & ~flag_bits::kDefinedMask) == 0);
    }

    // --- FLUID lane --------------------------------------------------------
    std::size_t wet = 0;
    for (const std::uint16_t packed : world.shortLane(kFluidLane)) {
        if (packed != 0) {
            ++wet;
            CHECK(fluid_bits::fluidId(packed) == 0);  // water is the only fluid
            CHECK(fluid_bits::depth(packed) == 2);
        }
    }
    CHECK(wet == 10);

    // --- lanes the systems that own them do not exist for yet ---------------
    // TEMPERATURE, LIGHT and OPACITY are all-zero in every shipped world. That
    // is a fact about the current baker, not about the format.
    for (const std::uint16_t value : world.shortLane(kTemperatureLane)) {
        REQUIRE(value == 0);
    }
    for (const std::uint16_t value : world.shortLane(kLightLane)) {
        REQUIRE(value == 0);
    }
    for (const std::uint8_t value : world.byteLane(kOpacityLane)) {
        REQUIRE(value == 0);
    }

    // No shipped world carries overlay cells.
    CHECK(world.overlay(OverlayId::Charge).empty());
}

TEST_CASE("docks_surface loads completely") {
    const fx::BakedWorldFacts& facts = fx::kDocksSurface;
    const World world = loadWorldFile(fx::bakedMap(facts.name));

    // --- shape -------------------------------------------------------------
    CHECK(world.coords().chunksX() == 8);
    CHECK(world.coords().chunksY() == 6);
    CHECK(world.coords().chunksZ() == 4);
    CHECK(world.chunkCount() == 192);
    CHECK(world.tileCount() == 192u * 8192u);
    CHECK(world.tileCount() == 1572864u);
    // 256 x 192 x 32 tiles, border included.
    CHECK(world.coords().chunksX() * kChunkSizeX == 256);
    CHECK(world.coords().chunksY() * kChunkSizeY == 192);
    CHECK(world.coords().chunksZ() * kChunkSizeZ == 32);

    // 8x6x4 with a border leaves a 6x4x2 interior.
    std::size_t interior = 0;
    for (std::int32_t i = 0; i < world.coords().chunkCount(); ++i) {
        if (!world.coords().isVoidBorder(i)) {
            ++interior;
        }
    }
    CHECK(interior == 48);

    // --- FORM lane ---------------------------------------------------------
    const auto forms = histogram(world.byteLane(kFormLane));
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Void)) == 1179648);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Open)) == 91887);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Floor)) == 29410);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Wall)) == 271785);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Ramp)) == 82);
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Stair)) == 52);
    // All six forms present — this world exercises the whole enum.
    CHECK(forms.size() == 6);
    std::size_t total = 0;
    for (const auto& [value, count] : forms) {
        CHECK(value < kTileFormCount);
        total += count;
    }
    CHECK(total == world.tileCount());
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Void)) == 144u * 8192u);

    // --- MATERIAL lane -----------------------------------------------------
    std::map<std::uint16_t, std::size_t> materials;
    const std::span<const std::uint8_t> formLane = world.byteLane(kFormLane);
    const std::span<const std::uint16_t> materialLane = world.shortLane(kMaterialLane);
    for (std::size_t t = 0; t < world.tileCount(); ++t) {
        const TileForm form = static_cast<TileForm>(formLane[t]);
        if (form != TileForm::Void && form != TileForm::Open) {
            ++materials[materialLane[t]];
        }
    }
    // 15 distinct materials, reading correctly as a docks district: granite
    // dominant, then dirt, then reman concrete and brick.
    CHECK(materials.size() == 15);
    CHECK(materials.at(fx::kMaterialGranite) == 178421);
    CHECK(materials.at(fx::kMaterialDirt) == 102876);
    CHECK(materials.at(fx::kMaterialRemanConcrete) == 6234);
    CHECK(materials.at(fx::kMaterialBrick) == 5019);
    CHECK(materials.at(fx::kMaterialOak) == 2722);
    CHECK(materials.at(fx::kMaterialThatch) == 2679);
    CHECK(materials.at(fx::kMaterialTrudgeonWood) == 2449);
    CHECK(materials.at(fx::kMaterialAsh) == 86);
    for (const auto& [id, count] : materials) {
        CHECK(id < fx::kMaterialCount);
    }
    // The distribution is genuinely spread, not one value with noise.
    CHECK(materials.at(fx::kMaterialGranite) < 301567u);

    // --- FLAGS lane --------------------------------------------------------
    const auto flags = histogram(world.byteLane(kFlagsLane));
    CHECK(flags.size() == 2);
    CHECK(flags.at(0) == 121431);
    CHECK(flags.at(flag_bits::kBlocksMove | flag_bits::kBlocksLight) == 1451433);

    // --- FLUID lane: the harbour ------------------------------------------
    std::map<int, std::size_t> depths;
    for (const std::uint16_t packed : world.shortLane(kFluidLane)) {
        if (packed != 0) {
            CHECK(fluid_bits::fluidId(packed) == 0);
            CHECK_FALSE(fluid_bits::settled(packed));
            ++depths[fluid_bits::depth(packed)];
        }
    }
    CHECK(depths.at(2) == 39);
    CHECK(depths.at(4) == 8);
    CHECK(depths.at(7) == 7382);  // the harbour proper
    CHECK(depths.size() == 3);

    CHECK(world.overlay(OverlayId::Charge).empty());
}

TEST_CASE("the same world loads identically twice") {
    // Determinism at the coarsest granularity there is: the read path has no
    // hidden state, so two loads of the same bytes agree cell for cell.
    const World a = loadWorldFile(fx::bakedMap(fx::kDocksSurface.name));
    const World b = loadWorldFile(fx::bakedMap(fx::kDocksSurface.name));

    REQUIRE(a.tileCount() == b.tileCount());
    for (std::size_t lane = 0; lane < a.lanes().count(); ++lane) {
        CAPTURE(lane);
        if (a.lanes().byIndex(lane).bytesPerTile == 2) {
            const std::span<const std::uint16_t> left = a.shortLane(lane);
            const std::span<const std::uint16_t> right = b.shortLane(lane);
            REQUIRE(std::equal(left.begin(), left.end(), right.begin()));
        } else {
            const std::span<const std::uint8_t> left = a.byteLane(lane);
            const std::span<const std::uint8_t> right = b.byteLane(lane);
            REQUIRE(std::equal(left.begin(), left.end(), right.begin()));
        }
    }
}

TEST_CASE("compound_block loads completely") {
    const World world = loadWorldFile(fx::bakedMap(fx::kCompoundBlock.name));
    CHECK(world.chunkCount() == 108);
    CHECK(world.tileCount() == 108u * 8192u);

    const auto forms = histogram(world.byteLane(kFormLane));
    // 6x6x3 with a border leaves a 4x4x1 interior: 92 border chunks of VOID.
    CHECK(forms.at(static_cast<std::uint8_t>(TileForm::Void)) == 92u * 8192u);
    CHECK(forms.size() > 1);
}

// ---------------------------------------------------------------------------
// Strictness on the META/WRLD layer.
// ---------------------------------------------------------------------------

TEST_CASE("a bad META version is rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    std::vector<std::uint8_t> meta(save.section(sections::kMeta).begin(),
                                   save.section(sections::kMeta).end());
    meta[0] = 2;
    CHECK_THROWS_AS(readMetaSection(meta), FormatError);
}

TEST_CASE("a lane-set mismatch is rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    std::vector<std::uint8_t> meta(save.section(sections::kMeta).begin(),
                                   save.section(sections::kMeta).end());

    SUBCASE("wrong core lane width") {
        // The material lane's bytesPerTile: 1 version + 12 dims + 1 laneCount
        // + 1 nameLen + 8 name = offset 23.
        REQUIRE(meta[23] == 2);
        meta[23] = 1;
        CHECK_THROWS_AS(readMetaSection(meta), FormatError);
    }
    SUBCASE("wrong core lane name") {
        meta[15] = 'X';  // first char of "material"
        CHECK_THROWS_AS(readMetaSection(meta), FormatError);
    }
    SUBCASE("too few lanes") {
        meta[13] = 6;
        CHECK_THROWS_AS(readMetaSection(meta), FormatError);
    }
}

TEST_CASE("a non-zero site count is rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    std::vector<std::uint8_t> meta(save.section(sections::kMeta).begin(),
                                   save.section(sections::kMeta).end());
    meta[meta.size() - 4] = 1;  // siteCount
    CHECK_THROWS_AS(readMetaSection(meta), FormatError);
}

TEST_CASE("META dimensions outside the WorldConfig limits are rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    std::vector<std::uint8_t> meta(save.section(sections::kMeta).begin(),
                                   save.section(sections::kMeta).end());
    meta[1] = 2;  // chunksX = 2, below the 3-chunk minimum
    meta[2] = 0;
    meta[3] = 0;
    meta[4] = 0;
    CHECK_THROWS_AS(readMetaSection(meta), FormatError);
}

TEST_CASE("a WRLD chunk count that disagrees with META is rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    const WorldMeta meta = readMetaSection(save.section(sections::kMeta));
    std::vector<std::uint8_t> wrld(save.section(sections::kWrld).begin(),
                                   save.section(sections::kWrld).end());
    REQUIRE(wrld[0] == 36);
    wrld[0] = 35;
    CHECK_THROWS_AS(readWorldSection(meta, wrld), FormatError);
}

TEST_CASE("a truncated WRLD section is rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    const WorldMeta meta = readMetaSection(save.section(sections::kMeta));
    const std::span<const std::uint8_t> full = save.section(sections::kWrld);
    std::vector<std::uint8_t> wrld(full.begin(), full.end() - 40);
    CHECK_THROWS_AS(readWorldSection(meta, wrld), FormatError);
}

TEST_CASE("trailing bytes after the last WRLD chunk are rejected") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    const WorldMeta meta = readMetaSection(save.section(sections::kMeta));
    const std::span<const std::uint8_t> full = save.section(sections::kWrld);
    std::vector<std::uint8_t> wrld(full.begin(), full.end());
    wrld.push_back(0x00);
    CHECK_THROWS_AS(readWorldSection(meta, wrld), FormatError);
}
