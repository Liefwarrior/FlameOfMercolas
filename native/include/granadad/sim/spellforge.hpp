#pragma once

// Spellcrafting, Daggerfall-shaped, over the owner's own effect model.
//
// WHY THIS EXISTS AT ALL. content/raws/spells/spells.json is not a list of
// outcomes, it is a list of COMPOSITIONS: every one of the eleven authored
// craftings is (effect axis x time shape x magnitude x duration x target x
// range) and nothing else. That shape was authored deliberately -- the standing
// direction on magic is that effects must be MODULAR so spellcrafting can
// compose them the way Daggerfall's Mages Guild does. This file is the verb
// that shape was waiting for. It composes new craftings out of the same
// vocabulary, prices them with canon's own cost model, and refuses the ones
// canon says cannot exist.
//
// EVERYTHING HERE TRACES TO docs/lore/MAGIC-CANON.md, section 2, which traces
// to the novel. The constants below are that table, transcribed:
//
//   L454 "the further it travels the more is lost"     kResistPerTile = 4
//   L454 "the more you transfer the more is lost"      kResistPerTransferPoint = 1
//   L459 a link needs a bridge; the gift is rare       kResistUnbridged = 20
//   L457 many small transfers, dosed                   kOverTimePeriodTicks = 10
//   L454 duration is transfer too, coarser for a hold  kHeldPeriodTicks = 300
//   L98  "even with training it is limited"            kAttributeModifierLimit = 2
//   L567/L516 a docker is not a Luxerne                kVitalityFloor = 1
//
// THE PAIRING RULE, and why it is a canon argument rather than a validation
// nicety. Heat and the body's own tuning are HELD axes: a body carries no
// thermal store and an attribute is recomputed on every read, so there is
// nothing for a one-off to write and the only legal shape is WHILE_ACTIVE. A
// wound is a DELIVERED one: hit points are a written number and nothing reads a
// held vitality row, so the only legal shapes are INSTANT and OVER_TIME. The
// five other pairings would load, resolve, charge their full difficulty, report
// success and change nothing at all. They are refused, by name.
//
// WHAT IS NOT HERE, and the absence IS the design: THE FLAME OF MERCOLAS.
// MAGIC-CANON section 5.5 is binding -- no Flame axis, no Flame row, no Flame
// button, not in this sprint and not by accident in a later one. Everything
// composable here is the SOURCE: the weak, learnable, public-issue-edition
// craft a literate docks laborer picks up off a shallow book, which is exactly
// why it is weak.
//
// NO FLOATS. Every magnitude, duration, range and difficulty is an integer.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/spellbook.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// The skill every crafting on the owner's shelf is cast with, and therefore
/// the one a priest hands over and a forged crafting is measured in. Named once
/// here rather than spelled in three files: content/raws/spells/spells.json
/// authors `"skill": "linkcraft"` on all eleven rows, and asking the raws which
/// skill they need rather than assuming the teacher's own is the difference
/// between teaching from canon and teaching from a guess.
inline constexpr std::string_view kCraftingSkill = "linkcraft";

// ---------------------------------------------------------------------------
// the vocabulary, as the raws spell it
// ---------------------------------------------------------------------------

/// What a component moves. The three axes the owner's spells.json authors over.
enum class EffectKind : std::uint8_t {
    /// Heat. The working currency (L445).
    Temperature = 0,
    /// Hit points. Mending and harming are one operation with a sign (L445/L449).
    Vitality = 1,
    /// The body's own tuning -- WIT, MGT, AGI (L98).
    Attribute = 2,
    Unknown = 3,
};

/// How it moves over time.
enum class EffectMode : std::uint8_t {
    Instant = 0,
    OverTime = 1,
    WhileActive = 2,
    Unknown = 3,
};

/// Where the link goes.
enum class TargetShape : std::uint8_t {
    Self = 0,
    Touch = 1,
    /// The unbridged link. Canon gates it behind "the gift" and no authored row
    /// uses it -- the vocabulary supports it and the public shelf withholds it.
    Ranged = 2,
    Unknown = 3,
};

[[nodiscard]] EffectKind effectKindOf(std::string_view raw) noexcept;
[[nodiscard]] EffectMode effectModeOf(std::string_view raw) noexcept;
[[nodiscard]] TargetShape targetShapeOf(std::string_view raw) noexcept;
[[nodiscard]] std::string_view effectKindKey(EffectKind kind) noexcept;
[[nodiscard]] std::string_view effectModeKey(EffectMode mode) noexcept;
[[nodiscard]] std::string_view targetShapeKey(TargetShape target) noexcept;

// --- the same vocabulary, in words a player is allowed to see ---------------
//
// THE KEYS ABOVE ARE DATA AND MUST NEVER BE DRAWN. They are spells.json's own
// spelling, they are what a forged component is serialised back out as, and
// two of them -- OVER_TIME and WHILE_ACTIVE -- carry an underscore, which the
// 4x6 HUD font has no glyph for. Until this pass ForgeBench::fieldValue handed
// those keys straight to the workbench surface, so a player composing a
// crafting read "SHAPE: OVER TIME" with a hole punched through the middle of
// it, and read an enum identifier either way.
//
// These are the words MAGIC-CANON.md itself uses for the same three axes and
// three time-shapes -- heat, a wound, tuning; a one-off, a trickle, a hold --
// and they are what forgeErrorReason already says out loud to the player two
// lines further down the same panel ("HEAT AND TUNING ARE HELD, NOT
// DELIVERED", "A WOUND IS DELIVERED, NOT HELD"). The bench and the refusal now
// speak the same language.
[[nodiscard]] std::string_view effectKindWord(EffectKind kind) noexcept;
[[nodiscard]] std::string_view effectModeWord(EffectMode mode) noexcept;
[[nodiscard]] std::string_view targetShapeWord(TargetShape target) noexcept;

/// A duration in the clock a player keeps, not in the clock the engine keeps.
/// One tick is one second of simulated time (engine.hpp), and the bench used to
/// print the raw count with a "T" welded to it -- "HOW LONG: 600T". Seconds
/// under a minute, minutes and seconds above it, and "AT ONCE" for no time at
/// all, which is the only thing a one-off can honestly answer.
[[nodiscard]] std::string durationWords(std::int32_t seconds);

/// True for the axes a body HOLDS rather than receives.
[[nodiscard]] bool isHeldAxis(EffectKind kind) noexcept;

// ---------------------------------------------------------------------------
// the cost model -- MAGIC-CANON section 2, transcribed
// ---------------------------------------------------------------------------

inline constexpr std::int32_t kResistPerTile = 4;
inline constexpr std::int32_t kResistPerTransferPoint = 1;
inline constexpr std::int32_t kResistUnbridged = 20;
inline constexpr std::int32_t kOverTimePeriodTicks = 10;
inline constexpr std::int32_t kHeldPeriodTicks = 300;
/// The shortest hold worth authoring: one warmth cadence.
inline constexpr std::int32_t kHeldCadenceTicks = 50;
inline constexpr std::int32_t kAttributeModifierLimit = 2;
inline constexpr std::int32_t kVitalityFloor = 1;
/// How many pieces one crafting may be composed of. Three is what the forge
/// surface can show and price legibly; the authored shelf uses one.
inline constexpr std::int32_t kMaxComponents = 3;

/// What one component actually MOVES, in transfer points. A one-off moves its
/// magnitude once; a trickle pays for every dose it will deliver; a hold pays
/// for every period it keeps the link open. That last one is deliberately
/// coarser, because holding a link is not the same work as pushing fresh
/// transfers through it.
[[nodiscard]] std::int32_t transferPoints(const SpellComponent& component) noexcept;

/// What a whole crafting costs to open, before anybody's skill is consulted.
[[nodiscard]] std::int32_t spellDifficulty(const std::vector<SpellComponent>& components,
                                           TargetShape target, std::int32_t range,
                                           std::int32_t areaRadius) noexcept;
/// The same, for an authored row out of the raws.
[[nodiscard]] std::int32_t spellDifficulty(const Spell& spell) noexcept;

// ---------------------------------------------------------------------------
// what the forge refuses
// ---------------------------------------------------------------------------

enum class ForgeError : std::uint8_t {
    None = 0,
    /// Nothing composed at all.
    NoComponents = 1,
    /// More pieces than one crafting may hold.
    TooManyComponents = 2,
    /// An axis, mode or target shape the vocabulary does not have.
    UnknownAxis = 3,
    UnknownMode = 4,
    UnknownTarget = 5,
    /// A magnitude of zero consumes a slot and moves nothing.
    ZeroMagnitude = 6,
    /// A held axis authored as a one-off. Nothing would read it.
    HeldAxisNeedsHold = 7,
    /// A delivered axis authored as a hold. Nothing would read it either.
    DeliveredAxisCannotHold = 8,
    /// A live attribute row is read by every check in the game (L98).
    AttributeOverLimit = 9,
    /// A lingering component shorter than its own cadence delivers nothing.
    ShorterThanCadence = 10,
    /// SELF reaches nought tiles, TOUCH reaches one, the gift reaches further.
    RangeMismatch = 11,
    /// The student cannot hold it yet -- the literacy tier (L2472).
    BeyondSkill = 12,
    /// The Mission has not opened the workshop to you.
    BeyondRank = 13,
};

[[nodiscard]] std::string_view forgeErrorName(ForgeError error) noexcept;
/// A short line the surface can show. MENU FURNITURE, not anybody's voice --
/// the priest's refusal comes out of the authored forge.refused table.
[[nodiscard]] std::string_view forgeErrorReason(ForgeError error) noexcept;

/// Whether one piece is a legal composition. This is the whole pairing table.
[[nodiscard]] ForgeError componentError(const SpellComponent& component) noexcept;

// ---------------------------------------------------------------------------
// composing
// ---------------------------------------------------------------------------

/// What the player asked the priest for.
struct ForgeRequest {
    std::string displayName;
    TargetShape target = TargetShape::Self;
    std::int32_t range = 0;
    std::int32_t areaRadius = 0;
    std::vector<SpellComponent> components;
};

struct ForgeResult {
    bool ok = false;
    ForgeError error = ForgeError::None;
    /// The finished crafting. Its id is derived from its own shape, so the same
    /// composition forged twice is the same crafting and never a duplicate.
    Spell spell;
    std::int32_t difficulty = 0;
};

/// The deterministic id a composition gets. Pure function of the shape -- no
/// counter, no clock, no draw.
[[nodiscard]] std::string forgedSpellId(const ForgeRequest& request);

/// Composes, prices and validates. `skill` is the student's LINKCRAFT: the
/// difficulty a crafting opens at is what the literacy tier gates, exactly as
/// minLevel gates the authored shelf.
[[nodiscard]] ForgeResult forgeSpell(const ForgeRequest& request, std::int32_t linkcraftLevel);

/// The deepest difficulty a student at this level may compose. Rises with the
/// level and never buys certainty -- the same contract every check in this
/// project carries.
[[nodiscard]] std::int32_t forgeCeilingFor(std::int32_t linkcraftLevel) noexcept;

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

/// How many fields a composition has. Five, and they are canon's own four
/// questions plus the link: what is moved, how it is moved, how much, for how
/// long, and across what.
inline constexpr std::int32_t kForgeFieldCount = 5;

/// The bench a composing conversation runs on.
///
/// Deliberately shaped like the haggle: a small piece of SIMULATION state that
/// the surface reads and the keyboard nudges, never a widget that owns numbers.
/// It is hashed, so a composition half-finished when the gate takes its
/// fingerprint is part of the world both runs have to agree about.
///
/// It does NOT hide the pairing rule. Changing the axis snaps the shape in time
/// to something that axis can legally take, because a bench that opened on an
/// illegal composition would be a bench that starts by lying -- but the shape
/// field is still free, so a player who insists on a held wound is refused,
/// out loud, by the authored forge.refused table. Being told why is the lesson.
struct ForgeBench {
    bool active = false;
    /// Which field the cursor is on.
    std::int32_t field = 0;
    EffectKind axis = EffectKind::Vitality;
    EffectMode mode = EffectMode::Instant;
    std::int32_t magnitude = 1;
    std::int32_t durationTicks = 0;
    TargetShape target = TargetShape::Touch;

    void reset() noexcept;
    /// SELF reaches nought tiles, TOUCH one, the gift two. Derived, never typed.
    [[nodiscard]] std::int32_t range() const noexcept;
    [[nodiscard]] ForgeRequest request() const;
    /// What is wrong with it right now, or None.
    [[nodiscard]] ForgeError error() const noexcept;
    /// What it would cost to open right now.
    [[nodiscard]] std::int32_t difficulty() const noexcept;

    void moveField(std::int32_t delta) noexcept;
    void adjust(std::int32_t delta) noexcept;

    /// What the surface prints. ASCII, upper case, menu furniture.
    [[nodiscard]] std::string fieldLabel(std::int32_t index) const;
    [[nodiscard]] std::string fieldValue(std::int32_t index) const;

    void hashInto(HashSink& sink) const;
};

/// The widest and longest a bench will let a composition go. Not balance for
/// its own sake: the cost model prices every point and every period, so these
/// only stop the arrow keys running off into numbers no ceiling could ever
/// reach.
inline constexpr std::int32_t kBenchMagnitudeLimit = 20;
inline constexpr std::int32_t kBenchDurationLimit = 900;
inline constexpr std::int32_t kBenchDurationStep = 10;

// ---------------------------------------------------------------------------
// what the player knows
// ---------------------------------------------------------------------------

/// Craftings the player has been taught, and craftings the player composed.
///
/// THE S4 GAP ("NOTHING CASTS THESE") IS CLOSED: Tavern::playerCastEquipped
/// (S13) spends these through the linkcraft check, cooldownTicks included,
/// under MAGIC-CANON section 4's rules -- only the played actor casts,
/// nothing dies, the vitality floor is structural -- and the held-effects
/// build made WHILE_ACTIVE self tunings real state. What a FORGED row still
/// cannot do is hold a tuning: this bench has no param field, so a composed
/// ATTRIBUTE component names no string and the cast refuses it out loud
/// rather than guessing a limb. Giving the bench that sixth field is the
/// remaining half of this gap.
///
/// SIMULATION STATE: sorted by id, hashed, and byte-encodable, for the same
/// reason the social ledger is -- a thing the twin-run gate cannot see is a
/// thing the gate does not protect.
class Grimoire {
public:
    /// Records a crafting the priest handed over. False when it is already in.
    bool learn(const Spell& spell);
    /// Records one the player composed. Same storage; `crafted()` counts them
    /// apart, because "he taught me four" and "I made one" are different facts.
    bool inscribe(const Spell& spell);

    [[nodiscard]] bool knows(std::string_view id) const noexcept;
    [[nodiscard]] const Spell* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<Spell>& spells() const noexcept { return spells_; }
    [[nodiscard]] std::size_t size() const noexcept { return spells_.size(); }
    [[nodiscard]] std::int32_t craftedCount() const noexcept { return crafted_; }
    [[nodiscard]] std::int32_t learnedCount() const noexcept {
        return static_cast<std::int32_t>(spells_.size()) - crafted_;
    }

    void hashInto(HashSink& sink) const;

private:
    bool insert(const Spell& spell);
    /// Ascending by id, always.
    std::vector<Spell> spells_;
    std::int32_t crafted_ = 0;
};

}  // namespace granadad::sim
