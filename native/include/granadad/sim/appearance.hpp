#pragma once

// WHAT YOU LOOK LIKE, IN THE SAME VOCABULARY THE WARD ALREADY WEARS.
//
// render/actor_sheet.hpp says it plainly for the ward's own six hundred and
// change: content/art/sprites is a flat texel store and a tag query, twenty-
// five 16x16 figures in the MERCOLAS-24 palette, one per sim::WardType. Until
// this file, nothing let a PLAYER stand anywhere in that vocabulary -- a
// custom character had no look at all, and inventing a second art system for
// one body would be exactly the "polished" complaint actor_sheet.hpp's own
// header was written to answer, aimed at a body of one instead of six
// hundred.
//
// SO THIS IS NOT A WARDROBE. It is eleven of WardType's sixteen values --
// every adult humanoid trade the ward is drawn with -- named as short, stable
// ids a content file or a screen can hold. Left off, on purpose:
//
//   URCHIN   implies a child. Choosing "what you look like" is not choosing
//            to look like somebody's kid.
//   DOG, STRAY, CAT, MOUSE   the four beasts. No face, no trade, nothing a
//            player is choosing when they pick how they present -- the same
//            cut PLAY-MODE-SPEC.md's own Play-mode scoping already made for
//            the identical reason (client-observer/.../input/PlayModeInput
//            excludes AnimalActor/FeralActor: "no deference canon for
//            beasts").
//
// Both content/raws/companions/*.json's own appearanceType field (Gabri's
// fixed look; a future Devin entry could carry one too) and render/
// creation.cpp's CUSTOM path picker resolve through this ONE table, so
// nobody ever authors a second one and a typo'd id fails loudly rather than
// silently landing on WardType 0.
//
// NO FLOATS, NO SIMULATION STATE. A pure, static table and two lookups over
// it -- the same shape social.hpp's kHaggleSkill/kThieverySkill constants
// keep, just plural.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "granadad/sim/ward_actors.hpp"

namespace granadad::sim {

/// One playable look: the WardType it renders as (render::ActorSheet::forType
/// and render::actorSheetQuery both already handle any WardType -- nothing
/// there has to change for this to work), a stable id never shown on screen,
/// and a short label that is.
struct AppearanceOption {
    WardType type;
    /// "priest_of_the_flame" -- content/raws/companions/*.json's own
    /// appearanceType field and CreationResult::look (render/creation.hpp)
    /// both hold exactly this string.
    std::string_view id;
    /// "PRIEST OF THE FLAME" -- shown on the custom screen.
    std::string_view label;
};

/// The eleven, in ward_actors.hpp's own WardType order. Never empty -- this
/// is a compiled table, not a raws load, so there is no missing-file case to
/// answer defensively the way SkillTrack::load or ActorSheet::load do.
[[nodiscard]] const std::vector<AppearanceOption>& appearanceOptions() noexcept;

/// Parses one option's id. std::nullopt for anything not in
/// appearanceOptions() -- INCLUDING a real WardType this table deliberately
/// left off (Urchin, the four beasts) and a plain typo. Never fabricates a
/// look nobody authored.
[[nodiscard]] std::optional<WardType> appearanceTypeFromId(std::string_view id) noexcept;

/// The option naming `type`, or nullptr when `type` is not one of the eleven.
[[nodiscard]] const AppearanceOption* appearanceOptionFor(WardType type) noexcept;

}  // namespace granadad::sim
