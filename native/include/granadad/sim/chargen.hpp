#pragma once

// A NAME FOR YOURSELF, BEFORE THE WARD GIVES YOU ONE. legend.hpp derives what
// the ward calls you from what you go on to do; this file is the one earlier
// question nothing in this build has ever asked -- what were you already good
// at, walking in.
//
// DAGGERFALL-INSPIRED, ON PURPOSE. Three tiers of skill (Primary, Major,
// Minor) and a pool of points to raise attributes above their floor is the
// exact shape of a Daggerfall-style character sheet, chosen because it is the
// UI vocabulary this sprint's brief was written against. It is not a UI here
// -- there is no screen in this file, no text, no key. It is the arithmetic a
// BG3/DOS2-style origin-template screen (Devin, Gabri, or a fully custom
// path) will eventually stand on top of, built and proved first so that
// screen has real numbers to show rather than numbers invented to match it.
//
// WIRED INTO THE REAL PROGRESSION SYSTEM, NOT A RIVAL ONE. Chargen::designate
// only ever accepts an id social.hpp's SkillTrack already knows -- it reads
// content/raws/skills/skills.json through the SAME SkillTrack the rest of the
// game uses to check "is this a real skill", and Chargen::apply() writes
// starting levels through SkillTrack::setLevel(), the same setter every other
// caller uses. There is no second skill list anywhere in this file.
//
// THE FLAME IS NOT ON THE SHEET. content/raws/skills/skills.json's own note
// calls the_flame "Gabri-unique Source track (ability-unlock effects out of
// scope this pass)" -- aptitudeTier FLAME, the one skill this build tags that
// way. Chargen::designate refuses it. A fresh arrival choosing "Primary: The
// Flame" off a menu, for a track this build cannot yet unlock anything on,
// would be a promise the game does not keep -- the exact class of bug the
// standing quality bar exists to catch, just moved from a rendered string to
// a chargen choice.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO. AptitudeTier (attributes.hpp) is
// parsed off the raws now (social.hpp's SkillTrack::Entry carries it) but
// this file's starting-level math does not read it, and neither does
// usesForLevel() (social.hpp) for the XP a skill earns after chargen. The
// Java reference engine ties aptitude to a use-XP cost ratio
// (PROGRESSION-SPEC.md section 1); wiring that same ratio into usesForLevel()
// would change how fast every ALREADY-LIVE skill grinds (skyrunning and
// streetwise are both Favored and are levelled every session already,
// cracksmanship and linkcraft are Trained) for every existing save and every
// existing test that times a grind against it -- lockpick.hpp's own
// kFeelLevel comment pins an exact probe count against the CURRENT flat
// formula. That is a live-tuning change with its own blast radius and its own
// verification pass, not a chargen mechanic, so it is left alone here,
// stated rather than silently skipped.
//
// NOT SIMULATION STATE. Chargen is a one-time calculator: it holds the
// designations and the attribute spend WHILE a character is being built, and
// apply() hands its answer to a real SkillTrack and a real AttributeBlock and
// is done. Nothing hashes a Chargen and nothing needs to -- once apply() has
// run, the only state that persists is the SkillTrack levels it set (already
// hashed by SkillTrack::hashInto) and the AttributeBlock it returned (hashable
// on its own terms, see attributes.hpp), exactly the way a calculator is not
// part of the ledger it wrote a number into.
//
// NO FLOATS. Every slot count, every starting level and every point in the
// bonus pool is a small integer.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/attributes.hpp"

namespace granadad::sim {

class SkillTrack;

// ---------------------------------------------------------------------------
// skill designation
// ---------------------------------------------------------------------------

/// Where one skill sits on the sheet. None is "not chosen yet", not a fourth
/// tier -- it holds no slot and buys no starting level.
enum class SkillDesignation : std::uint8_t {
    None = 0,
    Primary = 1,
    Major = 2,
    Minor = 3,
};

[[nodiscard]] std::string_view skillDesignationName(SkillDesignation designation) noexcept;

/// Daggerfall's own numbers: three Primary, three Major, six Minor. Twelve
/// skills named out of the raws' twenty (nineteen, with THE FLAME excluded),
/// which leaves eight the sheet never touches -- exactly the shape the
/// reference UI was built against, not a number invented for this pass.
inline constexpr std::int32_t kPrimarySkillSlots = 3;
inline constexpr std::int32_t kMajorSkillSlots = 3;
inline constexpr std::int32_t kMinorSkillSlots = 6;

/// How many slots a tier has, total. 0 for None -- there is nothing to fill.
[[nodiscard]] std::int32_t skillDesignationSlots(SkillDesignation designation) noexcept;

/// Starting level a designation buys, 0..100, the same scale SkillTrack's own
/// level already reads on. (Placeholder / needs-blessing, same status as
/// every other starting number in PROGRESSION-SPEC.md -- chosen to echo
/// Daggerfall's own Primary-higher-than-Major-higher-than-Minor starting
/// spread rather than measured against anything Granadad-specific yet.)
inline constexpr std::int32_t kPrimaryStartLevel = 30;
inline constexpr std::int32_t kMajorStartLevel = 15;
inline constexpr std::int32_t kMinorStartLevel = 5;

[[nodiscard]] std::int32_t startingLevelFor(SkillDesignation designation) noexcept;

/// One chosen skill, and the tier it was chosen at. Exposed read-only off
/// Chargen::picks() so a UI can draw the sheet without reaching into private
/// state.
struct SkillPick {
    std::string id;
    SkillDesignation tier = SkillDesignation::None;
};

// ---------------------------------------------------------------------------
// the attribute bonus pool
// ---------------------------------------------------------------------------

/// Total points a fresh character has to spend, across all four attributes.
/// (Placeholder / needs-blessing.)
inline constexpr std::int32_t kAttributeBonusPool = 24;
/// The most any single attribute's bonus can hold, so the pool cannot be
/// dumped into one number. (Placeholder / needs-blessing.)
inline constexpr std::int32_t kAttributeBonusPerAttributeCap = 15;

// ---------------------------------------------------------------------------
// Chargen
// ---------------------------------------------------------------------------

/// The sheet being built. Everything here is a plain calculator over the real
/// SkillTrack and a fresh AttributeBlock -- see this file's own header for
/// what it deliberately leaves for later.
class Chargen {
public:
    /// Puts `skillId` at `tier` against `raws` (the loaded SkillTrack every
    /// other system already reads). Fails, changing nothing, when:
    ///   - `tier` is None (clear() is the undesignate path, not this);
    ///   - `raws` does not know `skillId` (SkillTrack::find would return
    ///     nullptr);
    ///   - `skillId` is THE FLAME's own id (aptitudeTier Flame -- see this
    ///     file's header);
    ///   - `tier`'s slots are already full and `skillId` does not already
    ///     hold a DIFFERENT designation to move out of.
    /// Designating a skill already at `tier` is a no-op success. Designating
    /// a skill that already holds a different tier MOVES it -- frees its old
    /// slot, fills the new one -- exactly what clicking a different column
    /// does on the sheet a player will eventually see.
    bool designate(std::string_view skillId, SkillDesignation tier,
                   const SkillTrack& raws) noexcept;

    /// Drops whatever designation `skillId` holds. True if it held one.
    bool clear(std::string_view skillId) noexcept;

    [[nodiscard]] SkillDesignation designationOf(std::string_view skillId) const noexcept;
    [[nodiscard]] std::int32_t slotsFilled(SkillDesignation tier) const noexcept;
    [[nodiscard]] std::int32_t slotsRemaining(SkillDesignation tier) const noexcept;
    /// True once every Primary, Major and Minor slot holds a skill -- the
    /// gate a chargen flow checks before it lets a player leave this page.
    [[nodiscard]] bool skillsComplete() const noexcept;
    /// Every pick made so far, ascending by skill id.
    [[nodiscard]] const std::vector<SkillPick>& picks() const noexcept { return picks_; }

    // --- the attribute bonus pool --------------------------------------

    [[nodiscard]] std::int32_t attributeBonus(AttributeId attribute) const noexcept;
    [[nodiscard]] std::int32_t attributePointsSpent() const noexcept;
    [[nodiscard]] std::int32_t attributePointsRemaining() const noexcept {
        return kAttributeBonusPool - attributePointsSpent();
    }
    /// Moves `delta` points (positive to spend, negative to give back) onto
    /// `attribute`'s bonus. Fails, changing NOTHING, if the result would put
    /// that attribute's own bonus outside [0, kAttributeBonusPerAttributeCap]
    /// or the pool's own total outside [0, kAttributeBonusPool] -- a
    /// two-ended clamp that REFUSES rather than silently truncates, so a UI
    /// asking "can I afford this" gets a straight answer instead of a
    /// partial spend it has to detect on its own.
    bool spendAttributePoints(AttributeId attribute, std::int32_t delta) noexcept;

    // --- committing ------------------------------------------------------

    /// Writes every designated skill's starting level into `track` via
    /// SkillTrack::setLevel() -- the raws' own setter, so a designated skill
    /// starts exactly the way any other level-up leaves one: level set, uses
    /// zeroed. Skills never designated are left exactly as `track` already
    /// had them. Returns the AttributeBlock the bonus pool bought, layered
    /// over kAttributeBase.
    ///
    /// IDEMPOTENT: calling apply() twice with nothing else changed sets the
    /// same levels and returns the same block -- it recomputes from the
    /// picks and the pool every time rather than adding on top of whatever
    /// `track` already held, so re-running chargen (or a UI calling apply()
    /// after every click to preview the result) never compounds.
    [[nodiscard]] AttributeBlock apply(SkillTrack& track) const;

private:
    [[nodiscard]] SkillPick* findPick(std::string_view skillId) noexcept;
    [[nodiscard]] const SkillPick* findPick(std::string_view skillId) const noexcept;

    /// Ascending by id, the same discipline SkillTrack's own entries_ keeps.
    std::vector<SkillPick> picks_;
    std::array<std::int32_t, kAttributeCount> attributeBonus_{};
};

}  // namespace granadad::sim
