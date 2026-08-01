#include "granadad/sim/world_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "granadad/content/lanes.hpp"
#include "granadad/sim/engine_error.hpp"

namespace granadad::sim {
namespace {

std::string hex64(std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[static_cast<std::size_t>(value & 0xFu)];
        value >>= 4;
    }
    return "0x" + out;
}

}  // namespace

// ---------------------------------------------------------------------------
// HashSink
// ---------------------------------------------------------------------------

void HashSink::put_bytes(std::span<const std::uint8_t> bytes) noexcept {
    std::size_t i = 0;

    // Fill whatever partial block is already buffered, one byte at a time.
    while (buf_bits_ != 0 && i < bytes.size()) {
        fold(bytes[i]);
        ++i;
    }

    // Then eight at a time. This is an optimisation and nothing else: a whole
    // byte lane is 8192 bytes and every one of them would otherwise take a
    // shift, an or, a compare and a branch. The block is assembled EXPLICITLY
    // little-endian rather than by memcpy or reinterpret_cast, because host
    // byte order is part of the question this hash exists to answer and must
    // not also be part of the answer.
    while (i + 8 <= bytes.size()) {
        std::uint64_t block = 0;
        for (int k = 0; k < 8; ++k) {
            block |= static_cast<std::uint64_t>(bytes[i + static_cast<std::size_t>(k)])
                     << (k * 8);
        }
        fold_block(block);
        i += 8;
    }

    for (; i < bytes.size(); ++i) {
        fold(bytes[i]);
    }
}

// ---------------------------------------------------------------------------
// WorldHasher
// ---------------------------------------------------------------------------

HashSink& WorldHasher::section_sink(std::uint64_t salt) {
    return sections_.try_emplace(static_cast<std::int64_t>(salt), salt).first->second;
}

std::uint64_t WorldHasher::section_hash(std::uint64_t salt) const {
    const auto it = sections_.find(static_cast<std::int64_t>(salt));
    if (it == sections_.end()) {
        // Not "return the seed". An unfed section is a system that never hashed
        // itself, and a plausible-looking number here would let that system's
        // entire state drift without the combined hash ever moving.
        throw EngineError("no section was fed for salt " + hex64(salt));
    }
    return it->second.finished();
}

std::uint64_t WorldHasher::combined_hash() const {
    std::uint64_t h = mix64(COMBINE_SEED);
    for (const auto& entry : sections_) {
        h = mix64(h ^ static_cast<std::uint64_t>(entry.first));
        h = mix64(h + entry.second.finished());
    }
    return h;
}

void WorldHasher::hash_world(const content::World& world) {
    constexpr std::size_t kCells = static_cast<std::size_t>(content::kTilesPerChunk);

    HashSink& sink = section_sink(WORLD_SECTION_SALT);
    const content::LaneLayout& lanes = world.lanes();
    const std::size_t lane_count = lanes.count();
    const std::size_t chunk_count = world.chunkCount();

    // One cursor per overlay into its whole-world entry list. The world stores
    // overlays by GLOBAL tile index in ascending order, so each chunk's cells
    // are a contiguous run and a single forward sweep visits every entry once.
    // The alternative -- a lookup per chunk -- would be a map, and a map here
    // would be the exact hazard the lane registry has a comment about.
    std::array<std::size_t, content::kOverlayCount> cursor{};

    for (std::size_t chunk = 0; chunk < chunk_count; ++chunk) {
        sink.put_int(static_cast<std::uint32_t>(chunk));
        const std::size_t base = chunk * kCells;

        for (std::size_t li = 0; li < lane_count; ++li) {
            const content::LaneDef& lane = lanes.byIndex(li);
            sink.put_byte(static_cast<std::uint32_t>(lane.index));
            sink.put_byte(static_cast<std::uint32_t>(lane.bytesPerTile));
            if (lane.bytesPerTile == 2) {
                for (const std::uint16_t cell : world.shortLane(li).subspan(base, kCells)) {
                    sink.put_short(cell);
                }
            } else {
                sink.put_bytes(world.byteLane(li).subspan(base, kCells));
            }
        }

        for (std::size_t ov = 0; ov < content::kOverlayCount; ++ov) {
            const auto id = static_cast<content::OverlayId>(ov);
            const std::span<const content::OverlayEntry> entries = world.overlay(id);
            const std::uint64_t limit = static_cast<std::uint64_t>(base) + kCells;

            std::size_t end = cursor[ov];
            while (end < entries.size()
                   && static_cast<std::uint64_t>(entries[end].tileIndex) < limit) {
                ++end;
            }

            sink.put_byte(static_cast<std::uint32_t>(ov));
            sink.put_int(static_cast<std::uint32_t>(end - cursor[ov]));

            std::int64_t prev = -1;
            for (std::size_t i = cursor[ov]; i < end; ++i) {
                const std::int64_t local = static_cast<std::int64_t>(entries[i].tileIndex)
                                           - static_cast<std::int64_t>(base);
                if (local <= prev || local >= static_cast<std::int64_t>(kCells)) {
                    // A hard throw, not a sort. Out-of-order overlay cells mean
                    // the world was populated through a path that does not
                    // maintain the ascending invariant, and fixing it up here
                    // would hide that for the life of the project.
                    throw EngineError("overlay " + std::to_string(ov)
                                      + " cells must be ascending localIdx within chunk "
                                      + std::to_string(chunk) + ": " + std::to_string(local)
                                      + " after " + std::to_string(prev));
                }
                sink.put_short(static_cast<std::uint32_t>(local));
                sink.put_short(entries[i].value);
                prev = local;
            }
            cursor[ov] = end;
        }
    }
}

}  // namespace granadad::sim
