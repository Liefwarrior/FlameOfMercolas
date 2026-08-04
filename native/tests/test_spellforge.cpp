// Spellcrafting: the pairing table, the cost model, and the bench.
//
// Every number checked here traces to docs/lore/MAGIC-CANON.md section 2,
// which traces to the novel. The most valuable case in the file is the one that
// runs the RULES against the owner's OWN eleven authored craftings: if the
// pairing table this build refuses by ever disagrees with what
// content/raws/spells/spells.json actually ships, one of the two is wrong and
// the shipped file is not the one that is going to move.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/spellbook.hpp"
#include "granadad/sim/spellforge.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

[[nodiscard]] SpellComponent piece(const char* effect, const char* mode, std::int32_t magnitude,
                                   std::int32_t duration) {
    SpellComponent out;
    out.effect = effect;
    out.mode = mode;
    out.magnitude = magnitude;
    out.durationTicks = duration;
    return out;
}

[[nodiscard]] ForgeRequest one(const SpellComponent& component, TargetShape target) {
    ForgeRequest out;
    out.target = target;
    out.range = target == TargetShape::Self ? 0 : (target == TargetShape::Touch ? 1 : 2);
    out.components.push_back(component);
    return out;
}

}  // namespace

// ===========================================================================
// the pairing table
// ===========================================================================

TEST_CASE("heat and tuning are HELD; a wound is DELIVERED; the other five are refused by name") {
    // The nine (axis x shape) pairings, all of them, stated as a table. Four
    // are legal and five are not, and the five are the ones that would load,
    // charge their full difficulty, report success and change nothing.
    CHECK(componentError(piece("TEMPERATURE", "WHILE_ACTIVE", 15, 600)) == ForgeError::None);
    CHECK(componentError(piece("ATTRIBUTE", "WHILE_ACTIVE", 1, 900)) == ForgeError::None);
    CHECK(componentError(piece("VITALITY", "INSTANT", -1, 0)) == ForgeError::None);
    CHECK(componentError(piece("VITALITY", "OVER_TIME", -1, 30)) == ForgeError::None);

    CHECK(componentError(piece("TEMPERATURE", "INSTANT", 15, 0)) ==
          ForgeError::HeldAxisNeedsHold);
    CHECK(componentError(piece("TEMPERATURE", "OVER_TIME", 15, 600)) ==
          ForgeError::HeldAxisNeedsHold);
    CHECK(componentError(piece("ATTRIBUTE", "INSTANT", 1, 0)) == ForgeError::HeldAxisNeedsHold);
    CHECK(componentError(piece("ATTRIBUTE", "OVER_TIME", 1, 600)) ==
          ForgeError::HeldAxisNeedsHold);
    CHECK(componentError(piece("VITALITY", "WHILE_ACTIVE", -1, 600)) ==
          ForgeError::DeliveredAxisCannotHold);
}

TEST_CASE("what else the forge refuses, and why each refusal is a canon argument") {
    // A magnitude of nothing consumes a slot and moves nothing.
    CHECK(componentError(piece("VITALITY", "INSTANT", 0, 0)) == ForgeError::ZeroMagnitude);
    // L98: "even with training it is limited". A live attribute row is read by
    // every check in the game, so the bound is structural.
    CHECK(componentError(piece("ATTRIBUTE", "WHILE_ACTIVE", kAttributeModifierLimit, 900)) ==
          ForgeError::None);
    CHECK(componentError(piece("ATTRIBUTE", "WHILE_ACTIVE", kAttributeModifierLimit + 1, 900)) ==
          ForgeError::AttributeOverLimit);
    CHECK(componentError(piece("ATTRIBUTE", "WHILE_ACTIVE", -kAttributeModifierLimit - 1, 900)) ==
          ForgeError::AttributeOverLimit);
    // A lingering component shorter than its own cadence delivers nothing.
    CHECK(componentError(piece("VITALITY", "OVER_TIME", -1, kOverTimePeriodTicks - 1)) ==
          ForgeError::ShorterThanCadence);
    CHECK(componentError(piece("VITALITY", "OVER_TIME", -1, kOverTimePeriodTicks)) ==
          ForgeError::None);
    CHECK(componentError(piece("TEMPERATURE", "WHILE_ACTIVE", 15, kHeldCadenceTicks - 1)) ==
          ForgeError::ShorterThanCadence);
    // Vocabulary the model does not have.
    CHECK(componentError(piece("SOUL", "WHILE_ACTIVE", 1, 600)) == ForgeError::UnknownAxis);
    CHECK(componentError(piece("VITALITY", "FOREVER", -1, 600)) == ForgeError::UnknownMode);
}

TEST_CASE("the owner's eleven authored craftings all pass the rules this build enforces") {
    // The case that keeps the two honest. If the pairing table and the shipped
    // raws ever disagree, this is where it shows, and the raws win.
    const Spellbook book = Spellbook::load(content::contentDir());
    REQUIRE(book.loaded());
    CHECK(book.size() == 11);
    for (const Spell& spell : book.spells()) {
        CAPTURE(spell.id);
        CHECK(targetShapeOf(spell.target) != TargetShape::Unknown);
        // Every authored row is SELF or TOUCH: canon gates the unbridged link
        // behind the gift, so the vocabulary supports RANGED and the public
        // shelf withholds it.
        CHECK(targetShapeOf(spell.target) != TargetShape::Ranged);
        CHECK(spell.skill == kCraftingSkill);
        REQUIRE_FALSE(spell.components.empty());
        for (const SpellComponent& component : spell.components) {
            CAPTURE(component.effect);
            CAPTURE(component.mode);
            CHECK(componentError(component) == ForgeError::None);
        }
        // And the reach matches the link on every one of them.
        const TargetShape shape = targetShapeOf(spell.target);
        CHECK(((shape == TargetShape::Self && spell.range == 0) ||
               (shape == TargetShape::Touch && spell.range == 1)));
    }
}

// ===========================================================================
// the cost model
// ===========================================================================

TEST_CASE("distance bleeds, magnitude bleeds, and time is transfer too") {
    // L454: "the more you transfer the more is lost to nature, also the further
    // it travels the more is lost."
    const SpellComponent small = piece("VITALITY", "INSTANT", -1, 0);
    const SpellComponent big = piece("VITALITY", "INSTANT", -4, 0);
    CHECK(spellDifficulty({small}, TargetShape::Self, 0, 0) <
          spellDifficulty({big}, TargetShape::Self, 0, 0));
    CHECK(spellDifficulty({small}, TargetShape::Self, 0, 0) <
          spellDifficulty({small}, TargetShape::Touch, 1, 0));
    CHECK(spellDifficulty({small}, TargetShape::Touch, 1, 0) -
              spellDifficulty({small}, TargetShape::Self, 0, 0) ==
          kResistPerTile);

    // L459: the unbridged link takes the gift, and it is priced like it.
    CHECK(spellDifficulty({small}, TargetShape::Ranged, 2, 0) -
              spellDifficulty({small}, TargetShape::Touch, 2, 0) ==
          kResistUnbridged);

    // A trickle pays for every dose it will deliver.
    const SpellComponent shortTrickle = piece("VITALITY", "OVER_TIME", -1, 30);
    const SpellComponent longTrickle = piece("VITALITY", "OVER_TIME", -1, 300);
    CHECK(transferPoints(shortTrickle) == 3);
    CHECK(transferPoints(longTrickle) == 30);
    CHECK(spellDifficulty({shortTrickle}, TargetShape::Touch, 1, 0) <
          spellDifficulty({longTrickle}, TargetShape::Touch, 1, 0));

    // A hold pays per period it keeps the link open, and the period is
    // deliberately coarser -- holding a link is not the same work as pushing
    // fresh transfers through it.
    const SpellComponent hold = piece("TEMPERATURE", "WHILE_ACTIVE", 15, 600);
    CHECK(transferPoints(hold) == 15 * (600 / kHeldPeriodTicks));
    CHECK(transferPoints(piece("TEMPERATURE", "WHILE_ACTIVE", 15, 900)) > transferPoints(hold));
    CHECK(kHeldPeriodTicks > kOverTimePeriodTicks);

    // Width costs what distance costs, per ring.
    CHECK(spellDifficulty({small}, TargetShape::Touch, 1, 1) -
              spellDifficulty({small}, TargetShape::Touch, 1, 0) ==
          kResistPerTile);
}

TEST_CASE("the ceiling rises with the student and never buys everything") {
    CHECK(forgeCeilingFor(0) < forgeCeilingFor(1));
    CHECK(forgeCeilingFor(5) < forgeCeilingFor(9));
    // A novice can reach the nettle-snap and not the deep rows.
    const ForgeRequest sting = one(piece("VITALITY", "INSTANT", -1, 0), TargetShape::Touch);
    CHECK(forgeSpell(sting, 0).ok);
    ForgeRequest huge = one(piece("VITALITY", "OVER_TIME", -20, 900), TargetShape::Ranged);
    huge.range = 6;
    const ForgeResult refused = forgeSpell(huge, 0);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == ForgeError::BeyondSkill);
    // And the same composition is still out of reach a long way up the track,
    // which is what stops the forge being a way round the cost model.
    CHECK_FALSE(forgeSpell(huge, 20).ok);
}

TEST_CASE("a composition is refused for its shape before it is priced") {
    ForgeRequest empty;
    empty.target = TargetShape::Self;
    CHECK(forgeSpell(empty, 10).error == ForgeError::NoComponents);

    ForgeRequest crowded;
    crowded.target = TargetShape::Self;
    for (int i = 0; i < kMaxComponents + 1; ++i) {
        crowded.components.push_back(piece("TEMPERATURE", "WHILE_ACTIVE", 1 + i, 600));
    }
    CHECK(forgeSpell(crowded, 40).error == ForgeError::TooManyComponents);

    // SELF reaches nought tiles, TOUCH exactly one -- an arm or a blade.
    ForgeRequest wrongReach = one(piece("VITALITY", "INSTANT", -1, 0), TargetShape::Self);
    wrongReach.range = 3;
    CHECK(forgeSpell(wrongReach, 40).error == ForgeError::RangeMismatch);

    ForgeRequest held = one(piece("VITALITY", "WHILE_ACTIVE", -1, 600), TargetShape::Touch);
    CHECK(forgeSpell(held, 40).error == ForgeError::DeliveredAxisCannotHold);
}

TEST_CASE("a forged crafting is named by its own shape, so the same one twice is one") {
    const ForgeRequest a = one(piece("VITALITY", "INSTANT", -2, 0), TargetShape::Touch);
    ForgeRequest b = a;
    b.displayName = "A DIFFERENT NAME ENTIRELY";
    CHECK(forgedSpellId(a) == forgedSpellId(b));

    const ForgeRequest other = one(piece("VITALITY", "INSTANT", -3, 0), TargetShape::Touch);
    CHECK(forgedSpellId(a) != forgedSpellId(other));

    // Two pieces in the other order are the same crafting.
    ForgeRequest twoWays;
    twoWays.target = TargetShape::Self;
    twoWays.range = 0;
    twoWays.components.push_back(piece("TEMPERATURE", "WHILE_ACTIVE", 5, 600));
    twoWays.components.push_back(piece("ATTRIBUTE", "WHILE_ACTIVE", 1, 900));
    ForgeRequest swapped;
    swapped.target = TargetShape::Self;
    swapped.range = 0;
    swapped.components.push_back(piece("ATTRIBUTE", "WHILE_ACTIVE", 1, 900));
    swapped.components.push_back(piece("TEMPERATURE", "WHILE_ACTIVE", 5, 600));
    CHECK(forgedSpellId(twoWays) == forgedSpellId(swapped));

    const ForgeResult made = forgeSpell(twoWays, 40);
    REQUIRE(made.ok);
    CHECK(made.spell.id == forgedSpellId(twoWays));
    CHECK(made.spell.skill == kCraftingSkill);
    // A dearer crafting takes longer to be ready for. The cooldown is the cost
    // model again, not a second dial.
    CHECK(made.spell.cooldownTicks > 200);
    CHECK(made.difficulty == spellDifficulty(made.spell));
}

// ===========================================================================
// the bench
// ===========================================================================

TEST_CASE("the bench snaps to a legal shape when the axis changes, and still lets you be wrong") {
    ForgeBench bench;
    bench.reset();
    CHECK(bench.error() == ForgeError::None);

    // Walk the axis field onto a HELD axis: the shape in time follows, because
    // a bench that opened on an illegal composition would start by lying.
    bench.field = 0;
    for (int i = 0; i < 3; ++i) {
        bench.adjust(1);
        if (isHeldAxis(bench.axis)) {
            CHECK(bench.mode == EffectMode::WhileActive);
            CHECK(bench.durationTicks >= kHeldCadenceTicks);
            CHECK(bench.error() == ForgeError::None);
        }
    }

    // The SHAPE field is still free, so a player who insists on a held wound is
    // refused out loud rather than prevented -- being told why is the lesson.
    bench.reset();
    bench.field = 1;
    bench.adjust(2);
    CHECK(bench.mode == EffectMode::WhileActive);
    CHECK(bench.error() == ForgeError::DeliveredAxisCannotHold);
    CHECK_FALSE(forgeErrorReason(bench.error()).empty());

    // The attribute axis is clamped at the bench as well as at the loader.
    bench.reset();
    bench.field = 0;
    while (bench.axis != EffectKind::Attribute) {
        bench.adjust(1);
    }
    bench.field = 2;
    for (int i = 0; i < 20; ++i) {
        bench.adjust(1);
    }
    CHECK(bench.magnitude <= kAttributeModifierLimit);
    CHECK(bench.error() == ForgeError::None);

    // Fields wrap, so holding one direction walks the whole bench.
    bench.field = 0;
    bench.moveField(-1);
    CHECK(bench.field == kForgeFieldCount - 1);
    bench.moveField(1);
    CHECK(bench.field == 0);

    // Every field prints a label and a value; none of them is a question mark.
    for (std::int32_t i = 0; i < kForgeFieldCount; ++i) {
        CHECK(bench.fieldLabel(i) != "?");
        CHECK(bench.fieldValue(i) != "?");
    }
}

TEST_CASE("turning the bench up makes the composition dearer, every time") {
    ForgeBench bench;
    bench.reset();
    const std::int32_t base = bench.difficulty();
    bench.field = 2;
    bench.adjust(1);
    CHECK(bench.difficulty() > base);
    const std::int32_t bigger = bench.difficulty();
    bench.field = 4;
    while (bench.target != TargetShape::Ranged) {
        bench.adjust(1);
    }
    CHECK(bench.difficulty() > bigger);
}

// ===========================================================================
// the grimoire
// ===========================================================================

TEST_CASE("the grimoire keeps taught and made apart, sorted, and hashed") {
    const Spellbook book = Spellbook::load(content::contentDir());
    REQUIRE(book.loaded());

    Grimoire held;
    CHECK(held.size() == 0);
    CHECK(held.learn(book.spells().front()));
    // The same crafting twice is one crafting.
    CHECK_FALSE(held.learn(book.spells().front()));
    CHECK(held.learnedCount() == 1);
    CHECK(held.craftedCount() == 0);
    CHECK(held.knows(book.spells().front().id));
    CHECK_FALSE(held.knows("no-such-crafting"));

    const ForgeResult made =
        forgeSpell(one(piece("VITALITY", "INSTANT", -1, 0), TargetShape::Touch), 5);
    REQUIRE(made.ok);
    CHECK(held.inscribe(made.spell));
    CHECK(held.craftedCount() == 1);
    CHECK(held.learnedCount() == 1);
    CHECK(held.size() == 2);

    // Sorted by id, always, so two runs that learned the same things in a
    // different order hash the same.
    for (const Spell& spell : book.spells()) {
        held.learn(spell);
    }
    for (std::size_t i = 1; i < held.spells().size(); ++i) {
        CHECK(held.spells()[i - 1].id < held.spells()[i].id);
    }

    Grimoire other;
    for (std::size_t i = held.spells().size(); i > 0; --i) {
        other.learn(held.spells()[i - 1]);
    }
    // Learned back to front, and the inscribed one re-learned rather than
    // inscribed, so the CRAFTED count differs and the hash must say so.
    HashSink a(1);
    HashSink b(1);
    held.hashInto(a);
    other.hashInto(b);
    CHECK(a.finished() != b.finished());

    Grimoire same;
    for (std::size_t i = held.spells().size(); i > 0; --i) {
        const Spell& spell = held.spells()[i - 1];
        if (spell.id.rfind("forged.", 0) == 0) {
            same.inscribe(spell);
        } else {
            same.learn(spell);
        }
    }
    HashSink c(1);
    same.hashInto(c);
    CHECK(a.finished() == c.finished());
}
