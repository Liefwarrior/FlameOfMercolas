// THE FIXED SHEETS, PROVED AGAINST THE REAL RAWS AND THE REAL CHARGEN ENGINE.
//
// content/raws/companions/devin.json and gabri.json are hand-authored
// chargenPresets: which skill ids sit at Primary/Major/Minor and how each
// sheet's 24-point attribute bonus pool was spent. This file proves that
// resolving one is not a second, parallel arithmetic living in
// companions.cpp -- every level and every attribute below is checked
// against chargen.hpp's own exported constants (startingLevelFor,
// kPrimaryStartLevel/kMajorStartLevel/kMinorStartLevel, kAttributeBonusPool,
// kAttributeBonusPerAttributeCap) and attributes.hpp's own kAttributeBase,
// never a number this file quotes a second time. If any of those constants
// ever change, this file's own CHECKs move with them automatically; only the
// per-skill tier assignments below are this test's own knowledge.

#include <doctest/doctest.h>

#include <cstdint>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/appearance.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/companions.hpp"
#include "granadad/sim/social.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// Every word either file's prose fields can show is a sentence, the same
/// bar test_character.cpp already holds session.cpp's own strings to: no
/// control characters, and no raw skill/enum id leaking through where a
/// sentence was meant (the exact bug class the standing quality bar exists
/// to catch).
void checkReadableProse(const std::string& text) {
    INFO("text: ", text);
    CHECK_FALSE(text.empty());
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        CHECK(u >= 0x20);
        CHECK(u < 0x7F);
    }
    // "the_flame", "kit_keeping" and friends are the one shape of token this
    // build's raws vocabulary uses that ordinary prose never does -- two
    // lowercase words glued by an underscore. Its presence in a sentence
    // meant for a player to read is exactly session.cpp's own already-fixed
    // "no coin"/"out of stock" bug class.
    CHECK(text.find('_') == std::string::npos);
}

/// Every level a CompanionTemplate hands back is one of the engine's own
/// three tier levels -- there is no fourth number a chargenPreset can
/// legally resolve to.
void checkEveryLevelIsATierLevel(const CompanionTemplate& companion) {
    for (const CompanionSkill& skill : companion.startingSkills()) {
        INFO("skill: ", skill.id, " level: ", skill.level);
        CHECK((skill.level == startingLevelFor(SkillDesignation::Primary) ||
               skill.level == startingLevelFor(SkillDesignation::Major) ||
               skill.level == startingLevelFor(SkillDesignation::Minor)));
    }
}

}  // namespace

TEST_CASE("both companion files load, and every skill id they name is real") {
    const SkillTrack raws = SkillTrack::load(content::contentDir());
    REQUIRE(raws.loaded());

    for (const std::string_view id : {"devin", "gabri"}) {
        INFO("companion: ", id);
        const CompanionTemplate companion = CompanionTemplate::load(content::contentDir(), id);
        REQUIRE(companion.loaded());
        CHECK(companion.id() == id);
        CHECK_FALSE(companion.name().empty());
        REQUIRE_FALSE(companion.startingSkills().empty());
        // Never more than every slot the engine has to offer, and never THE
        // FLAME -- chargen.cpp's own designate() refuses it, and neither
        // file lists it in any tier.
        CHECK(companion.startingSkills().size() <=
              static_cast<std::size_t>(kPrimarySkillSlots + kMajorSkillSlots + kMinorSkillSlots));
        CHECK(companion.startingLevel("the_flame") == 0);
        checkEveryLevelIsATierLevel(companion);

        SkillTrack track = raws;
        const std::int32_t applied = companion.applyStartingSkills(track);
        CHECK(applied == static_cast<std::int32_t>(companion.startingSkills().size()));
        for (const CompanionSkill& skill : companion.startingSkills()) {
            CHECK(raws.find(skill.id) != nullptr);
            CHECK(track.level(skill.id) == skill.level);
        }
    }
}

TEST_CASE("Gabri's sheet fills exactly three Primary, three Major and five of six Minor slots") {
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(gabri.loaded());

    const std::int32_t primaryLevel = startingLevelFor(SkillDesignation::Primary);
    const std::int32_t majorLevel = startingLevelFor(SkillDesignation::Major);
    const std::int32_t minorLevel = startingLevelFor(SkillDesignation::Minor);

    // THE THREE PRIMARY -- grit, bladework, harness. No-nonsense as three
    // blunt, physical picks: take a hit, hit back, wear the armor.
    CHECK(gabri.startingLevel("grit") == primaryLevel);
    CHECK(gabri.startingLevel("bladework") == primaryLevel);
    CHECK(gabri.startingLevel("harness") == primaryLevel);

    // THE THREE MAJOR.
    CHECK(gabri.startingLevel("open_hand") == majorLevel);
    CHECK(gabri.startingLevel("kit_keeping") == majorLevel);
    CHECK(gabri.startingLevel("channeling") == majorLevel);

    // FIVE OF SIX MINOR -- the sheet's own minorSlotNote explains why the
    // sixth is left empty rather than forced onto a thinnest-justified pick.
    CHECK(gabri.startingLevel("sidearms") == minorLevel);
    CHECK(gabri.startingLevel("lancework") == minorLevel);
    CHECK(gabri.startingLevel("heavy_arms") == minorLevel);
    CHECK(gabri.startingLevel("dire_bows") == minorLevel);
    CHECK(gabri.startingLevel("shieldwall") == minorLevel);
    CHECK(gabri.startingSkills().size() == 11);
}

TEST_CASE("no-nonsense is a mechanical fact: what Gabri's sheet actually zeroes out") {
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(gabri.loaded());

    // HE DOES NOT PICK LOCKS. He burns them (novel L2718, gabri.json's own
    // canonAnchor) -- cracksmanship is the skill this build actually
    // levels (social.hpp's kThieverySkill) and Gabri's sheet leaves it
    // undesignated, same as a player who has never touched a lock.
    CHECK(gabri.startingLevel(kThieverySkill) == 0);
    // HE DOES NOT SNEAK. Full lawful immunity means he never has to; every
    // entry in the novel is a front door.
    CHECK(gabri.startingLevel(kRoofSkill) == 0);
    // HE DOES NOT HAGGLE, AND THE DESIGN DOC ALREADY RULED WHY --
    // PROGRESSION-SPEC.md section 2's own north-star guard: "A Streetwise 0
    // Gabri is still obeyed everywhere but pays list price."
    CHECK(gabri.startingLevel(kHaggleSkill) == 0);
    // NO POTIONS, NO BOOK MAGIC -- neither is his register; see the sheet's
    // own rationale for mixtures/linkcraft.
    CHECK(gabri.startingLevel("mixtures") == 0);
    CHECK(gabri.startingLevel("linkcraft") == 0);
    // THE FLAME IS LOCKED AT START FOR EVERYONE, Wielder included --
    // PROGRESSION-SPEC.md section 7, and not this file's invention.
    CHECK(gabri.startingLevel("the_flame") == 0);

    // WHAT HE DOES INVEST IN IS BLUNT AND PHYSICAL: grit, bladework and
    // harness (all Primary) outrank every Major and every Minor pick on the
    // sheet -- taking a hit and finishing the fight (novel L2703-2718) is
    // the one thing "no-nonsense" spends the most on.
    const std::int32_t primaryLevel = startingLevelFor(SkillDesignation::Primary);
    for (const CompanionSkill& skill : gabri.startingSkills()) {
        CHECK(primaryLevel >= skill.level);
    }
}

TEST_CASE("Gabri's and Devin's spent attribute pools both resolve exactly the way Chargen::apply would") {
    // THE IDENTICAL FORMULA (kAttributeBase + spend) reproducing BOTH
    // sheets' numbers, including one this test's author did not write, is
    // the strongest evidence companions.cpp is reading the raws rather than
    // re-deriving something that happens to agree with them once.
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    const CompanionTemplate devin = CompanionTemplate::load(content::contentDir(), "devin");
    REQUIRE(gabri.loaded());
    REQUIRE(devin.loaded());

    // Gabri: MGT 10, AGI 0, VIG 14, WIT 0 spent -- all Might and Vigor, none
    // of the pool anywhere a faster or cleverer build would put it.
    CHECK(gabri.derivedAttribute(AttributeId::Might) == kAttributeBase + 10);
    CHECK(gabri.derivedAttribute(AttributeId::Agility) == kAttributeBase);
    CHECK(gabri.derivedAttribute(AttributeId::Vigor) == kAttributeBase + 14);
    CHECK(gabri.derivedAttribute(AttributeId::Wit) == kAttributeBase);

    // Devin: MGT 0, AGI 12, VIG 4, WIT 8 spent -- built for range and
    // reading a room, per his own file's attributeBonusSpendNote.
    CHECK(devin.derivedAttribute(AttributeId::Might) == kAttributeBase);
    CHECK(devin.derivedAttribute(AttributeId::Agility) == kAttributeBase + 12);
    CHECK(devin.derivedAttribute(AttributeId::Vigor) == kAttributeBase + 4);
    CHECK(devin.derivedAttribute(AttributeId::Wit) == kAttributeBase + 8);

    // AND NEITHER SHEET OVERSPENDS OR EXCEEDS THE PER-ATTRIBUTE CAP -- the
    // exact two limits Chargen::spendAttributePoints enforces live, checked
    // here against the content instead of trusting the note in the JSON.
    for (const CompanionTemplate* companion : {&gabri, &devin}) {
        std::int32_t spent = 0;
        for (std::size_t i = 0; i < kAttributeCount; ++i) {
            const std::int32_t bonus =
                companion->derivedAttribute(static_cast<AttributeId>(i)) - kAttributeBase;
            CHECK(bonus >= 0);
            CHECK(bonus <= kAttributeBonusPerAttributeCap);
            spent += bonus;
        }
        CHECK(spent == kAttributeBonusPool);
    }
}

TEST_CASE("Presence is the one number that tells Gabri and Devin apart on purpose") {
    // PROGRESSION-SPEC.md section 5: Presence is "STATIC 100... the
    // Wielder's social weight is a constant of the world" -- a fact about
    // HOLDING THE TITLE, not a generic attribute. Gabri holds it; Devin's
    // own file deliberately leaves presenceAtStart unauthored (his
    // presenceNote: "an open question for whoever builds that system, not
    // answered here"), which this loader reads back as 0 -- absence, not a
    // claim of zero standing.
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    const CompanionTemplate devin = CompanionTemplate::load(content::contentDir(), "devin");
    REQUIRE(gabri.loaded());
    REQUIRE(devin.loaded());
    CHECK(gabri.presenceAtStart() == 100);
    CHECK(devin.presenceAtStart() == 0);
}

TEST_CASE("Gabri's appearance resolves through the ward's own sprite vocabulary, not a separate one") {
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(gabri.loaded());
    REQUIRE(gabri.appearanceType().has_value());
    CHECK(*gabri.appearanceType() == WardType::PriestOfTheFlame);

    // AND THE OPTION IS FINDABLE THE OTHER WAY TOO -- the same table the
    // CUSTOM path's picker walks names this exact WardType with a real
    // label, not a hole.
    const AppearanceOption* option = appearanceOptionFor(*gabri.appearanceType());
    REQUIRE(option != nullptr);
    CHECK(option->id == "priest_of_the_flame");
    CHECK_FALSE(option->label.empty());
}

TEST_CASE("Devin's file carries no appearanceType, and the loader says so honestly") {
    // gabri.json's own provenance states devin.json is deliberately left
    // unedited by this pass -- confirmed here rather than assumed: a future
    // author giving Devin a look is that file's own call, not silently
    // defaulted to something nobody wrote.
    const CompanionTemplate devin = CompanionTemplate::load(content::contentDir(), "devin");
    REQUIRE(devin.loaded());
    CHECK_FALSE(devin.appearanceType().has_value());
}

TEST_CASE("every word Gabri's sheet can show a player is a sentence, not a raw id") {
    const CompanionTemplate gabri = CompanionTemplate::load(content::contentDir(), "gabri");
    REQUIRE(gabri.loaded());
    checkReadableProse(gabri.bio());
    checkReadableProse(gabri.selfIntro());
    CHECK_FALSE(gabri.epithet().empty());
    CHECK(gabri.archetype() == "no-nonsense");
}

TEST_CASE("a companion id nobody authored answers loaded() == false, not a crash") {
    const CompanionTemplate ghost = CompanionTemplate::load(content::contentDir(), "nobody_wrote_this");
    CHECK_FALSE(ghost.loaded());
    CHECK(ghost.startingSkills().empty());
    CHECK_FALSE(ghost.appearanceType().has_value());
    CHECK(ghost.presenceAtStart() == 0);
}

// ---------------------------------------------------------------------------
// sim/appearance.hpp on its own terms
// ---------------------------------------------------------------------------

TEST_CASE("the eleven playable looks are exactly the ward's adult humanoid trades") {
    const std::vector<AppearanceOption>& options = appearanceOptions();
    REQUIRE(options.size() == 11);

    // ROUND-TRIPS, EVERY ONE: an id parses back to the exact type it was
    // authored against, and the type resolves back to the exact same option.
    for (const AppearanceOption& option : options) {
        INFO("option id: ", option.id);
        CHECK_FALSE(option.id.empty());
        CHECK_FALSE(option.label.empty());
        const std::optional<WardType> parsed = appearanceTypeFromId(option.id);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == option.type);
        const AppearanceOption* found = appearanceOptionFor(option.type);
        REQUIRE(found != nullptr);
        CHECK(found->id == option.id);
    }

    // AND THE FIVE LEFT OFF STAY OFF -- a child and four beasts are real
    // WardType values with real sprites, and neither this test nor the
    // table above may accidentally let one back in.
    CHECK(appearanceOptionFor(WardType::Urchin) == nullptr);
    CHECK(appearanceOptionFor(WardType::Dog) == nullptr);
    CHECK(appearanceOptionFor(WardType::Stray) == nullptr);
    CHECK(appearanceOptionFor(WardType::Cat) == nullptr);
    CHECK(appearanceOptionFor(WardType::Mouse) == nullptr);
}

TEST_CASE("an id nobody authored parses to nothing, not to WardType 0 by accident") {
    CHECK_FALSE(appearanceTypeFromId("urchin").has_value());
    CHECK_FALSE(appearanceTypeFromId("dog").has_value());
    CHECK_FALSE(appearanceTypeFromId("").has_value());
    CHECK_FALSE(appearanceTypeFromId("SERF").has_value());  // case-sensitive, like every other raws id
}
