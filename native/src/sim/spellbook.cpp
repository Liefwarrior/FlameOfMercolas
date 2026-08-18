#include "granadad/sim/spellbook.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// Reads an integer field that may be absent. The raws are canon and complete,
/// but a loader that throws on a field somebody has not written yet is a loader
/// that stops the game booting over a content edit.
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

}  // namespace

std::filesystem::path spellRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "spells" / "spells.json";
}

Spellbook Spellbook::load(const std::filesystem::path& contentDir) {
    Spellbook book;
    std::ifstream file(spellRawsPath(contentDir), std::ios::binary);
    if (!file) {
        return book;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Never throws out of here: the tavern must still open when the raws are
    // missing. Same rule the lamp bake follows.
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return book;
    }
    const auto spells = document.find("spells");
    if (spells == document.end() || !spells->is_array()) {
        return book;
    }

    for (const nlohmann::json& node : *spells) {
        if (!node.is_object()) {
            continue;
        }
        Spell spell;
        spell.id = stringOr(node, "id", "");
        if (spell.id.empty()) {
            continue;
        }
        spell.displayName = stringOr(node, "displayName", spell.id.c_str());
        spell.skill = stringOr(node, "skill", "");
        spell.minLevel = intOr(node, "minLevel", 0);
        spell.cooldownTicks = intOr(node, "cooldownTicks", 0);
        spell.target = stringOr(node, "target", "SELF");
        spell.range = intOr(node, "range", 0);
        spell.areaRadius = intOr(node, "areaRadius", 0);

        const auto components = node.find("components");
        if (components != node.end() && components->is_array()) {
            for (const nlohmann::json& part : *components) {
                if (!part.is_object()) {
                    continue;
                }
                SpellComponent component;
                component.effect = stringOr(part, "effect", "");
                component.mode = stringOr(part, "mode", "");
                component.magnitude = intOr(part, "magnitude", 0);
                component.durationTicks = intOr(part, "durationTicks", 0);
                component.param = stringOr(part, "param", "");
                spell.components.push_back(std::move(component));
            }
        }
        book.spells_.push_back(std::move(spell));
    }
    book.loaded_ = !book.spells_.empty();
    return book;
}

const Spell* Spellbook::find(std::string_view id) const noexcept {
    for (const Spell& spell : spells_) {
        if (spell.id == id) {
            return &spell;
        }
    }
    return nullptr;
}

std::vector<const Spell*> Spellbook::teachableAt(std::string_view skill,
                                                 std::int32_t level) const {
    std::vector<const Spell*> offer;
    for (const Spell& spell : spells_) {
        if (spell.skill == skill && spell.minLevel <= level) {
            offer.push_back(&spell);
        }
    }
    return offer;
}

}  // namespace granadad::sim
