// THE KIT -- the item record, what is on the body, what is on the floor and
// what is on the dead, proved at the seams the Oblivion-feel gap analysis
// named and D10 ruled:
//
//   THE REGISTRY   content/raws/items/items.json loads, every id the game
//                  names is a row, every hand item's span is its class's own
//                  (the sheet and strike() can never disagree), the five sack
//                  rows agree with contraband.cpp's numbers, and a row whose
//                  span lies is refused at load.
//   THE CODEC      the Kit round-trips byte for byte (counts, worn rows), by
//                  id (an appended raw does not move a saved Kit), and an id
//                  the raws lost refuses the whole image.
//   THE HASH       every count and every worn row moves the room's hash.
//   THE LOAD       240 at the base sheet, the legs slow past half, TAKE
//                  refuses past the budget with the numbers on the row.
//   THE HAND       grantPlayerWeapon puts THE EVICTOR in the Kit and the hand
//                  reads Evictor; BARE reads Fists; setPlayerCombat outranks.
//   THE VERBS      TAKE off an authored stand, THEIRS is a lift under the
//                  witness rule, DROP puts it back on the tile, WEAR/BARE.
//   THE DEAD       a corpse's search list is its role's authored kit less
//                  what was taken; the dead do not witness the taking.
//   DEFENCE v1     a worn coat turns a flat DR off a landed blow, never below
//                  one, and HARNESS trains on it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/items.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/shops.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// test_combat_action's Room: the engine, the room and a body the test
/// places on purpose, sixty steps to the tick.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         std::int32_t band = gull::kGroundBand, Angle yaw = kFacingSouth,
         std::uint64_t seed = 0x4752414E41444144ull)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(seed, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, band, yaw)),
          yaw_(yaw) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, seed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        push();
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] PlayerBody& body() noexcept { return *body_; }
    [[nodiscard]] const TileQuery& tiles() const noexcept { return *tiles_; }

    void stepOnce() {
        push();
        tavern_->stepMovement();
    }
    void second() {
        for (int i = 0; i < kStepsPerSecond; ++i) {
            stepOnce();
        }
        push();
        engine_->tick();
    }
    void run(int seconds) {
        for (int s = 0; s < seconds; ++s) {
            second();
        }
    }
    void settleToIdle() {
        for (int i = 0; i < kHardSwingRecoverySteps + 4 && !tavern_->playerCombatIdle(); ++i) {
            stepOnce();
        }
    }
    Tavern::PlayerSwingResult swing(bool hard) {
        settleToIdle();
        tavern_->playerAttackDown();
        const int hold = hard ? kHardSwingHoldSteps + 1 : 1;
        for (int i = 0; i < hold; ++i) {
            stepOnce();
        }
        return tavern_->playerAttackUp();
    }
    void setYaw(Angle yaw) {
        yaw_ = yaw & (kTurnFull - 1);
        body_->setYaw(yaw_);
        push();
    }
    void placeAt(std::int32_t x, std::int32_t y, std::int32_t band) {
        body_->placeAt(x, y, band);
        push();
    }
    [[nodiscard]] const Actor* findRole(ActorRole role) const {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.present() && !isFloored(actor.activity()) &&
                actor.role() != ActorRole::Vermin && actor.role() == role) {
                return &actor;
            }
        }
        return nullptr;
    }
    Angle standFacing(const Actor& mark) {
        const std::int32_t ax = mark.tileX();
        const std::int32_t ay = mark.tileY();
        const std::int32_t band = mark.band();
        struct Side {
            std::int32_t dx;
            std::int32_t dy;
            Angle yaw;
        };
        const Side sides[] = {
            {0, -1, kFacingSouth},
            {0, 1, kFacingNorth},
            {-1, 0, kFacingEast},
            {1, 0, kFacingWest},
        };
        for (const Side& s : sides) {
            const std::int32_t px = ax + s.dx;
            const std::int32_t py = ay + s.dy;
            if (tiles_->standable(px, py, band)) {
                body_->placeAt(px, py, band);
                setYaw(s.yaw);
                return s.yaw;
            }
        }
        return -1;
    }
    [[nodiscard]] std::uint64_t hash() const {
        HashSink sink(0x4B4954ull);
        tavern_->hash_into(sink);
        return sink.finished();
    }

private:
    void push() {
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
        tavern_->setPlayerYaw(yaw_);
    }

    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
    Angle yaw_;
};

/// A registry over a small vocabulary, with no file.
constexpr std::string_view kSmallRaws = R"({
  "items": [
    {"id": "cudgel", "name": "CUDGEL", "drams": 40, "royals": 6, "slot": "hand", "class": "blunt"},
    {"id": "coat", "name": "COAT", "drams": 60, "royals": 12, "slot": "body", "dr": 2},
    {"id": "hood", "name": "HOOD", "drams": 12, "royals": 3, "slot": "head", "dr": 1},
    {"id": "rope", "name": "ROPE", "drams": 48, "royals": 6}
  ]
})";

/// Kills the first present patron the way the combat cases do: steel, hard
/// swings on the sightline until one kills. Returns the corpse id or -1.
[[nodiscard]] std::int32_t killAPatron(Room& room) {
    const Actor* mark = room.findRole(ActorRole::Patron);
    if (mark == nullptr || room.standFacing(*mark) == -1) {
        return -1;
    }
    room.tavern().setPlayerCombat(Weapon::Edged, Intent::Subdue);
    for (int swings = 0; swings < 16; ++swings) {
        const Tavern::PlayerSwingResult result = room.swing(true);
        if (result.killed) {
            return result.targetId;
        }
    }
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// the registry
// ---------------------------------------------------------------------------

TEST_CASE("the item raws load: every id the game names is a row, no row is refused") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    for (const std::string& warning : items.warnings()) {
        FAIL_CHECK("refused: " << warning);
    }
    const char* named[] = {"cudgel", "the_evictor", "mace",    "bottle",  "knife",     "dagger",
                           "cutlass", "boat_hook",  "coat",    "hood",    "boots",     "purse",
                           "lantern", "rope",       "picks",   "letter",  "bale",      "strongbox",
                           "scalp",   "dust",       "moonshine", "flower", "artifact"};
    for (const char* id : named) {
        INFO("id: ", id);
        CHECK(items.find(id) != nullptr);
    }
    // THE EVICTOR is a row by the id the eviction case grants it by.
    const ItemDef* evictor = items.find(kEvictorWeaponId);
    REQUIRE(evictor != nullptr);
    CHECK(evictor->weaponClass == Weapon::Evictor);
    CHECK(evictor->slot == ItemSlot::Hand);
    CHECK(itemSheetLine(*evictor) == "THE EVICTOR 7-9 IMPACT");
    // Every row is a docker's thing: a name, a weight, and a line about it.
    for (const ItemDef& item : items.items()) {
        INFO("row: ", item.id);
        CHECK_FALSE(item.name.empty());
        CHECK_FALSE(item.desc.empty());
        CHECK(item.drams >= 0);
        // No em-dash, no colon elaboration in a spoken row.
        CHECK(item.desc.find("\xE2\x80\x94") == std::string::npos);
        for (const char c : item.name) {
            CHECK((c < 'a' || c > 'z'));
        }
    }
}

TEST_CASE("every hand item's span is its class's own, so the sheet and the roll agree") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    int hands = 0;
    for (const ItemDef& item : items.items()) {
        if (item.slot != ItemSlot::Hand) {
            continue;
        }
        ++hands;
        INFO("row: ", item.id);
        CHECK(item.damageLo == baseDamage(item.weaponClass));
        CHECK(item.damageHi == baseDamage(item.weaponClass) + kStrikeVarianceMax);
        // The class word on the line is the brawl line's: EDGE above it,
        // IMPACT below.
        const std::string line = itemSheetLine(item);
        CHECK(line.rfind(item.name, 0) == 0);
        if (item.weaponClass >= kFirstLethalWeapon) {
            CHECK(line.find(" EDGE") != std::string::npos);
        } else {
            CHECK(line.find(" IMPACT") != std::string::npos);
        }
    }
    CHECK(hands >= 8);
}

TEST_CASE("the five sack rows agree with contraband.cpp's weights, worths and heat") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    int faces = 0;
    for (const ItemDef& item : items.items()) {
        if (item.contraband.empty()) {
            continue;
        }
        ++faces;
        Contraband good = Contraband::Scalp;
        INFO("row: ", item.id);
        REQUIRE(contrabandFromSymbol(item.contraband, good));
        CHECK(item.drams == contrabandWeight(good));
        CHECK(item.royals == contrabandValue(good));
        CHECK(item.heat == contrabandHeat(good));
        CHECK(item.name == contrabandLabelFor(good, 1));
    }
    CHECK(faces == static_cast<int>(kContrabandCount));
}

TEST_CASE("a hand row whose authored span disagrees with its class is refused at load") {
    const ItemRegistry items = ItemRegistry::fromJson(R"({"items": [
        {"id": "lying_knife", "name": "KNIFE", "drams": 8, "slot": "hand", "class": "edged", "damage": [1, 99]},
        {"id": "honest_knife", "name": "KNIFE", "drams": 8, "slot": "hand", "class": "edged", "damage": [11, 13]},
        {"id": "quiet_knife", "name": "KNIFE", "drams": 8, "slot": "hand", "class": "edged"}
    ]})");
    CHECK(items.find("lying_knife") == nullptr);
    REQUIRE(items.find("honest_knife") != nullptr);
    REQUIRE(items.find("quiet_knife") != nullptr);
    CHECK(items.find("quiet_knife")->damageLo == 11);
    CHECK(items.find("quiet_knife")->damageHi == 13);
    REQUIRE(items.warnings().size() == 1);
    CHECK(items.warnings()[0].find("lying_knife") != std::string::npos);
    // And a stand for a thing the raws do not know is dropped, named.
    const ItemRegistry stands = ItemRegistry::fromJson(R"({"items": [
        {"id": "rope", "name": "ROPE", "drams": 48}],
        "stands": [{"item": "rope", "x": 1, "y": 2, "band": 3}, {"item": "anvil", "x": 1, "y": 2, "band": 3}]})");
    CHECK(stands.stands().size() == 1);
    CHECK(stands.warnings().size() == 1);
}

TEST_CASE("every authored stand is a standable tile of the baked Docks, and a corpse kit names only rows") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    const content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    const TileQuery tiles(world);
    REQUIRE_FALSE(items.stands().empty());
    for (const ItemStand& stand : items.stands()) {
        INFO("stand: ", stand.item, " at ", stand.x, ",", stand.y, " band ", stand.band);
        CHECK(tiles.standable(stand.x, stand.y, stand.band));
        CHECK(items.find(stand.item) != nullptr);
    }
    // The roster's roles all have a kit, and every named kit is a roster name.
    for (const char* role : {"patron", "bartender", "innkeeper", "bouncer", "priest", "skyrunner"}) {
        INFO("role: ", role);
        const CorpseKit* kit = items.carriedBy(role);
        REQUIRE(kit != nullptr);
        CHECK_FALSE(kit->items.empty());
        for (const std::string& id : kit->items) {
            CHECK(items.find(id) != nullptr);
        }
    }
    CHECK(items.carriedBy("vermin") == nullptr);
}

// ---------------------------------------------------------------------------
// the codec and the hash
// ---------------------------------------------------------------------------

TEST_CASE("the Kit round-trips through its codec, by id, and refuses an id the raws lost") {
    const ItemRegistry items = ItemRegistry::fromJson(kSmallRaws);
    REQUIRE(items.size() == 4);
    Kit kit(items.size());
    kit.add(items.indexOf("cudgel"), 1);
    kit.add(items.indexOf("coat"), 2);
    kit.add(items.indexOf("rope"), 3);
    REQUIRE(kit.wear(items, items.indexOf("cudgel")));
    REQUIRE(kit.wear(items, items.indexOf("coat")));
    CHECK(kit.units() == 6);
    CHECK(kit.weight(items) == 40 + 120 + 144);
    CHECK(kit.wornDr(items) == 2);
    CHECK(kit.handClass(items) == Weapon::Blunt);

    const std::vector<std::uint8_t> bytes = kit.encode(items);
    Kit back;
    REQUIRE(Kit::decode(bytes, items, back));
    CHECK(back.encode(items) == bytes);
    CHECK(back.count(items.indexOf("cudgel")) == 1);
    CHECK(back.count(items.indexOf("coat")) == 2);
    CHECK(back.count(items.indexOf("rope")) == 3);
    CHECK(back.worn(ItemSlot::Hand) == items.indexOf("cudgel"));
    CHECK(back.worn(ItemSlot::Body) == items.indexOf("coat"));
    CHECK(back.worn(ItemSlot::Head) == -1);

    // BY ID: the same bytes decode against a registry with a row APPENDED
    // and one INSERTED at the front -- the indices moved, the Kit did not.
    const ItemRegistry grown = ItemRegistry::fromJson(R"({"items": [
        {"id": "lantern", "name": "LANTERN", "drams": 30},
        {"id": "cudgel", "name": "CUDGEL", "drams": 40, "slot": "hand", "class": "blunt"},
        {"id": "coat", "name": "COAT", "drams": 60, "slot": "body", "dr": 2},
        {"id": "hood", "name": "HOOD", "drams": 12, "slot": "head", "dr": 1},
        {"id": "rope", "name": "ROPE", "drams": 48},
        {"id": "boots", "name": "BOOTS", "drams": 30, "slot": "feet", "dr": 1}
    ]})");
    Kit moved;
    REQUIRE(Kit::decode(bytes, grown, moved));
    CHECK(moved.count(grown.indexOf("rope")) == 3);
    CHECK(moved.worn(ItemSlot::Hand) == grown.indexOf("cudgel"));
    CHECK(moved.count(grown.indexOf("lantern")) == 0);

    // A registry that LOST the rope refuses the whole image.
    const ItemRegistry lost = ItemRegistry::fromJson(R"({"items": [
        {"id": "cudgel", "name": "CUDGEL", "drams": 40, "slot": "hand", "class": "blunt"},
        {"id": "coat", "name": "COAT", "drams": 60, "slot": "body", "dr": 2}
    ]})");
    Kit refused;
    CHECK_FALSE(Kit::decode(bytes, lost, refused));
    // Truncated, wrong magic, trailing bytes: all refused.
    std::vector<std::uint8_t> short_(bytes.begin(), bytes.end() - 1);
    CHECK_FALSE(Kit::decode(short_, items, refused));
    std::vector<std::uint8_t> wrong = bytes;
    wrong[1] = 'X';
    CHECK_FALSE(Kit::decode(wrong, items, refused));
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(Kit::decode(trailing, items, refused));

    // The ground list's codec, the same shape.
    std::vector<GroundItem> ground;
    ground.push_back(GroundItem{5, 6, 7, items.indexOf("rope"), 2, true});
    ground.push_back(GroundItem{1, 1, 1, items.indexOf("hood"), 1, false});
    const std::vector<std::uint8_t> groundBytes = encodeGround(ground, items);
    std::vector<GroundItem> groundBack;
    REQUIRE(decodeGround(groundBytes, items, groundBack));
    REQUIRE(groundBack.size() == 2);
    CHECK(groundBack[0].x == 5);
    CHECK(groundBack[0].owned);
    CHECK(groundBack[1].item == items.indexOf("hood"));
    CHECK(encodeGround(groundBack, items) == groundBytes);
}

TEST_CASE("a Kit taken to zero bares the slot it was worn in, and wear refuses the wrong slot") {
    const ItemRegistry items = ItemRegistry::fromJson(kSmallRaws);
    Kit kit(items.size());
    const std::int32_t coat = items.indexOf("coat");
    const std::int32_t rope = items.indexOf("rope");
    CHECK_FALSE(kit.wear(items, coat));  // not carried
    kit.add(coat, 1);
    CHECK(kit.wear(items, coat));
    CHECK(kit.isWorn(coat));
    CHECK_FALSE(kit.wear(items, rope));  // no slot
    kit.add(rope, 1);
    CHECK_FALSE(kit.wear(items, rope));
    CHECK(kit.take(coat, 1) == 1);
    CHECK(kit.worn(ItemSlot::Body) == -1);
    CHECK_FALSE(kit.isWorn(coat));
    CHECK(kit.wornDr(items) == 0);
}

TEST_CASE("every count and every worn row is in the room's hash") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.items().loaded());
    const std::uint64_t bare = room.hash();
    REQUIRE(tavern.giveItem("coat"));
    const std::uint64_t carried = room.hash();
    CHECK(carried != bare);
    REQUIRE(tavern.wearItem(tavern.items().indexOf("coat")).result == ServiceResult::Served);
    const std::uint64_t worn = room.hash();
    CHECK(worn != carried);
    // A drop moves the ground list AND the Kit: a different number again,
    // and never the bare one, because the coat is on the floor now.
    REQUIRE(tavern.dropItem(tavern.items().indexOf("coat")).result == ServiceResult::Served);
    const std::uint64_t dropped = room.hash();
    CHECK(dropped != worn);
    CHECK(dropped != bare);
}

// ---------------------------------------------------------------------------
// the load
// ---------------------------------------------------------------------------

TEST_CASE("the load: 240 drams at the base sheet, the legs slow past half, TAKE refuses past it") {
    CHECK(loadCapDrams(40) == 240);
    CHECK(loadCapDrams(40) == kStashDrams);
    CHECK(loadCapDrams(60) == 280);
    CHECK(loadSpeedScaleQ8(0, 240) == 256);
    CHECK(loadSpeedScaleQ8(120, 240) == 256);
    CHECK(loadSpeedScaleQ8(180, 240) == 192);
    CHECK(loadSpeedScaleQ8(240, 240) == kLoadSpeedFloorQ8);
    CHECK(loadSpeedScaleQ8(400, 240) == kLoadSpeedFloorQ8);
    CHECK(loadSpeedScaleQ8(10, 0) == 256);

    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.items().loaded());
    CHECK(tavern.loadBudget() == 240);
    CHECK(tavern.loadDrams() == kStartingPicks * tavern.items().find("picks")->drams);
    CHECK(tavern.loadSpeedQ8() == 256);
    // Four coils of rope: 192 drams, the legs at 192/256.
    for (int i = 0; i < 4; ++i) {
        REQUIRE(tavern.giveItem("rope"));
    }
    CHECK(tavern.loadDrams() == 192 + kStartingPicks);
    CHECK(tavern.loadSpeedQ8() < 256);
    CHECK(tavern.loadSpeedQ8() > kLoadSpeedFloorQ8);
    // A fifth would be 240 + picks: past the budget, refused with the
    // numbers on the row.
    CHECK_FALSE(tavern.giveItem("rope"));
    CHECK(tavern.loadDrams() == 192 + kStartingPicks);
    // Coin never weighs anything.
    tavern.setPlayerCoin(tavern.playerCoin() + 1000);
    CHECK(tavern.loadDrams() == 192 + kStartingPicks);
    // And the sack counts: a jar of quayfire is 24 drams of the same budget.
    REQUIRE(tavern.giveItem("moonshine"));
    CHECK(tavern.loadDrams() == 192 + kStartingPicks + 24);
    CHECK(tavern.dialogue().crimes().stash().count(Contraband::Moonshine) == 1);
}

// ---------------------------------------------------------------------------
// the hand
// ---------------------------------------------------------------------------

TEST_CASE("grantPlayerWeapon puts THE EVICTOR in the Kit and the hand reads it; bared, fists") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.items().loaded());
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK(tavern.heldItem() == nullptr);
    REQUIRE(tavern.grantPlayerWeapon(kEvictorWeaponId));
    CHECK(tavern.playerWeapon() == Weapon::Evictor);
    CHECK(tavern.playerHeldWeapon() == Weapon::Evictor);
    REQUIRE(tavern.heldItem() != nullptr);
    CHECK(tavern.heldItem()->id == kEvictorWeaponId);
    CHECK(tavern.kit().count(tavern.items().indexOf(kEvictorWeaponId)) == 1);
    CHECK(tavern.loadDrams() >= 44);
    // The tile's press bares it: fists again, the thing still carried.
    const std::int32_t index = tavern.items().indexOf(kEvictorWeaponId);
    CHECK(tavern.wearItem(index).line == "THE EVICTOR - OFF.");
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK(tavern.kit().count(index) == 1);
    CHECK(tavern.wearItem(index).line == "IN HAND - THE EVICTOR 7-9 IMPACT.");
    CHECK(tavern.playerWeapon() == Weapon::Evictor);
    // The sheet that arms a hand directly outranks the Kit, and bares it.
    tavern.setPlayerCombat(Weapon::Edged, Intent::Kill);
    CHECK(tavern.playerWeapon() == Weapon::Edged);
    CHECK(tavern.heldItem() == nullptr);
    CHECK(tavern.kit().count(index) == 1);
    // An unknown id refuses rather than disarming.
    CHECK_FALSE(tavern.grantPlayerWeapon("the_flame_itself"));
    // A row that is not a hand item refuses too.
    CHECK_FALSE(tavern.grantPlayerWeapon("coat"));
}

TEST_CASE("a quick slot holds a spell OR an item: an item slot wears the thing, never a lantern") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    const std::int32_t cudgel = tavern.items().indexOf("cudgel");
    const std::int32_t lantern = tavern.items().indexOf("lantern");
    CHECK_FALSE(tavern.bindItemToSlot(2, cudgel));  // not carried
    REQUIRE(tavern.giveItem("cudgel"));
    REQUIRE(tavern.giveItem("lantern"));
    CHECK(tavern.bindItemToSlot(2, cudgel));
    CHECK_FALSE(tavern.bindItemToSlot(3, lantern));  // no slot: not a promise the key can keep
    CHECK(tavern.slotSpell(2) == nullptr);
    REQUIRE(tavern.slotItem(2) != nullptr);
    CHECK(tavern.slotItem(2)->id == "cudgel");
    CHECK(tavern.slotItemIndex(2) == cudgel);
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK(tavern.equipSlot(2));
    CHECK(tavern.playerWeapon() == Weapon::Blunt);
    CHECK(tavern.equipSlot(2));  // already worn: still true, still worn
    CHECK(tavern.playerWeapon() == Weapon::Blunt);
    // Dropped: the slot names a thing no longer carried, and refuses.
    REQUIRE(tavern.dropItem(cudgel).result == ServiceResult::Served);
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK_FALSE(tavern.equipSlot(2));
    CHECK(tavern.clearSlot(2));
    CHECK(tavern.slotItem(2) == nullptr);
}

// ---------------------------------------------------------------------------
// the verbs
// ---------------------------------------------------------------------------

TEST_CASE("TAKE off an authored stand: the cudgel behind the bar is THEIRS and the lift is witnessed") {
    // Two in the afternoon: Gerta is at her post, the taproom has people in
    // it, and the cudgel is under the bar at her feet. The player behind the
    // bar, a tile south of her, within reach.
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBartenderY + 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    REQUIRE(room.tiles().standable(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand));
    const std::int32_t at = tavern.groundItemInReach();
    REQUIRE(at >= 0);
    const GroundItem& stand = tavern.groundItems()[static_cast<std::size_t>(at)];
    REQUIRE(tavern.items().at(stand.item) != nullptr);
    CHECK(tavern.items().at(stand.item)->id == "cudgel");
    CHECK(stand.owned);
    const std::size_t before = tavern.groundItems().size();
    const std::int32_t heatBefore = tavern.dialogue().crimes().heat();
    const std::int32_t liftsBefore = tavern.dialogue().crimes().tally(Crime::Lift);
    const Tavern::StealResult took = tavern.takeGroundItem();
    CHECK(took.result == ServiceResult::Served);
    CHECK(took.line.rfind("TAKEN - CUDGEL. THEIRS", 0) == 0);
    CHECK(tavern.groundItems().size() == before - 1);
    CHECK(tavern.kit().count(tavern.items().indexOf("cudgel")) == 1);
    // A lift on the ward's ledger, witnessed if anybody could see it -- the
    // same clause the box keeps: seen means heat, unseen means none.
    CHECK(tavern.dialogue().crimes().tally(Crime::Lift) == liftsBefore + 1);
    CHECK(took.seen == (tavern.witnessCount(kPlayerActorId) > 0));
    if (took.seen) {
        CHECK(tavern.dialogue().crimes().heat() > heatBefore);
    }
    // Not in the hand yet: TAKE carries, WEAR arms.
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    // Nothing left in reach to take.
    CHECK(tavern.takeGroundItem().result == ServiceResult::TooFar);
}

TEST_CASE("a free thing on the quay is taken with no crime, dropped back, and taken again") {
    // The rope on the Tarwalk at (151, 63), nobody's.
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    const std::int32_t rope = tavern.items().indexOf("rope");
    REQUIRE(rope >= 0);
    const std::int32_t liftsBefore = tavern.dialogue().crimes().tally(Crime::Lift);
    const Tavern::StealResult took = tavern.takeGroundItem();
    REQUIRE(took.result == ServiceResult::Served);
    CHECK(took.line == "TAKEN - ROPE. 48 DRAMS. " + std::to_string(tavern.loadDrams()) + "/240.");
    CHECK_FALSE(took.seen);
    CHECK(tavern.dialogue().crimes().tally(Crime::Lift) == liftsBefore);
    CHECK(tavern.kit().count(rope) == 1);
    CHECK(tavern.loadDrams() >= 48);
    // Wearing a rope is refused out loud; dropping it puts it on this tile.
    CHECK(tavern.wearItem(rope).result == ServiceResult::Refused);
    const Tavern::StealResult dropped = tavern.dropItem(rope);
    CHECK(dropped.result == ServiceResult::Served);
    CHECK(dropped.line == "DROPPED - ROPE.");
    CHECK(tavern.kit().count(rope) == 0);
    const std::int32_t at = tavern.groundItemInReach();
    REQUIRE(at >= 0);
    CHECK(tavern.groundItems()[static_cast<std::size_t>(at)].item == rope);
    CHECK(tavern.groundItems()[static_cast<std::size_t>(at)].x == 151);
    CHECK(tavern.groundItems()[static_cast<std::size_t>(at)].y == 63);
    CHECK_FALSE(tavern.groundItems()[static_cast<std::size_t>(at)].owned);
    CHECK(tavern.takeGroundItem().result == ServiceResult::Served);
    CHECK(tavern.kit().count(rope) == 1);
    // A second rope dropped on the same tile is one stack of two.
    REQUIRE(tavern.giveItem("rope"));
    REQUIRE(tavern.dropItem(rope).result == ServiceResult::Served);
    REQUIRE(tavern.dropItem(rope).result == ServiceResult::Served);
    std::size_t stacks = 0;
    for (const GroundItem& entry : tavern.groundItems()) {
        if (entry.item == rope && entry.x == 151 && entry.y == 63) {
            ++stacks;
            CHECK(entry.count == 2);
        }
    }
    CHECK(stacks == 1);
    CHECK(tavern.dropItem(rope).result == ServiceResult::Refused);
    // The sack and the picks are not put down by this verb.
    CHECK(tavern.dropItem(tavern.items().indexOf("picks")).line == "THAT STAYS ON YOU.");
    CHECK(tavern.dropItem(tavern.items().indexOf("dust")).line == "THAT STAYS ON YOU.");
}

TEST_CASE("TAKE refuses past the load with the numbers on the row, and the fixed thing never travels") {
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    // Load up to a coil short of the budget, then reach for the rope.
    for (int i = 0; i < 4; ++i) {
        REQUIRE(tavern.giveItem("rope"));
    }
    const Tavern::StealResult refused = tavern.takeGroundItem();
    CHECK(refused.result == ServiceResult::Refused);
    CHECK(refused.line.rfind("TOO MUCH ON YOU ALREADY. ", 0) == 0);
    CHECK(refused.line.find(" OF 240 DRAMS.") != std::string::npos);
    CHECK(tavern.groundItemInReach() >= 0);  // still there
    // The strongbox has a record and a refusal, never a weight on the body.
    const ItemDef* box = tavern.items().find("strongbox");
    REQUIRE(box != nullptr);
    CHECK(box->fixed);
    CHECK_FALSE(tavern.giveItem("strongbox"));
}

TEST_CASE("WEAR puts a coat on the back and a hood on the head; both print through the slot rows") {
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.giveItem("coat"));
    REQUIRE(tavern.giveItem("hood"));
    REQUIRE(tavern.giveItem("boots"));
    const std::int32_t coat = tavern.items().indexOf("coat");
    const std::int32_t hood = tavern.items().indexOf("hood");
    const std::int32_t boots = tavern.items().indexOf("boots");
    CHECK(tavern.wornDr() == 0);
    CHECK(tavern.wearItem(coat).line == "COAT - ON.");
    CHECK(tavern.wearItem(hood).line == "HOOD - ON.");
    CHECK(tavern.wearItem(boots).line == "BOOTS - ON.");
    CHECK(tavern.kit().worn(ItemSlot::Body) == coat);
    CHECK(tavern.kit().worn(ItemSlot::Head) == hood);
    CHECK(tavern.kit().worn(ItemSlot::Feet) == boots);
    CHECK(tavern.wornDr() == 4);
    // The raws hold a thing for every printed slot.
    CHECK(tavern.items().slotHasItems(ItemSlot::Hand));
    CHECK(tavern.items().slotHasItems(ItemSlot::Body));
    CHECK(tavern.items().slotHasItems(ItemSlot::Head));
    CHECK(tavern.items().slotHasItems(ItemSlot::Feet));
    CHECK(tavern.items().slotHasItems(ItemSlot::Trinket));
    CHECK(itemSlotSheetLabel(ItemSlot::Body) == "ON THE BACK");
    // Dropping a worn coat bares the back.
    REQUIRE(tavern.dropItem(coat).result == ServiceResult::Served);
    CHECK(tavern.kit().worn(ItemSlot::Body) == -1);
    CHECK(tavern.wornDr() == 2);
}

// ---------------------------------------------------------------------------
// the dead
// ---------------------------------------------------------------------------

TEST_CASE("a corpse's search list is its authored kit less what was taken; the dead do not witness") {
    Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    const std::int32_t victim = killAPatron(room);
    REQUIRE(victim >= 0);
    const Actor* corpse = tavern.actorById(victim);
    REQUIRE(corpse != nullptr);
    REQUIRE(corpse->activity() == Activity::Dead);
    // Standing over him: he is the search subject.
    room.placeAt(corpse->tileX(), corpse->tileY(), corpse->band());
    const Actor* subject = tavern.corpseInReach();
    REQUIRE(subject != nullptr);
    CHECK(subject->id() == victim);
    // A patron carries a knife and a purse -- or his own named kit.
    const std::vector<Tavern::CorpseRow> rows = tavern.corpseRows(victim);
    REQUIRE_FALSE(rows.empty());
    const CorpseKit* kit = tavern.items().carriedBy(corpse->name());
    if (kit == nullptr) {
        kit = tavern.items().carriedBy("patron");
    }
    REQUIRE(kit != nullptr);
    REQUIRE(rows.size() == kit->items.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CHECK(rows[i].kitRow == static_cast<std::int32_t>(i));
        CHECK(tavern.items().at(rows[i].item)->id == kit->items[i]);
    }
    const std::int32_t liftsBefore = tavern.dialogue().crimes().tally(Crime::Lift);
    const std::int32_t heatBefore = tavern.dialogue().crimes().heat();
    // Take the first row: it leaves the list, the mask remembers it, no
    // crime is recorded and no heat rises.
    const Tavern::StealResult took = tavern.takeFromCorpse(victim, rows[0].kitRow);
    REQUIRE(took.result == ServiceResult::Served);
    CHECK(took.line.rfind("TAKEN - ", 0) == 0);
    CHECK(tavern.corpseRows(victim).size() == rows.size() - 1);
    CHECK(tavern.takeFromCorpse(victim, rows[0].kitRow).result == ServiceResult::OutOfStock);
    CHECK(tavern.dialogue().crimes().tally(Crime::Lift) == liftsBefore);
    CHECK(tavern.dialogue().crimes().heat() == heatBefore);
    REQUIRE(tavern.corpseLoot().size() == 1);
    CHECK(tavern.corpseLoot()[0].actorId == victim);
    CHECK(tavern.corpseLoot()[0].takenMask == 1U);
    // TAKE ALL: every row goes, the list is empty, the mask is full.
    for (const Tavern::CorpseRow& row : tavern.corpseRows(victim)) {
        CHECK(tavern.takeFromCorpse(victim, row.kitRow).result == ServiceResult::Served);
    }
    CHECK(tavern.corpseRows(victim).empty());
    // A living man is never a search subject, and a body across the room is
    // out of reach: the quay apron, two tiles clear of the threshold.
    room.placeAt(gull::kStreetX, gull::kStreetY, gull::kGroundBand);
    CHECK(tavern.corpseInReach() == nullptr);
}

// ---------------------------------------------------------------------------
// defence v1
// ---------------------------------------------------------------------------

TEST_CASE("a worn coat turns a flat DR off a landed blow, never below one, and harness trains on it") {
    // Twin rooms, same seed: one in a coat and a hood (DR 3), one bare.
    // Start a brawl in both the same way and let the room swing; every blow
    // that lands on the coated body loses three, floored at one.
    auto fight = [](bool coated, std::int32_t& blowsTurned, std::int32_t& harness) {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
        Tavern& tavern = room.tavern();
        room.run(2);
        if (coated) {
            REQUIRE(tavern.giveItem("coat"));
            REQUIRE(tavern.giveItem("hood"));
            REQUIRE(tavern.wearItem(tavern.items().indexOf("coat")).result == ServiceResult::Served);
            REQUIRE(tavern.wearItem(tavern.items().indexOf("hood")).result == ServiceResult::Served);
            REQUIRE(tavern.wornDr() == 3);
        }
        const std::int32_t start = tavern.playerHp();
        const Actor* mark = room.findRole(ActorRole::Patron);
        REQUIRE(mark != nullptr);
        REQUIRE(room.standFacing(*mark) != -1);
        // A tap-swing brawl: he joins and swings back on his own cadence.
        for (int swings = 0; swings < 3; ++swings) {
            (void)room.swing(false);
        }
        room.run(8);
        blowsTurned = tavern.blowsTurned();
        harness = tavern.dialogue().skills().level(kHarnessSkill) +
                  tavern.dialogue().skills().find(kHarnessSkill)->uses;
        return start - tavern.playerHp();
    };
    std::int32_t turnedBare = 0;
    std::int32_t harnessBare = 0;
    const std::int32_t lostBare = fight(false, turnedBare, harnessBare);
    std::int32_t turnedCoat = 0;
    std::int32_t harnessCoat = 0;
    const std::int32_t lostCoat = fight(true, turnedCoat, harnessCoat);
    INFO("bare lost ", lostBare, " coated lost ", lostCoat, " blows turned ", turnedCoat);
    CHECK(turnedBare == 0);
    CHECK(harnessBare == 0);
    REQUIRE(lostBare > 0);
    // The same roll landed in both rooms; the coat only argued what it was
    // worth, so the coated body lost less and never nothing.
    CHECK(lostCoat < lostBare);
    CHECK(lostCoat > 0);
    CHECK(turnedCoat > 0);
    CHECK(harnessCoat > 0);
}

// ===========================================================================
// SHOPS (2026-10-09): the counters that sell a docker's things, and the one
// that buys them back
// ===========================================================================

TEST_CASE("the shop raws load: three counters on the gazetteer's sites, every shelf row an item") {
    const ShopRaws shops = ShopRaws::load(content::contentDir());
    REQUIRE(shops.loaded());
    REQUIRE(shops.counters().size() == 3);
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    for (const char* keeper : {"brann", "neddry", "fenner"}) {
        INFO(keeper);
        const ShopCounter* counter = shops.forKeeper(keeper);
        REQUIRE(counter != nullptr);
        CHECK_FALSE(counter->name.empty());
        for (const std::string& id : counter->sells) {
            INFO(id);
            const ItemDef* item = items.find(id);
            REQUIRE(item != nullptr);
            CHECK_FALSE(item->fixed);
            CHECK(item->contraband.empty());
            CHECK(item->royals > 0);
        }
    }
    // Only the pawn buys back, and never a case's own reward.
    CHECK_FALSE(shops.forKeeper("brann")->buys());
    CHECK_FALSE(shops.forKeeper("neddry")->buys());
    const ShopCounter* pawn = shops.forKeeper("fenner");
    CHECK(pawn->buys());
    CHECK(std::find(pawn->neverBuys.begin(), pawn->neverBuys.end(), "the_evictor") !=
          pawn->neverBuys.end());
    CHECK(shops.forKeeper("cull") == nullptr);
}

TEST_CASE("a counter with no keeper, no name or nothing to trade is dropped; a seen id is not overwritten") {
    const ShopRaws shops = ShopRaws::fromJson(R"({"counters": [
        {"id": "a", "keeper": "brann", "name": "A", "sells": ["rope"]},
        {"id": "a", "keeper": "neddry", "name": "Shadow", "sells": ["coat"]},
        {"id": "b", "keeper": "", "name": "B", "sells": ["rope"]},
        {"id": "c", "keeper": "fenner", "name": "C"},
        {"id": "d", "keeper": "fenner", "name": "D", "buysPercent": 250}
    ]})");
    REQUIRE(shops.counters().size() == 2);
    CHECK(shops.counters()[0].name == "A");
    CHECK(shops.counters()[1].id == "d");
    // A percentage past the whole is clamped to it, not honoured.
    CHECK(shops.counters()[1].buysPercent == 100);
    CHECK_FALSE(ShopRaws::fromJson("not json").loaded());
}

TEST_CASE("a counter's price is the item's worth through the guild term, never under one royal") {
    CHECK(counterPrice(9, 0) == 9);
    CHECK(counterPrice(9, -10) == 8);   // a member's rate, truncated
    CHECK(counterPrice(9, 25) == 11);   // a strong house pricing a stranger up
    CHECK(counterPrice(1, -50) == 1);   // never given away
    CHECK(counterOffer(9, 50) == 4);
    CHECK(counterOffer(1, 50) == 1);    // a thing worth anything fetches something
    CHECK(counterOffer(0, 50) == 0);    // a thing worth nothing fetches nothing
    CHECK(counterOffer(9, 0) == 0);     // a counter that does not buy
}

TEST_CASE("buying across a counter: the purse, the load, the Kit, and the hash that sees it") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    const std::int32_t lantern = tavern.items().indexOf("lantern");
    REQUIRE(lantern >= 0);

    const CounterView view = tavern.counterViewFor("brann", true);
    CHECK(view.keeps);
    CHECK(view.atCounter);
    CHECK_FALSE(view.buys);
    CHECK(view.name == "Brann's Chandlery");
    bool listed = false;
    for (const CounterWare& ware : view.sells) {
        listed = listed || ware.itemId == "lantern";
    }
    CHECK(listed);
    CHECK_FALSE(tavern.counterViewFor("cull", true).keeps);

    std::string line;
    tavern.setPlayerCoin(3);
    CHECK(tavern.buyFromCounter("lantern", 9, line) == ServiceResult::NoCoin);
    CHECK(line == "YOUR PURSE WILL NOT COVER IT.");
    CHECK(tavern.kit().count(lantern) == 0);

    tavern.setPlayerCoin(50);
    const std::uint64_t before = room.hash();
    CHECK(tavern.buyFromCounter("lantern", 9, line) == ServiceResult::Served);
    CHECK(line == "PAID 9C FOR THE LANTERN.");
    CHECK(tavern.kit().count(lantern) == 1);
    CHECK(tavern.playerCoin() == 41);
    // A purchase the twin-run gate could not see is one it does not protect.
    CHECK(room.hash() != before);

    // Past the load, refused with TAKE's own numbers, and nothing moves.
    tavern.setPlayerCoin(10000);
    std::int32_t bought = 0;
    while (tavern.buyFromCounter("lantern", 9, line) == ServiceResult::Served && bought < 100) {
        ++bought;
    }
    CHECK(bought < 100);
    CHECK(line.rfind("TOO MUCH ON YOU ALREADY. ", 0) == 0);
}

TEST_CASE("selling to the pawn: carried and unworn only, at its offer, and never its never list") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    tavern.setPlayerCoin(100);
    std::string line;
    REQUIRE(tavern.buyFromCounter("knife", 4, line) == ServiceResult::Served);
    REQUIRE(tavern.giveItem("the_evictor"));
    const std::int32_t knife = tavern.items().indexOf("knife");

    CounterView pawn = tavern.counterViewFor("fenner", true);
    REQUIRE(pawn.buys);
    std::int32_t offer = 0;
    bool evictor = false;
    for (const CounterWare& ware : pawn.pawnable) {
        if (ware.itemId == "knife") {
            offer = ware.royals;
        }
        evictor = evictor || ware.itemId == "the_evictor";
    }
    CHECK(offer == counterOffer(tavern.items().at(knife)->royals, 50));
    CHECK_FALSE(evictor);

    // Worn is not for sale: take it off first.
    tavern.wearItem(knife);
    REQUIRE(tavern.kit().isWorn(knife));
    pawn = tavern.counterViewFor("fenner", true);
    for (const CounterWare& ware : pawn.pawnable) {
        CHECK(ware.itemId != "knife");
    }
    CHECK(tavern.sellToCounter("knife", offer, line) == ServiceResult::Refused);
    CHECK(line == "TAKE IT OFF FIRST.");

    tavern.wearItem(knife);  // the same press bares it
    REQUIRE_FALSE(tavern.kit().isWorn(knife));
    const std::int32_t coin = tavern.playerCoin();
    CHECK(tavern.sellToCounter("knife", offer, line) == ServiceResult::Served);
    CHECK(line == "SOLD THE KNIFE FOR " + std::to_string(offer) + "C.");
    CHECK(tavern.kit().count(knife) == 0);
    CHECK(tavern.playerCoin() == coin + offer);
    CHECK(tavern.sellToCounter("knife", offer, line) == ServiceResult::Refused);
}

TEST_CASE("a keeper offers the shelf on their own list, and away from the counter says where it is") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    DialogueDirector& talk = tavern.dialogue();
    Speaker brann;
    brann.actorId = 9001;
    brann.name = "Brann";
    brann.notableId = "brann";
    brann.family = JobFamily::Trade;

    const auto indexOf = [&talk](TopicKind kind) {
        for (std::size_t i = 0; i < talk.topics().size(); ++i) {
            if (talk.topics()[i].kind == kind) {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    };

    // At the counter: SEE THE WARES opens the shelf, every row priced.
    talk.setCounter(tavern.counterViewFor("brann", true));
    REQUIRE(talk.open(brann, hourOfDay(14)));
    const std::int32_t wares = indexOf(TopicKind::Wares);
    REQUIRE(wares >= 0);
    CHECK(indexOf(TopicKind::Pawn) < 0);
    (void)talk.choose(static_cast<std::size_t>(wares));
    CHECK(talk.menu() == DialogueMenu::Wares);
    REQUIRE(indexOf(TopicKind::BuyItem) >= 0);
    for (const Topic& topic : talk.topics()) {
        if (topic.kind == TopicKind::BuyItem) {
            CHECK(topic.payload > 0);
            CHECK(topic.label.find(" (") != std::string::npos);
            CHECK(topic.label.back() == ')');
        }
    }
    // A purse that cannot cover it is refused by the counter's own sentence.
    talk.setPlayerCoin(0);
    const Reply broke = talk.choose(static_cast<std::size_t>(indexOf(TopicKind::BuyItem)));
    CHECK_FALSE(broke.ok);
    CHECK(broke.line == "YOUR PURSE WILL NOT COVER IT.");
    // Back goes to the root.
    (void)talk.choose(static_cast<std::size_t>(indexOf(TopicKind::Back)));
    CHECK(talk.menu() == DialogueMenu::Root);
    talk.close();
    CHECK_FALSE(talk.counter().keeps);

    // Met away from the counter: the row is there, and it says where to go.
    talk.setCounter(tavern.counterViewFor("brann", false));
    REQUIRE(talk.open(brann, hourOfDay(23)));
    const Reply away = talk.choose(static_cast<std::size_t>(indexOf(TopicKind::Wares)));
    CHECK(away.line == "NOT OUT HERE. COME BY BRANN'S CHANDLERY.");
    CHECK(talk.menu() == DialogueMenu::Root);
    talk.close();

    // Nobody else has a shelf.
    Speaker stranger = brann;
    stranger.notableId.clear();
    talk.setCounter(tavern.counterViewFor("", true));
    REQUIRE(talk.open(stranger, hourOfDay(14)));
    CHECK(indexOf(TopicKind::Wares) < 0);
    talk.close();
}
