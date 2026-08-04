#pragma once

// The guilds and factions of the ward, the ladders inside them, and what a rung
// is worth.
//
// TWO FILES, ONE FACT. content/raws/factions/factions.json is the owner's
// authored registry -- five factions and the job ids that belong to each -- and
// it is READ-ONLY canon. content/raws/factions/ranks.json is an S4 addition
// that hangs a LADDER off each of those five ids and adds nothing to the
// registry itself. A ladder naming a faction the owner's file does not have is
// refused at load, by name, so this pair cannot drift into a sixth faction by
// typo.
//
// WHAT A FACTION IS, MECHANICALLY. Four numbers per faction and not one more:
//
//   membership   are you on the roll at all
//   rank         which rung, 0 for none. EARNED: every rung, including the
//                first, costs standing with that faction AND a level in that
//                faction's own skill. You cannot talk your way up a ladder
//                that is measured in what your hands can do.
//   standing     what that faction thinks of you, [-100, 100]. Moved by deeds
//                done to its members, through exactly the Deed vocabulary the
//                social ledger already speaks.
//   influence    what that faction is worth IN THE WARD, [0, 100]. This is the
//                one that reaches out of the character sheet and changes the
//                world: it moves what a member-run counter charges, and it
//                moves how much rope the houses give a stranger.
//
// THE MIRROR. factions.json's own note says the Skyrunners are "the mirror
// ledger of every justice event" and warm to whoever the Watch corrects. So
// rivalry is not a flavour string: influence pushed toward one declared rival
// is pulled off the other, and standing gained with one is half-lost with the
// other. The Temple has no rival, deliberately -- DECISIONS.md's tenure ruling
// says the Church "never opposes anyone openly".
//
// WHAT A RUNG UNLOCKS is a TOKEN read by name, never by rank number, so
// re-ordering a ladder cannot silently move what a rung opens.
//
// NO FLOATS. Every number here is a clamped integer. NO UNORDERED CONTAINERS:
// factions are a vector in the raws' own sorted-key order and everything is
// indexed by position in it, which is also the order factions.json's own note
// specifies (dockhands=0, merchants=1, skyrunners=2, temple=3, watch=4).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/social.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

/// One faction, straight out of the owner's registry.
struct Faction {
    std::string id;
    std::string displayName;
    /// content/raws/jobs/jobs.json ids. Ascending.
    std::vector<std::string> memberJobs;
};

/// One rung of one ladder.
struct FactionRank {
    std::string title;
    /// Standing with this faction the rung costs.
    std::int32_t standing = 0;
    /// Level in the ladder's own skill the rung costs.
    std::int32_t skillLevel = 0;
    /// What the rung opens, by name. Ascending, deduplicated.
    std::vector<std::string> unlocks;
};

/// One faction's own ranking system.
struct FactionLadder {
    /// The skill the rungs are measured in. A skills.json id.
    std::string skill;
    std::int32_t baseInfluence = 0;
    /// Faction ids this one is the mirror of. Ascending.
    std::vector<std::string> rivals;
    /// Ascending by rung. Index 0 IS rank 1; rank 0 is "not a member".
    std::vector<FactionRank> ranks;
};

/// Everything the raws say about who there is to join and what it costs.
class FactionRegistry {
public:
    /// Reads both files. NEVER throws: a missing registry leaves an empty one
    /// and every query says "no such faction", which is the same rule the
    /// barks, the notables and the spells follow -- the game must still boot
    /// while a content file is being edited.
    [[nodiscard]] static FactionRegistry load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !factions_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return factions_.size(); }
    [[nodiscard]] const std::vector<Faction>& factions() const noexcept { return factions_; }

    /// Position in the sorted registry, or -1. That position is the id every
    /// other call here takes, and it is stable because the raws are sorted.
    [[nodiscard]] std::int32_t indexOf(std::string_view id) const noexcept;
    [[nodiscard]] const Faction* find(std::string_view id) const noexcept;
    [[nodiscard]] const Faction* at(std::int32_t index) const noexcept;
    /// nullptr when the ranks file does not ladder this faction.
    [[nodiscard]] const FactionLadder* ladder(std::int32_t index) const noexcept;

    /// The faction that claims jobs beginning `prefix` + '.', or -1.
    ///
    /// This is how an actor gets a faction WITHOUT anybody writing a second
    /// table: the tavern knows a bartender is `trade`, factions.json says
    /// trade.stallkeep belongs to the merchants, and the answer falls out of
    /// the owner's file rather than out of a switch in a .cpp.
    [[nodiscard]] std::int32_t factionForJobPrefix(std::string_view prefix) const noexcept;

    /// Indices of the declared rivals of `index`. Ascending.
    [[nodiscard]] std::vector<std::int32_t> rivalsOf(std::int32_t index) const;

private:
    /// Ascending by id. Never reordered after load.
    std::vector<Faction> factions_;
    /// Parallel to factions_. An empty ladder means the ranks file is silent.
    std::vector<FactionLadder> ladders_;
};

[[nodiscard]] std::filesystem::path factionRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path factionRankRawsPath(const std::filesystem::path& contentDir);

// ---------------------------------------------------------------------------
// where the player stands
// ---------------------------------------------------------------------------

inline constexpr std::int32_t kFactionStandingMin = -100;
inline constexpr std::int32_t kFactionStandingMax = 100;
inline constexpr std::int32_t kInfluenceMin = 0;
inline constexpr std::int32_t kInfluenceMax = 100;

/// How much of the ward's regard a promotion moves toward a faction. Small on
/// purpose: one player climbing one ladder is not a revolution, and five rungs
/// of it is still only a quarter of the scale.
inline constexpr std::int32_t kInfluencePerRank = 5;

/// How far a deed done to a member moves that faction's opinion of you, as a
/// share of what it moved the PERSON. A guild hears about it secondhand.
[[nodiscard]] std::int32_t factionDeedWeight(Deed deed) noexcept;

/// What the player is to one faction.
struct FactionStanding {
    bool member = false;
    /// 0 when not on a rung. 1 is the first.
    std::int32_t rank = 0;
    std::int32_t standing = 0;
    std::int32_t influence = 0;
};

/// Why a request to sign on or to climb was refused.
enum class LadderResult : std::uint8_t {
    Granted = 0,
    /// The raws do not ladder this faction, or there is no such faction.
    NoLadder = 1,
    /// Already on the roll (join), or already on the top rung (advance).
    AlreadyThere = 2,
    /// Not enough standing with them.
    NeedsStanding = 3,
    /// The rung is measured in a skill, and yours is short.
    NeedsSkill = 4,
    /// Asked to climb without being on the roll at all.
    NotAMember = 5,
};

[[nodiscard]] std::string_view ladderResultName(LadderResult result) noexcept;

/// The player's membership, rank, standing and each faction's influence.
///
/// SIMULATION STATE, hashed and byte-encodable for exactly the reason the
/// social ledger is: a relationship the twin-run gate cannot see is one the
/// gate does not protect.
class FactionLedger {
public:
    /// Points the ledger at a registry and sizes the per-faction rows.
    ///
    /// SHARED, not borrowed, and the difference is a bug that would have been
    /// very hard to find: the director that owns both is built by a factory
    /// that returns BY VALUE, so a raw pointer taken to a member of the local
    /// would dangle the moment the return was a move rather than an elision.
    /// A shared owner survives the copy and every copy agrees about the raws.
    void attach(std::shared_ptr<const FactionRegistry> registry);
    [[nodiscard]] const FactionRegistry* registry() const noexcept { return registry_.get(); }
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    [[nodiscard]] bool isMember(std::int32_t index) const noexcept;
    [[nodiscard]] std::int32_t rank(std::int32_t index) const noexcept;
    [[nodiscard]] std::int32_t standing(std::int32_t index) const noexcept;
    [[nodiscard]] std::int32_t influence(std::int32_t index) const noexcept;
    /// The rung's authored title, or "" when not on one.
    [[nodiscard]] std::string_view rankTitle(std::int32_t index) const noexcept;
    /// The next rung's requirements, or nullptr at the top of the ladder.
    [[nodiscard]] const FactionRank* nextRung(std::int32_t index) const noexcept;
    /// True when any rung at or below the current one lists `token`.
    [[nodiscard]] bool unlocked(std::int32_t index, std::string_view token) const noexcept;

    /// The highest rank the player holds anywhere, and where. -1 when nowhere.
    [[nodiscard]] std::int32_t highestRankedFaction() const noexcept;

    // --- moving the numbers -------------------------------------------------

    /// Adds standing, clamped, and halves the OPPOSITE onto every declared
    /// rival -- the mirror ledger, in one line.
    void addStanding(std::int32_t index, std::int32_t delta);
    /// Records a deed done to a member of `index`.
    void recordDeed(std::int32_t index, Deed deed);
    /// Moves influence, clamped, and takes the same off every declared rival.
    void shiftInfluence(std::int32_t index, std::int32_t delta);

    /// Signs on. Grants rank 1 when its requirements are met, and refuses
    /// otherwise -- EVERY rung is earned, including the first.
    LadderResult join(std::int32_t index, const SkillTrack& skills);
    /// Climbs one rung.
    LadderResult advance(std::int32_t index, const SkillTrack& skills);
    /// What join()/advance() would answer, without doing it.
    [[nodiscard]] LadderResult check(std::int32_t index, const SkillTrack& skills) const;

    // --- persistence --------------------------------------------------------

    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes, FactionLedger& out);

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] bool inRange(std::int32_t index) const noexcept;
    [[nodiscard]] LadderResult checkRung(std::int32_t index, std::int32_t rung,
                                         const SkillTrack& skills) const;

    std::shared_ptr<const FactionRegistry> registry_;
    /// Parallel to the registry. Dense, so iteration order is the raws' order.
    std::vector<FactionStanding> rows_;
};

// ---------------------------------------------------------------------------
// what influence does to a price
// ---------------------------------------------------------------------------

/// The percentage a counter run by a member of `index` moves for this player.
///
/// THIS IS THE WHOLE "REAL IMPACT ON THE WORLD VIA TRADE" REQUIREMENT, and it
/// is deliberately three small terms rather than one big one:
///
///   the member's rate   a guild that has unlocked 'price' for you sells to you
///                       as one of its own, and deeper rungs sell cheaper
///   the guild's weight  a faction with influence over the ward prices
///                       STRANGERS up. Weak guilds cannot.
///   plain standing      what they think of you, always worth a little
///
/// Clamped, so no ladder ever turns a purchase into a gift.
[[nodiscard]] std::int32_t guildPricePercent(const FactionLedger& ledger,
                                             std::int32_t index) noexcept;

}  // namespace granadad::sim
