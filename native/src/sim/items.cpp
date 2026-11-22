#include "granadad/sim/items.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

constexpr std::uint8_t kKitMagic0 = 'G';
constexpr std::uint8_t kKitMagic1 = 'K';
/// Phase 9: the worn rows are the Loadout's own image, length-prefixed.
constexpr std::uint8_t kKitVersion = 3;
/// The versions the decoder still reads: the five-slot worn block with the
/// stolen counts (phase 8), and without them (the Kit build).
constexpr std::uint8_t kKitVersionFiveSlots = 2;
constexpr std::uint8_t kKitVersionNoStolen = 1;
/// The marker that opens the hash's extension tail; see Kit::hashInto.
constexpr std::uint32_t kKitTailMarker = 0x4B49544FU;  // "KITO"
constexpr std::uint32_t kKitStolenMarker = 0x4B495453U;  // "KITS"
/// The marker before the two slots phase 9 appended; see Loadout::hashInto.
constexpr std::uint32_t kLoadoutTailMarker = 0x4B49544CU;  // "KITL"
constexpr std::uint8_t kLoadoutMagic0 = 'G';
constexpr std::uint8_t kLoadoutMagic1 = 'L';
constexpr std::uint8_t kLoadoutVersion = 1;
constexpr std::uint8_t kGroundMagic0 = 'G';
constexpr std::uint8_t kGroundMagic1 = 'D';
constexpr std::uint8_t kGroundVersion = 1;

/// The same tolerant readers spellbook.cpp keeps: a field somebody has not
/// written yet is a fallback, never a boot failure over a content edit.
[[nodiscard]] std::int32_t intOr(const nlohmann::json& node, const char* key,
                                 std::int32_t fallback) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

[[nodiscard]] std::string stringOr(const nlohmann::json& node, const char* key,
                                   const char* fallback) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return std::string(fallback);
    }
    return found->get<std::string>();
}

[[nodiscard]] bool boolOr(const nlohmann::json& node, const char* key, bool fallback) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_boolean()) {
        return fallback;
    }
    return found->get<bool>();
}

/// The raws' word for a brawl class ("fists", "improvised", "blunt",
/// "evictor", "edged") -- weaponName's own spellings, plus the evictor's id.
[[nodiscard]] bool weaponFromSymbol(std::string_view symbol, Weapon& out) noexcept {
    if (symbol == "fists" || symbol.empty()) {
        out = Weapon::Fists;
        return true;
    }
    if (symbol == "improvised") {
        out = Weapon::Improvised;
        return true;
    }
    if (symbol == "blunt" || symbol == "cudgel") {
        out = Weapon::Blunt;
        return true;
    }
    if (symbol == "evictor" || symbol == "the evictor") {
        out = Weapon::Evictor;
        return true;
    }
    if (symbol == "edged") {
        out = Weapon::Edged;
        return true;
    }
    return false;
}

void putI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFU));
}

[[nodiscard]] bool takeI32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                           std::int32_t& out) {
    if (cursor + 4 > bytes.size()) {
        return false;
    }
    const std::uint32_t bits = static_cast<std::uint32_t>(bytes[cursor]) |
                               (static_cast<std::uint32_t>(bytes[cursor + 1]) << 8) |
                               (static_cast<std::uint32_t>(bytes[cursor + 2]) << 16) |
                               (static_cast<std::uint32_t>(bytes[cursor + 3]) << 24);
    cursor += 4;
    out = static_cast<std::int32_t>(bits);
    return true;
}

/// A length-prefixed id: one byte of length (an id is short by construction;
/// the loader refuses longer), then the bytes.
void putId(std::vector<std::uint8_t>& out, std::string_view id) {
    out.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(id.size(), 255)));
    for (std::size_t i = 0; i < id.size() && i < 255; ++i) {
        out.push_back(static_cast<std::uint8_t>(id[i]));
    }
}

[[nodiscard]] bool takeId(const std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                          std::string& out) {
    if (cursor >= bytes.size()) {
        return false;
    }
    const std::size_t length = bytes[cursor];
    ++cursor;
    if (cursor + length > bytes.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data() + cursor), length);
    cursor += length;
    return true;
}

/// An id the codec can carry and a JSON key can hold: 1..255 bytes.
constexpr std::size_t kMaxIdBytes = 255;

/// The grip of the row worn in the hand, OneHanded bare.
[[nodiscard]] WeaponGrip handGripOf(const Loadout& loadout, const ItemRegistry& registry) noexcept {
    const ItemDef* item = registry.at(loadout.worn(ItemSlot::Hand));
    return item != nullptr ? item->grip : WeaponGrip::OneHanded;
}

/// THE WEAR RULES, on a loadout: the row goes into its own slot (whatever
/// was there is bared by the write), and the two-hand rule bares the other
/// hand -- a two-handed row bares the off hand, a shield bares a two-handed
/// row out of the hand. False, and nothing written, for a row that is never
/// worn. Counts are the Kit's to check; compareGear runs this on a copy.
bool wearInto(Loadout& loadout, const ItemRegistry& registry, std::int32_t index) noexcept {
    const ItemDef* item = registry.at(index);
    if (item == nullptr || item->slot == ItemSlot::None || item->fixed) {
        return false;
    }
    if (item->slot == ItemSlot::Hand && item->grip == WeaponGrip::TwoHanded) {
        loadout.bare(ItemSlot::OffHand);
    }
    if (item->slot == ItemSlot::OffHand &&
        handGripOf(loadout, registry) == WeaponGrip::TwoHanded) {
        loadout.bare(ItemSlot::Hand);
    }
    loadout.set(item->slot, index);
    return true;
}

/// The span a loadout's hand rolls: the worn hand row's own, or the fist's.
void handSpanOf(const Loadout& loadout, const ItemRegistry& registry, std::int32_t& lo,
                std::int32_t& hi) noexcept {
    const ItemDef* item = registry.at(loadout.worn(ItemSlot::Hand));
    if (item != nullptr) {
        lo = item->damageLo;
        hi = item->damageHi;
        return;
    }
    lo = baseDamage(Weapon::Fists);
    hi = lo + kStrikeVarianceMax;
}

/// Every non-hand slot's DR, summed: Kit::wornDr's rule over a loadout.
[[nodiscard]] std::int32_t drOf(const Loadout& loadout, const ItemRegistry& registry) noexcept {
    std::int32_t dr = 0;
    for (std::size_t i = 0; i < kWornSlotCount; ++i) {
        const ItemSlot slot = wornSlotAt(static_cast<std::int32_t>(i));
        if (slot == ItemSlot::Hand) {
            continue;
        }
        const ItemDef* item = registry.at(loadout.worn(slot));
        if (item != nullptr) {
            dr += item->dr;
        }
    }
    return dr;
}

}  // namespace

// ---------------------------------------------------------------------------
// the slots
// ---------------------------------------------------------------------------

std::string_view itemSlotSymbol(ItemSlot slot) noexcept {
    switch (slot) {
        case ItemSlot::None:
            return "none";
        case ItemSlot::Hand:
            return "hand";
        case ItemSlot::Body:
            return "body";
        case ItemSlot::Head:
            return "head";
        case ItemSlot::Feet:
            return "feet";
        case ItemSlot::Trinket:
            return "trinket";
        case ItemSlot::OffHand:
            return "offhand";
        case ItemSlot::Hands:
            return "hands";
    }
    return "none";
}

bool itemSlotFromSymbol(std::string_view symbol, ItemSlot& out) noexcept {
    for (std::size_t i = 0; i < kItemSlotCount; ++i) {
        const ItemSlot slot = static_cast<ItemSlot>(i);
        if (itemSlotSymbol(slot) == symbol) {
            out = slot;
            return true;
        }
    }
    return false;
}

std::string_view itemSlotSheetLabel(ItemSlot slot) noexcept {
    switch (slot) {
        case ItemSlot::None:
            return "CARRIED";
        case ItemSlot::Hand:
            return "IN HAND";
        case ItemSlot::Body:
            return "ON THE BACK";
        case ItemSlot::Head:
            return "ON THE HEAD";
        case ItemSlot::Feet:
            return "ON THE FEET";
        case ItemSlot::Trinket:
            return "AT THE BELT";
        case ItemSlot::OffHand:
            return "IN THE OTHER HAND";
        case ItemSlot::Hands:
            return "ON THE HANDS";
    }
    return "CARRIED";
}

std::string_view itemSlotShortWord(ItemSlot slot) noexcept {
    switch (slot) {
        case ItemSlot::None:
            return "CARRIED";
        case ItemSlot::Hand:
            return "HAND";
        case ItemSlot::Body:
            return "BODY";
        case ItemSlot::Head:
            return "HEAD";
        case ItemSlot::Feet:
            return "FEET";
        case ItemSlot::Trinket:
            return "TRINKET";
        case ItemSlot::OffHand:
            return "OFF-HAND";
        case ItemSlot::Hands:
            return "HANDS";
    }
    return "CARRIED";
}

// ---------------------------------------------------------------------------
// the grip
// ---------------------------------------------------------------------------

std::string_view weaponGripSymbol(WeaponGrip grip) noexcept {
    switch (grip) {
        case WeaponGrip::OneHanded:
            return "one";
        case WeaponGrip::TwoHanded:
            return "two";
        case WeaponGrip::Shield:
            return "shield";
    }
    return "one";
}

bool weaponGripFromSymbol(std::string_view symbol, WeaponGrip& out) noexcept {
    for (const WeaponGrip grip : {WeaponGrip::OneHanded, WeaponGrip::TwoHanded, WeaponGrip::Shield}) {
        if (weaponGripSymbol(grip) == symbol) {
            out = grip;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// the record
// ---------------------------------------------------------------------------

std::string itemSheetLine(const ItemDef& item) {
    if (item.slot != ItemSlot::Hand) {
        return item.name;
    }
    std::string line = item.name;
    line += ' ';
    line += std::to_string(item.damageLo);
    line += '-';
    line += std::to_string(item.damageHi);
    line += item.weaponClass == Weapon::Edged ? " EDGE" : " IMPACT";
    return line;
}

ItemCategory itemCategory(const ItemDef& item) noexcept {
    if (!item.contraband.empty()) {
        return ItemCategory::Goods;
    }
    if (item.slot == ItemSlot::Hand) {
        return ItemCategory::Weapon;
    }
    if (item.slot == ItemSlot::Body || item.slot == ItemSlot::Head ||
        item.slot == ItemSlot::Feet || item.slot == ItemSlot::Hands ||
        item.slot == ItemSlot::OffHand) {
        return ItemCategory::Armour;
    }
    if (!item.effects.empty()) {
        return ItemCategory::Consumable;
    }
    return ItemCategory::Gear;
}

std::string_view itemCategoryName(ItemCategory category) noexcept {
    switch (category) {
        case ItemCategory::Weapon:
            return "WEAPONS";
        case ItemCategory::Armour:
            return "ARMOUR";
        case ItemCategory::Consumable:
            return "CONSUMABLES";
        case ItemCategory::Gear:
            return "GEAR";
        case ItemCategory::Goods:
            return "GOODS";
    }
    return "GEAR";
}

std::string itemEffectLine(const ItemDef& item) {
    std::string line;
    for (const ItemEffect& effect : item.effects) {
        if (!line.empty()) {
            line += ", ";
        }
        if (effect.kind == ItemEffectKind::Vitality) {
            line += effect.magnitude > 0 ? "MENDS " : "COSTS ";
            line += std::to_string(effect.magnitude > 0 ? effect.magnitude : -effect.magnitude);
        } else {
            line += std::string(attributeName(effect.attribute));
            for (char& c : line) {
                if (c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
            }
            line += effect.magnitude > 0 ? " +" : " -";
            line += std::to_string(effect.magnitude > 0 ? effect.magnitude : -effect.magnitude);
            line += " FOR " + std::to_string(effect.seconds) + " S";
        }
    }
    return line;
}

ItemInspect inspectItem(const ItemDef& item, std::int32_t count, bool worn,
                        std::int32_t stolen) {
    ItemInspect out;
    out.name = item.name;
    out.desc = item.desc;
    out.category = itemCategory(item);
    out.count = std::max(0, count);
    out.unitDrams = item.drams;
    out.unitRoyals = item.royals;
    if (item.slot == ItemSlot::Hand) {
        out.damageLo = item.damageLo;
        out.damageHi = item.damageHi;
        out.damageWord = item.weaponClass == Weapon::Edged ? "EDGE" : "IMPACT";
    }
    if (item.slot == ItemSlot::Hand || item.slot == ItemSlot::OffHand) {
        out.grip = item.grip;
    }
    out.dr = item.dr;
    out.heat = item.heat;
    out.worn = worn;
    out.stolen = std::clamp(stolen, 0, out.count);
    out.useVerb = item.useVerb;
    out.useLine = itemEffectLine(item);
    out.fixed = item.fixed;
    return out;
}

std::filesystem::path itemRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "items" / "items.json";
}

ItemRegistry ItemRegistry::load(const std::filesystem::path& contentDir) {
    const auto slurp = [](const std::filesystem::path& path, std::string& into) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        into = text.str();
        return true;
    };
    std::string text;
    if (!slurp(itemRawsPath(contentDir), text)) {
        return ItemRegistry{};
    }
    ItemRegistry out = fromJson(text);
    // THE EXTRA FILES, after the first: a missing one is simply not there (a
    // build with the old raws boots as it always did).
    for (const std::string_view name : kExtraItemFiles) {
        if (slurp(itemRawsPath(contentDir).parent_path() / std::string(name), text)) {
            out.appendJson(text);
        }
    }
    return out;
}

ItemRegistry ItemRegistry::fromJson(std::string_view json) {
    ItemRegistry out;
    out.appendRows(json, true);
    out.baseRows_ = out.items_.size();
    return out;
}

void ItemRegistry::appendJson(std::string_view json) {
    appendRows(json, false);
}

void ItemRegistry::appendRows(std::string_view json, bool firstFile) {
    ItemRegistry& out = *this;
    // Never throws out of here: the game must still boot when the raws are
    // missing or half-typed. The spellbook's rule.
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return;
    }
    const auto items = document.find("items");
    if (items == document.end() || !items->is_array()) {
        return;
    }
    for (const nlohmann::json& node : *items) {
        if (!node.is_object()) {
            continue;
        }
        ItemDef item;
        item.id = stringOr(node, "id", "");
        if (item.id.empty() || item.id.size() > kMaxIdBytes) {
            out.warnings_.push_back("item without a usable id");
            continue;
        }
        if (out.find(item.id) != nullptr) {
            out.warnings_.push_back("duplicate item id: " + item.id);
            continue;
        }
        item.name = stringOr(node, "name", item.id.c_str());
        item.drams = intOr(node, "drams", 0);
        item.royals = intOr(node, "royals", 0);
        const std::string slot = stringOr(node, "slot", "none");
        if (!itemSlotFromSymbol(slot, item.slot)) {
            out.warnings_.push_back("unknown slot on " + item.id + ": " + slot);
            continue;
        }
        const std::string cls = stringOr(node, "class", "fists");
        if (!weaponFromSymbol(cls, item.weaponClass)) {
            out.warnings_.push_back("unknown class on " + item.id + ": " + cls);
            continue;
        }
        if (item.slot == ItemSlot::Hand) {
            // THE FIRST FILE'S ROWS promise their CLASS's span -- the Kit
            // build ruled it, and every one of them rolls exactly it -- so an
            // authored span there is checked against the class and a row
            // that disagrees is refused rather than printed as a lie. An
            // EXTRA file's row (items_gear.json) rolls its OWN span
            // (strikeWithSpan): it is only checked for sense -- a blow worth
            // at least one point, low before high, no wider than
            // kStrikeSpanWidthMax.
            const std::int32_t lo = baseDamage(item.weaponClass);
            const std::int32_t hi = lo + kStrikeVarianceMax;
            item.damageLo = lo;
            item.damageHi = hi;
            const auto span = node.find("damage");
            if (span != node.end()) {
                const bool shaped = span->is_array() && span->size() == 2 &&
                                    (*span)[0].is_number_integer() &&
                                    (*span)[1].is_number_integer();
                const std::int32_t authoredLo = shaped ? (*span)[0].get<std::int32_t>() : 0;
                const std::int32_t authoredHi = shaped ? (*span)[1].get<std::int32_t>() : 0;
                if (firstFile) {
                    if (!shaped || authoredLo != lo || authoredHi != hi) {
                        out.warnings_.push_back("damage span on " + item.id +
                                                " disagrees with its class");
                        continue;
                    }
                } else {
                    if (!shaped || authoredLo < 1 || authoredHi < authoredLo ||
                        authoredHi - authoredLo > kStrikeSpanWidthMax) {
                        out.warnings_.push_back("damage span on " + item.id +
                                                " is not [low, high] with 1 <= low <= high <= low + " +
                                                std::to_string(kStrikeSpanWidthMax));
                        continue;
                    }
                    item.damageLo = authoredLo;
                    item.damageHi = authoredHi;
                }
            }
        }
        // THE GRIP. A hand row is one-handed unless it says "two"; an
        // off-hand row is a shield (and may say so, never anything else);
        // any other row has no grip, and one that names one is refused --
        // a grip on a coat would be a column nothing reads, printed as if
        // something did.
        {
            const auto gripNode = node.find("grip");
            const bool authored = gripNode != node.end();
            WeaponGrip grip = item.slot == ItemSlot::OffHand ? WeaponGrip::Shield
                                                             : WeaponGrip::OneHanded;
            if (authored && (!gripNode->is_string() ||
                             !weaponGripFromSymbol(gripNode->get<std::string>(), grip))) {
                out.warnings_.push_back("unknown grip on " + item.id);
                continue;
            }
            const bool fits =
                (item.slot == ItemSlot::Hand && grip != WeaponGrip::Shield) ||
                (item.slot == ItemSlot::OffHand && grip == WeaponGrip::Shield) ||
                (item.slot != ItemSlot::Hand && item.slot != ItemSlot::OffHand && !authored);
            if (!fits) {
                out.warnings_.push_back("grip " + std::string(weaponGripSymbol(grip)) + " on " +
                                        item.id + " does not fit its slot");
                continue;
            }
            item.grip = grip;
            if (item.slot == ItemSlot::Hand) {
                const GripTiming timing = gripTiming(grip);
                item.reachQ8 = timing.reachQ8;
                item.recoverySteps = timing.recoverySteps;
                item.hardRecoverySteps = timing.hardRecoverySteps;
            }
        }
        item.model = stringOr(node, "model", "");
        item.dr = intOr(node, "dr", 0);
        item.heat = intOr(node, "heat", 0);
        item.contraband = stringOr(node, "contraband", "");
        item.desc = stringOr(node, "desc", "");
        item.fixed = boolOr(node, "fixed", false);
        // WHAT A USE DOES. `"use": {"verb": "EAT", "effects": [...]}`, each
        // effect `{"effect": "vitality", "magnitude": 8}` or `{"effect":
        // "attribute", "param": "MGT", "magnitude": 1, "seconds": 300}` --
        // the spells' own raw tokens. A use that names an effect the sim has
        // no reader for, or an attribute it does not know, refuses the whole
        // row rather than printing a use that does nothing.
        bool badUse = false;
        if (const auto use = node.find("use"); use != node.end()) {
            if (!use->is_object()) {
                badUse = true;
            } else {
                item.useVerb = stringOr(*use, "verb", "USE");
                item.useDone = stringOr(*use, "done", "USED");
                const auto effects = use->find("effects");
                if (effects == use->end() || !effects->is_array() || effects->empty()) {
                    badUse = true;
                } else {
                    for (const nlohmann::json& entry : *effects) {
                        ItemEffect effect;
                        const std::string kind = entry.is_object() ? stringOr(entry, "effect", "") : "";
                        effect.magnitude = entry.is_object() ? intOr(entry, "magnitude", 0) : 0;
                        if (kind == "vitality") {
                            effect.kind = ItemEffectKind::Vitality;
                        } else if (kind == "attribute") {
                            effect.kind = ItemEffectKind::Attribute;
                            const std::optional<AttributeId> attribute =
                                attributeFromRaw(stringOr(entry, "param", ""));
                            effect.seconds = intOr(entry, "seconds", 0);
                            if (!attribute.has_value() || effect.seconds <= 0) {
                                badUse = true;
                                break;
                            }
                            effect.attribute = *attribute;
                        } else {
                            badUse = true;
                            break;
                        }
                        if (effect.magnitude == 0) {
                            badUse = true;
                            break;
                        }
                        item.effects.push_back(effect);
                    }
                }
            }
        }
        if (badUse) {
            out.warnings_.push_back("unusable use on " + item.id);
            continue;
        }
        // The span it is drawn over, for the crosshair: two whole
        // centimetres, low before high, or the default stands.
        const auto drawn = node.find("drawnCm");
        if (drawn != node.end()) {
            if (!drawn->is_array() || drawn->size() != 2 || !(*drawn)[0].is_number_integer() ||
                !(*drawn)[1].is_number_integer() ||
                (*drawn)[0].get<std::int32_t>() >= (*drawn)[1].get<std::int32_t>()) {
                out.warnings_.push_back("drawnCm on " + item.id + " is not [low, high] in centimetres");
            } else {
                item.drawnLoCm = (*drawn)[0].get<std::int32_t>();
                item.drawnHiCm = (*drawn)[1].get<std::int32_t>();
            }
        }
        if (item.drams < 0 || item.royals < 0 || item.dr < 0 || item.heat < 0) {
            out.warnings_.push_back("negative number on " + item.id);
            continue;
        }
        out.items_.push_back(std::move(item));
    }

    const auto stands = document.find("stands");
    if (stands != document.end() && stands->is_array()) {
        for (const nlohmann::json& node : *stands) {
            if (!node.is_object()) {
                continue;
            }
            ItemStand stand;
            stand.item = stringOr(node, "item", "");
            if (out.find(stand.item) == nullptr) {
                out.warnings_.push_back("stand for an unknown item: " + stand.item);
                continue;
            }
            stand.x = intOr(node, "x", 0);
            stand.y = intOr(node, "y", 0);
            stand.band = intOr(node, "band", 0);
            stand.count = std::max(1, intOr(node, "count", 1));
            stand.owned = boolOr(node, "owned", false);
            out.stands_.push_back(std::move(stand));
        }
    }

    const auto carried = document.find("carried");
    if (carried != document.end() && carried->is_object()) {
        for (const auto& [key, value] : carried->items()) {
            if (!value.is_array() || key.empty()) {
                continue;
            }
            CorpseKit kit;
            kit.key = key;
            for (const nlohmann::json& entry : value) {
                if (!entry.is_string()) {
                    continue;
                }
                const std::string id = entry.get<std::string>();
                if (out.find(id) == nullptr) {
                    out.warnings_.push_back("corpse kit " + key + " names an unknown item: " + id);
                    continue;
                }
                // A mask of 32 bits is the whole of what a corpse can carry.
                if (kit.items.size() < 32) {
                    kit.items.push_back(id);
                }
            }
            out.carried_.push_back(std::move(kit));
        }
        // nlohmann's object iteration is ITS insertion order for an
        // ordered_json and sorted otherwise; sorted by key here so the
        // table is the same on every machine whatever the parser did.
        std::sort(out.carried_.begin(), out.carried_.end(),
                  [](const CorpseKit& a, const CorpseKit& b) { return a.key < b.key; });
    }
}

const ItemDef* ItemRegistry::find(std::string_view id) const noexcept {
    for (const ItemDef& item : items_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

std::int32_t ItemRegistry::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

const ItemDef* ItemRegistry::at(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) {
        return nullptr;
    }
    return &items_[static_cast<std::size_t>(index)];
}

const CorpseKit* ItemRegistry::carriedBy(std::string_view key) const noexcept {
    for (const CorpseKit& kit : carried_) {
        if (kit.key == key) {
            return &kit;
        }
    }
    return nullptr;
}

bool ItemRegistry::slotHasItems(ItemSlot slot) const noexcept {
    for (const ItemDef& item : items_) {
        if (item.slot == slot) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// what is on the body
// ---------------------------------------------------------------------------

// --- the loadout ------------------------------------------------------------

std::int32_t Loadout::worn(ItemSlot slot) const noexcept {
    const std::int32_t at = wornSlotIndex(slot);
    if (at < 0 || static_cast<std::size_t>(at) >= kWornSlotCount) {
        return -1;
    }
    return worn_[static_cast<std::size_t>(at)];
}

bool Loadout::isWorn(std::int32_t index) const noexcept {
    if (index < 0) {
        return false;
    }
    for (const std::int32_t wornIndex : worn_) {
        if (wornIndex == index) {
            return true;
        }
    }
    return false;
}

void Loadout::set(ItemSlot slot, std::int32_t index) noexcept {
    const std::int32_t at = wornSlotIndex(slot);
    if (at < 0 || static_cast<std::size_t>(at) >= kWornSlotCount) {
        return;
    }
    worn_[static_cast<std::size_t>(at)] = index < 0 ? -1 : index;
}

void Loadout::bare(ItemSlot slot) noexcept {
    set(slot, -1);
}

void Loadout::clear() noexcept {
    for (std::int32_t& index : worn_) {
        index = -1;
    }
}

std::vector<std::uint8_t> Loadout::encode(const ItemRegistry& registry) const {
    std::vector<std::uint8_t> out;
    out.push_back(kLoadoutMagic0);
    out.push_back(kLoadoutMagic1);
    out.push_back(kLoadoutVersion);
    out.push_back(static_cast<std::uint8_t>(kWornSlotCount));
    for (const std::int32_t index : worn_) {
        const ItemDef* item = registry.at(index);
        putId(out, item != nullptr ? std::string_view(item->id) : std::string_view());
    }
    return out;
}

bool Loadout::decode(const std::vector<std::uint8_t>& bytes, const ItemRegistry& registry,
                     Loadout& out) {
    if (bytes.size() < 4 || bytes[0] != kLoadoutMagic0 || bytes[1] != kLoadoutMagic1 ||
        bytes[2] != kLoadoutVersion || bytes[3] != static_cast<std::uint8_t>(kWornSlotCount)) {
        return false;
    }
    Loadout parsed;
    std::size_t cursor = 4;
    for (std::size_t slot = 0; slot < kWornSlotCount; ++slot) {
        std::string id;
        if (!takeId(bytes, cursor, id)) {
            return false;
        }
        if (id.empty()) {
            continue;
        }
        const std::int32_t index = registry.indexOf(id);
        const ItemDef* item = registry.at(index);
        if (item == nullptr || item->fixed ||
            wornSlotIndex(item->slot) != static_cast<std::int32_t>(slot)) {
            return false;
        }
        parsed.worn_[slot] = index;
    }
    if (cursor != bytes.size()) {
        return false;
    }
    // A two-handed row in the hand with something in the other hand is a
    // state wear() can never make: the image is lying, so none of it is read.
    if (parsed.worn(ItemSlot::OffHand) >= 0 &&
        handGripOf(parsed, registry) == WeaponGrip::TwoHanded) {
        return false;
    }
    out = parsed;
    return true;
}

void Loadout::hashInto(HashSink& sink) const {
    // The five-slot Kit's own words, in its own order: nothing that existed
    // before the off hand and the hands moves.
    for (std::size_t i = 0; i < kLegacyWornSlotCount; ++i) {
        sink.put_int(static_cast<std::uint32_t>(worn_[i]));
    }
    const std::int32_t offHand = worn(ItemSlot::OffHand);
    const std::int32_t hands = worn(ItemSlot::Hands);
    if (offHand >= 0 || hands >= 0) {
        sink.put_int(kLoadoutTailMarker);
        sink.put_int(static_cast<std::uint32_t>(offHand));
        sink.put_int(static_cast<std::uint32_t>(hands));
    }
}

// --- the kit ----------------------------------------------------------------

void Kit::resize(std::size_t rows) {
    counts_.resize(rows, 0);
    stolen_.resize(rows, 0);
    for (std::size_t i = 0; i < kWornSlotCount; ++i) {
        const ItemSlot slot = wornSlotAt(static_cast<std::int32_t>(i));
        const std::int32_t index = loadout_.worn(slot);
        if (index >= 0 && static_cast<std::size_t>(index) >= rows) {
            loadout_.bare(slot);
        }
    }
}

std::int32_t Kit::count(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= counts_.size()) {
        return 0;
    }
    return counts_[static_cast<std::size_t>(index)];
}

std::int32_t Kit::units() const noexcept {
    std::int32_t total = 0;
    for (const std::int32_t n : counts_) {
        total += n;
    }
    return total;
}

std::int32_t Kit::weight(const ItemRegistry& registry) const noexcept {
    std::int32_t drams = 0;
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] <= 0) {
            continue;
        }
        const ItemDef* item = registry.at(static_cast<std::int32_t>(i));
        if (item != nullptr) {
            drams += counts_[i] * item->drams;
        }
    }
    return drams;
}

std::int32_t Kit::heatIfSearched(const ItemRegistry& registry) const noexcept {
    std::int32_t heat = 0;
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] <= 0) {
            continue;
        }
        const ItemDef* item = registry.at(static_cast<std::int32_t>(i));
        if (item != nullptr) {
            heat += counts_[i] * item->heat;
        }
    }
    return heat;
}

std::int32_t Kit::add(std::int32_t index, std::int32_t units) {
    if (index < 0 || static_cast<std::size_t>(index) >= counts_.size() || units <= 0) {
        return 0;
    }
    counts_[static_cast<std::size_t>(index)] += units;
    return units;
}

std::int32_t Kit::take(std::int32_t index, std::int32_t units) {
    if (index < 0 || static_cast<std::size_t>(index) >= counts_.size() || units <= 0) {
        return 0;
    }
    std::int32_t& held = counts_[static_cast<std::size_t>(index)];
    const std::int32_t took = std::min(units, held);
    held -= took;
    // What is left keeps as much of its THEIRS as there is left of it: the
    // units that are nobody's are the ones that leave first.
    std::int32_t& lifted = stolen_[static_cast<std::size_t>(index)];
    lifted = std::min(lifted, held);
    if (held <= 0) {
        for (std::size_t i = 0; i < kWornSlotCount; ++i) {
            const ItemSlot slot = wornSlotAt(static_cast<std::int32_t>(i));
            if (loadout_.worn(slot) == index) {
                loadout_.bare(slot);
            }
        }
    }
    return took;
}

std::int32_t Kit::stolen(std::int32_t index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= stolen_.size()) {
        return 0;
    }
    return stolen_[static_cast<std::size_t>(index)];
}

void Kit::markStolen(std::int32_t index, std::int32_t units) {
    if (index < 0 || static_cast<std::size_t>(index) >= counts_.size() || units <= 0) {
        return;
    }
    std::int32_t& lifted = stolen_[static_cast<std::size_t>(index)];
    lifted = std::min(counts_[static_cast<std::size_t>(index)], lifted + units);
}

void Kit::clear() noexcept {
    for (std::int32_t& n : counts_) {
        n = 0;
    }
    for (std::int32_t& n : stolen_) {
        n = 0;
    }
    loadout_.clear();
}

std::int32_t Kit::worn(ItemSlot slot) const noexcept {
    return loadout_.worn(slot);
}

bool Kit::isWorn(std::int32_t index) const noexcept {
    return loadout_.isWorn(index);
}

bool Kit::wear(const ItemRegistry& registry, std::int32_t index) {
    if (count(index) <= 0) {
        return false;
    }
    return wearInto(loadout_, registry, index);
}

std::vector<ItemSlot> Kit::wouldBare(const ItemRegistry& registry, std::int32_t index) const {
    std::vector<ItemSlot> out;
    const ItemDef* item = registry.at(index);
    if (item == nullptr || item->slot == ItemSlot::None || item->fixed) {
        return out;
    }
    const std::int32_t occupant = loadout_.worn(item->slot);
    if (occupant == index) {
        // Already on: the press would take nothing off (and the loadout it
        // sits in already satisfies the two-hand rule).
        return out;
    }
    if (occupant >= 0) {
        out.push_back(item->slot);
    }
    if (item->slot == ItemSlot::Hand && item->grip == WeaponGrip::TwoHanded &&
        loadout_.worn(ItemSlot::OffHand) >= 0) {
        out.push_back(ItemSlot::OffHand);
    }
    if (item->slot == ItemSlot::OffHand &&
        handGripOf(loadout_, registry) == WeaponGrip::TwoHanded) {
        out.push_back(ItemSlot::Hand);
    }
    return out;
}

void Kit::bare(ItemSlot slot) noexcept {
    loadout_.bare(slot);
}

std::int32_t Kit::wornDr(const ItemRegistry& registry) const noexcept {
    return drOf(loadout_, registry);
}

Weapon Kit::handClass(const ItemRegistry& registry) const noexcept {
    const ItemDef* item = registry.at(worn(ItemSlot::Hand));
    return item != nullptr ? item->weaponClass : Weapon::Fists;
}

std::int32_t Kit::handDamageLo(const ItemRegistry& registry) const noexcept {
    std::int32_t lo = 0;
    std::int32_t hi = 0;
    handSpanOf(loadout_, registry, lo, hi);
    return lo;
}

std::int32_t Kit::handDamageHi(const ItemRegistry& registry) const noexcept {
    std::int32_t lo = 0;
    std::int32_t hi = 0;
    handSpanOf(loadout_, registry, lo, hi);
    return hi;
}

WeaponGrip Kit::handGrip(const ItemRegistry& registry) const noexcept {
    return handGripOf(loadout_, registry);
}

std::vector<std::uint8_t> Kit::encode(const ItemRegistry& registry) const {
    std::vector<std::uint8_t> out;
    out.push_back(kKitMagic0);
    out.push_back(kKitMagic1);
    out.push_back(kKitVersion);
    // Only the rows that hold something, each by its id: a raws row added
    // after the save was written does not move anything in it.
    std::int32_t held = 0;
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] > 0 && registry.at(static_cast<std::int32_t>(i)) != nullptr) {
            ++held;
        }
    }
    putI32(out, held);
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        const ItemDef* item = registry.at(static_cast<std::int32_t>(i));
        if (counts_[i] <= 0 || item == nullptr) {
            continue;
        }
        putId(out, item->id);
        putI32(out, counts_[i]);
    }
    // Version 3: the worn rows are the Loadout's own image, length-prefixed,
    // so the gear state's codec is its own and the Kit only carries it.
    const std::vector<std::uint8_t> wornImage = loadout_.encode(registry);
    putI32(out, static_cast<std::int32_t>(wornImage.size()));
    out.insert(out.end(), wornImage.begin(), wornImage.end());
    // Version 2: the stolen counts, only for rows that carry some, by id.
    std::int32_t lifted = 0;
    for (std::size_t i = 0; i < stolen_.size(); ++i) {
        if (stolen_[i] > 0 && counts_[i] > 0 && registry.at(static_cast<std::int32_t>(i)) != nullptr) {
            ++lifted;
        }
    }
    putI32(out, lifted);
    for (std::size_t i = 0; i < stolen_.size(); ++i) {
        const ItemDef* item = registry.at(static_cast<std::int32_t>(i));
        if (stolen_[i] <= 0 || counts_[i] <= 0 || item == nullptr) {
            continue;
        }
        putId(out, item->id);
        putI32(out, stolen_[i]);
    }
    return out;
}

bool Kit::decode(const std::vector<std::uint8_t>& bytes, const ItemRegistry& registry,
                 Kit& out) {
    if (bytes.size() < 3 || bytes[0] != kKitMagic0 || bytes[1] != kKitMagic1 ||
        (bytes[2] != kKitVersion && bytes[2] != kKitVersionFiveSlots &&
         bytes[2] != kKitVersionNoStolen)) {
        return false;
    }
    const bool hasStolen = bytes[2] != kKitVersionNoStolen;
    const bool hasLoadout = bytes[2] == kKitVersion;
    Kit parsed(registry.size());
    std::size_t cursor = 3;
    std::int32_t held = 0;
    if (!takeI32(bytes, cursor, held) || held < 0) {
        return false;
    }
    for (std::int32_t i = 0; i < held; ++i) {
        std::string id;
        std::int32_t count = 0;
        if (!takeId(bytes, cursor, id) || !takeI32(bytes, cursor, count) || count < 0) {
            return false;
        }
        const std::int32_t index = registry.indexOf(id);
        if (index < 0) {
            return false;
        }
        parsed.counts_[static_cast<std::size_t>(index)] = count;
    }
    if (hasLoadout) {
        // VERSION 3: the Loadout's own image, by its own codec; the Kit adds
        // the one rule the loadout cannot know -- a worn row is carried.
        std::int32_t length = 0;
        if (!takeI32(bytes, cursor, length) || length < 0 ||
            static_cast<std::size_t>(length) > bytes.size() - cursor) {
            return false;
        }
        const auto from = bytes.begin() + static_cast<std::ptrdiff_t>(cursor);
        const std::vector<std::uint8_t> image(from, from + static_cast<std::ptrdiff_t>(length));
        cursor += static_cast<std::size_t>(length);
        if (!Loadout::decode(image, registry, parsed.loadout_)) {
            return false;
        }
        for (std::size_t i = 0; i < kWornSlotCount; ++i) {
            const std::int32_t index = parsed.loadout_.worn(wornSlotAt(static_cast<std::int32_t>(i)));
            if (index >= 0 && parsed.counts_[static_cast<std::size_t>(index)] <= 0) {
                return false;
            }
        }
    } else {
        // VERSIONS 1 AND 2: the five-slot block (Hand..Trinket), exactly as
        // the Kit wrote it before phase 9. The other hand and the hands are
        // bare in any image that old, because no such slot existed.
        if (cursor >= bytes.size() ||
            bytes[cursor] != static_cast<std::uint8_t>(kLegacyWornSlotCount)) {
            return false;
        }
        ++cursor;
        for (std::size_t slot = 0; slot < kLegacyWornSlotCount; ++slot) {
            std::string id;
            if (!takeId(bytes, cursor, id)) {
                return false;
            }
            if (id.empty()) {
                continue;
            }
            const std::int32_t index = registry.indexOf(id);
            if (index < 0 || parsed.counts_[static_cast<std::size_t>(index)] <= 0) {
                return false;
            }
            const ItemDef* item = registry.at(index);
            if (item == nullptr || wornSlotIndex(item->slot) != static_cast<std::int32_t>(slot)) {
                return false;
            }
            parsed.loadout_.set(item->slot, index);
        }
    }
    if (hasStolen) {
        std::int32_t lifted = 0;
        if (!takeI32(bytes, cursor, lifted) || lifted < 0) {
            return false;
        }
        for (std::int32_t i = 0; i < lifted; ++i) {
            std::string id;
            std::int32_t units = 0;
            if (!takeId(bytes, cursor, id) || !takeI32(bytes, cursor, units) || units <= 0) {
                return false;
            }
            const std::int32_t index = registry.indexOf(id);
            // A stolen count past what is carried is a lie the image cannot
            // tell: refuse the whole of it, like an id the raws lost.
            if (index < 0 || units > parsed.counts_[static_cast<std::size_t>(index)]) {
                return false;
            }
            parsed.stolen_[static_cast<std::size_t>(index)] = units;
        }
    }
    if (cursor != bytes.size()) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

void Kit::hashInto(HashSink& sink, std::size_t legacyRows) const {
    const std::size_t head = std::min(legacyRows, counts_.size());
    sink.put_int(static_cast<std::uint32_t>(head));
    for (std::size_t i = 0; i < head; ++i) {
        sink.put_int(static_cast<std::uint32_t>(counts_[i]));
    }
    // The worn rows: the five the Kit always wrote, in its order, and the two
    // phase 9 appended only when one of them holds something.
    loadout_.hashInto(sink);
    // THE TAIL: rows an extra raws file added, and stolen units, written only
    // when there is something to write -- see the header on why.
    bool tail = false;
    for (std::size_t i = head; i < counts_.size() && !tail; ++i) {
        tail = counts_[i] != 0;
    }
    if (tail) {
        sink.put_int(kKitTailMarker);
        for (std::size_t i = head; i < counts_.size(); ++i) {
            if (counts_[i] != 0) {
                sink.put_int(static_cast<std::uint32_t>(i));
                sink.put_int(static_cast<std::uint32_t>(counts_[i]));
            }
        }
    }
    bool lifted = false;
    for (std::size_t i = 0; i < stolen_.size() && !lifted; ++i) {
        lifted = stolen_[i] != 0;
    }
    if (lifted) {
        sink.put_int(kKitStolenMarker);
        for (std::size_t i = 0; i < stolen_.size(); ++i) {
            if (stolen_[i] != 0) {
                sink.put_int(static_cast<std::uint32_t>(i));
                sink.put_int(static_cast<std::uint32_t>(stolen_[i]));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// the comparison
// ---------------------------------------------------------------------------

GearCompare compareGear(const Kit& kit, const ItemRegistry& registry, std::int32_t index) {
    GearCompare out;
    const ItemDef* item = registry.at(index);
    if (item == nullptr || item->slot == ItemSlot::None || item->fixed ||
        kit.worn(item->slot) == index) {
        return out;
    }
    // THEN is the wear rules run on a copy of what is worn -- the same
    // function Kit::wear runs -- so the line and the press cannot disagree.
    const Loadout& now = kit.loadout();
    Loadout then = now;
    if (!wearInto(then, registry, index)) {
        return out;
    }
    out.applies = true;
    out.drNow = drOf(now, registry);
    out.drThen = drOf(then, registry);
    out.showsDamage = item->slot == ItemSlot::Hand || then.worn(ItemSlot::Hand) != now.worn(ItemSlot::Hand);
    handSpanOf(now, registry, out.dmgLoNow, out.dmgHiNow);
    handSpanOf(then, registry, out.dmgLoThen, out.dmgHiThen);
    const GripTiming timingNow = gripTiming(handGripOf(now, registry));
    const GripTiming timingThen = gripTiming(handGripOf(then, registry));
    out.reachNowQ8 = timingNow.reachQ8;
    out.reachThenQ8 = timingThen.reachQ8;
    out.recoveryNow = timingNow.recoverySteps;
    out.recoveryThen = timingThen.recoverySteps;
    out.bares = kit.wouldBare(registry, index);
    return out;
}

// ---------------------------------------------------------------------------
// what is on the ground
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> encodeGround(const std::vector<GroundItem>& ground,
                                       const ItemRegistry& registry) {
    std::vector<std::uint8_t> out;
    out.push_back(kGroundMagic0);
    out.push_back(kGroundMagic1);
    out.push_back(kGroundVersion);
    std::int32_t kept = 0;
    for (const GroundItem& entry : ground) {
        if (registry.at(entry.item) != nullptr) {
            ++kept;
        }
    }
    putI32(out, kept);
    for (const GroundItem& entry : ground) {
        const ItemDef* item = registry.at(entry.item);
        if (item == nullptr) {
            continue;
        }
        putId(out, item->id);
        putI32(out, entry.x);
        putI32(out, entry.y);
        putI32(out, entry.band);
        putI32(out, entry.count);
        out.push_back(entry.owned ? 1U : 0U);
    }
    return out;
}

bool decodeGround(const std::vector<std::uint8_t>& bytes, const ItemRegistry& registry,
                  std::vector<GroundItem>& out) {
    if (bytes.size() < 3 || bytes[0] != kGroundMagic0 || bytes[1] != kGroundMagic1 ||
        bytes[2] != kGroundVersion) {
        return false;
    }
    std::size_t cursor = 3;
    std::int32_t kept = 0;
    if (!takeI32(bytes, cursor, kept) || kept < 0) {
        return false;
    }
    std::vector<GroundItem> parsed;
    parsed.reserve(static_cast<std::size_t>(kept));
    for (std::int32_t i = 0; i < kept; ++i) {
        std::string id;
        GroundItem entry;
        if (!takeId(bytes, cursor, id) || !takeI32(bytes, cursor, entry.x) ||
            !takeI32(bytes, cursor, entry.y) || !takeI32(bytes, cursor, entry.band) ||
            !takeI32(bytes, cursor, entry.count) || cursor >= bytes.size()) {
            return false;
        }
        entry.owned = bytes[cursor] != 0;
        ++cursor;
        entry.item = registry.indexOf(id);
        if (entry.item < 0 || entry.count <= 0) {
            return false;
        }
        parsed.push_back(entry);
    }
    if (cursor != bytes.size()) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

}  // namespace granadad::sim
