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
    // TIME-AND-TENURE BUILD: the press opens the hour-select page IN SLEEP
    // MODE now, rather than committing a fixed night on the spot -- see
    // interact()'s own bed-branch note. Not talked, not picked, and the
    // clock has not moved yet: choosing an hour is what spends it.
    CHECK_FALSE(session.talking());
    CHECK_FALSE(session.picking());
    REQUIRE(session.waitOpen());
    CHECK(session.waitSleeping());
    CHECK(session.timeOfDay() == before);

    // Eight hours (row 8) from nine at night is five in the morning -- the
    // chosen waking hour the ruling asked for, through Tavern::sleepUntil.
    session.chooseWaitRow(7);
    CHECK_FALSE(session.waitOpen());
    CHECK(session.lastMessage() == "SLEPT UNTIL 05:00.");
    CHECK(session.timeOfDay() == sim::hourOfDay(5));

    // And the R verb keeps the old one-press night, unchanged.
    session.body().placeAt(room.standX, room.standY, sim::gull::kUpperBand);
    session.stepMany(sim::MoveInput{}, 1);
    session.restHere();
    CHECK(session.lastMessage() == "SLEPT UNTIL MORNING.");
    CHECK(session.timeOfDay() == sim::hourOfDay(7));
}

TEST_CASE("Interact resolves to PICK LOCK facing a locked box, sneaking or not") {
    // ROOM 1, NOT RENTED -- a stranger's box, still shut. Room 0 is left free
    // for the rest test above to rent without the two colliding inside one
    // process (each TEST_CASE gets its own fresh Session either way, but the
    // choice of room keeps the two cases legible independently).
    //
    // FIVE IN THE MORNING, not nine at night: a guest room can have its own
    // tenant asleep in it, and PERSON outranks BOX in interact()'s own
    // priority order (correctly -- a body in reach is not a fixture), so an
    // hour with somebody actually home resolves to TALK/PICKPOCKET instead
    // and this case is proving the wrong branch. 5am is the same hour
    // "Interact resolves to LOOK" already uses and already know is quiet.
    const sim::gull::GuestRoom& room = sim::gull::kRooms[1];

    SUBCASE("upright") {
        Session session(gullAt(5, room.standX, room.standY, sim::gull::kUpperBand));
        REQUIRE(session.stance() == sim::Stance::Upright);
        REQUIRE(session.tavern().nearestTo(session.body().x(), session.body().y(),
                                           sim::kReachQ8) == nullptr);
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
        Session session(gullAt(5, room.standX, room.standY, sim::gull::kUpperBand));
        session.setCrouched(true);
        REQUIRE(session.stance() == sim::Stance::Crouched);
        REQUIRE(session.tavern().nearestTo(session.body().x(), session.body().y(),
                                           sim::kLiftReachQ8) == nullptr);
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

// ===========================================================================
// THE CROSSHAIR PASS -- the same walk, naming what it is about to act on
// ===========================================================================
//
// The owner's note, verbatim: "The 'E' button shouldn't have that label text be
// at the bottom of the screen. It needs to be improved to be properly
// contextual and when shown hover a bit to the top-right of the center
// crosshair."
//
// The VERB half was already exact and every case above proves it. These prove
// the half that is new: that the prompt names the right object, that the name
// tracks state the way the verb does, and -- the one that could quietly ruin a
// playthrough -- that asking the question does not answer it.

TEST_CASE("the crosshair names the person the key would actually speak to") {
    Session session(gullAt(11, sim::gull::kBartenderX, sim::gull::kBarY - 1,
                           sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);

    const sim::Actor* reached =
        session.tavern().nearestTo(session.body().x(), session.body().y(), sim::kReachQ8);
    REQUIRE(reached != nullptr);

    const Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "TALK");
    CHECK(aim.kind == AimKind::Person);
    // THE BODY THE KEY REACHES, NOT "SOMEBODY IN THE ROOM". A prompt that
    // named the wrong one of two people in a doorway would be worse than the
    // bare verb it replaced, so the name is checked against the very pointer
    // interact() itself resolves through.
    CHECK(aim.subject == reached->name());
    CHECK(aim.note == std::string(sim::actorRoleName(reached->role())));
    CHECK_FALSE(aim.subject.empty());

    // AND interactPrompt() IS STILL EXACTLY THIS TARGET'S VERB. One walk, one
    // description of the resolution order -- see interactTarget()'s header.
    CHECK(session.interactPrompt() == aim.verb);

    // CROUCH: the verb changes, the person does not.
    session.body().placeAt(sim::gull::kBartenderX + 1, sim::gull::kBartenderY,
                           sim::gull::kGroundBand);
    session.setCrouched(true);
    const Session::InteractTarget lifted = session.interactTarget();
    CHECK(lifted.verb == "PICKPOCKET");
    CHECK(lifted.kind == AimKind::Person);
    CHECK(lifted.subject == aim.subject);
}

TEST_CASE("the crosshair names the box, and state changes the note with the verb") {
    const sim::gull::GuestRoom& room = sim::gull::kRooms[1];
    Session session(gullAt(5, room.standX, room.standY, sim::gull::kUpperBand));
    REQUIRE(session.tavern().nearestTo(session.body().x(), session.body().y(),
                                       sim::kReachQ8) == nullptr);

    const Session::InteractTarget shut = session.interactTarget();
    CHECK(shut.verb == "PICK LOCK");
    CHECK(shut.kind == AimKind::Thing);
    CHECK(shut.subject == "THE STRONGBOX");
    // ROOM 2 IS kRooms[1] -- the note counts the way a guest does, from one.
    CHECK(shut.note == "ROOM 2  LOCKED");

    // AND THE PLAYER'S OWN BED SAYS WHOSE IT IS RATHER THAN OFFERING A KEY
    // THAT REFUSES. "never a greyed-out disabled button" is the reference's
    // own rule; here the verb stays live and the NOTE carries the state.
    Session mine(gullAt(21, sim::gull::kBartenderX, sim::gull::kBartenderY,
                        sim::gull::kGroundBand));
    mine.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    REQUIRE(rentFirstRoom(mine));
    const sim::gull::GuestRoom& own = sim::gull::kRooms[0];
    mine.body().placeAt(own.standX, own.standY, sim::gull::kUpperBand);
    const Session::InteractTarget bed = mine.interactTarget();
    CHECK(bed.verb == "REST");
    CHECK(bed.subject == "YOUR BED");
    CHECK(bed.note == "ROOM 1");
    CHECK(bed.kind == AimKind::Thing);
}

TEST_CASE("the crosshair names a lead the book has heard of, and naming it does not read it") {
    // THE MISSION BACK ROOM -- the casebook's one `start` lead, so it is Open
    // from the first minute of the game and is exactly what a new player is
    // sent to find. The site is authored at (126,110) on the ground band.
    Session session(gullAt(11, 126, 110, sim::gull::kGroundBand));
    REQUIRE(session.casebook().active());

    const int lead = session.leadInLookReach();
    REQUIRE(lead >= 0);
    const sim::Lead& site = session.casebook().raws()->leads()[static_cast<std::size_t>(lead)];
    REQUIRE(session.casebook().state(static_cast<std::int32_t>(lead)) == sim::LeadState::Open);

    const Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "LOOK");
    CHECK(aim.kind == AimKind::Clue);
    CHECK(aim.subject == site.what);
    // ABSENCE COSTS NOTHING: an unread lead is the ordinary case and says
    // nothing extra about itself.
    CHECK(aim.note.empty());

    // THE ONE THAT COULD RUIN A PLAYTHROUGH. Casebook::look() OPENS what the
    // lead opens, stamps heardAt() and moves the ward's dread. Drawing a HUD
    // label must not do any of that -- so ask a hundred times and prove the
    // book has not moved a single field.
    const std::int32_t readBefore = session.casebook().readCount();
    const std::int32_t dreadBefore = session.casebook().dread();
    const std::size_t knownBefore = session.casebook().known().size();
    for (int i = 0; i < 100; ++i) {
        (void)session.interactTarget();
    }
    CHECK(session.casebook().readCount() == readBefore);
    CHECK(session.casebook().dread() == dreadBefore);
    CHECK(session.casebook().known().size() == knownBefore);
    CHECK(session.casebook().state(static_cast<std::int32_t>(lead)) == sim::LeadState::Open);

    // NOW ACTUALLY LOOK, and the note changes with the state -- the
    // reference's own "state changes the row, the label and the verb
    // together", on the smallest surface in the game.
    session.examine();
    CHECK(session.casebook().state(static_cast<std::int32_t>(lead)) != sim::LeadState::Open);
    const Session::InteractTarget after = session.interactTarget();
    CHECK(after.subject == site.what);
    CHECK(after.note == "ALREADY READ");
    CHECK(after.kind == AimKind::Clue);
}

TEST_CASE("the crosshair names the building it is pointed at, and only that one") {
    // THE RAY, PROVED FROM THE PAVEMENT. The Gilded Gull's authored footprint
    // is x 146..160, y 66..79 (docks_signs_generated.hpp), so every candidate
    // below stands OUTSIDE it, on the Tarwalk -- which is what makes this a
    // test of the line of sight rather than of "the place I happen to be
    // standing in".
    //
    // THE STAND-OFF IS FOUND, NOT ASSUMED. A body in reach outranks a building
    // in interact()'s own order and always should -- so rather than hoping a
    // hand-picked tile is empty at a hand-picked hour, the case walks the
    // pavement outside the door until it finds a spot where the key really
    // would resolve to LOOK, and says so loudly if the ward has filled all of
    // them. Four in the morning is the quiet hour --burgle already uses.
    Session session(gullAt(4, 153, 63, sim::gull::kGroundBand, 180));
    bool stood = false;
    for (std::int32_t y = 65; y >= 63 && !stood; --y) {
        for (std::int32_t x = 148; x <= 158 && !stood; ++x) {
            session.body().placeAt(x, y, sim::gull::kGroundBand);
            stood = session.interactPrompt() == "LOOK";
        }
    }
    REQUIRE(stood);

    // FACING THE DOOR: the building names itself.
    session.body().setYaw(sim::angle_from_degrees(180));
    const Session::InteractTarget facing = session.interactTarget();
    CHECK(facing.verb == "LOOK");
    CHECK(facing.kind == AimKind::Place);
    CHECK(facing.subject == "The Gilded Gull");

    // TURN ROUND AND IT IS NOT THE GULL ANY MORE. A prompt that answered "the
    // nearest building" rather than "the one under the reticle" would pass
    // every check above and still be wrong in play.
    session.body().setYaw(sim::angle_from_degrees(0));
    const Session::InteractTarget away = session.interactTarget();
    CHECK(away.subject != "The Gilded Gull");

    // AND A STREET IS NEVER THE SUBJECT. The compass ribbon prints the ward's
    // street name every frame; a crosshair repeating it would be the HUD
    // saying one thing twice, so ways are skipped by the walk on purpose.
    CHECK(facing.subject != "Tarwalk");
    CHECK(away.subject != "Tarwalk");
}

TEST_CASE("the aim prompt stands down whole when a page owns the keyboard") {
    Session session(gullAt(11, sim::gull::kBartenderX, sim::gull::kBarY - 1,
                           sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    REQUIRE_FALSE(session.interactTarget().verb.empty());
    REQUIRE_FALSE(session.interactTarget().subject.empty());

    // EVERY PART GOES, NOT JUST THE VERB. hud.cpp draws nothing at all with an
    // empty verb, reticle included, and that is what keeps the centre-clear
    // guarantee true under every page in the game -- but a subject left behind
    // here would still be a name Session was carrying for a row nobody draws.
    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());
    const Session::InteractTarget under = session.interactTarget();
    CHECK(under.verb.empty());
    CHECK(under.subject.empty());
    CHECK(under.note.empty());
    CHECK(under.kind == AimKind::Nothing);
}

TEST_CASE("no row of the aim prompt ever says the same word twice") {
    // THE WARD HAS BODIES WHOSE NAME IS THEIR TRADE. The first capture of the
    // crosshair over one of the district's mice read "MOUSE  MOUSE" -- a
    // qualifier qualifying nothing, holding twice the width to say one thing,
    // which is exactly the defect polish-1 deleted "NOBODY IN PARTICULAR" for.
    //
    // A SWEEP AND NOT THE ONE TILE THAT CAUGHT IT. The rule belongs to all
    // eight exits of the walk (see Session::resolveInteract), so this walks a
    // grid across the whole district at an hour when the ward is out and
    // holds every answer to it. Deterministic: same world, same hour, same
    // tiles, and nothing here moves the simulation.
    Session session(gullAt(20, 153, 63, sim::gull::kGroundBand));
    std::size_t named = 0;
    for (std::int32_t y = 40; y <= 120; y += 7) {
        for (std::int32_t x = 40; x <= 200; x += 7) {
            session.body().placeAt(x, y, sim::gull::kGroundBand);
            const Session::InteractTarget aim = session.interactTarget();
            if (aim.subject.empty()) {
                continue;
            }
            ++named;
            INFO("at (", x, ",", y, ") the crosshair said \"", aim.subject, "\" / \"", aim.note,
                 "\"");
            CHECK_FALSE(aim.note == aim.subject);
            // AND A NOTE NEVER STANDS ALONE. It is a qualifier; without the
            // thing it qualifies it is a word with no referent on the loudest
            // spot on the frame.
            CHECK_FALSE(aim.subject.empty());
        }
    }
    // NOT VACUOUS: the sweep really did land on things with names.
    CHECK(named > 20);
}
