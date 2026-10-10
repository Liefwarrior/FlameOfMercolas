#include "granadad/sim/brawl.hpp"

#include <algorithm>
#include <string>

namespace granadad::sim {

std::string_view weaponName(Weapon weapon) noexcept {
    switch (weapon) {
        case Weapon::Fists:
            return "fists";
        case Weapon::Improvised:
            return "improvised";
        case Weapon::Blunt:
            return "cudgel";
        case Weapon::Evictor:
            return "the evictor";
        case Weapon::Edged:
            return "edged";
    }
    return "?";
}

std::string_view intentName(Intent intent) noexcept {
    switch (intent) {
        case Intent::Subdue:
            return "subdue";
        case Intent::Harm:
            return "harm";
        case Intent::Kill:
            return "kill";
    }
    return "?";
}

std::string_view fightClassName(FightClass fight) noexcept {
    return fight == FightClass::Brawl ? "brawl" : "lethal";
}

FightClass classifyFight(std::span<const Fighter> fighters) noexcept {
    if (fighters.size() < 2) {
        return FightClass::Brawl;
    }

    bool anyoneMeansHarm = false;
    for (const Fighter& fighter : fighters) {
        // B1: nothing edged is out.
        if (fighter.weapon >= kFirstLethalWeapon) {
            return FightClass::Lethal;
        }
        // B2: nobody means to kill.
        if (fighter.intent >= Intent::Kill) {
            return FightClass::Lethal;
        }
        if (fighter.intent >= Intent::Harm) {
            anyoneMeansHarm = true;
        }
    }

    // B3: nobody is being finished. Checked after the loop because it is a fact
    // about the fight, not about a fighter: a bloodied man among people who
    // only mean to put him out is still a bar fight, and the same man among
    // someone who means him harm is not.
    if (anyoneMeansHarm) {
        for (const Fighter& fighter : fighters) {
            if (isBloodied(fighter)) {
                return FightClass::Lethal;
            }
        }
    }
    return FightClass::Brawl;
}

std::int32_t baseDamage(Weapon weapon) noexcept {
    switch (weapon) {
        case Weapon::Fists:
            return 3;
        case Weapon::Improvised:
            return 5;
        case Weapon::Blunt:
            return 7;
        case Weapon::Evictor:
            // A cudgel's own seven. The head band is the whole of what the
            // forging bought -- see the enum -- so the arithmetic stays a
            // cudgel's and the damage curve nobody re-tuned stays untouched.
            return 7;
        case Weapon::Edged:
            return 11;
    }
    return 0;
}

std::string weaponSheetLine(Weapon weapon) {
    std::string_view name = "?";
    switch (weapon) {
        case Weapon::Fists:
            name = "FISTS";
            break;
        case Weapon::Improvised:
            name = "IMPROVISED";
            break;
        case Weapon::Blunt:
            name = "CUDGEL";
            break;
        case Weapon::Evictor:
            name = "THE EVICTOR";
            break;
        case Weapon::Edged:
            name = "EDGED";
            break;
    }
    const std::int32_t base = baseDamage(weapon);
    std::string line{name};
    line += ' ';
    line += std::to_string(base);
    line += '-';
    line += std::to_string(base + kStrikeVarianceMax);
    // The class, on the lethal line's own polarity so a future weapon cannot
    // read IMPACT on the sheet and EDGE to classifyFight.
    line += weapon >= kFirstLethalWeapon ? " EDGE" : " IMPACT";
    return line;
}

std::int32_t blockedDamage(std::int32_t damage, std::int32_t shieldwallLevel) noexcept {
    if (damage <= 0) {
        return 0;
    }
    const std::int32_t levelSpan = kBlockKeptPercentAtZero - kBlockKeptPercentFloor;
    const std::int32_t level = std::max(0, std::min(shieldwallLevel, levelSpan));
    const std::int32_t keptPercent = kBlockKeptPercentAtZero - level;
    return std::max(1, damage * keptPercent / 100);
}

Blow strike(Weapon weapon, Fighter& target, std::uint64_t roll,
            std::int32_t damageBonus, std::int32_t fatigueTermQ8,
            std::int32_t chargeQ8) noexcept {
    Blow blow;
    // One in eight swings misses outright. A brawl that never whiffs reads as a
    // spreadsheet; one that whiffs half the time reads as broken.
    if ((roll & 0x7U) == 0U) {
        return blow;
    }
    // FATIGUE BUILD: the second whiff band, and only ever a SECOND one -- the
    // 1-in-8 above is the full-pool baseline and is deliberately untouched, so
    // a full pool never buys past what shipped. The band is 0 wide at
    // kFatigueTermFullQ8 and (full-empty)/8 == 16 of 64 at the floor, read off
    // high bits the variance below never looks at. No draw is added: this is
    // the same roll, argued about harder. See strike()'s own header.
    const std::int32_t term = std::clamp(fatigueTermQ8, kFatigueTermEmptyQ8,
                                         kFatigueTermFullQ8);
    const std::int32_t tiredMiss64 = (kFatigueTermFullQ8 - term) / 8;
    if (tiredMiss64 > 0 &&
        static_cast<std::int32_t>((roll >> 32) & 63U) < tiredMiss64) {
        return blow;
    }
    const bool wasBloodied = isBloodied(target);
    // Base, plus 0..2. Small numbers on purpose: the damage curve is what makes
    // a fist fight take a dozen exchanges instead of two. The MGT bonus rides
    // the same scale (0 at base 40, +4 at the ceiling) and the floor keeps a
    // landed blow a blow whatever a future negative bonus does.
    const std::int32_t variance = static_cast<std::int32_t>((roll >> 3) % 3U);
    blow.landed = true;
    // ACTION-COMBAT BUILD: the charge tier scales the ROLLED weapon damage
    // (base + variance) before the MGT bonus and the floor. ((base+variance) *
    // chargeQ8) >> 8: at kSwingChargeQ8 (256) this is base+variance to the bit,
    // so a tap is the shipped blow; at kHardSwingChargeQ8 (512) it doubles. A
    // widening multiply then a truncating shift, once, so no rounding drifts;
    // the bonus and the max(1,..) floor land after, exactly as before, and a
    // weak arm still bruises. No new draw: the tier is a parameter.
    const std::int32_t rolled = baseDamage(weapon) + variance;
    const std::int32_t scaled =
        static_cast<std::int32_t>((static_cast<std::int64_t>(rolled) * chargeQ8) >> 8);
    blow.damage = std::max(1, scaled + damageBonus);
    target.hp = std::max(0, target.hp - blow.damage);
    if (weapon == Weapon::Evictor &&
        ((roll >> kEvictorHeadRollShift) & 0xFFU) < kEvictorHeadBand256) {
        // THE CROWN. Same-roll discipline, third carving: the head band the
        // spec already ruled (kEvictorHeadBand256's own header), read off
        // bits nothing else claims as a primary, on a blow that has already
        // landed through both whiff bands. Zero hp through the same door
        // every beating uses, so Downed, the heal, and the quarter-health
        // stand-up never need to hear the word Evictor.
        target.hp = 0;
        blow.crowned = true;
    }
    blow.bloodied = !wasBloodied && isBloodied(target);
    blow.downed = isDowned(target.hp);
    return blow;
}

}  // namespace granadad::sim
