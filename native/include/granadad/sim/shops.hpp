#pragma once

// SHOPS (2026-10-09). The ward's counters that sell a docker's things, and the
// one that buys them back.
//
// WHY THIS EXISTS. The Kit landed (DECISIONS 2026-09-12) with twenty-three
// rows, every one with an authored worth in Royals -- and nobody in the ward
// would sell one or buy one. The only purchases in the build were a mug and a
// room. The gazetteer had already sited the counters (K08 Brann's Chandlery,
// "ship stores: lamp oil, biscuit, cordage"; K28 the Slop-Chest, "sailors'
// clothing and dry-goods"; K15 Fenner's Pawn, "stolen-goods laundering chain;
// economy sink") and notables.json had already bound a keeper to each. This
// file is the shelf behind those three counters: content/raws/shops/shops.json,
// read the tolerant way every raws loader here reads.
//
// WHAT IT DELIBERATELY IS NOT. Not a second economy: a price is the item's own
// authored royals through the counter's guild term (guildPricePercent, the
// same term a mug of ale already pays), and the haggle skill does NOT move it
// -- S3's law is that skill decides exactly one thing in the conversation
// layer, and it is the price of a mug of ale. Not stock on a body: the Kit
// ruling says a ward body carries nothing, so the shelf is the COUNTER's, and
// it is unlimited in this first version, which is also why it adds no state of
// its own -- what moves is the player's Kit and purse (both hashed already)
// and the keeper's own purse (hashed with the ward).
//
// NO FLOATS. NO UNORDERED CONTAINERS: counters in authored order, shelves in
// authored order.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::sim {

/// One counter, as authored.
struct ShopCounter {
    /// The raws key: "branns".
    std::string id;
    /// The notables.json id of the body behind it: "brann".
    std::string keeper;
    /// What the ward calls the place, as a sentence would: "Brann's Chandlery".
    std::string name;
    /// items.json ids, in shelf order.
    std::vector<std::string> sells;
    /// Zero for a counter that only sells. Otherwise the share of a thing's
    /// worth the keeper pays for it, in whole percent.
    std::int32_t buysPercent = 0;
    /// items.json ids this counter will not take whatever they are worth.
    std::vector<std::string> neverBuys;

    [[nodiscard]] bool buys() const noexcept { return buysPercent > 0; }
};

class ShopRaws {
public:
    /// content/raws/shops/shops.json. NEVER THROWS: a missing or malformed file
    /// is an empty table and no counter in the ward sells anything, which is
    /// the rule every raws loader in this build keeps. A counter with no id, no
    /// keeper or nothing to sell and nothing it buys is dropped; a counter id
    /// seen before is dropped, not overwritten.
    [[nodiscard]] static ShopRaws load(const std::filesystem::path& contentDir);
    [[nodiscard]] static ShopRaws fromJson(std::string_view json);

    [[nodiscard]] bool loaded() const noexcept { return !counters_.empty(); }
    [[nodiscard]] const std::vector<ShopCounter>& counters() const noexcept { return counters_; }
    /// The counter this notable keeps, or nullptr.
    [[nodiscard]] const ShopCounter* forKeeper(std::string_view notableId) const noexcept;

    /// Drops every shelf row the predicate refuses, and every counter left
    /// with nothing to sell and nothing it buys -- how the owner of the item
    /// registry and the notables (Tavern) refuses an id neither file has.
    template <typename KeepItem, typename KeepKeeper>
    void prune(KeepItem keepItem, KeepKeeper keepKeeper) {
        std::vector<ShopCounter> kept;
        for (ShopCounter& counter : counters_) {
            if (!keepKeeper(counter.keeper)) {
                continue;
            }
            std::vector<std::string> shelf;
            for (std::string& id : counter.sells) {
                if (keepItem(id)) {
                    shelf.push_back(std::move(id));
                }
            }
            counter.sells = std::move(shelf);
            if (counter.sells.empty() && !counter.buys()) {
                continue;
            }
            kept.push_back(std::move(counter));
        }
        counters_ = std::move(kept);
    }

private:
    std::vector<ShopCounter> counters_;
};

[[nodiscard]] std::filesystem::path shopRawsPath(const std::filesystem::path& contentDir);

/// What a counter asks for one of a thing worth `royals`, through the guild
/// term `guildPercent` (guildPricePercent: negative is a member's rate,
/// positive a strong house pricing a stranger up). Never less than one.
[[nodiscard]] std::int32_t counterPrice(std::int32_t royals, std::int32_t guildPercent) noexcept;

/// What a counter that buys pays for one of a thing worth `royals`. Never
/// less than one for a thing worth anything; zero for a thing worth nothing.
[[nodiscard]] std::int32_t counterOffer(std::int32_t royals, std::int32_t buysPercent) noexcept;

}  // namespace granadad::sim
