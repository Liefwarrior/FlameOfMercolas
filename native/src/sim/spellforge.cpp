#include "granadad/sim/spellforge.hpp"

#include <algorithm>

namespace granadad::sim {

namespace {

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

/// Absolute value that cannot trip on INT_MIN: every magnitude here is small,
/// and a clamp is cheaper than an argument about undefined behaviour.
[[nodiscard]] std::int32_t magnitudeOf(std::int32_t value) noexcept {
    return value < 0 ? -std::max(value, -1000000) : std::min(value, 1000000);
}

}  // namespace

// ---------------------------------------------------------------------------
// vocabulary
// ---------------------------------------------------------------------------

EffectKind effectKindOf(std::string_view raw) noexcept {
    if (raw == "TEMPERATURE") {
        return EffectKind::Temperature;
    }
    if (raw == "VITALITY") {
        return EffectKind::Vitality;
    }
    if (raw == "ATTRIBUTE") {
        return EffectKind::Attribute;
    }
    return EffectKind::Unknown;
}

EffectMode effectModeOf(std::string_view raw) noexcept {
    if (raw == "INSTANT") {
        return EffectMode::Instant;
    }
    if (raw == "OVER_TIME") {
        return EffectMode::OverTime;
    }
    if (raw == "WHILE_ACTIVE") {
        return EffectMode::WhileActive;
    }
    return EffectMode::Unknown;
}

TargetShape targetShapeOf(std::string_view raw) noexcept {
    if (raw == "SELF") {
        return TargetShape::Self;
    }
    if (raw == "TOUCH") {
        return TargetShape::Touch;
    }
    if (raw == "RANGED") {
        return TargetShape::Ranged;
    }
    return TargetShape::Unknown;
}

std::string_view effectKindKey(EffectKind kind) noexcept {
    switch (kind) {
        case EffectKind::Temperature:
            return "TEMPERATURE";
        case EffectKind::Vitality:
            return "VITALITY";
        case EffectKind::Attribute:
            return "ATTRIBUTE";
        case EffectKind::Unknown:
            break;
    }
    return "?";
}

std::string_view effectModeKey(EffectMode mode) noexcept {
    switch (mode) {
        case EffectMode::Instant:
            return "INSTANT";
        case EffectMode::OverTime:
            return "OVER_TIME";
        case EffectMode::WhileActive:
            return "WHILE_ACTIVE";
        case EffectMode::Unknown:
            break;
    }
    return "?";
}

std::string_view targetShapeKey(TargetShape target) noexcept {
    switch (target) {
        case TargetShape::Self:
            return "SELF";
        case TargetShape::Touch:
            return "TOUCH";
        case TargetShape::Ranged:
            return "RANGED";
        case TargetShape::Unknown:
            break;
    }
    return "?";
}

// --- the same three vocabularies, in words -----------------------------------
//
// Every one of these is MAGIC-CANON.md's own noun for the thing. Nothing here
// is drawn from the raws' spelling, and nothing here is fed back into the raws:
// see the header on why the keys and the words are two different functions.

std::string_view effectKindWord(EffectKind kind) noexcept {
    switch (kind) {
        case EffectKind::Temperature:
            return "HEAT";
        case EffectKind::Vitality:
            return "A WOUND";
        case EffectKind::Attribute:
            return "TUNING";
        case EffectKind::Unknown:
            break;
    }
    return "NOTHING";
}

std::string_view effectModeWord(EffectMode mode) noexcept {
    switch (mode) {
        case EffectMode::Instant:
            return "AT ONCE";
        case EffectMode::OverTime:
            return "A TRICKLE";
        case EffectMode::WhileActive:
            return "A HOLD";
        case EffectMode::Unknown:
            break;
    }
    return "NOTHING";
}

std::string_view targetShapeWord(TargetShape target) noexcept {
    switch (target) {
        case TargetShape::Self:
            return "YOURSELF";
        case TargetShape::Touch:
            return "A TOUCH";
        case TargetShape::Ranged:
            // Canon's word for a link with no bridge under it. "RANGED" is the
            // raws' key and stays in the raws.
            return "OPEN AIR";
        case TargetShape::Unknown:
            break;
    }
    return "NOWHERE";
}

std::string durationWords(std::int32_t seconds) {
    if (seconds <= 0) {
        // "NO TIME", not "AT ONCE", and the frame is what said so. A one-off
        // forces the clock to zero, so the panel drew SHAPE and LASTS with the
        // same two words one above the other and looked like it had repeated
        // itself. They are answers to two different questions and now read as
        // two: the shape is AT ONCE, and what it lasts is NO TIME.
        return "NO TIME";
    }
    const std::int32_t minutes = seconds / 60;
    const std::int32_t rest = seconds % 60;
    // THE COLUMN IS TWENTY GLYPHS WIDE AT EVERY RESOLUTION THIS GAME RUNS AT --
    // dialogue_view.cpp sizes it from the frame width and the font scales with
    // the height, so (width - 2*margin) / 3 / advance comes out 20 at 320x180,
    // 640x360, 960x540 and 1280x720 alike, and drawDialogue TRUNCATES past it.
    // "LASTS: " spends seven of those, so a value has thirteen. Standing values
    // are spelled out; only the compound needs shortening to fit, and the
    // longest one it can produce -- "14 MIN 50 SEC" -- is exactly thirteen.
    if (minutes == 0) {
        return std::to_string(rest) + " SECONDS";
    }
    if (rest == 0) {
        return std::to_string(minutes) + (minutes == 1 ? " MINUTE" : " MINUTES");
    }
    return std::to_string(minutes) + " MIN " + std::to_string(rest) + " SEC";
}

bool isHeldAxis(EffectKind kind) noexcept {
    // Heat and the body's own tuning are held; a wound is delivered. See the
    // header on why that is a statement about what code reads, not a taste.
    return kind == EffectKind::Temperature || kind == EffectKind::Attribute;
}

// ---------------------------------------------------------------------------
// the cost model
// ---------------------------------------------------------------------------

std::int32_t transferPoints(const SpellComponent& component) noexcept {
    const std::int32_t magnitude = magnitudeOf(component.magnitude);
    switch (effectModeOf(component.mode)) {
        case EffectMode::Instant:
            return magnitude;
        case EffectMode::OverTime:
            // Every dose it will deliver, so a long trickle is honestly dearer.
            return magnitude * std::max(1, component.durationTicks / kOverTimePeriodTicks);
        case EffectMode::WhileActive:
            // Every period it keeps the link open. Coarser on purpose.
            return magnitude * std::max(1, component.durationTicks / kHeldPeriodTicks);
        case EffectMode::Unknown:
            break;
    }
    return magnitude;
}

std::int32_t spellDifficulty(const std::vector<SpellComponent>& components, TargetShape target,
                             std::int32_t range, std::int32_t areaRadius) noexcept {
    std::int32_t total = 0;
    for (const SpellComponent& component : components) {
        total += transferPoints(component) * kResistPerTransferPoint;
    }
    // Distance bleeds, and so does width: a crafting spread over an area is
    // paying the distance term once per ring it has to reach.
    total += std::max(0, range) * kResistPerTile;
    total += std::max(0, areaRadius) * kResistPerTile;
    if (target == TargetShape::Ranged) {
        total += kResistUnbridged;
    }
    return total;
}

std::int32_t spellDifficulty(const Spell& spell) noexcept {
    return spellDifficulty(spell.components, targetShapeOf(spell.target), spell.range,
                           spell.areaRadius);
}

// ---------------------------------------------------------------------------
// refusals
// ---------------------------------------------------------------------------

std::string_view forgeErrorName(ForgeError error) noexcept {
    switch (error) {
        case ForgeError::None:
            return "none";
        case ForgeError::NoComponents:
            return "no components";
        case ForgeError::TooManyComponents:
            return "too many components";
        case ForgeError::UnknownAxis:
            return "unknown axis";
        case ForgeError::UnknownMode:
            return "unknown mode";
        case ForgeError::UnknownTarget:
            return "unknown target";
        case ForgeError::ZeroMagnitude:
            return "zero magnitude";
        case ForgeError::HeldAxisNeedsHold:
            return "held axis needs a hold";
        case ForgeError::DeliveredAxisCannotHold:
            return "delivered axis cannot hold";
        case ForgeError::AttributeOverLimit:
            return "attribute over limit";
        case ForgeError::ShorterThanCadence:
            return "shorter than its cadence";
        case ForgeError::RangeMismatch:
            return "range does not match the link";
        case ForgeError::BeyondSkill:
            return "beyond the student";
        case ForgeError::BeyondRank:
            return "beyond the rank";
    }
    return "?";
}

std::string_view forgeErrorReason(ForgeError error) noexcept {
    switch (error) {
        case ForgeError::None:
            return "";
        case ForgeError::NoComponents:
            return "IT MOVES NOTHING.";
        case ForgeError::TooManyComponents:
            return "TOO MANY PARTS TO HOLD AT ONCE.";
        case ForgeError::UnknownAxis:
            return "THAT IS NOT AN AXIS.";
        case ForgeError::UnknownMode:
            return "THAT IS NOT A SHAPE IN TIME.";
        case ForgeError::UnknownTarget:
            return "THAT IS NOT A LINK.";
        case ForgeError::ZeroMagnitude:
            return "NOTHING MOVED IS NOTHING DONE.";
        case ForgeError::HeldAxisNeedsHold:
            return "HEAT AND TUNING ARE HELD, NOT DELIVERED.";
        case ForgeError::DeliveredAxisCannotHold:
            return "A WOUND IS DELIVERED, NOT HELD.";
        case ForgeError::AttributeOverLimit:
            return "NO ONE OFF THIS SHELF IS THAT MUCH STRONGER.";
        case ForgeError::ShorterThanCadence:
            return "IT WOULD END BEFORE IT ARRIVED.";
        case ForgeError::RangeMismatch:
            return "THE REACH DOES NOT MATCH THE LINK.";
        case ForgeError::BeyondSkill:
            return "BEYOND YOUR HANDS TONIGHT.";
        case ForgeError::BeyondRank:
            return "THE WORKSHOP IS NOT OPEN TO YOU.";
    }
    return "";
}

ForgeError componentError(const SpellComponent& component) noexcept {
    const EffectKind kind = effectKindOf(component.effect);
    if (kind == EffectKind::Unknown) {
        return ForgeError::UnknownAxis;
    }
    const EffectMode mode = effectModeOf(component.mode);
    if (mode == EffectMode::Unknown) {
        return ForgeError::UnknownMode;
    }
    if (component.magnitude == 0) {
        return ForgeError::ZeroMagnitude;
    }
    if (isHeldAxis(kind) && mode != EffectMode::WhileActive) {
        return ForgeError::HeldAxisNeedsHold;
    }
    if (!isHeldAxis(kind) && mode == EffectMode::WhileActive) {
        return ForgeError::DeliveredAxisCannotHold;
    }
    if (kind == EffectKind::Attribute &&
        magnitudeOf(component.magnitude) > kAttributeModifierLimit) {
        return ForgeError::AttributeOverLimit;
    }
    if (mode == EffectMode::OverTime && component.durationTicks < kOverTimePeriodTicks) {
        return ForgeError::ShorterThanCadence;
    }
    if (mode == EffectMode::WhileActive && component.durationTicks < kHeldCadenceTicks) {
        return ForgeError::ShorterThanCadence;
    }
    return ForgeError::None;
}

// ---------------------------------------------------------------------------
// composing
// ---------------------------------------------------------------------------

std::int32_t forgeCeilingFor(std::int32_t linkcraftLevel) noexcept {
    // A novice can compose the smallest things on the shelf and nothing more.
    // Twelve plus three a level puts the authored deep rows (close_the_cut,
    // sap_the_step) inside reach at about the level their own minLevel gates
    // them at, and keeps a level-0 student to nettle-snaps.
    return 12 + 3 * std::max(0, linkcraftLevel);
}

std::string forgedSpellId(const ForgeRequest& request) {
    // Derived from the SHAPE, so the same composition forged twice is the same
    // crafting. No counter, no clock, no draw -- an id that came from a
    // sequence number would make a grimoire depend on the order of play and
    // the twin-run gate would have to account for it.
    std::string id = "forged." + lower(targetShapeKey(request.target));
    id += ".r" + std::to_string(std::max(0, request.range));
    id += ".a" + std::to_string(std::max(0, request.areaRadius));
    std::vector<std::string> parts;
    parts.reserve(request.components.size());
    for (const SpellComponent& component : request.components) {
        std::string part = "." + lower(component.effect) + "." + lower(component.mode);
        part += component.magnitude < 0 ? ".m" : ".p";
        part += std::to_string(magnitudeOf(component.magnitude));
        part += ".d" + std::to_string(std::max(0, component.durationTicks));
        parts.push_back(std::move(part));
    }
    // Sorted, because two compositions that differ only in the order the player
    // typed them are the same crafting.
    std::sort(parts.begin(), parts.end());
    for (const std::string& part : parts) {
        id += part;
    }
    return id;
}

ForgeResult forgeSpell(const ForgeRequest& request, std::int32_t linkcraftLevel) {
    ForgeResult out;
    if (request.components.empty()) {
        out.error = ForgeError::NoComponents;
        return out;
    }
    if (static_cast<std::int32_t>(request.components.size()) > kMaxComponents) {
        out.error = ForgeError::TooManyComponents;
        return out;
    }
    if (request.target == TargetShape::Unknown) {
        out.error = ForgeError::UnknownTarget;
        return out;
    }
    // SELF reaches nought tiles, TOUCH reaches exactly one -- an arm or a blade
    // (L459) -- and anything further is the unbridged link the gift is for.
    const bool reachOk = (request.target == TargetShape::Self && request.range == 0) ||
                         (request.target == TargetShape::Touch && request.range == 1) ||
                         (request.target == TargetShape::Ranged && request.range >= 2);
    if (!reachOk || request.areaRadius < 0) {
        out.error = ForgeError::RangeMismatch;
        return out;
    }
    for (const SpellComponent& component : request.components) {
        const ForgeError error = componentError(component);
        if (error != ForgeError::None) {
            out.error = error;
            return out;
        }
    }

    const std::int32_t difficulty =
        spellDifficulty(request.components, request.target, request.range, request.areaRadius);
    if (difficulty > forgeCeilingFor(linkcraftLevel)) {
        out.error = ForgeError::BeyondSkill;
        out.difficulty = difficulty;
        return out;
    }

    out.ok = true;
    out.difficulty = difficulty;
    out.spell.id = forgedSpellId(request);
    out.spell.displayName =
        request.displayName.empty() ? std::string("A CRAFTING OF YOUR OWN") : request.displayName;
    // Cast with the same skill the whole authored shelf is cast with. A forged
    // crafting is not a second magic system.
    out.spell.skill = "linkcraft";
    out.spell.minLevel = linkcraftLevel;
    // The cooldown is the cost model again, not a second dial: a dearer
    // crafting takes longer to be ready for. Floored so nothing is spammable.
    out.spell.cooldownTicks = 200 + difficulty * 20;
    out.spell.target = std::string(targetShapeKey(request.target));
    out.spell.range = request.range;
    out.spell.areaRadius = request.areaRadius;
    out.spell.components = request.components;
    // Sorted the same way the id is, so the row and its id can never disagree
    // about what the crafting is.
    std::sort(out.spell.components.begin(), out.spell.components.end(),
              [](const SpellComponent& a, const SpellComponent& b) {
                  if (a.effect != b.effect) {
                      return a.effect < b.effect;
                  }
                  if (a.mode != b.mode) {
                      return a.mode < b.mode;
                  }
                  if (a.magnitude != b.magnitude) {
                      return a.magnitude < b.magnitude;
                  }
                  return a.durationTicks < b.durationTicks;
              });
    return out;
}

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

void ForgeBench::reset() noexcept {
    active = false;
    field = 0;
    axis = EffectKind::Vitality;
    mode = EffectMode::Instant;
    magnitude = 1;
    durationTicks = 0;
    target = TargetShape::Touch;
}

std::int32_t ForgeBench::range() const noexcept {
    switch (target) {
        case TargetShape::Self:
            return 0;
        case TargetShape::Touch:
            return 1;
        case TargetShape::Ranged:
            return 2;
        case TargetShape::Unknown:
            break;
    }
    return 0;
}

ForgeRequest ForgeBench::request() const {
    ForgeRequest out;
    out.target = target;
    out.range = range();
    out.areaRadius = 0;
    SpellComponent component;
    component.effect = std::string(effectKindKey(axis));
    component.mode = std::string(effectModeKey(mode));
    component.magnitude = magnitude;
    component.durationTicks = durationTicks;
    out.components.push_back(std::move(component));
    return out;
}

ForgeError ForgeBench::error() const noexcept {
    const ForgeRequest composed = request();
    return componentError(composed.components.front());
}

std::int32_t ForgeBench::difficulty() const noexcept {
    const ForgeRequest composed = request();
    return spellDifficulty(composed.components, composed.target, composed.range,
                           composed.areaRadius);
}

void ForgeBench::moveField(std::int32_t delta) noexcept {
    if (delta == 0) {
        return;
    }
    const std::int32_t count = kForgeFieldCount;
    field = ((field + delta) % count + count) % count;
}

void ForgeBench::adjust(std::int32_t delta) noexcept {
    if (delta == 0) {
        return;
    }
    switch (field) {
        case 0: {
            const std::int32_t step = ((static_cast<std::int32_t>(axis) + delta) % 3 + 3) % 3;
            axis = static_cast<EffectKind>(step);
            // Snapped to a shape this axis can legally take. See the header on
            // why the bench does this and the SHAPE field still does not.
            if (isHeldAxis(axis)) {
                mode = EffectMode::WhileActive;
                durationTicks = std::max(durationTicks, kHeldCadenceTicks);
                if (axis == EffectKind::Attribute) {
                    magnitude = std::clamp(magnitude, -kAttributeModifierLimit,
                                           kAttributeModifierLimit);
                    if (magnitude == 0) {
                        magnitude = 1;
                    }
                }
            } else {
                mode = EffectMode::Instant;
                durationTicks = 0;
            }
            break;
        }
        case 1: {
            const std::int32_t step = ((static_cast<std::int32_t>(mode) + delta) % 3 + 3) % 3;
            mode = static_cast<EffectMode>(step);
            // A lingering shape with no time on it delivers nothing, so the
            // bench opens the clock rather than making the player find it.
            if (mode == EffectMode::OverTime) {
                durationTicks = std::max(durationTicks, kOverTimePeriodTicks);
            } else if (mode == EffectMode::WhileActive) {
                durationTicks = std::max(durationTicks, kHeldCadenceTicks);
            }
            break;
        }
        case 2: {
            const std::int32_t limit =
                axis == EffectKind::Attribute ? kAttributeModifierLimit : kBenchMagnitudeLimit;
            magnitude = std::clamp(magnitude + delta, -limit, limit);
            break;
        }
        case 3:
            durationTicks =
                std::clamp(durationTicks + delta * kBenchDurationStep, 0, kBenchDurationLimit);
            break;
        case 4: {
            const std::int32_t step = ((static_cast<std::int32_t>(target) + delta) % 3 + 3) % 3;
            target = static_cast<TargetShape>(step);
            break;
        }
        default:
            break;
    }
}

std::string ForgeBench::fieldLabel(std::int32_t index) const {
    switch (index) {
        case 0:
            return "MOVES";
        case 1:
            return "SHAPE";
        case 2:
            return "HOW MUCH";
        case 3:
            // Canon's question is "how long"; the bench asks it in the verb so
            // the answer has room to be words instead of a tick count. The
            // labels have always paraphrased -- canon's "how far" is ACROSS --
            // and the priest still asks all four in full in his own lines.
            return "LASTS";
        case 4:
            return "ACROSS";
        default:
            break;
    }
    return "?";
}

std::string ForgeBench::fieldValue(std::int32_t index) const {
    // WORDS, NOT KEYS. Every row of this panel used to be the raws' own
    // identifier -- "OVER_TIME", "WHILE_ACTIVE", "RANGED" -- and a duration
    // printed as engine ticks with a T on the end. See the header.
    switch (index) {
        case 0:
            return std::string(effectKindWord(axis));
        case 1:
            return std::string(effectModeWord(mode));
        case 2:
            return std::to_string(magnitude);
        case 3:
            return durationWords(durationTicks);
        case 4:
            return std::string(targetShapeWord(target));
        default:
            break;
    }
    return "?";
}

void ForgeBench::hashInto(HashSink& sink) const {
    sink.put_byte(active ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(field));
    sink.put_byte(static_cast<std::uint32_t>(axis));
    sink.put_byte(static_cast<std::uint32_t>(mode));
    sink.put_int(static_cast<std::uint32_t>(magnitude));
    sink.put_int(static_cast<std::uint32_t>(durationTicks));
    sink.put_byte(static_cast<std::uint32_t>(target));
}

// ---------------------------------------------------------------------------
// the grimoire
// ---------------------------------------------------------------------------

bool Grimoire::insert(const Spell& spell) {
    if (spell.id.empty()) {
        return false;
    }
    const auto at = std::lower_bound(
        spells_.begin(), spells_.end(), spell.id,
        [](const Spell& held, const std::string& probe) { return held.id < probe; });
    if (at != spells_.end() && at->id == spell.id) {
        return false;
    }
    spells_.insert(at, spell);
    return true;
}

bool Grimoire::learn(const Spell& spell) {
    return insert(spell);
}

bool Grimoire::inscribe(const Spell& spell) {
    if (!insert(spell)) {
        return false;
    }
    ++crafted_;
    return true;
}

bool Grimoire::knows(std::string_view id) const noexcept {
    return find(id) != nullptr;
}

const Spell* Grimoire::find(std::string_view id) const noexcept {
    const auto at = std::lower_bound(
        spells_.begin(), spells_.end(), id,
        [](const Spell& held, std::string_view probe) { return held.id < probe; });
    if (at == spells_.end() || at->id != id) {
        return nullptr;
    }
    return &*at;
}

void Grimoire::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(spells_.size()));
    sink.put_int(static_cast<std::uint32_t>(crafted_));
    for (const Spell& spell : spells_) {
        put_string(sink, spell.id);
        sink.put_int(static_cast<std::uint32_t>(spell.minLevel));
        sink.put_int(static_cast<std::uint32_t>(spell.cooldownTicks));
        sink.put_int(static_cast<std::uint32_t>(spell.range));
        sink.put_int(static_cast<std::uint32_t>(spell.areaRadius));
        sink.put_int(static_cast<std::uint32_t>(spell.components.size()));
        for (const SpellComponent& component : spell.components) {
            put_string(sink, component.effect);
            put_string(sink, component.mode);
            sink.put_int(static_cast<std::uint32_t>(component.magnitude));
            sink.put_int(static_cast<std::uint32_t>(component.durationTicks));
            // HELD-EFFECTS BUILD: the param column joins the hash the moment
            // the loader stops dropping it -- which attribute a tuning row
            // moves decides what a cast does, so two runs disagreeing about
            // it would be two different games. DELIBERATE STRUCTURE CHANGE
            // to the live Grimoire hash, the S13 shape: the pinned codec
            // goldens (test_world_hash.cpp) hash fixed byte specs, not this
            // struct, and the live gates compare THIS shape against itself.
            put_string(sink, component.param);
        }
    }
}

}  // namespace granadad::sim
