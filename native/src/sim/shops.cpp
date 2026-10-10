#include "granadad/sim/shops.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    return found->get<std::string>();
}

[[nodiscard]] std::vector<std::string> stringArray(const nlohmann::json& node, const char* key) {
    std::vector<std::string> out;
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        return out;
    }
    for (const nlohmann::json& row : *found) {
        if (row.is_string() && !row.get<std::string>().empty()) {
            out.push_back(row.get<std::string>());
        }
    }
    return out;
}

}  // namespace

std::filesystem::path shopRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "shops" / "shops.json";
}

ShopRaws ShopRaws::load(const std::filesystem::path& contentDir) {
    std::ifstream file(shopRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return ShopRaws{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return fromJson(text.str());
}

ShopRaws ShopRaws::fromJson(std::string_view json) {
    ShopRaws out;
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto counters = document.find("counters");
    if (counters == document.end() || !counters->is_array()) {
        return out;
    }
    for (const nlohmann::json& node : *counters) {
        if (!node.is_object()) {
            continue;
        }
        ShopCounter counter;
        counter.id = stringField(node, "id");
        counter.keeper = stringField(node, "keeper");
        counter.name = stringField(node, "name");
        counter.sells = stringArray(node, "sells");
        counter.neverBuys = stringArray(node, "neverBuys");
        const auto percent = node.find("buysPercent");
        if (percent != node.end() && percent->is_number_integer()) {
            counter.buysPercent = std::clamp(percent->get<std::int32_t>(), 0, 100);
        }
        if (counter.id.empty() || counter.keeper.empty() || counter.name.empty()) {
            continue;
        }
        if (counter.sells.empty() && !counter.buys()) {
            continue;
        }
        const bool seen = std::any_of(out.counters_.begin(), out.counters_.end(),
                                      [&counter](const ShopCounter& other) {
                                          return other.id == counter.id ||
                                                 other.keeper == counter.keeper;
                                      });
        if (seen) {
            continue;
        }
        out.counters_.push_back(std::move(counter));
    }
    return out;
}

const ShopCounter* ShopRaws::forKeeper(std::string_view notableId) const noexcept {
    if (notableId.empty()) {
        return nullptr;
    }
    for (const ShopCounter& counter : counters_) {
        if (counter.keeper == notableId) {
            return &counter;
        }
    }
    return nullptr;
}

std::int32_t counterPrice(std::int32_t royals, std::int32_t guildPercent) noexcept {
    // Integer, truncating toward zero, floored at one: a counter never gives a
    // thing away, however much the house thinks of you.
    const std::int64_t scaled =
        static_cast<std::int64_t>(royals) * (100 + static_cast<std::int64_t>(guildPercent)) / 100;
    return static_cast<std::int32_t>(std::max<std::int64_t>(1, scaled));
}

std::int32_t counterOffer(std::int32_t royals, std::int32_t buysPercent) noexcept {
    if (royals <= 0 || buysPercent <= 0) {
        return 0;
    }
    const std::int64_t scaled =
        static_cast<std::int64_t>(royals) * static_cast<std::int64_t>(buysPercent) / 100;
    return static_cast<std::int32_t>(std::max<std::int64_t>(1, scaled));
}

}  // namespace granadad::sim
