// TASK #81. THE RADIANT QUEST GENERATOR'S ANTI-REPETITION CHECK.
//
// test_radiant_quest.cpp already proves a generated objective binds to a real
// body, a real place, and never a fabricated noun. It does NOT prove the
// generator stays honest over TIME -- a template that only ever draws the
// same two people, or whose composed sentence collapses onto one exact string
// for hundreds of boards running, would pass every one of those cases and
// still read to a player as a slot machine with the serial numbers filed off.
// That is the specific failure this file exists to catch, and it is kept in
// its own file rather than folded into test_radiant_quest.cpp's so a content
// addition to either radiant raws file never has to touch this one.
//
// WHAT "PER FACTION" MEANS HERE. RadiantTemplate.giverTypes names a WardType
// (shopkeeper, watch, keeper, wastrel, urchin), never a faction directly --
// but ward_voice.cpp already resolves a live speaker's own factionId by the
// same two-step path this file reuses: WardType -> JobFamily (wardJobFamily,
// ward_voice.hpp) -> that family's raws key (jobFamilyKey, barks.hpp) ->
// FactionRegistry::factionForJobPrefix (faction.hpp). Reusing that path
// instead of inventing a second WardType-to-faction table means a giver here
// is grouped exactly the way the dialogue layer would introduce them, PRESENTED
// identity and all -- a WardType::Thief giver reads as Wastrel here for the
// identical reason tavern.cpp's own Skyrunner-contact comment gives ("Wisp
// presents as a wastrel... presented identity versus true identity"), and
// wastrel.streetlife is "deliberately unaffiliated" by factions.json's own
// note, so that giver's bucket is keyed -1 and labelled "unaffiliated" rather
// than invented a faction for it.
//
// THE SAMPLE. Hundreds of days of boards, off the one shared, never-ticked
// ward fixture every other case in the radiant suite reads -- so every axis
// of variation in the output traces to the board's own draw (which template,
// which two live bodies, which good, how many), which is exactly the set of
// axes a mad-lib generator fails to vary. Every offer is bucketed twice --
// once by templateId, the actual sentence shape a "mad-lib" complaint is
// about, and once by the giver's faction, the grouping this task asked for --
// plus once into a whole-sample bucket, so the headline claim ("this does not
// read as the same sentence repeated") is proved directly and not only
// inferred from its parts.
//
// THE BAR. For a bucket to pass: more than half its generated sentences are
// literally different strings; no single sentence is more than a fifth of the
// bucket; and the cast rotates -- MORE THAN ONE DIFFERENT BODY was drawn into
// the giver slot and into the target slot, UNLESS THE RAWS THEMSELVES NAME
// ONLY ONE BODY THAT COULD EVER STAND THERE. That carve-out is not a
// loophole: Father Maell is the ward's only WardType::PriestOfTheFlame
// (ward_actors.hpp's own comment -- "exactly one -- Father Maell at the
// Mission") and deliver_mission_burial_word's giverTypes names nothing else,
// so a single recurring giver there is the raws' own authored intent, not a
// draw that has quietly collapsed. So this file computes, per bucket, the
// ACTUAL number of living bodies the raws could ever have drawn into that
// slot -- for a template, the ward's own headcount of its giverTypes/
// targetTypes; for a faction or the whole sample, the same headcount summed
// over every type that presents as it -- and requires the cast to rotate
// UP TO THAT CEILING, never past it. Two clears every pool bigger than one;
// one clears only a pool that is actually one body deep, which the kennel's
// two AnimalKeepers (native/src/sim/ward_roster.cpp: one at "impound", one
// running "kennel-row") and Father Maell's own one both still are, in
// opposite ways.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/radiant_quest.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/ward_voice.hpp"

#include "support/ward_fixture.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace testfix = granadad::testfix;

namespace {

const RadiantRaws& raws() {
    static const RadiantRaws loaded = RadiantRaws::load(content::contentDir());
    return loaded;
}

const FactionRegistry& factions() {
    static const FactionRegistry loaded = FactionRegistry::load(content::contentDir());
    return loaded;
}

/// A template by id, or nullptr -- this file's own copy of the one-line
/// lookup test_radiant_quest.cpp keeps under the same name, over whatever the
/// full merged raws table currently holds.
[[nodiscard]] const RadiantTemplate* templateNamed(std::string_view id) {
    for (const RadiantTemplate& tmpl : raws().templates()) {
        if (tmpl.id == id) {
            return &tmpl;
        }
    }
    return nullptr;
}

/// The REAL faction a giver's WardType presents as, or -1 for a type
/// factions.json does not claim -- see the file header for why this is the
/// same two-step resolution ward_voice.cpp uses for a speaker's own
/// factionId, and not a second table invented here.
[[nodiscard]] std::int32_t giverFactionIndex(WardType type) {
    return factions().factionForJobPrefix(jobFamilyKey(wardJobFamily(type)));
}

/// The registry's own displayName for `index`, or "unaffiliated" for -1 --
/// what INFO and a failure message print, never a bare number.
[[nodiscard]] std::string factionLabel(std::int32_t index) {
    if (const Faction* found = factions().at(index); found != nullptr) {
        return found->displayName;
    }
    return "unaffiliated";
}

/// How many living, visible people of each WardType the ward actually holds
/// -- the ceiling every "does the cast rotate" check below is measured
/// against, so a role the raws only ever hand to one named body is judged
/// against the one body it could ever have, not against a constant this file
/// invented.
[[nodiscard]] std::map<WardType, std::size_t> personCountsIn(const WardPopulation& ward) {
    std::map<WardType, std::size_t> counts;
    for (const WardActor& actor : ward.actors()) {
        if (actor.dead || !actor.visible() || !isPerson(actor.type)) {
            continue;
        }
        ++counts[actor.type];
    }
    return counts;
}

/// The ward headcount summed over `types` -- how many bodies could ever have
/// been drawn into a role restricted to exactly this list of WardTypes.
[[nodiscard]] std::size_t headcount(const std::map<WardType, std::size_t>& counts,
                                    const std::vector<WardType>& types) {
    std::size_t total = 0;
    for (const WardType type : types) {
        const auto found = counts.find(type);
        if (found != counts.end()) {
            total += found->second;
        }
    }
    return total;
}

/// The ward headcount summed over every person WardType that presents as
/// faction `index` -- the same faction bucket byFaction below groups givers
/// into, so the ceiling for "does a faction's own cast rotate" is measured
/// against exactly the bodies that faction could ever hand the board.
[[nodiscard]] std::size_t headcountForFaction(const std::map<WardType, std::size_t>& counts,
                                              std::int32_t index) {
    std::size_t total = 0;
    for (const auto& entry : counts) {
        if (giverFactionIndex(entry.first) == index) {
            total += entry.second;
        }
    }
    return total;
}

/// One bucket's running tally: every brief string this bucket produced, plus
/// which live bodies stood in the giver and target slot -- what "the same two
/// names forever" would look like if this generator were failing.
struct RepetitionBucket {
    std::vector<std::string> briefs;
    std::set<std::int32_t> giverIds;
    std::set<std::int32_t> targetIds;
};

void recordInto(RepetitionBucket& bucket, const RadiantObjective& row) {
    bucket.briefs.push_back(row.brief);
    bucket.giverIds.insert(row.giverActorId);
    bucket.targetIds.insert(row.targetActorId);
}

/// A LARGE sample must not read as a mad-lib: mostly-different sentences, no
/// single sentence standing in for the rest, and the cast rotating up to
/// however many bodies the raws could ever have drawn into each slot (see the
/// file header on `giverCeiling`/`targetCeiling` -- most buckets have dozens
/// or hundreds of candidates and this floors at two; a slot the raws hand to
/// exactly one named body floors at the one body it has). `label` is what
/// INFO prints when one of these trips.
void checkNotAMadLib(const RepetitionBucket& bucket, const std::string& label,
                     std::size_t giverCeiling, std::size_t targetCeiling) {
    INFO(label, ": ", bucket.briefs.size(), " offers");
    REQUIRE_FALSE(bucket.briefs.empty());

    std::map<std::string, std::size_t> counts;
    for (const std::string& brief : bucket.briefs) {
        ++counts[brief];
    }
    std::size_t maxRepeat = 0;
    for (const auto& entry : counts) {
        maxRepeat = std::max(maxRepeat, entry.second);
    }
    INFO("distinct sentences ", counts.size(), " of ", bucket.briefs.size(),
        ", most-repeated sentence seen ", maxRepeat, " times");

    // MOST GENERATED SENTENCES ARE LITERALLY DIFFERENT STRINGS. A generator
    // that always drew the same giver against the same target and the same
    // good would fail this outright; the live ward's own pools are wide
    // enough that a healthy board clears it by a wide margin.
    CHECK(counts.size() * 2 > bucket.briefs.size());

    // NO SINGLE SENTENCE DOMINATES THE BUCKET. The failure this line exists
    // to catch is a draw that has quietly collapsed onto one pairing -- a
    // stuck modulus or a mis-salted RNG call, the shape of bug the twin-run
    // gate's own experiments in this task's history were built to surface --
    // which reads on screen as the same three words for the rest of the
    // session.
    CHECK(maxRepeat * 5 <= bucket.briefs.size());

    // THE CAST ROTATES, up to the ceiling the raws themselves allow. Two
    // clears every pool bigger than one; a slot the raws restrict to a single
    // named body (Father Maell, and no other WardType::PriestOfTheFlame
    // exists to draw) is held to that one body and no fewer.
    CHECK(bucket.giverIds.size() >= std::min<std::size_t>(2, giverCeiling));
    CHECK(bucket.targetIds.size() >= std::min<std::size_t>(2, targetCeiling));
}

}  // namespace

TEST_CASE(
    "a large sample of radiant offers, per faction and per template, does not read as the same mad-lib repeated") {
    REQUIRE(raws().loaded());
    REQUIRE(factions().loaded());
    const WardPopulation& ward = testfix::wardAt(8, 0);

    // A LARGE SAMPLE: kSampleDays days of a board apiece, up to
    // kRadiantObjectivesPerDay offers a day, over every template the raws
    // currently load. The ward itself never ticks across this loop --
    // wardAt(8, 0) is read-only and shared with every other case in the
    // process -- so a giver's own place never moves between draws, and every
    // axis of variation in the sentence traces to the board's own draw.
    constexpr std::int32_t kSampleDays = 900;

    std::map<std::string, RepetitionBucket> byTemplate;
    std::map<std::int32_t, RepetitionBucket> byFaction;
    RepetitionBucket overall;

    for (std::int32_t day = 0; day < kSampleDays; ++day) {
        RadiantBoard board;
        board.refresh(day, testfix::kSeed, raws(), ward);
        for (const RadiantObjective& row : board.objectives()) {
            const WardActor* giver = ward.byId(row.giverActorId);
            REQUIRE(giver != nullptr);
            const std::int32_t faction = giverFactionIndex(giver->type);

            recordInto(byTemplate[row.templateId], row);
            recordInto(byFaction[faction], row);
            recordInto(overall, row);
        }
    }

    // THE SAMPLE ACTUALLY IS LARGE, so anything this case finds below is a
    // real signal and not one unlucky day.
    REQUIRE(overall.briefs.size() > 2000);

    // EVERY TEMPLATE THE RAWS CURRENTLY LOAD GOT DRAWN AT LEAST ONCE. A
    // template that never appears across nine hundred days is a template this
    // sample could not judge -- this line is what would notice a future
    // template whose giver/target pool has quietly gone empty.
    CHECK(byTemplate.size() == raws().templates().size());

    // AT LEAST THREE DISTINCT FACTIONS' WORTH OF GIVERS DREW A BOARD across
    // the sample -- the owner's six templates alone span merchants
    // (shopkeeper), watch and dockhands (the kennel's keepers) givers, plus
    // the deliberately unaffiliated street (wastrel/urchin).
    CHECK(byFaction.size() >= 3);

    // THE CEILINGS every checkNotAMadLib() call below measures the cast
    // against -- see the file header and personCountsIn()'s own comment for
    // why this is the ward's actual headcount and not a constant.
    const std::map<WardType, std::size_t> personCounts = personCountsIn(ward);
    std::size_t totalPersons = 0;
    for (const auto& entry : personCounts) {
        totalPersons += entry.second;
    }

    // THE HEADLINE CLAIM, proved directly against the whole sample and not
    // only inferred from its parts. Every person in the ward could in
    // principle have been drawn into either slot across the whole sample, so
    // both ceilings are the ward's total headcount.
    checkNotAMadLib(overall, "the whole sample", totalPersons, totalPersons);

    // PER TEMPLATE -- the actual sentence shape a "mad-lib" complaint names.
    // The ceiling is THIS template's own authored giverTypes/targetTypes,
    // which is what makes deliver_mission_burial_word's one-body giver
    // (Father Maell, and nobody else) a pass rather than a false alarm.
    for (const auto& entry : byTemplate) {
        const RadiantTemplate* tmpl = templateNamed(entry.first);
        REQUIRE(tmpl != nullptr);
        checkNotAMadLib(entry.second, "template " + entry.first,
                        headcount(personCounts, tmpl->giverTypes),
                        headcount(personCounts, tmpl->targetTypes));
    }

    // PER FACTION -- the grouping this task asked for. The giver ceiling is
    // the ward's headcount over every WardType that presents as this faction;
    // the target slot is never restricted to the giver's own faction (a
    // merchant's fetch can target a serf, a sailor, a carter...), so its
    // ceiling stays the ward's whole headcount, same as the overall bucket.
    for (const auto& entry : byFaction) {
        checkNotAMadLib(entry.second, "faction " + factionLabel(entry.first),
                        headcountForFaction(personCounts, entry.first), totalPersons);
    }
}
