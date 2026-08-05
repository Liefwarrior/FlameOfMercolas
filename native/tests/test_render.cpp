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

    // Something was drawn.
    CHECK(stats.worldPixels > 8000);
    // (S1 also asserted worldPixels + skyPixels == w*h here. That is an
    // identity -- world_renderer.cpp defines skyPixels as w*h - worldPixels --
    // so it could not fail. Removed rather than kept as decoration.)
    //
    // THE SKY MOVED OUT OF THIS FRAME IN polish-1 AND THAT IS THE FIX WORKING.
    //
    // This case used to assert skyPixels > 3000 and `the top twelve rows are
    // under 5% world` from the authored spawn, and both passed for a bad
    // reason: a storey was one tile tall, so the Gilded Gull's frontage six
    // tiles across the Tarwalk topped out BELOW the eye line and you could see
    // over the whole district from the pavement.
    //
    // A storey is three tiles now (sim/vertical_scale.hpp). That frontage is
    // three storeys and a roof-slum plane above it, so it stands about 9 tiles
    // -- 8 m -- out of a street 5.5 m wide, and the top of it is 50 degrees up
    // from an eye with a 29-degree half-frame. It fills the view, the way the
    // wall of a real warehouse fills the view when you stand under it. A test
    // that demands sky from that spot is a test demanding the bug back.
    //
    // So the two halves of the original claim are now asked of the two frames
    // that can honestly answer them: the level frame still owes us ground under
    // our feet, and a frame that LOOKS UP still owes us sky.

    // It is a lit scene with depth, not a flat fill.
    CHECK(stats.distinctColours > 200);
    CHECK(stats.meanLuma > 0.01F);
    CHECK(stats.meanLuma < 0.7F);
    CHECK(stats.nearestDepth < 3.0F);
    CHECK(stats.furthestDepth > 12.0F);

    // The bottom of the frame is GROUND and the top is SKY, stated as the
    // direction it actually is. S1 checked `groundLuma != skyLuma`, which is
    // true for a flipped projection too.
    const auto worldFraction = [&frame](int y0, int y1) {
        std::size_t world = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                if (std::isfinite(frame.depth()[frame.index(x, y)])) {
                    ++world;
                }
            }
        }
        return static_cast<float>(world) /
               static_cast<float>(frame.width() * (y1 - y0));
    };
    // Standing on a street with the eye level: the deck fills the bottom of the
    // frame. Whatever else changes about the district's height, the paving
    // under your own boots does not go anywhere.
    CHECK(worldFraction(150, 180) > 0.99F);

    // Every pixel that is world has a finite depth; every sky pixel does not.
    std::size_t finite = 0;
    for (const float d : frame.depth()) {
        if (std::isfinite(d)) {
            ++finite;
        }
    }
    CHECK(finite == stats.worldPixels);

    // THE PROJECTION IS THE RIGHT WAY UP, checked on the ground rather than on
    // the sky, so that it stays a fact about the projection and not a fact
    // about how tall the buildings happen to be. Paving runs AWAY from you as
    // your eye climbs the frame: the pixel at your boots is nearer than the
    // pixel most of the way up toward the horizon. A flipped frame reverses it.
    const float atBoots = frame.depth()[frame.index(160, 178)];
    const float towardHorizon = frame.depth()[frame.index(160, 100)];
    REQUIRE(std::isfinite(atBoots));
    REQUIRE(std::isfinite(towardHorizon));
    CHECK(atBoots < towardHorizon);

    // AND NOW LOOK UP. Seventy degrees of pitch clears anything the Docks has:
    // the tallest thing in the baked district is the roof-slum plane four bands
    // over the quay, twelve tiles, and a frontage six tiles away would have to
    // stand sixteen tiles proud of the eye to reach the top of this frame. So
    // the sky is up there, and this is the half of the original claim that
    // survives -- asked of a camera that can honestly answer it.
    session.body().setPitch(sim::angle_from_degrees(70));
    Framebuffer looking(320, 180);
    const FrameStats up = session.drawFrame(looking);
    CHECK(up.skyPixels > 3000);

    std::size_t skyAtTop = 0;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < looking.width(); ++x) {
            if (!std::isfinite(looking.depth()[looking.index(x, y)])) {
                ++skyAtTop;
            }
        }
    }
    CHECK(skyAtTop > static_cast<std::size_t>(looking.width() * 12) * 9 / 10);
}

TEST_CASE("a storey is three tiles tall and the eye is a person's eye inside it") {
    // THE CASE ELI'S COMPLAINT DESERVED. "All buildings only appear to be one
    // tile high" was true and nothing in 469 tests said so, because every
    // height in the build was a level number cast to a float and the projection
    // agreed with itself all the way down.
    //
    // These are the numbers that make a district read as a city. They are
    // arithmetic, not pixels, so they cannot rot the way a screenshot does.
    CHECK(sim::kTilesPerBand == 3);
    CHECK(kBandHeight == doctest::Approx(3.0F));

    // A level's walking surface is three tiles above the one below it, so the
    // WALL that spans a level is three tiles of side face. One tile was the
    // crawlspace.
    CHECK(bandSurface(20) - bandSurface(19) == doctest::Approx(3.0F));

    // The body's z axis counts BANDS in Q8 and the renderer's counts TILES, and
    // the eye has to be at the same height on both. 435/256 = 1.70 tiles.
    CHECK(sim::kEyeHeight * sim::kTilesPerBand == sim::kEyeHeightTilesQ8);
    Session session(docksAt(20));
    const Camera view = session.camera();
    const float feet = bandSurface(session.body().band());
    CHECK(view.z - feet == doctest::Approx(1.70F).epsilon(0.01));

    // AND THE WHOLE POINT: the eye stands well under the top of the single
    // storey it is inside. It used to stand 0.80 of the way up it, which is why
    // a two-storey warehouse read as a garden wall.
    CHECK(view.z < bandSurface(session.body().band() + 1));
    CHECK(bandSurface(session.body().band() + 1) - view.z > 1.0F);

    // People are people-sized against the same scale. The tallest billboard an
    // actor is made of must reach near the player's own eye -- a district of
    // knee-high figures would have made the buildings look right and everything
    // else look like a diorama.
    const std::vector<SpriteInstance> people = session.actorSprites();
    if (!people.empty()) {
        // Each part measured against the floor of the level it is standing on,
        // not against the player's -- the Gull's people are spread over the
        // taproom and the guest floor above it, and a head three tiles higher
        // than the eye is a person upstairs, not a giant.
        float crown = 0.0F;
        for (const SpriteInstance& part : people) {
            const auto level = static_cast<std::int32_t>(std::floor(part.z / kBandHeight));
            crown = std::max(crown, part.z + part.halfHeight - bandSurface(level));
        }
        CHECK(crown > 1.5F);
        CHECK(crown < 2.3F);
    }
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
    // bandSurface(), like every other height in the build: a level is
    // kBandHeight tiles tall and `target->z` is a level number, not a height.
    flame.z = bandSurface(target->z) + 1.90F;
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

TEST_CASE("standing under a lamp does not white out the frame") {
    // An additive billboard grows on screen as you approach it, so its total
    // contribution grows with the square of how close you are. Two tiles from
    // the Gilded Gull's door lamp at five in the morning, that was a
    // featureless white disc across half the view. A flame is a small hot
    // thing: walking up to it makes it bigger, not brighter.
    SessionConfig config = docksAt(5);
    const Session probe(config);
    const Lamp* target = nullptr;
    for (const Lamp& lamp : probe.renderer().lamps()) {
        if (lamp.name == "lamp_gull_door") {
            target = &lamp;
        }
    }
    REQUIRE(target != nullptr);

    const auto lumaFrom = [&config, target](std::int32_t away) {
        SessionConfig standing = config;
        standing.spawnX = target->x;
        standing.spawnY = target->y - away;
        standing.spawnBand = target->z;
        standing.spawnYaw = sim::kFacingSouth;
        standing.spawnYawGiven = true;
        Session session(standing);
        REQUIRE(session.body().spawnedLegally());
        Framebuffer frame(320, 180);
        return session.drawFrame(frame).meanLuma;
    };

    const float pressedAgainstIt = lumaFrom(1);
    const float acrossTheStreet = lumaFrom(5);

    // MEASURED, from this exact tile and this exact lamp: 0.1290 without the
    // roll-off, 0.1015 with it. The threshold sits between the two on purpose,
    // so this case is RED under the defect rather than merely true without it —
    // checked by mutation on 2026-08-04.
    CHECK(pressedAgainstIt < 0.115F);
    // ...and the lamp is still doing something: brighter one tile away than
    // five, or the roll-off has simply deleted it.
    CHECK(pressedAgainstIt > acrossTheStreet * 1.5F);
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
    // All eight points, not two. S1 checked N and W and then asserted
    // `body.yaw() == kSpawnYaw` -- which session.cpp assigns from that same
    // constant -- under a title about the compass, and never drew one.
    CHECK(sim::compass_point(sim::kFacingNorth) == "N");
    CHECK(sim::compass_point(sim::kFacingEast) == "E");
    CHECK(sim::compass_point(sim::kFacingSouth) == "S");
    CHECK(sim::compass_point(sim::kFacingWest) == "W");
    CHECK(sim::compass_point(sim::kFacingNorth + sim::kTurnFull / 8) == "NE");
    CHECK(sim::compass_point(sim::kFacingEast + sim::kTurnFull / 8) == "SE");
    CHECK(sim::compass_point(sim::kFacingSouth + sim::kTurnFull / 8) == "SW");
    CHECK(sim::compass_point(sim::kFacingWest + sim::kTurnFull / 8) == "NW");
    // It rounds to the nearest point rather than truncating toward one.
    CHECK(sim::compass_point(sim::kFacingNorth + sim::kTurnFull / 32) == "N");
    CHECK(sim::compass_point(sim::kTurnFull - sim::kTurnFull / 32) == "N");

    // And the drawn needle tracks the BAM it is handed. Two headings, one
    // frame each, and the strip the compass lives in must not be identical --
    // otherwise the ribbon is a picture and the yaw is decoration.
    const auto compassStrip = [](std::int32_t yawBam) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.yawBam = yawBam;
        drawHud(frame, hud);
        std::vector<std::uint32_t> strip;
        for (int y = 0; y < 24; ++y) {
            for (int x = 80; x < 240; ++x) {
                strip.push_back(frame.pixels()[frame.index(x, y)]);
            }
        }
        return strip;
    };
    CHECK(compassStrip(sim::kFacingNorth) != compassStrip(sim::kFacingEast));
    CHECK(compassStrip(sim::kFacingNorth) == compassStrip(sim::kFacingNorth));
    // A degree of turn moves it; a full turn brings it back to the same pixels.
    CHECK(compassStrip(sim::kFacingNorth) ==
          compassStrip(sim::kFacingNorth + sim::kTurnFull));
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
    // A behavioural check on the whole stack: walk north off Tarwalk to the
    // quay lip. What was a deck two feet under the eye becomes open harbour two
    // levels down and thirty tiles wide, so the lower half of the frame both
    // recedes and darkens -- water is the darkest thing in the district at
    // dusk. S1 checked only that the two numbers DIFFERED, under a comment that
    // promised a direction.
    Session session(docksAt(20));
    session.body().setYaw(sim::kFacingNorth);
    Framebuffer before(320, 180);
    session.drawFrame(before);
    const float startLower = meanLuma(before, 0, 110, 320, 180);

    const auto meanLowerDepth = [](const Framebuffer& frame) {
        double sum = 0.0;
        int count = 0;
        for (int y = 110; y < 180; ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                const float d = frame.depth()[frame.index(x, y)];
                if (std::isfinite(d)) {
                    sum += static_cast<double>(d);
                    ++count;
                }
            }
        }
        return count == 0 ? 0.0F : static_cast<float>(sum / static_cast<double>(count));
    };
    const float startDepth = meanLowerDepth(before);

    sim::MoveInput forward;
    forward.forward = 1;
    session.stepMany(forward, 8 * sim::kStepsPerSecond);

    Framebuffer after(320, 180);
    session.drawFrame(after);
    const float endLower = meanLuma(after, 0, 110, 320, 180);
    const float endDepth = meanLowerDepth(after);

    // It walked, and it stopped at the lip rather than in the water.
    CHECK(session.body().tileY() < sim::docks::kSpawnTileY);
    CHECK(session.body().band() == sim::docks::kBandQuayside);
    // ...and the view below the horizon is now the harbour: further away, and
    // darker than the lamplit deck it replaced.
    CHECK(endDepth > startDepth);
    CHECK(endLower < startLower);
}

// ===========================================================================
// THE TOPIC LIST -- the S3 review's second and third findings, closed
// ===========================================================================

namespace {

/// Draws a surface over a flat frame and answers whether the exclusion
/// rectangle came out untouched. The same proof the S3 case uses, in a helper,
/// because S4 has three more surfaces to hold to it.
[[nodiscard]] bool centreUntouched(const DialogueViewState& view, int width, int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(dressed, view);
    const CentreRect centre = hudCentreRect(width, height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                return false;
            }
        }
    }
    return true;
}

/// How many pixels a surface actually changed. Keeps every check above from
/// passing because nothing drew at all.
[[nodiscard]] std::size_t inkOf(const DialogueViewState& view, int width, int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(dressed, view);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        changed += bare.pixels()[i] != dressed.pixels()[i] ? 1U : 0U;
    }
    return changed;
}

}  // namespace

TEST_CASE("every topic is reachable by a number printed beside it") {
    // S3 numbered twelve slots 1-9 and then printed a full stop for the rest,
    // reachable only by arrow keys with nothing on screen saying so; and the
    // thirteenth topic would have vanished with no ellipsis at all. Master Venn
    // already filled all twelve. Both findings are closed by paging.
    CHECK(topicPageCount(0) == 1);
    CHECK(topicPageCount(1) == 1);
    CHECK(topicPageCount(9) == 1);
    CHECK(topicPageCount(10) == 2);
    CHECK(topicPageCount(18) == 2);
    CHECK(topicPageCount(19) == 3);
    CHECK_FALSE(topicsPaginate(9));
    CHECK(topicsPaginate(10));

    // NO TOPIC IS EVER DROPPED: every index of a list of any length lands on
    // exactly one page, in a slot that has a key printed on it.
    for (int count = 1; count <= 40; ++count) {
        for (int i = 0; i < count; ++i) {
            const int page = topicPageOf(i);
            const int slot = i - page * kTopicPageSize;
            CHECK(page >= 0);
            CHECK(page < topicPageCount(static_cast<std::size_t>(count)));
            CHECK(slot >= 0);
            CHECK(slot < kTopicPageSize);
        }
    }

    // A page and its MORE row fit the grid, so a page is never short of room.
    CHECK(kTopicPageSize + 1 <= kTopicSlots);
}

TEST_CASE("page two of a long list DRAWS nine numbered rows, not none") {
    // THE CASE S4 SHOULD HAVE HAD, and the S4 review is why it exists.
    //
    // S4 closed "topics past the ninth were unreachable" with the arithmetic
    // case above, which never touches the drawing code. The review reinstated
    // the original bug in dialogue_view.cpp -- `std::min(total,
    // kTopicPageSize)` where the real line reads `std::min(total, first +
    // kTopicPageSize)`, which makes page two and page three draw ZERO topic
    // rows -- and the whole 311-case gate stayed green, because the neighbour
    // below only asserts that SOME ink is on screen and an empty page still has
    // a speaker header on it.
    //
    // topicRowsFor IS the drawing path: drawDialogue calls it and prints
    // exactly what it returns. That mutation empties the vector this asserts.
    std::vector<std::string> topics;
    for (int i = 0; i < 20; ++i) {
        topics.push_back("ASK ABOUT SOMETHING NUMBER " + std::to_string(i + 1));
    }
    const int capacity = kTopicSlots;

    // Page one: nine numbered rows plus the row that names the key which turns
    // the page.
    const std::vector<TopicRow> first = topicRowsFor(topics, 0, 0, capacity);
    REQUIRE(first.size() == static_cast<std::size_t>(kTopicPageSize) + 1);
    for (int i = 0; i < kTopicPageSize; ++i) {
        const std::string& label = first[static_cast<std::size_t>(i)].label;
        INFO("page 1 row ", i, " = ", label);
        REQUIRE(label.rfind(std::to_string(i + 1) + " ", 0) == 0);
        REQUIRE(label.find(topics[static_cast<std::size_t>(i)]) != std::string::npos);
    }
    CHECK(first.back().label == "0 MORE (1/3)");
    CHECK(first.front().picked);

    // PAGE TWO. Nine rows again, numbered 1..9 again -- the number is the KEY,
    // and the key is a slot on the visible page -- each carrying the tenth to
    // eighteenth topic of the list.
    const std::vector<TopicRow> second = topicRowsFor(topics, 1, kTopicPageSize, capacity);
    REQUIRE(second.size() == static_cast<std::size_t>(kTopicPageSize) + 1);
    for (int i = 0; i < kTopicPageSize; ++i) {
        const std::string& label = second[static_cast<std::size_t>(i)].label;
        INFO("page 2 row ", i, " = ", label);
        REQUIRE(label.rfind(std::to_string(i + 1) + " ", 0) == 0);
        REQUIRE(label.find(topics[static_cast<std::size_t>(kTopicPageSize + i)]) !=
                std::string::npos);
    }
    CHECK(second.back().label == "0 MORE (2/3)");
    CHECK(second.front().picked);

    // Page three is the short one: two topics and the MORE row.
    const std::vector<TopicRow> third = topicRowsFor(topics, 2, 2 * kTopicPageSize, capacity);
    REQUIRE(third.size() == 3);
    CHECK(third[0].label.rfind("1 ", 0) == 0);
    CHECK(third[1].label.rfind("2 ", 0) == 0);
    CHECK(third.back().label == "0 MORE (3/3)");

    // Every topic of the list appears on exactly one page, in a row with a key
    // printed on it. This is the claim the whole paging scheme rests on.
    //
    // Matched on the WHOLE row and not on a substring of it: "...NUMBER 1" is a
    // prefix of "...NUMBER 12", and the first version of this counted every
    // topic three times and told me the paging was broken when it was not.
    for (const std::string& topic : topics) {
        int seen = 0;
        for (int page = 0; page < topicPageCount(topics.size()); ++page) {
            for (const TopicRow& row : topicRowsFor(topics, page, -1, capacity)) {
                const std::size_t space = row.label.find(' ');
                if (space != std::string::npos && row.label.substr(space + 1) == topic) {
                    ++seen;
                }
            }
        }
        INFO("topic ", topic);
        REQUIRE(seen == 1);
    }

    // A single page of nine has no MORE row at all.
    const std::vector<std::string> few(topics.begin(), topics.begin() + kTopicPageSize);
    const std::vector<TopicRow> only = topicRowsFor(few, 0, 0, capacity);
    CHECK(only.size() == static_cast<std::size_t>(kTopicPageSize));
}

TEST_CASE("a topic label stops short of the next column's key") {
    // S4's own headline frame shows topic 5 reading "ASK TO BE MADE SHE" with
    // the next column's "9" jammed against the E. A row is drawn at x + half a
    // glyph and the next column's cursor arrow at x - half a glyph, so a label
    // sized to the whole column overruns it by a glyph and collides with the
    // arrow after it. Two glyphs back, and the arithmetic is pinned here.
    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        const int scale = std::max(1, height / 180);
        const int margin = 5 * scale;
        const int glyphAdvance = 5 * scale;
        const int columnWidth = (width - 2 * margin) / kTopicColumns;
        const int room = std::max(1, columnWidth / glyphAdvance - 2);
        const int labelEnds = glyphAdvance / 2 + room * glyphAdvance;
        const int nextArrowStarts = columnWidth - glyphAdvance / 2;
        INFO("height ", height, " label ends ", labelEnds, " arrow at ", nextArrowStarts);
        REQUIRE(labelEnds <= nextArrowStarts);
    }
}

TEST_CASE("a long topic list draws its page, says there is more, and keeps the centre clear") {
    DialogueViewState view;
    view.open = true;
    view.speaker = "MASTER VENN";
    view.epithet = "LANDLORD OF THE GILDED GULL";
    view.attitude = "WARM";
    view.line = "A BED IS TWELVE AND IT COMES WITH THE DOOR BOLTED.";
    // Twenty topics: three pages, and more than the grid can hold at once.
    for (int i = 0; i < 20; ++i) {
        view.topics.push_back("ASK ABOUT SOMETHING NUMBER " + std::to_string(i + 1));
    }

    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        view.page = 0;
        view.cursor = 0;
        CHECK(centreUntouched(view, width, height));
        const std::size_t firstInk = inkOf(view, width, height);
        CHECK(firstInk > 0);

        view.page = 2;
        view.cursor = 2 * kTopicPageSize;
        CHECK(centreUntouched(view, width, height));
        CHECK(inkOf(view, width, height) > 0);
    }

    // Two different pages of the same list are two different pictures, so the
    // MORE key is doing something a player can see.
    Framebuffer first(640, 360);
    first.clear(Rgb{0.20F, 0.18F, 0.16F});
    view.page = 0;
    view.cursor = 0;
    drawDialogue(first, view);
    Framebuffer second(640, 360);
    second.clear(Rgb{0.20F, 0.18F, 0.16F});
    view.page = 1;
    view.cursor = kTopicPageSize;
    drawDialogue(second, view);
    bool differs = false;
    for (std::size_t i = 0; i < first.pixels().size(); ++i) {
        if (first.pixels()[i] != second.pixels()[i]) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("the workbench draws where a conversation is allowed to be") {
    DialogueViewState view;
    view.open = true;
    view.speaker = "FATHER MAELL";
    view.epithet = "OF THE MISSION";
    view.attitude = "FRIEND";
    view.line = "THE BOOKS STOP WHERE THE MONEY STOPPED. AFTER THAT YOU ARE COMPOSING.";
    view.forging = true;
    view.forgeCursor = 2;
    view.forgeDifficulty = 9;
    view.forgeCeiling = 21;
    view.forgeFields = {"MOVES: VITALITY", "SHAPE: INSTANT", "HOW MUCH: -2", "HOW LONG: 0T",
                        "ACROSS: TOUCH"};

    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        CHECK(centreUntouched(view, width, height));
        CHECK(inkOf(view, width, height) > 0);
    }

    // A refused composition says so on the bench, before the priest has to --
    // and saying so is visible. Compared PIXEL BY PIXEL rather than by counting
    // changed pixels: the panel fill already covers every cell the text lands
    // in, so a count is the same number whatever the words are.
    Framebuffer hinted(640, 360);
    hinted.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(hinted, view);
    view.forgeProblem = "A WOUND IS DELIVERED, NOT HELD.";
    Framebuffer complained(640, 360);
    complained.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawDialogue(complained, view);
    CHECK(centreUntouched(view, 640, 360));
    bool saysWhy = false;
    for (std::size_t i = 0; i < hinted.pixels().size(); ++i) {
        if (hinted.pixels()[i] != complained.pixels()[i]) {
            saysWhy = true;
            break;
        }
    }
    CHECK(saysWhy);
}
