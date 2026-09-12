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

// ---------------------------------------------------------------------------
// #82: the register
// ---------------------------------------------------------------------------

/// The register the PLAYER dials for one exchange -- exactly the three the
/// environment brief named and no others. Unlike Attitude (what somebody
/// remembers feeling about you) this is never stored anywhere: it colours the
/// next thing said and the next thing asked, and it is gone the moment the
/// player dials it away again. See DialogueDirector::setTone.
enum class Tone : std::uint8_t {
    Polite = 0,
    Normal = 1,
    Blunt = 2,
};

/// The authored key suffix a line can be written under: "polite" / "blunt".
/// TONE::NORMAL HAS NO SUFFIX AND IS NEVER PASSED HERE -- the untagged line is
/// already the normal register, which is every line barks.json has ever
/// authored, so nothing tone-aware widens a chain at all until the player
/// actually reaches for POLITE or BLUNT. Asking for Tone::Normal's key is a
/// caller error and returns empty rather than inventing a suffix nobody
/// authored under.
[[nodiscard]] std::string_view toneKey(Tone tone) noexcept;
/// What the HUD calls it.
[[nodiscard]] std::string_view toneName(Tone tone) noexcept;

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

/// The owner's own table file. Always loaded FIRST and always wins a duplicate
/// key -- see barkRawsFiles().
[[nodiscard]] std::filesystem::path barkRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path barkRawsDir(const std::filesystem::path& contentDir);

/// Every *.json in content/raws/barks, the owner's first and the rest in sorted
/// order. S4 added a SECOND table file rather than editing the owner's 59KB of
/// canon; this is the seam that makes that legal, and the ordering is the rule
/// that makes it safe.
[[nodiscard]] std::vector<std::filesystem::path> barkRawsFiles(
    const std::filesystem::path& contentDir);

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

    /// VOICE LANE. The SAME row pick over a CELL: the key's own rows joined
    /// with its numbered siblings' (`<key>.2`, then `<key>.3`, ...) and
    /// rotated across the union. This is how an authored greet cell gets
    /// breadth without the owner's barks.json being edited -- the canon rows
    /// stay in his file and win their key, the extra rows sit in a
    /// supplementary file under the sibling key, and the chain still lands
    /// on the canon key (greetingKey() never names a sibling). A key with no
    /// sibling authored reads exactly as line() does, row for row, so every
    /// caller that predates this draws what it always drew.
    [[nodiscard]] std::string_view cellLine(std::string_view key,
                                            std::int32_t index) const noexcept;
    /// How many rows the cell holds, siblings included. Zero for an
    /// unauthored key. Public so the suite can pin the rule the greeting's
    /// rotation depends on: a cell must never total seven rows.
    [[nodiscard]] std::int32_t cellRowCount(std::string_view key) const noexcept;

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

/// #82. The "TELL ME ABOUT" fallback chain: the SAME shape greetChain already
/// uses -- family, then family+attitude, then family+attitude+band, most
/// specific first -- with a topic id folded in front of it and a bare
/// `<prefix>.<id>` as the one rung that is NEVER optional. A location and a
/// thing both want "the same fact in a different mouth", which is exactly
/// what a greeting already is; this is that mechanism, reused rather than
/// reinvented, for two new axes of "ask about something" content.
[[nodiscard]] std::vector<std::string> topicChain(std::string_view prefix, std::string_view id,
                                                   JobFamily family, Attitude attitude,
                                                   TimeBand band);

/// VOICE LANE. The attitude keys a chain tries for one attitude, most
/// specific first. Neutral is the bare cell and names nothing; the tiers
/// above and below it borrow the NEAREST authored neighbour and never cross
/// the middle: kin falls to friend and then to warm, hostile falls to cold,
/// and a cold cell never borrows a warm row. Exposed so a case can assert the
/// ladder itself rather than only a line that came out of it.
[[nodiscard]] std::vector<std::string_view> attitudeFallbacks(Attitude attitude);

/// VOICE LANE. The chain for a line said WITHOUT the talk key -- a body
/// passing you (pass), a goodbye (farewell), a snatch between two bodies
/// (overheard), a witness's word on a deed (react.<family>.<deed> is built
/// by the caller as its own prefix). Shape, most specific first:
///
///     <prefix>.<type>.<attitude>      the trade's own cell at this attitude
///     <prefix>.<family>.<attitude>    the family's
///     ... the same two rungs for each fallback attitude in the tier ...
///     <prefix>.<attitude>             a generic row at this attitude, and
///                                     the fallback tiers after it
///     <prefix>.<type>                 the trade's bare cell (neutral)
///     <prefix>.<family>               the family's
///     <prefix>                        the floor every prefix authors
///
/// Every attitude rung sits above every bare one: what they think of you
/// outranks the flavour of their trade, and the bare cells ARE the neutral
/// register (Neutral names no rung at all).
///
/// `typeKey` is the ward trade's own key (wardVoiceKey) or empty for a body
/// with only a family -- the Gilded Gull's roster -- and an empty type skips
/// its rungs rather than building a key nobody authored under.
[[nodiscard]] std::vector<std::string> voiceChain(std::string_view prefix, std::string_view typeKey,
                                                  JobFamily family, Attitude attitude);

/// VOICE LANE. The goodbye: voiceChain("farewell", ...), named because two
/// call sites (the LEAVE topic and the walk-off) must agree on it.
[[nodiscard]] std::vector<std::string> farewellChain(std::string_view typeKey, JobFamily family,
                                                     Attitude attitude);

/// VOICE LANE. WHAT THE WARD SAYS in a trade's own mouth: rumor.<family>
/// first, then the owner's generic gossip table as the floor it always was.
[[nodiscard]] std::vector<std::string> rumorChain(JobFamily family);

/// Fold to the ASCII the 4x6 HUD font can actually draw. The authored JSON
/// carries mojibake em-dashes (a UTF-8 em-dash re-encoded through cp1252), and
/// every byte of one would otherwise render as a blank column. A run of
/// non-ASCII collapses to a single '-', which is what those bytes MEANT.
[[nodiscard]] std::string foldToAscii(std::string_view text);

}  // namespace granadad::sim
