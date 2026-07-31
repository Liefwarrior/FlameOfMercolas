#pragma once

// All chunk/tile index bit math, audited once.
//
// Chunks are 32x32x8 tiles. Two orderings are load-bearing and must never drift:
//
//   localIdx   = (localZ << 10) | (localY << 5) | localX      -- x-fastest
//   chunkIndex = (cz * chunksY + cy) * chunksX + cx           -- THE canonical
//                chunk order for every side-effectful iteration, hash and save
//
// Pure integer math. No floats, by binding constraint.

#include <cstddef>
#include <cstdint>

#include "granadad/content/byte_reader.hpp"
#include "granadad/content/format_error.hpp"

namespace granadad::content {

inline constexpr std::int32_t kChunkSizeX = 32;
inline constexpr std::int32_t kChunkSizeY = 32;
inline constexpr std::int32_t kChunkSizeZ = 8;

/// Tiles per chunk: 32*32*8. The chunk codec's CELLS constant.
inline constexpr std::int32_t kTilesPerChunk = kChunkSizeX * kChunkSizeY * kChunkSizeZ;
static_assert(kTilesPerChunk == 8192);

// World dimension limits, border ring included.
inline constexpr std::int32_t kMinChunksPerAxis = 3;
inline constexpr std::int32_t kMaxChunksXY = 4096 / kChunkSizeX;  // 128
inline constexpr std::int32_t kMaxChunksZ = 64 / kChunkSizeZ;     // 8

/// The 30-bit packed absolute tile position `(z << 24) | (y << 12) | x` — the
/// lingua franca of every queue and event payload. x and y are 12-bit
/// (0..4095), z is 6-bit (0..63), all including the VOID border.
namespace packed_pos {

inline constexpr int kXBits = 12;
inline constexpr int kYBits = 12;
inline constexpr int kZBits = 6;
inline constexpr int kYShift = 12;
inline constexpr int kZShift = 24;

inline constexpr std::int32_t kXMask = (1 << kXBits) - 1;
inline constexpr std::int32_t kYMask = (1 << kYBits) - 1;
inline constexpr std::int32_t kZMask = (1 << kZBits) - 1;

/// Packs world-tile coordinates. Callers guarantee range, as in the Java.
[[nodiscard]] constexpr std::int32_t pack(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
    return (z << kZShift) | (y << kYShift) | x;
}

[[nodiscard]] constexpr std::int32_t x(std::int32_t pos) noexcept {
    return pos & kXMask;
}

[[nodiscard]] constexpr std::int32_t y(std::int32_t pos) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(pos) >> kYShift) & kYMask;
}

[[nodiscard]] constexpr std::int32_t z(std::int32_t pos) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(pos) >> kZShift) & kZMask;
}

/// One step in a direction.
///
/// The Java is a plain `pos + (dz<<24) + (dy<<12) + dx`, which leans on the
/// immutable VOID border ring making one blind step from any concrete tile
/// safe. That is signed overflow the moment anyone steps off a border tile, so
/// it goes through the wrapping helpers here rather than trusting -fwrapv.
[[nodiscard]] constexpr std::int32_t step(std::int32_t pos, std::int32_t dx, std::int32_t dy,
                                          std::int32_t dz) noexcept {
    return wrapAdd(wrapAdd(wrapAdd(pos, wrapMul(dz, 1 << kZShift)), wrapMul(dy, 1 << kYShift)), dx);
}

}  // namespace packed_pos

/// Chunk/tile index math bound to one world's dimensions. Immutable.
class Coords {
public:
    constexpr Coords(std::int32_t chunksX, std::int32_t chunksY, std::int32_t chunksZ) noexcept
        : chunksX_(chunksX), chunksY_(chunksY), chunksZ_(chunksZ) {}

    /// Validates against the WorldConfig limits (border included) and throws
    /// FormatError otherwise — the loader's gate on META's dimensions.
    static Coords checked(std::int32_t chunksX, std::int32_t chunksY, std::int32_t chunksZ);

    [[nodiscard]] constexpr std::int32_t chunksX() const noexcept { return chunksX_; }
    [[nodiscard]] constexpr std::int32_t chunksY() const noexcept { return chunksY_; }
    [[nodiscard]] constexpr std::int32_t chunksZ() const noexcept { return chunksZ_; }

    /// Total chunk count, border included.
    [[nodiscard]] constexpr std::int32_t chunkCount() const noexcept {
        return chunksX_ * chunksY_ * chunksZ_;
    }

    [[nodiscard]] constexpr std::int32_t chunkIndexOf(std::int32_t cx, std::int32_t cy,
                                                      std::int32_t cz) const noexcept {
        return (cz * chunksY_ + cy) * chunksX_ + cx;
    }

    [[nodiscard]] constexpr std::int32_t chunkX(std::int32_t chunkIndex) const noexcept {
        return chunkIndex % chunksX_;
    }

    [[nodiscard]] constexpr std::int32_t chunkY(std::int32_t chunkIndex) const noexcept {
        return (chunkIndex / chunksX_) % chunksY_;
    }

    [[nodiscard]] constexpr std::int32_t chunkZ(std::int32_t chunkIndex) const noexcept {
        return chunkIndex / (chunksX_ * chunksY_);
    }

    /// The chunk containing a packed tile position.
    [[nodiscard]] constexpr std::int32_t chunkIndex(std::int32_t pos) const noexcept {
        return chunkIndexOf(packed_pos::x(pos) >> 5, packed_pos::y(pos) >> 5,
                            packed_pos::z(pos) >> 3);
    }

    /// A tile's index within its chunk's dense lanes, range [0, 8192).
    [[nodiscard]] static constexpr std::int32_t localIdx(std::int32_t pos) noexcept {
        const std::int32_t lx = packed_pos::x(pos) & (kChunkSizeX - 1);
        const std::int32_t ly = packed_pos::y(pos) & (kChunkSizeY - 1);
        const std::int32_t lz = packed_pos::z(pos) & (kChunkSizeZ - 1);
        return (lz << 10) | (ly << 5) | lx;
    }

    /// localIdx from chunk-local coordinates.
    [[nodiscard]] static constexpr std::int32_t localIdxOf(std::int32_t lx, std::int32_t ly,
                                                           std::int32_t lz) noexcept {
        return (lz << 10) | (ly << 5) | lx;
    }

    /// Reconstructs the packed world position of `localIdx` within `chunkIndex`.
    [[nodiscard]] constexpr std::int32_t packedPos(std::int32_t chunkIndex,
                                                   std::int32_t localIdx) const noexcept {
        const std::int32_t px = (chunkX(chunkIndex) << 5) | (localIdx & 31);
        const std::int32_t py = (chunkY(chunkIndex) << 5) |
                                (static_cast<std::int32_t>(static_cast<std::uint32_t>(localIdx) >> 5) & 31);
        const std::int32_t pz = (chunkZ(chunkIndex) << 3) |
                                (static_cast<std::int32_t>(static_cast<std::uint32_t>(localIdx) >> 10) & 7);
        return packed_pos::pack(px, py, pz);
    }

    /// Whether a chunk lies on the immutable VOID border ring (any face).
    /// Border chunks are in-bounds and readable; every write into them is
    /// rejected by the writer.
    [[nodiscard]] constexpr bool isVoidBorder(std::int32_t chunkIndex) const noexcept {
        const std::int32_t cx = chunkX(chunkIndex);
        const std::int32_t cy = chunkY(chunkIndex);
        const std::int32_t cz = chunkZ(chunkIndex);
        return cx == 0 || cx == chunksX_ - 1 || cy == 0 || cy == chunksY_ - 1 || cz == 0 ||
               cz == chunksZ_ - 1;
    }

private:
    std::int32_t chunksX_;
    std::int32_t chunksY_;
    std::int32_t chunksZ_;
};

}  // namespace granadad::content
