// The sheet before the sheet: three tiers of skill, a bonus-point pool for
// attributes, and the arithmetic that turns both into a real SkillTrack and a
// real AttributeBlock. No UI, no map, no window -- see chargen.hpp's own
// header for exactly what this proves and what it deliberately leaves alone.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <tuple>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/attributes.hpp"
#include "granadad/sim/chargen.hpp"
#include "granadad/sim/social.hpp"

using namespace granadad::sim;

namespace {

[[nodiscard]] SkillTrack raws() {
    SkillTrack track = SkillTrack::load(granadad::content::contentDir());
    REQUIRE(track.loaded());
    return track;
}

}  // namespace

// ===========================================================================
// designation
// ===========================================================================

TEST_CASE("designate refuses a skill the raws do not know, and changes nothing") {
    const SkillTrack track = raws();
    Chargen sheet;

    CHECK_FALSE(sheet.designate("persuasion", SkillDesignation::Primary, track));
    CHECK(sheet.designationOf("persuasion") == SkillDesignation::None);
    CHECK(sheet.slotsFilled(SkillDesignation::Primary) == 0);
    CHECK(sheet.picks().empty());
}

TEST_CASE("designate refuses SkillDesignation::None -- that is clear()'s job") {
    const SkillTrack track = raws();
    Chargen sheet;
    CHECK_FALSE(sheet.designate("sidearms", SkillDesignation::None, track));
    CHECK(sheet.designationOf("sidearms") == SkillDesignation::None);
}

TEST_CASE("THE FLAME never lands on the sheet, at any tier") {
    // content/raws/skills/skills.json's own note calls the_flame
    // "Gabri-unique Source track (ability-unlock effects out of scope this
    // pass)" -- aptitudeTier FLAME is the tag this file's header explains.
    const SkillTrack track = raws();
    REQUIRE(track.aptitudeTier("the_flame") == AptitudeTier::Flame);

    Chargen sheet;
    CHECK_FALSE(sheet.designate("the_flame", SkillDesignation::Primary, track));
    CHECK_FALSE(sheet.designate("the_flame", SkillDesignation::Major, track));
    CHECK_FALSE(sheet.designate("the_flame", SkillDesignation::Minor, track));
    CHECK(sheet.designationOf("the_flame") == SkillDesignation::None);
}

TEST_CASE("three Primary slots, no more") {
    const SkillTrack track = raws();
    Chargen sheet;

    CHECK(sheet.designate("sidearms", SkillDesignation::Primary, track));
    CHECK(sheet.designate("bladework", SkillDesignation::Primary, track));
    CHECK(sheet.designate("open_hand", SkillDesignation::Primary, track));
    CHECK(sheet.slotsFilled(SkillDesignation::Primary) == kPrimarySkillSlots);
    CHECK(sheet.slotsRemaining(SkillDesignation::Primary) == 0);

    // The fourth is refused, and the first three are untouched by the refusal.
    CHECK_FALSE(sheet.designate("lancework", SkillDesignation::Primary, track));
    CHECK(sheet.designationOf("lancework") == SkillDesignation::None);
    CHECK(sheet.slotsFilled(SkillDesignation::Primary) == kPrimarySkillSlots);
}

TEST_CASE("designating a skill already at that tier is a no-op success") {
    const SkillTrack track = raws();
    Chargen sheet;
    REQUIRE(sheet.designate("sidearms", SkillDesignation::Primary, track));
    CHECK(sheet.designate("sidearms", SkillDesignation::Primary, track));
    CHECK(sheet.slotsFilled(SkillDesignation::Primary) == 1);
    CHECK(sheet.picks().size() == 1);
}

TEST_CASE("designating a skill at a new tier MOVES it and frees the old slot") {
    const SkillTrack track = raws();
    Chargen sheet;
    REQUIRE(sheet.designate("sidearms", SkillDesignation::Primary, track));
    REQUIRE(sheet.slotsFilled(SkillDesignation::Primary) == 1);

    CHECK(sheet.designate("sidearms", SkillDesignation::Major, track));
    CHECK(sheet.designationOf("sidearms") == SkillDesignation::Major);
    CHECK(sheet.slotsFilled(SkillDesignation::Primary) == 0);
    CHECK(sheet.slotsFilled(SkillDesignation::Major) == 1);
    // Still one pick, not two -- it moved, it did not duplicate.
    CHECK(sheet.picks().size() == 1);
}

TEST_CASE("a full tier still lets an ALREADY-DESIGNATED skill move within it, "
          "but never lets a stranger in") {
    const SkillTrack track = raws();
    Chargen sheet;
    REQUIRE(sheet.designate("sidearms", SkillDesignation::Primary, track));
    REQUIRE(sheet.designate("bladework", SkillDesignation::Primary, track));
    REQUIRE(sheet.designate("open_hand", SkillDesignation::Primary, track));
    REQUIRE(sheet.slotsRemaining(SkillDesignation::Primary) == 0);

    // Re-affirming one already in the full tier still succeeds (no-op path).
    CHECK(sheet.designate("sidearms", SkillDesignation::Primary, track));
    // A skill with no designation cannot muscle into the full tier.
    CHECK_FALSE(sheet.designate("lancework", SkillDesignation::Primary, track));
}

TEST_CASE("clear frees a slot, and reports whether it had one") {
    const SkillTrack track = raws();
    Chargen sheet;
    CHECK_FALSE(sheet.clear("sidearms"));  // never designated

    REQUIRE(sheet.designate("sidearms", SkillDesignation::Minor, track));
    CHECK(sheet.clear("sidearms"));
    CHECK(sheet.designationOf("sidearms") == SkillDesignation::None);
    CHECK(sheet.slotsFilled(SkillDesignation::Minor) == 0);
    CHECK_FALSE(sheet.clear("sidearms"));  // already gone
}

TEST_CASE("skillsComplete is true only once every tier is full") {
    const SkillTrack track = raws();
    Chargen sheet;
    CHECK_FALSE(sheet.skillsComplete());

    const char* primary[] = {"sidearms", "bladework", "open_hand"};
    const char* major[] = {"lancework", "heavy_arms", "shieldwall"};
    const char* minor[] = {"harness", "grit", "kit_keeping",
                           "mixtures", "streetwise", "cracksmanship"};
    for (const char* id : primary) {
        REQUIRE(sheet.designate(id, SkillDesignation::Primary, track));
    }
    CHECK_FALSE(sheet.skillsComplete());
    for (const char* id : major) {
        REQUIRE(sheet.designate(id, SkillDesignation::Major, track));
    }
    CHECK_FALSE(sheet.skillsComplete());
    for (const char* id : minor) {
        REQUIRE(sheet.designate(id, SkillDesignation::Minor, track));
    }
    CHECK(sheet.skillsComplete());

    // Freeing one slot un-completes the sheet again.
    CHECK(sheet.clear("streetwise"));
    CHECK_FALSE(sheet.skillsComplete());
}

// ===========================================================================
// applying to a real SkillTrack
// ===========================================================================

TEST_CASE("apply writes starting levels into the real SkillTrack, and only "
          "for skills this sheet actually designated") {
    SkillTrack track = raws();
    REQUIRE(track.level("sidearms") == 0);
    REQUIRE(track.level("bladework") == 0);
    REQUIRE(track.level("open_hand") == 0);
    REQUIRE(track.level("heavy_arms") == 0);

    Chargen sheet;
    REQUIRE(sheet.designate("sidearms", SkillDesignation::Primary, track));
    REQUIRE(sheet.designate("bladework", SkillDesignation::Major, track));
    REQUIRE(sheet.designate("open_hand", SkillDesignation::Minor, track));

    std::ignore = sheet.apply(track);

    CHECK(track.level("sidearms") == kPrimaryStartLevel);
    CHECK(track.level("bladework") == kMajorStartLevel);
    CHECK(track.level("open_hand") == kMinorStartLevel);
    // Never designated -- left exactly as the fresh raws had it.
    CHECK(track.level("heavy_arms") == 0);
}

TEST_CASE("apply is idempotent: calling it twice does not compound") {
    SkillTrack track = raws();
    Chargen sheet;
    REQUIRE(sheet.designate("sidearms", SkillDesignation::Primary, track));

    std::ignore = sheet.apply(track);
    CHECK(track.level("sidearms") == kPrimaryStartLevel);
    std::ignore = sheet.apply(track);
    CHECK(track.level("sidearms") == kPrimaryStartLevel);

    // setLevel() zeroes uses too, so a re-apply after some play would reset
    // banked progress -- documented behaviour, not a surprise: apply() is a
    // chargen commit, not a top-up.
    CHECK(track.find("sidearms")->uses == 0);
}

TEST_CASE("apply folds the attribute bonus pool onto the base, and nothing "
          "else moves") {
    SkillTrack track = raws();
    Chargen sheet;
    REQUIRE(sheet.spendAttributePoints(AttributeId::Agility, 10));

    const AttributeBlock block = sheet.apply(track);
    CHECK(block.value(AttributeId::Agility) == kAttributeBase + 10);
    CHECK(block.value(AttributeId::Might) == kAttributeBase);
    CHECK(block.value(AttributeId::Vigor) == kAttributeBase);
    CHECK(block.value(AttributeId::Wit) == kAttributeBase);
}

// ===========================================================================
// the attribute bonus pool
// ===========================================================================

TEST_CASE("a fresh sheet spends nothing and has the whole pool") {
    const Chargen sheet;
    CHECK(sheet.attributePointsSpent() == 0);
    CHECK(sheet.attributePointsRemaining() == kAttributeBonusPool);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(kAttributeCount); ++i) {
        CHECK(sheet.attributeBonus(static_cast<AttributeId>(i)) == 0);
    }
}

TEST_CASE("spending clamps at the per-attribute cap and REFUSES rather than "
          "truncating") {
    Chargen sheet;
    CHECK(sheet.spendAttributePoints(AttributeId::Might, kAttributeBonusPerAttributeCap));
    CHECK(sheet.attributeBonus(AttributeId::Might) == kAttributeBonusPerAttributeCap);

    // One more is refused outright -- the bonus does not creep to the cap and
    // silently drop the rest.
    CHECK_FALSE(sheet.spendAttributePoints(AttributeId::Might, 1));
    CHECK(sheet.attributeBonus(AttributeId::Might) == kAttributeBonusPerAttributeCap);
}

TEST_CASE("spending clamps at the pool's own total, across every attribute") {
    Chargen sheet;
    // Exhaust the pool across three attributes without hitting any one
    // attribute's own cap.
    REQUIRE(kAttributeBonusPool <= 3 * kAttributeBonusPerAttributeCap);
    std::int32_t remaining = kAttributeBonusPool;
    const AttributeId order[] = {AttributeId::Might, AttributeId::Agility, AttributeId::Vigor};
    for (const AttributeId attribute : order) {
        const std::int32_t take = std::min(remaining, kAttributeBonusPerAttributeCap);
        if (take <= 0) {
            break;
        }
        REQUIRE(sheet.spendAttributePoints(attribute, take));
        remaining -= take;
    }
    CHECK(sheet.attributePointsRemaining() == 0);
    // The pool is empty even though Wit's own per-attribute cap has room.
    CHECK_FALSE(sheet.spendAttributePoints(AttributeId::Wit, 1));
}

TEST_CASE("spending is reversible, and a refund below zero is refused") {
    Chargen sheet;
    REQUIRE(sheet.spendAttributePoints(AttributeId::Wit, 5));
    CHECK(sheet.spendAttributePoints(AttributeId::Wit, -5));
    CHECK(sheet.attributeBonus(AttributeId::Wit) == 0);
    CHECK(sheet.attributePointsRemaining() == kAttributeBonusPool);

    // Cannot refund past zero.
    CHECK_FALSE(sheet.spendAttributePoints(AttributeId::Wit, -1));
    CHECK(sheet.attributeBonus(AttributeId::Wit) == 0);
}

// ===========================================================================
// attributes.hpp on its own
// ===========================================================================

TEST_CASE("AttributeBlock starts every attribute at the base, and clamps at "
          "both the floor and the ceiling") {
    AttributeBlock block;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(kAttributeCount); ++i) {
        CHECK(block.value(static_cast<AttributeId>(i)) == kAttributeBase);
    }

    block.setValue(AttributeId::Might, 1000);
    CHECK(block.value(AttributeId::Might) == kAttributeCeiling);
    block.setValue(AttributeId::Might, -1000);
    CHECK(block.value(AttributeId::Might) == kAttributeFloor);
}

TEST_CASE("attributeFromRaw parses the raws' own four tokens, and nothing "
          "else -- NONE and typos both read as absent") {
    CHECK(attributeFromRaw("MGT") == AttributeId::Might);
    CHECK(attributeFromRaw("AGI") == AttributeId::Agility);
    CHECK(attributeFromRaw("VIG") == AttributeId::Vigor);
    CHECK(attributeFromRaw("WIT") == AttributeId::Wit);
    CHECK_FALSE(attributeFromRaw("NONE").has_value());
    CHECK_FALSE(attributeFromRaw("mgt").has_value());
    CHECK_FALSE(attributeFromRaw("").has_value());
}

TEST_CASE("aptitudeTierFromRaw parses the raws' own four tokens, and falls "
          "back to Trained rather than crashing on the unknown") {
    CHECK(aptitudeTierFromRaw("FAVORED") == AptitudeTier::Favored);
    CHECK(aptitudeTierFromRaw("TRAINED") == AptitudeTier::Trained);
    CHECK(aptitudeTierFromRaw("NEGLECTED") == AptitudeTier::Neglected);
    CHECK(aptitudeTierFromRaw("FLAME") == AptitudeTier::Flame);
    CHECK(aptitudeTierFromRaw("") == AptitudeTier::Trained);
    CHECK(aptitudeTierFromRaw("favored") == AptitudeTier::Trained);
}
