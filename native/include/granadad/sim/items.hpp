#pragma once

// THE KIT -- the things a body owns, and the record every one of them is.
//
// WHY THIS EXISTS. The Oblivion-feel gap analysis put it plainly: the game had
// every part of an inventory except the noun. The hand was one hashed enum
// byte (sim::Weapon), a stolen thing was an int on the crime ledger, the
// contraband was five counts in a sack, the picks were an int, a letter was a
// raws row -- five ad-hoc "things you hold" with no common record. Nothing
// could be picked up that was not authored as a bespoke tavern stand, nothing
// could be dropped, nothing worn, and the world renderer drew no item at all.
// D10 (the Oblivion roadmap) ruled the Kit a GO: inventory lives on the
// Character tile, gold stays weightless and is not an item, IN HAND is
// always printed once the Kit exists, an armour slot prints only when the
// raws hold an item for it, defence v1 is flat integer DR per worn piece, and
// WardActors carry, drop and yield nothing.
//
// TWO THINGS LIVE HERE.
//
//   THE REGISTRY   content/raws/items/items.json read the way spellbook.hpp
//                  reads the spells: tolerant, never throwing, document
//                  order. One row per thing a Granadad docker could own -- a
//                  cudgel, a knife, a coat, a lantern -- with its name, its
//                  weight in DRAMS, its worth in Royals, the slot it is worn
//                  in, the Weapon class it swings as, the span it does, the
//                  flat DR it turns, the heat the Watch adds per unit when it
//                  finds it on you, and one line about it in the ward's own
//                  register. The registry also carries where the world puts
//                  things (the authored stands) and what a roster body is
//                  carrying when it dies (the corpse kits, per role).
//
//   THE KIT        what the player is carrying: dense int32 counts per
//                  registry row -- the Stash pattern (contraband.hpp) copied
//                  whole: byte-encodable, hashed, no floats, no unordered
//                  containers -- plus the equip map, one worn row per slot.
//
// WEAPON SURVIVES AS THE ITEM'S CLASS COLUMN. brawl.hpp's Weapon enum is the
// brawl line's B1 (nothing edged is out) and strike()'s damage table, and
// neither is touched: a hand item names a class, the Tavern derives what the
// hand holds from the worn hand row, and strike()/classifyFight read the
// class exactly as before. A knife IS Edged; a mace IS Blunt.
//
// THE STASH STAYS THE ILLICIT VIEW. The five contraband goods are rows here
// too (so the sheet can print them and the 3D world can draw a sack), but
// their COUNTS live where they always did -- CrimeLedger::stash(), hashed
// under the ledger, codec v5, read by the Watch's load rule, the impound, the
// contract board and the radiant work. The Kit never stores a contraband
// count; Tavern::kitRows() composes one list out of both. Folding the sack
// into the Kit would have re-cut the court's codec and six readers for no
// feel the player could see -- he sees ONE list either way.
//
// THE LOAD. Every dram on the body -- the Kit, the sack, the bale on the
// shoulder -- against a budget off MIGHT (PROGRESSION-SPEC section 5's
// encumbrance cap, scaled to this build's units so the base sheet's budget
// is the sack's own 240): TAKE refuses past it, and the legs slow through a
// Q8 term composed at the one movement seam (Session::step, beside AGI's
// gait reader) -- 256 to half the budget, down to 128 at the budget. Player
// only; a WardActor never reads this.
//
// INTEGERS ONLY. Drams, Royals, DR, heat, counts and slots are all int32 and
// stay that way. Nothing here draws a random number.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/brawl.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the slots
// ---------------------------------------------------------------------------

/// Where a thing is worn. Append-only: the ordinal is hashed and written into
/// the Kit's encoding. None is a thing that is carried and never worn.
///
/// D10 ruled hand / body / head / trinket; FEET is the one addition, because
/// the authored set carries a pair of boots and a boot is not a trinket. The
/// sheet prints a worn slot only when the raws hold at least one item for it,
/// so an unused slot costs no row.
enum class ItemSlot : std::uint8_t {
    None = 0,
    Hand = 1,
    Body = 2,
    Head = 3,
    Feet = 4,
    Trinket = 5,
};

/// Slots a thing can be worn in: Hand..Trinket, dense from 1.
inline constexpr std::size_t kWornSlotCount = 5;
inline constexpr std::size_t kItemSlotCount = kWornSlotCount + 1;

/// The raws' own spelling ("hand", "body", "head", "feet", "trinket",
/// "none"), and back.
[[nodiscard]] std::string_view itemSlotSymbol(ItemSlot slot) noexcept;
[[nodiscard]] bool itemSlotFromSymbol(std::string_view symbol, ItemSlot& out) noexcept;
/// What the sheet calls the slot: IN HAND, ON THE BACK, ON THE HEAD, ON THE
/// FEET, AT THE BELT -- the reference sheet's worded rows.
[[nodiscard]] std::string_view itemSlotSheetLabel(ItemSlot slot) noexcept;
/// The dense index of a worn slot (Hand = 0 .. Trinket = 4), or -1 for None.
[[nodiscard]] constexpr std::int32_t wornSlotIndex(ItemSlot slot) noexcept {
    return slot == ItemSlot::None ? -1 : static_cast<std::int32_t>(slot) - 1;
}
[[nodiscard]] constexpr ItemSlot wornSlotAt(std::int32_t index) noexcept {
    return static_cast<ItemSlot>(index + 1);
}

// ---------------------------------------------------------------------------
// the record
// ---------------------------------------------------------------------------

/// One thing a docker could own. Straight out of the raws.
struct ItemDef {
    /// The raws key: snake_case, THE one spelling every other file uses.
    std::string id;
    /// What the sheet calls it. ASCII, upper case, short enough for a row.
    std::string name;
    /// Weight. What the load rule and the Watch's eye count.
    std::int32_t drams = 0;
    /// Worth, in Royals, to somebody who wants it.
    std::int32_t royals = 0;
    ItemSlot slot = ItemSlot::None;
    /// The brawl line's own class -- what strike() rolls and B1 reads. Fists
    /// for anything that is not a weapon.
    Weapon weaponClass = Weapon::Fists;
    /// The unrolled span a hand item does, base..base+kStrikeVarianceMax of
    /// its class. Filled from the class when the raws leave it out, and a
    /// row whose authored span disagrees with its class is REFUSED at load:
    /// the sheet and the roll must never disagree about a number.
    std::int32_t damageLo = 0;
    std::int32_t damageHi = 0;
    /// Flat damage reduction while worn. Defence v1: every landed blow on the
    /// body loses the sum of every worn piece's DR, floored at one point
    /// through any coat (the guard's own floor).
    std::int32_t dr = 0;
    /// What the Watch adds to its opinion of you, per unit, when it finds
    /// this on you. Zero for a thing anybody may carry.
    std::int32_t heat = 0;
    /// The contraband good this row is the sheet's face for ("dust",
    /// "moonshine"...), or empty. A row with this set is never counted in
    /// the Kit: its count is the sack's.
    std::string contraband;
    /// One line, in the ward's own register.
    std::string desc;
    /// A thing that stands where it is authored and is never carried: the
    /// strongbox. It has a record so the world can draw it and the prompt
    /// can name it; TAKE refuses it out loud.
    bool fixed = false;
};

/// Where the world puts a thing before anybody moves it.
struct ItemStand {
    std::string item;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    std::int32_t count = 1;
    /// Somebody's. Taking it is theft (a witness, heat, the roofs' standing)
    /// and the crosshair says THEIRS before the press.
    bool owned = false;
};

/// What a roster body is carrying when it dies. Keyed by the actor's NAME
/// first (a notable's own kit), then by actorRoleName(role).
struct CorpseKit {
    std::string key;
    std::vector<std::string> items;
};

/// The sheet's own line for a hand item: "CUDGEL 7-9 IMPACT", "KNIFE 11-13
/// EDGE" -- weaponSheetLine's idiom with the item's name in the class's seat.
[[nodiscard]] std::string itemSheetLine(const ItemDef& item);

/// Every item the raws know, in document order. Document order is
/// deliberate for the reason the spellbook's is: stable, the owner's, and a
/// sort here would be this file inventing a shop.
class ItemRegistry {
public:
    /// Reads content/raws/items/items.json under `contentDir`. Never throws:
    /// a missing or malformed file yields an empty registry, and `loaded()`
    /// says so. The game boots either way -- with no items there is nothing
    /// to take, and the sheet prints IN HAND FISTS.
    [[nodiscard]] static ItemRegistry load(const std::filesystem::path& contentDir);
    /// The same parse over a string, for a test that wants a small vocabulary
    /// without a file on disk.
    [[nodiscard]] static ItemRegistry fromJson(std::string_view json);

    [[nodiscard]] bool loaded() const noexcept { return !items_.empty(); }
    [[nodiscard]] const std::vector<ItemDef>& items() const noexcept { return items_; }
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

    /// The row with this id, or nullptr.
    [[nodiscard]] const ItemDef* find(std::string_view id) const noexcept;
    /// Its index (the Kit's own key), or -1.
    [[nodiscard]] std::int32_t indexOf(std::string_view id) const noexcept;
    /// The row at an index, or nullptr out of range.
    [[nodiscard]] const ItemDef* at(std::int32_t index) const noexcept;

    /// Where the world puts things, in document order.
    [[nodiscard]] const std::vector<ItemStand>& stands() const noexcept { return stands_; }
    /// The corpse kit for a name or a role key, or nullptr.
    [[nodiscard]] const CorpseKit* carriedBy(std::string_view key) const noexcept;

    /// True when at least one row is worn in this slot -- the rule the sheet
    /// prints a slot row by.
    [[nodiscard]] bool slotHasItems(ItemSlot slot) const noexcept;

    /// Rows the loader refused, by id and reason, for a report.
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

private:
    std::vector<ItemDef> items_;
    std::vector<ItemStand> stands_;
    std::vector<CorpseKit> carried_;
    std::vector<std::string> warnings_;
};

/// content/raws/items/items.json.
[[nodiscard]] std::filesystem::path itemRawsPath(const std::filesystem::path& contentDir);

/// The skill a blow turned by worn kit trains. content/raws/skills/
/// skills.json's own row: "harness", armour familiarity, governing VIG. Named
/// once here the way kBlockSkill is in brawl.hpp.
inline constexpr std::string_view kHarnessSkill = "harness";

// ---------------------------------------------------------------------------
// the load
// ---------------------------------------------------------------------------

/// The budget: kLoadBaseDrams + kLoadPerMight * MGT. At the base sheet's 40
/// that is 240 -- the sack's own kStashDrams, so a body that could carry ten
/// jars before the Kit existed can carry ten jars and nothing else after it.
/// PROGRESSION-SPEC section 5's 300 + 20 x MGT, in this build's units.
inline constexpr std::int32_t kLoadBaseDrams = 160;
inline constexpr std::int32_t kLoadPerMight = 2;
[[nodiscard]] constexpr std::int32_t loadCapDrams(std::int32_t might) noexcept {
    return kLoadBaseDrams + kLoadPerMight * might;
}

/// The legs under a load, Q8: 256 up to half the budget, then linearly down
/// to kLoadSpeedFloorQ8 at the budget and past it -- the soft slowdown the
/// reference's encumbrance has, with no hard stop because TAKE refuses past
/// the budget before the legs ever reach it. Pure integers; composed with
/// agilitySpeedScaleQ8 by the caller (a product, >> 8).
inline constexpr std::int32_t kLoadSpeedFloorQ8 = 128;
[[nodiscard]] constexpr std::int32_t loadSpeedScaleQ8(std::int32_t loadDrams,
                                                       std::int32_t capDrams) noexcept {
    if (capDrams <= 0 || loadDrams <= 0) {
        return 256;
    }
    const std::int32_t half = capDrams / 2;
    if (loadDrams <= half) {
        return 256;
    }
    const std::int32_t over = loadDrams - half;
    const std::int32_t span = capDrams - half;
    if (span <= 0 || over >= span) {
        return kLoadSpeedFloorQ8;
    }
    return 256 - ((256 - kLoadSpeedFloorQ8) * over) / span;
}

// ---------------------------------------------------------------------------
// what is on the body
// ---------------------------------------------------------------------------

/// Everything the player owns that is not coin and not the sack. Dense, in
/// registry order, so iteration order is the vocabulary's.
class Kit {
public:
    Kit() = default;
    explicit Kit(std::size_t rows) { resize(rows); }

    /// Sizes the counts to the registry. Counts past the new size are lost;
    /// a worn row past it is bared.
    void resize(std::size_t rows);
    [[nodiscard]] std::size_t rows() const noexcept { return counts_.size(); }

    [[nodiscard]] std::int32_t count(std::int32_t index) const noexcept;
    /// Every unit of every row.
    [[nodiscard]] std::int32_t units() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return units() == 0; }
    /// Drams, over the registry's weights. The sack and the bale are not in
    /// it; Tavern::loadDrams adds them.
    [[nodiscard]] std::int32_t weight(const ItemRegistry& registry) const noexcept;
    /// What the Watch would add to its opinion of you for what is in here.
    [[nodiscard]] std::int32_t heatIfSearched(const ItemRegistry& registry) const noexcept;

    /// Puts units in. Returns how many went in -- always `units` for a row
    /// in range; the LOAD rule is the owner's (it knows the sheet) and is
    /// checked before this is called.
    std::int32_t add(std::int32_t index, std::int32_t units);
    /// Takes units out. Returns how many were there to take. A row taken to
    /// zero is bared from any slot it was worn in.
    std::int32_t take(std::int32_t index, std::int32_t units);
    void clear() noexcept;

    /// The row worn in a slot, or -1.
    [[nodiscard]] std::int32_t worn(ItemSlot slot) const noexcept;
    /// True when this row is worn anywhere.
    [[nodiscard]] bool isWorn(std::int32_t index) const noexcept;
    /// Wears a row in `slot`. Refused (false) for a row not carried, a slot
    /// of None, or a row whose registry slot is not this one -- the caller
    /// passes the registry so a coat can never go on a head. Whatever was
    /// worn there is bared.
    bool wear(const ItemRegistry& registry, std::int32_t index);
    /// Bares a slot. Idempotent.
    void bare(ItemSlot slot) noexcept;
    /// The sum of every worn piece's DR. Hand items turn nothing in v1.
    [[nodiscard]] std::int32_t wornDr(const ItemRegistry& registry) const noexcept;
    /// The class of the worn hand row, or Fists.
    [[nodiscard]] Weapon handClass(const ItemRegistry& registry) const noexcept;

    /// By ID, not by index: an appended raws row does not move a saved Kit.
    [[nodiscard]] std::vector<std::uint8_t> encode(const ItemRegistry& registry) const;
    /// False for a malformed image, a version this build does not read, or an
    /// id the registry no longer knows -- a save that names a thing the raws
    /// have lost is refused whole rather than quietly emptied of it.
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes,
                                     const ItemRegistry& registry, Kit& out);
    void hashInto(HashSink& sink) const;

private:
    std::vector<std::int32_t> counts_;
    /// Dense by wornSlotIndex, -1 empty.
    std::array<std::int32_t, kWornSlotCount> worn_{{-1, -1, -1, -1, -1}};
};

// ---------------------------------------------------------------------------
// what is on the ground
// ---------------------------------------------------------------------------

/// A thing lying on a tile: authored at a stand, or dropped there. Hashed by
/// the room that owns the list; drawn by the 3D world from the tile.
struct GroundItem {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    /// Registry index.
    std::int32_t item = -1;
    std::int32_t count = 1;
    /// Somebody's: taking it is theft.
    bool owned = false;
};

/// What has been taken off one corpse: bit i set once row i of its corpse
/// kit is gone. Appended on the first search, hashed with the rest.
struct CorpseLoot {
    std::int32_t actorId = -1;
    std::uint32_t takenMask = 0;
};

/// The codec for a whole ground list, the Kit's shape: magic, version, then
/// each entry with its item by id. For the day a save frame exists.
[[nodiscard]] std::vector<std::uint8_t> encodeGround(const std::vector<GroundItem>& ground,
                                                     const ItemRegistry& registry);
[[nodiscard]] bool decodeGround(const std::vector<std::uint8_t>& bytes,
                                const ItemRegistry& registry, std::vector<GroundItem>& out);

}  // namespace granadad::sim
