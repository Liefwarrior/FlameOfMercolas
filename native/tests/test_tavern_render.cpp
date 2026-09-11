// The tavern as the player sees it: the clock, the label, the light, and the
// people in the frame.
//
// The sim suite proves the room WORKS. This one proves you can stand in it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/contract.hpp"
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
    // AND PEOPLE ARE VISIBLE IN IT. The S2 review's fourth finding: this case
    // used to assert spritePixels > 0, and a lit Gull carries nine flame
    // sprites -- so it passed with every human being in the room invisible.
    // actorPixels counts only billboards flagged `person`.
    CHECK(nightStats.actorPixels > 0);
    CHECK(nightStats.actorPixels <= nightStats.spritePixels);

    // Five in the morning: the fire is banked, the doors are shut, and there is
    // nobody in the building at all.
    //
    // NOBODY, not NOTHING -- S6 changed what the empty room contains and this
    // is the case that says so. The rats are on the skirting between one in the
    // morning and eleven, which is the whole reason the ward's bounty is
    // something a player has to keep an hour for. presentCount() counts PEOPLE
    // and reads zero; the frame is not empty, and that is deliberate.
    CHECK_FALSE(dawn.tavern().fireLit());
    CHECK_FALSE(dawn.tavern().isOpen());
    CHECK(dawn.tavernLights().empty());
    CHECK(dawn.tavern().presentCount() == 0);
    CHECK(dawn.tavern().verminPresent() == sim::kVerminPerNight);
    CHECK(dawnStats.actorPixels == 0);

    // And the two frames are genuinely different pictures: brighter, more
    // colours, and sprite pixels where there were none.
    CHECK(nightStats.meanLuma > dawnStats.meanLuma * 1.5F);
    CHECK(nightStats.distinctColours > dawnStats.distinctColours);
    // Sprite pixels at dawn are the rats and nothing else: a third of a
    // person's height, unlit, in a black room. A handful of pixels against the
    // night frame's thousands.
    CHECK(dawnStats.spritePixels < nightStats.spritePixels / 20);

    // Neither is a picture of the outdoors: standing in a room means the walls
    // fill the frame.
    CHECK(nightStats.skyPixels == 0);
    CHECK(dawnStats.skyPixels == 0);
}

TEST_CASE("sprite pixels and people are counted apart") {
    // The other half of the S2 review's fourth finding. It is not enough that
    // actorPixels is non-zero when the room is full; the two counters have to
    // be genuinely different things, or "the room is lit and full at nine" is
    // still a claim that candles can satisfy.
    //
    // So the same camera, the same room, the same hour, drawn twice: once with
    // only the flames in the sprite list, once with the people added.
    //
    // Standing north of the bar looking down the room, because the Gull is
    // full of full-height furniture and from most of its corners you cannot
    // see a flame at all -- which is itself worth knowing and is why this case
    // names its camera instead of taking the default one.
    Session session(insideTheGull(21, 152, 68, 180));
    RenderSettings settings;
    settings.timeOfDay = session.timeOfDay();
    settings.dynamicLamps = session.tavernLights();
    REQUIRE_FALSE(settings.dynamicLamps.empty());

    std::vector<SpriteInstance> flames = session.renderer().lampSprites(0.0F);
    for (const Lamp& light : settings.dynamicLamps) {
        SpriteInstance flame;
        flame.x = static_cast<float>(light.x) + 0.5F;
        flame.y = static_cast<float>(light.y) + 0.5F;
        flame.z = bandSurface(light.z) + 1.20F;
        flame.halfWidth = 0.18F;
        flame.halfHeight = 0.24F;
        flame.colour = Rgb{1.0F, 0.6F, 0.25F};
        flame.glow = 1.0F;
        flames.push_back(flame);
    }
    // Nothing the renderer itself produces is ever a person.
    for (const SpriteInstance& sprite : flames) {
        REQUIRE_FALSE(sprite.person);
    }

    Framebuffer lit(320, 180);
    const FrameStats withoutPeople =
        session.renderer().renderFrame(lit, session.camera(), settings, flames);

    std::vector<SpriteInstance> everything = flames;
    const std::vector<SpriteInstance> people = session.actorSprites();
    REQUIRE_FALSE(people.empty());
    for (const SpriteInstance& sprite : people) {
        REQUIRE(sprite.person);
    }
    everything.insert(everything.end(), people.begin(), people.end());

    Framebuffer full(320, 180);
    const FrameStats withPeople =
        session.renderer().renderFrame(full, session.camera(), settings, everything);

    // A lit room with nobody drawn in it: sprite pixels, and not one of them a
    // person. THIS is the frame the old assertion could not tell apart from a
    // room full of people.
    CHECK(withoutPeople.spritePixels > 0);
    CHECK(withoutPeople.actorPixels == 0);
    // Add the people and only the second counter moves off zero.
    CHECK(withPeople.actorPixels > 0);
    CHECK(withPeople.spritePixels > withoutPeople.spritePixels);
}

TEST_CASE("an actor is drawn where the simulation says the actor is") {
    // The renderer reads the Q8 the simulation owns. If a later sprint moves
    // interpolation into the renderer, this is what says so.
    //
    // S3: THE OLD VERSION OF THIS CASE COULD NOT FAIL. It compared sprite.x
    // against a STATIONARY bartender -- and a stationary actor sits at a tile
    // centre, where x()/256 and tileX + 0.5 are the same float. The S2 review
    // replaced the sub-tile read with `actor.tileX() + 0.5F`, throwing the Q8
    // away outright, and the case stayed green. So the actor below is caught
    // MID-STRIDE, between two tile centres, which is the only state that can
    // tell the two reads apart.
    // Ten seconds before ten at night, which is when the Skyrunner contact
    // comes in off the quay. Nobody already seated ever leaves a tile centre;
    // an ARRIVAL crosses the whole taproom, and that is the state this case
    // needs and the old one never had.
    SessionConfig config = insideTheGull(21, 152, 68, 180);
    config.timeOfDay = 21 * 3600 + 59 * 60 + 50;
    Session session(config);

    const sim::Actor* walker = nullptr;
    std::int32_t walkerX = 0;
    std::int32_t walkerY = 0;
    for (int second = 0; second < 60 && walker == nullptr; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (!actor.present()) {
                continue;
            }
            // Off a tile centre on either axis: mid-stride.
            if (sim::q8_sub(actor.x()) != sim::kSubHalf ||
                sim::q8_sub(actor.y()) != sim::kSubHalf) {
                walker = &actor;
                walkerX = actor.x();
                walkerY = actor.y();
                break;
            }
        }
    }
    INFO("no actor was ever caught between two tiles");
    REQUIRE(walker != nullptr);
    REQUIRE(walker->x() == walkerX);
    REQUIRE(walker->y() == walkerY);

    const float wantX = static_cast<float>(walker->x()) / 256.0F;
    const float wantY = static_cast<float>(walker->y()) / 256.0F;
    const float centreX = static_cast<float>(walker->tileX()) + 0.5F;
    const float centreY = static_cast<float>(walker->tileY()) + 0.5F;
    // The mutation the review used produces exactly these, and they are NOT
    // where the actor is.
    REQUIRE(std::abs(wantX - centreX) + std::abs(wantY - centreY) > 0.02F);

    const std::vector<SpriteInstance> sprites = session.actorSprites();
    REQUIRE_FALSE(sprites.empty());
    int matching = 0;
    int atTileCentre = 0;
    for (const SpriteInstance& sprite : sprites) {
        if (std::abs(sprite.x - wantX) < 0.001F && std::abs(sprite.y - wantY) < 0.001F) {
            ++matching;
        }
        if (std::abs(sprite.x - centreX) < 0.001F && std::abs(sprite.y - centreY) < 0.001F) {
            ++atTileCentre;
        }
    }
    // The body's billboard sits on the body's own position.
    //
    // IT USED TO BE THREE, and the number moving is the whole of what #78 did
    // to this file: an actor was legs, torso and head stacked, and it is now
    // ONE drawn figure out of content/art/sprites. What this case is actually
    // about -- that the renderer reads the Q8 the SIMULATION owns, and that a
    // mutation replacing it with `tileX() + 0.5F` goes red -- is untouched, and
    // that is why the assertion below is on the position and not on the count.
    CHECK(matching >= 1);
    CHECK(atTileCentre == 0);
    // The sub-tile position really is inside its tile and not on its centre.
    CHECK(wantX >= static_cast<float>(walker->tileX()));
    CHECK(wantX < static_cast<float>(walker->tileX()) + 1.0F);
    CHECK(wantY >= static_cast<float>(walker->tileY()));
    CHECK(wantY < static_cast<float>(walker->tileY()) + 1.0F);

    // Every billboard a solid, never a glow -- an actor is not a light source
    // -- and every one of them a drawn FIGURE rather than an ellipse.
    for (const SpriteInstance& sprite : sprites) {
        CHECK(sprite.glow == 0.0F);
        CHECK(sprite.person);
        CHECK(sprite.art != nullptr);
        CHECK(sprite.artSize == 16);
        CHECK(sprite.artU1 >= sprite.artU0);
        CHECK(sprite.artV1 >= sprite.artV0);
    }
}

TEST_CASE("a person is a figure somebody drew, not an egg with a head on it") {
    // #78, AND THE OWNER'S WORDS FOR IT. He captured a conversation frame and
    // said the actor sprites were "plain egg shapes with a round head", which
    // will not read as polished with hundreds of them on screen. This is the
    // claim that replaced them: the taproom and the ward are drawn out of the
    // SAME sheet, so a guard is a guard and a priest is a priest wherever they
    // are standing, and the art is the owner's own rather than three ellipses
    // in three browns.
    Session session(insideTheGull(21, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));

    // The pack really was found. Without this the case below would pass against
    // the procedural fallback, which is the exact failure mode every raws
    // loader in this build has a Dockerfile check for.
    REQUIRE(session.actorSheet().fromAuthoredArt());
    // Twenty-five actor figures are authored; a sheet that lost half of them
    // would still load and would still draw people.
    CHECK(session.actorSheet().spriteCount() >= 25);

    // And the roles that have to be told apart at a glance are told apart by
    // DIFFERENT art, not by a tint of the same shape.
    const ActorSprite& guard = session.actorSheet().forType(sim::WardType::MilitiaWatch, 0);
    const ActorSprite& priest = session.actorSheet().forType(sim::WardType::PriestOfTheFlame, 0);
    const ActorSprite& urchin = session.actorSheet().forType(sim::WardType::Urchin, 0);
    const auto differs = [](const ActorSprite& a, const ActorSprite& b) {
        for (std::size_t i = 0; i < ActorSprite::kTexels; ++i) {
            if (a.texels[i] != b.texels[i]) {
                return true;
            }
        }
        return false;
    };
    CHECK(differs(guard, priest));
    CHECK(differs(guard, urchin));
    CHECK(differs(priest, urchin));

    // Every figure is measured to its own INK. A sprite drawn short inside its
    // sixteen-row cell would otherwise stand its own padding off the pavement.
    for (const sim::WardType type : {sim::WardType::Serf, sim::WardType::MilitiaWatch,
                                     sim::WardType::PriestOfTheFlame, sim::WardType::Urchin,
                                     sim::WardType::Mouse}) {
        const ActorSprite& art = session.actorSheet().forType(type, 0);
        CHECK(art.firstRow <= art.lastRow);
        CHECK(art.lastRow < ActorSprite::kPx);
        CHECK(art.firstCol <= art.lastCol);
    }
}

TEST_CASE("the facing the simulation has been hashing since S2 is finally drawn") {
    // The S2 review's sixth finding: Actor::faceToward computes an eight-point
    // facing, hashInto commits it to world state every tick, and no renderer
    // ever read it. A feature that existed only as data.
    Session session(insideTheGull(21, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
    const sim::Actor* bartender = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.role() == sim::ActorRole::Bartender && actor.present()) {
            bartender = &actor;
        }
    }
    REQUIRE(bartender != nullptr);
    // Behind the bar means facing across it -- north, toward the customer.
    REQUIRE(bartender->facing() == sim::kFacingNorth);

    Camera infront = session.camera();
    infront.x = static_cast<float>(bartender->tileX()) + 0.5F;
    infront.y = static_cast<float>(bartender->tileY()) - 4.0F;
    Camera behind = infront;
    behind.y = static_cast<float>(bartender->tileY()) + 4.0F;

    // #78 CHANGED WHAT THE FACING BUYS, and the change is worth stating.
    //
    // S3 spent it on a pale patch stuck to the side of the head, and this case
    // used to count billboards: one more when the eye was in front of him than
    // behind. content/art/sprites is drawn FRONT-ON and has no back view, and
    // inventing one would be this build authoring art. So the facing is spent
    // on LIGHT instead -- somebody looking your way catches what light there is
    // and somebody turned away is a silhouette -- which is true, and at eight
    // tiles in lamplight it is the one thing about a person you need to read.
    //
    // The claim under test is unchanged: the eight-point facing the simulation
    // has been hashing since S2 reaches the frame. Only what it does there has.
    const auto litnessAt = [&](const Camera& view) {
        float best = 0.0F;
        for (const SpriteInstance& sprite : session.actorSprites(view)) {
            const float dx = sprite.x - (static_cast<float>(bartender->x()) / 256.0F);
            const float dy = sprite.y - (static_cast<float>(bartender->y()) / 256.0F);
            if (std::abs(dx) + std::abs(dy) > 0.01F) {
                continue;  // somebody else
            }
            best = std::max(best, sprite.colour.r + sprite.colour.g + sprite.colour.b);
        }
        return best;
    };
    const float front = litnessAt(infront);
    const float back = litnessAt(behind);
    REQUIRE(front > 0.0F);
    REQUIRE(back > 0.0F);
    // Same actor, same lamps, same hour -- only which way the eye is.
    CHECK(front > back);
    // And the difference is the shade the code asks for and not a rounding
    // error: a mutation that drops the facing term entirely makes these equal.
    CHECK(back < front * 0.9F);
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

TEST_CASE("a scaled clock carries the room's clock with it, hour for hour") {
    // WHAT IS LEFT HERE OF "the tavern empties itself between closing and dawn",
    // and the split is worth stating because the case that used to be here was
    // THIRTY-SIX PER CENT OF THE ENTIRE GATE -- 439 seconds of 1,229.
    //
    // It ran five hours of world at clockScale 120 through a full Session. A
    // Session stands up three systems: the taproom, the compound roll and the
    // ward's six hundred and sixty-one people. Measured, in the Debug build the
    // gate compiles:
    //
    //     tavern tick              5 us
    //     compound roll tick       0 us
    //     ward population tick    31,000 us   (at one in the morning)
    //
    // So 99.98% of that case was six hundred bodies walking the district at
    // night, in a case that read presentCount(), isOpen() and fireLit() and
    // nothing else. Nobody in the ward is spawned inside the Gull and no wire
    // runs from the population to the taproom -- every system draws from its
    // own stream (see PhasedEngine::Registration) -- so the ward could not have
    // changed the answer even in principle.
    //
    // The emptying claim therefore moved, unchanged, onto the Room fixture in
    // tests/test_tavern.cpp: same Tavern, same PhasedEngine, same movement
    // cadence, same five hours, same four assertions. What stays HERE is the
    // half that genuinely needs a Session -- that the scaled clock the capture
    // scripts run on carries the room's clock with it -- and it is asked in
    // seconds instead of hours.
    // EIGHT IN THE MORNING AND NOT ONE, and the hour is a cost and not a
    // scenario. Every engine tick a Session takes is a ward tick, and a ward
    // tick costs what the ward is doing: 1.2 ms at eight, 6 ms at noon, 12 ms
    // at eight in the evening, 31 ms between ten at night and four in the
    // morning (measured, Debug, /600 ticks). A case whose subject is arithmetic
    // on a clock should buy the cheapest hour there is. A case whose subject IS
    // the hour pays for it and says so.
    SessionConfig config = insideTheGull(8);
    config.clockScale = 120;
    Session session(config);
    const int start = session.timeOfDay();

    sim::MoveInput still;
    session.stepMany(still, 2 * sim::kStepsPerSecond);  // two seconds of movement

    // Two seconds of movement, four minutes of world.
    CHECK(session.elapsedSeconds() == 2 * 120);
    CHECK(session.timeOfDay() == start + 2 * 120);
    // And the room is on the same clock, which is the whole claim: a capture
    // that reaches a named hour by scaling must reach it in the taproom too.
    CHECK(session.tavern().timeOfDay() == session.timeOfDay());
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
    SUBCASE("E opens a conversation, and buying is a topic on it") {
        // Eleven, when the doors have just opened: Gerta is behind the bar and
        // the crowd that would otherwise be the NEAREST person to the player is
        // still on the quay.
        //
        // S3 CHANGED WHAT E MEANS. It used to produce one sentence and silently
        // complete a purchase; it now opens a topic list with the purchase on
        // it, beside asking her about the vanished clerk. The coin still moves
        // through the same buyDrink() it always did.
        Session session(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
        session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
        REQUIRE_FALSE(session.talking());
        session.interact();
        REQUIRE(session.talking());
        CHECK_FALSE(session.lastMessage().empty());
        CHECK(session.tavern().dialogue().topics().size() > 4);

        std::size_t buy = session.tavern().dialogue().topics().size();
        for (std::size_t i = 0; i < session.tavern().dialogue().topics().size(); ++i) {
            if (session.tavern().dialogue().topics()[i].kind == sim::TopicKind::Buy) {
                buy = i;
            }
        }
        REQUIRE(buy < session.tavern().dialogue().topics().size());
        const std::int32_t purse = session.tavern().playerCoin();
        session.chooseTopic(buy);
        CHECK(session.tavern().playerCoin() == purse - sim::kDrinkPrice);
        CHECK(session.tavern().drinksPlayerHasHad() == 1);
        // And it is still a conversation afterwards: you can buy a second.
        CHECK(session.talking());
        session.closeConversation();
        CHECK_FALSE(session.talking());
    }
    SUBCASE("F throws a punch and the house minds") {
        // ACTION-COMBAT BUILD: punch() is now a TAP of the sightline swing, so
        // it lands on the FIRST BODY ON THE LOOK-RAY (VETO 1) rather than the
        // radial-nearest it used to. The roster spawns on the quay and walks in,
        // so rather than guess who ends up within reach, stand the player one
        // tile off a present body and face straight at it -- the deterministic
        // way to put a body on the crosshair. reportOffence(Brawled) then takes
        // Welcome to BeingWarned whoever it was and wherever they stand.
        Session session(insideTheGull(19, sim::gull::kBartenderX, sim::gull::kBarY - 1, 0));
        session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
        const sim::Actor* mark = nullptr;
        for (std::int32_t id = 1; id <= 20 && mark == nullptr; ++id) {
            const sim::Actor* who = session.tavern().actorById(id);
            if (who != nullptr && who->present() && who->role() != sim::ActorRole::Vermin) {
                mark = who;
            }
        }
        REQUIRE(mark != nullptr);
        // One tile NORTH of the body, facing SOUTH (180 degrees) straight down
        // onto it -- placeAt is a teleport, so a wall between makes no odds to
        // the raycast, which reads bodies and not tiles.
        session.body().placeAt(mark->tileX(), mark->tileY() - 1, sim::gull::kGroundBand);
        session.body().setYaw(sim::angle_from_degrees(180));
        // One step pushes the new pose into the room (syncTavernToBody), so the
        // sightline casts down the yaw just set rather than the spawn's.
        session.stepMany(sim::MoveInput{}, 1);
        REQUIRE(session.tavern().playerStanding() == sim::Standing::Welcome);
        REQUIRE(session.tavern().playerSightlineTarget());
        session.punch();
        CHECK(session.tavern().playerStanding() == sim::Standing::BeingWarned);
        // THE SAY-ROW DIET: a connecting tap no longer logs "HIT X FOR N." --
        // the wash, the reticle and the audio carry a landed blow now, so the
        // house minding (BeingWarned) is the fact, not a per-blow line. The row
        // stays quiet for a landed non-downing tap.
        CHECK(session.lastMessage().find(" FOR ") == std::string::npos);
    }
    SUBCASE("R refuses when there is no room rented") {
        Session session(insideTheGull(21));
        session.restHere();
        // IN A SENTENCE, NOT IN THE ENUM'S NAME. This used to be "CANNOT REST
        // HERE - " with serviceResultName welded onto it, so a player who
        // pressed R without a bed was answered "nobody there" -- which is not
        // even what Tavern::sleep means by NobodyThere. It means none of the
        // beds is yours.
        CHECK(session.lastMessage() == "NO BED HERE IS YOURS.");
        CHECK(session.lastMessage().find("nobody") == std::string::npos);
        CHECK(session.timeOfDay() == 21 * 3600);
    }
    SUBCASE("a message fades on its own") {
        Session session(insideTheGull(21));
        session.interact();
        REQUIRE_FALSE(session.lastMessage().empty());
        session.stepMany(sim::MoveInput{}, 7 * sim::kStepsPerSecond);
        CHECK(session.lastMessage().empty());
    }
    SUBCASE("E in an empty room opens nothing, and falls through to a look") {
        // #85. THIS IS THE BEHAVIOUR CHANGE THE CONSOLIDATION MEANS. Before
        // #85, Interact tried ONLY talk and said "NOBODY WITHIN REACH." when
        // nobody was there. It now falls all the way through interact()'s
        // resolution order -- no bed, no person, no box/bale/rat -- to the
        // investigation look (examine(), the old Q key), which NEVER refuses
        // (LookResult::line is documented "Never empty."). An empty room is
        // exactly where that fallback is supposed to catch a press that
        // resolved to nothing else, rather than the player learning nothing
        // happened.
        Session session(insideTheGull(5, 148, 68, 180));
        REQUIRE(session.tavern().presentCount() == 0);
        session.interact();
        CHECK_FALSE(session.talking());
        CHECK_FALSE(session.lastMessage().empty());
        // AND IT IS THE LOOK, NOT THE OLD REFUSAL -- the refusal text is
        // gone; something else, real, took its place.
        CHECK(session.lastMessage() != "NOBODY WITHIN REACH.");
    }
}

TEST_CASE("the conversation surface leaves the centre of the screen alone") {
    // TASK #66, applied to the one thing most likely to break it. The obvious
    // way to draw dialogue is a big box in the middle of the screen, and that
    // is precisely what the Java build's first-person view did wrong. This
    // drives the surface with the worst case it will ever see -- the longest
    // authored line, a full twelve-topic list, a haggle open -- and requires
    // the exclusion rectangle to come out untouched.
    for (const int height : {180, 360}) {
        const int width = height * 16 / 9;
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.20F, 0.18F, 0.16F});
        Framebuffer dressed(width, height);
        dressed.clear(Rgb{0.20F, 0.18F, 0.16F});

        DialogueViewState view;
        view.open = true;
        view.speaker = "HARBORMASTER OTTAVAN CRELL";
        view.epithet = "OF THE WEIGHHOUSE AND THE IMPOUND YARD";
        view.attitude = "HOSTILE";
        view.line =
            "EVERY CRATE THAT CROSSES MY QUAY HAS A NUMBER. MOST HAVE THE TRUE ONE. "
            "ASK YOUR QUESTION AS YOU WOULD LOAD A HOLD, HEAVIEST FIRST, AND DO NOT "
            "MAKE ME SAY ANY OF IT TWICE.";
        for (int i = 0; i < kTopicSlots; ++i) {
            view.topics.push_back("ASK ABOUT SOMETHING RATHER LONG NUMBER " +
                                  std::to_string(i));
        }
        view.cursor = 5;
        drawDialogue(dressed, view);

        const CentreRect centre = hudCentreRect(width, height);
        for (int y = centre.y0; y < centre.y1; ++y) {
            for (int x = centre.x0; x < centre.x1; ++x) {
                REQUIRE(bare.pixels()[bare.index(x, y)] == dressed.pixels()[dressed.index(x, y)]);
            }
        }

        // ...and it is not vacuous: both bands really drew.
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

        // The haggle counter uses the same band and must obey the same rule.
        Framebuffer counter(width, height);
        counter.clear(Rgb{0.20F, 0.18F, 0.16F});
        view.haggling = true;
        view.goods = "A ROOM";
        view.asking = 13;
        view.offer = 9;
        view.patience = 3;
        drawDialogue(counter, view);
        for (int y = centre.y0; y < centre.y1; ++y) {
            for (int x = centre.x0; x < centre.x1; ++x) {
                REQUIRE(bare.pixels()[bare.index(x, y)] == counter.pixels()[counter.index(x, y)]);
            }
        }

        // A closed conversation draws nothing whatsoever.
        Framebuffer shut(width, height);
        shut.clear(Rgb{0.20F, 0.18F, 0.16F});
        DialogueViewState closed;
        drawDialogue(shut, closed);
        CHECK(shut.pixels() == bare.pixels());
    }
}

TEST_CASE("a spoken line wraps at spaces and never off the edge") {
    const std::vector<std::string> wrapped =
        wrapText("the ward talks mostly true always late", 12);
    REQUIRE_FALSE(wrapped.empty());
    for (const std::string& line : wrapped) {
        REQUIRE(line.size() <= 12);
        REQUIRE(line.front() != ' ');
        REQUIRE(line.back() != ' ');
    }
    // Nothing is lost: the words come back in order.
    std::string rejoined;
    for (const std::string& line : wrapped) {
        if (!rejoined.empty()) {
            rejoined += ' ';
        }
        rejoined += line;
    }
    CHECK(rejoined == "the ward talks mostly true always late");

    // A word longer than the column is cut rather than allowed to overrun.
    const std::vector<std::string> long1 = wrapText("supercalifragilistic", 6);
    REQUIRE(long1.size() == 4);
    for (const std::string& line : long1) {
        REQUIRE(line.size() <= 6);
    }
    CHECK(wrapText("", 20).empty());
    CHECK(wrapText("anything", 0).empty());
}

TEST_CASE("a topic label too long for its column stops at a word, not mid-word") {
    // THE S5 REVIEW'S FIFTH FINDING, and it is visible in the sprint's own
    // shipped frame: docs/frames/s5-skyrunner-line.png has "6 THE VANISHED CLE"
    // and "7 ASK TO BE MADE R" printed on it. drawDialogue was calling
    // label.resize(room), which cuts wherever the column happens to end.
    CHECK(clipLabel("6 ASK ABOUT THE VANISHED CLERK", 18) == "6 ASK ABOUT THE.");
    CHECK(clipLabel("7 ASK TO BE MADE READY", 18) == "7 ASK TO BE MADE.");
    // A word boundary is a PREFERENCE, not a law: breaking "6 PICK THEIR
    // POCKET" after THEIR would throw five usable columns away, so the cut is
    // where the column ends and the mark is what makes it legible.
    CHECK(clipLabel("6 PICK THEIR POCKET", 18) == "6 PICK THEIR POCK.");
    // Nothing is cut that fits.
    CHECK(clipLabel("1 LEAVE", 18) == "1 LEAVE");
    CHECK(clipLabel("123456789012345678", 18) == "123456789012345678");
    // The mark never makes the label wider than the column it was cut for.
    for (const std::size_t room : {std::size_t{4}, std::size_t{8}, std::size_t{18}}) {
        CHECK(clipLabel("6 ASK ABOUT THE VANISHED CLERK", room).size() <= room);
    }
    // One word with nowhere to break is still cut -- and still says so, rather
    // than pretending the label ended where the column did.
    CHECK(clipLabel("SUPERCALIFRAGILISTIC", 8) == "SUPERCA.");
    // The number and its space are furniture, not a word boundary: cutting
    // there would print "6." and name nothing.
    CHECK(clipLabel("6 VANISHED", 5) == "6 VA.");
    CHECK(clipLabel("anything", 0).empty());
}

TEST_CASE("no topic is cut, and none is dropped, at any size the game is played at") {
    // THE DEFECT THIS CLOSES, PHOTOGRAPHED BEFORE IT WAS CLOSED:
    // docs/frames/conversation/before-640x360.png has "4 SQUALL - 4 FLOW.",
    // "7 PICK THEIR POCK.", "8 SELL WHAT YOU." and "2 THE VANISHED." on it,
    // and a `>` arrow beside the picked row that UI-REFERENCE-TERMINAL.md
    // forbids by name. The cause was a FIXED layout -- three columns of
    // eighteen glyphs at every resolution, because kTopicColumns said 3 and
    // the arithmetic said 18 -- against labels nobody could shorten further.
    //
    // The fix is that the column count now comes from the longest label the
    // page actually holds (panel.hpp's planOptionList), and this is the claim
    // stated as a claim rather than as a screenshot: at every size this game
    // is played at, EVERY row of the page is on screen and EVERY one of them
    // is its whole label.
    //
    // 320x180 is deliberately not in the list. The bottom band may not begin
    // above the exclusion rectangle, which leaves it five rows there, and the
    // documented fallback below is what happens instead.
    DialogueViewState state;
    state.open = true;
    state.speaker = "MASTER VENN";
    state.epithet = "LANDLORD OF THE GILDED GULL";
    state.attitude = "WARM";
    state.line = "A BED IS TWELVE AND IT COMES WITH THE DOOR BOLTED.";
    // Finch's real list, the one the before-frame was taken of, plus the
    // longest label the contract board can build.
    state.topics = {"TELL ME ABOUT...",  "THE VANISHED CLERK",  "ASK TO BE MADE ROBBER",
                    "SQUALL - 4 FLOWER", "LUFF - 3 FLOWER",     "BUY THEM A DRINK",
                    "PICK THEIR POCKET", "SELL WHAT YOU TOOK 0", "SAY NO MORE"};
    state.cursor = 2;

    for (const int height : {360, 540, 720, 1080}) {
        const int width = height * 16 / 9;
        const TopicLayout layout = dialogueTopicLayout(state, width, height);
        INFO(width, "x", height, ": ", layout.plan.columns, " columns x ", layout.plan.rows,
             " rows, label column ", layout.plan.labelCells);
        // Every topic of the page is on screen. A page that silently loses
        // rows is the failure paging exists to prevent.
        REQUIRE(layout.options.size() == state.topics.size());
        // ...and every one of them is WHOLE. This is the assertion that goes
        // red if anybody re-authors a fixed column count.
        for (std::size_t i = 0; i < layout.options.size(); ++i) {
            INFO("row ", i, " printed as '", layout.options[i].label, "'");
            REQUIRE(layout.options[i].label == state.topics[i]);
            REQUIRE(layout.plan.labelCells >= static_cast<int>(state.topics[i].size()));
        }
        // The key printed beside it is the key that picks it, and the cursor
        // lands on the row it is actually on.
        CHECK(layout.options[2].key == "3");
        CHECK(layout.selected == 2);
        // Nothing was abbreviated, so the rule above the list is free to carry
        // the instruction rather than a restatement.
        CHECK_FALSE(layout.abbreviated);
        // The whole page fits the columns it was planned into.
        CHECK(layout.plan.columns * layout.plan.rows >= static_cast<int>(layout.options.size()));
    }

    // THE NARROW-WINDOW FALLBACK, AND ITS ORDER, PINNED. At 320x180 the page
    // does not fit at full width. What must NOT happen is what the first
    // version of this pass did there: show six of the nine and print a
    // "0 MORE (1/1)" that turns to a page which does not exist. Labels are
    // abbreviated -- by clipLabel, which marks its cut -- before a single row
    // is given up.
    const TopicLayout small = dialogueTopicLayout(state, 320, 180);
    INFO("320x180: ", small.plan.columns, " columns x ", small.plan.rows, " rows");
    CHECK(small.options.size() == state.topics.size());
    CHECK(small.abbreviated);
    for (const PanelOption& option : small.options) {
        INFO("row '", option.label, "'");
        // Cut, but never wider than the column it was cut for, and never
        // silently: a shortened label ends in the mark.
        REQUIRE(static_cast<int>(option.label.size()) <= small.plan.labelCells);
        REQUIRE_FALSE(option.label.empty());
    }
    // A twelve-topic list still pages, and the row that turns the page is
    // still on screen with the key that turns it printed beside it.
    DialogueViewState many = state;
    many.topics.assign(20, std::string("ASK ABOUT SOMETHING RATHER LONG NUMBER"));
    many.cursor = 0;
    const TopicLayout paged = dialogueTopicLayout(many, 640, 360);
    REQUIRE_FALSE(paged.options.empty());
    CHECK(paged.options.back().key == "0");
    CHECK(paged.options.back().label.rfind("MORE (1/", 0) == 0);
}

TEST_CASE("the sack and the job are one line each, on the edge, and empty when there is nothing") {
    // THE HUD RULE, applied to the two things S6 added to it. An inventory in
    // this game is a line in a corner until it has earned more -- the Java
    // build's first-person view failed exactly here, with an inspector sheet
    // that ate the right half of the screen.
    Session session(insideTheGull(23, 152, 70, 0));
    CHECK(session.stashLine().empty());
    CHECK(session.contractLine().empty());

    sim::DialogueDirector& talk = session.tavern().dialogue();
    talk.crimes().stash().add(sim::Contraband::Flower, 3);
    const std::string sack = session.stashLine();
    // What is on you, AND what it weighs -- the weight is the number a watchman
    // is actually looking at, so it is on the line and not buried in a sheet.
    CHECK(sack.find("3 FLOWER") != std::string::npos);
    CHECK(sack.find("DR") != std::string::npos);
    CHECK(sack.size() <= 34);

    // A taken job replaces the questline's objective, because a job has a
    // deadline and a questline does not.
    REQUIRE_FALSE(talk.contracts().contracts().empty());
    const std::int32_t id = talk.contracts().contracts().front().id;
    REQUIRE(talk.contracts().take(id) == sim::TakeResult::Taken);
    const sim::Contract* job = talk.contracts().find(id);
    REQUIRE(job != nullptr);
    const std::string work = session.contractLine();
    CHECK(work.find(std::string(sim::contrabandLabel(job->good))) != std::string::npos);
    // "have / wanted", so a player can see how close they are without opening
    // anything.
    CHECK(work.find("/" + std::to_string(job->units)) != std::string::npos);
    CHECK(work.size() <= 34);
}

TEST_CASE("a scripted line sets the clock it needs, and never one that was asked for") {
    // THE S6 REVIEW'S FIRST FIX-FIRST ITEM. `--skyrun` at its own documented
    // invocation landed 0 of 9 beats and exited 1, because the flag defaults to
    // eight in the evening and Finch does not keep the snug until ten.
    SmokeRunConfig skyrun;
    skyrun.skyrun = true;
    // One in the morning: Finch keeps the snug until three and Watchman Cull
    // went home at one. S6 moved this hour, and moved it for a reason the game
    // itself made -- see scriptedStartHour.
    CHECK(scriptedStartHour(skyrun) == 1);

    SmokeRunConfig flame;
    flame.flame = true;
    CHECK(scriptedStartHour(flame) == 20);

    // The roof line wants the door open and nobody in particular.
    SmokeRunConfig roofs;
    roofs.roofs = true;
    CHECK(scriptedStartHour(roofs) == -1);

    // A plain capture is a plain capture.
    CHECK(scriptedStartHour(SmokeRunConfig{}) == -1);
}

TEST_CASE("the whole frame, with somebody talking, still leaves the middle clear") {
    // The surface and the HUD together, over a real render of the real room --
    // which is the only configuration that actually ships.
    Session session(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    session.interact();
    REQUIRE(session.talking());

    Framebuffer plain(320, 180);
    Session quiet(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
    quiet.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    quiet.drawFrame(plain);

    Framebuffer talking(320, 180);
    session.drawFrame(talking);

    // The two frames differ -- the panel is really there...
    CHECK(plain.pixels() != talking.pixels());
    // ...and inside the exclusion rectangle they are identical, because
    // everything the conversation drew stayed on an edge.
    const CentreRect centre = hudCentreRect(320, 180);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(plain.pixels()[plain.index(x, y)] == talking.pixels()[talking.index(x, y)]);
        }
    }
}

// ===========================================================================
// S7 -- the two S6 findings the frames themselves proved
// ===========================================================================

TEST_CASE("no HUD line is ever drawn off the edge of the frame it is in") {
    // THE S6 REVIEW'S FOURTH FINDING, and docs/frames/s6-skyrun-quiet.png is
    // the evidence: "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE, OR I
    // PUT YOU" with the last two words gone off the right edge, cut mid-glyph.
    // Session clips what it composes itself, in say(), to a guessed 56 columns.
    // The bouncer's warning went straight into hud.alert and never met that
    // clip -- and it is 68 characters. A caller-side column count is a guess
    // about a frame it cannot see, so the clip lives where the width is known.
    const std::string longWarning =
        "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE, OR I PUT YOU OUT.";
    for (const int width : {320, 640, 1280}) {
        const int scale = std::max(1, (width * 180 / 320) / 180);
        const int room = width - 2 * (6 * scale);
        const std::string clipped = clipToWidth(longWarning, room, scale);
        INFO("width ", width, " scale ", scale, " clipped '", clipped, "'");
        CHECK(textWidth(clipped, scale) <= room);
        // It says that it cut, rather than leaving a sentence that looks like
        // it ended where the frame did.
        CHECK(clipped.size() >= 3);
        if (clipped.size() < longWarning.size()) {
            CHECK(clipped.substr(clipped.size() - 2) == "..");
        }
    }
    // Nothing that fits is touched.
    CHECK(clipToWidth("HP", 400, 2) == "HP");
    CHECK(clipToWidth("", 400, 2).empty());
    // And no room at all draws nothing rather than one stray glyph.
    CHECK(clipToWidth("ANYTHING", 0, 2).empty());

    // Now the real thing: the frame itself. A 68-character alert is drawn
    // centred at 320x180, and every pixel of it lands inside the frame.
    Session session(insideTheGull(23, 152, 70, 0));
    Framebuffer frame(320, 180);
    HudState hud;
    hud.alert = std::string_view{longWarning};
    session.drawFrame(frame);
    drawHud(frame, hud);
    // drawText clips to the framebuffer, so the proof that nothing ran off is
    // the clip itself: at 320 wide with a 6-pixel margin there is room for 61
    // glyphs and the warning is 68, so it MUST have been cut.
    CHECK(clipToWidth(longWarning, 320 - 12, 1).size() < longWarning.size());
}

TEST_CASE("a warning shouted mid-conversation does not land on the topic grid") {
    // THE S7 REVIEW'S FIRST FINDING, and docs/frames/s7-skyrun.png was shipped
    // as proof of a DIFFERENT fix while carrying this one:
    //
    //   "2KLEDCTARBECK@GYOU HAVE HAD/THESKONLY WORD0YOURGET.1/THE DOOR."
    //
    // hud.cpp draws the alert at height - margin - 23*scale. dialogue_view.cpp
    // claims the bottom band from height - margin - rowStep*6 and draws the
    // topic grid inside it. Session draws the panel and then the HUD over it,
    // so the warning won and row two of the grid became sludge across all three
    // columns.
    //
    // The previous case above RENDERED a frame with an alert on it and then
    // asserted a pure clipToWidth() call -- no pixel of what it drew was ever
    // examined, which is exactly why this shipped. So this one looks at the
    // pixels, and it proves the assertion has teeth in the same breath: the
    // band must be untouched with the rule on, and must CHANGE with it off.
    const std::string longWarning =
        "KLED TARBECK: THAT IS YOUR ONE. OUT OF THIS HOUSE, OR I PUT YOU OUT.";

    for (const int height : {180, 360, 720}) {
        const int width = height * 16 / 9;
        const int scale = std::max(1, height / 180);
        const int margin = 5 * scale;
        const int rowStep = 8 * scale;
        const CentreRect centre = hudCentreRect(width, height);
        // The band dialogue_view.cpp itself claims, computed the same way.
        const int bandTop = std::max(centre.y1 + scale, height - margin - rowStep * 6);

        DialogueViewState panel;
        panel.open = true;
        panel.speaker = "FINCH";
        panel.epithet = "THE QUIET TENANT";
        panel.attitude = "COLD";
        panel.line = "You have had the only word you get.";
        panel.topics = {"SIGN ON: THE SKYRUNNERS", "THE VANISHED CLERK", "SELL WHAT WAS TAKEN",
                        "LEAN ON HIM",             "ASK ABOUT THE ROOFS", "BUY A DRINK FOR HIM",
                        "A HAND IN HIS PURSE",     "ASK ABOUT THE WARD",  "LEAVE"};
        panel.cursor = 0;

        const auto bandOf = [&](const Framebuffer& target) {
            std::vector<std::uint32_t> band;
            for (int y = bandTop; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    band.push_back(target.pixels()[target.index(x, y)]);
                }
            }
            return band;
        };

        // The grid alone.
        Framebuffer bare(width, height);
        bare.clear(Rgb{0.0F, 0.0F, 0.0F});
        drawDialogue(bare, panel);

        // The grid, with a warning shouted across it, drawn the way Session
        // draws it: the alert into the conversation's own top band, and the
        // HUD's copy of it stood down.
        Framebuffer shouted(width, height);
        shouted.clear(Rgb{0.0F, 0.0F, 0.0F});
        DialogueViewState withAlert = panel;
        withAlert.alert = longWarning;
        drawDialogue(shouted, withAlert);
        HudState quiet;
        quiet.alert = std::string_view{longWarning};
        quiet.showAlert = false;
        quiet.showHealth = false;
        quiet.showCompass = false;
        drawHud(shouted, quiet);

        INFO(width, "x", height, " band from y=", bandTop);
        // NOT ONE PIXEL of the topic grid moved.
        CHECK(bandOf(bare) == bandOf(shouted));
        // And the warning really was drawn somewhere: the frames differ ABOVE
        // the band, or this case would pass just as happily against an alert
        // that was thrown away.
        CHECK(bare.pixels() != shouted.pixels());

        // THE MUTATION, RUN HERE RATHER THAN DESCRIBED. Let the HUD draw the
        // alert where S7 drew it and the band must change -- which is what
        // makes the assertion above a claim and not a coincidence.
        Framebuffer collided(width, height);
        collided.clear(Rgb{0.0F, 0.0F, 0.0F});
        drawDialogue(collided, panel);
        HudState loud;
        loud.alert = std::string_view{longWarning};
        loud.showAlert = true;
        loud.showHealth = false;
        loud.showCompass = false;
        drawHud(collided, loud);
        CHECK(bandOf(bare) != bandOf(collided));
    }
}

TEST_CASE("the picked topic is spelled out in full under the grid, however long it is") {
    // THE S6 REVIEW'S THIRD FINDING, and the cruellest of them: the sprint did
    // the work of making every proper noun authored and then printed
    // "8 TAKE 3 SCALPS." beside "9 TAKE 4 SCALPS.", and "7 SIGN ON: THE." --
    // a row that names nothing at all. A topic column is eighteen glyphs at
    // every resolution this game runs at; some labels are longer than that no
    // matter how they are worded, and more clipping logic cannot fix it.
    DialogueViewState state;
    state.open = true;
    state.speaker = "FINCH";
    state.line = "Sit down.";
    state.topics = {"SIGN ON: THE SKYRUNNERS", "VETCH - 4 SCALPS", "LEAVE"};

    state.cursor = 0;
    CHECK(dialogueDetailLine(state) == "SIGN ON: THE SKYRUNNERS");
    state.cursor = 1;
    CHECK(dialogueDetailLine(state) == "VETCH - 4 SCALPS");
    // A cursor off the end of a shrinking list names nothing rather than
    // reading past it.
    state.cursor = 9;
    CHECK(dialogueDetailLine(state).empty());
    // And a cursor left behind on another page describes nothing, because a
    // detail line for a row nobody can see is worse than none.
    state.topics.assign(14, std::string("A TOPIC"));
    state.cursor = 0;
    state.page = 1;
    CHECK(dialogueDetailLine(state).empty());
    state.page = 0;
    CHECK(dialogueDetailLine(state) == "A TOPIC");
    state.topics.clear();
    CHECK(dialogueDetailLine(state).empty());

    // On the frame: the surface draws the detail line, it stays out of the
    // play space, and it lands inside the bottom band.
    DialogueViewState drawn;
    drawn.open = true;
    drawn.speaker = "FINCH";
    drawn.line = "Nobody joins us. People stop being strangers.";
    drawn.topics = {"SIGN ON: THE SKYRUNNERS", "LEAVE"};
    drawn.cursor = 0;

    Framebuffer without(320, 180);
    without.clear(Rgb{0.0F, 0.0F, 0.0F});
    DialogueViewState bare = drawn;
    bare.topics = {"LEAVE"};
    bare.cursor = 0;
    drawDialogue(without, bare);

    Framebuffer with(320, 180);
    with.clear(Rgb{0.0F, 0.0F, 0.0F});
    drawDialogue(with, drawn);

    // The long label really is on the frame somewhere the short one is not.
    CHECK(without.pixels() != with.pixels());
    // And the centre is still untouched by either.
    const CentreRect centre = hudCentreRect(320, 180);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(without.pixels()[without.index(x, y)] ==
                    with.pixels()[with.index(x, y)]);
        }
    }
}

TEST_CASE("two jobs on one board are told apart by the first word, not the last") {
    // "8 TAKE 3 SCALPS." and "9 TAKE 4 SCALPS." were the same row twice as far
    // as a player scanning the list was concerned, because the authored name
    // went LAST and last is what the column ate. It goes first now.
    Session session(insideTheGull(23, 152, 70, 0));
    sim::DialogueDirector& talk = session.tavern().dialogue();
    REQUIRE_FALSE(talk.contracts().contracts().empty());
    for (const sim::Contract& row : talk.contracts().contracts()) {
        INFO("label '", row.label, "'");
        // The patron leads, and it is a name out of the owner's own file.
        const std::size_t gap = row.label.find(" - ");
        REQUIRE(gap != std::string::npos);
        REQUIRE(gap > 0);
        const std::string patron = row.label.substr(0, gap);
        // AND IT SURVIVES THE COLUMN. This is the actual claim: eighteen
        // glyphs, two of them the number that picks the row, and the name is
        // still legible after the cut. With the S6 ordering it was the name
        // and only the name that got eaten.
        const std::string onScreen = clipLabel("1 " + row.label, 18);
        INFO("on screen '", onScreen, "'");
        CHECK(onScreen.find(patron) != std::string::npos);
        // It still says how many of what, in full or in the detail line.
        CHECK(row.label.find(std::to_string(row.units)) != std::string::npos);
        CHECK(row.label.find(std::string(sim::contrabandLabel(row.good))) !=
              std::string::npos);
        DialogueViewState state;
        state.topics = {row.label};
        state.cursor = 0;
        CHECK(dialogueDetailLine(state) == row.label);
    }
}

// ===========================================================================
// S8 -- the nemesis, through the client's own calls
// ===========================================================================

TEST_CASE("the scripted nemesis arc is played, not staged, and the HUD says who he is") {
    // THE SPRINT'S ACCEPTANCE AT THE CLIENT LEVEL. runNemesisLine walks the
    // body, presses the punch key, lets the room resolve a real brawl, and
    // takes the respawn Session::step() finds -- Tavern::concedeTo exists and
    // is deliberately not used anywhere in it.
    SmokeRunConfig config;
    config.session = insideTheGull(20, 152, 70, 180);
    config.session.width = 320;
    config.session.height = 180;
    config.steps = 0;
    config.walk = false;
    config.nemesis = true;
    config.nemesisEnd = "away";

    const SmokeRunResult result = runSmoke(config);
    INFO(result.summary);
    // EVERY BEAT. A scripted run that landed some of what it asked for is a
    // frame that is not a picture of what it claims -- S5's own review found
    // exactly that, and runSmoke fails the process for it now.
    CHECK(result.nemesisBeats == 7);
    CHECK(result.scriptedLanded == result.scriptedWanted);
    CHECK(result.ok);
    // And the summary names him, so a build log is evidence rather than a
    // number. The arc is worth nothing if you cannot tell who rose.
    CHECK(result.summary.find("Tarn Wrenhale") != std::string::npos);
    CHECK(result.summary.find("holds") != std::string::npos);

    // AND THE `talk` ENDING PHOTOGRAPHS THE RIGHT MAN. His stool and Wick
    // Hempson's are one tile apart and speakTo opens on whoever is nearest, so
    // the first shipped frame of this line was a conversation with the wrong
    // docker -- the same class of thing the S4 review caught in the Priest of
    // the Flame's capture, and the reason the ending retries.
    SmokeRunConfig talking = config;
    talking.nemesisEnd = "talk";
    const SmokeRunResult shown = runSmoke(talking);
    INFO(shown.summary);
    CHECK(shown.nemesisBeats == 7);
    CHECK(shown.talking);
    CHECK(shown.summary.find("talking to Tarn Wrenhale") != std::string::npos);
}

TEST_CASE("the man who put you down is one line on an edge, and the centre stays empty") {
    // The HUD rule, applied to the newest row on it. COMBAT-FEEL-REFERENCE
    // section 3: the HUD hugs all four edges and the centre stays clear. The
    // Java build's first-person view failed exactly here.
    Session session(insideTheGull(20, 152, 70, 180));
    CHECK(session.rivalLine().empty());

    // Lose to him three times, through the room's own beating path.
    const sim::Actor* tarn = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.name() == "Tarn Wrenhale") {
            tarn = &actor;
        }
    }
    REQUIRE(tarn != nullptr);
    for (int round = 0; round < 3; ++round) {
        session.tavern().concedeTo(tarn->id());
        session.settleDefeat();
        session.skipToHour(20);
    }
    const std::string line = session.rivalLine();
    INFO("rival line '", line, "'");
    CHECK_FALSE(line.empty());
    CHECK(line.find("TARN WRENHALE") != std::string::npos);
    // UI-EA (LANE HUD): the diet cut the row to name and count ("rank
    // 6 -> 3") -- no RIVAL prefix, no title, and HUNTING is carried by the
    // row's red ink (HudState::rivalHunts), never spelled. The count is a
    // value and values never get vaguer.
    CHECK(line.find("x3") != std::string::npos);
    CHECK(line.find("HUNTING") == std::string::npos);
    CHECK(line.find("RIVAL") == std::string::npos);

    // On the frame: it draws, and the play space is untouched by it.
    Framebuffer without(320, 180);
    without.clear(Rgb{0.0F, 0.0F, 0.0F});
    HudState bare;
    drawHud(without, bare);

    Framebuffer with(320, 180);
    with.clear(Rgb{0.0F, 0.0F, 0.0F});
    HudState named;
    named.rivalLabel = std::string_view{line};
    drawHud(with, named);

    CHECK(without.pixels() != with.pixels());
    const CentreRect centre = hudCentreRect(320, 180);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(without.pixels()[without.index(x, y)] == with.pixels()[with.index(x, y)]);
        }
    }
    // And it is anchored to the right edge, so a long name cannot run off the
    // frame the way S6's alert did.
    CHECK(textWidth(line, 1) <= 320 - 12);
}

TEST_CASE("the ward's roll is in the windowed game, and a rival can take ground on it") {
    // THE S7 REVIEW'S EIGHTH FINDING. The compounds were built and nothing with
    // a window on it ever constructed one -- "3,303 lines of economy that the
    // player cannot see, touch, or be affected by". A Session builds the roll
    // now, on the same engine the Gull runs on.
    Session session(insideTheGull(20, 152, 70, 180));
    REQUIRE(session.ward().loaded());
    CHECK(session.ward().plots().size() >= 5);
    CHECK(session.ward().heads() > 0);

    const std::int32_t gullet = session.ward().plotNamed("C4_GULLET");
    REQUIRE(gullet >= 0);
    CHECK(session.ward().plots()[static_cast<std::size_t>(gullet)].tenure == sim::Tenure::Vacant);

    const sim::Actor* tarn = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.name() == "Tarn Wrenhale") {
            tarn = &actor;
        }
    }
    REQUIRE(tarn != nullptr);
    for (int round = 0; round < 3; ++round) {
        session.tavern().concedeTo(tarn->id());
        session.settleDefeat();
        session.skipToHour(20);
    }
    const sim::Plot& plot = session.ward().plots()[static_cast<std::size_t>(gullet)];
    CHECK(plot.tenure == sim::Tenure::Charged);
    CHECK(plot.heldBy == "Tarn Wrenhale");

    // AND THE ROLL IS TICKING, not merely present: a day of the ward passes
    // while the player stands in the taproom, and the land does a day's work.
    //
    // S9 REPLACES TWO TAUTOLOGIES HERE. The S8 review found that this block
    // asserted `day() >= dayBefore` and `stats().days >= 0` on monotonic
    // int64s that start at zero -- both true of a ward that had never run a
    // second -- and its own probe showed exactly that: ten slept nights and ten
    // minutes of play left stats().days at 0, because Ward::tick counts engine
    // seconds and every skip in Session moved the TAVERN'S clock without them.
    // These two go red on the code as it was.
    const std::int64_t dayBefore = session.ward().day();
    const std::int64_t harvestsBefore = session.ward().stats().harvests;
    // Twelve hours to the morning and twelve back to the evening: one whole
    // day of the ward, jumped exactly the way sleeping in a rented bed jumps
    // it. Nothing in between is simulated -- that is what a skip IS -- and the
    // land still has to do its day's work.
    session.skipToHour(8);
    session.skipToHour(20);
    session.stepMany(sim::MoveInput{}, 240);
    CHECK(session.ward().day() > dayBefore);
    CHECK(session.ward().stats().days > 0);
    CHECK(session.ward().stats().harvests > harvestsBefore);
}


// ===========================================================================
// S13 -- the guard row and the equipped-crafting row
// ===========================================================================

TEST_CASE("the guard row is the room's fact, and a conversation lowers it") {
    // The client only reports the key's physical edge (setBlocking); what the
    // ROOM is told is derived once a step, in step() -- so a page eating the
    // keyboard lowers the guard with no release edge ever firing, and the
    // page closing under a still-held key raises it again. blockLine() reads
    // tavern().playerBlocking(), the exact state tickBrawl reads, so this
    // case is pinning the row to the fact and not to the keypress.
    Session session(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
    CHECK(session.blockLine().empty());

    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine() == "GUARD UP");
    CHECK(session.tavern().playerBlocking());

    session.interact();
    REQUIRE(session.talking());
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine().empty());
    CHECK_FALSE(session.tavern().playerBlocking());

    session.closeConversation();
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine() == "GUARD UP");

    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine().empty());
}

TEST_CASE("the swing charge shows on its row and drops the guard while the hand is busy") {
    // ACTION-COMBAT BUILD (section 5, channels 1-2, and section 1.3). The HELD
    // HARD row is the hard tier made visible, and a swing DROPS the guard: the
    // derived block state gained the combat-idle clause, so the GUARD UP row
    // honestly vanishes for the swing window. Both are the room's own state,
    // read through the same accessors the reticle and tickBrawl read.
    Session session(insideTheGull(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, 180));
    session.stepMany(sim::MoveInput{}, 2);

    // A raised guard holds while the hand is idle.
    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine() == "GUARD UP");
    CHECK(session.chargeLine().empty());

    // The instant a swing is charging the guard drops (the hand is no longer
    // idle), and the HELD HARD row is still empty below the hard threshold.
    session.attackDown();
    session.stepMany(sim::MoveInput{}, 2);
    CHECK_FALSE(session.tavern().playerCombatIdle());
    CHECK(session.blockLine().empty());
    CHECK(session.chargeLine().empty());

    // Held past the hard threshold, the row names the held weapon in the
    // sheet's own span grammar ("HELD HARD -- FISTS 6-10" at the base sheet).
    session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps);
    CHECK(session.tavern().playerChargeHard());
    CHECK(session.chargeLine().rfind("HELD HARD --", 0) == 0);

    // Released, the row is gone at once (recovery is not charging), and the
    // guard returns the moment the hand is idle again.
    session.attackUp();
    CHECK(session.chargeLine().empty());
    session.stepMany(sim::MoveInput{}, sim::kHardSwingRecoverySteps + 2);
    CHECK(session.tavern().playerCombatIdle());
    CHECK(session.blockLine() == "GUARD UP");
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.blockLine().empty());
}

TEST_CASE("the equipped-crafting row appears the moment there is one to ready") {
    Session session(insideTheGull(20, 152, 70, 0));
    // ABSENCE COSTS NOTHING: the grimoire is empty at spawn and the row says
    // nothing rather than saying so.
    CHECK(session.spellLine().empty());
    // And the key itself refuses out loud rather than doing nothing -- the
    // COMMON state, and the main path a new player actually hits.
    session.castEquipped();
    CHECK(session.lastMessage() == "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.");

    // The first crafting learned is the default equip -- no menu trip; the
    // row appears with it, one line on the top-right edge.
    const sim::Spell* sting = session.tavern().spellbook().find("sting");
    REQUIRE(sting != nullptr);
    REQUIRE(session.tavern().dialogue().grimoire().learn(*sting));
    CHECK(session.spellLine() == "CAST  STING");
}

TEST_CASE("the active-effect row carries a live hold's name and its countdown") {
    // HELD-EFFECTS BUILD. The row is absent until a hold is genuinely live,
    // says the crafting's own name with the seconds it has left, counts DOWN
    // as room time passes, and goes away when the hold lapses.
    Session session(insideTheGull(20, 152, 70, 0));
    CHECK(session.effectLine(0).empty());

    const sim::Spell* steady = session.tavern().spellbook().find("steady_the_hand");
    REQUIRE(steady != nullptr);
    REQUIRE(session.tavern().dialogue().grimoire().learn(*steady));
    // Level 10 puts the check at its 95 ceiling; the fizzle tail is walked
    // out with real steps, the same shape every cast case in this file uses.
    REQUIRE(session.tavern().dialogue().skills().setLevel("linkcraft", 10));
    bool held = false;
    for (int attempt = 0; attempt < 12 && !held; ++attempt) {
        session.castEquipped();
        held = !session.tavern().heldEffects().empty();
        if (!held) {
            session.stepMany(sim::MoveInput{}, 31 * 60);
        }
    }
    REQUIRE(held);

    CHECK(session.effectLine(0) == "STEADY THE HAND 900S");
    CHECK(session.effectLine(1).empty());
    // A minute of room time later the same row reads a minute less --
    // continuous time-remaining, not a snapshot.
    session.stepMany(sim::MoveInput{}, 60 * 60);
    CHECK(session.effectLine(0) == "STEADY THE HAND 840S");
}

// ===========================================================================
// THE POINTER PASS -- the bottom band's topic list, invertible
// ===========================================================================
//
// One widget is five of the ship note's silent pages (pause, options,
// grimoire, wait, a conversation), so one hit-test is the mouse for all five.
// These cases pin dialogueTopicAtPixel to the drawing arithmetic the same way
// test_casebook_page pins casebookLeadAtPixel: the geometry names a pixel for
// a row, and moving the cursor onto that row has to move drawn ink UNDER that
// exact pixel -- if the band rect or the plan ever drift from what
// drawDialogue composes, the named pixel lands on ground that does not
// change and the case goes red.

namespace {

/// The centre pixel of drawn row `i`, off the geometry's own plan --
/// drawOptionListPlanned's column-major walk, inverted by hand once, here.
[[nodiscard]] std::pair<int, int> topicRowPixel(const TopicListGeometry& geo, int i) {
    const int column = i / geo.plan.rows;
    const int row = i % geo.plan.rows;
    const int px = geo.list.x + geo.metric.widthOf(column * geo.plan.stride) +
                   geo.metric.widthOf(geo.plan.columnCells) / 2;
    const int py = geo.list.y + geo.metric.heightOf(row) + geo.metric.cellH() / 2;
    return {px, py};
}

}  // namespace

TEST_CASE("the topic-band hit-test is the inverse of what the band drew") {
    DialogueViewState state;
    state.open = true;
    state.speaker = "MENU";
    state.epithet = "ENTER SELECTS  ESC RESUMES";
    state.line = "THE DOCKS DO NOT WAIT ON YOU.";
    state.topics = {"RESUME", "WAIT", "CONTROLS", "SETTINGS", "QUIT"};
    state.cursor = 0;
    state.page = 0;

    const TopicListGeometry geo = dialogueTopicListGeometry(state, 640, 360);
    REQUIRE(geo.usable);
    REQUIRE(geo.count == 5);

    for (int i = 0; i < geo.count; ++i) {
        const auto [px, py] = topicRowPixel(geo, i);
        INFO("row ", i, " at (", px, ",", py, ")");
        const DialogueTopicHit hit = dialogueTopicAtPixel(state, 640, 360, px, py);
        CHECK(hit.row == i);
        CHECK(hit.index == i);
        CHECK(hit.slot == i);
        CHECK_FALSE(hit.more);
    }
    // Off the band entirely: no row, no index -- a click there stays modal.
    CHECK(dialogueTopicAtPixel(state, 640, 360, 320, 40).row == -1);

    // AND THE PIXEL IS WHERE THE INK IS. Putting the cursor on row 3 must
    // change the frame at exactly the pixel the hit-test names for row 3 --
    // the inverted fill arriving under the pointer. This is the coupling that
    // keeps the hit-test's private band arithmetic honest against
    // drawDialogue's own.
    const auto [px3, py3] = topicRowPixel(geo, 3);
    Framebuffer onZero(640, 360);
    drawDialogue(onZero, state);
    state.cursor = 3;
    Framebuffer onThree(640, 360);
    drawDialogue(onThree, state);
    const std::size_t at = static_cast<std::size_t>(py3) * 640 + static_cast<std::size_t>(px3);
    CHECK(onZero.pixels()[at] != onThree.pixels()[at]);
}

TEST_CASE("page two of a long list answers with page-two indices, and the MORE row says so") {
    DialogueViewState state;
    state.open = true;
    state.speaker = "MASTER VENN";
    state.attitude = "WARM";
    state.line = "TWELVE THINGS TO SAY.";
    for (int i = 0; i < 12; ++i) {
        state.topics.push_back("TOPIC NUMBER " + std::to_string(i + 1));
    }
    state.cursor = 9;
    state.page = 1;

    const TopicListGeometry geo = dialogueTopicListGeometry(state, 640, 360);
    REQUIRE(geo.usable);
    // Page two of twelve: rows ten to twelve, then the MORE row.
    REQUIRE(geo.count == 4);

    for (int i = 0; i < 3; ++i) {
        const auto [px, py] = topicRowPixel(geo, i);
        const DialogueTopicHit hit = dialogueTopicAtPixel(state, 640, 360, px, py);
        INFO("row ", i);
        CHECK(hit.index == 9 + i);
        CHECK(hit.slot == i);
        CHECK_FALSE(hit.more);
    }
    const auto [mx, my] = topicRowPixel(geo, 3);
    const DialogueTopicHit more = dialogueTopicAtPixel(state, 640, 360, mx, my);
    CHECK(more.more);
    CHECK(more.index == -1);
}

TEST_CASE("the haggle, the workbench and a letter stay modal to the pointer") {
    DialogueViewState state;
    state.open = true;
    state.speaker = "MASTER VENN";
    state.topics = {"A ROW"};
    state.haggling = true;
    CHECK_FALSE(dialogueTopicListGeometry(state, 640, 360).usable);
    state.haggling = false;
    state.forging = true;
    CHECK_FALSE(dialogueTopicListGeometry(state, 640, 360).usable);
    state.forging = false;
    state.letter = true;
    CHECK_FALSE(dialogueTopicListGeometry(state, 640, 360).usable);
}
