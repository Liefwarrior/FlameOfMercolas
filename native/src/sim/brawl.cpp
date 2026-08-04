#include "granadad/sim/brawl.hpp"

#include <algorithm>

namespace granadad::sim {

std::string_view weaponName(Weapon weapon) noexcept {
    switch (weapon) {
        case Weapon::Fists:
            return "fists";
        case Weapon::Improvised:
            return "improvised";
        case Weapon::Blunt:
            return "cudgel";
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
        case Weapon::Edged:
            return 11;
    }
    return 0;
}

Blow strike(Weapon weapon, Fighter& target, std::uint64_t roll) noexcept {
    Blow blow;
    // One in eight swings misses outright. A brawl that never whiffs reads as a
    // spreadsheet; one that whiffs half the time reads as broken.
    if ((roll & 0x7U) == 0U) {
        return blow;
    }
    const bool wasBloodied = isBloodied(target);
    // Base, plus 0..2. Small numbers on purpose: the damage curve is what makes
    // a fist fight take a dozen exchanges instead of two.
    const std::int32_t variance = static_cast<std::int32_t>((roll >> 3) % 3U);
    blow.landed = true;
    blow.damage = baseDamage(weapon) + variance;
    target.hp = std::max(0, target.hp - blow.damage);
    blow.bloodied = !wasBloodied && isBloodied(target);
    blow.downed = isDowned(target.hp);
    return blow;
}

}  // namespace granadad::sim
