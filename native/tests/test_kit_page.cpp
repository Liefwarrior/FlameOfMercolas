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
    // Standing on it with a level eye, the coil is under the frame: not
    // named (Session::groundItemUnderReticle). The eye goes down onto it.
    CHECK(session.interactTarget().subject != "ROPE");
    session.body().setPitch(sim::angle_from_degrees(-85));
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
    // On the boards a tile ahead: under a level frame, so the eye goes down.
    CHECK(night.interactTarget().subject != "CUDGEL");
    night.body().setPitch(sim::angle_from_degrees(-50));
    const Session::InteractTarget theirs = night.interactTarget();
    CHECK(theirs.verb == "TAKE");
    CHECK(theirs.subject == "CUDGEL");
    CHECK(theirs.note == "THEIRS");
    CHECK(theirs.kind == AimKind::Owned);
    night.interact();
    CHECK(night.lastMessage().rfind("TAKEN - CUDGEL. THEIRS", 0) == 0);
    CHECK(night.tavern().kit().count(night.tavern().items().indexOf("cudgel")) == 1);
}

TEST_CASE("the crosshair names a thing on the ground only when the reticle is on it, and E takes only what it named") {
    // THE 2026-10-06 DIRECTOR'S FRAME (20-tarwalk-at-coil-west): at the
    // Tarwalk coil facing west, the reticle on a stall block eleven tiles
    // off and the coil under the frame's bottom edge, the prompt read ROPE
    // 48DR / E - TAKE. The read was the nearest thing in the take's reach,
    // wherever the eye was. Two tiles east of the coil, a level eye: the
    // coil is in reach and 34.6 degrees under the reticle, off the frame.
    // Four in the morning, so nobody on the quay outranks either read.
    Session session(gullAt(4, 153, 63, sim::gull::kGroundBand, 270));
    REQUIRE(session.tavern().items().loaded());
    const std::int32_t ropeRow = session.tavern().items().indexOf("rope");
    std::int32_t coil = -1;
    for (std::size_t i = 0; i < session.tavern().groundItems().size(); ++i) {
        const sim::GroundItem& entry = session.tavern().groundItems()[i];
        if (entry.item == ropeRow && entry.x == 151 && entry.y == 63) {
            coil = static_cast<std::int32_t>(i);
        }
    }
    REQUIRE(coil >= 0);
    // In the take's reach: what the old read would have named.
    CHECK(session.tavern().groundItemInReachOf(session.body().x(), session.body().y(),
                                               session.body().band()) == coil);
    // A level eye is not on it: not named, and the press does not take it.
    CHECK(session.groundItemUnderReticle() == -1);
    const Session::InteractTarget level = session.interactTarget();
    CHECK(level.subject != "ROPE");
    CHECK(level.verb != "TAKE");
    session.interact();
    if (session.talking()) {
        session.closeConversation();
    }
    CHECK(session.tavern().kit().count(ropeRow) == 0);
    // The eye down onto the coil: named, and the press takes what it named.
    session.body().setPitch(sim::angle_from_degrees(-35));
    CHECK(session.groundItemUnderReticle() == coil);
    const Session::InteractTarget down = session.interactTarget();
    CHECK(down.verb == "TAKE");
    CHECK(down.subject == "ROPE");
    session.interact();
    CHECK(session.tavern().kit().count(ropeRow) == 1);
    // And the eye past it, at the feet: the coil is behind the reticle now.
    Session past(gullAt(4, 153, 63, sim::gull::kGroundBand, 270));
    past.body().setPitch(sim::angle_from_degrees(-85));
    CHECK(past.groundItemUnderReticle() == -1);
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
    // TAKE (the rope or the knife, both underfoot: either is one fewer),
    // the eye down on them -- the reticle has to be on a thing to take it.
    session.body().setPitch(sim::angle_from_degrees(-85));
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

// ---------------------------------------------------------------------------
// KIT BUILD: a thing on a tile is the size of a thing on a tile
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

#include <nlohmann/json.hpp>

#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/world_scene.hpp"

namespace {

/// A piece file's POSITION bounds (Y up, metres -- the export pipeline's
/// convention, the one static_pieces.cpp's extents are quoted in) over every
/// primitive, off the accessors' own min/max. False when the file is not
/// here to be measured.
bool gltfBounds(const std::filesystem::path& file, float (&lo)[3], float (&hi)[3]) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    const nlohmann::json gltf = nlohmann::json::parse(in, nullptr, false);
    if (gltf.is_discarded() || !gltf.contains("meshes") || !gltf.contains("accessors")) {
        return false;
    }
    constexpr float kInf = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        lo[i] = kInf;
        hi[i] = -kInf;
    }
    bool any = false;
    for (const nlohmann::json& mesh : gltf["meshes"]) {
        if (!mesh.contains("primitives")) {
            continue;
        }
        for (const nlohmann::json& prim : mesh["primitives"]) {
            if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION")) {
                continue;
            }
            const nlohmann::json& acc = gltf["accessors"][prim["attributes"]["POSITION"].get<std::size_t>()];
            if (!acc.contains("min") || !acc.contains("max")) {
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], acc["min"][static_cast<std::size_t>(i)].get<float>());
                hi[i] = std::max(hi[i], acc["max"][static_cast<std::size_t>(i)].get<float>());
            }
            any = true;
        }
    }
    return any;
}

/// How a piece file stands at an items row's own scale and pitch: the
/// bounds' eight corners turned by the row's pitch about X the way the
/// adapter turns them (MatrixRotateX, before the yaw). `tall` is the
/// vertical span; `extent` the longest of the three spans, which is what
/// says a thing is a thing and not a speck -- a letter lies flat, 2.7 cm
/// of plank, and is still half a metre of sheet on the boards. False when
/// the file is not here. `foot` and `head` are the turned span's ends at
/// the row's scale, under and over the origin.
bool itemStanding(const std::filesystem::path& file, float pitch, float scale, float& tall, float& extent,
                  float& foot, float& head) {
    float lo[3];
    float hi[3];
    if (!gltfBounds(file, lo, hi)) {
        return false;
    }
    const float c = std::cos(pitch);
    const float s = std::sin(pitch);
    constexpr float kInf = std::numeric_limits<float>::infinity();
    float top = -kInf;
    float bottom = kInf;
    float zHi = -kInf;
    float zLo = kInf;
    for (int corner = 0; corner < 8; ++corner) {
        const float y = (corner & 2) != 0 ? hi[1] : lo[1];
        const float z = (corner & 4) != 0 ? hi[2] : lo[2];
        const float turnedY = y * c - z * s;
        const float turnedZ = y * s + z * c;
        top = std::max(top, turnedY);
        bottom = std::min(bottom, turnedY);
        zHi = std::max(zHi, turnedZ);
        zLo = std::min(zLo, turnedZ);
    }
    tall = (top - bottom) * scale;
    extent = std::max({tall, (hi[0] - lo[0]) * scale, (zHi - zLo) * scale});
    foot = bottom * scale;
    head = top * scale;
    return true;
}

/// One glTF primitive's triangles, de-indexed: three positions and three
/// UVs per triangle, read off the file's own binary buffer.
struct GltfTriangles {
    std::vector<float> y;   // 3 per triangle
    std::vector<float> uv;  // 6 per triangle
    [[nodiscard]] std::size_t count() const noexcept { return y.size() / 3; }
};

bool gltfTriangles(const std::filesystem::path& file, GltfTriangles& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    const nlohmann::json gltf = nlohmann::json::parse(in, nullptr, false);
    if (gltf.is_discarded() || !gltf.contains("buffers") || gltf["buffers"].empty()) {
        return false;
    }
    std::ifstream binIn(file.parent_path() / gltf["buffers"][0]["uri"].get<std::string>(), std::ios::binary);
    if (!binIn) {
        return false;
    }
    const std::vector<char> bin((std::istreambuf_iterator<char>(binIn)), std::istreambuf_iterator<char>());
    // An accessor's element `i`, component `k`, as a float or an index.
    const auto where = [&gltf](std::size_t accessor, std::size_t i, std::size_t width, std::size_t size) {
        const nlohmann::json& acc = gltf["accessors"][accessor];
        const nlohmann::json& view = gltf["bufferViews"][acc["bufferView"].get<std::size_t>()];
        const std::size_t stride = view.value("byteStride", width * size);
        return view.value("byteOffset", std::size_t{0}) + acc.value("byteOffset", std::size_t{0}) + i * stride;
    };
    const auto floatAt = [&bin](std::size_t at) {
        float f = 0.0F;
        if (at + sizeof(float) <= bin.size()) {
            std::memcpy(&f, bin.data() + at, sizeof(float));
        }
        return f;
    };
    for (const nlohmann::json& mesh : gltf["meshes"]) {
        for (const nlohmann::json& prim : mesh["primitives"]) {
            const std::size_t pos = prim["attributes"]["POSITION"].get<std::size_t>();
            const std::size_t tex = prim["attributes"]["TEXCOORD_0"].get<std::size_t>();
            const std::size_t ind = prim["indices"].get<std::size_t>();
            const int type = gltf["accessors"][ind]["componentType"].get<int>();
            const std::size_t isize = type == 5125 ? 4U : (type == 5123 ? 2U : 1U);
            const std::size_t n = gltf["accessors"][ind]["count"].get<std::size_t>();
            for (std::size_t k = 0; k < n; ++k) {
                const std::size_t at = where(ind, k, 1, isize);
                std::uint32_t v = 0;
                if (at + isize <= bin.size()) {
                    std::memcpy(&v, bin.data() + at, isize);  // little-endian, both toolchains
                }
                out.y.push_back(floatAt(where(pos, v, 3, 4) + 4));
                out.uv.push_back(floatAt(where(tex, v, 2, 4)));
                out.uv.push_back(floatAt(where(tex, v, 2, 4) + 4));
            }
        }
    }
    return out.count() > 0;
}

/// The adapter's own trim and glass test (rl_backend.cpp trimModel,
/// applyGlass): EVERY vertex of the triangle in the band and on the patch.
bool inAdapterCut(const GltfTriangles& t, std::size_t tri, float bandLo, float bandHi, float u0, float u1, float v0,
           float v1) {
    for (std::size_t k = 0; k < 3; ++k) {
        const float y = t.y[tri * 3 + k];
        const float u = t.uv[(tri * 3 + k) * 2];
        const float v = t.uv[(tri * 3 + k) * 2 + 1];
        if (y < bandLo || y > bandHi || u < u0 || u > u1 || v < v0 || v > v1) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("every thing the items table puts on a tile is the size of a thing: a hand's thing under 0.6 m, a fixture under 0.75, nothing a speck") {
    // THE 2026-09-25 CRITIC read a door-sized plank standing on the Tarwalk
    // as the ROPE stand's coil. It was not -- the coil is 0.32 m at its 3.4x
    // (the knot is exported nine centimetres across) and the plank is one of
    // the map's own 2x2 timber stall blocks, the one at (140,62) -- but
    // nothing pinned the table against exactly that mistake: a row's scale
    // is a bare number in the catalogue, and one written for a hand-sized
    // export against a metre-tall one would stand a wardrobe on the quay
    // with nobody the wiser. So the table is measured here, file by file,
    // at its own scale and pitch.
    //
    // Two upper bounds, because two kinds of thing: what a hand carries
    // lies or stands at the feet, under 0.6 m; the two fixtures (the
    // strongbox at a bed's foot, the snug's bale) are furniture, knee to
    // thigh, under 0.75. And one lower bound for every row, on the LONGEST
    // span, not the height (the 2026-10-06 auditor: a 3 cm floor on the
    // height failed the letter, which lies flat by design): a row scaled
    // down to nothing would pass the upper bound and draw a speck.
    //
    // WHERE THE ART IS, EVERY ROW IS MEASURED. The licensed export tree is
    // not in the docker build context (.dockerignore is an allowlist), so
    // the Linux half finds no content/art/lot-3d/static and says so; the
    // host half (scripts/verify-windows.ps1 runs dist\granadad-tests.exe
    // with GRANADAD_CONTENT_DIR at the repo's own content/, where the tree
    // is mirrored) measures every row or fails. A case that measured zero
    // rows where the art is cannot pass.
    namespace render3d = granadad::render3d;
    const render3d::StaticCatalogue catalogue =
        render3d::StaticCatalogue::load(render3d::staticCataloguePath(content::contentDir()));
    REQUIRE_FALSE(catalogue.empty());
    REQUIRE_FALSE(catalogue.itemIds().empty());
    const std::filesystem::path statics = content::contentDir() / "art" / "lot-3d" / "static";
    const bool artHere = std::filesystem::is_directory(statics);
    const sim::ItemRegistry registry = sim::ItemRegistry::load(content::contentDir());
    REQUIRE(registry.loaded());

    constexpr float kInHand = 0.6F;
    constexpr float kFixture = 0.75F;
    constexpr float kNotASpeck = 0.08F;
    std::size_t measured = 0;
    std::string unmeasured;
    for (const std::string& id : catalogue.itemIds()) {
        const render3d::PieceSpec* spec = catalogue.itemPiece(id);
        REQUIRE(spec != nullptr);
        float tall = 0.0F;
        float extent = 0.0F;
        float foot = 0.0F;
        float head = 0.0F;
        if (!itemStanding(statics / spec->file, spec->pitch, spec->scale, tall, extent, foot, head)) {
            unmeasured += " " + id;
            continue;
        }
        INFO(id, " -> ", spec->file, " at ", spec->scale, "x, pitch ", spec->pitch, ": ", tall, " m tall, ",
             extent, " m across");
        const bool fixture = id == "bale" || id == "strongbox";
        CHECK(tall <= (fixture ? kFixture : kInHand));
        CHECK(extent >= kNotASpeck);
        // THE SPAN THE CROSSHAIR AIMS AT IS THE SPAN DRAWN (the items raws'
        // drawnCm, Session::groundItemUnderReticle): the row's lift plus
        // the turned mesh at its scale -- and a trimmed row's head where
        // the trim leaves it (the table lantern's ring, not its cord).
        if (spec->trimBandHi > spec->trimBandLo) {
            CHECK(spec->pitch == 0.0F);
            GltfTriangles mesh;
            REQUIRE(gltfTriangles(statics / spec->file, mesh));
            float kept = -std::numeric_limits<float>::infinity();
            for (std::size_t tri = 0; tri < mesh.count(); ++tri) {
                if (inAdapterCut(mesh, tri, spec->trimBandLo, spec->trimBandHi, spec->trimU0, spec->trimU1,
                                 spec->trimV0, spec->trimV1)) {
                    continue;
                }
                for (std::size_t k = 0; k < 3; ++k) {
                    kept = std::max(kept, mesh.y[tri * 3 + k]);
                }
            }
            head = kept * spec->scale;
        }
        const sim::ItemDef* def = registry.find(id);
        REQUIRE(def != nullptr);
        INFO("drawn ", spec->lift + foot, " .. ", spec->lift + head, " m; drawnCm ", def->drawnLoCm, " .. ",
             def->drawnHiCm);
        CHECK(std::abs(spec->lift + foot - static_cast<float>(def->drawnLoCm) * 0.01F) <= 0.03F);
        CHECK(std::abs(spec->lift + head - static_cast<float>(def->drawnHiCm) * 0.01F) <= 0.03F);
        ++measured;
    }
    // The rope in particular, since it is the one the frame was read against.
    if (const render3d::PieceSpec* rope = catalogue.itemPiece("rope"); rope != nullptr) {
        float coil = 0.0F;
        float across = 0.0F;
        float under = 0.0F;
        float over = 0.0F;
        if (itemStanding(statics / rope->file, rope->pitch, rope->scale, coil, across, under, over)) {
            CHECK(coil < 0.4F);
        }
    }
    if (!artHere) {
        MESSAGE("skipped: no licensed static tree at " << statics.string()
                                                       << " (the docker half); the host half measures every row");
        CHECK(measured == 0U);
        return;
    }
    INFO("rows the art tree could not measure:", unmeasured);
    CHECK(unmeasured.empty());
    REQUIRE(measured == catalogue.itemIds().size());
    MESSAGE(measured << " item rows measured against " << statics.string());
}

TEST_CASE("the Gull's table lantern stands on the barrel under it, sheds its cord, and burns at night like the hung ones") {
    // THE 2026-10-06 CRITICS. The director: its bail cord stuck straight up
    // into empty air, and at 22:00 it was dark -- no flame, no glow -- while
    // the same model on the street wall burned. The auditor: "foot on the
    // barrel's head" was a claim nothing pinned, and the prop under the
    // stand is the placer's hash pick, not a fact anybody wrote down.
    namespace render3d = granadad::render3d;
    const render3d::StaticCatalogue catalogue =
        render3d::StaticCatalogue::load(render3d::staticCataloguePath(content::contentDir()));
    REQUIRE_FALSE(catalogue.empty());
    const render3d::PieceSpec* lantern = catalogue.itemPiece("lantern");
    REQUIRE(lantern != nullptr);
    const std::filesystem::path statics = content::contentDir() / "art" / "lot-3d" / "static";
    const bool artHere = std::filesystem::is_directory(statics);
    constexpr std::int32_t kStandX = 150;
    constexpr std::int32_t kStandY = 74;
    constexpr std::int32_t kStandBand = 19;
    // The stand itself is where the raws say.
    bool stood = false;
    {
        const Session probe(gullAt(14, kStandX, kStandY + 2, kStandBand, 0));
        for (const sim::GroundItem& entry : probe.tavern().groundItems()) {
            const sim::ItemDef* thing = probe.tavern().items().at(entry.item);
            stood = stood || (thing != nullptr && thing->id == "lantern" && entry.x == kStandX &&
                              entry.y == kStandY && entry.band == kStandBand);
        }
    }
    REQUIRE(stood);

    SUBCASE("its foot on the head of the prop the placer stood on its tile") {
        Session session(gullAt(14, kStandX, kStandY + 2, kStandBand, 0));
        const render3d::StaticPlacements placed =
            render3d::placeStaticPieces(session.tiles(), catalogue, session.renderer().lamps());
        const render3d::StaticPlacement* prop = nullptr;
        std::size_t props = 0;
        for (const render3d::StaticPlacement& p : placed.placements) {
            const bool hashPicked = p.role == render3d::PieceRole::PropBarrel ||
                                    p.role == render3d::PieceRole::PropCrate ||
                                    p.role == render3d::PieceRole::PropSack;
            if (hashPicked && p.lightX == kStandX && p.lightY == kStandY && p.lightZ == kStandBand) {
                prop = &p;
                ++props;
            }
        }
        // ONE prop under it, and it is the one the frames show: a barrel.
        REQUIRE(props == 1U);
        REQUIRE(prop != nullptr);
        CHECK(prop->role == render3d::PieceRole::PropBarrel);
        const render3d::PieceSpec& under = catalogue.pieces()[prop->instance.piece];
        // Under it in plan: the lantern's origin (the tile's centre, the
        // first thing on its tile) inside the barrel's own radius.
        const float dx = prop->instance.position.x - (static_cast<float>(kStandX) + 0.5F);
        const float dz = prop->instance.position.z - (static_cast<float>(kStandY) + 0.5F);
        CHECK(std::sqrt(dx * dx + dz * dz) <= under.thickness);
        float propLo[3];
        float propHi[3];
        float lampLo[3];
        float lampHi[3];
        if (!artHere) {
            MESSAGE("foot height unmeasured: no licensed static tree at " << statics.string());
            return;
        }
        REQUIRE(gltfBounds(statics / under.file, propLo, propHi));
        REQUIRE(gltfBounds(statics / lantern->file, lampLo, lampHi));
        const float head = prop->instance.position.y + propHi[1] * prop->instance.scale.y;
        const float foot = granadad::render::bandSurface(kStandBand) + lantern->lift + lampLo[1] * lantern->scale;
        INFO("barrel head ", head, " m, lantern foot ", foot, " m");
        CHECK(std::abs(foot - head) <= 0.02F);
    }

    SUBCASE("the cord the hung lantern hangs by is trimmed off the one on the table, and nothing else is") {
        // The trim is the table lantern's; the hung lantern keeps its cord.
        REQUIRE(lantern->trimBandHi > lantern->trimBandLo);
        const render3d::PieceSpec* hung = catalogue.piece(render3d::PieceRole::LampWall);
        REQUIRE(hung != nullptr);
        CHECK(hung->file == lantern->file);
        CHECK_FALSE(hung->trimBandHi > hung->trimBandLo);
        const std::vector<render3d::StaticPieceRef> refs = catalogue.pieceRefs();
        const int item = catalogue.pieceIndex(render3d::PieceRole::Item, lantern->variant);
        const int wall = catalogue.pieceIndex(render3d::PieceRole::LampWall, hung->variant);
        REQUIRE(item >= 0);
        REQUIRE(wall >= 0);
        CHECK(refs[static_cast<std::size_t>(item)].cutsTrim());
        CHECK(refs[static_cast<std::size_t>(item)].cutsGlass());
        CHECK_FALSE(refs[static_cast<std::size_t>(wall)].cutsTrim());
        if (!artHere) {
            MESSAGE("cord unmeasured: no licensed static tree at " << statics.string());
            return;
        }
        GltfTriangles mesh;
        REQUIRE(gltfTriangles(statics / lantern->file, mesh));
        float keptTop = -std::numeric_limits<float>::infinity();
        float fileTop = -std::numeric_limits<float>::infinity();
        float foot = std::numeric_limits<float>::infinity();
        float droppedLow = std::numeric_limits<float>::infinity();
        std::size_t dropped = 0;
        std::size_t glassDropped = 0;
        for (std::size_t tri = 0; tri < mesh.count(); ++tri) {
            const bool trim = inAdapterCut(mesh, tri, lantern->trimBandLo, lantern->trimBandHi, lantern->trimU0,
                                    lantern->trimU1, lantern->trimV0, lantern->trimV1);
            const bool glass = inAdapterCut(mesh, tri, lantern->glassBandLo, lantern->glassBandHi, lantern->glassU0,
                                     lantern->glassU1, lantern->glassV0, lantern->glassV1);
            for (std::size_t k = 0; k < 3; ++k) {
                const float y = mesh.y[tri * 3 + k];
                fileTop = std::max(fileTop, y);
                foot = std::min(foot, y);
                if (trim) {
                    droppedLow = std::min(droppedLow, y);
                } else {
                    keptTop = std::max(keptTop, y);
                }
            }
            dropped += trim ? 1U : 0U;
            glassDropped += trim && glass ? 1U : 0U;
        }
        const float scale = lantern->scale;
        INFO("dropped ", dropped, " of ", mesh.count(), "; kept top ", (keptTop - foot) * scale,
             " m over the foot, the file's ", (fileTop - foot) * scale);
        // The cord goes: something is dropped, all of it above the glass,
        // none of it glass, and the lamp loses the better part of two
        // hands of height (the cord stood ~0.19 m over the ring at 0.45x).
        CHECK(dropped > 0U);
        CHECK(glassDropped == 0U);
        CHECK(droppedLow > lantern->glassBandHi);
        CHECK((fileTop - keptTop) * scale >= 0.15F);
        // What is left is a lantern: base, glass, cap and its ring.
        CHECK((keptTop - foot) * scale <= 0.36F);
        CHECK((keptTop - foot) * scale >= 0.3F);
    }

    SUBCASE("after dark it burns: a halo of its own, its glass hot, its metal under the glass") {
        // The eye across the room, seven tiles south of the stand, so the
        // halo's up-close fade (faceHaloToEye) is spent and what is left is
        // the hour's own rule. Pinned weather: clear (SessionConfig's
        // default), so no fog takes a share.
        const auto viewFrom = [](const Session& s) {
            granadad::render::Camera view = s.camera();
            view.x = static_cast<float>(kStandX) + 0.5F;
            view.y = static_cast<float>(kStandY) + 7.5F;
            view.z = granadad::render::bandSurface(kStandBand) + 1.6F;
            return view;
        };
        const auto lanternOf = [&catalogue, lantern](const std::vector<render3d::StaticInstance>& things) {
            const render3d::StaticInstance* found = nullptr;
            const int index = catalogue.pieceIndex(render3d::PieceRole::Item, lantern->variant);
            for (const render3d::StaticInstance& t : things) {
                if (static_cast<int>(t.piece) == index &&
                    std::abs(t.position.x - (static_cast<float>(kStandX) + 0.5F)) < 0.3F &&
                    std::abs(t.position.z - (static_cast<float>(kStandY) + 0.5F)) < 0.3F) {
                    found = &t;
                }
            }
            return found;
        };
        const int flameIndex = catalogue.pieceIndex(render3d::PieceRole::Flame);
        REQUIRE(flameIndex >= 0);

        Session night(gullAt(22, kStandX, kStandY + 2, kStandBand, 0));
        const granadad::render::Camera nightView = viewFrom(night);
        const std::vector<render3d::StaticInstance> halos =
            render3d::groundItemHalos(night, catalogue, nightView);
        REQUIRE(halos.size() == 1U);
        const render3d::StaticInstance& halo = halos.front();
        CHECK(halo.role == static_cast<std::uint8_t>(render3d::PieceRole::Flame));
        CHECK(static_cast<int>(halo.piece) == flameIndex);
        CHECK(halo.mode == render3d::kDrawHalo);
        // The hung lanterns' own night alpha: the knob's 210, near whole.
        INFO("halo alpha at 22:00 ", static_cast<int>(halo.tint.a));
        CHECK(halo.tint.a >= 150);
        // Its centre on the flame in the glass, not on the ring over it:
        // the quad's corner is half a width and half a height off.
        const render3d::PieceSpec* flame = catalogue.piece(render3d::PieceRole::Flame);
        REQUIRE(flame != nullptr);
        const float glassMid = granadad::render::bandSurface(kStandBand) + lantern->lift +
                               0.5F * (lantern->glassBandLo + lantern->glassBandHi) * lantern->scale;
        const float h = flame->height * halo.scale.y;
        CHECK(std::abs(halo.position.y + 0.5F * h * std::cos(halo.pitch) - flame->lift - glassMid) <= 0.05F);
        // The body: glass hot, metal capped under it.
        const std::vector<render3d::StaticInstance> things =
            render3d::groundItemInstances(night, catalogue, nightView);
        const render3d::StaticInstance* lit = lanternOf(things);
        REQUIRE(lit != nullptr);
        CHECK(lit->pane.r == 255);
        CHECK(lit->pane.g >= 230);
        CHECK(static_cast<int>(lit->tint.r) <= static_cast<int>(lantern->tint.r) / 2 + 1);
        CHECK(static_cast<int>(lit->tint.r) < static_cast<int>(lit->pane.r));

        // By day: a trace of a halo (the hour's own share), dark glass, and
        // the metal at its full catalogue tint, as the hung ones by day.
        Session day(gullAt(14, kStandX, kStandY + 2, kStandBand, 0));
        const granadad::render::Camera dayView = viewFrom(day);
        const std::vector<render3d::StaticInstance> dayHalos = render3d::groundItemHalos(day, catalogue, dayView);
        REQUIRE(dayHalos.size() == 1U);
        INFO("halo alpha at 14:00 ", static_cast<int>(dayHalos.front().tint.a), " against ",
             static_cast<int>(halo.tint.a), " at 22:00");
        CHECK(static_cast<int>(dayHalos.front().tint.a) * 3 <= static_cast<int>(halo.tint.a));
        const std::vector<render3d::StaticInstance> dayThings =
            render3d::groundItemInstances(day, catalogue, dayView);
        const render3d::StaticInstance* cold = lanternOf(dayThings);
        REQUIRE(cold != nullptr);
        CHECK(cold->pane.r < 100);
        CHECK(cold->tint.r == lantern->tint.r);

        // No catalogue, no halo.
        const render3d::StaticCatalogue bare = render3d::StaticCatalogue::fromJson("{}");
        CHECK(render3d::groundItemHalos(night, bare, nightView).empty());
    }
}
