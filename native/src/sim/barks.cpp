#include "granadad/sim/barks.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// Thresholds for the three authored mastery bands. The band NAMES are canon
/// (mastery.<skill>.novice|adept|master); where the lines start is ours, and it
/// follows notables.json's own note -- "masters ~30-50, journeymen ~10-25".
constexpr std::int32_t kNoviceFrom = 5;
constexpr std::int32_t kAdeptFrom = 20;
constexpr std::int32_t kMasterFrom = 40;

}  // namespace

// ---------------------------------------------------------------------------
// vocabulary
// ---------------------------------------------------------------------------

std::string_view attitudeKey(Attitude attitude) noexcept {
    switch (attitude) {
        case Attitude::Hostile:
            return "hostile";
        case Attitude::Cold:
            return "cold";
        case Attitude::Neutral:
            return "neutral";
        case Attitude::Warm:
            return "warm";
        case Attitude::Friend:
            return "friend";
        case Attitude::Kin:
            return "kin";
    }
    return "neutral";
}

std::string_view attitudeName(Attitude attitude) noexcept {
    switch (attitude) {
        case Attitude::Hostile:
            return "HOSTILE";
        case Attitude::Cold:
            return "COLD";
        case Attitude::Neutral:
            return "NEUTRAL";
        case Attitude::Warm:
            return "WARM";
        case Attitude::Friend:
            return "FRIEND";
        case Attitude::Kin:
            return "KIN";
    }
    return "NEUTRAL";
}

std::string_view jobFamilyKey(JobFamily family) noexcept {
    switch (family) {
        case JobFamily::Serf:
            return "serf";
        case JobFamily::Wastrel:
            return "wastrel";
        case JobFamily::Watch:
            return "watch";
        case JobFamily::Clergy:
            return "clergy";
        case JobFamily::Trade:
            return "trade";
        case JobFamily::Maritime:
            return "maritime";
        case JobFamily::Husbandry:
            return "husbandry";
        case JobFamily::Beast:
            return "beast";
        case JobFamily::FlameOfMerc:
            return "flame_of_merc";
    }
    return "serf";
}

std::string_view timeBandKey(TimeBand band) noexcept {
    switch (band) {
        case TimeBand::Morning:
            return "morning";
        case TimeBand::Day:
            return "day";
        case TimeBand::Evening:
            return "evening";
        case TimeBand::Night:
            return "night";
    }
    return "day";
}

TimeBand timeBandOf(std::int32_t secondOfDay) noexcept {
    const std::int32_t wrapped = ((secondOfDay % 86400) + 86400) % 86400;
    const std::int32_t hour = wrapped / 3600;
    if (hour >= 5 && hour < 11) {
        return TimeBand::Morning;
    }
    if (hour >= 11 && hour < 17) {
        return TimeBand::Day;
    }
    if (hour >= 17 && hour < 22) {
        return TimeBand::Evening;
    }
    return TimeBand::Night;
}

// ---------------------------------------------------------------------------
// ascii folding
// ---------------------------------------------------------------------------

std::string foldToAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool inHighRun = false;
    for (const char raw : text) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (byte >= 0x80U) {
            if (!inHighRun) {
                out.push_back('-');
                inHighRun = true;
            }
            continue;
        }
        inHighRun = false;
        // Control characters would render as blanks and can only have come from
        // a hand edit; a tab is a space and everything else is dropped.
        if (byte == '\t') {
            out.push_back(' ');
        } else if (byte >= 0x20U) {
            out.push_back(raw);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// the tables
// ---------------------------------------------------------------------------

std::filesystem::path barkRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "barks" / "barks.json";
}

BarkTables BarkTables::load(const std::filesystem::path& contentDir) {
    BarkTables out;
    std::ifstream file(barkRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();

    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }
    const auto tables = document.find("tables");
    if (tables == document.end() || !tables->is_array()) {
        return out;
    }

    for (const nlohmann::json& node : *tables) {
        if (!node.is_object()) {
            continue;
        }
        const auto key = node.find("key");
        const auto rows = node.find("rows");
        if (key == node.end() || !key->is_string() || rows == node.end() || !rows->is_array()) {
            continue;
        }
        Table table;
        table.key = key->get<std::string>();
        if (table.key.empty()) {
            continue;
        }
        for (const nlohmann::json& row : *rows) {
            if (!row.is_string()) {
                continue;
            }
            std::string line = foldToAscii(row.get<std::string>());
            if (!line.empty()) {
                table.rows.push_back(std::move(line));
            }
        }
        if (table.rows.empty()) {
            continue;
        }
        out.tables_.push_back(std::move(table));
    }

    // Sorted by key so lookup is a binary search and iteration order is a
    // property of the CONTENT rather than of the standard library.
    std::sort(out.tables_.begin(), out.tables_.end(),
              [](const Table& a, const Table& b) { return a.key < b.key; });
    // A duplicate key in the raws would make lookup depend on sort stability.
    // The last one wins, deterministically, and the earlier is dropped.
    out.tables_.erase(std::unique(out.tables_.begin(), out.tables_.end(),
                                  [](const Table& a, const Table& b) { return a.key == b.key; }),
                      out.tables_.end());
    return out;
}

std::size_t BarkTables::rowCount() const noexcept {
    std::size_t total = 0;
    for (const Table& table : tables_) {
        total += table.rows.size();
    }
    return total;
}

const std::vector<std::string>* BarkTables::rows(std::string_view key) const noexcept {
    const auto found = std::lower_bound(
        tables_.begin(), tables_.end(), key,
        [](const Table& table, std::string_view probe) { return table.key < probe; });
    if (found == tables_.end() || found->key != key) {
        return nullptr;
    }
    return &found->rows;
}

std::string_view BarkTables::line(std::string_view key, std::int32_t index) const noexcept {
    const std::vector<std::string>* found = rows(key);
    if (found == nullptr || found->empty()) {
        return {};
    }
    const std::int32_t count = static_cast<std::int32_t>(found->size());
    // Made non-negative BEFORE the modulo: C++ truncates toward zero, so
    // -1 % 4 is -1 and would index off the front of the table.
    const std::int32_t slot = ((index % count) + count) % count;
    return (*found)[static_cast<std::size_t>(slot)];
}

std::string_view BarkTables::resolve(const std::vector<std::string>& candidates) const noexcept {
    for (const std::string& candidate : candidates) {
        if (has(candidate)) {
            return candidate;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// key building
// ---------------------------------------------------------------------------

std::vector<std::string> greetChain(JobFamily family, Attitude attitude, TimeBand band) {
    const std::string base = "greet." + std::string(jobFamilyKey(family));
    const std::string tiered = base + "." + std::string(attitudeKey(attitude));
    return {tiered + "." + std::string(timeBandKey(band)), tiered, base};
}

std::vector<std::string> personalChain(std::string_view notableId) {
    std::vector<std::string> chain;
    if (!notableId.empty()) {
        chain.push_back("personal." + std::string(notableId));
    }
    chain.emplace_back("personal");
    return chain;
}

std::vector<std::string> gossipChain(std::string_view historyId) {
    std::vector<std::string> chain;
    if (!historyId.empty()) {
        chain.push_back("gossip." + std::string(historyId));
    }
    chain.emplace_back("gossip");
    return chain;
}

std::vector<std::string> masteryChain(std::string_view skillId, std::int32_t level) {
    if (skillId.empty() || level < kNoviceFrom) {
        return {};
    }
    const std::string base = "mastery." + std::string(skillId) + ".";
    // Most specific band FIRST, then down: a master who has no master table
    // authored for their skill still has something to say about it.
    if (level >= kMasterFrom) {
        return {base + "master", base + "adept", base + "novice"};
    }
    if (level >= kAdeptFrom) {
        return {base + "adept", base + "novice"};
    }
    return {base + "novice"};
}

}  // namespace granadad::sim
