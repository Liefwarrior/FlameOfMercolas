#pragma once

// What people remember about you, and what you have got good at.
//
// This is the layer every social system after S3 sits on. Three things live
// here and they are deliberately one file, because they are one fact:
//
//   SocialLedger   per-actor memory. What you did to THEM, how many times, and
//                  the standing that follows from it. Plus a ward-wide number
//                  for what everybody else HEARD, because a robbery in a full
//                  taproom is not a private arrangement.
//   SkillTrack     the player's skills, over the vocabulary of
//                  content/raws/skills/skills.json. Use-XP: you get better at
//                  the thing you keep doing (the Morrowind steer), and the
//                  raws own the list of things there are to get better at.
//   Deed           the closed vocabulary of things that move either.
//
// PERSISTENT means: an actor's memory of you lives in SIMULATION STATE for the
// life of the world, is folded into the world hash, and survives the clock
// jumping -- sleeping a night does not wipe the fact that you robbed the
// bartender. Every ledger entry is also encodable to a byte string with a
// round-trip test, which is the seam a save file will use.
//
// VERIFICATION GAP (S3): there is no save FILE. encode()/decode() are proven by
// round trip, but nothing writes one to disk, because S3 has no save system to
// hang it on and inventing one badly is worse than not having it.
//
// NO FLOATS. Disposition is a signed integer in [-100, 100] and every rule that
// moves it is integer arithmetic. No unordered containers: the ledger is a
// vector kept sorted by actor id.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/attributes.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// deeds
// ---------------------------------------------------------------------------

/// Everything the player can do that somebody remembers. Append-only: the
/// ordinal is hashed and written into the ledger's encoding, so an insert in
/// the middle would silently reinterpret every stored deed.
enum class Deed : std::uint8_t {
    /// Spoke to them civilly. Barely counts, and that is the point -- talking
    /// to somebody is not a favour.
    Spoke = 0,
    /// Bought them a drink out of your own purse.
    BoughtDrink = 1,
    /// Took their asking price without a word of argument.
    PaidAsking = 2,
    /// Haggled, and settled somewhere they could live with.
    HaggledFair = 3,
    /// Ground them below what the goods are worth.
    HaggledHard = 4,
    /// Opened with an offer that insults the trade.
    Lowballed = 5,
    /// Walked away from a haggle you started.
    WalkedOut = 6,
    /// Put a hand in their purse. Whether or not they caught you.
    Robbed = 7,
    /// Threw a punch at them.
    Struck = 8,
    /// Drew steel in their house.
    DrewSteel = 9,
    /// Heard something they were carrying and needed to say.
    Listened = 10,
    /// #82. Chose the polite register while hearing them out. Worth a hair
    /// more than plain Listened and held to the exact same talk ceiling --
    /// see record()'s own note on why NEITHER can out-talk it.
    SpokePolitely = 11,
    /// #82. Chose the blunt register while hearing them out. UNLIKE every
    /// other talk-only deed above it, this one is NOT held to the talk
    /// ceiling: a flat tongue can cost you standing you already had, the same
    /// as WalkedOut or Lowballed can -- see record()'s own note.
    SpokeBluntly = 12,
};

inline constexpr std::size_t kDeedCount = 13;

[[nodiscard]] std::string_view deedName(Deed deed) noexcept;

/// How far a deed moves the disposition of the person it was done TO.
[[nodiscard]] std::int32_t deedWeight(Deed deed) noexcept;

/// How far it moves everybody who SAW it. Always smaller, and always the same
/// sign -- witnessing generosity is not as good as receiving it, and witnessing
/// a robbery is nearly as bad as being robbed.
[[nodiscard]] std::int32_t witnessWeight(Deed deed) noexcept;

// ---------------------------------------------------------------------------
// disposition
// ---------------------------------------------------------------------------

inline constexpr std::int32_t kDispositionMin = -100;
inline constexpr std::int32_t kDispositionMax = 100;

/// Where the six authored attitude bands sit on the scale. These are the ONLY
/// thresholds; anything that needs to know how somebody feels asks here.
inline constexpr std::int32_t kHostileAtOrBelow = -40;
inline constexpr std::int32_t kColdAtOrBelow = -10;
inline constexpr std::int32_t kWarmAtOrAbove = 20;
inline constexpr std::int32_t kFriendAtOrAbove = 50;
inline constexpr std::int32_t kKinAtOrAbove = 85;

/// The highest standing that conversation ALONE can reach: one short of WARM.
/// Everything above it costs coin, a favour, or trouble avoided.
inline constexpr std::int32_t kTalkCeiling = kWarmAtOrAbove - 1;

/// What reputationLabel() says when the ward has no opinion of you, which is
/// the state every new game starts in and most games stay in.
///
/// IT IS A NAMED CONSTANT BECAUSE THE HUD HAS TO RECOGNISE IT. A row that says
/// nothing happened does not get a row -- polish-1's rule -- and the way the
/// HUD knows this row says nothing is by comparing against this, not against a
/// second copy of the string typed into the renderer.
inline constexpr std::string_view kReputationUnremarkable = "NOBODY IN PARTICULAR";

[[nodiscard]] Attitude attitudeFor(std::int32_t disposition) noexcept;

/// What an actor remembers about the player. One per actor who has ever
/// noticed them; actors never met are absent and read as a clean slate.
struct Memory {
    std::int32_t actorId = 0;
    std::int32_t disposition = 0;
    /// How many times the player did them a good turn, and a bad one. Kept
    /// separately from the score so "he remembers you helped him once and
    /// robbed him twice" is answerable, which a single number cannot do.
    std::int32_t favours = 0;
    std::int32_t injuries = 0;
    /// Conversations had. Drives which authored row of a table is spoken, so
    /// talking to somebody twice does not get the same sentence twice.
    std::int32_t talks = 0;
    Deed lastDeed = Deed::Spoke;
};

/// Every actor's memory of the player, plus what the ward has heard.
class SocialLedger {
public:
    /// Disposition toward the player. Absent actors read 0 -- a stranger.
    [[nodiscard]] std::int32_t dispositionOf(std::int32_t actorId) const noexcept;
    [[nodiscard]] Attitude attitudeOf(std::int32_t actorId) const noexcept {
        return attitudeFor(dispositionOf(actorId));
    }
    [[nodiscard]] const Memory* memoryOf(std::int32_t actorId) const noexcept;
    [[nodiscard]] const std::vector<Memory>& memories() const noexcept { return memories_; }
    [[nodiscard]] bool knows(std::int32_t actorId) const noexcept {
        return memoryOf(actorId) != nullptr;
    }

    /// Records a deed done TO `actorId`. Returns the new disposition.
    std::int32_t record(std::int32_t actorId, Deed deed);
    /// Records that `actorId` SAW a deed done to somebody else.
    std::int32_t witness(std::int32_t actorId, Deed deed);
    /// Bumps the conversation counter and returns the new count. Which authored
    /// row gets spoken rotates on this.
    std::int32_t noteConversation(std::int32_t actorId);

    /// Nudges a disposition directly, clamped. For the seeded starting standing
    /// an authored relationship implies -- not for gameplay, which goes through
    /// record().
    void seed(std::int32_t actorId, std::int32_t disposition);

    // --- the ward ----------------------------------------------------------

    /// What the district as a whole has heard, [-100, 100]. Moves a fraction of
    /// what a personal deed moves, and only when somebody saw it.
    [[nodiscard]] std::int32_t reputation() const noexcept { return reputation_; }
    [[nodiscard]] Attitude wardAttitude() const noexcept { return attitudeFor(reputation_); }
    /// A short line the HUD can show. Reputation readable in the world rather
    /// than a hidden number.
    [[nodiscard]] std::string_view reputationLabel() const noexcept;

    [[nodiscard]] std::int32_t deedsDone() const noexcept { return deedsDone_; }


    // --- persistence -------------------------------------------------------

    /// A deterministic little-endian byte string. Versioned, and decode()
    /// refuses anything it does not recognise rather than reinterpreting it.
    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes, SocialLedger& out);

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] Memory& entryFor(std::int32_t actorId);

    /// Ascending by actorId, always.
    std::vector<Memory> memories_;
    std::int32_t reputation_ = 0;
    std::int32_t deedsDone_ = 0;
};

// ---------------------------------------------------------------------------
// skills
// ---------------------------------------------------------------------------

/// The skill the ward's haggling actually runs on. STREETWISE is FAVORED in
/// content/raws/skills/skills.json and its own authored mastery lines are about
/// buying at the dawn bell and selling at the dusk one, which is exactly what a
/// haggle is.
inline constexpr std::string_view kHaggleSkill = "streetwise";
/// And the one a hand in somebody's purse runs on.
inline constexpr std::string_view kThieverySkill = "cracksmanship";
/// And the one the roofs run on. SKYRUNNING is the owner's own id in
/// content/raws/skills/skills.json -- it has been in the vocabulary since S1
/// with nothing in the build that used it. Every mantle, leap and landing is a
/// use of it, which is the Morrowind steer applied to a movement mode: you get
/// better at the roofs by being on them.
inline constexpr std::string_view kRoofSkill = "skyrunning";

/// Uses needed to gain one level, at level L. Rises with the level, so the
/// first ten come quickly and the fortieth does not.
[[nodiscard]] std::int32_t usesForLevel(std::int32_t level) noexcept;

// ---------------------------------------------------------------------------
// the difficulty dagger
// ---------------------------------------------------------------------------
//
// Daggerfall's custom path prices advantages and disadvantages in one scalar
// shown as a dagger on a gauge, and the net buys your LEVELING SPEED. This is
// that dial's landing point in the engine: a Q8 advancement multiplier on the
// player's own SkillTrack, folded in exactly where usesForLevel() is
// consulted (SkillTrack::use) and nowhere else. usesForLevel() itself is
// untouched -- every caller that quotes its numbers (lockpick.hpp's 18-probe
// feel pin, the session's own comments) still reads the same flat formula.
//
// Q8, NO FLOATS: 256 is 1.0x. At the NEUTRAL default the scaled charge is
// bit-for-bit usesForLevel() (u * 256 / 256 == u, exactly), so every grind
// this build has ever timed is unchanged until a player actually moves the
// dagger. Above 256 levels come faster (fewer uses per level, floored at 1);
// below 256 they come slower.
//
// PLAYER-SCOPED by construction: the multiplier lives on the SkillTrack
// instance, and the only SkillTrack that levels through use() is the
// player's own (DialogueDirector::skills()). It IS simulation state -- a
// dagger the twin-run gate cannot see is a dagger it does not protect -- so
// hashInto() commits it. That is a deliberate hash-STRUCTURE change, stated
// in its own commit. There is no save frame to bump: SkillTrack has no
// encode()/decode() pair yet (the same S3 verification gap the SocialLedger
// header states), so the day one is written, the multiplier goes into frame
// one.
inline constexpr std::int32_t kDaggerNeutralQ8 = 256;
/// 0.3x, the slowest advancement the custom path may buy...
inline constexpr std::int32_t kDaggerMinQ8 = 77;
/// ...and 3.0x, the fastest. setAdvanceMultiplierQ8 clamps to these.
inline constexpr std::int32_t kDaggerMaxQ8 = 768;

/// The player's skills, over the vocabulary the raws own.
///
/// The LIST of skills is never written here. It is read from
/// content/raws/skills/skills.json, so a skill added to the raws exists here
/// with no code change, and a skill id this code asks for that the raws do not
/// have is a loud nullptr rather than a silent zero.
class SkillTrack {
public:
    struct Entry {
        std::string id;
        std::string displayName;
        std::int32_t level = 0;
        std::int32_t uses = 0;
        /// Off the raw's own governingAttribute column (attributes.hpp).
        /// std::nullopt for the one skill whose raw says "NONE" -- THE FLAME.
        std::optional<AttributeId> governingAttribute;
        /// Off the raw's own aptitudeTier column. Trained when the raw is
        /// silent or says something this loader does not recognise, the same
        /// fallback aptitudeTierFromRaw() itself documents.
        AptitudeTier aptitudeTier = AptitudeTier::Trained;
    };

    /// Reads the skill vocabulary. NEVER throws; a missing file leaves an empty
    /// track and every level reads 0.
    [[nodiscard]] static SkillTrack load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    /// nullptr when the raws do not define this skill.
    [[nodiscard]] const Entry* find(std::string_view id) const noexcept;
    [[nodiscard]] std::int32_t level(std::string_view id) const noexcept;
    [[nodiscard]] bool setLevel(std::string_view id, std::int32_t level) noexcept;
    /// std::nullopt for a skill the raws do not define, same as for THE
    /// FLAME's own authored "NONE" -- the two are indistinguishable on
    /// purpose, since neither has an attribute to report.
    [[nodiscard]] std::optional<AttributeId> governingAttribute(std::string_view id) const noexcept;
    /// Trained for a skill the raws do not define, the same permissive
    /// default level() gives an unknown id (0) rather than a crash.
    [[nodiscard]] AptitudeTier aptitudeTier(std::string_view id) const noexcept;

    /// One use of a skill. Returns true when it levelled. Ignored, returning
    /// false, for a skill the raws do not define.
    bool use(std::string_view id, std::int32_t effort = 1) noexcept;

    // --- the difficulty dagger -------------------------------------------

    [[nodiscard]] std::int32_t advanceMultiplierQ8() const noexcept {
        return advanceMultiplierQ8_;
    }
    /// Clamped to [kDaggerMinQ8, kDaggerMaxQ8]. See the dagger's own header
    /// above usesForLevel() for what this number is and why the default
    /// changes nothing.
    void setAdvanceMultiplierQ8(std::int32_t q8) noexcept;
    /// What one use() actually charges at `level` under the CURRENT dagger:
    /// usesForLevel(level) scaled by the multiplier, floored at 1 so no
    /// setting ever makes a level free. Exposed so a test (or a UI showing
    /// "uses to next level") reads the same arithmetic use() runs rather
    /// than a second copy of it.
    [[nodiscard]] std::int32_t scaledUsesForLevel(std::int32_t level) const noexcept;

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] Entry* findMutable(std::string_view id) noexcept;
    /// Ascending by id.
    std::vector<Entry> entries_;
    /// The dagger. Neutral until the custom path's advantage shop moves it.
    std::int32_t advanceMultiplierQ8_ = kDaggerNeutralQ8;
};

[[nodiscard]] std::filesystem::path skillRawsPath(const std::filesystem::path& contentDir);

}  // namespace granadad::sim
