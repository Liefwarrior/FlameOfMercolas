#include "granadad/sim/chargen.hpp"

#include <algorithm>

#include "granadad/sim/social.hpp"

namespace granadad::sim {

std::string_view skillDesignationName(SkillDesignation designation) noexcept {
    switch (designation) {
        case SkillDesignation::Primary:
            return "Primary";
        case SkillDesignation::Major:
            return "Major";
        case SkillDesignation::Minor:
            return "Minor";
        case SkillDesignation::None:
            return "Undesignated";
    }
    return "Undesignated";
}

std::int32_t skillDesignationSlots(SkillDesignation designation) noexcept {
    switch (designation) {
        case SkillDesignation::Primary:
            return kPrimarySkillSlots;
        case SkillDesignation::Major:
            return kMajorSkillSlots;
        case SkillDesignation::Minor:
            return kMinorSkillSlots;
        case SkillDesignation::None:
            return 0;
    }
    return 0;
}

std::int32_t startingLevelFor(SkillDesignation designation) noexcept {
    switch (designation) {
        case SkillDesignation::Primary:
            return kPrimaryStartLevel;
        case SkillDesignation::Major:
            return kMajorStartLevel;
        case SkillDesignation::Minor:
            return kMinorStartLevel;
        case SkillDesignation::None:
            return 0;
    }
    return 0;
}

SkillPick* Chargen::findPick(std::string_view skillId) noexcept {
    const auto found = std::lower_bound(
        picks_.begin(), picks_.end(), skillId,
        [](const SkillPick& pick, std::string_view probe) { return pick.id < probe; });
    if (found == picks_.end() || found->id != skillId) {
        return nullptr;
    }
    return &*found;
}

const SkillPick* Chargen::findPick(std::string_view skillId) const noexcept {
    const auto found = std::lower_bound(
        picks_.begin(), picks_.end(), skillId,
        [](const SkillPick& pick, std::string_view probe) { return pick.id < probe; });
    if (found == picks_.end() || found->id != skillId) {
        return nullptr;
    }
    return &*found;
}

bool Chargen::designate(std::string_view skillId, SkillDesignation tier,
                        const SkillTrack& raws) noexcept {
    if (tier == SkillDesignation::None) {
        // Not a slot -- clear() is the undesignate path.
        return false;
    }
    const SkillTrack::Entry* entry = raws.find(skillId);
    if (entry == nullptr) {
        return false;
    }
    if (entry->aptitudeTier == AptitudeTier::Flame) {
        // THE FLAME is Gabri's own Source track, not a skill a fresh arrival
        // picks off a sheet -- see this file's header.
        return false;
    }

    SkillPick* existing = findPick(skillId);
    if (existing != nullptr && existing->tier == tier) {
        return true;  // already exactly here
    }

    const std::int32_t cap = skillDesignationSlots(tier);
    const std::int32_t filled = slotsFilled(tier);
    if (filled >= cap) {
        // The tier is full. A skill already holding a DIFFERENT tier does not
        // get to bump one of these out -- the caller clears a slot first,
        // same as a player would have to on the sheet.
        return false;
    }

    if (existing != nullptr) {
        existing->tier = tier;  // move, freeing its old tier's slot
        return true;
    }

    SkillPick fresh;
    fresh.id = std::string(skillId);
    fresh.tier = tier;
    const auto pos =
        std::lower_bound(picks_.begin(), picks_.end(), fresh,
                         [](const SkillPick& a, const SkillPick& b) { return a.id < b.id; });
    picks_.insert(pos, std::move(fresh));
    return true;
}

bool Chargen::clear(std::string_view skillId) noexcept {
    const auto pos = std::lower_bound(
        picks_.begin(), picks_.end(), skillId,
        [](const SkillPick& pick, std::string_view probe) { return pick.id < probe; });
    if (pos == picks_.end() || pos->id != skillId) {
        return false;
    }
    picks_.erase(pos);
    return true;
}

SkillDesignation Chargen::designationOf(std::string_view skillId) const noexcept {
    const SkillPick* pick = findPick(skillId);
    return pick == nullptr ? SkillDesignation::None : pick->tier;
}

std::int32_t Chargen::slotsFilled(SkillDesignation tier) const noexcept {
    std::int32_t count = 0;
    for (const SkillPick& pick : picks_) {
        if (pick.tier == tier) {
            ++count;
        }
    }
    return count;
}

std::int32_t Chargen::slotsRemaining(SkillDesignation tier) const noexcept {
    return skillDesignationSlots(tier) - slotsFilled(tier);
}

bool Chargen::skillsComplete() const noexcept {
    return slotsRemaining(SkillDesignation::Primary) == 0 &&
           slotsRemaining(SkillDesignation::Major) == 0 &&
           slotsRemaining(SkillDesignation::Minor) == 0;
}

std::int32_t Chargen::attributeBonus(AttributeId attribute) const noexcept {
    return attributeBonus_[static_cast<std::size_t>(attribute)];
}

std::int32_t Chargen::attributePointsSpent() const noexcept {
    std::int32_t total = 0;
    for (const std::int32_t bonus : attributeBonus_) {
        total += bonus;
    }
    return total;
}

bool Chargen::spendAttributePoints(AttributeId attribute, std::int32_t delta) noexcept {
    if (delta == 0) {
        return true;
    }
    const std::size_t index = static_cast<std::size_t>(attribute);
    const std::int32_t nextForAttribute = attributeBonus_[index] + delta;
    if (nextForAttribute < 0 || nextForAttribute > kAttributeBonusPerAttributeCap) {
        return false;
    }
    const std::int32_t nextTotal = attributePointsSpent() + delta;
    if (nextTotal < 0 || nextTotal > kAttributeBonusPool) {
        return false;
    }
    attributeBonus_[index] = nextForAttribute;
    return true;
}

AttributeBlock Chargen::apply(SkillTrack& track) const {
    for (const SkillPick& pick : picks_) {
        // setLevel()'s [[nodiscard]] bool reports "did the raws know this
        // id" -- true for every pick designate() ever accepted against the
        // SAME raws. A caller handing apply() a DIFFERENT SkillTrack than the
        // one designations were validated against is the one way this can
        // come back false; that skill is silently left at whatever `track`
        // already had it, the same "unknown id is a no-op, not a crash" rule
        // SkillTrack's own setters and social.cpp's SkillTrack::use() follow.
        (void)track.setLevel(pick.id, startingLevelFor(pick.tier));
    }
    AttributeBlock block;
    for (std::size_t i = 0; i < kAttributeCount; ++i) {
        const AttributeId attribute = static_cast<AttributeId>(i);
        block.setValue(attribute, kAttributeBase + attributeBonus_[i]);
    }
    return block;
}

}  // namespace granadad::sim
