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

#include "granadad/sim/attributes.hpp"
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
///
/// PHASE 9 (OBLIVION-PLAN 8.3) APPENDS OFFHAND AND HANDS. The plan's text
/// says "insert"; they are appended instead, so no shipped ordinal moves and
/// nothing hashed or encoded under 1..5 is reinterpreted: the Kit codec's
/// version 1 and 2 images keep their five-slot block and decode onto
/// Hand..Trinket, and the Kit's hash writes the first five slots exactly as
/// before (Loadout::hashInto). Where the two new slots SHOW is the screen's
/// business -- kSlotScreenOrder -- not the ordinal's.
enum class ItemSlot : std::uint8_t {
    None = 0,
    Hand = 1,
    Body = 2,
    Head = 3,
    Feet = 4,
    Trinket = 5,
    /// The other hand: a shield, and nothing else in this build.
    OffHand = 6,
    /// Gloves and gauntlets.
    Hands = 7,
};

/// Slots a thing can be worn in: Hand..Hands, dense from 1.
inline constexpr std::size_t kWornSlotCount = 7;
inline constexpr std::size_t kItemSlotCount = kWornSlotCount + 1;
/// The worn slots the Kit knew before phase 9 (Hand..Trinket): the size of
/// the version 1 and 2 codec's worn block, and the head of the hash.
inline constexpr std::size_t kLegacyWornSlotCount = 5;

/// The order the Character tile lists the seven worn rows in: the hands
/// first (what you hit with and what you block with), then head to foot,
/// then the belt. A screen order, never an ordinal.
inline constexpr ItemSlot kSlotScreenOrder[kWornSlotCount] = {
    ItemSlot::Hand,  ItemSlot::OffHand, ItemSlot::Head,    ItemSlot::Body,
    ItemSlot::Hands, ItemSlot::Feet,    ItemSlot::Trinket,
};

/// The raws' own spelling ("hand", "body", "head", "feet", "trinket",
/// "offhand", "hands", "none"), and back.
[[nodiscard]] std::string_view itemSlotSymbol(ItemSlot slot) noexcept;
[[nodiscard]] bool itemSlotFromSymbol(std::string_view symbol, ItemSlot& out) noexcept;
/// What the sheet calls the slot: IN HAND, ON THE BACK, ON THE HEAD, ON THE
/// FEET, AT THE BELT, IN THE OTHER HAND, ON THE HANDS -- the reference
/// sheet's worded rows.
[[nodiscard]] std::string_view itemSlotSheetLabel(ItemSlot slot) noexcept;
/// The pack's one-word name for the slot: HAND, OFF-HAND, HEAD, BODY, HANDS,
/// FEET, TRINKET (CARRIED for None).
[[nodiscard]] std::string_view itemSlotShortWord(ItemSlot slot) noexcept;
/// The dense index of a worn slot (Hand = 0 .. Hands = 6), or -1 for None.
[[nodiscard]] constexpr std::int32_t wornSlotIndex(ItemSlot slot) noexcept {
    return slot == ItemSlot::None ? -1 : static_cast<std::int32_t>(slot) - 1;
}
[[nodiscard]] constexpr ItemSlot wornSlotAt(std::int32_t index) noexcept {
    return static_cast<ItemSlot>(index + 1);
}

// ---------------------------------------------------------------------------
// the grip
// ---------------------------------------------------------------------------

/// How a thing is held -- the gear class OBLIVION-PLAN 8.3 names (one-handed,
/// two-handed, shield). Append-only. A Hand row is OneHanded or TwoHanded; an
/// OffHand row is a Shield; every other row is OneHanded by default and the
/// grip is never read for it. Never hashed: the Kit hashes rows, and a row's
/// grip is the raws'.
enum class WeaponGrip : std::uint8_t {
    OneHanded = 0,
    TwoHanded = 1,
    Shield = 2,
};

/// What a grip does to the swing machine: how far the blow reaches (Q8, down
/// the look-ray), and how long the arm is locked out after a swing and a hard
/// swing, and how long a hold must be to make a swing HARD, in movement
/// steps. Per grip, not per row -- the plan's "reach and speed per weapon
/// class" -- so the table below is the whole of it and a raws row cannot
/// author a reach.
struct GripTiming {
    std::int32_t reachQ8 = 0;
    std::int32_t recoverySteps = 0;
    std::int32_t hardRecoverySteps = 0;
    std::int32_t hardHoldSteps = 0;
};

/// The table. ONE-HANDED IS EXACTLY THE SHIPPED SWING: kMeleeReach, the 36
/// and 54 step lockouts and the 15 step hard hold (tavern.hpp's
/// kSwingRecoverySteps, kHardSwingRecoverySteps and kHardSwingHoldSteps,
/// static_asserted equal there) -- bare fists and every items.json weapon
/// are one-handed, so nothing that existed before this table moves by a
/// step. TWO-HANDED reaches a tile and five eighths (416) and pays for it a
/// third slower on every count: 48 and 72 to recover, 21 to wind a hard
/// swing.
///
/// A SHIELD IS NEVER SWUNG, AND THE SWING CAN NEVER ASK FOR IT. The only
/// reader is the player's swing, through Kit::handGrip -- the grip of the row
/// in the HAND -- and the loader refuses a Hand row whose grip is "shield"
/// (ItemRegistry::appendRows), so no Kit ever holds one there. Its case exists
/// only because the switch must name every enumerator (-Wswitch); it returns
/// the one-handed row, static_asserted below, so that if the refusal were
/// ever lost the swing would read the arm's own numbers and nothing more.
[[nodiscard]] constexpr GripTiming gripTiming(WeaponGrip grip) noexcept {
    switch (grip) {
        case WeaponGrip::OneHanded:
            return GripTiming{kMeleeReach, 36, 54, 15};
        case WeaponGrip::TwoHanded:
            return GripTiming{416, 48, 72, 21};
        case WeaponGrip::Shield:
            return GripTiming{kMeleeReach, 36, 54, 15};
    }
    return GripTiming{kMeleeReach, 36, 54, 15};
}
static_assert(gripTiming(WeaponGrip::Shield).reachQ8 == gripTiming(WeaponGrip::OneHanded).reachQ8 &&
              gripTiming(WeaponGrip::Shield).recoverySteps ==
                  gripTiming(WeaponGrip::OneHanded).recoverySteps &&
              gripTiming(WeaponGrip::Shield).hardRecoverySteps ==
                  gripTiming(WeaponGrip::OneHanded).hardRecoverySteps &&
              gripTiming(WeaponGrip::Shield).hardHoldSteps ==
                  gripTiming(WeaponGrip::OneHanded).hardHoldSteps,
              "the unreachable shield row is the arm's own");

/// The raws' spelling: "one", "two", "shield".
[[nodiscard]] std::string_view weaponGripSymbol(WeaponGrip grip) noexcept;
[[nodiscard]] bool weaponGripFromSymbol(std::string_view symbol, WeaponGrip& out) noexcept;

// ---------------------------------------------------------------------------
// the record
// ---------------------------------------------------------------------------

/// What a USE does to the body, in the integer effect vocabulary the spells
/// already speak (spellforge.hpp's Vitality and Attribute axes). Nothing
/// else is invented: Temperature has no reader in this ward, and the phase
/// that owns a new effect kind is the spell phase, not this one.
enum class ItemEffectKind : std::uint8_t {
    /// Hit points, signed. A mend is positive. Instant.
    Vitality = 0,
    /// A held tuning of one attribute, clamped like a spell's (+/-2 over
    /// every hold that is live), for `seconds` of room time.
    Attribute = 1,
};

struct ItemEffect {
    ItemEffectKind kind = ItemEffectKind::Vitality;
    std::int32_t magnitude = 0;
    /// The string a held tuning turns. Meaningless for Vitality.
    AttributeId attribute = AttributeId::Might;
    /// How long a held tuning lasts, in room seconds. Zero for Vitality.
    std::int32_t seconds = 0;
};

/// What the pack groups a row under. Derived from the row (never authored,
/// so a raws file cannot disagree with the rule): a hand item is a WEAPON, a
/// worn body, head, feet or hands piece is ARMOUR and so is a shield in the
/// other hand (it is worn for what it turns, and it turns DR like a coat
/// does; it is never swung), anything with a use is a
/// CONSUMABLE, a sack good is GOODS, and the rest -- ropes, picks, lamps,
/// purses, letters -- is GEAR. The numeric order is the pack's sort order.
enum class ItemCategory : std::uint8_t {
    Weapon = 0,
    Armour = 1,
    Consumable = 2,
    Gear = 3,
    Goods = 4,
};
inline constexpr std::size_t kItemCategoryCount = 5;

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
    /// The unrolled span a hand item does. Filled from the class
    /// (base..base+kStrikeVarianceMax) when the raws leave it out. A row of
    /// the FIRST file (items.json) whose authored span disagrees with its
    /// class is REFUSED at load -- those rows promise the class's roll. A row
    /// of an extra file (items_gear.json) may author its own span, which is
    /// what the player's blow rolls (strikeWithSpan): lo >= 1, hi >= lo, and
    /// no wider than kStrikeSpanWidthMax, or it is refused. Either way the
    /// sheet and the roll never disagree about a number.
    std::int32_t damageLo = 0;
    std::int32_t damageHi = 0;
    /// How it is held. One-handed unless the raws say "two" (a Hand row) or
    /// it is an OffHand row (always a Shield). See WeaponGrip.
    WeaponGrip grip = WeaponGrip::OneHanded;
    /// The render key for the thing ("maul", "buckler"): which prop the
    /// world, the viewmodel and the figure draw for it. Empty for a row that
    /// names none (every items.json row), where the renderer falls back by
    /// class. Never hashed, never read by a rule.
    std::string model;
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
    /// The span the thing is DRAWN over lying on a tile, in centimetres
    /// above the floor (`drawnCm` in the raws: the 3D items table's lift,
    /// scale and pitch over its mesh, pinned against the meshes in
    /// test_kit_page). Read by the crosshair only (Session::
    /// groundItemUnderReticle) -- never by a rule, never hashed: the reticle
    /// has to be ON the thing to name it. A hand's thing at the feet when
    /// the raws leave it out.
    std::int32_t drawnLoCm = 0;
    std::int32_t drawnHiCm = 30;
    /// What USE does, in the order it is applied; empty for a thing that
    /// cannot be used. A row with effects is a consumable: one unit is spent
    /// by every use.
    std::vector<ItemEffect> effects;
    /// The verb the pack prints for it ("EAT", "DRINK", "BIND"). Empty when
    /// the row has no effects.
    std::string useVerb;
    /// The same verb done: "ATE", "DRANK", "BOUND" -- the message line's word.
    std::string useDone;
};

/// The pack's group for a row; see ItemCategory.
[[nodiscard]] ItemCategory itemCategory(const ItemDef& item) noexcept;
/// "WEAPONS", "ARMOUR", "CONSUMABLES", "GEAR", "GOODS".
[[nodiscard]] std::string_view itemCategoryName(ItemCategory category) noexcept;
/// One line saying what a use does: "MENDS 8", "MIGHT +1 FOR 300 S", or
/// both joined with ", ". Empty for a row with no effects. Pure.
[[nodiscard]] std::string itemEffectLine(const ItemDef& item);

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
    /// Reads content/raws/items/items.json under `contentDir`, then every
    /// file of kExtraItemFiles beside it (items_consumables.json), appended
    /// in that order: `items.json` is the owner's and is never edited, so a
    /// phase's new rows ride in a file of their own and land AFTER every row
    /// the first file holds -- an old save's ids and the old rows' indices
    /// stay where they were. Never throws:
    /// a missing or malformed file yields an empty registry, and `loaded()`
    /// says so. The game boots either way -- with no items there is nothing
    /// to take, and the sheet prints IN HAND FISTS.
    [[nodiscard]] static ItemRegistry load(const std::filesystem::path& contentDir);
    /// The same parse over a string, for a test that wants a small vocabulary
    /// without a file on disk.
    [[nodiscard]] static ItemRegistry fromJson(std::string_view json);

    /// The same parse over a string, appended to what is already loaded: the
    /// way an extra file's rows join the registry. Ids already present are
    /// refused with a warning, never overwritten. Rows appended here are
    /// EXTRA rows: a hand row may author its own damage span (see
    /// ItemDef::damageLo); the first file's rows (fromJson) may not.
    void appendJson(std::string_view json);
    /// How many rows the first file (items.json) holds -- the rows the Kit's
    /// hash has always covered. Rows from `baseRows()` on came from an extra
    /// file; see Kit::hashInto on why the hash tells them apart.
    [[nodiscard]] std::size_t baseRows() const noexcept { return baseRows_; }

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
    /// The one parse. `firstFile` is true for the base file's rows, which
    /// keep the class-span refusal.
    void appendRows(std::string_view json, bool firstFile);

    std::vector<ItemDef> items_;
    std::vector<ItemStand> stands_;
    std::vector<CorpseKit> carried_;
    std::vector<std::string> warnings_;
    std::size_t baseRows_ = 0;
};

/// content/raws/items/items.json.
[[nodiscard]] std::filesystem::path itemRawsPath(const std::filesystem::path& contentDir);
/// The extra files read after it, in order: the Phase 8 consumables, then
/// the Phase 9 gear (weapons by grip, shields, armour by piece). Every row of
/// a later file lands after every row of an earlier one, so no saved index
/// and no hashed index of an earlier row ever moves.
inline constexpr std::string_view kExtraItemFiles[] = {"items_consumables.json",
                                                       "items_gear.json"};

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
// the inspect record
// ---------------------------------------------------------------------------

/// Everything the pack's detail pane says about one row, as numbers and words
/// and no layout: the raws' own columns, what is carried of it, whether it is
/// worn and how much of it is somebody else's. A pure function of its inputs,
/// so a case can pin the record without a screen (and the screen can never
/// say a number the sim would not).
struct ItemInspect {
    std::string name;
    std::string desc;
    ItemCategory category = ItemCategory::Gear;
    std::int32_t count = 0;
    /// One unit's weight and worth.
    std::int32_t unitDrams = 0;
    std::int32_t unitRoyals = 0;
    /// A hand item's span (both zero for everything else) and the sheet's
    /// word for its class: "IMPACT" or "EDGE".
    std::int32_t damageLo = 0;
    std::int32_t damageHi = 0;
    std::string damageWord;
    /// How a hand or off-hand row is held; OneHanded for everything else.
    WeaponGrip grip = WeaponGrip::OneHanded;
    std::int32_t dr = 0;
    /// What the Watch adds per unit when it finds one on you.
    std::int32_t heat = 0;
    bool worn = false;
    /// Units lifted off somebody (THEIRS); at most `count`.
    std::int32_t stolen = 0;
    /// What a use does ("MENDS 8"), and the verb, or both empty.
    std::string useVerb;
    std::string useLine;
    bool fixed = false;
};

[[nodiscard]] ItemInspect inspectItem(const ItemDef& item, std::int32_t count, bool worn,
                                      std::int32_t stolen);

// ---------------------------------------------------------------------------
// what is worn
// ---------------------------------------------------------------------------

/// THE LOADOUT: one worn row per slot, dense by wornSlotIndex, -1 bare.
/// Phase 9's gear state, with its OWN codec (OBLIVION-PLAN 8.1 rule 2: each
/// phase ships the codec for the state it adds). It knows no counts and no
/// wear rules -- set() puts any index anywhere -- because both belong to the
/// Kit that owns it: Kit::wear checks the row is carried and fits the slot
/// and applies the two-hand rule, then writes here.
class Loadout {
public:
    /// The row worn in a slot, or -1 (None is never worn).
    [[nodiscard]] std::int32_t worn(ItemSlot slot) const noexcept;
    /// True when this row is worn in any slot.
    [[nodiscard]] bool isWorn(std::int32_t index) const noexcept;
    /// Writes a row into a slot. No checks; see the class header. None is
    /// ignored.
    void set(ItemSlot slot, std::int32_t index) noexcept;
    /// Bares a slot. Idempotent.
    void bare(ItemSlot slot) noexcept;
    /// Bares every slot.
    void clear() noexcept;

    /// Magic "GL", version 1, then the slot count (kWornSlotCount) and one
    /// length-prefixed id per slot in wornSlotIndex order, empty for bare --
    /// BY ID, the Kit's rule, so an appended raws row never moves a saved
    /// loadout. A worn index the registry does not know is written bare.
    [[nodiscard]] std::vector<std::uint8_t> encode(const ItemRegistry& registry) const;
    /// False (and `out` untouched) for a wrong magic or version, a slot
    /// count that is not this build's, a truncated or over-long image, an id
    /// the raws no longer know, a row worn in a slot that is not its own, or
    /// a two-handed hand with something in the other hand -- a state Kit::
    /// wear can never make, so an image that holds one is lying.
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes,
                                     const ItemRegistry& registry, Loadout& out);
    /// THE HASH KEEPS EVERY OLD BASELINE PUT. The first five slots
    /// (Hand..Trinket) are written exactly as the five-slot Kit wrote them;
    /// then, ONLY when OffHand or Hands holds something, the marker
    /// 0x4B49544C ("KITL") and those two rows. A run that never wears a
    /// shield or gloves hashes byte for byte what it hashed before phase 9.
    void hashInto(HashSink& sink) const;

    [[nodiscard]] bool operator==(const Loadout& other) const noexcept = default;

private:
    std::array<std::int32_t, kWornSlotCount> worn_{{-1, -1, -1, -1, -1, -1, -1}};
};

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
    /// zero is bared from any slot it was worn in. Units that are not
    /// somebody else's go first: what is left still carries its THEIRS.
    std::int32_t take(std::int32_t index, std::int32_t units);
    void clear() noexcept;

    /// How many units of this row were lifted off somebody -- the pack's
    /// THEIRS. Never more than the row's count. Carrying it changes nothing
    /// about the Watch (the crime was judged the moment it was lifted); it is
    /// the record the inspect pane reads.
    [[nodiscard]] std::int32_t stolen(std::int32_t index) const noexcept;
    /// Marks `units` of a carried row as lifted. Clamped to what is carried.
    void markStolen(std::int32_t index, std::int32_t units);

    /// The row worn in a slot, or -1.
    [[nodiscard]] std::int32_t worn(ItemSlot slot) const noexcept;
    /// True when this row is worn anywhere.
    [[nodiscard]] bool isWorn(std::int32_t index) const noexcept;
    /// Wears a row in its registry slot. Refused (false) for a row not
    /// carried, a row of slot None, or a fixed row -- the caller passes the
    /// registry so a coat can never go on a head. Whatever was worn there is
    /// bared, and so is whatever THE TWO-HAND RULE takes off: a two-handed
    /// row in the hand bares the other hand, and a shield put on while a
    /// two-handed row is in the hand bares the hand.
    bool wear(const ItemRegistry& registry, std::int32_t index);
    /// What wearing this row would take off, before the press, so the screen
    /// can say it: its slot when something else is in it, then the slot the
    /// two-hand rule would bare when something is in that. Empty for a row
    /// already worn, a row that is never worn, or an index out of range.
    /// Asks only what WEARING does, not whether the row is carried.
    [[nodiscard]] std::vector<ItemSlot> wouldBare(const ItemRegistry& registry,
                                                  std::int32_t index) const;
    /// Bares a slot. Idempotent.
    void bare(ItemSlot slot) noexcept;
    /// The sum of every worn piece's DR -- every slot but the hand, so a
    /// shield in the other hand and gloves count like a coat. Hand items
    /// turn nothing in v1.
    [[nodiscard]] std::int32_t wornDr(const ItemRegistry& registry) const noexcept;
    /// The class of the worn hand row, or Fists.
    [[nodiscard]] Weapon handClass(const ItemRegistry& registry) const noexcept;
    /// The span the player's blow rolls: the worn hand row's own span, or,
    /// with the hand bare, `bareClass`'s span (baseDamage..+kStrikeVarianceMax)
    /// -- Fists, or the class Tavern::setPlayerCombat armed with no row behind
    /// it. handSpanOf below is the one function; the blow and the inspect
    /// pane both read it.
    [[nodiscard]] std::int32_t handDamageLo(const ItemRegistry& registry,
                                            Weapon bareClass = Weapon::Fists) const noexcept;
    [[nodiscard]] std::int32_t handDamageHi(const ItemRegistry& registry,
                                            Weapon bareClass = Weapon::Fists) const noexcept;
    /// The worn hand row's grip, or OneHanded bare (fists swing one-handed).
    [[nodiscard]] WeaponGrip handGrip(const ItemRegistry& registry) const noexcept;
    /// Every worn row, as its own state with its own codec.
    [[nodiscard]] const Loadout& loadout() const noexcept { return loadout_; }

    /// By ID, not by index: an appended raws row does not move a saved Kit.
    /// Version 3 (phase 9): magic, version, the counts as before, then a u32
    /// length and the Loadout's own image (Loadout::encode), then the stolen
    /// block. Versions 1 and 2 still decode: their five-slot worn block maps
    /// onto Hand..Trinket and the other hand and the hands decode bare;
    /// version 1 has no stolen block (nothing stolen).
    [[nodiscard]] std::vector<std::uint8_t> encode(const ItemRegistry& registry) const;
    /// False for a malformed image, a version this build does not read, or an
    /// id the registry no longer knows -- a save that names a thing the raws
    /// have lost is refused whole rather than quietly emptied of it.
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes,
                                     const ItemRegistry& registry, Kit& out);
    /// `legacyRows` is the registry's baseRows(): the first `legacyRows`
    /// counts and the worn rows hash exactly as they did before any extra
    /// raws file existed, and rows past it hash as a tail that is written
    /// ONLY when one of them holds something (a count, or a stolen unit).
    /// An appended raws file therefore moves no hash for a body that holds
    /// none of its rows, which is what keeps the twin-run baselines of a
    /// workload that never touches the new rows exactly where they were.
    /// The worn rows are Loadout::hashInto's, which keeps the same promise
    /// for the two slots phase 9 appended.
    void hashInto(HashSink& sink, std::size_t legacyRows) const;
    /// Every row, as before (a Kit built over one file).
    void hashInto(HashSink& sink) const { hashInto(sink, counts_.size()); }

private:
    std::vector<std::int32_t> counts_;
    /// Units of each row that are somebody's, dense with counts_.
    std::vector<std::int32_t> stolen_;
    /// What is worn, one row per slot.
    Loadout loadout_;
};

// ---------------------------------------------------------------------------
// the comparison
// ---------------------------------------------------------------------------

/// What the inspect pane's comparison line says about wearing a row: the
/// numbers now, and the numbers after, from the sim's own rules (Kit::
/// wornDr, the hand's span, gripTiming) and nothing else, so the line can
/// never promise what the press will not do.
struct GearCompare {
    /// The row is wearable (a slot, not fixed) and not the one already worn
    /// there. Nothing else is meaningful when false.
    bool applies = false;
    /// Kit::wornDr now, and after wearing the row.
    std::int32_t drNow = 0;
    std::int32_t drThen = 0;
    /// A Hand row, or a row whose wearing would bare the hand: the damage
    /// span, reach and speed change.
    bool showsDamage = false;
    std::int32_t dmgLoNow = 0;
    std::int32_t dmgHiNow = 0;
    std::int32_t dmgLoThen = 0;
    std::int32_t dmgHiThen = 0;
    /// gripTiming(hand grip).reachQ8 now and after.
    std::int32_t reachNowQ8 = 0;
    std::int32_t reachThenQ8 = 0;
    /// gripTiming(hand grip).recoverySteps now and after: the tap's lockout,
    /// the swing's speed.
    std::int32_t recoveryNow = 0;
    std::int32_t recoveryThen = 0;
    /// Kit::wouldBare -- what the press takes off.
    std::vector<ItemSlot> bares;
};

/// The span a loadout's hand rolls: the worn hand row's own, or, bare,
/// `bareClass`'s class span. THE ONE FUNCTION behind the player's blow
/// (Tavern::playerDamageLo/Hi, through Kit::handDamageLo/Hi) and the pane's
/// comparison, so the two can never disagree.
struct DamageSpan {
    std::int32_t lo = 0;
    std::int32_t hi = 0;
};
[[nodiscard]] DamageSpan handSpanOf(const Loadout& loadout, const ItemRegistry& registry,
                                    Weapon bareClass) noexcept;

/// The comparison for wearing row `index` over what `kit` wears now. Pure:
/// the kit is not touched. Asks what wearing does, not whether the row is
/// carried (the pane only shows carried rows). `bareClass` is what a bare
/// hand swings as NOW (Tavern::compareGear passes the class setPlayerCombat
/// armed); wearing that changes the hand resets it to Fists, as
/// Tavern::wearItem does, so THEN follows the press exactly.
[[nodiscard]] GearCompare compareGear(const Kit& kit, const ItemRegistry& registry,
                                      std::int32_t index, Weapon bareClass = Weapon::Fists);

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
