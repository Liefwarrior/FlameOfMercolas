#include "granadad/sim/items.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

constexpr std::uint8_t kKitMagic0 = 'G';
constexpr std::uint8_t kKitMagic1 = 'K';
constexpr std::uint8_t kKitVersion = 1;
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
    }
    return "CARRIED";
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

std::filesystem::path itemRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "items" / "items.json";
}

ItemRegistry ItemRegistry::load(const std::filesystem::path& contentDir) {
    std::ifstream file(itemRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return ItemRegistry{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return fromJson(text.str());
}

ItemRegistry ItemRegistry::fromJson(std::string_view json) {
    ItemRegistry out;
    // Never throws out of here: the game must still boot when the raws are
    // missing or half-typed. The spellbook's rule.
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto items = document.find("items");
    if (items == document.end() || !items->is_array()) {
        return out;
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
            // The span is the CLASS's -- strike() rolls the class and nothing
            // else -- so an authored span is checked against it and a row
            // that disagrees is refused rather than printed as a lie.
            const std::int32_t lo = baseDamage(item.weaponClass);
            const std::int32_t hi = lo + kStrikeVarianceMax;
            item.damageLo = lo;
            item.damageHi = hi;
            const auto span = node.find("damage");
            if (span != node.end()) {
                if (!span->is_array() || span->size() != 2 || !(*span)[0].is_number_integer() ||
                    !(*span)[1].is_number_integer() || (*span)[0].get<std::int32_t>() != lo ||
                    (*span)[1].get<std::int32_t>() != hi) {
                    out.warnings_.push_back("damage span on " + item.id +
                                            " disagrees with its class");
                    continue;
                }
            }
        }
        item.dr = intOr(node, "dr", 0);
        item.heat = intOr(node, "heat", 0);
        item.contraband = stringOr(node, "contraband", "");
        item.desc = stringOr(node, "desc", "");
        item.fixed = boolOr(node, "fixed", false);
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
    return out;
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

void Kit::resize(std::size_t rows) {
    counts_.resize(rows, 0);
    for (std::int32_t& index : worn_) {
        if (index >= 0 && static_cast<std::size_t>(index) >= rows) {
            index = -1;
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
    if (held <= 0) {
        for (std::int32_t& wornIndex : worn_) {
            if (wornIndex == index) {
                wornIndex = -1;
            }
        }
    }
    return took;
}

void Kit::clear() noexcept {
    for (std::int32_t& n : counts_) {
        n = 0;
    }
    for (std::int32_t& index : worn_) {
        index = -1;
    }
}

std::int32_t Kit::worn(ItemSlot slot) const noexcept {
    const std::int32_t at = wornSlotIndex(slot);
    if (at < 0) {
        return -1;
    }
    return worn_[static_cast<std::size_t>(at)];
}

bool Kit::isWorn(std::int32_t index) const noexcept {
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

bool Kit::wear(const ItemRegistry& registry, std::int32_t index) {
    if (count(index) <= 0) {
        return false;
    }
    const ItemDef* item = registry.at(index);
    if (item == nullptr || item->slot == ItemSlot::None || item->fixed) {
        return false;
    }
    const std::int32_t at = wornSlotIndex(item->slot);
    worn_[static_cast<std::size_t>(at)] = index;
    return true;
}

void Kit::bare(ItemSlot slot) noexcept {
    const std::int32_t at = wornSlotIndex(slot);
    if (at >= 0) {
        worn_[static_cast<std::size_t>(at)] = -1;
    }
}

std::int32_t Kit::wornDr(const ItemRegistry& registry) const noexcept {
    std::int32_t dr = 0;
    for (std::size_t i = 0; i < kWornSlotCount; ++i) {
        if (wornSlotAt(static_cast<std::int32_t>(i)) == ItemSlot::Hand) {
            continue;
        }
        const ItemDef* item = registry.at(worn_[i]);
        if (item != nullptr) {
            dr += item->dr;
        }
    }
    return dr;
}

Weapon Kit::handClass(const ItemRegistry& registry) const noexcept {
    const ItemDef* item = registry.at(worn(ItemSlot::Hand));
    return item != nullptr ? item->weaponClass : Weapon::Fists;
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
    out.push_back(static_cast<std::uint8_t>(kWornSlotCount));
    for (const std::int32_t index : worn_) {
        const ItemDef* item = registry.at(index);
        putId(out, item != nullptr ? std::string_view(item->id) : std::string_view());
    }
    return out;
}

bool Kit::decode(const std::vector<std::uint8_t>& bytes, const ItemRegistry& registry,
                 Kit& out) {
    if (bytes.size() < 3 || bytes[0] != kKitMagic0 || bytes[1] != kKitMagic1 ||
        bytes[2] != kKitVersion) {
        return false;
    }
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
    if (cursor >= bytes.size() || bytes[cursor] != static_cast<std::uint8_t>(kWornSlotCount)) {
        return false;
    }
    ++cursor;
    for (std::size_t slot = 0; slot < kWornSlotCount; ++slot) {
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
        parsed.worn_[slot] = index;
    }
    if (cursor != bytes.size()) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

void Kit::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(counts_.size()));
    for (const std::int32_t n : counts_) {
        sink.put_int(static_cast<std::uint32_t>(n));
    }
    for (const std::int32_t index : worn_) {
        sink.put_int(static_cast<std::uint32_t>(index));
    }
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
