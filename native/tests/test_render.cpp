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
#include <utility>
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
    REQUIRE(session.lampCount() == 28);
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
    // THE NEAR BOUND WAS MEASURING A WALL, AND #79 TOOK THE WALL AWAY.
    //
    // `nearestDepth < 3.0` passed from the S2 spawn because that spawn stood in
    // a slot between two warehouses with masonry about a tile from the eye --
    // which is the same fact that made its `seen=` zero and got the shot
    // re-aimed. From an open street the nearest surface in the frame is the
    // PAVING at the bottom edge, and where that lands is arithmetic rather than
    // taste: a 1.70-tile eye over a frame whose lower half subtends 29.4
    // degrees puts the bottom row of pavement 1.70 / tan(29.4) = 3.0 tiles out.
    // The old bound sat exactly on that number, so on an open street it was a
    // coin toss.
    //
    // What the case actually wants to say is that the frame has RANGE -- near
    // geometry and far geometry in one picture, rather than a flat fill -- so
    // it says that, in a form no spawn can make vacuous.
    CHECK(stats.nearestDepth < 5.0F);
    CHECK(stats.furthestDepth > 12.0F);
    CHECK(stats.furthestDepth > 3.0F * stats.nearestDepth);

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

    // Every pixel of geometry has a finite depth, and the sky behind all of it
    // does not.
    //
    // THIS USED TO BE AN EQUALITY AND #79 MADE IT AN INTERVAL, for a reason
    // worth writing down rather than relaxing quietly. `worldPixels` counts
    // what the raycaster wrote -- walls, floors, roofs. Billboards are drawn
    // AFTER it and write depth of their own, so a figure standing against the
    // sky is a finite-depth pixel that is not a world pixel. That is correct:
    // a body has to occlude, and a body silhouetted on the skyline is the shape
    // of a person and not a hole in the sky.
    //
    // The equality only held before because the old spawn was walled in and
    // drew NOBODY -- `seen=0` at eight in the morning, which is the defect #79
    // exists to fix. The first frame after the re-aim put fifteen pixels of
    // dockhand against the sky and turned this red. So the claim is now stated
    // as what it always meant: no finite pixel that is neither.
    std::size_t finite = 0;
    for (const float d : frame.depth()) {
        if (std::isfinite(d)) {
            ++finite;
        }
    }
    CHECK(finite >= stats.worldPixels);
    CHECK(finite <= stats.worldPixels + stats.spritePixels);
    CHECK(finite + stats.skyPixels >= frame.pixels().size());

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

TEST_CASE("the skyline backdrop stands in the southern sky and the night swallows it") {
    // DISTRICT PHASE A, settled: the owner picked the stepped silhouette, the
    // losing variants and the GRANADAD_SKYLINE selector are gone, and the
    // compositor always paints the one skyline. The Inner Wall and the palace
    // are "never maps, only backdrops" (Gazetteer section 1), so they are
    // paint in the sky band -- and paint can be asserted on: south has it,
    // seaward never does, night swallows it whole.
    //
    // The camera floats high over the ward on purpose. Up there the sky is
    // unoccluded AND the z-window clips the whole district out of the frame,
    // so every frame below is ALL sky (pinned via FrameStats) and every
    // comparison is sky against sky. Street-level occlusion needs no selector
    // to prove: the backdrop writes sky pixels only and the world pass
    // unconditionally overwrites the pixels it draws geometry into, so any
    // frame with skyPixels == 0 provably carries zero backdrop pixels.
    //
    // THE CAMERA WENT UP TWO BANDS IN DISTRICT PHASE B, AND THE REASON IS THE
    // POINT OF THAT PASS. This stood at bandSurface(26), which with the default
    // levelsBelow of 4 puts the z-window's floor at world z22 -- and that was
    // all sky only because the Docks had nothing that tall within
    // RenderSettings::maxDistance of the spawn. Phase B gave the Mission of the
    // Flame a lantern-turret whose crown reaches world z23, fifty-two tiles from
    // the spawn, and 477 pixels of it walked into this frame and turned the
    // REQUIRE below red. That is the deliverable arriving, not a regression: the
    // ward is supposed to have a landmark you can see across it now.
    //
    // So the camera goes to bandSurface(28), where the window's floor is z24.
    // World z23 is the LAST interior band this format has (a 4-chunk-deep world
    // with a one-chunk VOID border), so from up here the window is provably
    // above everything anything can ever author into this map -- a structural
    // guarantee rather than a standing bet on the ward staying short.
    Session session(docksAt(12));
    Camera aloft = session.camera();
    aloft.z = bandSurface(28);
    aloft.pitch = 0.0F;

    constexpr float kSouth = 3.14159265F;
    const auto pixelsAt = [&](float yaw, int hour) {
        RenderSettings settings;
        settings.timeOfDay = hour * 3600;
        settings.drawSprites = false;
        Camera view = aloft;
        view.yaw = yaw;
        Framebuffer frame(320, 180);
        const FrameStats stats = session.renderer().renderFrame(frame, view, settings, {});
        // Sky against sky, or every comparison below is comparing geometry.
        REQUIRE(stats.skyPixels == static_cast<std::size_t>(320 * 180));
        return frame.pixels();
    };
    const auto differing = [](const std::vector<std::uint32_t>& a,
                              const std::vector<std::uint32_t>& b) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                ++count;
            }
        }
        return count;
    };

    // Facing south at noon the silhouette is present. The bare sky gradient is
    // a function of the screen ROW alone -- no yaw term anywhere in it -- so a
    // southern all-sky frame can differ from a seaward one only where the
    // backdrop stands, and it does, substantially.
    const std::vector<std::uint32_t> seaward = pixelsAt(0.0F, 12);
    const std::vector<std::uint32_t> southern = pixelsAt(kSouth, 12);
    CHECK(differing(seaward, southern) > 200U);

    // It is a MASS, not a glow: everything it touches gets darker.
    double seawardSum = 0.0;
    double southernSum = 0.0;
    for (std::size_t i = 0; i < seaward.size(); ++i) {
        const Rgb before = unpackRgb(seaward[i]);
        const Rgb after = unpackRgb(southern[i]);
        seawardSum += static_cast<double>(0.2126F * before.r + 0.7152F * before.g +
                                          0.0722F * before.b);
        southernSum += static_cast<double>(0.2126F * after.r + 0.7152F * after.g +
                                           0.0722F * after.b);
    }
    CHECK(southernSum < seawardSum);

    // Seaward is EMPTY, per the geography the table was authored from. Two
    // different seaward yaws whose whole frustums sit inside the unauthored
    // arc (hfovTan 1: +/-45 degrees around each) render the same frame -- a
    // silhouette pans with yaw while the row-only gradient cannot, so any
    // authored mass over the harbour would force these apart.
    CHECK(differing(pixelsAt(-0.25F, 12), pixelsAt(0.25F, 12)) == 0U);

    // And at midnight daylight is exactly 0 (the day-curve case above pins
    // it), the backdrop's alpha is daylight times the fog term, and the
    // closed-in fog swallows the mass entirely: the southern sky and the
    // seaward sky become the same frame.
    CHECK(differing(pixelsAt(0.0F, 0), pixelsAt(kSouth, 0)) == 0U);
}

TEST_CASE("the two harbour beacons carry through the night fog") {
    // DISTRICT PHASE A. The Weighhouse signal mast and the Mission's doctrinal
    // night lamp are tagged by name in lampSprites, and a tagged glow takes a
    // reduced fog wash after dark -- a lighthouse behaviour, the honest way a
    // distant lamp stays a point of light in weather that has already eaten
    // the wall it hangs on.
    Session session(docksAt(0));

    // Exactly the two authored beacons, and exactly by name.
    std::size_t beacons = 0;
    for (const SpriteInstance& sprite : session.renderer().lampSprites(0.0F)) {
        if (sprite.beacon) {
            ++beacons;
        }
    }
    CHECK(beacons == 2U);

    // The same glow, the same spot, thirty tiles out at midnight, once tagged
    // and once not: the beacon's point survives brighter. The camera floats
    // above the z-window so both frames are the sprite against open sky.
    Camera aloft = session.camera();
    aloft.z = bandSurface(26);
    aloft.yaw = 0.0F;
    aloft.pitch = 0.0F;

    RenderSettings settings;
    settings.timeOfDay = 0;
    // No skyline switch exists any more, and none is needed here: at midnight
    // daylight is 0 so the backdrop's alpha is exactly 0, and the camera faces
    // north into the unauthored seaward arc besides. The sky behind the sprite
    // is bare gradient twice over.

    SpriteInstance glow;
    glow.x = aloft.x;
    glow.y = aloft.y - 30.0F;
    glow.z = aloft.z;
    glow.halfWidth = 0.6F;
    glow.halfHeight = 0.6F;
    glow.colour = Rgb{1.0F, 0.88F, 0.66F};
    glow.glow = 1.0F;

    const auto centreLuma = [&](bool beacon) {
        SpriteInstance sprite = glow;
        sprite.beacon = beacon;
        Framebuffer frame(320, 180);
        const FrameStats stats =
            session.renderer().renderFrame(frame, aloft, settings, {sprite});
        REQUIRE(stats.spritePixels > 0);
        const Rgb centre = unpackRgb(frame.pixels()[frame.index(160, 90)]);
        return 0.2126F * centre.r + 0.7152F * centre.g + 0.0722F * centre.b;
    };

    const float carried = centreLuma(true);
    const float drowned = centreLuma(false);
    CHECK(carried > drowned * 1.5F);

    // At NOON the exception does not exist: a beacon is an ordinary lamp in
    // ordinary daylight, so the two draws are pixel-identical.
    settings.timeOfDay = 12 * 3600;
    SpriteInstance tagged = glow;
    tagged.beacon = true;
    SpriteInstance plain = glow;
    Framebuffer dayTagged(320, 180);
    Framebuffer dayPlain(320, 180);
    session.renderer().renderFrame(dayTagged, aloft, settings, {tagged});
    session.renderer().renderFrame(dayPlain, aloft, settings, {plain});
    CHECK(dayTagged.pixels() == dayPlain.pixels());
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

TEST_CASE("every HUD row lit at once still leaves the centre clear") {
    // THE S10 GAP, CLOSED, AND IT WAS REAL. hud.hpp carried a VERIFICATION GAP
    // saying the guild row (y - 24*scale) and the objective row (y - 32*scale)
    // crossed the exclusion rectangle at 320x180 and 640x360 whenever they were
    // non-empty, and the top-right stack ran six rows deep at nine scale units
    // each into a rectangle that starts at 39 pixels down. Both were true. The
    // reason no case caught either is in the two cases above this one: the only
    // HudStates ever drawn in this suite set four or five fields, and the
    // defect needs eleven.
    //
    // So this one lights EVERY field the struct has, with the longest string
    // each is clipped to in session.cpp, at all three resolutions the game is
    // captured at. Rows are allocated out of a slot grid now and a row with no
    // legal slot is dropped rather than drawn, which is what makes it pass.
    HudState full;
    full.health = 61;
    full.healthMax = 100;
    // FATIGUE BUILD: the wind bar lit too, mid-pool, so the one element this
    // build adds is inside the every-field proof from the day it ships.
    full.fatigue = 80;
    full.fatigueMax = 160;
    full.yawBam = sim::kFacingWest;
    full.timeOfDaySeconds = 21 * 3600 + 47 * 60;
    full.coin = 99999;
    full.standingLabel = "THE WARD WANTS YOU GONE";
    full.heatLabel = "CONDEMNED  WANTED  HEAT 84  LOOT 12  BALE";
    full.stashLabel = "3 FLOWER  2 WIRE  4 DUST  240DR";
    // S13: the two rows the Cast/Block task added, at their own longest.
    full.spellLabel = "CAST  SET THE SHOULDERS (599S)";
    full.blockLabel = "GUARD UP";
    // HELD-EFFECTS BUILD: all four live-hold rows at once, at their own
    // longest -- the fullest top-right stack this game can produce.
    full.effectLabels = {"SET THE SHOULDERS 899S", "STEADY THE HAND 899S",
                         "CLEAR THE HEAD 899S", "WARM THE HANDS 599S"};
    // SPELLS BUILD: the quick bar strip at full strength -- every slot
    // loaded, the longest authored name selected, equipped and selected on
    // different cells so both marks draw.
    full.quickSlots = {"STING",  "SCALD", "SET THE SHOULDERS", "WARD THE COLD",
                       "A CRAFTING OF YOUR OWN", "STING", "SCALD", "STING",
                       "SCALD",  "STING"};
    full.quickSelected = 4;
    full.quickEquipped = 2;
    full.quickBarFade = 1.0F;
    full.stealthLabel = "SEEN CROUCH  LIT 88  LOUD";
    full.caseLabel = "CASE 4/6 > THE DROWNED HOLD  EMPTYING";
    full.guildLabel = "THE SKYRUNNERS - THE WARD'S OWN SHADOW";
    full.objectiveLabel = "TAKE THE BALE PAST THE WEIGHHOUSE";
    full.rivalLabel = "RIVAL BRAM MARROW - CRAFTLORD x7  HUNTING";
    full.lockLabel = "LOCK  PINS ***--  DEPTH ....+....  PICKS 2";
    full.roomLabel = "THE GULL  14 IN  LOUD";
    full.alert = "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE, OR I PUT YOU OUT.";
    // DISTRICT PHASE D: the threshold plate, at the longest name docks.hpp's
    // kPlaces can produce and at full strength, sitting at the LOWEST point
    // its own drift can put it -- the one instant it comes closest to the
    // exclusion rectangle. It is a centred element in a HUD whose whole rule
    // is that the middle stays empty, so it belongs in the case that lights
    // everything at once rather than only in its own file.
    full.placePlate = "THE GILDED GULL - ROOMS";
    full.placePlateFade = 1.0F;
    full.placePlateDrift = -1.0F;
    // THE CROSSHAIR PASS: aimVerb/aimSubject/aimNote ARE THE ONE GROUP OF
    // FIELDS THIS CASE DELIBERATELY LEAVES DARK, and that is the point rather
    // than an omission. They are the documented exemption from the rule this
    // case exists to prove (hud.hpp's own header, and the owner's own
    // sentence), so lighting them here would turn a hard zero into a
    // negotiation. With the aim prompt down, drawHud() must still put ZERO
    // pixels in the play space with all sixteen other rows lit -- exactly the
    // guarantee this case has always made -- and the next case is what holds
    // the aim prompt itself to its own fence.

    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}}) {
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, full);

        const CentreRect centre = hudCentreRect(width, height);
        std::size_t trespass = 0;
        for (int y = centre.y0; y < centre.y1; ++y) {
            for (int x = centre.x0; x < centre.x1; ++x) {
                trespass +=
                    bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)] ? 1U
                                                                                            : 0U;
            }
        }
        INFO("at ", width, "x", height, " the HUD put ", trespass,
             " pixels inside the play space");
        CHECK(trespass == 0);

        // And it is not vacuous: with sixteen rows asked for, all four edges
        // drew something.
        const auto changedIn = [&](int x0, int y0, int x1, int y1) {
            std::size_t changed = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    changed += bare.pixels()[bare.index(x, y)] !=
                                       dressed.pixels()[dressed.index(x, y)]
                                   ? 1U
                                   : 0U;
                }
            }
            return changed;
        };
        CHECK(changedIn(0, 0, width, centre.y0) > 200);
        CHECK(changedIn(0, centre.y1, width, height) > 200);
    }
}

TEST_CASE("a bottom-band or top-right label does not run off the frame at an off-16:9 window") {
    // THE SAME CLASS OF DEFECT the chargen sheet's clipLabel() had -- a hard
    // cut with no usable boundary, invisible to a suite that only ever
    // exercised the one shape that happened to survive it. session.cpp's
    // guildLine()/heatLine()/rivalLine() (and stashLine/stealthLine/
    // standingLabel/objectiveLine/caseLine alongside them) clip their own
    // text to a GUESSED CHARACTER COUNT -- 34, 34, 44 -- sized to fit the
    // three 16:9 captures this suite has always rendered at (320x180,
    // 640x360, 960x540). drawTopRight/drawBottomBand draw every one of those
    // strings straight off that guess with no clipToWidth of their own, the
    // way roomLabel/lockLabel/alert already have: "a line anchored right or
    // left is clamped by its anchor" (hud.hpp's own clipToWidth doc) is only
    // true if whoever pre-clipped it actually knew the frame's real pixel
    // width, and session.cpp's guess only agrees with the real one at 16:9.
    //
    // --width and --height are independent CLI flags (main.cpp: each is only
    // floored at 64, neither is derived from the other), so a window that is
    // not 16:9 is a supported configuration this suite never exercised.
    //
    // At 220x640 the budget these rows should have -- width - 2*margin -- is
    // well under what a 34-glyph label needs at this height's minor scale.
    // Unclipped, a LEFT-anchored row (guildLabel) runs off the RIGHT edge
    // with no mark that anything was cut; a RIGHT-anchored one (rivalLabel,
    // and every row of the top-right stack) starts its draw at a negative x
    // and runs off the LEFT edge instead -- losing the FRONT of the line,
    // which is the higher-priority half of every one of these (heatLabel's
    // "CONDEMNED  WANTED" reads before "HEAT 84").
    const int width = 220;
    const int height = 640;
    const std::string longLabel(34, 'A');

    const int scale = hudScale(height);
    const int minor = hudMinorScale(height);
    const int margin = 6 * scale;
    // The premise: this label genuinely does not fit the row's real budget,
    // or every check below is vacuous.
    REQUIRE(textWidth(longLabel, minor) > width - 2 * margin);

    const auto rightmostInk = [&](const Framebuffer& bare, const Framebuffer& dressed) {
        int rightmost = -1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    rightmost = std::max(rightmost, x);
                }
            }
        }
        return rightmost;
    };
    const auto leftmostInk = [&](const Framebuffer& bare, const Framebuffer& dressed) {
        int leftmost = width;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    leftmost = std::min(leftmost, x);
                }
            }
        }
        return leftmost;
    };

    SUBCASE("left-anchored: guildLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.guildLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int rightmost = rightmostInk(bare, dressed);
        REQUIRE(rightmost >= 0);  // it drew, so the check below is not vacuous
        INFO("rightmost ink at x=", rightmost, " of width ", width, " (margin ", margin, ")");
        CHECK(rightmost < width - margin);
    }

    SUBCASE("right-anchored, bottom band: rivalLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.rivalLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int leftmost = leftmostInk(bare, dressed);
        REQUIRE(leftmost < width);  // it drew, so the check below is not vacuous
        INFO("leftmost ink at x=", leftmost, " of width ", width, " (margin ", margin, ")");
        CHECK(leftmost >= margin);
    }

    SUBCASE("right-anchored, top-right stack: heatLabel") {
        HudState state;
        state.showHealth = false;
        state.showCompass = false;
        state.heatLabel = longLabel;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, state);

        const int leftmost = leftmostInk(bare, dressed);
        REQUIRE(leftmost < width);
        INFO("leftmost ink at x=", leftmost, " of width ", width, " (margin ", margin, ")");
        CHECK(leftmost >= margin);
    }
}

TEST_CASE("the HUD costs a fraction of the frame, and the fraction is pinned") {
    // "I love the vibe of the UI but just be more careful with real estate."
    //
    // The vibe is not testable and is not being tested. What the owner was
    // looking at IS: eleven rows of 4x6 glyphs all drawn at the same size, a
    // build banner in the top-left of every screenshot, and a two-row block in
    // the top-centre. On the real scenes, measured with `--nohud` as the
    // baseline, the interface went from 5.80% of a street frame to 2.91% and
    // from 6.89% of a rooftop frame to 4.11%.
    //
    // WITHOUT A CEILING WRITTEN DOWN, the twelfth row costs nothing to add and
    // nobody notices until it is the fourteenth. This is that ceiling: ink over
    // a fully lit HUD -- every field this struct has, at once, which no real
    // frame ever shows -- counted the same way --nohud counts it. It measures
    // 5.78% and the bar is at 6.5%, which leaves room for a row and not for
    // four.
    //
    // It is INK and not claimed area on purpose: ink is a diff and cannot be
    // argued with. The claimed figure -- rows closed up into boxes, which is
    // the space you actually lose -- went 8.19% to 5.13% on the street frame
    // and is in docs/HUD-REAL-ESTATE.md with the commands that produce it.
    HudState full;
    full.health = 100;
    full.yawBam = sim::kFacingWest;
    full.timeOfDaySeconds = 20 * 3600;
    full.coin = 4071;
    full.standingLabel = "THE WARD WANTS YOU GONE";
    full.heatLabel = "WANTED  HEAT 84  LOOT 12";
    full.stashLabel = "3 FLOWER  240DR";
    full.stealthLabel = "HIDDEN CROUCH  DARK 4  QUIET";
    full.caseLabel = "CASE 4/6 > THE DROWNED HOLD";
    full.guildLabel = "THE SKYRUNNERS - SKYRUNNER";
    full.objectiveLabel = "TAKE THE BALE PAST THE WEIGHHOUSE";
    full.rivalLabel = "RIVAL BRAM MARROW - CRAFTLORD x7";
    full.roomLabel = "THE GULL  14 IN  LOUD";

    Framebuffer bare(960, 540);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(960, 540);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(dressed, full);
    std::size_t ink = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        ink += bare.pixels()[i] != dressed.pixels()[i] ? 1U : 0U;
    }
    const double fraction = static_cast<double>(ink) / static_cast<double>(bare.pixels().size());
    MESSAGE("a fully lit HUD at 960x540 is " << ink << " pixels, " << fraction * 100.0
                                             << "% of the frame");
    CHECK(ink > 2000);         // it drew, so the ceiling is not vacuous
    CHECK(fraction < 0.065);   // and every row it has fits in a sixteenth of the screen
}

TEST_CASE("the HUD's health bar tracks the number it is given") {
    // HARDENING PASS. THE BAR IS NO LONGER ONE FIXED RED, so "count the red
    // pixels" stopped being a legal way to measure how many segments are lit
    // -- a full bar is GREEN now, and a hue check tuned for the old constant
    // colour would just read a fully healthy player as an empty bar. Counted
    // by SATURATION instead (max channel minus min channel): every stop of
    // the health gradient (green, amber, red) is well clear of the muted
    // bronze frame border and the near-black backing plate, which is what a
    // segment count needs to stay a fact about how many segments are FILLED,
    // not about which colour they happen to be filled with.
    //
    // SCOPED TO THE BAR'S OWN RECTANGLE, NOT THE WHOLE FRAME -- a whole-frame
    // scan also caught the compass ribbon's fixed north mark (a small,
    // equally saturated yellow tick drawn regardless of health), which is
    // exactly why the ORIGINAL red-only filter looked safe: red hue happened
    // to exclude that mark by accident, not by scoping. Health is default
    // HudState's only non-empty field here, so nothing else in the bar's own
    // bottom-left corner can light up.
    const auto litPixels = [](int health) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.health = health;
        drawHud(frame, hud);
        const int scale = std::max(1, frame.height() / 180);
        const int barX0 = 6 * scale - scale;               // band.margin() - scale (frame ring)
        const int barY0 = frame.height() - 12 * scale - scale;
        const int barX1 = 6 * scale + 48 * scale + scale;   // margin + barWidth + scale
        const int barY1 = frame.height() - 6 * scale + scale;
        std::size_t lit = 0;
        for (int y = barY0; y < barY1; ++y) {
            for (int x = barX0; x < barX1; ++x) {
                const Rgb colour = unpackRgb(frame.pixels()[frame.index(x, y)]);
                const float lo = std::min({colour.r, colour.g, colour.b});
                const float hi = std::max({colour.r, colour.g, colour.b});
                if (hi - lo > 0.3F) {
                    ++lit;
                }
            }
        }
        return lit;
    };
    CHECK(litPixels(100) > litPixels(50));
    CHECK(litPixels(50) > litPixels(10));
    CHECK(litPixels(0) == 0);
}

TEST_CASE("the health bar's own colour answers danger at a glance, not just the segment count") {
    // TASK #4 OF THE HARDENING BRIEF: full health and near-death used to
    // render identically -- a fixed red bar regardless of the number behind
    // it. This is the glance test: sample the leftmost filled segment (the
    // one pixel every fill level from 1 to 100 lights) and check its own
    // colour, not merely whether it is lit.
    const auto firstSegmentColour = [](int health) {
        Framebuffer frame(320, 180);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        HudState hud;
        hud.health = health;
        drawHud(frame, hud);
        // The bar's own top-left corner interior, one pixel in from the
        // frame border drawHealth() paints -- see its own header on the
        // margin()/scale() geometry this mirrors. Always inside segment 0
        // for any health > 0, at every resolution hudScale() can produce.
        const int scale = std::max(1, frame.height() / 180);
        const int x = 6 * scale + 1;
        const int y = frame.height() - 6 * scale - 6 * scale + 1;
        return unpackRgb(frame.pixels()[frame.index(x, y)]);
    };
    const Rgb full = firstSegmentColour(100);
    const Rgb dying = firstSegmentColour(4);
    INFO("full: (", full.r, ", ", full.g, ", ", full.b, ")  dying: (", dying.r, ", ", dying.g,
         ", ", dying.b, ")");
    // Full health reads GREENER than red (g outweighs r); near death reads
    // the other way round -- the near-universal genre convention the brief
    // asks for, checkable without pinning the exact gradient stops.
    CHECK(full.g > full.r);
    CHECK(dying.r > dying.g);
    // And the two are not the same colour -- the gradient actually moves.
    CHECK((full.r != dying.r || full.g != dying.g || full.b != dying.b));
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
    CHECK(result.lampCount == 28);
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
    //
    // IT STARTS FROM ITS OWN TILE AND NOT FROM THE SPAWN (#79). This is a claim
    // about the renderer; hanging it off kSpawnTileY made it a claim about the
    // opening shot too, so re-aiming the opening shot would have turned it red
    // for a reason that has nothing to do with water. docks::kQuayApproach is
    // the open apron this walk needs, named where the spawn is named.
    SessionConfig config = docksAt(20);
    config.spawnX = sim::docks::kQuayApproachX;
    config.spawnY = sim::docks::kQuayApproachY;
    config.spawnBand = sim::docks::kBandQuayside;
    Session session(config);
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
    CHECK(session.body().tileY() < sim::docks::kQuayApproachY);
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

TEST_CASE("the topic band is as deep as the list it holds and no deeper") {
    // polish-1. The bottom band claimed the deepest it could legally go
    // whichever speaker was in front of you, so a doorman with two things to
    // say took the same fifth of the frame as Master Venn with twelve. The top
    // band has been sized from what it draws since S3; this one never was.
    const auto bandTop = [](int topics) {
        DialogueViewState view;
        view.open = true;
        view.speaker = "GERTA";
        view.line = "WHAT.";
        for (int i = 0; i < topics; ++i) {
            view.topics.push_back("A THING " + std::to_string(i + 1));
        }
        Framebuffer bare(960, 540);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(960, 540);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawDialogue(dressed, view);
        const CentreRect centre = hudCentreRect(960, 540);
        for (int y = centre.y1; y < 540; ++y) {
            for (int x = 0; x < 960; ++x) {
                if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                    return y;
                }
            }
        }
        return 540;
    };
    const int shortList = bandTop(2);
    const int fullList = bandTop(20);
    INFO("two topics start the band at ", shortList, ", twenty at ", fullList);
    CHECK(shortList > fullList);
    // And the deep one still stops at the rectangle rather than inside it.
    CHECK(fullList >= hudCentreRect(960, 540).y1);
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

// ===========================================================================
// THE CROSSHAIR PASS -- the one element allowed in the middle, fenced
// ===========================================================================

namespace {

/// Every pixel drawHud() changed over a flat field, as a list of coordinates.
[[nodiscard]] std::vector<std::pair<int, int>> hudInk(const HudState& state, int width,
                                                      int height) {
    Framebuffer bare(width, height);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(width, height);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(dressed, state);
    std::vector<std::pair<int, int>> out;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]) {
                out.emplace_back(x, y);
            }
        }
    }
    return out;
}

/// The aim prompt at its loudest, with the longest strings session.cpp can
/// actually hand it: the casebook's longest authored `what` and a ward trade.
[[nodiscard]] HudState loudAim() {
    HudState aim;
    aim.showHealth = false;
    aim.showCompass = false;
    aim.timeOfDaySeconds = -1;
    aim.coin = -1;
    aim.aimKey = "E";
    aim.aimVerb = "PICKPOCKET";
    aim.aimSubject = "THE BODY, AND WHOEVER FOUND IT";
    aim.aimNote = "ALREADY READ";
    aim.aimKind = static_cast<int>(AimKind::Clue);
    return aim;
}

}  // namespace

TEST_CASE("the aim prompt is the only thing in the play space, and it stays in its fence") {
    // THE OWNER'S OWN INSTRUCTION, HELD TO A RECTANGLE. "when shown hover a
    // bit to the top-right of the center crosshair" is a direct order to break
    // the centre-clear rule, so it is broken in exactly one place and that
    // place is a clamp rather than a comment -- see hudAimRect().
    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{640, 360},
                                        std::pair{960, 540}, std::pair{1920, 1080}}) {
        const HudState aim = loudAim();
        const CentreRect fence = hudAimRect(width, height);
        const std::vector<std::pair<int, int>> lit = hudInk(aim, width, height);

        INFO("at ", width, "x", height);
        // NOT VACUOUS: it drew something.
        CHECK(lit.size() > 200);
        std::size_t escaped = 0;
        for (const auto& [x, y] : lit) {
            if (x < fence.x0 || x >= fence.x1 || y < fence.y0 || y >= fence.y1) {
                ++escaped;
            }
        }
        CHECK(escaped == 0);

        // UP AND TO THE RIGHT, LITERALLY. Everything the PROMPT draws is above
        // the horizontal centre line and right of the vertical one; the only
        // ink below or left of centre belongs to the reticle's own two ticks,
        // which is what the prompt is hanging off.
        const int cx = width / 2;
        const int cy = height / 2;
        std::size_t textLeftOfCentre = 0;
        for (const auto& [x, y] : lit) {
            if (y < cy - hudMinorScale(height) * 4 && x < cx) {
                ++textLeftOfCentre;
            }
        }
        CHECK(textLeftOfCentre == 0);

        // AND THE EXACT AIM PIXEL IS NEVER PAINTED. A reticle that covered the
        // thing it points at would be a worse crosshair than none.
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});
        drawHud(dressed, aim);
        CHECK(bare.pixels()[bare.index(cx, cy)] == dressed.pixels()[dressed.index(cx, cy)]);
    }
}

TEST_CASE("the verb row holds its place when a subject arrives and when it goes") {
    // THE REFERENCE'S "PANES HOLD THEIR HEIGHT", ON THE SMALLEST SURFACE IN
    // THE GAME. Sweeping the crosshair across a doorway changes what the
    // subject row says and whether there IS one; if that moved the verb, the
    // one row a player reads every second of play would jitter under their
    // eye. So the subject grows UPWARD off a verb row that never moves.
    constexpr int kWidth = 960;
    constexpr int kHeight = 540;

    HudState bare = loudAim();
    bare.aimSubject = {};
    bare.aimNote = {};
    bare.aimKind = static_cast<int>(AimKind::Nothing);

    const HudState named = loudAim();

    const auto lowestRow = [](const std::vector<std::pair<int, int>>& lit, int cy) {
        // The lowest text row: the ticks live within a few units of centre, so
        // anything above them is prompt.
        int top = 1 << 30;
        for (const auto& [x, y] : lit) {
            (void)x;
            if (y < cy - 8 && y < top) {
                top = y;
            }
        }
        return top;
    };

    const std::vector<std::pair<int, int>> withoutSubject = hudInk(bare, kWidth, kHeight);
    const std::vector<std::pair<int, int>> withSubject = hudInk(named, kWidth, kHeight);
    CHECK(withSubject.size() > withoutSubject.size());

    // THE VERB ROW'S OWN BAND IS PIXEL-IDENTICAL BETWEEN THE TWO. Compared as
    // a band rather than as a y, because the subject row genuinely does draw
    // above it and would otherwise be counted as movement.
    const CentreRect band = hudAimVerbRow(kWidth, kHeight);
    Framebuffer a(kWidth, kHeight);
    a.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(a, bare);
    Framebuffer b(kWidth, kHeight);
    b.clear(Rgb{0.20F, 0.18F, 0.16F});
    drawHud(b, named);
    // THE BAND'S OWN COLUMNS, NOT THE WHOLE ROW. hudAimVerbRow starts at the
    // prompt's left edge on purpose: the reticle's upper tick shares these
    // pixel rows and it is SUPPOSED to change -- it takes the subject's accent
    // when there is one, which is the reference's "highlight the current
    // interaction target". What must hold still is the verb's own text.
    std::size_t moved = 0;
    for (int y = band.y0; y < band.y1; ++y) {
        for (int x = band.x0; x < band.x1; ++x) {
            moved += a.pixels()[a.index(x, y)] != b.pixels()[b.index(x, y)] ? 1U : 0U;
        }
    }
    CHECK(moved == 0);

    // And the subject really did land ABOVE it rather than pushing it down.
    CHECK(lowestRow(withSubject, kHeight / 2) < lowestRow(withoutSubject, kHeight / 2));
}

TEST_CASE("an empty verb draws no reticle and no prompt at all") {
    // THIS IS WHAT KEEPS THE OLD GUARANTEE TRUE. Session empties the verb for
    // every page in the game (conversingNow), so under a menu, a map or a
    // conversation the middle of the screen is exactly as clear as it was
    // before this pass existed.
    HudState quiet = loudAim();
    quiet.aimVerb = {};
    for (const auto& [width, height] : {std::pair{320, 180}, std::pair{960, 540}}) {
        INFO("at ", width, "x", height);
        CHECK(hudInk(quiet, width, height).empty());
    }
}
