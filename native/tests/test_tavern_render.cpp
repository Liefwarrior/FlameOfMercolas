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
    // AND PEOPLE ARE VISIBLE IN IT. The S2 review's fourth finding: this case
    // used to assert spritePixels > 0, and a lit Gull carries nine flame
    // sprites -- so it passed with every human being in the room invisible.
    // actorPixels counts only billboards flagged `person`.
    CHECK(nightStats.actorPixels > 0);
    CHECK(nightStats.actorPixels <= nightStats.spritePixels);

    // Five in the morning: the fire is banked, the doors are shut, and there is
    // nobody in the building at all.
    CHECK_FALSE(dawn.tavern().fireLit());
    CHECK_FALSE(dawn.tavern().isOpen());
    CHECK(dawn.tavernLights().empty());
    CHECK(dawn.tavern().presentCount() == 0);
    CHECK(dawn.actorSprites().empty());
    CHECK(dawnStats.actorPixels == 0);

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
        flame.z = static_cast<float>(light.z) + 0.5F;
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
    // Legs, torso and head all sit on the body's own position.
    CHECK(matching >= 3);
    CHECK(atTileCentre == 0);
    // The sub-tile position really is inside its tile and not on its centre.
    CHECK(wantX >= static_cast<float>(walker->tileX()));
    CHECK(wantX < static_cast<float>(walker->tileX()) + 1.0F);
    CHECK(wantY >= static_cast<float>(walker->tileY()));
    CHECK(wantY < static_cast<float>(walker->tileY()) + 1.0F);

    // Every billboard a solid, never a glow -- an actor is not a light source.
    for (const SpriteInstance& sprite : sprites) {
        CHECK(sprite.glow == 0.0F);
        CHECK(sprite.person);
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

    const std::size_t seenFromFront = session.actorSprites(infront).size();
    const std::size_t seenFromBack = session.actorSprites(behind).size();
    // Same actors, same light, same everything -- only which way the eye is.
    CHECK(seenFromFront != seenFromBack);
    // And the difference is specifically the bartender's face: exactly one
    // billboard sits at his position when the eye is in front of him and does
    // not when it is behind him.
    const auto facesAt = [&](const Camera& view) {
        int count = 0;
        for (const SpriteInstance& sprite : session.actorSprites(view)) {
            const float dx = sprite.x - (static_cast<float>(bartender->x()) / 256.0F);
            const float dy = sprite.y - (static_cast<float>(bartender->y()) / 256.0F);
            // The face is the one part pushed off the body's own axis.
            if (std::abs(dx) + std::abs(dy) > 0.01F && std::abs(dx) + std::abs(dy) < 0.30F) {
                ++count;
            }
        }
        return count;
    };
    CHECK(facesAt(infront) > facesAt(behind));
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
    SUBCASE("E in an empty room says so and opens nothing") {
        Session session(insideTheGull(5, 148, 68, 180));
        REQUIRE(session.tavern().presentCount() == 0);
        session.interact();
        CHECK_FALSE(session.talking());
        CHECK(session.lastMessage() == "NOBODY WITHIN REACH");
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
        view.speaker = "HARBOURMASTER OTTAVAN CRELL";
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

TEST_CASE("a scripted line sets the clock it needs, and never one that was asked for") {
    // THE S6 REVIEW'S FIRST FIX-FIRST ITEM. `--skyrun` at its own documented
    // invocation landed 0 of 9 beats and exited 1, because the flag defaults to
    // eight in the evening and Finch does not keep the snug until ten.
    SmokeRunConfig skyrun;
    skyrun.skyrun = true;
    CHECK(scriptedStartHour(skyrun) == 22);

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
