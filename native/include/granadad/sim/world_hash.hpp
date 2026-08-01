#pragma once

// The one world hasher: the canonical 64-bit answer to "what is the state?"
//
// Every system feeds its canonical state into its own sink; the world feeds its
// decoded lanes and overlays into a reserved one. The sub-hashes then fold
// together into a single combined hash. Sub-hashes are not decoration -- when a
// golden master diverges they name the first divergent SYSTEM, which is the
// difference between a two-minute bisect and a two-day one.
//
// WHAT IS HASHED
//   Decoded lane values, chunks ascending by chunkIndex, lanes in registry
//   order, overlay cells ascending by localIdx. That is the whole contract.
//
// WHAT IS DELIBERATELY NOT
//   Chunk lifecycle state, revision numbers, dirty bits, the compressed
//   representation (lanes are hashed DECODED), change logs, the tick number,
//   the world seed, and anything wall-clock. A frozen chunk and a live chunk
//   holding identical tiles hash identically, which is the point: the logical
//   world is the thing under test, not the engine's bookkeeping about it.
//
// THE ONE TRAP
//   Sub-hashes combine in ascending salt order, and the order is SIGNED. Java
//   keys the map with java.lang.Long, whose natural ordering is signed, so the
//   `world` section (salt 0xC437... -- negative) folds BEFORE `actors` (salt
//   0x10AC... -- positive). A C++ std::map<std::uint64_t, ...> would fold them
//   the other way round and produce a different combined hash for identical
//   state, with every sub-hash matching. See kCombinedVectors, which pins
//   exactly that pair.

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

#include "granadad/content/world.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/system_id.hpp"

namespace granadad::sim {

/// Pinned sub-hash seed ("TROJSAV1").
inline constexpr std::uint64_t SECTION_SEED = 0x54524F4A53415631ull;
/// Pinned combined-hash seed ("FLAMEV01").
inline constexpr std::uint64_t COMBINE_SEED = 0x464C414D45563031ull;

/// The hashing protocol systems and the world feed state into.
///
/// Widths are EXPLICIT and the parameters are unsigned. Java's are `int` and
/// `long` masked down inside; taking unsigned here says the same thing at the
/// call site and makes a caller with signed state write the cast, which is the
/// moment they get to think about whether the sign matters. It does not -- all
/// 32 bits of a negative int fold either way -- but the cast is where a reader
/// can check.
///
/// A concrete class, not an interface: there is exactly one implementation and
/// a virtual call per byte over 2.7 million tiles is not free.
class HashSink {
public:
    explicit constexpr HashSink(std::uint64_t salt) noexcept
        : h_(mix64(SECTION_SEED ^ salt)), salt_(salt) {}

    /// Folds the low 8 bits.
    constexpr void put_byte(std::uint32_t v) noexcept { fold(v); }

    /// Folds the low 16 bits, little-endian.
    constexpr void put_short(std::uint32_t v) noexcept {
        fold(v);
        fold(v >> 8);
    }

    /// Folds all 32 bits, little-endian.
    constexpr void put_int(std::uint32_t v) noexcept {
        fold(v);
        fold(v >> 8);
        fold(v >> 16);
        fold(v >> 24);
    }

    /// Folds all 64 bits, little-endian.
    constexpr void put_long(std::uint64_t v) noexcept {
        put_int(static_cast<std::uint32_t>(v));
        put_int(static_cast<std::uint32_t>(v >> 32));
    }

    /// Folds raw bytes in order.
    void put_bytes(std::span<const std::uint8_t> bytes) noexcept;

    /// The sub-hash of everything folded so far.
    ///
    /// PURE: it does not disturb the stream, so it may be read repeatedly and a
    /// sink may keep being fed afterwards. The tail block is tagged with its
    /// byte count in the top byte -- always free, because a partial tail
    /// occupies at most 56 bits -- which is what stops N zero bytes hashing the
    /// same as N+1. The total byte count folds in unconditionally after it.
    [[nodiscard]] constexpr std::uint64_t finished() const noexcept {
        std::uint64_t r = h_;
        if (buf_bits_ != 0) {
            r = mix64(r ^ (buf_ | (static_cast<std::uint64_t>(buf_bits_ >> 3) << 56)));
        }
        return mix64(r ^ total_bytes_);
    }

    [[nodiscard]] constexpr std::uint64_t salt() const noexcept { return salt_; }
    [[nodiscard]] constexpr std::uint64_t total_bytes() const noexcept { return total_bytes_; }

private:
    constexpr void fold(std::uint32_t b) noexcept {
        buf_ |= static_cast<std::uint64_t>(b & 0xFFu) << buf_bits_;
        buf_bits_ += 8;
        ++total_bytes_;
        if (buf_bits_ == 64) {
            h_ = mix64(h_ ^ buf_);
            buf_ = 0;
            buf_bits_ = 0;
        }
    }

    /// Folds one already-assembled little-endian 8-byte block. Only legal when
    /// the buffer is empty; put_bytes uses it for the aligned middle of a lane.
    constexpr void fold_block(std::uint64_t block) noexcept {
        h_ = mix64(h_ ^ block);
        total_bytes_ += 8;
    }

    std::uint64_t h_;
    std::uint64_t salt_;
    std::uint64_t buf_ = 0;
    int buf_bits_ = 0;
    std::uint64_t total_bytes_ = 0;
};

/// Collects sub-hashes and folds them into one number.
class WorldHasher {
public:
    WorldHasher() = default;

    /// The sink accumulating the section keyed by `salt`, created on first use.
    [[nodiscard]] HashSink& section_sink(std::uint64_t salt);

    /// The sink accumulating `system`'s sub-hash.
    [[nodiscard]] HashSink& section_sink(const SystemId& system) {
        return section_sink(system.salt());
    }

    /// Contributes the world's canonical logical content: every chunk ascending
    /// by chunkIndex, every lane in registry order, every one of the 8192 cells,
    /// then each overlay's cells ascending by localIdx.
    ///
    /// Throws EngineError if an overlay's cells are not strictly ascending
    /// within a chunk -- a hard throw and not a sort, because unsorted overlay
    /// cells mean the world was built through a path that does not maintain the
    /// invariant, and quietly sorting them here would hide that forever.
    void hash_world(const content::World& world);

    /// The finished sub-hash of a section. Throws EngineError if nothing was
    /// ever fed under that salt -- an unfed section is a system that forgot to
    /// hash itself, and returning a seed-only value would make that invisible.
    [[nodiscard]] std::uint64_t section_hash(std::uint64_t salt) const;
    [[nodiscard]] std::uint64_t section_hash(const SystemId& system) const {
        return section_hash(system.salt());
    }

    /// How many sections have been fed.
    [[nodiscard]] std::size_t section_count() const noexcept { return sections_.size(); }

    /// All sub-hashes folded in ascending SIGNED salt order:
    ///     h = mix64(COMBINE_SEED)
    ///     per section: h = mix64(h ^ salt); h = mix64(h + subHash)
    ///
    /// Invariant to the order sinks were created and to the order they were
    /// fed. Not invariant to the signedness of the map key -- see the header
    /// comment.
    [[nodiscard]] std::uint64_t combined_hash() const;

private:
    // std::int64_t, and that is the entire reason this member has a comment.
    // Java's TreeMap<Long, ...> iterates in signed order; std::map<uint64_t>
    // would iterate in unsigned order and silently produce a different combined
    // hash from identical state.
    std::map<std::int64_t, HashSink> sections_;
};

}  // namespace granadad::sim
