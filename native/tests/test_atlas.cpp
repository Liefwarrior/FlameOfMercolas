// The tile art: the material ordering it is indexed by, the procedural
// fallback, and the owner's actual pack when it is present.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/atlas.hpp"

using namespace granadad::render;

TEST_CASE("the material ordering matches the ids baked into the shipped worlds") {
    const std::span<const std::string_view> ids = materialIds();
    // 21 authored raws under content/raws/materials plus the one the
    // getilia-soak treatment mints.
    REQUIRE(ids.size() == 22);

    // Sorted as strings, which is how sim-core assigns the registry ids.
    for (std::size_t i = 1; i < ids.size(); ++i) {
        CAPTURE(i);
        CAPTURE(std::string(ids[i - 1]));
        CAPTURE(std::string(ids[i]));
        REQUIRE(ids[i - 1] < ids[i]);
    }

    // The ids the TROJSAV suite already reads out of the shipped bytes
    // (native/content/tests/fixtures.hpp). If the raws grow a material the
    // MATERIAL lane shifts under every world, and these are what say so.
    CHECK(ids[0] == "ash");
    CHECK(ids[1] == "brick");
    CHECK(ids[6] == "dirt");
    CHECK(ids[8] == "granite");
    CHECK(ids[14] == "oak");
    CHECK(ids[16] == "reman_concrete");
    CHECK(ids[19] == "thatch");
    CHECK(ids[20] == "trudgeon_wood");
}

TEST_CASE("the procedural fallback covers every material and every face") {
    const TileAtlas atlas = TileAtlas::procedural();
    CHECK_FALSE(atlas.fromAuthoredArt());
    CHECK(atlas.tileCount() > materialIds().size());
    CHECK(atlas.lightTintQ8().size() == 32);
    CHECK(atlas.waterDepthAlphaQ8().size() == 8);

    // Every material/face pair resolves to a real tile, and none of them is the
    // missing-texture chequer -- a fallback that quietly renders the whole
    // district magenta is not a fallback.
    const std::size_t missing = atlas.tileFor(9999, FaceKind::Side, 0);
    for (std::uint16_t m = 0; m < static_cast<std::uint16_t>(materialIds().size()); ++m) {
        for (std::size_t f = 0; f < kFaceKindCount; ++f) {
            CAPTURE(m);
            CAPTURE(f);
            const std::size_t tile = atlas.tileFor(m, static_cast<FaceKind>(f), 0);
            REQUIRE(tile != missing);
            REQUIRE(tile < atlas.tileCount());
        }
    }

    // Distinct materials look distinct, or the world is one grey soup.
    const Rgb granite = atlas.averageOf(atlas.tileFor(8, FaceKind::Side, 0));
    const Rgb thatch = atlas.averageOf(atlas.tileFor(19, FaceKind::Side, 0));
    CHECK(std::abs(granite.r - thatch.r) + std::abs(granite.g - thatch.g) +
              std::abs(granite.b - thatch.b) >
          0.15F);
}

TEST_CASE("tile lookup is stable per cell, so walls do not shimmer") {
    const TileAtlas atlas = TileAtlas::procedural();
    const std::uint32_t key = 0x1234ABCDU;
    const std::size_t first = atlas.tileFor(8, FaceKind::Side, key);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(atlas.tileFor(8, FaceKind::Side, key) == first);
    }
}

TEST_CASE("texel sampling wraps rather than reading out of bounds") {
    const TileAtlas atlas = TileAtlas::procedural();
    const std::size_t tile = atlas.tileFor(8, FaceKind::FloorTop, 0);
    CHECK(atlas.texelRaw(tile, 0, 0) == atlas.texelRaw(tile, 16, 16));
    CHECK(atlas.texelRaw(tile, -1, -1) == atlas.texelRaw(tile, 15, 15));
    // And an out-of-range tile index answers rather than crashing.
    CHECK(atlas.texelRaw(atlas.tileCount() + 50, 3, 3) == 0xFFFF00FFU);
}

TEST_CASE("the owner's art pack loads when it is there") {
    const TileAtlas atlas = TileAtlas::load(granadad::content::contentDir());
    // The docker build admits content/art/custom (43 KB) precisely so this
    // case is real there and not skipped. If it ever starts skipping, the
    // renderer is being tested against its own fallback.
    REQUIRE(atlas.fromAuthoredArt());

    // 112 authored regions covering 22 materials (19 base + the 3 civic-facade materials,
    // each with its own distinct wall region -- see atlas.cpp's load()); every region is at
    // least one tile and several carry animation variants, so the store is well over 100.
    CHECK(atlas.tileCount() > 100);
    CHECK(atlas.lightTintQ8().size() == 32);
    CHECK(atlas.lightTintQ8().front() == 36);
    CHECK(atlas.lightTintQ8().back() == 256);
    CHECK(atlas.waterDepthAlphaQ8()[0] == 0);
    CHECK(atlas.waterDepthAlphaQ8()[7] == 240);

    const std::size_t missing = atlas.tileFor(9999, FaceKind::Side, 0);
    for (std::uint16_t m = 0; m < static_cast<std::uint16_t>(materialIds().size()); ++m) {
        for (std::size_t f = 0; f < kFaceKindCount; ++f) {
            CAPTURE(std::string(materialIds()[m]));
            CAPTURE(f);
            // Including the three facade materials, which now carry their own distinct
            // Side/BlockTop wall art (a pediment/pilaster-trimmed region, not their base
            // material's); their FloorTop/RampTop/StairTop still alias to the base material
            // they face, since a facade is never authored as a floor/ramp/stair.
            REQUIRE(atlas.tileFor(m, static_cast<FaceKind>(f), 0) != missing);
        }
    }

    // The authored pack is not flat colour: a granite face has real texture in
    // it, which is what makes the chunky close-range look work.
    const std::size_t tile = atlas.tileFor(8, FaceKind::Side, 0);
    std::vector<std::uint32_t> distinct;
    for (int v = 0; v < TileAtlas::kTilePx; ++v) {
        for (int u = 0; u < TileAtlas::kTilePx; ++u) {
            distinct.push_back(atlas.texelRaw(tile, u, v));
        }
    }
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    CHECK(distinct.size() > 2);
}

TEST_CASE("the three facade materials carry their own art, not their base material's") {
    // Regression test for the bug this task fixed: art-mapping.json used to have zero
    // regions for granite_facade/brick_facade/reman_facade, so atlas.cpp's own fallback
    // silently aliased their Side (wall) art to their base material -- a civic building's
    // "pedimented-colonnade" street frontage rendered pixel-identical to a plain wall, with
    // nothing here to catch a regression back to that state.
    const TileAtlas atlas = TileAtlas::load(granadad::content::contentDir());
    REQUIRE(atlas.fromAuthoredArt());

    // kMaterialIds is sorted; see atlas.cpp's kMaterialIds literal for these positions.
    struct Pair {
        std::uint16_t facade;
        std::uint16_t base;
        const char* name;
    };
    const Pair pairs[] = {
        {2, 1, "brick_facade vs brick"},
        {9, 8, "granite_facade vs granite"},
        {17, 16, "reman_facade vs reman_concrete"},
    };
    for (const Pair& p : pairs) {
        CAPTURE(p.name);
        const std::size_t facadeTile = atlas.tileFor(p.facade, FaceKind::Side, 0);
        const std::size_t baseTile = atlas.tileFor(p.base, FaceKind::Side, 0);
        // Not just a different tile index -- genuinely different pixels, so a future
        // regression that re-aliases the facade onto its base (same average colour, same
        // texture) would fail this even if it kept the tile counts looking plausible.
        CHECK(facadeTile != baseTile);
        bool anyTexelDiffers = false;
        for (int v = 0; v < TileAtlas::kTilePx && !anyTexelDiffers; ++v) {
            for (int u = 0; u < TileAtlas::kTilePx; ++u) {
                if (atlas.texelRaw(facadeTile, u, v) != atlas.texelRaw(baseTile, u, v)) {
                    anyTexelDiffers = true;
                    break;
                }
            }
        }
        CHECK(anyTexelDiffers);
    }
}
