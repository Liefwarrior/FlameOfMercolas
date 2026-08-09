#include "granadad/sim/appearance.hpp"

#include <algorithm>

namespace granadad::sim {

const std::vector<AppearanceOption>& appearanceOptions() noexcept {
    // WARD_ACTORS.HPP'S OWN ORDER, minus Urchin and the four beasts -- see
    // the header. render::actorSheetQuery already knows a tag string for
    // every one of these, and render::ActorSheet::forType already knows how
    // to hand back a sprite for every one of these; this table adds nothing
    // to either, it only says WHICH of WardType's sixteen a player may stand
    // in.
    // FOUR OF THESE USED TO BE LONGER, AND THE COLUMN NEVER FORGAVE IT. The
    // custom screen's LOOK row prints "N LOOK  " (eight glyphs: the row
    // number, a space, "LOOK" and two more) ahead of whichever of these a
    // player has cycled to, or GABRI has fixed, inside the same eighteen-
    // glyph column render/creation.cpp's skill rows answer to -- ten glyphs
    // of budget left for the label itself. "MILITIA WATCH" (13), "ANIMAL
    // KEEPER" (13), "PRIEST OF THE FLAME" (20) and "DISCIPLE OF THE FLAME"
    // (21) all ran over it: test_creation.cpp's "DEVIN's and GABRI's rows
    // survive the real eighteen-glyph column" case caught GABRI's own fixed
    // row clipping to "2 LOOK  PRIEST OF." -- his look silently unnamed.
    // Shortened to the single recognizable word every one of these WardTypes
    // already answers to elsewhere in this build -- ward_actors.hpp's own
    // wardTypeName(), upper-cased to match this table's own case -- rather
    // than an abbreviation invented fresh here.
    static const std::vector<AppearanceOption> kOptions = {
        {WardType::Serf, "serf", "SERF"},
        {WardType::Shopkeeper, "shopkeeper", "SHOPKEEPER"},
        {WardType::Sailor, "sailor", "SAILOR"},
        {WardType::Fisher, "fisher", "FISHER"},
        {WardType::Carter, "carter", "CARTER"},
        {WardType::MilitiaWatch, "militia_watch", "WATCH"},
        {WardType::Wastrel, "wastrel", "WASTREL"},
        {WardType::Thief, "thief", "THIEF"},
        {WardType::PriestOfTheFlame, "priest_of_the_flame", "PRIEST"},
        {WardType::DiscipleOfTheFlame, "disciple_of_the_flame", "DISCIPLE"},
        {WardType::AnimalKeeper, "animal_keeper", "KEEPER"},
    };
    return kOptions;
}

std::optional<WardType> appearanceTypeFromId(std::string_view id) noexcept {
    const std::vector<AppearanceOption>& options = appearanceOptions();
    const auto found =
        std::find_if(options.begin(), options.end(),
                     [&](const AppearanceOption& option) { return option.id == id; });
    if (found == options.end()) {
        return std::nullopt;
    }
    return found->type;
}

const AppearanceOption* appearanceOptionFor(WardType type) noexcept {
    const std::vector<AppearanceOption>& options = appearanceOptions();
    const auto found = std::find_if(
        options.begin(), options.end(),
        [&](const AppearanceOption& option) { return option.type == type; });
    return found == options.end() ? nullptr : &*found;
}

}  // namespace granadad::sim
