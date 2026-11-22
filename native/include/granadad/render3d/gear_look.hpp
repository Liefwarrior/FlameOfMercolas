#pragma once

// WHAT THE WORN ROWS LOOK LIKE ON A BODY -- PHASE 9 (OBLIVION-PLAN 8.3,
// "Appearance": a pure function of the Kit's worn rows reaches the scene
// description -- the cap on the head, the coat colour, the blade in hand).
//
// GearLook is the renderer's word for a loadout, the way ActorLook is its
// word for a person: a few small indices, packed losslessly into a key that
// rides ActorInstance::gearKey and is hashed with the rest of the crowd. It
// knows nothing about DR, damage or grips -- those are the sim's -- only what
// the eye should see:
//
//   head    the cap or the helmet (and the hood the base raws already have)
//   body    the coat, the quilted jack, the riveted one
//   prop    what the right hand holds, one per distinct thing a hand can
//           hold in this build
//   shield  what the left arm carries
//   hands   the gloves
//   feet    the boots
//
// THE TABLE IS KEYED BY ItemDef::model (the raws' "model": "maul"), the key
// the sim builder writes for every phase 9 row. A row with no model (every
// items.json row: the cudgel, the knife, the coat, the hood, the boots) is
// looked up by its id, and a row the table has never heard of falls back by
// its Weapon class (a hand row) or its slot (anything else), so a new raws
// row is drawn as SOMETHING of the right kind the day it is authored rather
// than as nothing.
//
// No sim header here: the GearLook itself is plain bytes so the one raylib
// TU (rl/, through figure.hpp) can unpack a key without pulling the sim's
// weapon header in. gear_look.cpp holds the readers of the Kit.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace granadad::sim {
class Kit;
class ItemRegistry;
struct ItemDef;
enum class Weapon : std::uint8_t;
}  // namespace granadad::sim

namespace granadad::render3d {

/// On the head. Drawn through ActorLook::headGear (dressedLook): the wool
/// cap is the knit cap, the iron pot the helmet, the hood the hood, any
/// other head row the flat cap.
enum class GearHead : std::uint8_t { None = 0, WoolCap, IronPot, Hood, Cap };
/// On the back. The coat is drawn by the look (CoatStyle::Coat in the body's
/// own colour); the gambeson and the brigandine as a jack over the torso.
enum class GearBody : std::uint8_t { None = 0, Coat, Gambeson, Brigandine };
/// In the right hand: one per distinct thing a hand can hold. Append-only
/// (the key is hashed).
enum class GearProp : std::uint8_t {
    None = 0,
    Cudgel,
    Evictor,
    Bottle,
    Knife,
    Dagger,
    Cutlass,
    BoatHook,
    Hatchet,
    Mace,
    Maul,
    BoardingAxe,
};
inline constexpr std::size_t kGearPropCount = 12;
/// On the left arm.
enum class GearShield : std::uint8_t { None = 0, Buckler, RoundShield };
/// On the hands.
enum class GearHands : std::uint8_t { None = 0, WorkGloves, Gauntlets };
/// On the feet, over the boot leather every figure already wears.
enum class GearFeet : std::uint8_t { None = 0, Boots, HobnailBoots };

/// A loadout, as the eye sees it.
///
/// THE KEY, 15 bits, lossless over the six fields:
///
///     bits  0..3   prop    (GearProp, 0..11)
///     bits  4..5   shield  (GearShield, 0..2)
///     bits  6..8   head    (GearHead, 0..4)
///     bits  9..10  body    (GearBody, 0..3)
///     bits 11..12  hands   (GearHands, 0..2)
///     bits 13..14  feet    (GearFeet, 0..2)
///
/// Zero is "wears nothing of the Kit's" and is the only key a bare body
/// has, which is what lets the scene hash leave it out.
struct GearLook {
    GearHead head = GearHead::None;
    GearBody body = GearBody::None;
    GearProp prop = GearProp::None;
    GearShield shield = GearShield::None;
    GearHands hands = GearHands::None;
    GearFeet feet = GearFeet::None;

    [[nodiscard]] constexpr std::uint32_t key() const noexcept {
        return (static_cast<std::uint32_t>(prop) & 15U) | (static_cast<std::uint32_t>(shield) & 3U) << 4U |
               (static_cast<std::uint32_t>(head) & 7U) << 6U | (static_cast<std::uint32_t>(body) & 3U) << 9U |
               (static_cast<std::uint32_t>(hands) & 3U) << 11U | (static_cast<std::uint32_t>(feet) & 3U) << 13U;
    }
    [[nodiscard]] static constexpr GearLook fromKey(std::uint32_t k) noexcept {
        GearLook g;
        g.prop = static_cast<GearProp>(k & 15U);
        g.shield = static_cast<GearShield>((k >> 4U) & 3U);
        g.head = static_cast<GearHead>((k >> 6U) & 7U);
        g.body = static_cast<GearBody>((k >> 9U) & 3U);
        g.hands = static_cast<GearHands>((k >> 11U) & 3U);
        g.feet = static_cast<GearFeet>((k >> 13U) & 3U);
        return g;
    }
    [[nodiscard]] constexpr bool operator==(const GearLook& o) const noexcept { return key() == o.key(); }
    /// True when there is anything for the kit MESH to draw: the head and
    /// the coat are the look's, so a cap alone needs no kit.
    [[nodiscard]] constexpr bool hasKit() const noexcept {
        return prop != GearProp::None || shield != GearShield::None || hands != GearHands::None ||
               feet != GearFeet::None || body == GearBody::Gambeson || body == GearBody::Brigandine;
    }
};
inline constexpr std::uint32_t kGearKeyBits = 15;

/// THE LOOK OFF THE KIT. A pure function of the worn rows (Kit::worn per
/// slot, resolved through the registry): the same rows give the same look,
/// a carried row that is not worn gives nothing, the trinket is never
/// drawn. A worn index the registry does not know is skipped.
[[nodiscard]] GearLook gearLookOf(const sim::Kit& kit, const sim::ItemRegistry& items);

/// One row's contribution, with everything else bare: the row's model key
/// (or its id when it has none) through the table, the class or slot
/// fallback after. A row of slot None or Trinket, or nullptr, is bare.
[[nodiscard]] GearLook gearLookOfItem(const sim::ItemDef* item) noexcept;

/// The prop for a model key ("maul", "cutlass"), or None for a key the
/// table does not hold.
[[nodiscard]] GearProp gearPropOfModel(std::string_view model) noexcept;

/// THE GULL'S OWN, who hold a sim::Weapon and no Kit: the cudgel for a
/// bouncer's Blunt, the headed cudgel for the Evictor, a bottle off the
/// table for Improvised, a knife for Edged (the blade the nemesis draws),
/// nothing for Fists. Also every model-less Hand row's fallback.
[[nodiscard]] GearProp gearPropOfWeapon(sim::Weapon weapon) noexcept;

/// The model key a prop is drawn as ("cutlass"), for a report.
[[nodiscard]] std::string_view gearPropName(GearProp prop) noexcept;

/// True for the props held in both hands (the maul, the boarding axe): the
/// figure carries them head-down at the side, the viewmodel puts the left
/// hand on the haft.
[[nodiscard]] constexpr bool gearPropTwoHanded(GearProp prop) noexcept {
    return prop == GearProp::Maul || prop == GearProp::BoardingAxe;
}

}  // namespace granadad::render3d
