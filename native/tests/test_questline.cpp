// Questlines: the raws, and the journal that walks them.
//
// The load rule is the interesting part and it deserves a case of its own.
// content/raws/quests/ now holds two files with two different schemas. The
// owner's quests.json describes the vanished clerk in conditions this build
// cannot evaluate -- enter_zone, search, a locked drawer in a bank hall -- and
// must be skipped. It is skipped because of its SHAPE, not its filename, so
// nothing here has a list of files it knows about.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const QuestBook& book() {
    static const QuestBook loaded = QuestBook::load(content::contentDir());
    return loaded;
}

}  // namespace

TEST_CASE("a quest file is read for its shape, and the owner's is left alone") {
    REQUIRE(book().loaded());
    // Exactly the lines this build can actually finish.
    CHECK(book().size() == 2);
    CHECK(book().find("flame-disciple") != nullptr);
    CHECK(book().find("skyrunner-tenant") != nullptr);
    // The owner's own file is in the same directory and is NOT loaded, because
    // it has no top-level `stages` array. Skipping it by name would have been a
    // list of filenames somebody has to maintain.
    CHECK(book().find("quests") == nullptr);
    CHECK(book().find("vanished-clerk") == nullptr);
}

TEST_CASE("the Priest of the Flame line is authored end to end") {
    const Questline* line = book().find("flame-disciple");
    REQUIRE(line != nullptr);
    CHECK(line->faction == "temple");
    CHECK(line->giver == "maell");
    REQUIRE(line->stages.size() == 6);

    // Every stage has a party, a label, an objective, a log line and an
    // authored table to speak from -- a stage missing any of those is a stage
    // the player walks into and gets nothing from.
    const BarkTables tables = BarkTables::load(content::contentDir());
    REQUIRE(tables.loaded());
    const NotableRegistry notables = NotableRegistry::load(content::contentDir());
    REQUIRE(notables.loaded());
    for (const QuestStage& stage : line->stages) {
        CAPTURE(stage.key);
        CHECK(stage.kind != StageKind::Unknown);
        CHECK_FALSE(stage.party.empty());
        CHECK_FALSE(stage.label.empty());
        CHECK_FALSE(stage.objective.empty());
        CHECK_FALSE(stage.log.empty());
        CHECK_FALSE(stage.barkKey.empty());
        // The party is one of the Forty, by id, and not a name somebody typed.
        CHECK(notables.find(stage.party) != nullptr);
        // And the table it speaks from is really authored.
        CHECK(tables.has(stage.barkKey));
    }

    // The arc: an oath that makes you a member, a counted deed, two
    // conversations in two different places, a teaching and a composition.
    CHECK(line->stages[0].kind == StageKind::Oath);
    CHECK(line->stages[0].grantsRank == 1);
    CHECK(line->stages[1].kind == StageKind::Alms);
    CHECK(line->stages[1].count == 3);
    CHECK(line->stages[2].kind == StageKind::Talk);
    // The third stage is somewhere ELSE, and that is the design: the
    // investigation is knowing where to ask.
    CHECK(line->stages[2].party != line->stages[1].party);
    CHECK(line->stages[3].kind == StageKind::Talk);
    CHECK(line->stages[4].kind == StageKind::Teach);
    CHECK(line->stages[5].kind == StageKind::Forge);
    CHECK(line->stages[5].terminal);

    // Only the oath hands over a rung. Everything after it is standing, and
    // standing has to be spent on the ladder deliberately.
    std::int32_t granted = 0;
    for (const QuestStage& stage : line->stages) {
        granted += stage.grantsRank;
        CHECK(stage.standing > 0);
    }
    CHECK(granted == 1);
}

TEST_CASE("every line's faction and giver are real, for every line the book has") {
    // completeStage (dialogue.cpp) resolves `line.faction` through
    // FactionRegistry::indexOf and hands the -1 a dangling name would produce
    // straight to FactionLedger::addStanding/advance, which treats an
    // out-of-range index as a silent no-op (inRange() guards every one of
    // them) rather than a refusal. The two authored lines both happen to name
    // real factions and real givers today; test_questline.cpp and
    // test_crime.cpp each pin ONE line's ONE field to a literal string, which
    // proves that line's own value and nothing about the other line or the
    // other field. This is the generic version: every line the book actually
    // loaded, checked against the same registries the game reads at runtime.
    const FactionRegistry factions = FactionRegistry::load(content::contentDir());
    REQUIRE(factions.size() > 0);
    const NotableRegistry notables = NotableRegistry::load(content::contentDir());
    REQUIRE(notables.loaded());
    REQUIRE(book().size() > 0);
    for (const Questline& line : book().lines()) {
        CAPTURE(line.id);
        CHECK(factions.indexOf(line.faction) >= 0);
        CHECK(notables.find(line.giver) != nullptr);
    }
}

TEST_CASE("the journal walks the stages, writes the log, and stops at the end") {
    const Questline* line = book().find("flame-disciple");
    REQUIRE(line != nullptr);
    QuestJournal journal;

    CHECK_FALSE(journal.started(line->id));
    CHECK_FALSE(journal.done(line->id));
    CHECK(journal.stage(line->id) == 0);
    CHECK(journal.stagesDone(line->id) == 0);
    // A line nobody has heard of reads as a clean slate rather than a crash.
    CHECK(journal.stage("no-such-line") == 0);
    CHECK_FALSE(journal.done("no-such-line"));

    journal.start(line->id);
    CHECK(journal.started(line->id));

    for (std::size_t i = 0; i < line->stages.size(); ++i) {
        CHECK(journal.stage(line->id) == static_cast<std::int32_t>(i));
        CHECK(journal.advance(*line));
        CHECK(journal.log().size() == i + 1);
    }
    CHECK(journal.done(line->id));
    CHECK(journal.stagesDone(line->id) == static_cast<std::int32_t>(line->stages.size()));
    // A finished line does not advance again and does not write the log twice.
    CHECK_FALSE(journal.advance(*line));
    CHECK(journal.log().size() == line->stages.size());
}

TEST_CASE("a counted stage counts, and the count belongs to the stage that used it") {
    const Questline* line = book().find("flame-disciple");
    REQUIRE(line != nullptr);
    QuestJournal journal;
    journal.start(line->id);
    journal.bumpCounter(line->id, 2);
    CHECK(journal.counter(line->id) == 2);
    // Advancing clears it: the next counted stage starts from nothing.
    REQUIRE(journal.advance(*line));
    CHECK(journal.counter(line->id) == 0);
    // Never negative, however hard it is pushed.
    journal.bumpCounter(line->id, -5);
    CHECK(journal.counter(line->id) == 0);
}

TEST_CASE("where the player is in a line is in the hash") {
    const Questline* line = book().find("flame-disciple");
    REQUIRE(line != nullptr);

    QuestJournal fresh;
    QuestJournal moved;
    HashSink a(1);
    fresh.hashInto(a);
    HashSink b(1);
    moved.hashInto(b);
    REQUIRE(a.finished() == b.finished());

    moved.start(line->id);
    REQUIRE(moved.advance(*line));
    HashSink c(1);
    moved.hashInto(c);
    CHECK(c.finished() != a.finished());

    // Two journals that reached the same stage the same way agree.
    QuestJournal alongside;
    alongside.start(line->id);
    REQUIRE(alongside.advance(*line));
    HashSink d(1);
    alongside.hashInto(d);
    CHECK(d.finished() == c.finished());
}

TEST_CASE("the stage vocabulary is closed, and an unknown kind never becomes a wall") {
    // Every kind this build resolves has a name, and a raws string outside the
    // vocabulary is Unknown rather than silently the first entry.
    CHECK(stageKindOf("oath") == StageKind::Oath);
    CHECK(stageKindOf("alms") == StageKind::Alms);
    CHECK(stageKindOf("talk") == StageKind::Talk);
    CHECK(stageKindOf("teach") == StageKind::Teach);
    CHECK(stageKindOf("forge") == StageKind::Forge);
    CHECK(stageKindOf("enter_zone") == StageKind::Unknown);
    CHECK(stageKindOf("") == StageKind::Unknown);
    CHECK(stageKindName(StageKind::Oath) == "oath");
    CHECK(stageKindName(StageKind::Unknown) == "?");
}
