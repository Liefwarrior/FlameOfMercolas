#include "granadad/render3d/gear_look.hpp"

#include "granadad/sim/brawl.hpp"
#include "granadad/sim/items.hpp"

namespace granadad::render3d {

namespace {

/// What a key draws as, and the slot it may be worn in: a key matches only a
/// row of its own slot, so a model string on the wrong row (a "cutlass" on a
/// head) falls through to the slot's fallback instead of drawing a blade on
/// somebody's skull.
struct Entry {
    std::string_view key;
    sim::ItemSlot slot;
    std::uint8_t value;
};

template <typename E>
[[nodiscard]] constexpr std::uint8_t v(E e) noexcept {
    return static_cast<std::uint8_t>(e);
}

/// THE TABLE: every model key OBLIVION-PLAN's phase 9 contract names, plus
/// the ids of the items.json rows (which carry no model) that the eye can
/// tell apart. Sorted by nothing; searched in order; twenty-odd rows.
constexpr Entry kTable[] = {
    // In the hand.
    {"cudgel", sim::ItemSlot::Hand, v(GearProp::Cudgel)},
    {"the_evictor", sim::ItemSlot::Hand, v(GearProp::Evictor)},
    {"bottle", sim::ItemSlot::Hand, v(GearProp::Bottle)},
    {"knife", sim::ItemSlot::Hand, v(GearProp::Knife)},
    {"dagger", sim::ItemSlot::Hand, v(GearProp::Dagger)},
    {"cutlass", sim::ItemSlot::Hand, v(GearProp::Cutlass)},
    {"boat_hook", sim::ItemSlot::Hand, v(GearProp::BoatHook)},
    {"hatchet", sim::ItemSlot::Hand, v(GearProp::Hatchet)},
    {"mace", sim::ItemSlot::Hand, v(GearProp::Mace)},
    {"maul", sim::ItemSlot::Hand, v(GearProp::Maul)},
    {"boarding_axe", sim::ItemSlot::Hand, v(GearProp::BoardingAxe)},
    // On the other arm.
    {"buckler", sim::ItemSlot::OffHand, v(GearShield::Buckler)},
    {"round_shield", sim::ItemSlot::OffHand, v(GearShield::RoundShield)},
    // On the head.
    {"wool_cap", sim::ItemSlot::Head, v(GearHead::WoolCap)},
    {"iron_pot", sim::ItemSlot::Head, v(GearHead::IronPot)},
    {"hood", sim::ItemSlot::Head, v(GearHead::Hood)},
    {"flat_cap", sim::ItemSlot::Head, v(GearHead::Cap)},
    // On the back.
    {"coat", sim::ItemSlot::Body, v(GearBody::Coat)},
    {"gambeson", sim::ItemSlot::Body, v(GearBody::Gambeson)},
    {"brigandine", sim::ItemSlot::Body, v(GearBody::Brigandine)},
    // On the hands and the feet.
    {"work_gloves", sim::ItemSlot::Hands, v(GearHands::WorkGloves)},
    {"gauntlets", sim::ItemSlot::Hands, v(GearHands::Gauntlets)},
    {"boots", sim::ItemSlot::Feet, v(GearFeet::Boots)},
    {"hobnail_boots", sim::ItemSlot::Feet, v(GearFeet::HobnailBoots)},
};

[[nodiscard]] const Entry* lookup(std::string_view key, sim::ItemSlot slot) noexcept {
    for (const Entry& entry : kTable) {
        if (entry.key == key && entry.slot == slot) {
            return &entry;
        }
    }
    return nullptr;
}

/// Writes one row's look into `out`'s field for the row's slot.
void dress(GearLook& out, const sim::ItemDef& item) noexcept {
    const std::string_view key = item.model.empty() ? std::string_view(item.id) : std::string_view(item.model);
    const Entry* hit = lookup(key, item.slot);
    switch (item.slot) {
        case sim::ItemSlot::Hand:
            out.prop = hit != nullptr ? static_cast<GearProp>(hit->value) : gearPropOfWeapon(item.weaponClass);
            break;
        case sim::ItemSlot::OffHand:
            // A row in the other hand is a shield in this build (the raws
            // refuse anything else there); an unknown one is the round one.
            out.shield = hit != nullptr ? static_cast<GearShield>(hit->value) : GearShield::RoundShield;
            break;
        case sim::ItemSlot::Head:
            out.head = hit != nullptr ? static_cast<GearHead>(hit->value) : GearHead::Cap;
            break;
        case sim::ItemSlot::Body:
            out.body = hit != nullptr ? static_cast<GearBody>(hit->value) : GearBody::Coat;
            break;
        case sim::ItemSlot::Hands:
            out.hands = hit != nullptr ? static_cast<GearHands>(hit->value) : GearHands::WorkGloves;
            break;
        case sim::ItemSlot::Feet:
            out.feet = hit != nullptr ? static_cast<GearFeet>(hit->value) : GearFeet::Boots;
            break;
        case sim::ItemSlot::None:
        case sim::ItemSlot::Trinket:
            // Carried, or at the belt: a purse is not seen at street range.
            break;
    }
}

}  // namespace

GearProp gearPropOfModel(std::string_view model) noexcept {
    const Entry* hit = lookup(model, sim::ItemSlot::Hand);
    return hit != nullptr ? static_cast<GearProp>(hit->value) : GearProp::None;
}

GearProp gearPropOfWeapon(sim::Weapon weapon) noexcept {
    switch (weapon) {
        case sim::Weapon::Fists:
            return GearProp::None;
        case sim::Weapon::Improvised:
            return GearProp::Bottle;
        case sim::Weapon::Blunt:
            return GearProp::Cudgel;
        case sim::Weapon::Evictor:
            return GearProp::Evictor;
        case sim::Weapon::Edged:
            return GearProp::Knife;
    }
    return GearProp::None;
}

std::string_view gearPropName(GearProp prop) noexcept {
    switch (prop) {
        case GearProp::None: return "none";
        case GearProp::Cudgel: return "cudgel";
        case GearProp::Evictor: return "the_evictor";
        case GearProp::Bottle: return "bottle";
        case GearProp::Knife: return "knife";
        case GearProp::Dagger: return "dagger";
        case GearProp::Cutlass: return "cutlass";
        case GearProp::BoatHook: return "boat_hook";
        case GearProp::Hatchet: return "hatchet";
        case GearProp::Mace: return "mace";
        case GearProp::Maul: return "maul";
        case GearProp::BoardingAxe: return "boarding_axe";
    }
    return "none";
}

GearLook gearLookOfItem(const sim::ItemDef* item) noexcept {
    GearLook out;
    if (item != nullptr) {
        dress(out, *item);
    }
    return out;
}

GearLook gearLookOf(const sim::Kit& kit, const sim::ItemRegistry& items) {
    GearLook out;
    for (std::int32_t s = 0; s < static_cast<std::int32_t>(sim::kWornSlotCount); ++s) {
        const sim::ItemSlot slot = sim::wornSlotAt(s);
        const sim::ItemDef* item = items.at(kit.worn(slot));
        // A row worn in a slot that is not its own is a state Kit::wear
        // never makes; drawn by its own slot it could land twice, so it is
        // not drawn at all.
        if (item == nullptr || item->slot != slot) {
            continue;
        }
        dress(out, *item);
    }
    return out;
}

}  // namespace granadad::render3d
