// The Daggerfall flow's content, proved against the real raws: the nine
// callings, the ward's ten questions, the twelve biography questions, the
// closed effect vocabulary, the pure tally, and the difficulty dagger. See
// chargen_raws.hpp's own header for what refuses and why.

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/chargen_raws.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;

namespace {

[[nodiscard]] SkillTrack skills() {
    SkillTrack track = SkillTrack::load(granadad::content::contentDir());
    REQUIRE(track.loaded());
    return track;
}

[[nodiscard]] FactionRegistry factions() {
    FactionRegistry registry = FactionRegistry::load(granadad::content::contentDir());
    REQUIRE(registry.loaded());
    return registry;
}

[[nodiscard]] NotableRegistry notables() {
    NotableRegistry registry = NotableRegistry::load(granadad::content::contentDir());
    REQUIRE(registry.loaded());
    return registry;
}

[[nodiscard]] CallingRegistry callings() {
    CallingRegistry registry = CallingRegistry::load(granadad::content::contentDir(), skills());
    for (const std::string& error : registry.errors()) {
        MESSAGE(error);
    }
    REQUIRE(registry.loaded());
    return registry;
}

[[nodiscard]] ChargenQuiz quiz() {
    ChargenQuiz loaded = ChargenQuiz::load(granadad::content::contentDir(), callings());
    for (const std::string& error : loaded.errors()) {
        MESSAGE(error);
    }
    REQUIRE(loaded.loaded());
    return loaded;
}

[[nodiscard]] BiographyRegistry biography() {
    BiographyRegistry loaded = BiographyRegistry::load(granadad::content::contentDir(), skills(),
                                                       factions(), notables());
    for (const std::string& error : loaded.errors()) {
        MESSAGE(error);
    }
    REQUIRE(loaded.loaded());
    return loaded;
}

/// Writes `text` to a scratch file and returns its path -- the refusal
/// tests' seam into loadFromFile.
[[nodiscard]] std::filesystem::path scratchFile(const char* name, const std::string& text) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "granadad-chargen-raws-case";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return file;
}

}  // namespace

// ===========================================================================
// the difficulty dagger
// ===========================================================================

TEST_CASE("the dagger defaults to neutral, and neutral is bit-for-bit the old charge") {
    const SkillTrack track = skills();
    CHECK(track.advanceMultiplierQ8() == kDaggerNeutralQ8);
    for (std::int32_t level = 0; level <= 100; ++level) {
        CHECK(track.scaledUsesForLevel(level) == usesForLevel(level));
    }
}

TEST_CASE("the default dagger keeps every existing grind timing: a fresh skill "
          "levels on exactly the flat schedule") {
    SkillTrack track = skills();
    // 4 uses for level 0 -> 1: three do nothing, the fourth levels. The exact
    // arithmetic every grind-timing test in this suite already leans on.
    CHECK_FALSE(track.use("streetwise"));
    CHECK_FALSE(track.use("streetwise"));
    CHECK_FALSE(track.use("streetwise"));
    CHECK(track.use("streetwise"));
    CHECK(track.level("streetwise") == 1);
}

TEST_CASE("dagger boundary math at 77, 256 and 768") {
    SkillTrack track = skills();

    track.setAdvanceMultiplierQ8(kDaggerMaxQ8);  // 3.0x advancement
    // usesForLevel(0) = 4 -> 4 * 256 / 768 = 1 (integer), floored at 1.
    CHECK(track.scaledUsesForLevel(0) == 1);
    // usesForLevel(10) = 24 -> 24 * 256 / 768 = 8.
    CHECK(track.scaledUsesForLevel(10) == 8);

    track.setAdvanceMultiplierQ8(kDaggerMinQ8);  // 0.3x advancement
    // usesForLevel(0) = 4 -> 4 * 256 / 77 = 13 (truncated from 13.29...).
    CHECK(track.scaledUsesForLevel(0) == 13);
    // usesForLevel(10) = 24 -> 24 * 256 / 77 = 79 (truncated from 79.79...).
    CHECK(track.scaledUsesForLevel(10) == 79);

    track.setAdvanceMultiplierQ8(kDaggerNeutralQ8);
    CHECK(track.scaledUsesForLevel(0) == 4);

    // Never free, even at the fastest setting and the cheapest level.
    track.setAdvanceMultiplierQ8(kDaggerMaxQ8);
    for (std::int32_t level = 0; level <= 100; ++level) {
        CHECK(track.scaledUsesForLevel(level) >= 1);
    }
}

TEST_CASE("setAdvanceMultiplierQ8 clamps to the dagger's own range") {
    SkillTrack track = skills();
    track.setAdvanceMultiplierQ8(0);
    CHECK(track.advanceMultiplierQ8() == kDaggerMinQ8);
    track.setAdvanceMultiplierQ8(100000);
    CHECK(track.advanceMultiplierQ8() == kDaggerMaxQ8);
    track.setAdvanceMultiplierQ8(300);
    CHECK(track.advanceMultiplierQ8() == 300);
}

TEST_CASE("a fast dagger levels faster through use() itself, not only on paper") {
    SkillTrack fast = skills();
    fast.setAdvanceMultiplierQ8(kDaggerMaxQ8);
    // Charge at level 0 is 1 use, at level 1 (usesForLevel=6) it is 2.
    CHECK(fast.use("skyrunning"));
    CHECK(fast.level("skyrunning") == 1);
    CHECK_FALSE(fast.use("skyrunning"));
    CHECK(fast.use("skyrunning"));
    CHECK(fast.level("skyrunning") == 2);
}

TEST_CASE("the dagger is hashed: moving it moves the track's hash") {
    SkillTrack neutral = skills();
    SkillTrack moved = skills();
    moved.setAdvanceMultiplierQ8(300);

    HashSink a(1);
    neutral.hashInto(a);
    HashSink b(1);
    moved.hashInto(b);
    CHECK(a.finished() != b.finished());

    // And two identically-set tracks agree -- the twin-run property in
    // miniature.
    SkillTrack movedTwin = skills();
    movedTwin.setAdvanceMultiplierQ8(300);
    HashSink c(1);
    movedTwin.hashInto(c);
    CHECK(b.finished() == c.finished());
}

TEST_CASE("dagger points to Q8: the doc's own line, integerized") {
    // 1.0 + 0.05 x P, Q8, rounded half-up.
    CHECK(daggerMultiplierQ8ForPoints(0) == kDaggerNeutralQ8);
    CHECK(daggerMultiplierQ8ForPoints(kDaggerPointsMin) == 154);   // 0.6x = 153.6
    CHECK(daggerMultiplierQ8ForPoints(kDaggerPointsMax) == 410);   // 1.6x = 409.6
    // Clamped at the P clamp, not the Q8 clamp: past the caps nothing moves.
    CHECK(daggerMultiplierQ8ForPoints(-100) == daggerMultiplierQ8ForPoints(kDaggerPointsMin));
    CHECK(daggerMultiplierQ8ForPoints(100) == daggerMultiplierQ8ForPoints(kDaggerPointsMax));
}

// ===========================================================================
// the callings
// ===========================================================================

TEST_CASE("the nine callings load off the real raws, in the doc's own roster order") {
    const CallingRegistry registry = callings();
    REQUIRE(registry.callings().size() == 9);
    const std::array<const char*, 9> roster = {"dockhand",  "deckhand",   "watch_runner",
                                               "netter",    "mudlark",    "roof_tenant",
                                               "almsbearer", "stallkeep", "copy_clerk"};
    for (std::size_t i = 0; i < roster.size(); ++i) {
        CHECK(registry.callings()[i].id == roster[i]);
    }
}

TEST_CASE("every calling's sheet designates cleanly and spends exactly the pool") {
    const SkillTrack track = skills();
    const CallingRegistry registry = callings();
    for (const CallingTemplate& calling : registry.callings()) {
        CAPTURE(calling.id);
        Chargen sheet;
        CHECK(calling.designateInto(sheet, track));
        CHECK(sheet.slotsFilled(SkillDesignation::Primary) == kPrimarySkillSlots);
        CHECK(sheet.slotsFilled(SkillDesignation::Major) == kMajorSkillSlots);
        CHECK(sheet.attributePointsRemaining() == 0);
    }
}

TEST_CASE("the dockhand's sheet is the doc's, applied through Chargen's own arithmetic") {
    SkillTrack track = skills();
    const CallingRegistry registry = callings();
    const CallingTemplate* dockhand = registry.find("dockhand");
    REQUIRE(dockhand != nullptr);
    CHECK(dockhand->dominantAxis == ChargenAxis::A);
    CHECK_FALSE(dockhand->secondaryAxis.has_value());

    Chargen sheet;
    REQUIRE(dockhand->designateInto(sheet, track));
    const AttributeBlock block = sheet.apply(track);
    CHECK(track.level("grit") == kPrimaryStartLevel);
    CHECK(track.level("heavy_arms") == kMajorStartLevel);
    CHECK(track.level("harness") == kMinorStartLevel);
    // 50/42/50/42 -- MGT/AGI/VIG/WIT, the doc's own totals over base 40.
    CHECK(block.value(AttributeId::Might) == 50);
    CHECK(block.value(AttributeId::Agility) == 42);
    CHECK(block.value(AttributeId::Vigor) == 50);
    CHECK(block.value(AttributeId::Wit) == 42);
}

TEST_CASE("callings refuse loudly on an unknown skill id") {
    const std::filesystem::path file = scratchFile("bad-skill.json", R"({
      "callings": [{
        "id": "dockhand", "name": "Dockhand", "oneLine": "x",
        "axis": {"dominant": "A"},
        "primary": ["grit", "kit_keeping", "persuasion"],
        "major": ["heavy_arms", "fieldcraft", "shieldwall"],
        "minor": [],
        "attributeBonusSpend": {"MGT": 10, "AGI": 2, "VIG": 10, "WIT": 2}
      }]
    })");
    const CallingRegistry registry = CallingRegistry::loadFromFile(file, skills());
    CHECK_FALSE(registry.loaded());
    REQUIRE_FALSE(registry.errors().empty());
    bool named = false;
    for (const std::string& error : registry.errors()) {
        if (error.find("persuasion") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);
}

TEST_CASE("callings refuse THE FLAME, an overfull tier, and a spend off the pool") {
    const std::filesystem::path file = scratchFile("bad-shape.json", R"({
      "callings": [{
        "id": "wrong", "name": "Wrong", "oneLine": "x",
        "axis": {"dominant": "A"},
        "primary": ["the_flame", "grit", "kit_keeping"],
        "major": ["heavy_arms", "fieldcraft", "shieldwall"],
        "minor": ["harness", "streetwise", "seacraft", "fishing", "lancework", "mixtures", "sidearms"],
        "attributeBonusSpend": {"MGT": 20, "AGI": 2, "VIG": 10, "WIT": 2}
      }]
    })");
    const CallingRegistry registry = CallingRegistry::loadFromFile(file, skills());
    CHECK_FALSE(registry.loaded());
    // Three distinct refusals, each by name: the flame, the seventh minor,
    // the 34-point spend with a 20 in it.
    CHECK(registry.errors().size() >= 3);
}

// ===========================================================================
// the quiz
// ===========================================================================

TEST_CASE("the quiz loads: ten questions, the approved axis trio, a full verdict table") {
    const ChargenQuiz loaded = quiz();
    CHECK(loaded.questions().size() == 10);
    REQUIRE(loaded.axes().size() == kChargenAxisCount);
    CHECK(loaded.axes()[0].name == "THE HAND");
    CHECK(loaded.axes()[1].name == "THE MUDLARK");
    CHECK(loaded.axes()[2].name == "THE DISCIPLE");
    CHECK(loaded.pureAt() == 6);
    CHECK(loaded.verdictRow(ChargenAxis::A).pure == "dockhand");
    CHECK(loaded.verdictRow(ChargenAxis::B).pure == "roof_tenant");
    CHECK(loaded.verdictRow(ChargenAxis::C).pure == "almsbearer");
    for (const QuizQuestion& question : loaded.questions()) {
        CHECK(question.answers.size() == kChargenAxisCount);
    }
}

TEST_CASE("the tally is deterministic and matches the doc's table, exhaustively") {
    const ChargenQuiz loaded = quiz();
    const std::size_t questionCount = loaded.questions().size();
    REQUIRE(questionCount == 10);

    // For each question, which answer index scores which axis -- so the
    // exhaustive walk below can choose BY AXIS.
    std::vector<std::array<std::int32_t, kChargenAxisCount>> answerFor(questionCount);
    for (std::size_t q = 0; q < questionCount; ++q) {
        for (std::size_t a = 0; a < loaded.questions()[q].answers.size(); ++a) {
            answerFor[q][static_cast<std::size_t>(loaded.questions()[q].answers[a].axis)] =
                static_cast<std::int32_t>(a);
        }
    }

    // The doc's section 3.3, reimplemented INDEPENDENTLY as the oracle.
    const auto oracle = [](const std::array<std::int32_t, 3>& counts,
                           ChargenAxis lastAxis) -> std::string {
        // Dominant: highest count; tie -> the tied axis Q10 scored, else A > B > C.
        int dom = 0;
        for (int i = 1; i < 3; ++i) {
            if (counts[static_cast<std::size_t>(i)] > counts[static_cast<std::size_t>(dom)]) {
                dom = i;
            }
        }
        if (counts[static_cast<std::size_t>(lastAxis)] == counts[static_cast<std::size_t>(dom)]) {
            dom = static_cast<int>(lastAxis);
        }
        const std::int32_t a = counts[0];
        const std::int32_t b = counts[1];
        const std::int32_t c = counts[2];
        if (dom == 0) {
            if (a >= 6) {
                return "dockhand";
            }
            return b >= c ? "deckhand" : "watch_runner";
        }
        if (dom == 1) {
            if (b >= 6) {
                return "roof_tenant";
            }
            return a >= c ? "netter" : "mudlark";
        }
        if (c >= 6) {
            return "almsbearer";
        }
        return a >= b ? "stallkeep" : "copy_clerk";
    };

    // Every one of the 3^10 = 59049 answer sequences: tally twice (same
    // answers -> same verdict) and check against the oracle.
    std::vector<std::int32_t> chosen(questionCount, 0);
    std::vector<ChargenAxis> axes(questionCount, ChargenAxis::A);
    for (std::int32_t combo = 0; combo < 59049; ++combo) {
        std::int32_t rest = combo;
        std::array<std::int32_t, 3> counts{};
        for (std::size_t q = 0; q < questionCount; ++q) {
            const std::int32_t axis = rest % 3;
            rest /= 3;
            axes[q] = static_cast<ChargenAxis>(axis);
            chosen[q] = answerFor[q][static_cast<std::size_t>(axis)];
            ++counts[static_cast<std::size_t>(axis)];
        }
        const std::optional<QuizTally> tally = tallyQuiz(loaded, chosen);
        REQUIRE(tally.has_value());
        const std::optional<QuizTally> again = tallyQuiz(loaded, chosen);
        REQUIRE(again.has_value());
        CHECK(tally->calling == again->calling);
        CHECK(tally->counts == counts);
        const std::string expected = oracle(counts, axes.back());
        if (tally->calling != expected) {
            CAPTURE(counts[0]);
            CAPTURE(counts[1]);
            CAPTURE(counts[2]);
            CHECK(tally->calling == expected);
            break;
        }
    }
}

TEST_CASE("the tie-break is the doc's own: the last answer's axis wins a tie it is in") {
    const ChargenQuiz loaded = quiz();
    // 5 A, 5 B, with the LAST answer scoring B: B is dominant, secondary is
    // A (5 >= 0) -> netter.
    std::vector<std::int32_t> chosen;
    for (std::size_t q = 0; q < 5; ++q) {
        chosen.push_back(0);  // authored order is A, B, C
    }
    for (std::size_t q = 0; q < 5; ++q) {
        chosen.push_back(1);
    }
    const std::optional<QuizTally> tally = tallyQuiz(loaded, chosen);
    REQUIRE(tally.has_value());
    CHECK(tally->dominant == ChargenAxis::B);
    CHECK(tally->calling == "netter");

    // Same 5/5 split with the last five FIRST: the final answer scores A,
    // so A is dominant, secondary B (5 > 0) -> deckhand.
    std::vector<std::int32_t> flipped;
    for (std::size_t q = 0; q < 5; ++q) {
        flipped.push_back(1);
    }
    for (std::size_t q = 0; q < 5; ++q) {
        flipped.push_back(0);
    }
    const std::optional<QuizTally> other = tallyQuiz(loaded, flipped);
    REQUIRE(other.has_value());
    CHECK(other->dominant == ChargenAxis::A);
    CHECK(other->calling == "deckhand");
}

TEST_CASE("the quiz refuses a verdict whose calling disagrees with its cell") {
    // roof_tenant is B-pure; wiring it into A.pure must refuse at load.
    const std::filesystem::path file = scratchFile("bad-verdict.json", R"({
      "axes": [
        {"code": "A", "id": "hand", "name": "THE HAND"},
        {"code": "B", "id": "mudlark", "name": "THE MUDLARK"},
        {"code": "C", "id": "disciple", "name": "THE DISCIPLE"}
      ],
      "verdicts": {
        "pureAt": 6,
        "A": {"pure": "roof_tenant", "withB": "deckhand", "withC": "watch_runner"},
        "B": {"pure": "dockhand", "withA": "netter", "withC": "mudlark"},
        "C": {"pure": "almsbearer", "withA": "stallkeep", "withB": "copy_clerk"}
      },
      "questions": [{"prompt": "x", "answers": [
        {"axis": "A", "text": "a"}, {"axis": "B", "text": "b"}, {"axis": "C", "text": "c"}
      ]}]
    })");
    const ChargenQuiz loaded = ChargenQuiz::loadFromFile(file, callings());
    CHECK_FALSE(loaded.loaded());
    CHECK_FALSE(loaded.errors().empty());
}

TEST_CASE("tallyQuiz refuses a malformed answer sheet rather than mis-scoring it") {
    const ChargenQuiz loaded = quiz();
    CHECK_FALSE(tallyQuiz(loaded, {}).has_value());
    std::vector<std::int32_t> tooFew(9, 0);
    CHECK_FALSE(tallyQuiz(loaded, tooFew).has_value());
    std::vector<std::int32_t> outOfRange(10, 0);
    outOfRange[3] = 7;
    CHECK_FALSE(tallyQuiz(loaded, outOfRange).has_value());
}

// ===========================================================================
// the biography
// ===========================================================================

TEST_CASE("the biography loads: twelve questions, every effect in the closed vocabulary, "
          "every answer's faction spread zero-sum") {
    const BiographyRegistry loaded = biography();
    REQUIRE(loaded.questions().size() == 12);
    CHECK(loaded.questions().front().id == "B1");
    CHECK(loaded.questions().back().id == "B12");
    for (const BiographyQuestion& question : loaded.questions()) {
        CAPTURE(question.id);
        CHECK(question.answers.size() >= 3);
        for (const BiographyAnswer& answer : question.answers) {
            std::int32_t factionSum = 0;
            for (const ChargenEffect& effect : answer.effects) {
                if (effect.kind == ChargenEffectKind::FactionStanding) {
                    factionSum += effect.amount;
                }
            }
            CHECK(factionSum == 0);
        }
    }
}

TEST_CASE("accumulateBiography is pure: same answers, same effects, every time") {
    const BiographyRegistry loaded = biography();
    std::vector<std::int32_t> chosen = {1, 2, 0, 3, 0, 1, 1, 3, 2, 2, 3, 0};
    const std::optional<ChargenEffects> once = accumulateBiography(loaded, chosen);
    const std::optional<ChargenEffects> twice = accumulateBiography(loaded, chosen);
    REQUIRE(once.has_value());
    REQUIRE(twice.has_value());
    CHECK(*once == *twice);
}

TEST_CASE("the accumulator sums a known selection to the doc's own numbers") {
    const BiographyRegistry loaded = biography();
    // B1 b, B2 c, B3 c, B4 b, B5 d, B6 b, B7 b, B8 a, B9 d, B10 b, B11 c,
    // B12 d -- a roofed, hungry, secret-keeping childhood, chosen because it
    // touches every lever.
    const std::vector<std::int32_t> chosen = {1, 2, 2, 1, 3, 1, 1, 0, 3, 1, 2, 3};
    const std::optional<ChargenEffects> fx = accumulateBiography(loaded, chosen);
    REQUIRE(fx.has_value());

    // Hand-summed from the doc:
    // coin: B3c -10 -> -10.
    CHECK(fx->coinDelta == -10);
    // heat: B3c +5, B4b +10, B6b +5, B8a +15, B10b +5 -> 40.
    CHECK(fx->heat == 40);
    // hpMax: B2c -2, B5d +2, B9d -2 -> -2.
    CHECK(fx->hpMaxDelta == -2);
    // dagger: the biography never touches it.
    CHECK(fx->daggerPoints == 0);

    const auto lookup = [](const std::vector<std::pair<std::string, std::int32_t>>& list,
                           const std::string& key) -> std::int32_t {
        for (const auto& [id, amount] : list) {
            if (id == key) {
                return amount;
            }
        }
        return 0;
    };
    // streetwise: B2c +2, B3c +4, B5d +2, B7b +2, B10b +4, B11c +4, B12d +2
    // -> 20. The first gate run of this suite failed here on 14: the
    // original hand-sum forgot B11c ("read the new names... streetwise +4,
    // linkcraft +2", CHARGEN-DAGGERFALL-DRAFT.md B11) and B12d ("someone is
    // dead on the strand... streetwise +2", doc B12) -- the doc and the
    // shipped biography.json agree on both, so the sum was the stale side.
    CHECK(lookup(fx->skillDeltas, "streetwise") == 20);
    // skyrunning: B2c +6, B4b +4, B6b +6 -> 16.
    CHECK(lookup(fx->skillDeltas, "skyrunning") == 16);
    // linkcraft: B1b +4, B11c +2 -> 6.
    CHECK(lookup(fx->skillDeltas, "linkcraft") == 6);
    // grit: B9d +6 -> 6.
    CHECK(lookup(fx->skillDeltas, "grit") == 6);
    // skyrunners: B1b -2, B2c +6, B4b +4, B6b +4 -> 12; watch: B2c -4,
    // B4b -4, B6b -4 -> -12. The whole spread still sums to zero.
    CHECK(lookup(fx->factionStandings, "skyrunners") == 12);
    CHECK(lookup(fx->factionStandings, "watch") == -12);
    std::int32_t total = 0;
    for (const auto& [id, amount] : fx->factionStandings) {
        total += amount;
    }
    CHECK(total == 0);
    // seeds: B3c redda +10, B2c finch +5, B6b finch +10 -> finch 15,
    // B7b sethra +10, B10b sethra +5 -> 15.
    CHECK(lookup(fx->dispositionSeeds, "redda") == 10);
    CHECK(lookup(fx->dispositionSeeds, "finch") == 15);
    CHECK(lookup(fx->dispositionSeeds, "sethra") == 15);
}

TEST_CASE("applySkillDeltas lands on top of the designated start, through setLevel") {
    SkillTrack track = skills();
    const CallingRegistry registry = callings();
    const CallingTemplate* roofTenant = registry.find("roof_tenant");
    REQUIRE(roofTenant != nullptr);
    Chargen sheet;
    REQUIRE(roofTenant->designateInto(sheet, track));
    (void)sheet.apply(track);
    REQUIRE(track.level("skyrunning") == kPrimaryStartLevel);

    ChargenEffects fx;
    fx.skillDeltas = {{"skyrunning", 6}, {"unheard_of", 3}};
    // One known, one refused -- the honesty count.
    CHECK(applySkillDeltas(track, fx) == 1);
    CHECK(track.level("skyrunning") == kPrimaryStartLevel + 6);
}

TEST_CASE("the biography refuses loudly on an unknown effect kind") {
    const std::filesystem::path file = scratchFile("bad-kind.json", R"({
      "questions": [{
        "id": "B1", "prompt": "x",
        "answers": [{"text": "a", "effects": [{"kind": "grantItem", "target": "knife", "amount": 1}]}]
      }]
    })");
    const BiographyRegistry loaded =
        BiographyRegistry::loadFromFile(file, skills(), factions(), notables());
    CHECK_FALSE(loaded.loaded());
    REQUIRE_FALSE(loaded.errors().empty());
    CHECK(loaded.errors().front().find("grantItem") != std::string::npos);
}

TEST_CASE("the biography refuses an unknown skill, faction and notable, by name") {
    const std::filesystem::path file = scratchFile("bad-targets.json", R"({
      "questions": [{
        "id": "B1", "prompt": "x",
        "answers": [{"text": "a", "effects": [
          {"kind": "skillDelta", "target": "persuasion", "amount": 2},
          {"kind": "factionStanding", "target": "assassins", "amount": 2},
          {"kind": "factionStanding", "target": "watch", "amount": -2},
          {"kind": "actorDisposition", "target": "nobody_of_that_name", "amount": 5}
        ]}]
      }]
    })");
    const BiographyRegistry loaded =
        BiographyRegistry::loadFromFile(file, skills(), factions(), notables());
    CHECK_FALSE(loaded.loaded());
    CHECK(loaded.errors().size() >= 3);
}

TEST_CASE("the biography refuses an answer whose faction spread is not zero-sum") {
    const std::filesystem::path file = scratchFile("bad-sum.json", R"({
      "questions": [{
        "id": "B1", "prompt": "x",
        "answers": [{"text": "a", "effects": [
          {"kind": "factionStanding", "target": "temple", "amount": 4},
          {"kind": "factionStanding", "target": "merchants", "amount": -2}
        ]}]
      }]
    })");
    const BiographyRegistry loaded =
        BiographyRegistry::loadFromFile(file, skills(), factions(), notables());
    CHECK_FALSE(loaded.loaded());
    bool named = false;
    for (const std::string& error : loaded.errors()) {
        if (error.find("sum to 2") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);
}

// ===========================================================================
// the faction seed seam
// ===========================================================================

TEST_CASE("seedStanding writes the row directly, with no rival mirror") {
    const auto registry = std::make_shared<FactionRegistry>(factions());
    const std::int32_t watch = registry->indexOf("watch");
    const std::int32_t skyrunners = registry->indexOf("skyrunners");
    REQUIRE(watch >= 0);
    REQUIRE(skyrunners >= 0);

    FactionLedger seeded;
    seeded.attach(registry);
    seeded.seedStanding(watch, 6);
    CHECK(seeded.standing(watch) == 6);
    // THE POINT: the declared rival did not move. addStanding would have
    // mirrored -3 onto the roofs and double-counted the authored spread.
    CHECK(seeded.standing(skyrunners) == 0);

    FactionLedger mirrored;
    mirrored.attach(registry);
    mirrored.addStanding(watch, 6);
    CHECK(mirrored.standing(skyrunners) == -3);

    // And it clamps, like every standing write does.
    seeded.seedStanding(watch, 1000);
    CHECK(seeded.standing(watch) == kFactionStandingMax);
}
