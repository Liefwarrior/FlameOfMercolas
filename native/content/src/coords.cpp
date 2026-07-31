#include "granadad/content/coords.hpp"

#include <string>

namespace granadad::content {
namespace {

void requireRange(const char* name, std::int32_t value, std::int32_t max) {
    if (value < kMinChunksPerAxis || value > max) {
        throw FormatError(std::string(name) + " must be in [" + std::to_string(kMinChunksPerAxis) +
                          ", " + std::to_string(max) + "] (border included): " +
                          std::to_string(value));
    }
}

}  // namespace

Coords Coords::checked(std::int32_t chunksX, std::int32_t chunksY, std::int32_t chunksZ) {
    requireRange("chunksX", chunksX, kMaxChunksXY);
    requireRange("chunksY", chunksY, kMaxChunksXY);
    requireRange("chunksZ", chunksZ, kMaxChunksZ);
    return Coords(chunksX, chunksY, chunksZ);
}

}  // namespace granadad::content
