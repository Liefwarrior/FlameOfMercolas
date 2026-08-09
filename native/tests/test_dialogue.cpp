// The conversation layer, against the owner's actual raws.
//
// Nothing in here invents a line of dialogue and neither does the code it
// tests: every case that asserts something is SAID asserts it came out of
// content/raws/barks/barks.json under an authored key.
//
// Three groups:
//
//   CONTENT   the raws load, the key vocabulary the code builds actually
//             exists in them, and no authored table is unreachable.
//   RULE      the director, driven with a synthetic Speaker and no map at all.
//   ROOM      the Gilded Gull, running, with the acceptance claim in it: an
//             actor's disposition changing what that actor DOES.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <filesystem>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const BarkTables& barks() {
    static const BarkTables tables = BarkTables::load(content::contentDir());
    return tables;
}

const NotableRegistry& registry() {
    static const NotableRegistry loaded = NotableRegistry::load(content::contentDir());
    return loaded;
}

/// Somebody to talk to, with no world anywhere near them.
Speaker dockerNamed(std::int32_t id, std::string name) {
    Speaker speaker;
    speaker.actorId = id;
    speaker.name = std::move(name);
    speaker.epithet = "of the east quay";
    speaker.family = JobFamily::Serf;
    speaker.skillId = "fieldcraft";
    speaker.skillLevel = 25;
    speaker.haggleSkill = 10;
    speaker.awareness = 10;
    speaker.purse = 14;
    return speaker;
}

}  // namespace

// ===========================================================================
// CONTENT -- the raws, and the keys this code builds against them
// ===========================================================================

TEST_CASE("a second bark file adds to the ward's voice and can never overwrite it") {
    // S4 needed the priest to speak and content/raws/barks/barks.json is the
    // owner's canon, so the loader reads the DIRECTORY: the owner's file first,
    // everything else after it in sorted order, first file wins a duplicate
    // key. Not left to alphabetical luck -- a file called "a_barks.json" would
    // otherwise take the ward's voice over silently.
    const std::vector<std::filesystem::path> files = barkRawsFiles(content::contentDir());
    REQUIRE(files.size() >= 2);
    CHECK(files.front() == barkRawsPath(content::contentDir()));
    for (std::size_t i = 2; i < files.size(); ++i) {
        CHECK(files[i - 1] < files[i]);
    }
    // Both files' keys are really in the table set.
    CHECK(barks().has("greet.trade.neutral"));
    CHECK(barks().has("personal.venn"));
    CHECK(barks().has("faction.temple.join"));
    CHECK(barks().has("quest.flame-disciple.oath.maell"));
}

TEST_CASE("the owner's bark tables load, all of them") {
    REQUIRE(barks().loaded());
    // 210 tables in the owner's content/raws/barks/barks.json, plus the 32 in
    // content/raws/barks/flame_barks.json (S4), the 22 in roof_barks.json (S5),
    // the 18 in contract_barks.json (S6), the 12 in house_barks.json (S7), the
    // 8 in nemesis_barks.json (S8), the 25 in ward_barks.json (#79), the 4 in
    // tone_barks.json and the 26 in topic_barks.json (both #82) beside it --
    // each sprint adds a SECOND file rather than editing 59KB of canon, and
    // BarkTables::load reads the whole directory. Pinned: content added should
    // be a visible change here, and content LOST should be red.
    CHECK(barks().tableCount() == 357);
    CHECK(barks().rowCount() > 500);
    // Sorted by key, which is what makes lookup a binary search rather than a
    // hash whose iteration order is the standard library's business.
    for (std::size_t i = 1; i < barks().tables().size(); ++i) {
        REQUIRE(barks().tables()[i - 1].key < barks().tables()[i].key);
    }
    // No empty table and no empty row: either would be a silent blank line in
    // somebody's mouth.
    for (const BarkTables::Table& table : barks().tables()) {
        REQUIRE_FALSE(table.key.empty());
        REQUIRE_FALSE(table.rows.empty());
        for (const std::string& row : table.rows) {
            REQUIRE_FALSE(row.empty());
        }
    }
}

TEST_CASE("every family has all six attitudes, which is why six is the number") {
    REQUIRE(barks().loaded());
    for (std::size_t f = 0; f < kJobFamilyCount; ++f) {
        const JobFamily family = static_cast<JobFamily>(f);
        for (int a = 0; a <= static_cast<int>(Attitude::Kin); ++a) {
            const Attitude attitude = static_cast<Attitude>(a);
            const std::string key =
                "greet." + std::string(jobFamilyKey(family)) + "." + std::string(attitudeKey(attitude));
            INFO("key ", key);
            REQUIRE(barks().has(key));
        }
        // And the bare family table, which is the last rung of the chain.
        REQUIRE(barks().has("greet." + std::string(jobFamilyKey(family))));
    }
}

TEST_CASE("the greeting chain resolves for every family, attitude and hour") {
    REQUIRE(barks().loaded());
    // The chain must never bottom out: a greeting that resolves to nothing is
    // somebody standing in front of you saying "...".
    for (std::size_t f = 0; f < kJobFamilyCount; ++f) {
        for (int a = 0; a <= static_cast<int>(Attitude::Kin); ++a) {
            for (int b = 0; b <= static_cast<int>(TimeBand::Night); ++b) {
                const std::vector<std::string> chain =
                    greetChain(static_cast<JobFamily>(f), static_cast<Attitude>(a),
                               static_cast<TimeBand>(b));
                REQUIRE(chain.size() == 3);
                const std::string_view key = barks().resolve(chain);
                INFO("chain head ", chain.front());
                REQUIRE_FALSE(key.empty());
                REQUIRE_FALSE(barks().line(key, 0).empty());
            }
        }
    }
    // The chain is genuinely a FALLBACK and not a formality: the raws author
    // greet.trade.warm.<band> and do NOT author greet.trade.hostile.<band>, so
    // one of those resolves to the specific key and the other to the general.
    CHECK(barks().resolve(greetChain(JobFamily::Trade, Attitude::Warm, TimeBand::Night)) ==
          "greet.trade.warm.night");
    CHECK(barks().resolve(greetChain(JobFamily::Trade, Attitude::Hostile, TimeBand::Night)) ==
          "greet.trade.hostile");
}

TEST_CASE("the hours the greetings refine over cover the whole clock") {
    // Every second of the day lands in exactly one band, and all four are used.
    bool seen[4] = {false, false, false, false};
    for (std::int32_t second = 0; second < 86400; second += 60) {
        seen[static_cast<std::size_t>(timeBandOf(second))] = true;
    }
    for (const bool used : seen) {
        REQUIRE(used);
    }
    // Wrapping, both ways, because a clock that reads -1 must not index off the
    // front of anything.
    CHECK(timeBandOf(-1) == timeBandOf(86399));
    CHECK(timeBandOf(86400) == timeBandOf(0));
    CHECK(timeBandOf(hourOfDay(3)) == TimeBand::Night);
    CHECK(timeBandOf(hourOfDay(8)) == TimeBand::Morning);
    CHECK(timeBandOf(hourOfDay(13)) == TimeBand::Day);
    CHECK(timeBandOf(hourOfDay(20)) == TimeBand::Evening);
}

TEST_CASE("the raws' mojibake em-dashes become something the font can draw") {
    // The authored JSON carries a UTF-8 em-dash re-encoded through cp1252 --
    // three code points, six bytes, and every one of them a blank column in the
    // 4x6 HUD font. A run of them collapses to the single character they meant.
    CHECK(foldToAscii("a\xE2\x80\x94\x62") == "a-b");
    CHECK(foldToAscii("plain ascii, with punctuation!") == "plain ascii, with punctuation!");
    CHECK(foldToAscii("tab\there") == "tab here");
    // Every loaded row is pure ASCII, because the whole file went through it.
    for (const BarkTables::Table& table : barks().tables()) {
        for (const std::string& row : table.rows) {
            for (const char c : row) {
                REQUIRE(static_cast<unsigned char>(c) < 0x80U);
            }
        }
    }
}

TEST_CASE("a table lookup cannot walk off either end of a table") {
    REQUIRE(barks().loaded());
    const std::vector<std::string>* rows = barks().rows("gossip");
    REQUIRE(rows != nullptr);
    const std::int32_t count = static_cast<std::int32_t>(rows->size());
    // Negative counters, huge counters, and the wrap point: all in range.
    for (std::int32_t i = -1000; i <= 1000; ++i) {
        const std::string_view line = barks().line("gossip", i);
        REQUIRE_FALSE(line.empty());
        REQUIRE(line == (*rows)[static_cast<std::size_t>(((i % count) + count) % count)]);
    }
    // A key that is not authored is empty, not a crash and not a guess.
    CHECK(barks().line("greet.dragon.friendly", 0).empty());
    CHECK(barks().rows("greet.dragon.friendly") == nullptr);
    CHECK(barks().resolve({"nope.one", "nope.two"}).empty());
}

TEST_CASE("the Forty Notables, their stories, and who may repeat them") {
    REQUIRE(registry().loaded());
    // 42 -- the Forty plus the vanished-clerk pass's Widow Sedge, plus Haddie.
    CHECK(registry().notables().size() == 42);
    CHECK(registry().histories().size() == 15);
    CHECK(registry().domains().size() == 15);

    for (const Notable& notable : registry().notables()) {
        INFO("notable ", notable.id);
        REQUIRE_FALSE(notable.id.empty());
        REQUIRE_FALSE(notable.name.empty());
        REQUIRE_FALSE(notable.bio.empty());
        // Every one of the Forty has a personal table authored for them. This
        // is the reachability claim: no notable is a name with nothing to say.
        REQUIRE(barks().has("personal." + notable.id));
    }

    for (const History& history : registry().histories()) {
        INFO("history ", history.id);
        // Both parties are real people...
        REQUIRE(registry().find(history.a) != nullptr);
        REQUIRE(registry().find(history.b) != nullptr);
        REQUIRE(history.a != history.b);
        // ...and the story has a table of its own to be told out of.
        REQUIRE_FALSE(history.gossipKey.empty());
        REQUIRE(barks().has(history.gossipKey));
        REQUIRE(history.gossipKey == "gossip." + history.id);
    }

    for (const RumorDomain& domain : registry().domains()) {
        INFO("domain ", domain.historyId);
        const History* history = registry().history(domain.historyId);
        REQUIRE(history != nullptr);
        REQUIRE_FALSE(domain.knowers.empty());
        for (const std::string& knower : domain.knowers) {
            INFO("knower ", knower);
            REQUIRE(registry().find(knower) != nullptr);
            // The raws' own rule, stated in rumors.json: a history's parties
            // always know their own story and are never re-declared as knowers.
            REQUIRE_FALSE(history->involves(knower));
        }
    }
}

TEST_CASE("who can tell you what is decided by the raws, not by a dice roll") {
    REQUIRE(registry().loaded());

    // Master Venn is party to one story and licensed to repeat two more.
    const std::vector<const History*> venn = registry().tellableBy("venn");
    REQUIRE(venn.size() == 3);
    // His OWN story first: the parties always know theirs.
    CHECK(venn[0]->id == "vess-venn-grudge");
    CHECK(venn[0]->involves("venn"));
    // Then what the domains license, ascending.
    CHECK(venn[1]->id == "brann-dagny-grayledger");
    CHECK(venn[2]->id == "luff-redda-lamp");
    CHECK(registry().isKnower("brann-dagny-grayledger", "venn"));
    // Being a PARTY is not being a KNOWER -- the file never re-declares them.
    CHECK_FALSE(registry().isKnower("vess-venn-grudge", "venn"));

    // Father Maell knows exactly his own, and no gossip at all.
    const std::vector<const History*> maell = registry().tellableBy("maell");
    REQUIRE(maell.size() == 1);
    CHECK(maell[0]->id == "sethra-maell-ember");

    // Captain Wake is nobody's confidant, which is a fact about the content and
    // not an omission here.
    CHECK(registry().tellableBy("wake").empty());
    // And somebody who does not exist tells you nothing rather than crashing.
    CHECK(registry().tellableBy("nobody").empty());
    CHECK(registry().tellableBy("").empty());

    // No authored gossip table is dead content: every history has at least the
    // two parties who can tell it, and most have more.
    for (const History& history : registry().histories()) {
        INFO("history ", history.id);
        std::size_t tellers = 0;
        for (const Notable& notable : registry().notables()) {
            const std::vector<const History*> theirs = registry().tellableBy(notable.id);
            tellers += std::count(theirs.begin(), theirs.end(), &history);
        }
        REQUIRE(tellers >= 2);
    }
}

// ===========================================================================
// RULE -- the director, with no world
// ===========================================================================

TEST_CASE("standing is AUDIBLE: the same person greets you out of a different table") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    REQUIRE(director.barks().loaded());

    const Speaker docker = dockerNamed(4, "Wick Hempson");
    REQUIRE(director.open(docker, hourOfDay(21)));
    const std::string neutralKey = director.greetingKey();
    const std::string neutralLine = director.greeting();
    CHECK(neutralKey == "greet.serf.neutral.evening");
    CHECK(director.attitude() == Attitude::Neutral);
    director.close();

    // Rob him. One deed, and it is the same man in the same room at the same
    // hour -- only what he thinks of you has changed.
    director.ledger().record(4, Deed::Robbed);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.attitude() == Attitude::Hostile);
    CHECK(director.greetingKey() == "greet.serf.hostile");
    CHECK(director.greetingKey() != neutralKey);
    CHECK(director.greeting() != neutralLine);
    // And it is the CONTENT that changed, not a number: the line he now says is
    // one of the authored hostile rows and none of the neutral ones.
    const std::vector<std::string>* hostile = director.barks().rows("greet.serf.hostile");
    REQUIRE(hostile != nullptr);
    CHECK(std::find(hostile->begin(), hostile->end(), director.greeting()) != hostile->end());
    director.close();

    // Stand him enough drinks and the table changes again, the other way.
    for (int i = 0; i < 12; ++i) {
        director.ledger().record(4, Deed::BoughtDrink);
    }
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.attitude() == Attitude::Kin);
    CHECK(director.greetingKey() == "greet.serf.kin");
}

TEST_CASE("a mood outranks a greeting, because somebody on the floor is not chatting") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    Speaker docker = dockerNamed(4, "Wick Hempson");
    docker.moodKey = "mood.downed";
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.greetingKey() == "mood.downed");
}

// ===========================================================================
// #82 -- the tone selector: POLITE / NORMAL / BLUNT, chosen per exchange
// ===========================================================================

TEST_CASE("the tone dial is silent at NORMAL, and changes what is actually said once you turn it") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    // Warm, and on purpose: greet.serf.warm has no per-hour refinement
    // authored (unlike greet.serf.neutral, which has all four), so the
    // untagged tiered key cannot win the chain before a tone-tagged one gets
    // a chance to.
    const Speaker docker = dockerNamed(21, "Tam Sallow");
    director.ledger().seed(21, kWarmAtOrAbove);

    REQUIRE(director.tone() == Tone::Normal);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.attitude() == Attitude::Warm);
    CHECK(director.greetingKey() == "greet.serf.warm");
    const std::string normalLine = director.greeting();
    director.close();

    director.setTone(Tone::Polite);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.greetingKey() == "greet.serf.warm.polite");
    const std::vector<std::string>* polite = director.barks().rows("greet.serf.warm.polite");
    REQUIRE(polite != nullptr);
    CHECK(std::find(polite->begin(), polite->end(), director.greeting()) != polite->end());
    CHECK(director.greeting() != normalLine);
    director.close();

    director.setTone(Tone::Blunt);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.greetingKey() == "greet.serf.warm.blunt");
    const std::vector<std::string>* blunt = director.barks().rows("greet.serf.warm.blunt");
    REQUIRE(blunt != nullptr);
    CHECK(std::find(blunt->begin(), blunt->end(), director.greeting()) != blunt->end());
    CHECK(director.greeting() != normalLine);
    director.close();

    // And dialled back, it says exactly what it always said -- nothing about
    // the untagged table moved underneath it.
    director.setTone(Tone::Normal);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.greetingKey() == "greet.serf.warm");
}

TEST_CASE("tone can open a job list a colder tongue keeps shut, and shut one a warmer tongue had open") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    // Day 3 of this exact seed is the combination test_contract.cpp already
    // proves leaves every broker, Venn included, with at least one live offer.
    director.postContracts(3, 0x4752414E41444144ull);

    Speaker venn = dockerNamed(1, "Master Venn");
    venn.notableId = "venn";
    venn.family = JobFamily::Trade;
    venn.skillId = "streetwise";
    venn.skillLevel = 30;

    const auto offeredCount = [&]() {
        return std::count_if(director.topics().begin(), director.topics().end(),
                             [](const Topic& t) {
                                 return t.kind == TopicKind::TakeContract && t.payload >= 0;
                             });
    };
    const auto askedAboutWork = [&]() {
        return std::any_of(director.topics().begin(), director.topics().end(),
                           [](const Topic& t) {
                               return t.kind == TopicKind::TakeContract && t.payload < 0;
                           });
    };

    SUBCASE("cold enough to be refused, warm enough once you ask nicely") {
        director.ledger().seed(1, kColdAtOrBelow);
        REQUIRE(director.open(venn, hourOfDay(21)));
        CHECK(director.attitude() == Attitude::Cold);
        // Venn's own "acquaintance" gate is closed: the placeholder shows and
        // his real offers do not.
        CHECK(askedAboutWork());
        CHECK(offeredCount() == 0);

        director.setTone(Tone::Polite);
        CHECK_FALSE(askedAboutWork());
        CHECK(offeredCount() > 0);
    }

    SUBCASE("neutral enough to be heard, closed off by a flat tongue") {
        director.ledger().seed(1, kColdAtOrBelow + 1);
        REQUIRE(director.open(venn, hourOfDay(21)));
        CHECK(director.attitude() == Attitude::Neutral);
        CHECK_FALSE(askedAboutWork());
        CHECK(offeredCount() > 0);

        director.setTone(Tone::Blunt);
        CHECK(askedAboutWork());
        CHECK(offeredCount() == 0);
    }
}

namespace {

[[nodiscard]] bool has(const std::vector<Topic>& list, TopicKind kind) {
    return std::any_of(list.begin(), list.end(), [&](const Topic& t) { return t.kind == kind; });
}

/// The index of the first topic of a kind, or list.size() if there is none.
[[nodiscard]] std::size_t indexOf(const std::vector<Topic>& list, TopicKind kind) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].kind == kind) {
            return i;
        }
    }
    return list.size();
}

/// Opens the tree and walks down to one named category branch. REQUIREs both
/// hops land, because a test that silently stayed at the root would pass
/// against a tree that never opened.
void openBranch(DialogueDirector& director, std::string_view category) {
    const std::vector<Topic>& root = director.topics();
    const std::size_t ask = indexOf(root, TopicKind::Ask);
    REQUIRE(ask < root.size());
    director.choose(ask);
    REQUIRE(director.menu() == DialogueMenu::Category);

    const std::vector<Topic>& categories = director.topics();
    std::size_t branch = categories.size();
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (categories[i].kind == TopicKind::Category && categories[i].arg == category) {
            branch = i;
        }
    }
    REQUIRE(branch < categories.size());
    director.choose(branch);
}

}  // namespace

TEST_CASE("the same topic, dialled to a different register, answers differently -- and the ledger hears it") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    const Speaker docker = dockerNamed(22, "Nace Fenwright");
    REQUIRE(director.open(docker, hourOfDay(21)));
    // #82's THEIR OWN BUSINESS moved into the PERSON branch of the tree; see
    // dialogue.hpp's own note on why. openBranch walks the door in exactly
    // the way a player would.
    openBranch(director, kAskPerson);
    REQUIRE(director.menu() == DialogueMenu::Person);

    std::size_t at = indexOf(director.topics(), TopicKind::Personal);
    REQUIRE(at < director.topics().size());
    CHECK(director.topics()[at].barkKey == "personal");
    const std::int32_t start = director.ledger().dispositionOf(22);

    // Dialling the tone rebuilds whichever level is on screen -- here, the
    // PERSON branch itself, not the root -- which is exactly what proves the
    // dial reaches a topic three levels deep and not only the door into it.
    director.setTone(Tone::Polite);
    REQUIRE(director.menu() == DialogueMenu::Person);
    at = indexOf(director.topics(), TopicKind::Personal);
    REQUIRE(at < director.topics().size());
    CHECK(director.topics()[at].barkKey == "personal.polite");
    const Reply polite = director.choose(at);
    REQUIRE(polite.ok);
    CHECK(polite.dispositionBefore == start);
    const std::vector<std::string>* politeRows = director.barks().rows("personal.polite");
    REQUIRE(politeRows != nullptr);
    CHECK(std::find(politeRows->begin(), politeRows->end(), polite.line) != politeRows->end());
    // Held to the exact same talk ceiling every "just listening" deed always
    // was -- SpokePolitely rides Listened's own branch.
    CHECK(polite.dispositionAfter == start + deedWeight(Deed::SpokePolitely));

    director.setTone(Tone::Blunt);
    REQUIRE(director.menu() == DialogueMenu::Person);
    at = indexOf(director.topics(), TopicKind::Personal);
    REQUIRE(at < director.topics().size());
    CHECK(director.topics()[at].barkKey == "personal.blunt");
    const Reply blunt = director.choose(at);
    REQUIRE(blunt.ok);
    CHECK(blunt.dispositionBefore == polite.dispositionAfter);
    const std::vector<std::string>* bluntRows = director.barks().rows("personal.blunt");
    REQUIRE(bluntRows != nullptr);
    CHECK(std::find(bluntRows->begin(), bluntRows->end(), blunt.line) != bluntRows->end());
    CHECK(blunt.line != polite.line);
    // NOT held to the ceiling: a flat tongue costs standing you already had,
    // the same as WalkedOut or Lowballed would.
    CHECK(blunt.dispositionAfter == polite.dispositionAfter + deedWeight(Deed::SpokeBluntly));
    CHECK(blunt.dispositionAfter < polite.dispositionAfter);
}

TEST_CASE("the topic list is built out of what this person is allowed to know") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());

    SUBCASE("a hired hand the raws never named still has plenty to say") {
        REQUIRE(director.open(dockerNamed(4, "Wick Hempson"), hourOfDay(21)));
        const std::vector<Topic>& topics = director.topics();
        // #82: the root keeps exactly the verbs -- the quest everybody has
        // heard of (deliberately still here, mirrored into the tree rather
        // than moved -- see dialogue.hpp's own note), the two verbs that
        // change how he feels about you, and the door into the tree.
        CHECK(has(topics, TopicKind::Ask));
        CHECK(has(topics, TopicKind::Quest));
        CHECK(has(topics, TopicKind::BuyDrinkFor));
        CHECK(has(topics, TopicKind::PickPocket));
        CHECK(has(topics, TopicKind::Leave));
        // He sells nothing, so there is nothing to argue about.
        CHECK_FALSE(has(topics, TopicKind::Trade));
        CHECK_FALSE(has(topics, TopicKind::Buy));
        // THEIR BUSINESS, the ward's talk and shop talk moved behind the door
        // in #82 -- none of them is a root topic for anybody any more.
        CHECK_FALSE(has(topics, TopicKind::Personal));
        CHECK_FALSE(has(topics, TopicKind::WardTalk));
        CHECK_FALSE(has(topics, TopicKind::Mastery));
        // Every topic that speaks does so out of a key that really exists.
        for (const Topic& topic : topics) {
            INFO("topic ", topic.label);
            REQUIRE_FALSE(topic.label.empty());
            if (!topic.barkKey.empty()) {
                REQUIRE(director.barks().has(topic.barkKey));
            }
        }

        // Walk the door in. He has no story of his own, because the raws
        // gave him none, so PERSON offers his own business and the ward's
        // talk and nothing about anybody else; WORK offers his own trade.
        openBranch(director, kAskPerson);
        const std::vector<Topic>& person = director.topics();
        CHECK(director.menu() == DialogueMenu::Person);
        CHECK(has(person, TopicKind::Personal));
        CHECK(has(person, TopicKind::WardTalk));
        CHECK(has(person, TopicKind::Back));
        // ...and he has no story of his own, because the raws gave him none.
        CHECK_FALSE(has(person, TopicKind::History));
        for (const Topic& topic : person) {
            if (!topic.barkKey.empty()) {
                REQUIRE(director.barks().has(topic.barkKey));
            }
        }

        // Back up to the category list, back up again to the root -- and
        // down the WORK branch from there: fieldcraft 25 has a table.
        // openBranch always starts its own walk from the root's own Ask
        // topic, so it is re-entered fresh rather than resumed mid-tree.
        director.choose(indexOf(person, TopicKind::Back));
        CHECK(director.menu() == DialogueMenu::Category);
        director.choose(indexOf(director.topics(), TopicKind::Back));
        CHECK(director.menu() == DialogueMenu::Root);
        openBranch(director, kAskWork);
        CHECK(director.menu() == DialogueMenu::Work);
        CHECK(has(director.topics(), TopicKind::Mastery));
        CHECK(has(director.topics(), TopicKind::Back));
    }

    SUBCASE("a notable carries the stories the rumor domains license") {
        Speaker venn = dockerNamed(1, "Master Venn");
        venn.notableId = "venn";
        venn.family = JobFamily::Trade;
        venn.skillId = "streetwise";
        venn.skillLevel = 30;
        venn.trades = true;
        venn.goods = Goods::Room;
        venn.basePrice = 12;
        REQUIRE(director.open(venn, hourOfDay(21)));

        // Buy/Trade are unaffected verbs and never moved.
        CHECK(has(director.topics(), TopicKind::Buy));
        CHECK(has(director.topics(), TopicKind::Trade));

        openBranch(director, kAskPerson);
        std::vector<std::string> stories;
        for (const Topic& topic : director.topics()) {
            if (topic.kind == TopicKind::History) {
                stories.push_back(topic.barkKey);
            }
        }
        REQUIRE(stories.size() == 3);
        CHECK(stories[0] == "gossip.vess-venn-grudge");
        CHECK(stories[1] == "gossip.brann-dagny-grayledger");
        CHECK(stories[2] == "gossip.luff-redda-lamp");
        // He is a notable, so he speaks from his OWN table and not the generic
        // one -- the difference between a person and a job title.
        const Topic& personal = director.topics().front();
        REQUIRE(personal.kind == TopicKind::Personal);
        CHECK(personal.barkKey == "personal.venn");
    }
}

TEST_CASE("asking about something says an authored line and is worth something") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    REQUIRE(director.open(dockerNamed(4, "Wick Hempson"), hourOfDay(21)));

    std::size_t quest = director.topics().size();
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::Quest) {
            quest = i;
        }
    }
    REQUIRE(quest < director.topics().size());
    const std::string key = director.topics()[quest].barkKey;
    const Reply reply = director.choose(quest);
    CHECK(reply.ok);
    CHECK(reply.kind == TopicKind::Quest);
    const std::vector<std::string>* rows = director.barks().rows(key);
    REQUIRE(rows != nullptr);
    // The line came out of the raws. Not "looks like it did" -- IS one of them.
    CHECK(std::find(rows->begin(), rows->end(), reply.line) != rows->end());
    CHECK(reply.dispositionAfter > reply.dispositionBefore);
    CHECK_FALSE(reply.closes);

    // Out of range is nothing at all, rather than a read off the end.
    const Reply nothing = director.choose(9999);
    CHECK_FALSE(nothing.ok);
    CHECK(nothing.line.empty());
}

TEST_CASE("speaking to the same person twice is two different sentences") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    const Speaker docker = dockerNamed(4, "Wick Hempson");
    std::vector<std::string> heard;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(director.open(docker, hourOfDay(21)));
        heard.push_back(director.greeting());
        director.close();
    }
    // The neutral evening table has four rows; four conversations should walk
    // them rather than repeating one.
    std::sort(heard.begin(), heard.end());
    heard.erase(std::unique(heard.begin(), heard.end()), heard.end());
    CHECK(heard.size() >= 3);
}

TEST_CASE("the same conversation, run twice, says exactly the same things") {
    // Draw-free, and this is what proves it: two directors, identical inputs,
    // identical output down to the string. A roll hiding in the selector would
    // show up here as a disagreement inside one process.
    const Speaker docker = dockerNamed(4, "Wick Hempson");
    std::vector<std::string> first;
    std::vector<std::string> second;
    for (int run = 0; run < 2; ++run) {
        DialogueDirector director = DialogueDirector::load(content::contentDir());
        std::vector<std::string>& into = run == 0 ? first : second;
        for (int conversation = 0; conversation < 3; ++conversation) {
            REQUIRE(director.open(docker, hourOfDay(21)));
            into.push_back(director.greeting());
            for (std::size_t i = 0; i < director.topics().size(); ++i) {
                if (director.topics()[i].kind == TopicKind::Leave ||
                    director.topics()[i].kind == TopicKind::PickPocket) {
                    continue;
                }
                into.push_back(director.choose(i).line);
            }
            director.close();
        }
    }
    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
}

TEST_CASE("a hand in a purse either lands or gets caught, and skill decides which") {
    Speaker docker = dockerNamed(4, "Wick Hempson");
    docker.awareness = 20;

    SUBCASE("a beginner is caught, and it is remembered") {
        DialogueDirector director = DialogueDirector::load(content::contentDir());
        REQUIRE(director.open(docker, hourOfDay(21)));
        std::size_t at = director.topics().size();
        for (std::size_t i = 0; i < director.topics().size(); ++i) {
            if (director.topics()[i].kind == TopicKind::PickPocket) {
                at = i;
            }
        }
        REQUIRE(at < director.topics().size());
        const Reply reply = director.choose(at);
        CHECK(reply.ok);
        CHECK(reply.offence);
        CHECK(reply.coinDelta == 0);
        CHECK(reply.closes);
        CHECK(director.ledger().attitudeOf(4) == Attitude::Hostile);
        CHECK(director.ledger().memoryOf(4)->injuries == 1);
        CHECK(director.ledger().memoryOf(4)->lastDeed == Deed::Robbed);
    }

    SUBCASE("a cracksman lifts it clean, and nobody remembers anything") {
        DialogueDirector director = DialogueDirector::load(content::contentDir());
        REQUIRE(director.skills().setLevel(kThieverySkill, 40));
        REQUIRE(director.open(docker, hourOfDay(21)));
        std::size_t at = director.topics().size();
        for (std::size_t i = 0; i < director.topics().size(); ++i) {
            if (director.topics()[i].kind == TopicKind::PickPocket) {
                at = i;
            }
        }
        REQUIRE(at < director.topics().size());
        const Reply reply = director.choose(at);
        CHECK(reply.ok);
        CHECK_FALSE(reply.offence);
        CHECK(reply.coinDelta > 0);
        // Nothing was done TO him that he knows about, so his standing is
        // whatever the conversation left it -- not hostile.
        CHECK(director.ledger().attitudeOf(4) != Attitude::Hostile);
    }
}

TEST_CASE("standing somebody a drink costs what a drink costs") {
    // The dialogue layer must not include tavern.hpp, so it names the price
    // itself. This is the pin that stops the two drifting apart.
    CHECK(kBoughtDrinkCost == kDrinkPrice);

    DialogueDirector director = DialogueDirector::load(content::contentDir());
    director.setPlayerCoin(0);
    REQUIRE(director.open(dockerNamed(4, "Wick Hempson"), hourOfDay(21)));
    std::size_t at = director.topics().size();
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::BuyDrinkFor) {
            at = i;
        }
    }
    REQUIRE(at < director.topics().size());

    // An empty purse buys nothing and earns nothing.
    const Reply broke = director.choose(at);
    CHECK_FALSE(broke.ok);
    CHECK(broke.coinDelta == 0);
    CHECK(director.ledger().dispositionOf(4) < kWarmAtOrAbove);

    director.setPlayerCoin(40);
    const Reply bought = director.choose(at);
    CHECK(bought.ok);
    CHECK(bought.coinDelta == -kBoughtDrinkCost);
    CHECK(bought.dispositionAfter == bought.dispositionBefore + deedWeight(Deed::BoughtDrink));
    CHECK(director.ledger().memoryOf(4)->favours == 1);
    // Twice more and he is genuinely warm toward you -- which no amount of
    // conversation would have managed.
    director.choose(at);
    CHECK(director.ledger().attitudeOf(4) == Attitude::Warm);
}

// ===========================================================================
// #82 -- THE "TELL ME ABOUT" TREE
// ===========================================================================

TEST_CASE("the tree opens under TELL ME ABOUT and BACK always lands one level up") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    REQUIRE(director.open(dockerNamed(4, "Wick Hempson"), hourOfDay(21)));
    CHECK(director.menu() == DialogueMenu::Root);

    openBranch(director, kAskLocation);
    CHECK(director.menu() == DialogueMenu::Location);
    // A PLACE would not have shown at all with nothing behind it -- five
    // curated landmarks are common knowledge, so at least one resolves.
    CHECK(director.topics().size() > 1);
    CHECK(has(director.topics(), TopicKind::Location));
    CHECK(has(director.topics(), TopicKind::Back));

    // BACK from a leaf lands on the category list, and BACK from there lands
    // on the root -- never anywhere else, and never closing the
    // conversation.
    const Reply backToCategory = director.choose(indexOf(director.topics(), TopicKind::Back));
    CHECK(backToCategory.ok);
    CHECK_FALSE(backToCategory.closes);
    CHECK(director.menu() == DialogueMenu::Category);
    CHECK(has(director.topics(), TopicKind::Back));

    director.choose(indexOf(director.topics(), TopicKind::Back));
    CHECK(director.menu() == DialogueMenu::Root);
    // Back at the root, exactly the list open() built the first time.
    CHECK(has(director.topics(), TopicKind::Ask));
    CHECK_FALSE(has(director.topics(), TopicKind::Back));
}

TEST_CASE("a dockhand and a priest describe the same place in different words") {
    // THE ACCEPTANCE CLAIM: the SAME topic, asked of two different families,
    // resolves through their own voice -- the fallback chain topicChain()
    // shares with greetChain, proven on real authored content rather than a
    // synthetic table.
    Speaker docker = dockerNamed(4, "Wick Hempson");
    docker.family = JobFamily::Serf;

    Speaker priest = dockerNamed(5, "Father Something");
    priest.family = JobFamily::Clergy;

    DialogueDirector dockerTalk = DialogueDirector::load(content::contentDir());
    REQUIRE(dockerTalk.open(docker, hourOfDay(13)));
    openBranch(dockerTalk, kAskLocation);
    const std::size_t dockerMission = indexOf(dockerTalk.topics(), TopicKind::Location);
    REQUIRE(dockerMission < dockerTalk.topics().size());
    // Walk to the specific "THE MISSION" leaf -- more than one location may
    // be on the list, and the claim is about this one, which has a
    // clergy-specific table authored.
    std::size_t at = dockerTalk.topics().size();
    for (std::size_t i = 0; i < dockerTalk.topics().size(); ++i) {
        if (dockerTalk.topics()[i].kind == TopicKind::Location &&
            dockerTalk.topics()[i].arg == "mission") {
            at = i;
        }
    }
    REQUIRE(at < dockerTalk.topics().size());
    const std::string dockerKey = dockerTalk.topics()[at].barkKey;
    const Reply dockerReply = dockerTalk.choose(at);
    CHECK(dockerReply.ok);
    // A serf has no family-specific table authored for the Mission, so he
    // falls all the way back to the generic key.
    CHECK(dockerKey == "location.mission");

    DialogueDirector priestTalk = DialogueDirector::load(content::contentDir());
    REQUIRE(priestTalk.open(priest, hourOfDay(13)));
    openBranch(priestTalk, kAskLocation);
    std::size_t priestAt = priestTalk.topics().size();
    for (std::size_t i = 0; i < priestTalk.topics().size(); ++i) {
        if (priestTalk.topics()[i].kind == TopicKind::Location &&
            priestTalk.topics()[i].arg == "mission") {
            priestAt = i;
        }
    }
    REQUIRE(priestAt < priestTalk.topics().size());
    const std::string priestKey = priestTalk.topics()[priestAt].barkKey;
    const Reply priestReply = priestTalk.choose(priestAt);
    CHECK(priestReply.ok);
    // A priest has his own authored table, and the key PROVES it -- not
    // merely that the line happens to differ.
    CHECK(priestKey == "location.mission.clergy");
    CHECK(priestKey != dockerKey);

    // And the words themselves are different, each genuinely out of its own
    // authored table.
    CHECK(dockerReply.line != priestReply.line);
    const std::vector<std::string>* dockerRows = dockerTalk.barks().rows(dockerKey);
    const std::vector<std::string>* priestRows = priestTalk.barks().rows(priestKey);
    REQUIRE(dockerRows != nullptr);
    REQUIRE(priestRows != nullptr);
    CHECK(std::find(dockerRows->begin(), dockerRows->end(), dockerReply.line) !=
          dockerRows->end());
    CHECK(std::find(priestRows->begin(), priestRows->end(), priestReply.line) !=
          priestRows->end());
}

TEST_CASE("the THING branch answers too, and a watchman's passport line is his own") {
    Speaker watch = dockerNamed(6, "Watchman Somebody");
    watch.family = JobFamily::Watch;
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    REQUIRE(director.open(watch, hourOfDay(13)));

    openBranch(director, kAskThing);
    CHECK(director.menu() == DialogueMenu::Thing);
    std::size_t passport = director.topics().size();
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::Thing && director.topics()[i].arg == "a_passport") {
            passport = i;
        }
    }
    REQUIRE(passport < director.topics().size());
    CHECK(director.topics()[passport].barkKey == "thing.a_passport.watch");
    const Reply reply = director.choose(passport);
    CHECK(reply.ok);
    CHECK_FALSE(reply.line.empty());
}

TEST_CASE("a walk through the whole tree, run twice, says exactly the same things") {
    // The same draw-free claim the flat list already proved, extended to
    // every level of the tree: LOCATION and THING are new content and a new
    // fallback chain, and #82's tone dial widens every chain the tree
    // resolves through -- if either hid a draw, this is where it would show.
    const Speaker docker = dockerNamed(4, "Wick Hempson");
    const auto walkOnce = [&]() {
        std::vector<std::string> heard;
        DialogueDirector director = DialogueDirector::load(content::contentDir());
        REQUIRE(director.open(docker, hourOfDay(21)));
        for (const std::string_view category : {"location", "person", "thing", "work", "quest"}) {
            openBranch(director, category);
            for (std::size_t i = 0; i < director.topics().size(); ++i) {
                if (director.topics()[i].kind == TopicKind::Back) {
                    continue;
                }
                heard.push_back(director.choose(i).line);
            }
            director.choose(indexOf(director.topics(), TopicKind::Back));  // -> category
            director.choose(indexOf(director.topics(), TopicKind::Back));  // -> root
        }
        return heard;
    };
    const std::vector<std::string> first = walkOnce();
    const std::vector<std::string> second = walkOnce();
    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
}

// ===========================================================================
// ROOM -- the Gilded Gull, with the acceptance claim in it
// ===========================================================================

namespace {

content::World& docksWorld() {
    // Non-const: PhasedEngine takes a mutable World. One load for the whole
    // file, and nothing in here writes to it.
    static content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

/// A tavern with a body standing somewhere in it. Just enough of the client's
/// loop to make the room real.
class AtTheBar {
public:
    explicit AtTheBar(std::int32_t timeOfDay = hourOfDay(11),
                      std::int32_t tileX = gull::kBartenderX,
                      std::int32_t tileY = gull::kBarY - 1)
        : tiles_(std::make_unique<TileQuery>(docksWorld())),
          engine_(std::make_unique<PhasedEngine>(kSeed, docksWorld())),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand,
                                             kFacingNorth)) {
        auto tavern =
            std::make_unique<Tavern>(*tiles_, timeOfDay, kSeed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] const TileQuery& tiles() const noexcept { return *tiles_; }
    [[nodiscard]] const PlayerBody& body() const noexcept { return *body_; }

    [[nodiscard]] const Actor* roleOf(ActorRole role) const {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.role() == role) {
                return &actor;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const Actor* bartender() const { return roleOf(ActorRole::Bartender); }

    /// Finds the topic of a kind on the open conversation.
    [[nodiscard]] std::size_t topicOf(TopicKind kind) const {
        const std::vector<Topic>& topics = tavern_->dialogue().topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind == kind) {
                return i;
            }
        }
        return topics.size();
    }

private:
    static constexpr std::uint64_t kSeed = 0x4752414E41444144ull;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
};

}  // namespace

TEST_CASE("the roster knows which of its people the raws actually named") {
    AtTheBar bar;
    // Five of the sixteen are among the Forty, and the tavern says which by
    // handing the dialogue layer their notables.json id. S5 added two: Finch,
    // who replaced S2's invented "Wisp" because the owner had already named the
    // ward's Skyrunner, and Watchman Cull, who gives the garrison a recruiter a
    // player can actually stand in front of.
    std::vector<std::string> named;
    for (const Actor& actor : bar.tavern().actors()) {
        const Speaker speaker = bar.tavern().speakerFor(actor);
        REQUIRE(speaker.actorId == actor.id());
        REQUIRE(speaker.name == actor.name());
        if (!speaker.notableId.empty()) {
            INFO("notable id ", speaker.notableId);
            // ...and it is a REAL id, not a plausible-looking string.
            REQUIRE(registry().find(speaker.notableId) != nullptr);
            named.push_back(speaker.notableId);
        }
    }
    std::sort(named.begin(), named.end());
    REQUIRE(named.size() == 5);
    CHECK(named[0] == "cull");
    CHECK(named[1] == "finch");
    CHECK(named[2] == "maell");
    CHECK(named[3] == "venn");
    CHECK(named[4] == "wake");
}

TEST_CASE("an actor's disposition changes what that actor DOES") {
    // THE SPRINT'S ACCEPTANCE CLAIM. Not "the number moved" -- the number moving
    // is arithmetic. This asserts the BEHAVIOUR on the other side of it: the
    // same bartender, in the same room, at the same hour, with the same stock
    // and the same purse, serves you or refuses to.
    //
    // Eleven, when the doors have just opened: Gerta is behind the bar and the
    // crowd that would otherwise be the nearest person is still out on the quay.
    AtTheBar bar(hourOfDay(11));
    const Actor* gerta = bar.bartender();
    REQUIRE(gerta != nullptr);
    REQUIRE(gerta->present());
    const std::int32_t id = gerta->id();

    // A stranger is served at the odds.
    REQUIRE(bar.tavern().dialogue().ledger().attitudeOf(id) == Attitude::Neutral);
    const std::int32_t strangerPrice = bar.tavern().drinkPriceForPlayer();
    CHECK(bar.tavern().buyDrink() == ServiceResult::Served);
    CHECK(bar.tavern().drinksPlayerHasHad() == 1);

    // Put a hand in her till, in a room with eleven other people in it.
    REQUIRE(bar.tavern().talkTo());
    const std::size_t lift = bar.topicOf(TopicKind::PickPocket);
    REQUIRE(lift < bar.tavern().dialogue().topics().size());
    const Reply caught = bar.tavern().chooseTopic(lift);
    REQUIRE(caught.offence);

    // She remembers, and now she will not pour.
    CHECK(bar.tavern().dialogue().ledger().attitudeOf(id) == Attitude::Hostile);
    CHECK(bar.tavern().dialogue().ledger().memoryOf(id)->lastDeed == Deed::Robbed);
    CHECK(bar.tavern().buyDrink() == ServiceResult::Refused);
    CHECK(bar.tavern().drinksPlayerHasHad() == 1);
    // Nothing else about the room changed: the stock is there, the doors are
    // open, she is on shift and standing where she was. It is her opinion.
    CHECK(bar.tavern().isOpen());
    CHECK(bar.tavern().drinkStock() > 0);
    CHECK(bar.bartender()->present());

    // The room saw it, and the ward heard about it.
    //
    // S4 CHANGED HOW MUCH. S3 counted every body in the building as a witness,
    // through walls and through the bar counter and across floors; S4's
    // spreadWitness wants the same floor and a sight line, so a quiet room at
    // eleven has two or three witnesses rather than eleven and the district
    // hears proportionally less. Asserted against a room where nothing
    // happened, rather than against a threshold that would move with the hour.
    AtTheBar untouched(hourOfDay(11));
    CHECK(bar.tavern().dialogue().ledger().reputation() < 0);
    CHECK(bar.tavern().dialogue().ledger().reputation() <
          untouched.tavern().dialogue().ledger().reputation());
    // And the house sent somebody.
    CHECK(bar.tavern().playerStanding() != Standing::Welcome);

    // The other direction, on a clean slate: a landlady who likes you pours,
    // and never charges more than she charges a stranger.
    AtTheBar friendly(hourOfDay(11));
    const std::int32_t friendlyId = friendly.bartender()->id();
    for (int i = 0; i < 8; ++i) {
        friendly.tavern().dialogue().ledger().record(friendlyId, Deed::BoughtDrink);
    }
    REQUIRE(friendly.tavern().dialogue().ledger().attitudeOf(friendlyId) == Attitude::Kin);
    CHECK(friendly.tavern().drinkPriceForPlayer() <= strangerPrice);
    CHECK(friendly.tavern().buyDrink() == ServiceResult::Served);
}

TEST_CASE("what a bed costs is what the landlord thinks of you") {
    // The same claim where the arithmetic has room to show: twelve coin, not
    // two, so every band is a different number.
    // Standing at the foot of the stair, which is where Master Venn keeps his
    // post -- close enough to actually be answered.
    AtTheBar bar(hourOfDay(11), gull::kStairX, gull::kStairY);
    REQUIRE(bar.tiles().standable(gull::kStairX, gull::kStairY, gull::kGroundBand));
    const Actor* venn = bar.roleOf(ActorRole::Innkeeper);
    REQUIRE(venn != nullptr);
    REQUIRE(venn->present());
    const std::int32_t id = venn->id();

    const std::int32_t neutral = bar.tavern().roomPriceForPlayer();
    bar.tavern().dialogue().ledger().seed(id, kHostileAtOrBelow);
    const std::int32_t hostile = bar.tavern().roomPriceForPlayer();
    bar.tavern().dialogue().ledger().seed(id, kKinAtOrAbove);
    const std::int32_t kin = bar.tavern().roomPriceForPlayer();

    CHECK(hostile > neutral);
    CHECK(kin < neutral);
    CHECK(hostile > kin + 5);
    // ...and a hostile landlord does not rent at all, however much you offer.
    bar.tavern().dialogue().ledger().seed(id, kHostileAtOrBelow);
    bar.tavern().setPlayerCoin(1000);
    CHECK(bar.tavern().rentRoom() == ServiceResult::Refused);
    CHECK(bar.tavern().rentedRoom() < 0);
    // The same man, the same stair, the same purse -- warm instead of hostile.
    bar.tavern().dialogue().ledger().seed(id, kKinAtOrAbove);
    CHECK(bar.tavern().rentRoom() == ServiceResult::Served);
    CHECK(bar.tavern().rentedRoom() >= 0);
}

TEST_CASE("a haggle across the bar sets the price the bar then charges") {
    AtTheBar bar(hourOfDay(11));
    REQUIRE(bar.tavern().talkTo());
    REQUIRE(bar.tavern().dialogue().speaker().actorId == bar.bartender()->id());
    const std::size_t trade = bar.topicOf(TopicKind::Trade);
    REQUIRE(trade < bar.tavern().dialogue().topics().size());

    const Reply opened = bar.tavern().chooseTopic(trade);
    CHECK(opened.haggling);
    REQUIRE(bar.tavern().dialogue().isHaggling());
    const std::int32_t reserve = bar.tavern().dialogue().haggle().reserve();
    const std::int32_t asking = bar.tavern().dialogue().haggle().asking();

    const Reply struck = bar.tavern().offerPrice(reserve);
    CHECK(struck.ok);
    CHECK_FALSE(bar.tavern().dialogue().isHaggling());
    // An AGREEMENT, not a payment: no coin has moved yet.
    CHECK(bar.tavern().negotiatedDrinkPrice() == reserve);
    CHECK(bar.tavern().drinkPriceForPlayer() == reserve);

    const std::int32_t purse = bar.tavern().playerCoin();
    REQUIRE(bar.tavern().buyDrink() == ServiceResult::Served);
    CHECK(bar.tavern().playerCoin() == purse - reserve);
    // One argument buys one drink; the next is back at the asking price.
    CHECK(bar.tavern().negotiatedDrinkPrice() == -1);
    CHECK(bar.tavern().drinkPriceForPlayer() == asking);
    // And the grind cost her some goodwill, which is the trade-off.
    CHECK(bar.tavern().dialogue().ledger().memoryOf(bar.bartender()->id()) != nullptr);
}

TEST_CASE("a night's sleep does not make anybody forget") {
    // PERSISTENCE, asserted where it is most likely to be lost: skipTo() throws
    // away everything that would have happened in the hours it skips, and it
    // must not throw away this.
    AtTheBar bar(hourOfDay(11));
    const std::int32_t id = bar.bartender()->id();
    bar.tavern().dialogue().ledger().record(id, Deed::Robbed);
    const std::int32_t before = bar.tavern().dialogue().ledger().dispositionOf(id);
    REQUIRE(before < 0);

    bar.tavern().skipTo(hourOfDay(7));
    CHECK(bar.tavern().dialogue().ledger().dispositionOf(id) == before);
    bar.tavern().skipTo(hourOfDay(21));
    CHECK(bar.tavern().dialogue().ledger().dispositionOf(id) == before);
    CHECK(bar.tavern().dialogue().ledger().attitudeOf(id) == Attitude::Hostile);
    // The cellar was restocked, so the world DID move on. It is the memory that
    // did not.
    CHECK(bar.tavern().drinkStock() == kOpeningStock);
}

TEST_CASE("a drawn blade is remembered by everybody who saw it") {
    // escalated() had no consumer in S2: a knife came out, one boolean changed,
    // and nothing else in the world happened. This is the consumer.
    AtTheBar bar(hourOfDay(21));
    bar.tavern().setPlayerCombat(Weapon::Edged, Intent::Kill);
    const std::int32_t witnesses = bar.tavern().presentCount();
    REQUIRE(witnesses > 3);

    const Tavern::PunchResult result = bar.tavern().playerPunchNearest();
    REQUIRE(result.swung);
    REQUIRE(bar.tavern().escalated());

    // The man it was pointed at, and the room around him.
    CHECK(bar.tavern().dialogue().ledger().dispositionOf(result.targetId) <= kHostileAtOrBelow);
    std::int32_t soured = 0;
    for (const Actor& actor : bar.tavern().actors()) {
        if (actor.present() && bar.tavern().dialogue().ledger().dispositionOf(actor.id()) < 0) {
            ++soured;
        }
    }
    CHECK(soured >= 3);
    // Cold, not hostile: S4's witness rule only counts the people who could
    // actually see it. A knife drawn in a full taproom still gets the ward
    // talking, and the phrase in the corner of the HUD changes to say so.
    CHECK(bar.tavern().dialogue().ledger().reputation() <= kColdAtOrBelow);
    CHECK(bar.tavern().dialogue().ledger().reputationLabel() != "NOBODY IN PARTICULAR");
    CHECK(bar.tavern().playerStanding() != Standing::Welcome);

    // Exactly once, however many times the classifier says "lethal": the room
    // does not get angrier every second the knife stays out.
    const std::int32_t after = bar.tavern().dialogue().ledger().reputation();
    const std::int32_t deeds = bar.tavern().dialogue().ledger().deedsDone();
    bar.tavern().playerPunchNearest();
    CHECK(bar.tavern().dialogue().ledger().reputation() == after);
    CHECK(bar.tavern().dialogue().ledger().deedsDone() == deeds);
}

TEST_CASE("the ward's memory is in the hash the twin-run gate compares") {
    // A relationship the gate cannot see is a relationship the gate does not
    // protect. Two rooms, identical in every way except one remembered deed,
    // must not hash the same.
    AtTheBar clean(hourOfDay(11));
    AtTheBar robbed(hourOfDay(11));
    HashSink a(1);
    clean.tavern().hash_into(a);
    HashSink b(1);
    robbed.tavern().hash_into(b);
    REQUIRE(a.finished() == b.finished());

    robbed.tavern().dialogue().ledger().record(robbed.bartender()->id(), Deed::Robbed);
    HashSink c(1);
    robbed.tavern().hash_into(c);
    CHECK(c.finished() != a.finished());

    // Same for a skill the player earned, and for a price on the table.
    AtTheBar skilled(hourOfDay(11));
    skilled.tavern().dialogue().skills().use(kHaggleSkill, 40);
    HashSink d(1);
    skilled.tavern().hash_into(d);
    CHECK(d.finished() != a.finished());
}
