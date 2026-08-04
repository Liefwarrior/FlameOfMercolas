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
#include <string>
#include <string_view>
#include <vector>

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
};

inline constexpr std::size_t kDeedCount = 11;

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

/// Uses needed to gain one level, at level L. Rises with the level, so the
/// first ten come quickly and the fortieth does not.
[[nodiscard]] std::int32_t usesForLevel(std::int32_t level) noexcept;

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

    /// One use of a skill. Returns true when it levelled. Ignored, returning
    /// false, for a skill the raws do not define.
    bool use(std::string_view id, std::int32_t effort = 1) noexcept;

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] Entry* findMutable(std::string_view id) noexcept;
    /// Ascending by id.
    std::vector<Entry> entries_;
};

[[nodiscard]] std::filesystem::path skillRawsPath(const std::filesystem::path& contentDir);

}  // namespace granadad::sim
