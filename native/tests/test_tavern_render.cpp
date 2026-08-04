// The tavern as the player sees it: the clock, the label, the light, and the
// people in the frame.
//
// The sim suite proves the room WORKS. This one proves you can stand in it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

namespace {

SessionConfig insideTheGull(int hour, std::int32_t x = 156, std::int32_t y = 76,
                            int yawDegrees = 315) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = sim::gull::kGroundBand;
    config.spawnYaw = sim::angle_from_degrees(yawDegrees);
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

TEST_CASE("the HUD names a place, not a z-level") {
    // THE DEFECT, from the S1 review: bandLabel() took the band and nothing
    // else, so standing at (200,100,z21) -- the far east of the district,
    // nowhere near the street -- the HUD read SALTGATE RISE. A label that
    // asserts a location it does not know is worse than no label.
    SessionConfig config = insideTheGull(20);

    // The spawn is on the Tarwalk, and says so.
    SessionConfig atSpawn = config;
    atSpawn.spawnX = -1;
    atSpawn.spawnY = -1;
    atSpawn.spawnBand = -1;
    CHECK(Session(atSpawn).placeLabel() == "TARWALK");

    // Inside the tavern, it names the tavern.
    CHECK(Session(config).placeLabel() == "THE GILDED GULL");

    // The exact tile the review used. Upper band, and nowhere near the Rise.
    SessionConfig far = config;
    far.spawnX = 200;
    far.spawnY = 100;
    far.spawnBand = sim::docks::kBandUpper;
    const std::string label = Session(far).placeLabel();
    CHECK(label != "SALTGATE RISE");
    CHECK(label == "THE DOCKS - UPPER");

    // ...and on the Rise itself, where the name IS true, it still says it.
    SessionConfig rise = config;
    rise.spawnX = 107;
    rise.spawnY = 70;
    rise.spawnBand = sim::docks::kBandQuayside;
    CHECK(Session(rise).placeLabel() == "SALTGATE RISE");
}

TEST_CASE("every named place is somewhere a body can actually be") {
    // The table is transcribed from the frect() calls that paved the district.
    // A rectangle that turns out to be roofs or water would put a street name
    // on a place with no street in it, which is the defect above with extra
    // steps.
    Session session(insideTheGull(20));
    const sim::TileQuery& tiles = session.tiles();
    for (std::size_t i = 0; i < sim::docks::kPlaceCount; ++i) {
        const sim::docks::Place& place = sim::docks::kPlaces[i];
        std::int32_t standable = 0;
        for (std::int32_t y = place.y0; y <= place.y1; ++y) {
            for (std::int32_t x = place.x0; x <= place.x1; ++x) {
                standable += tiles.standable(x, y, place.band) ? 1 : 0;
            }
        }
        INFO("place ", place.name);
        REQUIRE(standable >= 20);
    }
}

TEST_CASE("the room is lit and full at nine, dark and empty at five") {
    // THE SPRINT'S ACCEPTANCE FRAMES, asserted rather than described. Same
    // camera, same tile, same yaw; only the hour differs.
    Session night(insideTheGull(21));
    Session dawn(insideTheGull(5));

    Framebuffer nightFrame(320, 180);
    Framebuffer dawnFrame(320, 180);
    const FrameStats nightStats = night.drawFrame(nightFrame);
    const FrameStats dawnStats = dawn.drawFrame(dawnFrame);

    // Nine at night: the fire is lit, the candles and lanterns are out, and
    // there are people between the camera and the far wall.
    CHECK(night.tavern().fireLit());
    CHECK(night.tavern().isOpen());
    CHECK(night.tavernLights().size() >= 6);
    CHECK(night.tavern().presentCount() >= 6);
    CHECK_FALSE(night.actorSprites().empty());
    CHECK(nightStats.spritePixels > 0);

    // Five in the morning: the fire is banked, the doors are shut, and there is
    // nobody in the building at all.
    CHECK_FALSE(dawn.tavern().fireLit());
    CHECK_FALSE(dawn.tavern().isOpen());
    CHECK(dawn.tavernLights().empty());
    CHECK(dawn.tavern().presentCount() == 0);
    CHECK(dawn.actorSprites().empty());

    // And the two frames are genuinely different pictures: brighter, more
    // colours, and sprite pixels where there were none.
    CHECK(nightStats.meanLuma > dawnStats.meanLuma * 1.5F);
    CHECK(nightStats.distinctColours > dawnStats.distinctColours);
    CHECK(dawnStats.spritePixels == 0);

    // Neither is a picture of the outdoors: standing in a room means the walls
    // fill the frame.
    CHECK(nightStats.skyPixels == 0);
    CHECK(dawnStats.skyPixels == 0);
}

TEST_CASE("an actor is drawn where the simulation says the actor is") {
    // The renderer reads the Q8 the simulation owns. If a later sprint moves
    // interpolation into the renderer, this is what says so.
    Session session(insideTheGull(21, 152, 68, 180));
    const sim::Actor* bartender = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.role() == sim::ActorRole::Bartender && actor.present()) {
            bartender = &actor;
        }
    }
    REQUIRE(bartender != nullptr);

    const std::vector<SpriteInstance> sprites = session.actorSprites();
    REQUIRE_FALSE(sprites.empty());
    const float wantX = static_cast<float>(bartender->x()) / 256.0F;
    const float wantY = static_cast<float>(bartender->y()) / 256.0F;
    int matching = 0;
    for (const SpriteInstance& sprite : sprites) {
        if (std::abs(sprite.x - wantX) < 0.001F && std::abs(sprite.y - wantY) < 0.001F) {
            ++matching;
        }
    }
    // Three billboards to a standing figure: legs, torso, head.
    CHECK(matching == 3);
    // Every one of them a solid, not a glow -- an actor is not a light source.
    for (const SpriteInstance& sprite : sprites) {
        CHECK(sprite.glow == 0.0F);
    }
}

TEST_CASE("the world keeps its own time while the player stands still") {
    // S1's session was a diorama: the clock was frozen at construction and the
    // only thing that moved was the body.
    Session session(insideTheGull(20));
    const int start = session.timeOfDay();
    CHECK(session.elapsedSeconds() == 0);

    sim::MoveInput still;
    session.stepMany(still, 90 * sim::kStepsPerSecond);

    CHECK(session.elapsedSeconds() == 90);
    CHECK(session.timeOfDay() == start + 90);
    // And the room ran with it: the tavern's own clock is the same clock.
    CHECK(session.tavern().timeOfDay() == session.timeOfDay());
}

TEST_CASE("the tavern empties itself between closing and dawn") {
    // Run the room from one in the morning to five, at a hundred seconds of
    // world per second of movement, and watch it clear out.
    SessionConfig config = insideTheGull(1);
    config.clockScale = 120;
    Session session(config);
    REQUIRE(session.tavern().presentCount() > 0);

    sim::MoveInput still;
    session.stepMany(still, 150 * sim::kStepsPerSecond);  // ~5 simulated hours

    CHECK(session.timeOfDay() / 3600 >= 5);
    CHECK(session.tavern().presentCount() == 0);
    CHECK_FALSE(session.tavern().isOpen());
    CHECK_FALSE(session.tavern().fireLit());
}

TEST_CASE("the S2 HUD still hugs the edges and leaves the centre clear") {
    // Task #66, re-checked with every element S2 added turned ON at once: the
    // clock, the purse, the room line and a bouncer's warning across the
    // bottom. The Java build's first-person view died exactly here.
    Framebuffer bare(320, 180);
    bare.clear(Rgb{0.20F, 0.18F, 0.16F});
    Framebuffer dressed(320, 180);
    dressed.clear(Rgb{0.20F, 0.18F, 0.16F});

    HudState hud;
    hud.health = 61;
    hud.yawBam = sim::kFacingWest;
    hud.locationLabel = "THE GILDED GULL";
    hud.timeOfDaySeconds = 21 * 3600 + 47 * 60;
    hud.coin = 12345;
    hud.roomLabel = "THE GULL  12 IN  LOUD";
    hud.alert = "OX GULLBANE: THAT IS YOUR ONE. OUT OF THIS HOUSE.";
    drawHud(dressed, hud);

    const CentreRect centre = hudCentreRect(320, 180);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(bare.pixels()[bare.index(x, y)] == dressed.pixels()[dressed.index(x, y)]);
        }
    }
    // And every new element actually drew something, or the check is vacuous.
    const auto changedIn = [&](int x0, int y0, int x1, int y1) {
        std::size_t changed = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                changed += bare.pixels()[bare.index(x, y)] != dressed.pixels()[dressed.index(x, y)]
                               ? 1U
                               : 0U;
            }
        }
        return changed;
    };
    CHECK(changedIn(0, 150, 90, 180) > 50);    // health, bottom-left
    CHECK(changedIn(230, 0, 320, 30) > 30);    // clock and purse, top-right
    CHECK(changedIn(180, 160, 320, 180) > 30); // the room line, bottom-right
    CHECK(changedIn(0, 150, 320, 180) > 200);  // the warning, along the bottom
}

TEST_CASE("the three verbs do what the keys say they do") {
    // E, F and R, driven exactly as the client drives them.
    SUBCASE("E buys a drink from the bartender") {
        // Eleven, when the doors have just opened: Gerta is behind the bar and
        // the crowd that would otherwise be the NEAREST person to the player is
        // still on the quay. E does business with whoever is in front of you,
        // and at nine at night that is a docker with an elbow in your ribs.
        Session session(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
        session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
        const std::int32_t purse = session.tavern().playerCoin();
        session.interact();
        CHECK(session.tavern().playerCoin() == purse - sim::kDrinkPrice);
        CHECK(session.tavern().drinksPlayerHasHad() == 1);
        CHECK_FALSE(session.lastMessage().empty());
    }
    SUBCASE("F throws a punch and the house minds") {
        Session session(insideTheGull(19, sim::gull::kBartenderX, sim::gull::kBarY - 1, 0));
        session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
        REQUIRE(session.tavern().playerStanding() == sim::Standing::Welcome);
        session.punch();
        CHECK(session.tavern().playerStanding() == sim::Standing::BeingWarned);
        CHECK_FALSE(session.lastMessage().empty());
    }
    SUBCASE("R refuses when there is no room rented") {
        Session session(insideTheGull(21));
        session.restHere();
        CHECK(session.lastMessage().rfind("CANNOT REST", 0) == 0);
        CHECK(session.timeOfDay() == 21 * 3600);
    }
    SUBCASE("a message fades on its own") {
        Session session(insideTheGull(21));
        session.interact();
        REQUIRE_FALSE(session.lastMessage().empty());
        session.stepMany(sim::MoveInput{}, 7 * sim::kStepsPerSecond);
        CHECK(session.lastMessage().empty());
    }
}
