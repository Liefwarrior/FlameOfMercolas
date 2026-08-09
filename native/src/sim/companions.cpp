#include "granadad/sim/companions.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "granadad/sim/chargen.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string readWhole(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    return found != node.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    return found != node.end() && found->is_number_integer() ? found->get<std::int32_t>() : 0;
}

/// Every skill id named in `tierArray`, added at `level` -- the exact level
/// startingLevelFor(tier) (chargen.hpp) hands back for it, never a number
/// this file quotes itself, so a future change to kPrimaryStartLevel/
/// kMajorStartLevel/kMinorStartLevel is felt here automatically rather than
/// needing a second edit. Duplicate/unknown-shaped entries are skipped, not
/// fatal -- the same defensive stance every raws loader in this build keeps.
void appendTier(const nlohmann::json& preset, const char* tierKey, SkillDesignation tier,
                std::vector<CompanionSkill>& out) {
    const auto found = preset.find(tierKey);
    if (found == preset.end() || !found->is_array()) {
        return;
    }
    const std::int32_t level = startingLevelFor(tier);
    for (const nlohmann::json& entry : *found) {
        if (!entry.is_string()) {
            continue;
        }
        out.push_back(CompanionSkill{entry.get<std::string>(), level});
    }
}

}  // namespace

CompanionTemplate CompanionTemplate::load(const std::filesystem::path& contentDir,
                                          std::string_view id) {
    CompanionTemplate out;
    const std::filesystem::path path =
        contentDir / "raws" / "companions" / (std::string(id) + ".json");
    const std::string text = readWhole(path);
    if (text.empty()) {
        return out;
    }
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }

    // id COMES OFF THE FILE, NOT OFF THE CALLER'S ARGUMENT -- the same
    // discipline FACES-SPEC.md's named-face format holds ("id must equal
    // filename stem"). A file whose own "id" is missing or blank never
    // reports loaded(), even if the bytes otherwise parsed, so a truncated
    // or half-written companion file cannot pass as a real one.
    std::string fileId = stringField(document, "id");
    if (fileId.empty()) {
        return out;
    }

    out.id_ = std::move(fileId);
    out.name_ = stringField(document, "name");
    out.epithet_ = stringField(document, "epithet");
    out.archetype_ = stringField(document, "archetype");
    out.bio_ = stringField(document, "bio");
    out.selfIntro_ = stringField(document, "selfIntro");

    // THE FIXED SHEET IS A chargenPreset, not a raw level-per-skill table --
    // see the file header. "primary"/"major"/"minor" are arrays of skill ids;
    // "untouched" is documentation only (an id left there is simply never
    // named in the other three arrays, and startingLevel() already answers
    // 0 for anything this template never designated). the_flame is refused
    // by chargen.cpp's own designate() and is never listed in any tier here,
    // for either template -- the lock is enforced upstream, not re-checked.
    if (const auto preset = document.find("chargenPreset");
        preset != document.end() && preset->is_object()) {
        appendTier(*preset, "primary", SkillDesignation::Primary, out.startingSkills_);
        appendTier(*preset, "major", SkillDesignation::Major, out.startingSkills_);
        appendTier(*preset, "minor", SkillDesignation::Minor, out.startingSkills_);

        if (const auto spend = preset->find("attributeBonusSpend");
            spend != preset->end() && spend->is_object()) {
            // THE EXACT FORMULA Chargen::apply() USES (chargen.cpp): base
            // plus spend, nothing else -- read off attributes.hpp's own
            // kAttributeBase rather than a second copy of 40 quoted here.
            out.derivedAttributes_[static_cast<std::size_t>(AttributeId::Might)] =
                kAttributeBase + intField(*spend, "MGT");
            out.derivedAttributes_[static_cast<std::size_t>(AttributeId::Agility)] =
                kAttributeBase + intField(*spend, "AGI");
            out.derivedAttributes_[static_cast<std::size_t>(AttributeId::Vigor)] =
                kAttributeBase + intField(*spend, "VIG");
            out.derivedAttributes_[static_cast<std::size_t>(AttributeId::Wit)] =
                kAttributeBase + intField(*spend, "WIT");
        }
    }

    if (const auto appearance = document.find("appearanceType");
        appearance != document.end() && appearance->is_string()) {
        out.appearanceType_ = appearanceTypeFromId(appearance->get<std::string>());
    }

    // PRESENCE IS NOT ONE OF THE FOUR ATTRIBUTES attributeBonusSpend can
    // reach -- see the header on presenceAtStart(). A template that never
    // authors this field (every companion but an actual Wielder) reads 0,
    // which is deliberately not a claim of "zero social standing", only "no
    // number authored here" -- the same absent-reads-as-nothing rule
    // startingLevel() itself follows.
    out.presence_ = intField(document, "presenceAtStart");

    return out;
}

std::int32_t CompanionTemplate::startingLevel(std::string_view skillId) const noexcept {
    for (const CompanionSkill& skill : startingSkills_) {
        if (skill.id == skillId) {
            return skill.level;
        }
    }
    return 0;
}

std::int32_t CompanionTemplate::applyStartingSkills(SkillTrack& track) const {
    std::int32_t known = 0;
    for (const CompanionSkill& skill : startingSkills_) {
        // setLevel()'s [[nodiscard]] bool is exactly "did the raws know this
        // id" -- true for every id content/raws/skills/skills.json actually
        // authors. A companion file that names a skill the raws do not have
        // is a content bug, and this count is how a caller (or a test) finds
        // out rather than has it silently absorbed.
        if (track.setLevel(skill.id, skill.level)) {
            ++known;
        }
    }
    return known;
}

}  // namespace granadad::sim
