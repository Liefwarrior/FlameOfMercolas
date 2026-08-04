#include "granadad/sim/notables.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringOr(const nlohmann::json& node, const char* key,
                                   const char* fallback) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return std::string(fallback);
    }
    // Folded on the way in: every one of these strings can end up on the HUD,
    // and the raws carry mojibake em-dashes the 4x6 font cannot draw.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] nlohmann::json parseFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nlohmann::json();
    }
    std::ostringstream text;
    text << file.rdbuf();
    nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded()) {
        return nlohmann::json();
    }
    return document;
}

}  // namespace

// ---------------------------------------------------------------------------
// Notable
// ---------------------------------------------------------------------------

std::int32_t Notable::skillLevel(std::string_view id) const noexcept {
    for (const NotableSkill& entry : skills) {
        if (entry.skill == id) {
            return entry.level;
        }
    }
    return 0;
}

std::string_view Notable::bestSkill() const noexcept {
    const NotableSkill* best = nullptr;
    for (const NotableSkill& entry : skills) {
        // Strictly greater, and the list is already ascending by skill id, so a
        // tie resolves to the alphabetically first skill on every machine.
        if (best == nullptr || entry.level > best->level) {
            best = &entry;
        }
    }
    return best == nullptr ? std::string_view{} : std::string_view{best->skill};
}

std::int32_t Notable::bestSkillLevel() const noexcept {
    std::int32_t best = 0;
    for (const NotableSkill& entry : skills) {
        best = std::max(best, entry.level);
    }
    return best;
}

// ---------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------

std::filesystem::path notableRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "names" / "notables.json";
}
std::filesystem::path historyRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "names" / "histories.json";
}
std::filesystem::path rumorRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "rumors" / "rumors.json";
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

NotableRegistry NotableRegistry::load(const std::filesystem::path& contentDir) {
    NotableRegistry out;

    const nlohmann::json people = parseFile(notableRawsPath(contentDir));
    if (people.is_object()) {
        const auto array = people.find("notables");
        if (array != people.end() && array->is_array()) {
            for (const nlohmann::json& node : *array) {
                if (!node.is_object()) {
                    continue;
                }
                Notable notable;
                notable.id = stringOr(node, "id", "");
                if (notable.id.empty()) {
                    continue;
                }
                notable.name = stringOr(node, "name", notable.id.c_str());
                notable.epithet = stringOr(node, "epithet", "");
                notable.type = stringOr(node, "type", "");
                notable.site = stringOr(node, "site", "");
                notable.bio = stringOr(node, "bio", "");
                const auto skills = node.find("skills");
                if (skills != node.end() && skills->is_object()) {
                    for (const auto& entry : skills->items()) {
                        if (!entry.value().is_number_integer()) {
                            continue;
                        }
                        notable.skills.push_back(
                            NotableSkill{entry.key(), entry.value().get<std::int32_t>()});
                    }
                    // nlohmann's object iteration is already sorted by key, but
                    // sorting here means the guarantee is OURS and does not move
                    // if the JSON library's container type is ever configured
                    // differently.
                    std::sort(notable.skills.begin(), notable.skills.end(),
                              [](const NotableSkill& a, const NotableSkill& b) {
                                  return a.skill < b.skill;
                              });
                }
                out.notables_.push_back(std::move(notable));
            }
        }
    }

    const nlohmann::json stories = parseFile(historyRawsPath(contentDir));
    if (stories.is_object()) {
        const auto array = stories.find("histories");
        if (array != stories.end() && array->is_array()) {
            for (const nlohmann::json& node : *array) {
                if (!node.is_object()) {
                    continue;
                }
                History history;
                history.id = stringOr(node, "id", "");
                if (history.id.empty()) {
                    continue;
                }
                history.kind = stringOr(node, "kind", "");
                history.a = stringOr(node, "a", "");
                history.b = stringOr(node, "b", "");
                history.edge = stringOr(node, "edge", "");
                history.gossipKey = stringOr(node, "gossip", "");
                history.bioA = stringOr(node, "bioA", "");
                history.bioB = stringOr(node, "bioB", "");
                out.histories_.push_back(std::move(history));
            }
        }
    }

    const nlohmann::json rumors = parseFile(rumorRawsPath(contentDir));
    if (rumors.is_object()) {
        const auto array = rumors.find("domains");
        if (array != rumors.end() && array->is_array()) {
            for (const nlohmann::json& node : *array) {
                if (!node.is_object()) {
                    continue;
                }
                RumorDomain domain;
                domain.historyId = stringOr(node, "history", "");
                if (domain.historyId.empty()) {
                    continue;
                }
                const auto knowers = node.find("knowers");
                if (knowers != node.end() && knowers->is_array()) {
                    for (const nlohmann::json& knower : *knowers) {
                        if (knower.is_string()) {
                            domain.knowers.push_back(knower.get<std::string>());
                        }
                    }
                }
                std::sort(domain.knowers.begin(), domain.knowers.end());
                domain.knowers.erase(std::unique(domain.knowers.begin(), domain.knowers.end()),
                                     domain.knowers.end());
                out.domains_.push_back(std::move(domain));
            }
        }
    }

    std::sort(out.notables_.begin(), out.notables_.end(),
              [](const Notable& a, const Notable& b) { return a.id < b.id; });
    std::sort(out.histories_.begin(), out.histories_.end(),
              [](const History& a, const History& b) { return a.id < b.id; });
    std::sort(out.domains_.begin(), out.domains_.end(),
              [](const RumorDomain& a, const RumorDomain& b) { return a.historyId < b.historyId; });
    return out;
}

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

const Notable* NotableRegistry::find(std::string_view id) const noexcept {
    const auto found = std::lower_bound(
        notables_.begin(), notables_.end(), id,
        [](const Notable& notable, std::string_view probe) { return notable.id < probe; });
    if (found == notables_.end() || found->id != id) {
        return nullptr;
    }
    return &*found;
}

const History* NotableRegistry::history(std::string_view id) const noexcept {
    const auto found = std::lower_bound(
        histories_.begin(), histories_.end(), id,
        [](const History& history, std::string_view probe) { return history.id < probe; });
    if (found == histories_.end() || found->id != id) {
        return nullptr;
    }
    return &*found;
}

bool NotableRegistry::isKnower(std::string_view historyId,
                               std::string_view notableId) const noexcept {
    const auto found = std::lower_bound(
        domains_.begin(), domains_.end(), historyId,
        [](const RumorDomain& domain, std::string_view probe) { return domain.historyId < probe; });
    if (found == domains_.end() || found->historyId != historyId) {
        return false;
    }
    return std::binary_search(found->knowers.begin(), found->knowers.end(), notableId);
}

std::vector<const History*> NotableRegistry::tellableBy(std::string_view notableId) const {
    std::vector<const History*> own;
    std::vector<const History*> heard;
    if (notableId.empty()) {
        return own;
    }
    // histories_ is sorted by id, so both groups come out ascending without a
    // second sort.
    for (const History& history : histories_) {
        if (history.involves(notableId)) {
            own.push_back(&history);
        } else if (isKnower(history.id, notableId)) {
            heard.push_back(&history);
        }
    }
    own.insert(own.end(), heard.begin(), heard.end());
    return own;
}

}  // namespace granadad::sim
