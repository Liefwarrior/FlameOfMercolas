#include "granadad/content/lanes.hpp"

#include <string>

namespace granadad::content {

std::string_view tileFormName(TileForm form) noexcept {
    switch (form) {
        case TileForm::Void:
            return "VOID";
        case TileForm::Open:
            return "OPEN";
        case TileForm::Floor:
            return "FLOOR";
        case TileForm::Wall:
            return "WALL";
        case TileForm::Ramp:
            return "RAMP";
        case TileForm::Stair:
            return "STAIR";
    }
    return "?";
}

std::string_view overlayName(OverlayId overlay) noexcept {
    switch (overlay) {
        case OverlayId::Charge:
            return "CHARGE";
    }
    return "?";
}

LaneLayout LaneLayout::core() {
    LaneLayout layout;
    layout.registerLane(std::string(lane_names::kMaterial), 2);
    layout.registerLane(std::string(lane_names::kForm), 1);
    layout.registerLane(std::string(lane_names::kFlags), 1);
    layout.registerLane(std::string(lane_names::kTemperature), 2);
    layout.registerLane(std::string(lane_names::kFluid), 2);
    layout.registerLane(std::string(lane_names::kLight), 2);
    layout.registerLane(std::string(lane_names::kOpacity), 1);
    return layout;
}

void LaneLayout::registerLane(std::string name, int bytesPerTile) {
    if (name.empty()) {
        throw FormatError("lane name must be non-empty");
    }
    if (bytesPerTile != 1 && bytesPerTile != 2) {
        throw FormatError("lane '" + name + "' bytesPerTile must be 1 or 2: " +
                          std::to_string(bytesPerTile));
    }
    if (lanes_.size() >= kMaxLaneCount) {
        throw FormatError("lane count exceeds " + std::to_string(kMaxLaneCount));
    }
    lanes_.push_back(LaneDef{lanes_.size(), std::move(name), bytesPerTile});
}

const LaneDef& LaneLayout::byIndex(std::size_t index) const {
    if (index >= lanes_.size()) {
        throw FormatError("lane index " + std::to_string(index) + " out of range (" +
                          std::to_string(lanes_.size()) + " lanes)");
    }
    return lanes_[index];
}

const LaneDef* LaneLayout::find(std::string_view name) const noexcept {
    for (const LaneDef& lane : lanes_) {
        if (lane.name == name) {
            return &lane;
        }
    }
    return nullptr;
}

}  // namespace granadad::content
