#pragma once

// A questline: authored stages, and where the player is in them.
//
// WHAT THIS IS FOR. S3 gave the ward a voice and a memory. It had no ARC: every
// conversation was a leaf, and nothing you did in one changed what the next one
// offered. A faction you can join without a reason to keep coming back is a
// number on a sheet. This is the spine the Priest of the Flame line hangs on,
// and it is deliberately general -- the Skyrunner line and the Watch line drop
// in as raws files with no code here.
//
// TWO FILES IN ONE DIRECTORY, TOLD APART BY SHAPE, NOT BY NAME.
// content/raws/quests/quests.json is the owner's authored canon and is not
// touched: its vanished-clerk line advances on enter_zone / search / item
// conditions over a bank hall and a locked drawer, none of which this build
// simulates. This loader reads EVERY *.json in that directory and accepts only
// documents with a top-level `stages` array -- so the owner's file is skipped
// because of what it IS, not because of what it is called, and the day somebody
// writes a stages-shaped line into a third file it loads with no code change.
//
// THE STAGE VOCABULARY IS CODE; EVERYTHING ELSE IS DATA. Five kinds, and every
// one of them is a condition this build can actually evaluate in the room it
// actually simulates:
//
//   oath    accept at the named party. This is what makes you a member.
//   alms    do a counted deed N times, then report back to the party.
//   talk    stand in front of the named party and raise it.
//   teach   be taught a crafting out of content/raws/spells/spells.json.
//   forge   compose one of your own.
//
// Authoring a stage against a condition nothing can evaluate would be authoring
// a quest that cannot be finished, which is worse than authoring none.
//
// NO STAGE IS GATED BY A ROLL. DECISIONS.md's S3 ruling is binding: a stage
// advances because you found the right person, never because a check passed.
// The one gate anywhere near this is what the priest will TEACH, and that is
// the authored minLevel in the spell raws -- the literacy tier, not a die.
//
// NO FLOATS. NO UNORDERED CONTAINERS: lines and progress rows are vectors
// sorted by id.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

enum class StageKind : std::uint8_t {
    Oath = 0,
    Alms = 1,
    Talk = 2,
    Teach = 3,
    Forge = 4,
    Unknown = 5,
    /// S5. A counted stage over a NAMED tally, and the same machinery `alms`
    /// already was: `alms` is exactly `tally` with its counter fixed at
    /// "drinks", and it keeps its own word because the Mission's own line calls
    /// it that. The Skyrunners count lifts, cracks, runs, fences, leans and
    /// roofs, and not one line of C++ knows which -- the stage names its
    /// counter, the crime vocabulary names the same string, and they meet in
    /// DialogueDirector::noteTally.
    Tally = 6,
};

[[nodiscard]] StageKind stageKindOf(std::string_view raw) noexcept;
[[nodiscard]] std::string_view stageKindName(StageKind kind) noexcept;

struct QuestStage {
    std::string key;
    StageKind kind = StageKind::Unknown;
    /// A content/raws/names/notables.json id. Who you have to be standing in
    /// front of.
    std::string party;
    /// What the topic list calls it. ASCII, upper case, menu furniture.
    std::string label;
    /// What the journal says you should do.
    std::string objective;
    /// What the journal says you did.
    std::string log;
    /// The authored barks table the party speaks this stage from.
    std::string barkKey;
    /// How many times, for a counted stage.
    std::int32_t count = 0;
    /// WHAT is counted, for a counted stage. Empty on an `alms` stage means
    /// "drinks", which is what the Mission's own line has always counted.
    std::string counter;
    /// The rung this stage puts you on, or 0.
    std::int32_t grantsRank = 0;
    /// Standing with the line's faction the stage is worth.
    std::int32_t standing = 0;
    bool terminal = false;
};

struct Questline {
    std::string id;
    std::string title;
    /// A content/raws/factions/factions.json id, or empty.
    std::string faction;
    /// The notable who starts it.
    std::string giver;
    std::vector<QuestStage> stages;
};

/// Every authored line this build can run.
class QuestBook {
public:
    /// NEVER throws. A missing directory leaves an empty book and every query
    /// says "no such line", which is the rule every other raws loader here
    /// follows.
    [[nodiscard]] static QuestBook load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !lines_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return lines_.size(); }
    [[nodiscard]] const std::vector<Questline>& lines() const noexcept { return lines_; }
    [[nodiscard]] const Questline* find(std::string_view id) const noexcept;

private:
    /// Ascending by id. Never reordered after load.
    std::vector<Questline> lines_;
};

[[nodiscard]] std::filesystem::path questRawsDir(const std::filesystem::path& contentDir);

// ---------------------------------------------------------------------------
// where the player is
// ---------------------------------------------------------------------------

/// One line's progress.
struct QuestProgress {
    std::string questId;
    /// Index of the stage now wanted. Never past the last one.
    std::int32_t stage = 0;
    /// For a counted stage.
    std::int32_t counter = 0;
    bool started = false;
    bool done = false;
};

/// What the player has started, where they are, and what the journal says.
///
/// SIMULATION STATE and hashed. There is deliberately NO byte codec here, and
/// that is a disclosure rather than an oversight: a save only needs
/// (questId, stage, counter, flags), which the ledger codecs already show how
/// to write, and the log lines are re-derivable from the raws given those four.
/// Writing a second encoding for strings that are already in a content file
/// would be inventing a format nothing reads.
class QuestJournal {
public:
    void start(std::string_view questId);
    [[nodiscard]] bool started(std::string_view questId) const noexcept;
    [[nodiscard]] bool done(std::string_view questId) const noexcept;
    [[nodiscard]] std::int32_t stage(std::string_view questId) const noexcept;
    [[nodiscard]] std::int32_t counter(std::string_view questId) const noexcept;
    /// How many stages of this line are behind the player.
    [[nodiscard]] std::int32_t stagesDone(std::string_view questId) const noexcept;

    void bumpCounter(std::string_view questId, std::int32_t delta);

    /// Moves one stage on, writes the stage's authored log line into the
    /// journal, and marks the line done when the stage was terminal or the last
    /// one. Returns false when there was nothing to advance.
    bool advance(const Questline& line);

    [[nodiscard]] const std::vector<QuestProgress>& progress() const noexcept { return rows_; }
    /// The authored log lines, in the order they were earned.
    [[nodiscard]] const std::vector<std::string>& log() const noexcept { return log_; }

    void hashInto(HashSink& sink) const;

private:
    [[nodiscard]] const QuestProgress* rowFor(std::string_view questId) const noexcept;
    [[nodiscard]] QuestProgress& entryFor(std::string_view questId);

    /// Ascending by questId, always.
    std::vector<QuestProgress> rows_;
    std::vector<std::string> log_;
};

}  // namespace granadad::sim
