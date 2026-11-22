// PHASE 9 -- WEAPONS AND EQUIPMENT (OBLIVION-PLAN 8.3), proved at the seams
// the contract names:
//
//   THE LOAD       items_gear.json reads after items_consumables.json: the
//                  first file's 23 rows and the consumables keep every index,
//                  the gear lands after them, nothing is refused, and its
//                  stands are standable, nobody's, and clear of the Tarwalk
//                  tiles the phase 8 crosshair cases stand on.
//   THE SLOTS      OffHand and Hands are APPENDED: no shipped ordinal moves,
//                  every slot and grip spelling round-trips, the screen order
//                  is the plan's.
//   THE GRIP       one-handed IS the shipped swing (reach, the 36/54 lockouts,
//                  the 15-step hold); a two-handed row reaches further, winds
//                  longer and recovers slower, on the player's own machine.
//   THE SPAN       strikeWithSpan over a class's span is strike() to the bit,
//                  and a gear row rolls inside its own span and nowhere else.
//   THE RULE       a two-handed row bares the shield; a shield bares the
//                  two-hander; wouldBare says so before the press.
//   DEFENCE        DR sums every slot but the hand, flat, floored at one.
//   THE CODECS     the Loadout's own "GL" image; the Kit's version 3 carries
//                  it; version 1 and 2 images still decode, the new slots bare.
//   THE HASH       a body with nothing in the new slots hashes the five-slot
//                  Kit's words exactly (built here by hand); gloves move it.
//   THE COMPARISON compareGear is the wear rules run on a copy.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/items.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// test_kit's Room: the engine, the room and a body the test places on
/// purpose. settleToIdle waits as long as any grip's lockout can run.
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
        for (int i = 0; i < 200 && !tavern_->playerCombatIdle(); ++i) {
            stepOnce();
        }
    }
    /// Charges for `holdSteps` steps, releases, and counts the steps until
    /// the hand is free again: the lockout the swing bought. -1 when the
    /// release threw nothing.
    int swingAndRecover(int holdSteps) {
        settleToIdle();
        tavern_->playerAttackDown();
        for (int i = 0; i < holdSteps; ++i) {
            stepOnce();
        }
        const Tavern::PlayerSwingResult result = tavern_->playerAttackUp();
        if (!result.swung) {
            return -1;
        }
        int steps = 0;
        while (!tavern_->playerCombatIdle() && steps < 1000) {
            stepOnce();
            ++steps;
        }
        return steps;
    }
    void setYaw(Angle yaw) {
        yaw_ = yaw & (kTurnFull - 1);
        body_->setYaw(yaw_);
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
        HashSink sink(0x4745415Full);
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

/// A first file with a row for every shipped slot, no file on disk.
constexpr std::string_view kGearBase = R"({
  "items": [
    {"id": "cudgel", "name": "CUDGEL", "drams": 40, "slot": "hand", "class": "blunt"},
    {"id": "coat", "name": "COAT", "drams": 60, "slot": "body", "dr": 2},
    {"id": "hood", "name": "HOOD", "drams": 12, "slot": "head", "dr": 1},
    {"id": "boots", "name": "BOOTS", "drams": 30, "slot": "feet", "dr": 1},
    {"id": "purse", "name": "PURSE", "drams": 2, "slot": "trinket"},
    {"id": "rope", "name": "ROPE", "drams": 48}
  ]
})";

/// An extra file in items_gear.json's shape: a two-hander and a one-hander
/// with their own spans, a shield, gloves, a helmet.
constexpr std::string_view kGearExtra = R"({
  "items": [
    {"id": "maul", "name": "MAUL", "drams": 80, "slot": "hand", "class": "blunt", "grip": "two", "damage": [10, 16], "model": "maul"},
    {"id": "hatchet", "name": "HATCHET", "drams": 26, "slot": "hand", "class": "edged", "grip": "one", "damage": [9, 14]},
    {"id": "buckler", "name": "BUCKLER", "drams": 18, "slot": "offhand", "grip": "shield", "dr": 1},
    {"id": "gloves", "name": "GLOVES", "drams": 4, "slot": "hands", "dr": 1},
    {"id": "pot", "name": "POT", "drams": 26, "slot": "head", "dr": 2}
  ]
})";

[[nodiscard]] ItemRegistry gearRegistry() {
    ItemRegistry items = ItemRegistry::fromJson(kGearBase);
    items.appendJson(kGearExtra);
    return items;
}

constexpr std::uint64_t kHashSalt = 0x4B4954ull;

[[nodiscard]] std::uint64_t kitHash(const Kit& kit, std::size_t legacyRows) {
    HashSink sink(kHashSalt);
    kit.hashInto(sink, legacyRows);
    return sink.finished();
}

/// The codec's own little-endian i32 and length-prefixed id, for images
/// built by hand.
void putI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFU));
    }
}

void putId(std::vector<std::uint8_t>& out, std::string_view id) {
    out.push_back(static_cast<std::uint8_t>(id.size()));
    for (const char c : id) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
}

/// splitmix64: rolls with every bit stirred, so the whiff band, the
/// variance and the crown band all get exercised.
[[nodiscard]] std::uint64_t mixed(std::uint64_t n) {
    std::uint64_t z = n * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

/// The gear ids, in items_gear.json's order.
constexpr const char* kGearIds[] = {"hatchet",  "maul",       "boarding_axe", "buckler",
                                    "round_shield", "wool_cap", "iron_pot",   "gambeson",
                                    "brigandine", "work_gloves", "gauntlets"};

}  // namespace

// ---------------------------------------------------------------------------
// the load
// ---------------------------------------------------------------------------

TEST_CASE("the gear file loads after the consumables: every earlier index holds, nothing is refused") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    for (const std::string& warning : items.warnings()) {
        FAIL_CHECK("refused: " << warning);
    }
    // items.json is the base and stays 23 rows; the consumables keep the
    // indices phase 8 gave them; the gear comes after every one.
    CHECK(items.baseRows() == 23);
    CHECK(items.indexOf("cudgel") == 0);
    CHECK(items.indexOf("artifact") == 22);
    CHECK(items.indexOf("bread") == 23);
    CHECK(items.indexOf("ale") == 24);
    CHECK(items.indexOf("bandage") == 25);
    std::int32_t at = 26;
    for (const char* id : kGearIds) {
        INFO("gear row: ", id);
        CHECK(items.indexOf(id) == at);
        const ItemDef* row = items.find(id);
        REQUIRE(row != nullptr);
        // The render key is the row's own id.
        CHECK(row->model == std::string(id));
        CHECK_FALSE(row->desc.empty());
        CHECK(row->effects.empty());
        ++at;
    }
    CHECK(items.size() == 37);
    // The grips, and the slots they belong to.
    CHECK(items.find("hatchet")->grip == WeaponGrip::OneHanded);
    CHECK(items.find("maul")->grip == WeaponGrip::TwoHanded);
    CHECK(items.find("boarding_axe")->grip == WeaponGrip::TwoHanded);
    CHECK(items.find("buckler")->slot == ItemSlot::OffHand);
    CHECK(items.find("buckler")->grip == WeaponGrip::Shield);
    CHECK(items.find("round_shield")->slot == ItemSlot::OffHand);
    CHECK(items.find("round_shield")->grip == WeaponGrip::Shield);
    CHECK(items.find("work_gloves")->slot == ItemSlot::Hands);
    CHECK(items.find("gauntlets")->slot == ItemSlot::Hands);
    CHECK(items.find("iron_pot")->slot == ItemSlot::Head);
    CHECK(items.find("brigandine")->slot == ItemSlot::Body);
    // Every first-file row is one-handed and names no model: the renderer's
    // class fallback is what draws them.
    for (std::size_t i = 0; i < items.baseRows(); ++i) {
        const ItemDef& row = items.items()[i];
        INFO("row: ", row.id);
        CHECK(row.grip == WeaponGrip::OneHanded);
        CHECK(row.model.empty());
    }
    // The sheet prints the two new slots, because the raws hold rows for them.
    CHECK(items.slotHasItems(ItemSlot::OffHand));
    CHECK(items.slotHasItems(ItemSlot::Hands));
    // Shields and gloves are armour in the pack.
    CHECK(itemCategory(*items.find("buckler")) == ItemCategory::Armour);
    CHECK(itemCategory(*items.find("gauntlets")) == ItemCategory::Armour);
    CHECK(itemCategory(*items.find("maul")) == ItemCategory::Weapon);
}

TEST_CASE("the gear chest stands on the quay: standable, nobody's, off the Tarwalk crosshair tiles") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    const content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    const TileQuery tiles(world);
    std::set<std::string> chest(std::begin(kGearIds), std::end(kGearIds));
    // The cutlass and the mace are items.json's rows; their stands are here.
    chest.insert("cutlass");
    chest.insert("mace");
    std::set<std::string> stood;
    for (const ItemStand& stand : items.stands()) {
        if (chest.count(stand.item) == 0) {
            continue;
        }
        INFO("stand: ", stand.item, " at ", stand.x, ",", stand.y, " band ", stand.band);
        stood.insert(stand.item);
        CHECK(tiles.standable(stand.x, stand.y, stand.band));
        CHECK_FALSE(stand.owned);
        CHECK(stand.band == gull::kGroundBand);
        // test_kit_page stands at (153,63) facing west and needs nothing in
        // the take's reach under a level eye or the eye at the feet.
        const bool tarwalkBox = stand.x >= 152 && stand.x <= 156 && stand.y >= 62 && stand.y <= 64;
        CHECK_FALSE(tarwalkBox);
    }
    // Every gear row, the cutlass and the mace can be picked up somewhere.
    CHECK(stood == chest);
}

TEST_CASE("each named loadout is wearable whole and fits the base sheet's load") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    const std::vector<std::vector<std::string>> loadouts = {
        {"wool_cap", "gambeson", "cutlass"},
        {"iron_pot", "brigandine", "gauntlets", "mace", "round_shield", "boots"},
        {"iron_pot", "brigandine", "gauntlets", "maul"},
    };
    for (const std::vector<std::string>& ids : loadouts) {
        Kit kit(items.size());
        std::int32_t drams = 0;
        for (const std::string& id : ids) {
            const std::int32_t index = items.indexOf(id);
            INFO("row: ", id);
            REQUIRE(index >= 0);
            drams += items.at(index)->drams;
            kit.add(index, 1);
            CHECK(kit.wear(items, index));
        }
        // Every piece is still on: no row in a loadout bares another.
        for (const std::string& id : ids) {
            INFO("worn: ", id);
            CHECK(kit.isWorn(items.indexOf(id)));
        }
        INFO("loadout drams: ", drams);
        CHECK(drams < loadCapDrams(40));
        CHECK(kit.weight(items) == drams);
    }
}

TEST_CASE("an extra file's hand row authors its own span within sense; a grip that does not fit its slot is refused") {
    ItemRegistry items = ItemRegistry::fromJson(kGearBase);
    items.appendJson(R"({"items": [
      {"id": "zero", "name": "ZERO", "slot": "hand", "class": "blunt", "damage": [0, 3]},
      {"id": "backwards", "name": "BACKWARDS", "slot": "hand", "class": "blunt", "damage": [6, 5]},
      {"id": "lottery", "name": "LOTTERY", "slot": "hand", "class": "blunt", "damage": [1, 10]},
      {"id": "widest", "name": "WIDEST", "slot": "hand", "class": "blunt", "damage": [1, 9]},
      {"id": "classed", "name": "CLASSED", "slot": "hand", "class": "edged"},
      {"id": "coat_grip", "name": "COAT", "slot": "body", "grip": "one", "dr": 2},
      {"id": "two_shield", "name": "SHIELD", "slot": "offhand", "grip": "two", "dr": 1},
      {"id": "hand_shield", "name": "SHIELD", "slot": "hand", "grip": "shield"},
      {"id": "odd_grip", "name": "AXE", "slot": "hand", "grip": "three"},
      {"id": "plain_shield", "name": "SHIELD", "slot": "offhand", "dr": 1},
      {"id": "long", "name": "LONG", "slot": "hand", "class": "edged", "grip": "two", "damage": [12, 18]}
    ]})");
    for (const char* refused :
         {"zero", "backwards", "lottery", "coat_grip", "two_shield", "hand_shield", "odd_grip"}) {
        INFO("row: ", refused);
        CHECK(items.find(refused) == nullptr);
    }
    CHECK(items.warnings().size() == 7);
    // The widest span the rule allows, kept as authored.
    REQUIRE(items.find("widest") != nullptr);
    CHECK(items.find("widest")->damageLo == 1);
    CHECK(items.find("widest")->damageHi == 1 + kStrikeSpanWidthMax);
    // A row that authors no span rolls its class's.
    REQUIRE(items.find("classed") != nullptr);
    CHECK(items.find("classed")->damageLo == baseDamage(Weapon::Edged));
    CHECK(items.find("classed")->damageHi == baseDamage(Weapon::Edged) + kStrikeVarianceMax);
    // An off-hand row is a shield whether it says so or not, and carries no
    // swing numbers; a hand row carries its grip's.
    REQUIRE(items.find("plain_shield") != nullptr);
    CHECK(items.find("plain_shield")->grip == WeaponGrip::Shield);
    CHECK(items.find("plain_shield")->reachQ8 == 0);
    REQUIRE(items.find("long") != nullptr);
    CHECK(items.find("long")->reachQ8 == gripTiming(WeaponGrip::TwoHanded).reachQ8);
    CHECK(items.find("long")->recoverySteps == gripTiming(WeaponGrip::TwoHanded).recoverySteps);
    CHECK(items.find("classed")->reachQ8 == kMeleeReach);
    // THE FIRST FILE still promises its class's span: even a sane span that
    // is not the class's is refused there.
    const ItemRegistry first = ItemRegistry::fromJson(R"({"items": [
      {"id": "sane", "name": "SANE", "slot": "hand", "class": "blunt", "damage": [6, 10]}]})");
    CHECK(first.find("sane") == nullptr);
    CHECK(first.warnings().size() == 1);
}

// ---------------------------------------------------------------------------
// the slots and the grips
// ---------------------------------------------------------------------------

TEST_CASE("the two new slots are appended: every slot and grip spelling round-trips") {
    // No shipped ordinal moved.
    CHECK(static_cast<int>(ItemSlot::None) == 0);
    CHECK(static_cast<int>(ItemSlot::Hand) == 1);
    CHECK(static_cast<int>(ItemSlot::Trinket) == 5);
    CHECK(static_cast<int>(ItemSlot::OffHand) == 6);
    CHECK(static_cast<int>(ItemSlot::Hands) == 7);
    CHECK(kWornSlotCount == 7);
    CHECK(kItemSlotCount == 8);
    CHECK(kLegacyWornSlotCount == 5);
    for (std::size_t i = 0; i < kItemSlotCount; ++i) {
        const ItemSlot slot = static_cast<ItemSlot>(i);
        INFO("slot ", i);
        ItemSlot back = ItemSlot::None;
        REQUIRE(itemSlotFromSymbol(itemSlotSymbol(slot), back));
        CHECK(back == slot);
        CHECK_FALSE(itemSlotSheetLabel(slot).empty());
        CHECK_FALSE(itemSlotShortWord(slot).empty());
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(kWornSlotCount); ++i) {
        CHECK(wornSlotIndex(wornSlotAt(i)) == i);
    }
    CHECK(wornSlotIndex(ItemSlot::None) == -1);
    CHECK(itemSlotSymbol(ItemSlot::OffHand) == "offhand");
    CHECK(itemSlotSymbol(ItemSlot::Hands) == "hands");
    CHECK(itemSlotSheetLabel(ItemSlot::OffHand) == "IN THE OTHER HAND");
    CHECK(itemSlotSheetLabel(ItemSlot::Hands) == "ON THE HANDS");
    CHECK(itemSlotShortWord(ItemSlot::Hand) == "HAND");
    CHECK(itemSlotShortWord(ItemSlot::OffHand) == "OFF-HAND");
    CHECK(itemSlotShortWord(ItemSlot::Hands) == "HANDS");
    ItemSlot unknown = ItemSlot::Head;
    CHECK_FALSE(itemSlotFromSymbol("tail", unknown));
    CHECK(unknown == ItemSlot::Head);
    // The screen's order: what you hit with, what you block with, head to
    // foot, the belt. Every worn slot exactly once.
    const ItemSlot screen[] = {ItemSlot::Hand,  ItemSlot::OffHand, ItemSlot::Head,
                               ItemSlot::Body,  ItemSlot::Hands,   ItemSlot::Feet,
                               ItemSlot::Trinket};
    for (std::size_t i = 0; i < kWornSlotCount; ++i) {
        CHECK(kSlotScreenOrder[i] == screen[i]);
    }
    // The grips.
    for (const WeaponGrip grip : {WeaponGrip::OneHanded, WeaponGrip::TwoHanded, WeaponGrip::Shield}) {
        WeaponGrip back = WeaponGrip::OneHanded;
        REQUIRE(weaponGripFromSymbol(weaponGripSymbol(grip), back));
        CHECK(back == grip);
    }
    CHECK(weaponGripSymbol(WeaponGrip::OneHanded) == "one");
    CHECK(weaponGripSymbol(WeaponGrip::TwoHanded) == "two");
    CHECK(weaponGripSymbol(WeaponGrip::Shield) == "shield");
    WeaponGrip odd = WeaponGrip::TwoHanded;
    CHECK_FALSE(weaponGripFromSymbol("three", odd));
    CHECK(odd == WeaponGrip::TwoHanded);
}

TEST_CASE("gripTiming: one-handed is the shipped swing to the step, two-handed reaches further and pays for it") {
    constexpr GripTiming one = gripTiming(WeaponGrip::OneHanded);
    CHECK(one.reachQ8 == kMeleeReach);
    CHECK(one.recoverySteps == kSwingRecoverySteps);
    CHECK(one.hardRecoverySteps == kHardSwingRecoverySteps);
    CHECK(one.hardHoldSteps == kHardSwingHoldSteps);
    constexpr GripTiming two = gripTiming(WeaponGrip::TwoHanded);
    CHECK(two.reachQ8 == 416);
    CHECK(two.recoverySteps == 48);
    CHECK(two.hardRecoverySteps == 72);
    CHECK(two.hardHoldSteps == 21);
    CHECK(two.reachQ8 > one.reachQ8);
    CHECK(two.recoverySteps > one.recoverySteps);
    CHECK(two.hardRecoverySteps > one.hardRecoverySteps);
    CHECK(two.hardHoldSteps > one.hardHoldSteps);
    // A shield is never swung; asked anyway, it answers the arm's own numbers.
    constexpr GripTiming shield = gripTiming(WeaponGrip::Shield);
    CHECK(shield.reachQ8 == one.reachQ8);
    CHECK(shield.recoverySteps == one.recoverySteps);
    CHECK(shield.hardRecoverySteps == one.hardRecoverySteps);
    CHECK(shield.hardHoldSteps == one.hardHoldSteps);
    // Every loaded hand row carries its grip's numbers; nothing else carries any.
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    for (const ItemDef& row : items.items()) {
        INFO("row: ", row.id);
        if (row.slot == ItemSlot::Hand) {
            const GripTiming timing = gripTiming(row.grip);
            CHECK(row.reachQ8 == timing.reachQ8);
            CHECK(row.recoverySteps == timing.recoverySteps);
            CHECK(row.hardRecoverySteps == timing.hardRecoverySteps);
        } else {
            CHECK(row.reachQ8 == 0);
            CHECK(row.recoverySteps == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// the player's swing
// ---------------------------------------------------------------------------

TEST_CASE("the player's swing machine runs on the held grip: a maul winds longer and recovers slower than a fist") {
    // Four in the morning on the Tarwalk, facing the water: nobody in the
    // Gull's roster is on this ray, and a whiff buys the same lockout a hit
    // does.
    Room room(hourOfDay(4), 153, 63, gull::kGroundBand, kFacingWest);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.items().loaded());
    CHECK(tavern.playerReachQ8() == kMeleeReach);
    CHECK(tavern.playerHardHoldSteps() == kHardSwingHoldSteps);
    REQUIRE(tavern.playerSightlineAlong() == -1);
    // Bare: the shipped numbers, to the step.
    CHECK(room.swingAndRecover(1) == kSwingRecoverySteps);
    room.settleToIdle();
    tavern.playerAttackDown();
    for (int i = 0; i < kHardSwingHoldSteps; ++i) {
        room.stepOnce();
    }
    CHECK(tavern.playerChargeHard());
    {
        const Tavern::PlayerSwingResult hard = tavern.playerAttackUp();
        REQUIRE(hard.swung);
        CHECK(hard.hard);
    }
    room.settleToIdle();
    CHECK(room.swingAndRecover(kHardSwingHoldSteps) == kHardSwingRecoverySteps);

    // A maul in both hands.
    const GripTiming two = gripTiming(WeaponGrip::TwoHanded);
    const std::int32_t maul = tavern.items().indexOf("maul");
    REQUIRE(tavern.giveItem("maul"));
    REQUIRE(tavern.wearItem(maul).result == ServiceResult::Served);
    CHECK(tavern.playerWeapon() == Weapon::Blunt);
    CHECK(tavern.playerReachQ8() == two.reachQ8);
    CHECK(tavern.playerHardHoldSteps() == two.hardHoldSteps);
    REQUIRE(tavern.playerSightlineAlong() == -1);
    CHECK(room.swingAndRecover(1) == two.recoverySteps);
    // The one-handed hold is not hard enough for it; its own hold is.
    room.settleToIdle();
    tavern.playerAttackDown();
    for (int i = 0; i < kHardSwingHoldSteps; ++i) {
        room.stepOnce();
    }
    CHECK_FALSE(tavern.playerChargeHard());
    for (int i = kHardSwingHoldSteps; i < two.hardHoldSteps; ++i) {
        room.stepOnce();
    }
    CHECK(tavern.playerChargeHard());
    {
        const Tavern::PlayerSwingResult hard = tavern.playerAttackUp();
        REQUIRE(hard.swung);
        CHECK(hard.hard);
    }
    room.settleToIdle();
    CHECK(room.swingAndRecover(two.hardHoldSteps) == two.hardRecoverySteps);

    // Bared, the fist's numbers come straight back.
    tavern.bareSlot(ItemSlot::Hand);
    CHECK(tavern.playerReachQ8() == kMeleeReach);
    CHECK(room.swingAndRecover(1) == kSwingRecoverySteps);
}

TEST_CASE("a body a tile and a half down the ray is past a fist and inside a maul, on the swing's own sightline") {
    Room room(hourOfDay(14), gull::kBartenderX, gull::kBarY - 1);
    Tavern& tavern = room.tavern();
    room.run(2);
    // A tile and a half: past kMeleeReach, inside the two-handed reach.
    constexpr std::int32_t kOff = 384;
    constexpr std::int32_t kTwoHandedReach = gripTiming(WeaponGrip::TwoHanded).reachQ8;
    static_assert(kOff > kMeleeReach);
    static_assert(kOff <= kTwoHandedReach);
    // The swing's projection, the one sightlineTarget casts.
    struct Ray {
        std::int64_t fx;
        std::int64_t fy;
        std::int32_t x;
        std::int32_t y;
        [[nodiscard]] std::int64_t along(const Actor& a) const {
            const std::int64_t dx = static_cast<std::int64_t>(a.x()) - x;
            const std::int64_t dy = static_cast<std::int64_t>(a.y()) - y;
            return (fx * dx + fy * dy) >> 16;
        }
        [[nodiscard]] std::int64_t perp(const Actor& a) const {
            const std::int64_t dx = static_cast<std::int64_t>(a.x()) - x;
            const std::int64_t dy = static_cast<std::int64_t>(a.y()) - y;
            return (-fy * dx + fx * dy) >> 16;
        }
    };
    // Find a standing person with nobody else anywhere near the ray behind
    // him: the room moves, so the mark is found, not assumed.
    std::int32_t markId = -1;
    Angle yaw = kFacingNorth;
    Ray ray{0, 0, 0, 0};
    std::int32_t band = gull::kGroundBand;
    for (const Actor& mark : tavern.actors()) {
        if (markId >= 0) {
            break;
        }
        if (!mark.present() || isFloored(mark.activity()) || mark.role() == ActorRole::Vermin) {
            continue;
        }
        for (const Angle facing : {kFacingNorth, kFacingEast, kFacingSouth, kFacingWest}) {
            Ray candidate{forward_x_q16(facing), forward_y_q16(facing), 0, 0};
            candidate.x = mark.x() - static_cast<std::int32_t>((candidate.fx * kOff) >> 16);
            candidate.y = mark.y() - static_cast<std::int32_t>((candidate.fy * kOff) >> 16);
            bool clear = true;
            for (const Actor& other : tavern.actors()) {
                if (other.id() == mark.id() || !other.present() || isFloored(other.activity())) {
                    continue;
                }
                // Anybody on the maul's whole ray would be hit first (or
                // by the fist): the mark must be alone on it.
                const std::int64_t along = candidate.along(other);
                const std::int64_t perp = candidate.perp(other);
                const bool onRay = along > 0 && along <= kTwoHandedReach &&
                                   perp <= kBodyHalfWidth && perp >= -kBodyHalfWidth;
                clear = clear && !onRay;
            }
            if (clear) {
                markId = mark.id();
                yaw = facing;
                ray = candidate;
                band = mark.band();
                break;
            }
        }
    }
    REQUIRE(markId >= 0);
    const Actor* mark = nullptr;
    for (const Actor& actor : tavern.actors()) {
        if (actor.id() == markId) {
            mark = &actor;
        }
    }
    REQUIRE(mark != nullptr);
    const std::int64_t expected = ray.along(*mark);
    REQUIRE(expected > kMeleeReach);
    REQUIRE(expected <= kTwoHandedReach);

    // Placed straight into the room (no movement step, so nobody moves).
    tavern.setPlayer(ray.x, ray.y, band);
    tavern.setPlayerYaw(yaw);
    // Bare: past the fist. The ray finds nobody and the swing whiffs.
    CHECK(tavern.playerSightlineAlong() == -1);
    tavern.playerAttackDown();
    tavern.stepPlayerCombat();
    const Tavern::PlayerSwingResult whiff = tavern.playerAttackUp();
    REQUIRE(whiff.swung);
    CHECK(whiff.targetId == -1);
    for (int i = 0; i < 200 && !tavern.playerCombatIdle(); ++i) {
        tavern.stepPlayerCombat();
    }
    REQUIRE(tavern.playerCombatIdle());
    // The maul reaches him, on the same ray, and the swing lands on him.
    REQUIRE(tavern.giveItem("maul"));
    REQUIRE(tavern.wearItem(tavern.items().indexOf("maul")).result == ServiceResult::Served);
    CHECK(tavern.playerSightlineAlong() == expected);
    tavern.playerAttackDown();
    tavern.stepPlayerCombat();
    const Tavern::PlayerSwingResult hit = tavern.playerAttackUp();
    REQUIRE(hit.swung);
    CHECK(hit.targetId == markId);
}

TEST_CASE("strikeWithSpan is strike() to the bit over a class's span, and a gear span rolls inside itself") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    // EVERY items.json hand row, on the same rolls, at both tiers, with a
    // bonus riding: the held span IS the class span, so nothing differs.
    int rows = 0;
    int mismatches = 0;
    for (std::size_t i = 0; i < items.baseRows(); ++i) {
        const ItemDef& item = items.items()[i];
        if (item.slot != ItemSlot::Hand) {
            continue;
        }
        ++rows;
        CHECK(item.damageLo == baseDamage(item.weaponClass));
        CHECK(item.damageHi == baseDamage(item.weaponClass) + kStrikeVarianceMax);
        for (std::uint64_t n = 0; n < 512; ++n) {
            const std::uint64_t roll = mixed(n);
            for (const std::int32_t charge : {kSwingChargeQ8, kHardSwingChargeQ8}) {
                Fighter a{0, Weapon::Fists, Intent::Subdue, 40, 60};
                Fighter b = a;
                const Blow x = strike(item.weaponClass, a, roll, 1, kFatigueTermFullQ8, charge);
                const Blow y = strikeWithSpan(item.weaponClass, item.damageLo, item.damageHi, b,
                                              roll, 1, kFatigueTermFullQ8, charge);
                const bool same = x.landed == y.landed && x.damage == y.damage &&
                                  x.bloodied == y.bloodied && x.downed == y.downed &&
                                  x.crowned == y.crowned && a.hp == b.hp;
                mismatches += same ? 0 : 1;
            }
        }
    }
    CHECK(rows >= 8);
    CHECK(mismatches == 0);

    // A gear row's span: every landed tap is inside it, and the whole of it
    // is reachable off the same carving.
    const ItemDef* hatchet = items.find("hatchet");
    REQUIRE(hatchet != nullptr);
    CHECK(hatchet->damageLo == 9);
    CHECK(hatchet->damageHi == 14);
    std::set<std::int32_t> seen;
    int outside = 0;
    for (std::uint64_t n = 0; n < 2048; ++n) {
        Fighter target{0, Weapon::Fists, Intent::Subdue, 1000, 1000};
        const Blow blow = strikeWithSpan(hatchet->weaponClass, hatchet->damageLo, hatchet->damageHi,
                                         target, mixed(n));
        if (!blow.landed) {
            continue;
        }
        seen.insert(blow.damage);
        outside += (blow.damage < hatchet->damageLo || blow.damage > hatchet->damageHi) ? 1 : 0;
    }
    CHECK(outside == 0);
    CHECK(seen.size() == static_cast<std::size_t>(hatchet->damageHi - hatchet->damageLo + 1));
}

TEST_CASE("the player's blow rolls the held thing's span: the class's for an items.json row, the row's own for gear") {
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.items().loaded());
    // Bare: the fist's span.
    CHECK(tavern.playerDamageLo() == baseDamage(Weapon::Fists));
    CHECK(tavern.playerDamageHi() == baseDamage(Weapon::Fists) + kStrikeVarianceMax);
    // A class armed straight: that class's span, as strike() always rolled it.
    tavern.setPlayerCombat(Weapon::Edged, Intent::Subdue);
    CHECK(tavern.playerDamageLo() == baseDamage(Weapon::Edged));
    CHECK(tavern.playerDamageHi() == baseDamage(Weapon::Edged) + kStrikeVarianceMax);
    // items.json's cutlass: the same three-wide class span.
    const std::int32_t cutlass = tavern.items().indexOf("cutlass");
    REQUIRE(tavern.giveItem("cutlass"));
    REQUIRE(tavern.wearItem(cutlass).result == ServiceResult::Served);
    CHECK(tavern.playerDamageLo() == baseDamage(Weapon::Edged));
    CHECK(tavern.playerDamageHi() == baseDamage(Weapon::Edged) + kStrikeVarianceMax);
    CHECK(tavern.playerReachQ8() == kMeleeReach);
    // The hatchet: its own.
    const std::int32_t hatchet = tavern.items().indexOf("hatchet");
    REQUIRE(tavern.giveItem("hatchet"));
    REQUIRE(tavern.wearItem(hatchet).result == ServiceResult::Served);
    CHECK(tavern.playerWeapon() == Weapon::Edged);
    CHECK(tavern.playerDamageLo() == 9);
    CHECK(tavern.playerDamageHi() == 14);
    CHECK(tavern.playerReachQ8() == kMeleeReach);
    tavern.bareSlot(ItemSlot::Hand);
    CHECK(tavern.playerDamageLo() == baseDamage(Weapon::Fists));
}

// ---------------------------------------------------------------------------
// two hands and a shield
// ---------------------------------------------------------------------------

TEST_CASE("a two-handed row bares the shield, a shield bares the two-hander, and wouldBare says so first") {
    const ItemRegistry items = gearRegistry();
    REQUIRE(items.warnings().empty());
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t maul = items.indexOf("maul");
    const std::int32_t buckler = items.indexOf("buckler");
    Kit kit(items.size());
    kit.add(cudgel, 1);
    kit.add(maul, 1);
    kit.add(buckler, 1);
    REQUIRE(kit.wear(items, cudgel));
    // A shield beside a one-handed row takes nothing off.
    CHECK(kit.wouldBare(items, buckler).empty());
    REQUIRE(kit.wear(items, buckler));
    CHECK(kit.worn(ItemSlot::Hand) == cudgel);
    CHECK(kit.worn(ItemSlot::OffHand) == buckler);
    // The maul takes the cudgel's place AND the shield off the other arm.
    const std::vector<ItemSlot> handAndShield = {ItemSlot::Hand, ItemSlot::OffHand};
    CHECK(kit.wouldBare(items, maul) == handAndShield);
    REQUIRE(kit.wear(items, maul));
    CHECK(kit.worn(ItemSlot::Hand) == maul);
    CHECK(kit.worn(ItemSlot::OffHand) == -1);
    CHECK(kit.handGrip(items) == WeaponGrip::TwoHanded);
    CHECK(kit.count(buckler) == 1);  // bared, still carried
    // The shield back on takes the maul out of the hand.
    const std::vector<ItemSlot> handOnly = {ItemSlot::Hand};
    CHECK(kit.wouldBare(items, buckler) == handOnly);
    REQUIRE(kit.wear(items, buckler));
    CHECK(kit.worn(ItemSlot::OffHand) == buckler);
    CHECK(kit.worn(ItemSlot::Hand) == -1);
    CHECK(kit.handClass(items) == Weapon::Fists);
    CHECK(kit.handGrip(items) == WeaponGrip::OneHanded);
    // A one-handed row goes back in beside it.
    CHECK(kit.wouldBare(items, cudgel).empty());
    REQUIRE(kit.wear(items, cudgel));
    CHECK(kit.worn(ItemSlot::OffHand) == buckler);
}

TEST_CASE("wouldBare names only what the press takes off") {
    const ItemRegistry items = gearRegistry();
    const std::int32_t hood = items.indexOf("hood");
    const std::int32_t pot = items.indexOf("pot");
    const std::int32_t gloves = items.indexOf("gloves");
    const std::int32_t rope = items.indexOf("rope");
    const std::int32_t maul = items.indexOf("maul");
    Kit kit(items.size());
    // Nothing on: nothing comes off, carried or not (it asks only wearing).
    CHECK(kit.wouldBare(items, gloves).empty());
    CHECK(kit.wouldBare(items, maul).empty());
    // Out of range and never worn: nothing.
    CHECK(kit.wouldBare(items, -1).empty());
    CHECK(kit.wouldBare(items, 999).empty());
    CHECK(kit.wouldBare(items, rope).empty());
    // A pot over a hood takes the hood.
    kit.add(hood, 1);
    kit.add(pot, 1);
    REQUIRE(kit.wear(items, hood));
    const std::vector<ItemSlot> head = {ItemSlot::Head};
    CHECK(kit.wouldBare(items, pot) == head);
    // Already worn: the press takes nothing off.
    CHECK(kit.wouldBare(items, hood).empty());
}

TEST_CASE("WEAR through the room says what the two-hand rule took off, and the hand reads fists after a shield") {
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    const std::int32_t cutlass = tavern.items().indexOf("cutlass");
    const std::int32_t maul = tavern.items().indexOf("maul");
    const std::int32_t shield = tavern.items().indexOf("round_shield");
    REQUIRE(tavern.giveItem("cutlass"));
    REQUIRE(tavern.giveItem("maul"));
    REQUIRE(tavern.giveItem("round_shield"));
    CHECK(tavern.wearItem(cutlass).line == "IN HAND - CUTLASS 11-13 EDGE.");
    CHECK(tavern.wearItem(shield).line == "ROUND SHIELD - ON.");
    CHECK(tavern.kit().worn(ItemSlot::OffHand) == shield);
    CHECK(tavern.wearItem(maul).line == "IN HAND - MAUL 10-16 IMPACT. ROUND SHIELD - OFF.");
    CHECK(tavern.kit().worn(ItemSlot::OffHand) == -1);
    CHECK(tavern.playerWeapon() == Weapon::Blunt);
    CHECK(tavern.wearItem(shield).line == "ROUND SHIELD - ON. MAUL - OFF.");
    CHECK(tavern.kit().worn(ItemSlot::Hand) == -1);
    CHECK(tavern.kit().count(maul) == 1);
    CHECK(tavern.playerWeapon() == Weapon::Fists);
    CHECK(tavern.playerReachQ8() == kMeleeReach);
}

// ---------------------------------------------------------------------------
// defence
// ---------------------------------------------------------------------------

TEST_CASE("DR sums a shield, gloves, a helmet and a body piece flat; the hand turns nothing") {
    const ItemRegistry items = ItemRegistry::load(content::contentDir());
    REQUIRE(items.loaded());
    Kit kit(items.size());
    std::int32_t expected = 0;
    for (const char* id : {"round_shield", "gauntlets", "iron_pot", "brigandine", "boots", "mace"}) {
        const std::int32_t index = items.indexOf(id);
        REQUIRE(index >= 0);
        kit.add(index, 1);
        REQUIRE(kit.wear(items, index));
        if (items.at(index)->slot != ItemSlot::Hand) {
            expected += items.at(index)->dr;
        }
    }
    CHECK(expected == 2 + 2 + 2 + 4 + 1);
    CHECK(kit.wornDr(items) == expected);
    // Off the shield and the gloves, the sum drops by exactly theirs.
    kit.bare(ItemSlot::OffHand);
    kit.bare(ItemSlot::Hands);
    CHECK(kit.wornDr(items) == expected - 4);
}

TEST_CASE("a guard's kit turns a flat DR off every landed blow, never below one") {
    // test_kit's twin rooms: same seed, same fight; one body in the guard
    // loadout's armour, one bare. The same rolls land in both.
    auto fight = [](bool armoured, std::int32_t& blowsTurned) {
        Room room(hourOfDay(19), gull::kBartenderX, gull::kBarY - 1);
        Tavern& tavern = room.tavern();
        room.run(2);
        if (armoured) {
            for (const char* id : {"iron_pot", "brigandine", "gauntlets", "round_shield", "boots"}) {
                INFO("row: ", id);
                REQUIRE(tavern.giveItem(id));
                REQUIRE(tavern.wearItem(tavern.items().indexOf(id)).result == ServiceResult::Served);
            }
            REQUIRE(tavern.wornDr() == 11);
        }
        const std::int32_t start = tavern.playerHp();
        const Actor* mark = room.findRole(ActorRole::Patron);
        REQUIRE(mark != nullptr);
        REQUIRE(room.standFacing(*mark) != -1);
        for (int swings = 0; swings < 3; ++swings) {
            (void)room.swingAndRecover(1);
        }
        room.run(8);
        blowsTurned = tavern.blowsTurned();
        return start - tavern.playerHp();
    };
    std::int32_t turnedBare = 0;
    const std::int32_t lostBare = fight(false, turnedBare);
    std::int32_t turnedArmoured = 0;
    const std::int32_t lostArmoured = fight(true, turnedArmoured);
    INFO("bare lost ", lostBare, ", armoured lost ", lostArmoured, " over ", turnedArmoured,
         " turned blows");
    CHECK(turnedBare == 0);
    REQUIRE(lostBare > 0);
    CHECK(turnedArmoured > 0);
    CHECK(lostArmoured < lostBare);
    // THE FLOOR: every blow the kit turned still cost at least one point.
    CHECK(lostArmoured >= turnedArmoured);
}

// ---------------------------------------------------------------------------
// the codecs
// ---------------------------------------------------------------------------

TEST_CASE("the Loadout's own codec: GL v1 by id, round-trips, and refuses every lie") {
    const ItemRegistry items = gearRegistry();
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t pot = items.indexOf("pot");
    const std::int32_t buckler = items.indexOf("buckler");
    const std::int32_t gloves = items.indexOf("gloves");
    const std::int32_t maul = items.indexOf("maul");
    const std::int32_t coat = items.indexOf("coat");
    Loadout loadout;
    loadout.set(ItemSlot::Hand, cudgel);
    loadout.set(ItemSlot::Head, pot);
    loadout.set(ItemSlot::OffHand, buckler);
    loadout.set(ItemSlot::Hands, gloves);
    CHECK(loadout.worn(ItemSlot::Hands) == gloves);
    CHECK(loadout.isWorn(buckler));
    CHECK_FALSE(loadout.isWorn(coat));
    CHECK_FALSE(loadout.isWorn(-1));
    // The bytes, by hand: magic, version, slot count, one id per slot in
    // wornSlotIndex order (Hand, Body, Head, Feet, Trinket, OffHand, Hands).
    std::vector<std::uint8_t> expected = {'G', 'L', 1, 7};
    putId(expected, "cudgel");
    putId(expected, "");
    putId(expected, "pot");
    putId(expected, "");
    putId(expected, "");
    putId(expected, "buckler");
    putId(expected, "gloves");
    const std::vector<std::uint8_t> bytes = loadout.encode(items);
    CHECK(bytes == expected);
    Loadout back;
    REQUIRE(Loadout::decode(bytes, items, back));
    CHECK(back == loadout);
    CHECK(back.encode(items) == bytes);
    // Empty round-trips too.
    Loadout bare;
    Loadout bareBack;
    bareBack.set(ItemSlot::Feet, items.indexOf("boots"));
    REQUIRE(Loadout::decode(bare.encode(items), items, bareBack));
    CHECK(bareBack == bare);
    // A worn index the raws do not know is written bare.
    Loadout ghost;
    ghost.set(ItemSlot::Hand, 999);
    Loadout ghostBack;
    REQUIRE(Loadout::decode(ghost.encode(items), items, ghostBack));
    CHECK(ghostBack.worn(ItemSlot::Hand) == -1);

    // THE REFUSALS. `out` is untouched by every one.
    Loadout sentinel;
    sentinel.set(ItemSlot::Feet, items.indexOf("boots"));
    const Loadout before = sentinel;
    auto refused = [&](const std::vector<std::uint8_t>& image) {
        const bool decoded = Loadout::decode(image, items, sentinel);
        return !decoded && sentinel == before;
    };
    std::vector<std::uint8_t> wrongMagic = bytes;
    wrongMagic[1] = 'K';
    CHECK(refused(wrongMagic));
    std::vector<std::uint8_t> wrongVersion = bytes;
    wrongVersion[2] = 2;
    CHECK(refused(wrongVersion));
    std::vector<std::uint8_t> fiveSlots = bytes;
    fiveSlots[3] = 5;
    CHECK(refused(fiveSlots));
    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    CHECK(refused(truncated));
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK(refused(trailing));
    CHECK(refused(std::vector<std::uint8_t>{}));
    // An id the raws lost: the base file alone has no gloves.
    const ItemRegistry lost = ItemRegistry::fromJson(kGearBase);
    Loadout untouched;
    CHECK_FALSE(Loadout::decode(bytes, lost, untouched));
    CHECK(untouched == Loadout{});
    // A row in a slot that is not its own: a coat in the hand.
    std::vector<std::uint8_t> misplaced = {'G', 'L', 1, 7};
    putId(misplaced, "coat");
    for (int i = 0; i < 6; ++i) {
        putId(misplaced, "");
    }
    CHECK(refused(misplaced));
    // A two-handed row with a shield beside it: a state wear() cannot make.
    Loadout liar;
    liar.set(ItemSlot::Hand, maul);
    liar.set(ItemSlot::OffHand, buckler);
    CHECK(refused(liar.encode(items)));
    // clear() bares every slot.
    loadout.clear();
    CHECK(loadout == Loadout{});
}

TEST_CASE("the Kit's version 3 image carries the Loadout's own bytes, and round-trips the new slots") {
    const ItemRegistry items = gearRegistry();
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t coat = items.indexOf("coat");
    const std::int32_t gloves = items.indexOf("gloves");
    const std::int32_t buckler = items.indexOf("buckler");
    const std::int32_t rope = items.indexOf("rope");
    Kit kit(items.size());
    kit.add(cudgel, 1);
    kit.add(coat, 2);
    kit.add(gloves, 1);
    kit.add(buckler, 1);
    kit.add(rope, 3);
    REQUIRE(kit.wear(items, cudgel));
    REQUIRE(kit.wear(items, coat));
    REQUIRE(kit.wear(items, gloves));
    REQUIRE(kit.wear(items, buckler));
    kit.markStolen(coat, 1);
    const std::vector<std::uint8_t> bytes = kit.encode(items);
    REQUIRE(bytes.size() > 3);
    CHECK(bytes[0] == 'G');
    CHECK(bytes[1] == 'K');
    CHECK(bytes[2] == 3);
    Kit back;
    REQUIRE(Kit::decode(bytes, items, back));
    CHECK(back.encode(items) == bytes);
    CHECK(back.loadout() == kit.loadout());
    CHECK(back.worn(ItemSlot::OffHand) == buckler);
    CHECK(back.worn(ItemSlot::Hands) == gloves);
    CHECK(back.count(rope) == 3);
    CHECK(back.stolen(coat) == 1);
    // The worn rows are the Loadout's own image behind its u32 length.
    const std::vector<std::uint8_t> image = kit.loadout().encode(items);
    const auto found = std::search(bytes.begin(), bytes.end(), image.begin(), image.end());
    REQUIRE(found != bytes.end());
    const std::ptrdiff_t offset = found - bytes.begin();
    REQUIRE(offset >= 4);
    std::vector<std::uint8_t> length;
    putI32(length, static_cast<std::int32_t>(image.size()));
    CHECK(std::equal(length.begin(), length.end(), bytes.begin() + (offset - 4)));
    // A version this build does not read is refused whole.
    std::vector<std::uint8_t> v4 = bytes;
    v4[2] = 4;
    CHECK_FALSE(Kit::decode(v4, items, back));
    // Gloves worn but not carried: the loadout's image is sound and the Kit
    // still refuses it, because a worn row is a carried row.
    Loadout gloved;
    gloved.set(ItemSlot::Hands, gloves);
    const std::vector<std::uint8_t> glovedImage = gloved.encode(items);
    std::vector<std::uint8_t> lie = {'G', 'K', 3};
    putI32(lie, 1);
    putId(lie, "coat");
    putI32(lie, 1);
    putI32(lie, static_cast<std::int32_t>(glovedImage.size()));
    lie.insert(lie.end(), glovedImage.begin(), glovedImage.end());
    putI32(lie, 0);
    CHECK_FALSE(Kit::decode(lie, items, back));
    // The same image with the gloves carried decodes.
    std::vector<std::uint8_t> honest = {'G', 'K', 3};
    putI32(honest, 2);
    putId(honest, "coat");
    putI32(honest, 1);
    putId(honest, "gloves");
    putI32(honest, 1);
    putI32(honest, static_cast<std::int32_t>(glovedImage.size()));
    honest.insert(honest.end(), glovedImage.begin(), glovedImage.end());
    putI32(honest, 0);
    Kit honestBack;
    REQUIRE(Kit::decode(honest, items, honestBack));
    CHECK(honestBack.worn(ItemSlot::Hands) == gloves);
    // A length that runs past the image is refused.
    std::vector<std::uint8_t> overlong = honest;
    const std::size_t lengthAt = 3 + 4 + (1 + 4 + 4) + (1 + 6 + 4);
    overlong[lengthAt] = 0xFF;
    CHECK_FALSE(Kit::decode(overlong, items, back));
}

TEST_CASE("version 1 and 2 Kit images still decode: the five-slot block onto Hand..Trinket, the new slots bare") {
    const ItemRegistry items = gearRegistry();
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t coat = items.indexOf("coat");
    // The image the phase 8 build wrote, by hand.
    std::vector<std::uint8_t> v2 = {'G', 'K', 2};
    putI32(v2, 2);
    putId(v2, "cudgel");
    putI32(v2, 1);
    putId(v2, "coat");
    putI32(v2, 1);
    v2.push_back(5);
    putId(v2, "cudgel");
    putId(v2, "coat");
    putId(v2, "");
    putId(v2, "");
    putId(v2, "");
    // Version 1 is the same image with no stolen block.
    std::vector<std::uint8_t> v1 = v2;
    v1[2] = 1;
    putI32(v2, 1);
    putId(v2, "coat");
    putI32(v2, 1);

    Kit fromV2;
    REQUIRE(Kit::decode(v2, items, fromV2));
    CHECK(fromV2.worn(ItemSlot::Hand) == cudgel);
    CHECK(fromV2.worn(ItemSlot::Body) == coat);
    CHECK(fromV2.worn(ItemSlot::OffHand) == -1);
    CHECK(fromV2.worn(ItemSlot::Hands) == -1);
    CHECK(fromV2.stolen(coat) == 1);
    // Written again it is a version 3 image, and that reads back the same.
    const std::vector<std::uint8_t> rewritten = fromV2.encode(items);
    CHECK(rewritten[2] == 3);
    Kit again;
    REQUIRE(Kit::decode(rewritten, items, again));
    CHECK(again.loadout() == fromV2.loadout());
    CHECK(again.stolen(coat) == 1);

    Kit fromV1;
    REQUIRE(Kit::decode(v1, items, fromV1));
    CHECK(fromV1.worn(ItemSlot::Hand) == cudgel);
    CHECK(fromV1.worn(ItemSlot::Body) == coat);
    CHECK(fromV1.worn(ItemSlot::Hands) == -1);
    CHECK(fromV1.stolen(coat) == 0);
    // An old image hashes like a body built today with the same rows on.
    Kit built(items.size());
    built.add(cudgel, 1);
    built.add(coat, 1);
    REQUIRE(built.wear(items, cudgel));
    REQUIRE(built.wear(items, coat));
    CHECK(kitHash(fromV1, items.baseRows()) == kitHash(built, items.baseRows()));

    // A version 2 image with a seven-slot block is one no build ever wrote.
    std::vector<std::uint8_t> seven = {'G', 'K', 2};
    putI32(seven, 1);
    putId(seven, "cudgel");
    putI32(seven, 1);
    seven.push_back(7);
    putId(seven, "cudgel");
    for (int i = 0; i < 6; ++i) {
        putId(seven, "");
    }
    putI32(seven, 0);
    Kit refused;
    CHECK_FALSE(Kit::decode(seven, items, refused));
}

// ---------------------------------------------------------------------------
// the hash
// ---------------------------------------------------------------------------

TEST_CASE("nothing in the new slots hashes the five-slot Kit's words exactly; gloves add the KITL block") {
    const ItemRegistry items = gearRegistry();
    const std::size_t legacy = items.baseRows();
    REQUIRE(legacy == 6);
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t coat = items.indexOf("coat");
    const std::int32_t rope = items.indexOf("rope");
    const std::int32_t gloves = items.indexOf("gloves");
    const std::int32_t buckler = items.indexOf("buckler");
    Kit kit(items.size());
    kit.add(cudgel, 1);
    kit.add(coat, 1);
    kit.add(rope, 2);
    REQUIRE(kit.wear(items, cudgel));
    REQUIRE(kit.wear(items, coat));
    constexpr std::uint32_t kBare = 0xFFFFFFFFU;

    // THE FIVE-SLOT KIT'S OWN STREAM, by hand: the base rows' count, every
    // base count, then the five worn words (Hand..Trinket, -1 bare). No
    // extra row is held and nothing is stolen, so no tail.
    HashSink five(kHashSalt);
    five.put_int(static_cast<std::uint32_t>(legacy));
    for (std::size_t i = 0; i < legacy; ++i) {
        five.put_int(static_cast<std::uint32_t>(kit.count(static_cast<std::int32_t>(i))));
    }
    five.put_int(static_cast<std::uint32_t>(cudgel));
    five.put_int(static_cast<std::uint32_t>(coat));
    five.put_int(kBare);
    five.put_int(kBare);
    five.put_int(kBare);
    CHECK(kitHash(kit, legacy) == five.finished());
    // The loadout alone writes exactly those five words.
    HashSink alone(kHashSalt);
    kit.loadout().hashInto(alone);
    HashSink aloneByHand(kHashSalt);
    aloneByHand.put_int(static_cast<std::uint32_t>(cudgel));
    aloneByHand.put_int(static_cast<std::uint32_t>(coat));
    aloneByHand.put_int(kBare);
    aloneByHand.put_int(kBare);
    aloneByHand.put_int(kBare);
    CHECK(alone.finished() == aloneByHand.finished());

    // GLOVES ON: after the five words, the marker and the two new slots;
    // then (gloves are an extra file's row) the extra rows' tail as before.
    kit.add(gloves, 1);
    const std::uint64_t carried = kitHash(kit, legacy);
    REQUIRE(kit.wear(items, gloves));
    const std::uint64_t worn = kitHash(kit, legacy);
    CHECK(worn != carried);
    CHECK(worn != five.finished());
    HashSink gloved(kHashSalt);
    gloved.put_int(static_cast<std::uint32_t>(legacy));
    for (std::size_t i = 0; i < legacy; ++i) {
        gloved.put_int(static_cast<std::uint32_t>(kit.count(static_cast<std::int32_t>(i))));
    }
    gloved.put_int(static_cast<std::uint32_t>(cudgel));
    gloved.put_int(static_cast<std::uint32_t>(coat));
    gloved.put_int(kBare);
    gloved.put_int(kBare);
    gloved.put_int(kBare);
    gloved.put_int(0x4B49544CU);  // "KITL"
    gloved.put_int(kBare);        // the other hand
    gloved.put_int(static_cast<std::uint32_t>(gloves));
    gloved.put_int(0x4B49544FU);  // "KITO": the extra rows held
    gloved.put_int(static_cast<std::uint32_t>(gloves));
    gloved.put_int(1U);
    CHECK(worn == gloved.finished());
    // Off again: the KITL block goes with them.
    kit.bare(ItemSlot::Hands);
    CHECK(kitHash(kit, legacy) == carried);
    // A shield alone opens the block too, and is a different number.
    kit.add(buckler, 1);
    const std::uint64_t shieldCarried = kitHash(kit, legacy);
    REQUIRE(kit.wear(items, buckler));
    CHECK(kitHash(kit, legacy) != shieldCarried);
}

TEST_CASE("wearing gloves moves the room's hash; taking them off puts it back") {
    Room room(hourOfDay(14), 151, 63);
    Tavern& tavern = room.tavern();
    REQUIRE(tavern.giveItem("work_gloves"));
    const std::uint64_t carried = room.hash();
    const std::int32_t gloves = tavern.items().indexOf("work_gloves");
    REQUIRE(tavern.wearItem(gloves).line == "WORK GLOVES - ON.");
    CHECK(tavern.kit().worn(ItemSlot::Hands) == gloves);
    CHECK(tavern.wornDr() == 1);
    const std::uint64_t worn = room.hash();
    CHECK(worn != carried);
    REQUIRE(tavern.wearItem(gloves).line == "WORK GLOVES - OFF.");
    CHECK(room.hash() == carried);
}

// ---------------------------------------------------------------------------
// the comparison
// ---------------------------------------------------------------------------

TEST_CASE("compareGear is the wear rules run on a copy: DR, the span, reach, speed, and what comes off") {
    const ItemRegistry items = gearRegistry();
    const std::int32_t cudgel = items.indexOf("cudgel");
    const std::int32_t coat = items.indexOf("coat");
    const std::int32_t buckler = items.indexOf("buckler");
    const std::int32_t maul = items.indexOf("maul");
    const std::int32_t pot = items.indexOf("pot");
    const std::int32_t rope = items.indexOf("rope");
    Kit kit(items.size());
    for (const std::int32_t index : {cudgel, coat, buckler, maul, pot, rope}) {
        kit.add(index, 1);
    }
    REQUIRE(kit.wear(items, cudgel));
    REQUIRE(kit.wear(items, coat));
    REQUIRE(kit.wear(items, buckler));
    const std::vector<std::uint8_t> untouched = kit.encode(items);
    const GripTiming one = gripTiming(WeaponGrip::OneHanded);
    const GripTiming two = gripTiming(WeaponGrip::TwoHanded);

    // The maul over a cudgel and a buckler.
    const GearCompare toMaul = compareGear(kit, items, maul);
    CHECK(toMaul.applies);
    CHECK(toMaul.drNow == 3);
    CHECK(toMaul.drThen == 2);  // the buckler comes off
    CHECK(toMaul.showsDamage);
    CHECK(toMaul.dmgLoNow == 7);
    CHECK(toMaul.dmgHiNow == 9);
    CHECK(toMaul.dmgLoThen == 10);
    CHECK(toMaul.dmgHiThen == 16);
    CHECK(toMaul.reachNowQ8 == one.reachQ8);
    CHECK(toMaul.reachThenQ8 == two.reachQ8);
    CHECK(toMaul.recoveryNow == one.recoverySteps);
    CHECK(toMaul.recoveryThen == two.recoverySteps);
    const std::vector<ItemSlot> handAndShield = {ItemSlot::Hand, ItemSlot::OffHand};
    CHECK(toMaul.bares == handAndShield);
    CHECK(kit.encode(items) == untouched);  // pure

    // A pot on a bare head: armour only.
    const GearCompare toPot = compareGear(kit, items, pot);
    CHECK(toPot.applies);
    CHECK(toPot.drNow == 3);
    CHECK(toPot.drThen == 5);
    CHECK_FALSE(toPot.showsDamage);
    CHECK(toPot.bares.empty());

    // Nothing to compare: the row already worn, a row never worn, no row.
    CHECK_FALSE(compareGear(kit, items, cudgel).applies);
    CHECK_FALSE(compareGear(kit, items, rope).applies);
    CHECK_FALSE(compareGear(kit, items, -1).applies);

    // With the maul in both hands, the buckler would put the fist back.
    REQUIRE(kit.wear(items, maul));
    const GearCompare toShield = compareGear(kit, items, buckler);
    CHECK(toShield.applies);
    CHECK(toShield.drNow == 2);
    CHECK(toShield.drThen == 3);
    CHECK(toShield.showsDamage);
    CHECK(toShield.dmgLoNow == 10);
    CHECK(toShield.dmgHiNow == 16);
    CHECK(toShield.dmgLoThen == baseDamage(Weapon::Fists));
    CHECK(toShield.dmgHiThen == baseDamage(Weapon::Fists) + kStrikeVarianceMax);
    CHECK(toShield.reachNowQ8 == two.reachQ8);
    CHECK(toShield.reachThenQ8 == one.reachQ8);
    const std::vector<ItemSlot> handOnly = {ItemSlot::Hand};
    CHECK(toShield.bares == handOnly);
    // And what it promised is what the press does.
    REQUIRE(kit.wear(items, buckler));
    CHECK(kit.wornDr(items) == toShield.drThen);
    CHECK(kit.handDamageLo(items) == toShield.dmgLoThen);
    CHECK(kit.handDamageHi(items) == toShield.dmgHiThen);
}
