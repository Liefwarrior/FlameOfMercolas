// #85. THE CONSOLIDATED CONTROL SCHEME'S ACTUAL FEATURE: ONE Interact BUTTON,
// RESOLVED BY STANCE AND BY WHAT IS FACED.
//
// Eli's own brief: "make each interaction either stealth-specific or not...
// pickpocket if sneaking", and the acceptance he asked for is explicit about
// what "tested" means here -- "Interact resolves to talk/pickpocket/examine/
// rest correctly across sneak-on/sneak-off x each target type -- this is the
// actual feature, test it as such, not just that the action fires."
//
// So every case below asserts on the RESOLVED OUTCOME (talking() opened, a
// pickpocket line said with no conversation, the clock skipped to morning,
// picking() opened), not merely that interact() returned without crashing.
// Session::interactPrompt() is proved against the SAME scenarios, because a
// HUD label that can drift from what the button actually does is worse than
// no label -- see its own header in session.hpp.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

/// A session standing inside the Gilded Gull, the same shape
/// test_tavern_render.cpp's insideTheGull() uses -- built fresh here because
/// this file also needs the UPPER floor (the guest rooms and their boxes),
/// which that helper cannot reach (it hardcodes the ground band).
SessionConfig gullAt(int hour, std::int32_t x, std::int32_t y, std::int32_t band,
                     int yawDegrees = 180) {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = band;
    config.spawnYaw = sim::angle_from_degrees(yawDegrees);
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

/// The first present actor of this role, or nullptr.
[[nodiscard]] const sim::Actor* findRole(const Session& session, sim::ActorRole role) {
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.role() == role && actor.present()) {
            return &actor;
        }
    }
    return nullptr;
}

/// Rents the first guest room (kRooms[0]) by standing the body on the
/// innkeeper's own tile -- distance zero is comfortably inside rentRoom()'s
/// two-tile reach -- and answers whether it worked.
[[nodiscard]] bool rentFirstRoom(Session& session) {
    const sim::Actor* innkeeper = findRole(session, sim::ActorRole::Innkeeper);
    if (innkeeper == nullptr) {
        return false;
    }
    session.body().placeAt(innkeeper->tileX(), innkeeper->tileY(), sim::gull::kGroundBand);
    session.tavern().setPlayer(session.body().x(), session.body().y(), session.body().band());
    return session.tavern().rentRoom() == sim::ServiceResult::Served;
}

}  // namespace

TEST_CASE("Interact resolves to TALK upright and PICKPOCKET crouched, facing the same person") {
    // Eleven in the morning: the bartender is behind the counter and nobody
    // else is nearer to the spawn point than she is.
    Session session(gullAt(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    REQUIRE(session.stance() == sim::Stance::Upright);
    REQUIRE_FALSE(session.talking());

    // THE LABEL, BEFORE THE PRESS.
    CHECK(session.interactPrompt() == "TALK");

    session.interact();
    CHECK(session.talking());
    CHECK_FALSE(session.lastMessage().empty());
    const std::string talkedMessage = session.lastMessage();
    session.closeConversation();
    REQUIRE_FALSE(session.talking());

    // CROUCH, AND CLOSE THE GAP. Talk's reach (kReachQ8, 2 tiles) and lift's
    // (kLiftReachQ8, 1.5 tiles) are not the same number -- pickpocketing
    // asks for a hand closer than a word does -- and "in front of the bar
    // counter" is exactly 2 tiles from the bartender, at the edge of the
    // first and just outside the second. A real player closing from talking
    // range to pickpocketing range is the same tile of narrative sense this
    // moves for, not a rule bent to make the case pass.
    session.body().placeAt(sim::gull::kBartenderX + 1, sim::gull::kBartenderY,
                           sim::gull::kGroundBand);
    session.setCrouched(true);
    REQUIRE(session.stance() == sim::Stance::Crouched);

    // THE LABEL CHANGES LIVE, with no press in between. This is the exact
    // scenario the acceptance capture is built around: "[E] TALK" changing
    // to "[E] PICKPOCKET" the instant the player crouches facing somebody.
    CHECK(session.interactPrompt() == "PICKPOCKET");

    session.interact();
    // A PICKPOCKET NEVER OPENS A CONVERSATION -- this is the one assertion
    // that actually distinguishes "resolved to pickpocket" from "resolved to
    // talk", independent of which of liftFrom()'s several lines came out.
    CHECK_FALSE(session.talking());
    CHECK_FALSE(session.lastMessage().empty());
    CHECK(session.lastMessage() != talkedMessage);
}

TEST_CASE("Interact resolves to REST at your own rented bed, not sneaking") {
    Session session(gullAt(21, sim::gull::kBartenderX, sim::gull::kBartenderY, sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    REQUIRE(rentFirstRoom(session));

    // ROOM 0's OWN STAND TILE -- the same coordinates tavern.hpp's kRooms
    // table authors, so this is exactly where a player pressing the key at
    // their own bed-foot would be standing.
    const sim::gull::GuestRoom& room = sim::gull::kRooms[0];
    session.body().placeAt(room.standX, room.standY, sim::gull::kUpperBand);
    REQUIRE(session.stance() == sim::Stance::Upright);

    CHECK(session.interactPrompt() == "REST");

    const int before = session.timeOfDay();
    session.interact();
    // SLEPT, NOT TALKED, NOT PICKED A LOCK: the clock jumped to morning,
    // which is what only restHere()'s Served branch does.
    CHECK_FALSE(session.talking());
    CHECK_FALSE(session.picking());
    CHECK(session.lastMessage() == "SLEPT UNTIL MORNING.");
    CHECK(session.timeOfDay() == sim::hourOfDay(7));
    CHECK(session.timeOfDay() != before);
}

TEST_CASE("Interact resolves to PICK LOCK facing a locked box, sneaking or not") {
    // ROOM 1, NOT RENTED -- a stranger's box, still shut. Room 0 is left free
    // for the rest test above to rent without the two colliding inside one
    // process (each TEST_CASE gets its own fresh Session either way, but the
    // choice of room keeps the two cases legible independently).
    const sim::gull::GuestRoom& room = sim::gull::kRooms[1];

    SUBCASE("upright") {
        Session session(gullAt(21, room.standX, room.standY, sim::gull::kUpperBand));
        REQUIRE(session.stance() == sim::Stance::Upright);
        CHECK(session.interactPrompt() == "PICK LOCK");

        session.interact();
        // THE WIRE WENT IN -- not a conversation, not a look-around.
        CHECK(session.picking());
        CHECK_FALSE(session.talking());
    }
    SUBCASE("sneaking") {
        // #85's OWN CLAIM, LITERALLY: "facing a lock=pick it (sneaking or
        // not)". crackStrongbox() never reads stance, so this is the same
        // outcome through the sneaking branch of interact() instead of the
        // upright one.
        Session session(gullAt(21, room.standX, room.standY, sim::gull::kUpperBand));
        session.setCrouched(true);
        REQUIRE(session.stance() == sim::Stance::Crouched);
        CHECK(session.interactPrompt() == "PICK LOCK");

        session.interact();
        CHECK(session.picking());
        CHECK_FALSE(session.talking());
    }
}

TEST_CASE("Interact resolves to LOOK when nothing else is here, and never refuses") {
    // FIVE IN THE MORNING, ON THE STAIR ITSELF -- a tile scripted lines
    // already prove standable on the upper band (climbAndLand's own landing
    // point), and outside every room's standX/standY, so gull::roomAtStand
    // answers -1 here: no person in reach, no bed of the player's own, no
    // box underfoot.
    Session session(gullAt(5, sim::gull::kStairX, sim::gull::kStairY, sim::gull::kUpperBand));
    REQUIRE(session.tavern().presentCount() == 0);
    REQUIRE(session.stance() == sim::Stance::Upright);

    CHECK(session.interactPrompt() == "LOOK");

    session.interact();
    CHECK_FALSE(session.talking());
    CHECK_FALSE(session.picking());
    // NEVER EMPTY -- LookResult::line is documented "Never empty.", which is
    // the whole reason LOOK is the floor every other branch falls through to
    // rather than a fourth way to say nothing happened.
    CHECK_FALSE(session.lastMessage().empty());
}

TEST_CASE("interactPrompt stands down exactly when a page already owns the keyboard") {
    Session session(gullAt(11, sim::gull::kBartenderX, sim::gull::kBarY - 1, sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    REQUIRE(session.interactPrompt() == "TALK");

    session.interact();
    REQUIRE(session.talking());
    // THE TOPIC LIST ALREADY SHOWS WHAT INTERACT DOES ON THIS ROW -- a
    // second, floating label would say the same thing twice.
    CHECK(session.interactPrompt().empty());
    session.closeConversation();

    session.togglePause();
    REQUIRE(session.pauseOpen());
    CHECK(session.interactPrompt().empty());
    session.togglePause();

    session.toggleMenu();
    REQUIRE(session.menuOpen());
    CHECK(session.interactPrompt().empty());
}
