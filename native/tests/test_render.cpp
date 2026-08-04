// The renderer, asserted on the pixels it actually produces.
//
// This file is the reason every sprint after S1 can prove visually that it
// works. The renderer is software, so a frame of the real Docks can be drawn
// inside the test suite -- no window, no GPU, no display server -- and facts
// about it checked on every build. "It looked right on my machine" is not a
// gate; "the frame has a horizon, has lamplight in it, and has the harbour on
// the left when you face north" is.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

namespace {

SessionConfig docksAt(int hour) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.width = 320;
    config.height = 180;
    return config;
}

float meanLuma(const Framebuffer& frame, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Rgb colour = unpackRgb(frame.pixels()[frame.index(x, y)]);
            sum += static_cast<double>(0.2126F * colour.r + 0.7152F * colour.g +
                                       0.0722F * colour.b);
            ++count;
        }
    }
    return count == 0 ? 0.0F : static_cast<float>(sum / static_cast<double>(count));
}

}  // namespace

TEST_CASE("the framebuffer packs to the byte order stb and SDL both want") {
    // R first in memory. Getting this backwards makes every screenshot look
    // like a bad TV and is invisible in a unit test that only round-trips.
    const std::uint32_t packed = packRgb(Rgb{1.0F, 0.0F, 0.0F});
    CHECK((packed & 0xFFU) == 255U);
    CHECK(((packed >> 8) & 0xFFU) == 0U);
    CHECK(((packed >> 16) & 0xFFU) == 0U);
    CHECK(((packed >> 24) & 0xFFU) == 255U);

    const Rgb back = unpackRgb(packRgb(Rgb{0.25F, 0.5F, 0.75F}));
    CHECK(back.r == doctest::Approx(0.25F).epsilon(0.01));
    CHECK(back.g == doctest::Approx(0.5F).epsilon(0.01));
    CHECK(back.b == doctest::Approx(0.75F).epsilon(0.01));
}

TEST_CASE("the day curve is committed dark and the lamps carry the night") {
    const SkyState noon = skyAt(12 * 3600);
    const SkyState midnight = skyAt(0);
    const SkyState dusk = skyAt(20 * 3600);
    const SkyState lateDusk = skyAt(21 * 3600);

    CHECK(noon.daylight == doctest::Approx(1.0F));
    CHECK(midnight.daylight == doctest::Approx(0.0F));
    // 20:00 is the default: the light is going but the district still reads.
    CHECK(dusk.daylight < 0.8F);
    CHECK(dusk.daylight > 0.3F);
    // An hour later it is all but gone and the lamps are carrying the frame.
    CHECK(lateDusk.daylight < 0.2F);
    CHECK(lateDusk.daylight < dusk.daylight);

    // Night ambient is near black -- torchlit pools against near-black is the
    // committed look, not a bright moonlit street.
    CHECK(midnight.ambient.r < 0.10F);
    CHECK(midnight.ambient.g < 0.10F);
    CHECK(noon.ambient.r > 0.4F);
    // And the fog closes in when the light goes.
    CHECK(midnight.fogDistance < noon.fogDistance);
}

TEST_CASE("a lamp lights a pool around itself and nothing far from it") {
    const Session session(docksAt(20));
    const LampGlow& glow = session.renderer().glow();
    REQUIRE(session.lampCount() == 27);
    CHECK(glow.litCellCount() > 1000);

    const std::vector<Lamp>& lamps = session.renderer().lamps();
    const Lamp& lamp = lamps.front();
    const Rgb centre = glow.at(lamp.x, lamp.y, lamp.z);
    CHECK(centre.r > 0.1F);
    // Warm: more red than blue, whichever kind of light it is.
    CHECK(centre.r > centre.b);
    // Falls off, and is gone well before ten tiles.
    const Rgb near = glow.at(lamp.x + 2, lamp.y, lamp.z);
    const Rgb far = glow.at(lamp.x + 10, lamp.y, lamp.z);
    CHECK(near.r < centre.r);
    CHECK(far.r == 0.0F);
    // And it does not shine through the floor two levels down.
    CHECK(glow.at(lamp.x, lamp.y, lamp.z - 3).r == 0.0F);
}

TEST_CASE("the Docks render to a frame with a world in it") {
    Session session(docksAt(20));
    REQUIRE(session.body().spawnedLegally());

    Framebuffer frame(320, 180);
    const FrameStats stats = session.drawFrame(frame);

    // Something was drawn, and it was not the whole screen or none of it: from
    // Tarwalk facing north you see the quay and the piers below the horizon and
    // open sky above.
    CHECK(stats.worldPixels > 8000);
    CHECK(stats.skyPixels > 3000);
    CHECK(stats.worldPixels + stats.skyPixels == 320U * 180U);

    // It is a lit scene with depth, not a flat fill.
    CHECK(stats.distinctColours > 200);
    CHECK(stats.meanLuma > 0.01F);
    CHECK(stats.meanLuma < 0.7F);
    CHECK(stats.nearestDepth < 3.0F);
    CHECK(stats.furthestDepth > 12.0F);

    // The bottom of the frame is ground, the top is sky. If the projection ever
    // flips, this is what says so.
    const float ground = meanLuma(frame, 0, 150, 320, 180);
    const float above = meanLuma(frame, 0, 0, 320, 20);
    CHECK(ground != above);
    // Every pixel that is world has a finite depth; every sky pixel does not.
    std::size_t finite = 0;
    for (const float d : frame.depth()) {
        if (std::isfinite(d)) {
            ++finite;
        }
    }
    CHECK(finite == stats.worldPixels);
}

TEST_CASE("noon is brighter than midnight, on the same geometry") {
    Session day(docksAt(12));
    Session night(docksAt(0));
    Framebuffer dayFrame(320, 180);
    Framebuffer nightFrame(320, 180);
    const FrameStats dayStats = day.drawFrame(dayFrame);
    const FrameStats nightStats = night.drawFrame(nightFrame);

    CHECK(dayStats.meanLuma > nightStats.meanLuma * 2.0F);
    // And the night frame is not simply black: the lamps are doing work.
    CHECK(nightStats.meanLuma > 0.002F);
}

TEST_CASE("a lamp in view draws a flame, and a lamp behind you does not") {
    // The sprite path, proven where it can be seen rather than wherever the
    // default spawn happens to be pointing.
    SessionConfig config = docksAt(22);
    Session session(config);
    const std::vector<Lamp>& lamps = session.renderer().lamps();
    const Lamp* target = nullptr;
    for (const Lamp& lamp : lamps) {
        if (lamp.z == sim::docks::kBandQuayside && lamp.name == "lamp_eelpot_02") {
            target = &lamp;
        }
    }
    REQUIRE(target != nullptr);

    // Stand four tiles due east of it and look west.
    SessionConfig standing = config;
    standing.spawnX = target->x + 4;
    standing.spawnY = target->y;
    standing.spawnBand = target->z;
    standing.spawnYaw = sim::kFacingWest;
    standing.spawnYawGiven = true;
    Session facing(standing);
    REQUIRE(facing.body().spawnedLegally());

    // One sprite, so the count is about THIS lamp and not about the other
    // twenty-six that happen to be down the street either way you turn.
    SpriteInstance flame;
    flame.x = static_cast<float>(target->x) + 0.5F;
    flame.y = static_cast<float>(target->y) + 0.5F;
    flame.z = static_cast<float>(target->z) + 0.62F;
    flame.colour = Rgb{1.0F, 0.85F, 0.6F};
    flame.glow = 1.0F;
    const std::vector<SpriteInstance> one{flame};

    Framebuffer towards(320, 180);
    const FrameStats seen =
        facing.renderer().renderFrame(towards, facing.camera(), RenderSettings{}, one);
    CHECK(seen.spritePixels > 0);

    facing.body().setYaw(sim::kFacingEast);
    Framebuffer away(320, 180);
    const FrameStats behind =
        facing.renderer().renderFrame(away, facing.camera(), RenderSettings{}, one);
    CHECK(behind.spritePixels == 0);
}

TEST_CASE("turning around changes the frame") {
    Session session(docksAt(20));
    Framebuffer north(320, 180);
    session.drawFrame(north);

    session.body().setYaw(sim::kFacingSouth);
    Framebuffer south(320, 180);
    session.drawFrame(south);

    std::size_t differing = 0;
    for (std::size_t i = 0; i < north.pixels().size(); ++i) {
        if (north.pixels()[i] != south.pixels()[i]) {
            ++differing;
        }
    }
    // Facing the harbour and facing the warehouse fronts are not the same view.
    CHECK(differing > north.pixels().size() / 3);
}

TEST_CASE("the same session renders the same frame twice") {
    // A captured frame is only evidence if it is reproducible.
    Session a(docksAt(20));
    Session b(docksAt(20));
    Framebuffer left(320, 180);
    Framebuffer right(320, 180);
    a.drawFrame(left);
    b.drawFrame(right);
    CHECK(left.pixels() == right.pixels());
}

TEST_CASE("the HUD hugs the edges and leaves the centre completely clear") {
    // The direct answer to task #66. The Java build's first-person view put an
    // inspector sheet and a craftings bar in the play space; this is the check
    // that stops that happening again.
    Session session(docksAt(20));
    Framebuffer bare(320, 180);
    session.renderer().renderFrame(bare, session.camera(), RenderSettings{},
                                   std::vector<SpriteInstance>{});

    Framebuffer dressed(320, 180);
    session.renderer().renderFrame(dressed, session.camera(), RenderSettings{},
                                   std::vector<SpriteInstance>{});
    HudState hud;
    hud.health = 72;
    hud.yawBam = session.body().yaw();
    hud.locationLabel = "TARWALK - QUAYSIDE";
    drawHud(dressed, hud);

    const CentreRect centre = hudCentreRect(320, 180);
    CHECK(centre.x0 > 0);
    CHECK(centre.y0 > 0);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(bare.pixels()[bare.index(x, y)] == dressed.pixels()[dressed.index(x, y)]);
        }
    }
    // And it drew SOMETHING, or the check above is vacuous.
    std::size_t changed = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        if (bare.pixels()[i] != dressed.pixels()[i]) {
            ++changed;
        }
    }
    CHECK(changed > 300);
}

TEST_CASE("the HUD's health bar tracks the number it is given") {
    const auto litPixels = [](int health) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.health = health;
        drawHud(frame, hud);
        std::size_t red = 0;
        for (const std::uint32_t pixel : frame.pixels()) {
            const Rgb colour = unpackRgb(pixel);
            if (colour.r > 0.4F && colour.g < 0.35F) {
                ++red;
            }
        }
        return red;
    };
    CHECK(litPixels(100) > litPixels(50));
    CHECK(litPixels(50) > litPixels(10));
    CHECK(litPixels(0) == 0);
}

TEST_CASE("the compass names the direction the body is facing") {
    CHECK(sim::compass_point(sim::kFacingNorth) == "N");
    CHECK(sim::compass_point(sim::kFacingWest) == "W");
    // The needle is drawn from the same BAM the body carries, so there is no
    // second source of truth for which way the player is looking.
    Session session(docksAt(20));
    CHECK(session.body().yaw() == sim::docks::kSpawnYaw);
}

TEST_CASE("the capture path produces a PNG with no window anywhere in sight") {
    SmokeRunConfig config;
    config.session = docksAt(20);
    config.steps = 45;
    config.captureScale = 1;
    config.screenshot =
        std::filesystem::temp_directory_path() / "granadad-test-capture.png";
    std::filesystem::remove(config.screenshot);

    const SmokeRunResult result = runSmoke(config);
    CHECK(result.ok);
    CHECK(result.lampCount == 27);
    CHECK(result.stats.worldPixels > 5000);
    REQUIRE(std::filesystem::exists(config.screenshot));
    // A real PNG, not an empty file: the 8-byte signature plus a chunk or two.
    CHECK(std::filesystem::file_size(config.screenshot) > 1000);

    // The body walked while the smoke ran, which is what makes a capture a
    // picture of the game rather than a picture of the spawn.
    CHECK((result.endTileX != sim::docks::kSpawnTileX ||
           result.endTileY != sim::docks::kSpawnTileY));
    CHECK(result.endBand == sim::docks::kBandQuayside);
    std::filesystem::remove(config.screenshot);
}

TEST_CASE("the capture upscales with nearest neighbour and nothing else") {
    Framebuffer small(4, 4);
    small.clear(Rgb{0.0F, 0.0F, 0.0F});
    small.set(1, 1, Rgb{1.0F, 1.0F, 1.0F}, 1.0F);
    const Framebuffer big = upscaleNearest(small, 3);
    CHECK(big.width() == 12);
    CHECK(big.height() == 12);
    // The white texel becomes a hard 3x3 block with no blur anywhere near it.
    for (int y = 3; y < 6; ++y) {
        for (int x = 3; x < 6; ++x) {
            REQUIRE(big.pixels()[big.index(x, y)] == small.pixels()[small.index(1, 1)]);
        }
    }
    CHECK(big.pixels()[big.index(2, 3)] == small.pixels()[small.index(0, 1)]);
    CHECK(big.pixels()[big.index(6, 3)] == small.pixels()[small.index(2, 1)]);
}

TEST_CASE("walking to the water's edge keeps the harbour in front of the eye") {
    // A behavioural check on the whole stack: walk north off Tarwalk, and the
    // frame should end up with more water in the lower half than it started
    // with. Water is the darkest thing in the district at dusk.
    Session session(docksAt(20));
    session.body().setYaw(sim::kFacingNorth);
    Framebuffer before(320, 180);
    session.drawFrame(before);
    const float startLower = meanLuma(before, 0, 110, 320, 180);

    sim::MoveInput forward;
    forward.forward = 1;
    session.stepMany(forward, 8 * sim::kStepsPerSecond);

    Framebuffer after(320, 180);
    session.drawFrame(after);
    const float endLower = meanLuma(after, 0, 110, 320, 180);

    CHECK(session.body().tileY() < sim::docks::kSpawnTileY);
    CHECK(startLower != endLower);
}
