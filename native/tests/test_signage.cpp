// Docks signage: the generated table, the name-resolution priority order,
// and the world-space label pass that draws it.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/signage_renderer.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/docks_signs.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace docks = granadad::sim::docks;

namespace {

SessionConfig docksAt(int hour) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.width = 320;
    config.height = 180;
    return config;
}

/// True when at least one pixel of `frame` is exactly the sign ink colour --
/// full-alpha text, which is what a legible label this close always draws.
bool hasSignInk(const Framebuffer& frame) {
    constexpr Rgb kSignInk{0.93F, 0.87F, 0.68F};
    const std::uint32_t inkPacked = packRgb(kSignInk);
    for (const std::uint32_t pixel : frame.pixels()) {
        if (pixel == inkPacked) {
            return true;
        }
    }
    return false;
}

const docks::Sign* findSign(const char* id) {
    for (const docks::Sign& sign : docks::kSigns) {
        if (std::strcmp(sign.id, id) == 0) {
            return &sign;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("the generated sign table is the survey's own count, and every door has a real name") {
    // 83 markers total: the exact number the signage survey's own tmx parse
    // found (Part 2), not a round number somebody guessed.
    CHECK(docks::kSignCount == 83);

    std::size_t doors = 0;
    std::size_t ways = 0;
    for (const docks::Sign& sign : docks::kSigns) {
        // The K-numbering skips k35 on purpose (Part 2's own finding: an
        // unused number, not a missed building) -- so no id should ever
        // reference it.
        CHECK(std::strstr(sign.id, "k35") == nullptr);
        if (sign.kind == docks::SignKind::Door) {
            ++doors;
            // Part 3's finding, made a standing check: of the 40 doors the
            // survey found, ZERO needed a synthesized name -- every one
            // already carries a real `place` string recovered from the tmx.
            CHECK(sign.place != nullptr);
            CHECK(sign.place[0] != '\0');
        } else {
            ++ways;
        }
    }
    CHECK(doors == 40);
    CHECK(ways == 43);
}

TEST_CASE("a building already in docks::kPlaces wins over its own tmx sign") {
    const docks::Sign* gull = findSign("sign_k03_gilded_gull");
    REQUIRE(gull != nullptr);
    // Verifies the survey's own coordinate-transform proof: the generated
    // world rect for this sign is an EXACT match for kPlaces' own entry.
    CHECK(gull->x0 == 146);
    CHECK(gull->y0 == 66);
    CHECK(gull->x1 == 160);
    CHECK(gull->y1 == 79);
    CHECK(gull->band == sim::docks::kBandQuayside);

    const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(*gull);
    CHECK(resolved.tier == docks::SignSourceTier::KnownPlace);
    CHECK(resolved.text == "THE GILDED GULL");
}

TEST_CASE("a building with no docks::kPlaces entry reads its real tmx-recovered name") {
    const docks::Sign* weighhouse = findSign("sign_k01_weighhouse");
    REQUIRE(weighhouse != nullptr);

    const docks::ResolvedSignLabel resolved = docks::resolveSignLabel(*weighhouse);
    CHECK(resolved.tier == docks::SignSourceTier::TmxRecovered);
    CHECK(resolved.text == "The Weighhouse");
}

TEST_CASE("a sign in view draws its label, and the same sign behind you does not") {
    // The shipped spawn: docks.hpp's own header says the Gull's frontage and
    // its door lamp are in shot from here, which puts sign_k03_gilded_gull's
    // anchor in view too.
    Session session(docksAt(20));
    REQUIRE(session.body().spawnedLegally());

    Framebuffer facing(320, 180);
    session.renderer().renderFrame(facing, session.camera(), RenderSettings{}, {});
    drawSignage(facing, session.camera());
    CHECK(hasSignInk(facing));

    // Turn around: the same sign is now behind the eye and must not draw --
    // the near-cull (SignageSettings::minDistance) is a dot product against
    // forward, so a sign behind the camera projects to a negative distance
    // and is skipped before it ever reaches the screen or the depth test.
    session.body().setYaw(session.body().yaw() + sim::kTurnHalf);
    Framebuffer away(320, 180);
    session.renderer().renderFrame(away, session.camera(), RenderSettings{}, {});
    drawSignage(away, session.camera());
    CHECK_FALSE(hasSignInk(away));
}

TEST_CASE("a sign far past the legibility radius does not draw") {
    SessionConfig config = docksAt(20);
    // Out on the open water-facing quay, 40 tiles from anything named --
    // well past SignageSettings::maxDistance (16 tiles) from every sign in
    // the table.
    config.spawnX = 200;
    config.spawnY = 40;
    config.spawnBand = sim::docks::kBandQuayside;
    config.spawnYaw = sim::kFacingWest;
    config.spawnYawGiven = true;
    Session session(config);
    if (!session.body().spawnedLegally()) {
        return;  // not a legal stand here in every baked revision; not what this case is about
    }

    Framebuffer frame(320, 180);
    session.renderer().renderFrame(frame, session.camera(), RenderSettings{}, {});
    drawSignage(frame, session.camera());
    CHECK_FALSE(hasSignInk(frame));
}
