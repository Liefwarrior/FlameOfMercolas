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
    static const std::vector<AppearanceOption> kOptions = {
        {WardType::Serf, "serf", "SERF"},
        {WardType::Shopkeeper, "shopkeeper", "SHOPKEEPER"},
        {WardType::Sailor, "sailor", "SAILOR"},
        {WardType::Fisher, "fisher", "FISHER"},
        {WardType::Carter, "carter", "CARTER"},
        {WardType::MilitiaWatch, "militia_watch", "MILITIA WATCH"},
        {WardType::Wastrel, "wastrel", "WASTREL"},
        {WardType::Thief, "thief", "THIEF"},
        {WardType::PriestOfTheFlame, "priest_of_the_flame", "PRIEST OF THE FLAME"},
        {WardType::DiscipleOfTheFlame, "disciple_of_the_flame", "DISCIPLE OF THE FLAME"},
        {WardType::AnimalKeeper, "animal_keeper", "ANIMAL KEEPER"},
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
