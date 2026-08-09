#pragma once

// The four things a body is good at, before it has practiced anything.
//
// content/raws/skills/skills.json has carried a governingAttribute column
// (MGT, AGI, VIG, WIT or NONE) and an aptitudeTier column (FAVORED, TRAINED,
// NEGLECTED, FLAME) on every one of its twenty rows since S1. Until this
// file, nothing in native/ ever read either one -- SkillTrack::load()
// (social.hpp) took `id` and `displayName` off a raw and threw the rest of
// the object away. The columns were real, authored, and dead.
//
// THE FOUR NAMES ARE NOT INVENTED HERE. docs/design/PROGRESSION-SPEC.md
// section 5 named Might/Agility/Vigor/Wit for the Java reference engine
// before native/ existed, and the raws' own governingAttribute vocabulary
// followed suit. This file gives that vocabulary a value a character can
// actually hold, which is the one thing content/raws/skills/skills.json was
// never allowed to do for itself -- it is authored canon, read-only, and a
// number belongs in code.
//
// NO FLOATS. An attribute is a small integer, the same rule as every other
// piece of simulation state in native/.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// The four attributes a skill can be governed by. Ordinal is stable --
/// nothing persists it across a build yet, but nothing should have to relearn
/// this order later either.
enum class AttributeId : std::uint8_t {
    Might = 0,
    Agility = 1,
    Vigor = 2,
    Wit = 3,
};

inline constexpr std::size_t kAttributeCount = 4;

[[nodiscard]] std::string_view attributeName(AttributeId attribute) noexcept;
/// "MGT" / "AGI" / "VIG" / "WIT" -- the exact tokens
/// content/raws/skills/skills.json's own governingAttribute column uses.
[[nodiscard]] std::string_view attributeAbbrev(AttributeId attribute) noexcept;

/// Parses one raw governingAttribute token. std::nullopt for "NONE" (THE
/// FLAME's own value -- see docs/design/PROGRESSION-SPEC.md section 7) and
/// for anything the raws did not author. Never fabricates a fifth attribute
/// out of a typo.
[[nodiscard]] std::optional<AttributeId> attributeFromRaw(std::string_view token) noexcept;

/// The four aptitude tiers content/raws/skills/skills.json's aptitudeTier
/// column names. PROGRESSION-SPEC.md section 1 ties each to a use-XP cost
/// ratio for the Java reference engine (Favored x3/4, Trained x1, Neglected
/// x5/4, Flame x4). THIS BUILD DOES NOT YET APPLY THAT RATIO to
/// usesForLevel() (social.hpp) -- chargen.hpp's own header says why that
/// wiring is left for a later, separately-verified pass rather than folded in
/// here.
enum class AptitudeTier : std::uint8_t {
    Favored = 0,
    Trained = 1,
    Neglected = 2,
    Flame = 3,
};

[[nodiscard]] std::string_view aptitudeTierName(AptitudeTier tier) noexcept;
/// Parses one raw aptitudeTier token. Unrecognised text reads as Trained --
/// the middle of the road, not a crash, same fallback rule usesForLevel()
/// itself already leans on for an unknown level.
[[nodiscard]] AptitudeTier aptitudeTierFromRaw(std::string_view token) noexcept;

/// The lowest and highest an attribute can ever read, chargen or otherwise.
inline constexpr std::int32_t kAttributeFloor = 10;
inline constexpr std::int32_t kAttributeCeiling = 100;
/// Where all four sit before a single bonus point is spent. (Placeholder,
/// needs-blessing -- the same status every other starting number in
/// PROGRESSION-SPEC.md carries until Eli rules on it.)
inline constexpr std::int32_t kAttributeBase = 40;

/// The four attributes, held together. What Chargen::apply() (chargen.hpp)
/// hands back once a character is built; what a future player-sheet or save
/// system owns from there. This file only defines the shape and its
/// arithmetic -- it is not simulation state until something keeps one.
class AttributeBlock {
public:
    AttributeBlock() noexcept { values_.fill(kAttributeBase); }

    [[nodiscard]] std::int32_t value(AttributeId attribute) const noexcept {
        return values_[static_cast<std::size_t>(attribute)];
    }
    /// Clamped to [kAttributeFloor, kAttributeCeiling].
    void setValue(AttributeId attribute, std::int32_t value) noexcept;

    void hashInto(HashSink& sink) const;

private:
    std::array<std::int32_t, kAttributeCount> values_{};
};

}  // namespace granadad::sim
