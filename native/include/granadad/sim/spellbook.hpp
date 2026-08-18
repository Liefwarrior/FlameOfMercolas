#pragma once

// The eleven authored spells, read out of the owner's canon.
//
// content/raws/spells/spells.json is CANON and READ-ONLY. Every spell in it is
// a list of COMPONENTS -- an effect axis, a mode, a magnitude, a duration --
// rather than a hardcoded outcome, and that shape is the whole point: the
// standing direction on magic is that effects must be modular so spellcrafting
// can compose them the way Daggerfall does. A priest who teaches out of a table
// baked into a .cpp would be a priest who teaches something the owner never
// wrote, and S4 would have to unpick it.
//
// So the priest of the Flame's offer comes from the file. Loading it is
// tolerant by design: a missing raws directory yields an empty book and the
// tavern still opens, because a priest with nothing to teach is a smaller
// failure than a game that will not start.
//
// INTEGERS ONLY. Magnitudes, durations and ranges are all authored as integers
// and stay that way.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::sim {

/// One modular piece of a spell: what axis it moves, how, by how much, for how
/// long. Straight out of the raws, unaltered.
struct SpellComponent {
    std::string effect;
    std::string mode;
    std::int32_t magnitude = 0;
    std::int32_t durationTicks = 0;
    /// WHICH string an ATTRIBUTE row tunes -- "MGT" / "AGI" / "VIG" / "WIT",
    /// the raws' own governingAttribute vocabulary (attributes.hpp parses it).
    /// Authored on every ATTRIBUTE component in spells.json since S13 and
    /// dropped by the loader until the held-effects build gave the axis an
    /// engine that needs to know. Empty on every other axis, and empty on a
    /// FORGED tuning row -- ForgeBench has no param field yet, which is why
    /// the cast refuses those out loud rather than guessing a limb.
    std::string param;
};

/// One authored spell.
struct Spell {
    std::string id;
    std::string displayName;
    /// The skill it is cast with -- linkcraft, channeling, and so on.
    std::string skill;
    std::int32_t minLevel = 0;
    std::int32_t cooldownTicks = 0;
    std::string target;
    std::int32_t range = 0;
    std::int32_t areaRadius = 0;
    std::vector<SpellComponent> components;
};

/// Everything the priest can teach, in the order the raws list it. Document
/// order is deliberate: it is stable, it is the owner's order, and a sort here
/// would be this file inventing a curriculum.
class Spellbook {
public:
    /// Reads content/raws/spells/spells.json under `contentDir`. Never throws:
    /// a missing or malformed file yields an empty book, and `loaded()` says so.
    [[nodiscard]] static Spellbook load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] const std::vector<Spell>& spells() const noexcept { return spells_; }
    [[nodiscard]] std::size_t size() const noexcept { return spells_.size(); }

    /// The spell with this id, or nullptr.
    [[nodiscard]] const Spell* find(std::string_view id) const noexcept;

    /// Everything castable at or below `level` in `skill`. The priest's actual
    /// offer: he does not teach what the student cannot hold.
    [[nodiscard]] std::vector<const Spell*> teachableAt(std::string_view skill,
                                                        std::int32_t level) const;

private:
    bool loaded_ = false;
    std::vector<Spell> spells_;
};

/// content/raws/spells/spells.json.
[[nodiscard]] std::filesystem::path spellRawsPath(const std::filesystem::path& contentDir);

}  // namespace granadad::sim
