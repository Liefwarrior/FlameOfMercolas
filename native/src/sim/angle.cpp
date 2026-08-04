#include "granadad/sim/angle.hpp"

namespace granadad::sim {

std::string_view compass_point(Angle yaw) noexcept {
    // Eight sectors of 8192 BAM each, rounded rather than truncated: a facing
    // one unit short of due east should read "E", not "N".
    static constexpr std::string_view kPoints[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const std::int32_t wrapped = yaw & (kTurnFull - 1);
    const std::int32_t sector = ((wrapped + (kTurnFull / 16)) & (kTurnFull - 1)) / (kTurnFull / 8);
    return kPoints[sector];
}

}  // namespace granadad::sim
