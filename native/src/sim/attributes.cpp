#include "granadad/sim/attributes.hpp"

#include <algorithm>

namespace granadad::sim {

std::string_view attributeName(AttributeId attribute) noexcept {
    switch (attribute) {
        case AttributeId::Might:
            return "Might";
        case AttributeId::Agility:
            return "Agility";
        case AttributeId::Vigor:
            return "Vigor";
        case AttributeId::Wit:
            return "Wit";
    }
    return "Might";
}

std::string_view attributeAbbrev(AttributeId attribute) noexcept {
    switch (attribute) {
        case AttributeId::Might:
            return "MGT";
        case AttributeId::Agility:
            return "AGI";
        case AttributeId::Vigor:
            return "VIG";
        case AttributeId::Wit:
            return "WIT";
    }
    return "MGT";
}

std::optional<AttributeId> attributeFromRaw(std::string_view token) noexcept {
    if (token == "MGT") {
        return AttributeId::Might;
    }
    if (token == "AGI") {
        return AttributeId::Agility;
    }
    if (token == "VIG") {
        return AttributeId::Vigor;
    }
    if (token == "WIT") {
        return AttributeId::Wit;
    }
    // "NONE" (THE FLAME) and anything unrecognised both read as absent.
    return std::nullopt;
}

std::string_view aptitudeTierName(AptitudeTier tier) noexcept {
    switch (tier) {
        case AptitudeTier::Favored:
            return "Favored";
        case AptitudeTier::Trained:
            return "Trained";
        case AptitudeTier::Neglected:
            return "Neglected";
        case AptitudeTier::Flame:
            return "Flame";
    }
    return "Trained";
}

std::int32_t aptitudeCostQ8(AptitudeTier tier) noexcept {
    switch (tier) {
        case AptitudeTier::Favored:
            return 192;  // x3/4, exact
        case AptitudeTier::Trained:
            return 256;  // x1, the identity
        case AptitudeTier::Neglected:
            return 320;  // x5/4, exact
        case AptitudeTier::Flame:
            return 1024;  // x4
    }
    return 256;  // the same middle-of-the-road fallback aptitudeTierFromRaw takes
}

AptitudeTier aptitudeTierFromRaw(std::string_view token) noexcept {
    if (token == "FAVORED") {
        return AptitudeTier::Favored;
    }
    if (token == "NEGLECTED") {
        return AptitudeTier::Neglected;
    }
    if (token == "FLAME") {
        return AptitudeTier::Flame;
    }
    return AptitudeTier::Trained;
}

void AttributeBlock::setValue(AttributeId attribute, std::int32_t value) noexcept {
    values_[static_cast<std::size_t>(attribute)] =
        std::clamp(value, kAttributeFloor, kAttributeCeiling);
}

void AttributeBlock::hashInto(HashSink& sink) const {
    for (const std::int32_t value : values_) {
        sink.put_int(static_cast<std::uint32_t>(value));
    }
}

}  // namespace granadad::sim
