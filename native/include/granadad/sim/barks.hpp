#pragma once

// The authored voice of the Docks, read out of the owner's raws.
//
// content/raws/barks/barks.json is 59KB of CANON: 210 tables covering nine job
// families x six attitudes with time-of-day refinements, six mood overrides,
// a personal table for every one of the Forty Notables, one gossip table per
// authored micro-history, the vanished-clerk quest beats, and mastery lines per
// skill. NOTHING in this file writes a line of dialogue. It reads them, and it
// resolves which authored key applies.
//
// THE KEY VOCABULARY, as the file itself is authored:
//
//     greet.<family>                          the bare family greeting
//     greet.<family>.<attitude>               kin|friend|warm|neutral|cold|hostile
//     greet.<family>.<attitude>.<band>        morning|day|evening|night
//     personal / personal.<notableId>
//     gossip  / gossip.<historyId>
//     quest.<questId> / quest.<questId>.<beat>.<notableId>
//     mastery.<skill>.<novice|adept|master>
//     mood.<state>
//
// Not every combination is authored, deliberately -- the file has
// greet.watch.cold.<band> but not greet.watch.warm.<band>, because a cold Watch
// at four in the morning is a different sentence and a warm one is not. So
// lookup is a FALLBACK CHAIN, most specific first, and `resolve()` is the one
// place that chain exists.
//
// NO FLOATS, no unordered containers: tables are a vector sorted by key and
// searched by binary search, so iteration order is the file's own and identical
// on every machine.
//
// SELECTION IS DRAW-FREE. Which row of a table is spoken is a pure function of
// (actor id, how many times you have spoken to them). No RNG stream is touched,
// so a conversation cannot shift the simulation's draw sequence -- which is
// what would happen if talking to somebody consumed a roll and the twin-run
// gate then compared a run where the player talked against one where they did
// not.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the vocabulary
// ---------------------------------------------------------------------------

/// How somebody feels about the player. These six are not invented: they are
/// exactly the six suffixes content/raws/barks/barks.json authors greetings
/// over, and barkTierCoverage() asserts every family has all six.
enum class Attitude : std::uint8_t {
    Hostile = 0,
    Cold = 1,
    Neutral = 2,
    Warm = 3,
    Friend = 4,
    Kin = 5,
};

/// The authored key suffix: "hostile", "cold", ...
[[nodiscard]] std::string_view attitudeKey(Attitude attitude) noexcept;
/// What the HUD calls it.
[[nodiscard]] std::string_view attitudeName(Attitude attitude) noexcept;

/// The nine presented job families the greet tables are authored over.
enum class JobFamily : std::uint8_t {
    Serf = 0,
    Wastrel = 1,
    Watch = 2,
    Clergy = 3,
    Trade = 4,
    Maritime = 5,
    Husbandry = 6,
    Beast = 7,
    FlameOfMerc = 8,
};

inline constexpr std::size_t kJobFamilyCount = 9;

[[nodiscard]] std::string_view jobFamilyKey(JobFamily family) noexcept;

/// The four time bands the greet tables refine over.
enum class TimeBand : std::uint8_t {
    Morning = 0,
    Day = 1,
    Evening = 2,
    Night = 3,
};

[[nodiscard]] std::string_view timeBandKey(TimeBand band) noexcept;

/// Which band a second of the day falls in. Morning is the dawn muster through
/// mid-morning, day is the working shift, evening is the pay-out and the
/// tavern hours, night is the small hours -- the wage loop DOCKS-GAZETTEER
/// section 4 describes, not an arbitrary quartering of the clock.
[[nodiscard]] TimeBand timeBandOf(std::int32_t secondOfDay) noexcept;

// ---------------------------------------------------------------------------
// the tables
// ---------------------------------------------------------------------------

[[nodiscard]] std::filesystem::path barkRawsPath(const std::filesystem::path& contentDir);

/// Every authored table, keyed and searchable. Loaded once and shared.
class BarkTables {
public:
    struct Table {
        std::string key;
        std::vector<std::string> rows;
    };

    /// Reads content/raws/barks/barks.json. NEVER throws: a missing or
    /// malformed file leaves an empty set and every lookup returns nothing,
    /// which is the same rule the spell raws follow -- the game must still boot
    /// when a content file is being edited.
    [[nodiscard]] static BarkTables load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !tables_.empty(); }
    [[nodiscard]] std::size_t tableCount() const noexcept { return tables_.size(); }
    [[nodiscard]] std::size_t rowCount() const noexcept;
    [[nodiscard]] const std::vector<Table>& tables() const noexcept { return tables_; }

    /// The rows of one authored key, or nullptr. Binary search over a vector
    /// sorted by key -- never a hash map, see the header.
    [[nodiscard]] const std::vector<std::string>* rows(std::string_view key) const noexcept;
    [[nodiscard]] bool has(std::string_view key) const noexcept { return rows(key) != nullptr; }

    /// One row of an authored table, rotated by `index`. Empty when the key is
    /// not authored. `index` is taken modulo the row count and is made
    /// non-negative first, so a negative counter cannot index out of the table.
    [[nodiscard]] std::string_view line(std::string_view key, std::int32_t index) const noexcept;

    /// The FIRST key in `candidates` that is authored, or empty. This is the
    /// fallback chain, and it is the only place it exists.
    [[nodiscard]] std::string_view resolve(
        const std::vector<std::string>& candidates) const noexcept;

private:
    /// Sorted by key. Never reordered after load.
    std::vector<Table> tables_;
};

// ---------------------------------------------------------------------------
// key building
// ---------------------------------------------------------------------------

/// The greeting fallback chain for a family at an attitude at an hour, most
/// specific first. Exposed rather than hidden inside the selector so a test can
/// assert the chain itself rather than only its result.
[[nodiscard]] std::vector<std::string> greetChain(JobFamily family, Attitude attitude,
                                                  TimeBand band);

/// The personal chain for a notable: their own table, then the generic one.
[[nodiscard]] std::vector<std::string> personalChain(std::string_view notableId);

/// The gossip chain for one authored micro-history.
[[nodiscard]] std::vector<std::string> gossipChain(std::string_view historyId);

/// mastery.<skill>.<novice|adept|master> for a skill level, or an empty chain
/// when the level is below the novice band. The three bands are the authored
/// vocabulary; the thresholds are ours.
[[nodiscard]] std::vector<std::string> masteryChain(std::string_view skillId,
                                                    std::int32_t level);

/// Fold to the ASCII the 4x6 HUD font can actually draw. The authored JSON
/// carries mojibake em-dashes (a UTF-8 em-dash re-encoded through cp1252), and
/// every byte of one would otherwise render as a blank column. A run of
/// non-ASCII collapses to a single '-', which is what those bytes MEANT.
[[nodiscard]] std::string foldToAscii(std::string_view text);

}  // namespace granadad::sim
