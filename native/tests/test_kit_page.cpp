// THE KIT, ON THE SCREEN -- the session side of the Kit build, driven the way
// test_interact_context and test_grimoire drive theirs: straight at Session,
// because Session owns the interact walk, the prompt, the Character tile's
// verbs and the Grimoire widget the search list wears, and none of it
// touches SDL.
//
//   THE PROMPT     TAKE names the thing on the ground; TAKE QUIETLY crouched;
//                  THEIRS with the Owned accent before the press when taking
//                  it is theft; a weight note when it is nobody's; a named
//                  lead in reach outranks it; the press does what the prompt
//                  said.
//   THE SEARCH     SEARCH <NAME>  DEAD over a corpse; the press opens the
//                  list on the Grimoire widget; the rows are his kit; ENTER
//                  and the number take; TAKE ALL is the last row; an emptied
//                  body closes the list; the wheel's tap never reopens it.
//   THE LEGS       the load slows the body at the one movement seam.
//   THE ROW        a blow the coat turned says so once; the hands row names
//                  the item in the hand.
//
// The keyboard-to-Session wiring in main.cpp (X to drop, LEFT/RIGHT to slot)
// is NOT covered here, for the reason every other page gives: closing that
// hole needs a real SDL harness.

#include <doctest/doctest.h>

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/items.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

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

/// Kills the first present patron through the real verbs: steel, walk up,
/// face, hard swings until one kills. The corpse id, or -1.
[[nodiscard]] std::int32_t killAPatron(Session& session) {
    const sim::Actor* mark = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.present() && !sim::isFloored(actor.activity()) &&
            actor.role() == sim::ActorRole::Patron) {
            mark = &actor;
            break;
        }
    }
    if (mark == nullptr) {
        return -1;
    }
    const std::int32_t id = mark->id();
    const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    const int yaws[4] = {180, 0, 90, 270};
    for (int side = 0; side < 4; ++side) {
        const std::int32_t px = mark->tileX() + sides[side][0];
        const std::int32_t py = mark->tileY() + sides[side][1];
        if (!session.tiles().standable(px, py, mark->band())) {
            continue;
        }
        session.body().placeAt(px, py, mark->band());
        session.body().setYaw(sim::angle_from_degrees(yaws[side]));
        break;
    }
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
    for (int swings = 0; swings < 20; ++swings) {
        // A whole hard swing through the session's own edges, the recovery
        // stepped through.
        session.attackDown();
        session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
        session.attackUp();
        session.stepMany(sim::MoveInput{}, sim::kHardSwingRecoverySteps + 2);
        const sim::Actor* struck = session.tavern().actorById(id);
        if (struck != nullptr && struck->activity() == sim::Activity::Dead) {
            return id;
        }
        // He shifts his feet; face him again.
        if (struck != nullptr) {
            session.body().placeAt(session.body().tileX(), session.body().tileY(),
                                   session.body().band());
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("the crosshair names the thing on the ground, its weight when it is nobody's, THEIRS when it is not") {
    // The rope on the Tarwalk at (151, 63), nobody's: TAKE ROPE  48DR, a
    // Thing's accent. Four in the morning so no body outranks it.
    Session session(gullAt(4, 151, 63, sim::gull::kGroundBand));
    REQUIRE(session.tavern().items().loaded());
    Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "TAKE");
    CHECK(aim.subject == "ROPE");
    CHECK(aim.note == "48DR");
    CHECK(aim.kind == AimKind::Thing);
    // Crouched, the verb says how.
    session.setCrouched(true);
    REQUIRE(session.stance() == sim::Stance::Crouched);
    aim = session.interactTarget();
    CHECK(aim.verb == "TAKE QUIETLY");
    CHECK(aim.subject == "ROPE");
    session.setCrouched(false);

    // THE PRESS does what the prompt said: the rope is on the body, the
    // row says so, and the crosshair falls back to whatever is left.
    session.interact();
    CHECK(session.lastMessage() ==
          "TAKEN - ROPE. 48 DRAMS. " + std::to_string(session.tavern().loadDrams()) + "/240.");
    CHECK(session.tavern().kit().count(session.tavern().items().indexOf("rope")) == 1);
    CHECK(session.interactTarget().verb != "TAKE");

    // THE OWNERSHIP CUE. The cudgel under the bar at (152, 72) is the
    // house's: behind the bar a tile south of it, with Gerta at her post,
    // the crosshair says THEIRS in the Owned accent BEFORE the press --
    // but she outranks it while she is the nearer thing, so stand where the
    // cudgel is in reach and she is not the body the walk finds first.
    Session night(gullAt(4, sim::gull::kBartenderX, sim::gull::kBartenderY + 1,
                         sim::gull::kGroundBand, 0));
    REQUIRE(night.tavern().presentCount() == 0);
    const Session::InteractTarget theirs = night.interactTarget();
    CHECK(theirs.verb == "TAKE");
    CHECK(theirs.subject == "CUDGEL");
    CHECK(theirs.note == "THEIRS");
    CHECK(theirs.kind == AimKind::Owned);
    night.interact();
    CHECK(night.lastMessage().rfind("TAKEN - CUDGEL. THEIRS", 0) == 0);
    CHECK(night.tavern().kit().count(night.tavern().items().indexOf("cudgel")) == 1);
}

TEST_CASE("a named lead in reach outranks a thing on the ground, and the strongbox is never a TAKE") {
    // The Mission back room's lead at (126,110), Open from the first minute
    // -- drop a rope on it and the crosshair still names the lead.
    Session session(gullAt(11, 126, 110, sim::gull::kGroundBand));
    REQUIRE(session.casebook().active());
    REQUIRE(session.leadInLookReach() >= 0);
    REQUIRE(session.tavern().giveItem("rope"));
    REQUIRE(session.tavern().dropItem(session.tavern().items().indexOf("rope")).result ==
            sim::ServiceResult::Served);
    REQUIRE(session.tavern().groundItemInReach() >= 0);
    const Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "LOOK");
    CHECK(aim.kind == AimKind::Clue);
    session.interact();
    CHECK(session.casebook().readCount() > 0);
    CHECK(session.tavern().groundItemInReach() >= 0);  // still on the ground

    // The box's stand keeps its own prompt: PICK LOCK, never TAKE STRONGBOX.
    Session stair(gullAt(5, sim::gull::kRooms[0].standX, sim::gull::kRooms[0].standY,
                         sim::gull::kUpperBand));
    const Session::InteractTarget box = stair.interactTarget();
    CHECK(box.verb == "PICK LOCK");
    CHECK(box.subject == "THE STRONGBOX");
}

TEST_CASE("a corpse in reach is SEARCH <NAME>  DEAD, and the press opens his kit on the list widget") {
    Session session(gullAt(19, sim::gull::kBartenderX, sim::gull::kBarY - 1, sim::gull::kGroundBand));
    session.stepMany(sim::MoveInput{}, 2 * sim::kStepsPerSecond);
    const std::int32_t victim = killAPatron(session);
    REQUIRE(victim >= 0);
    const sim::Actor* corpse = session.tavern().actorById(victim);
    REQUIRE(corpse != nullptr);
    REQUIRE(corpse->activity() == sim::Activity::Dead);
    // Over him.
    session.body().placeAt(corpse->tileX(), corpse->tileY(), corpse->band());
    session.tavern().setPlayer(session.body().x(), session.body().y(), session.body().band());
    // The fight's own lull: hands down, so the prompt is the search and not
    // LOWER HANDS (a corpse outranks both regardless -- asserted below).
    const Session::InteractTarget aim = session.interactTarget();
    CHECK(aim.verb == "SEARCH");
    CHECK(aim.subject == corpse->name());
    CHECK(aim.note == "DEAD");
    CHECK(aim.kind == AimKind::Person);

    session.interact();
    REQUIRE(session.searchOpen());
    CHECK(session.grimoireOpen());
    CHECK(session.searchActorId() == victim);
    const std::vector<std::string> rows = session.grimoireRows();
    const std::vector<sim::Tavern::CorpseRow> carried = session.tavern().corpseRows(victim);
    REQUIRE_FALSE(carried.empty());
    REQUIRE(rows.size() == carried.size() + 1);
    CHECK(rows.back() == "TAKE ALL");
    for (std::size_t i = 0; i < carried.size(); ++i) {
        const sim::ItemDef* thing = session.tavern().items().at(carried[i].item);
        REQUIRE(thing != nullptr);
        CHECK(rows[i].rfind(thing->name + "  " + std::to_string(thing->drams) + "DR", 0) == 0);
    }
    // The widget wears his name.
    const DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    std::string shouted = corpse->name();
    for (char& c : shouted) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    CHECK(view.speaker == shouted);
    CHECK(view.topics.size() == rows.size());
    // The number takes the first row; the list is one shorter.
    session.chooseGrimoireRow(0);
    CHECK(session.lastMessage().rfind("TAKEN - ", 0) == 0);
    CHECK(session.grimoireRows().size() == rows.size() - 1);
    CHECK(session.searchOpen());
    // TAKE ALL takes the rest and the list closes over an empty body.
    const int last = static_cast<int>(session.grimoireRows().size()) - 1;
    session.chooseGrimoireRow(last);
    CHECK(session.lastMessage().rfind("TAKEN - ALL OF IT.", 0) == 0);
    CHECK_FALSE(session.searchOpen());
    CHECK_FALSE(session.grimoireOpen());
    CHECK(session.tavern().corpseRows(victim).empty());
    // Nothing on him now: the prompt still names him, the press says so
    // and closes at once.
    session.interact();
    CHECK(session.searchOpen());
    CHECK(session.grimoireRows().empty());
    session.chooseGrimoireRow(0);
    CHECK_FALSE(session.searchOpen());
    // The wheel's tap is the Grimoire, never the search list left behind.
    session.toggleGrimoire();
    CHECK(session.grimoireOpen());
    CHECK_FALSE(session.searchOpen());
    session.toggleGrimoire();
}

TEST_CASE("the load slows the legs at the movement seam, and nothing else on the body does") {
    Session light(gullAt(4, 151, 63, sim::gull::kGroundBand, 90));
    Session heavy(gullAt(4, 151, 63, sim::gull::kGroundBand, 90));
    // Four coils of rope: 192 drams of a 240 budget.
    for (int i = 0; i < 4; ++i) {
        REQUIRE(heavy.tavern().giveItem("rope"));
    }
    CHECK(heavy.tavern().loadSpeedQ8() < 256);
    CHECK(light.tavern().loadSpeedQ8() == 256);
    sim::MoveInput walk;
    walk.forward = 1;
    const auto walked = [](Session& s, std::int32_t x0, std::int32_t y0) {
        const std::int32_t dx = s.body().x() - x0;
        const std::int32_t dy = s.body().y() - y0;
        return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    };
    const std::int32_t lightX = light.body().x();
    const std::int32_t lightY = light.body().y();
    const std::int32_t heavyX = heavy.body().x();
    const std::int32_t heavyY = heavy.body().y();
    light.stepMany(walk, sim::kStepsPerSecond);
    heavy.stepMany(walk, sim::kStepsPerSecond);
    const std::int32_t lightWalked = walked(light, lightX, lightY);
    const std::int32_t heavyWalked = walked(heavy, heavyX, heavyY);
    INFO("light ", lightWalked, " heavy ", heavyWalked);
    CHECK(lightWalked > 0);
    CHECK(heavyWalked > 0);
    CHECK(heavyWalked < lightWalked);
    // Coin weighs nothing: a rich body walks like a poor one.
    Session rich(gullAt(4, 151, 63, sim::gull::kGroundBand, 90));
    rich.tavern().setPlayerCoin(9999);
    const std::int32_t richX = rich.body().x();
    const std::int32_t richY = rich.body().y();
    rich.stepMany(walk, sim::kStepsPerSecond);
    CHECK(walked(rich, richX, richY) == lightWalked);
}

TEST_CASE("the hands row names the thing in the hand, and the turn row sleeps until a blow is softened") {
    Session session(gullAt(19, sim::gull::kBartenderX, sim::gull::kBarY - 1, sim::gull::kGroundBand));
    sim::Tavern& tavern = session.tavern();
    REQUIRE(tavern.giveItem("knife"));
    REQUIRE(tavern.giveItem("coat"));
    REQUIRE(tavern.wearItem(tavern.items().indexOf("knife")).result == sim::ServiceResult::Served);
    REQUIRE(tavern.wearItem(tavern.items().indexOf("coat")).result == sim::ServiceResult::Served);
    CHECK(session.handsLine().empty());
    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(tavern.playerHandsUp());
    CHECK(session.handsLine() == "KNIFE UP");
    session.setBlocking(false);
    // The turn row is an event: asleep until the room's own counter moves.
    CHECK(session.turnLine().empty());
}

// ---------------------------------------------------------------------------
// KIT BUILD: the things on the tiles, in 3D
// ---------------------------------------------------------------------------

#include "granadad/render3d/ground_items.hpp"
#include "granadad/render3d/static_pieces.hpp"

TEST_CASE("a thing on a tile is an Item piece off the catalogue, and the placeholder rule holds") {
    namespace render3d = granadad::render3d;
    const render3d::StaticCatalogue catalogue =
        render3d::StaticCatalogue::load(render3d::staticCataloguePath(content::contentDir()));
    REQUIRE_FALSE(catalogue.empty());
    // The table dresses every item the raws put on a stand, the box and
    // the bale; the sack's own five rows are never on a tile and need no
    // row here.
    REQUIRE_FALSE(catalogue.itemIds().empty());
    CHECK(catalogue.itemPiece("knife") != nullptr);
    CHECK(catalogue.itemPiece("strongbox") != nullptr);
    CHECK(catalogue.itemPiece("bale") != nullptr);
    CHECK(catalogue.itemPiece("dust") == nullptr);
    // Every Item row indexes into the piece table where the description
    // expects it, in variant order.
    for (std::size_t v = 0; v < catalogue.itemIds().size(); ++v) {
        const render3d::PieceSpec* spec = catalogue.itemPiece(catalogue.itemIds()[v]);
        REQUIRE(spec != nullptr);
        CHECK(spec->role == render3d::PieceRole::Item);
        CHECK(spec->variant == static_cast<std::uint8_t>(v));
        CHECK(catalogue.pieceIndex(render3d::PieceRole::Item, spec->variant) >= 0);
        CHECK_FALSE(spec->file.empty());
    }
    // A weapon lies flat: pitched a quarter turn; a sack stands.
    CHECK(catalogue.itemPiece("cutlass")->pitch > 1.0F);
    CHECK(catalogue.itemPiece("bale")->pitch == 0.0F);

    // The Tarwalk at four in the morning: the rope and the boots stand on
    // the quay, the snug's bale and the four boxes are in the house. Drop a
    // knife and the frame has one thing more; take the rope and one fewer.
    Session session(gullAt(4, 151, 63, sim::gull::kGroundBand, 90));
    const std::vector<render3d::StaticInstance> before =
        render3d::groundItemInstances(session, catalogue, session.camera());
    REQUIRE_FALSE(before.empty());
    for (const render3d::StaticInstance& piece : before) {
        CHECK(piece.role == static_cast<std::uint8_t>(render3d::PieceRole::Item));
        CHECK(piece.piece < catalogue.pieces().size());
        CHECK(catalogue.pieces()[piece.piece].role == render3d::PieceRole::Item);
    }
    REQUIRE(session.tavern().giveItem("knife"));
    REQUIRE(session.tavern().dropItem(session.tavern().items().indexOf("knife")).result ==
            sim::ServiceResult::Served);
    const std::vector<render3d::StaticInstance> dropped =
        render3d::groundItemInstances(session, catalogue, session.camera());
    CHECK(dropped.size() == before.size() + 1);
    // Position from the tile, never from anything the renderer keeps: the
    // knife lies on the body's own tile at the band's surface plus its lift.
    // (The house has an authored knife on Edda's chair too; the one that is
    // new is the one on the body's own tile.)
    bool found = false;
    for (const render3d::StaticInstance& piece : dropped) {
        if (catalogue.itemIds()[catalogue.pieces()[piece.piece].variant] != "knife") {
            continue;
        }
        const bool here =
            std::abs(piece.position.x - (static_cast<float>(session.body().tileX()) + 0.5F)) < 0.6F &&
            std::abs(piece.position.z - (static_cast<float>(session.body().tileY()) + 0.5F)) < 0.6F;
        found = found || here;
    }
    CHECK(found);
    // TAKE (the rope is nearer than the knife or not, either is one fewer).
    session.interact();
    CHECK(session.lastMessage().rfind("TAKEN - ", 0) == 0);
    const std::vector<render3d::StaticInstance> taken =
        render3d::groundItemInstances(session, catalogue, session.camera());
    CHECK(taken.size() == before.size());
    // The same session described twice is the same bytes: pure over state.
    const std::vector<render3d::StaticInstance> again =
        render3d::groundItemInstances(session, catalogue, session.camera());
    REQUIRE(again.size() == taken.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        CHECK(again[i].piece == taken[i].piece);
        CHECK(again[i].position.x == taken[i].position.x);
        CHECK(again[i].yaw == taken[i].yaw);
        CHECK(again[i].tint.r == taken[i].tint.r);
    }
    // No catalogue table: no instance, nothing in the sim moved.
    const render3d::StaticCatalogue bare = render3d::StaticCatalogue::fromJson("{}");
    CHECK(render3d::groundItemInstances(session, bare, session.camera()).empty());
    CHECK(session.tavern().groundItems().size() >= 1);
}
