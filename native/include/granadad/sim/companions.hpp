#pragma once

// THE FIXED SHEETS: WHAT DEVIN AND GABRI WALK IN ALREADY KNOWING.
//
// content/raws/companions/*.json (devin.json, gabri.json) are hand-authored
// character sheets for the two BG3/Divinity-Original-Sin-2-style origin
// templates task #80 named. WIRED THROUGH THE REAL chargen.hpp/attributes.hpp
// ENGINE, NOT A RIVAL ONE: each raw's own `chargenPreset` names which skill
// ids sit at Primary/Major/Minor (chargen.hpp's own three tiers) and how its
// 24-point attribute bonus pool was spent -- the same vocabulary a CUSTOM
// character's own sheet is built out of, just hand-picked instead of point-
// bought. This file resolves that preset at load time: startingLevelFor(tier)
// (chargen.hpp) gives each designated skill its exact level, and
// kAttributeBase (attributes.hpp) plus the raw's own spend gives each
// attribute its exact value -- the SAME two formulas Chargen::apply() itself
// uses, so a CompanionTemplate's numbers can never drift from what a player
// building the identical sheet by hand through the CUSTOM path would get.
//
// WIRED INTO THE REAL PROGRESSION SYSTEM, NOT A RIVAL ONE. Every id a
// template's chargenPreset names is checked against the SAME SkillTrack the
// rest of the game reads (social.hpp) the moment applyStartingSkills() is
// actually called -- writing through SkillTrack::setLevel(), the one setter
// every other caller in this build uses. A typo'd skill id in the content
// file is a return value short of startingSkills().size(), never a silent
// skip and never a crash.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO. It does not spawn an actor and
// does not build a Session -- turning a chosen template into a running
// game's opening state is the remaining integration work. render/
// creation.hpp's CreationFlow already hosts a CompanionTemplate per origin
// card (see its own header), which is the one piece of that integration this
// file's own design anticipated and the one already built.
//
// NO FLOATS, NO SIMULATION STATE. Loaded once and read; nothing here is
// hashed and nothing here reaches PhasedEngine, the identical boundary
// chargen.hpp's own header draws around Chargen.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/appearance.hpp"
#include "granadad/sim/attributes.hpp"

namespace granadad::sim {

class SkillTrack;

/// One resolved skill line: an id and the EXACT starting level its
/// chargenPreset tier resolves to (startingLevelFor(tier), chargen.hpp) --
/// not the tier itself, see the file header. Only a template's DESIGNATED
/// skills appear here; an id the raw leaves untouched is simply absent
/// (startingLevel() answers 0 for it, same as SkillTrack::level() does for
/// an id it does not know).
struct CompanionSkill {
    std::string id;
    std::int32_t level = 0;
};

/// content/raws/companions/<id>.json, read whole and resolved through the
/// real chargen arithmetic.
class CompanionTemplate {
public:
    /// Reads content/raws/companions/<id>.json. NEVER throws: a missing or
    /// malformed file answers loaded() == false, the same defensive contract
    /// every other raws loader in this build keeps (SkillTrack::load,
    /// ActorSheet::load, NameRaws::load).
    [[nodiscard]] static CompanionTemplate load(const std::filesystem::path& contentDir,
                                                std::string_view id);

    [[nodiscard]] bool loaded() const noexcept { return !id_.empty(); }
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& epithet() const noexcept { return epithet_; }
    [[nodiscard]] const std::string& archetype() const noexcept { return archetype_; }
    [[nodiscard]] const std::string& bio() const noexcept { return bio_; }
    [[nodiscard]] const std::string& selfIntro() const noexcept { return selfIntro_; }
    /// Only the skills this template's chargenPreset actually designated
    /// (Primary, Major or Minor) -- see the struct's own header on why an
    /// untouched id is absent rather than present at 0.
    [[nodiscard]] const std::vector<CompanionSkill>& startingSkills() const noexcept {
        return startingSkills_;
    }
    /// The resolved starting level for `skillId`, or 0 if this template's
    /// chargenPreset never designated it -- the same "absent reads as zero"
    /// convention SkillTrack::level() itself uses for an id it does not know.
    [[nodiscard]] std::int32_t startingLevel(std::string_view skillId) const noexcept;

    /// Off sim/appearance.hpp's own eleven, or std::nullopt if the raw's
    /// appearanceType is missing, unrecognised, or names a WardType that
    /// table deliberately leaves out (a child, a beast).
    [[nodiscard]] std::optional<WardType> appearanceType() const noexcept {
        return appearanceType_;
    }

    /// kAttributeBase (attributes.hpp) plus the raw's own chargenPreset.
    /// attributeBonusSpend for `attribute` -- the exact value
    /// Chargen::apply() would hand back for the identical spend, computed
    /// once at load time rather than re-derived by every caller.
    [[nodiscard]] std::int32_t derivedAttribute(AttributeId attribute) const noexcept {
        return derivedAttributes_[static_cast<std::size_t>(attribute)];
    }
    /// The raw's own top-level presenceAtStart, or 0 if it does not author
    /// one. Presence is not one of attributes.hpp's four AttributeId values
    /// (PROGRESSION-SPEC.md section 5: "STATIC 100... not derived, not
    /// trainable" -- a fact about HOLDING THE WIELDER'S TITLE, not a generic
    /// attribute every character has an opinion about), so it is carried
    /// here as its own field rather than forcing a fifth ordinal into a type
    /// that answers a different question everywhere else it is used. Most
    /// templates (a companion, not a Wielder) are expected to leave this
    /// unauthored -- see devin.json's own presenceNote for why 0 here is
    /// "not answered", not "answered zero".
    [[nodiscard]] std::int32_t presenceAtStart() const noexcept { return presence_; }

    /// Writes every designated skill's resolved level into `track` via
    /// SkillTrack::setLevel() -- see the file header. Returns how many of
    /// this template's startingSkills entries the raws actually knew; a
    /// return short of startingSkills().size() means a skill id in the
    /// CONTENT file does not exist in content/raws/skills/skills.json, which
    /// is a content bug this call surfaces rather than swallows.
    std::int32_t applyStartingSkills(SkillTrack& track) const;

private:
    std::string id_;
    std::string name_;
    std::string epithet_;
    std::string archetype_;
    std::string bio_;
    std::string selfIntro_;
    std::vector<CompanionSkill> startingSkills_;
    std::optional<WardType> appearanceType_;
    std::array<std::int32_t, kAttributeCount> derivedAttributes_{};
    std::int32_t presence_ = 0;
};

}  // namespace granadad::sim
