#include "fingerprint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <vector>

#include "fixtures.hpp"
#include "granadad/content/crc32c.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/trojsav.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"

namespace granadad::content::testing {
namespace {

// ---------------------------------------------------------------------------
// formatting, without libc
// ---------------------------------------------------------------------------
// std::to_string and snprintf both end up in the C runtime's printf, and the
// two sides of this comparison link different C runtimes (glibc on Linux,
// msvcrt through mingw on Windows). For plain integers they would almost
// certainly agree — but "almost certainly" is the exact substance of the claim
// under test, so the report does not ask them.

std::string dec(std::uint64_t value) {
    char buffer[20];
    std::size_t at = sizeof(buffer);
    do {
        buffer[--at] = static_cast<char>('0' + static_cast<int>(value % 10u));
        value /= 10u;
    } while (value != 0u);
    return std::string(buffer + at, sizeof(buffer) - at);
}

// No std::size_t overload: on both targets here size_t and uint64_t are the
// same type, so it would be a redefinition rather than an overload.

std::string dec(std::int32_t value) {
    if (value < 0) {
        // Negated in unsigned space so INT32_MIN does not overflow.
        return "-" + dec(0u - static_cast<std::uint64_t>(value));
    }
    return dec(static_cast<std::uint64_t>(value));
}

std::string hexDigits(std::uint64_t value, int digits) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out(static_cast<std::size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[static_cast<std::size_t>(value & 0xFu)];
        value >>= 4;
    }
    return out;
}

std::string hex32(std::uint32_t value) {
    return "0x" + hexDigits(value, 8);
}

std::string hex64(std::uint64_t value) {
    return "0x" + hexDigits(value, 16);
}

// ---------------------------------------------------------------------------
// checksums over decoded state
// ---------------------------------------------------------------------------

/// CRC32C over one dense lane, serialised little-endian by hand.
///
/// A reinterpret_cast over the uint16 vector would be shorter and would give
/// the same answer on both of today's targets — and would silently stop being
/// a cross-platform check the moment either target stopped being LE. The point
/// of this file is to have an answer that does not depend on the host.
std::uint32_t laneCrc(const World& world, const LaneDef& lane) {
    if (lane.bytesPerTile == 1) {
        return crc32c(world.byteLane(lane.index));
    }
    const std::span<const std::uint16_t> cells = world.shortLane(lane.index);
    std::array<std::uint8_t, 8192> staging{};
    std::size_t used = 0;
    std::uint32_t crc = 0;
    for (const std::uint16_t cell : cells) {
        staging[used++] = static_cast<std::uint8_t>(cell & 0xFFu);
        staging[used++] = static_cast<std::uint8_t>((cell >> 8) & 0xFFu);
        if (used == staging.size()) {
            crc = crc32cUpdate(crc, std::span<const std::uint8_t>(staging.data(), used));
            used = 0;
        }
    }
    if (used != 0) {
        crc = crc32cUpdate(crc, std::span<const std::uint8_t>(staging.data(), used));
    }
    return crc;
}

/// CRC32C over a sparse overlay: u32 tile index then u16 value, both LE.
std::uint32_t overlayCrc(std::span<const OverlayEntry> entries) {
    std::array<std::uint8_t, 6> cell{};
    std::uint32_t crc = 0;
    for (const OverlayEntry& entry : entries) {
        cell[0] = static_cast<std::uint8_t>(entry.tileIndex & 0xFFu);
        cell[1] = static_cast<std::uint8_t>((entry.tileIndex >> 8) & 0xFFu);
        cell[2] = static_cast<std::uint8_t>((entry.tileIndex >> 16) & 0xFFu);
        cell[3] = static_cast<std::uint8_t>((entry.tileIndex >> 24) & 0xFFu);
        cell[4] = static_cast<std::uint8_t>(entry.value & 0xFFu);
        cell[5] = static_cast<std::uint8_t>((entry.value >> 8) & 0xFFu);
        crc = crc32cUpdate(crc, std::span<const std::uint8_t>(cell.data(), cell.size()));
    }
    return crc;
}

// ---------------------------------------------------------------------------
// histograms
// ---------------------------------------------------------------------------

std::string formLabel(std::size_t ordinal) {
    if (ordinal < kTileFormCount) {
        return std::string(tileFormName(static_cast<TileForm>(ordinal)));
    }
    // Not reachable through a valid file; printed rather than asserted so a
    // divergence shows up in the diff instead of aborting one side of it.
    return "form" + dec(static_cast<std::uint64_t>(ordinal));
}

/// min / max / sum / distinct for a lane the histogram would be too wide for.
/// The lane CRC above already pins every byte; this is the human-readable
/// summary that says WHERE a CRC mismatch is, and it costs one pass.
struct Summary {
    std::uint64_t min = 0;
    std::uint64_t max = 0;
    std::uint64_t sum = 0;
    std::uint64_t distinct = 0;
};

std::string render(const char* label, const Summary& summary) {
    return std::string("  ") + label + " min=" + dec(summary.min) + " max=" + dec(summary.max) +
           " sum=" + dec(summary.sum) + " distinct=" + dec(summary.distinct) + "\n";
}

Summary summarise(const std::vector<std::uint64_t>& histogram) {
    Summary summary;
    bool seen = false;
    for (std::size_t value = 0; value < histogram.size(); ++value) {
        const std::uint64_t count = histogram[value];
        if (count == 0) {
            continue;
        }
        const auto asValue = static_cast<std::uint64_t>(value);
        if (!seen) {
            summary.min = asValue;
            seen = true;
        }
        summary.max = asValue;
        summary.sum += asValue * count;
        ++summary.distinct;
    }
    return summary;
}

// ---------------------------------------------------------------------------
// one world
// ---------------------------------------------------------------------------

void reportWorld(std::string& out, const std::string& name) {
    const std::filesystem::path file = bakedMap(name);

    out += "\nworld " + name + "\n";
    out += "  file.bytes " + dec(static_cast<std::uint64_t>(std::filesystem::file_size(file))) +
           "\n";

    TrojSav save = TrojSav::readFile(file);
    const TrojSav::Header& header = save.header();
    out += "  header.formatVersion " + dec(static_cast<std::uint64_t>(header.formatVersion)) + "\n";
    out += "  header.worldSeed " + hex64(header.worldSeed) + "\n";
    out += "  header.tick " + dec(header.tick) + "\n";
    out += "  header.rawsFingerprint " + hex64(header.rawsFingerprint) + "\n";
    out += "  toc.count " + dec(save.toc().size()) + "\n";
    for (const TrojSav::TocEntry& entry : save.toc()) {
        out += "  toc " + entry.id.str() + " offset=" + dec(entry.offset) + " compressedLen=" +
               dec(entry.compressedLen) + " uncompressedLen=" + dec(entry.uncompressedLen) +
               " crc32c=" + hex32(entry.crc32c) + "\n";
    }

    // What miniz actually produced. section() already verifies the stored CRC
    // and throws on a mismatch, so re-checksumming it here is belt and braces —
    // kept because a report that only says "it verified" is exactly the kind of
    // claim this project does not take on faith. If mingw's miniz inflated
    // differently, this line is where it shows.
    for (const TrojSav::TocEntry& entry : save.toc()) {
        const std::span<const std::uint8_t> bytes = save.section(entry.id);
        out += "  inflated " + entry.id.str() + " bytes=" + dec(bytes.size()) +
               " crc32c=" + hex32(crc32c(bytes)) + "\n";
    }

    const World world = loadWorld(save);
    const Coords& coords = world.coords();
    out += "  coords chunks=" + dec(coords.chunksX()) + "x" + dec(coords.chunksY()) + "x" +
           dec(coords.chunksZ()) + " chunkCount=" + dec(coords.chunkCount()) +
           " tileCount=" + dec(world.tileCount()) + "\n";

    out += "  lanes.count " + dec(world.lanes().count()) + "\n";
    for (const LaneDef& lane : world.lanes().all()) {
        out += "  lane " + dec(lane.index) + " name=" + lane.name +
               " bytesPerTile=" + dec(static_cast<std::uint64_t>(lane.bytesPerTile)) +
               " crc32c=" + hex32(laneCrc(world, lane)) + "\n";
    }

    // One pass over every tile in the world, decoding through the typed
    // accessors rather than reading the lane bytes again — the shifts and masks
    // in fluid_bits are exactly the sort of thing that can differ between
    // toolchains, so they belong inside the comparison, not beside it.
    const std::size_t tiles = world.tileCount();
    std::array<std::uint64_t, 256> formHist{};
    std::array<std::uint64_t, 256> flagsHist{};
    std::vector<std::uint64_t> materialHist(65536, 0);
    std::vector<std::uint64_t> temperatureHist(65536, 0);
    std::vector<std::uint64_t> lightHist(65536, 0);
    std::vector<std::uint64_t> opacityHist(256, 0);
    // A flat count-by-value, not a map: iteration order then cannot be an
    // implementation detail of anybody's container, on either platform.
    std::vector<std::uint64_t> fluidHist(65536, 0);
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        ++formHist[static_cast<std::uint8_t>(world.form(tile))];
        ++flagsHist[world.flags(tile)];
        ++materialHist[world.material(tile)];
        ++temperatureHist[world.temperature(tile)];
        ++lightHist[world.light(tile)];
        ++opacityHist[world.opacity(tile)];
        ++fluidHist[world.fluid(tile)];
    }

    out += "  form.hist";
    for (std::size_t ordinal = 0; ordinal < formHist.size(); ++ordinal) {
        if (formHist[ordinal] != 0) {
            out += " " + formLabel(ordinal) + "=" + dec(formHist[ordinal]);
        }
    }
    out += "\n";

    out += "  material.hist";
    for (std::size_t id = 0; id < materialHist.size(); ++id) {
        if (materialHist[id] != 0) {
            out += " " + dec(id) + "=" + dec(materialHist[id]);
        }
    }
    out += "\n";

    out += "  flags.hist";
    for (std::size_t bits = 0; bits < flagsHist.size(); ++bits) {
        if (flagsHist[bits] != 0) {
            out += " " + hexDigits(static_cast<std::uint64_t>(bits), 2) + "=" + dec(flagsHist[bits]);
        }
    }
    out += "\n";

    out += "  fluid.hist";
    for (std::size_t value = 0; value < fluidHist.size(); ++value) {
        if (fluidHist[value] == 0) {
            continue;
        }
        const auto packed = static_cast<std::uint16_t>(value);
        out += " d" + dec(static_cast<std::uint64_t>(fluid_bits::depth(packed))) + "f" +
               dec(static_cast<std::uint64_t>(fluid_bits::fluidId(packed))) + "s" +
               (fluid_bits::settled(packed) ? "1" : "0") + "=" + dec(fluidHist[value]);
    }
    out += "\n";

    out += render("temperature", summarise(temperatureHist));
    out += render("light", summarise(lightHist));
    out += render("opacity", summarise(opacityHist));

    const std::span<const OverlayEntry> charge = world.overlay(OverlayId::Charge);
    std::uint64_t chargeSum = 0;
    for (const OverlayEntry& entry : charge) {
        chargeSum += static_cast<std::uint64_t>(entry.value);
    }
    out += "  overlay." + std::string(overlayName(OverlayId::Charge)) +
           " count=" + dec(charge.size()) + " sum=" + dec(chargeSum) +
           " crc32c=" + hex32(overlayCrc(charge)) + "\n";
}

}  // namespace

std::string fingerprintReport() {
    const std::filesystem::path baked = contentDir() / "maps" / "baked";

    // Enumerated, not hardcoded, so a fourth baked world joins the comparison
    // on both platforms the day it is authored. Sorted because directory order
    // is a filesystem detail and NTFS and overlayfs do not agree about it —
    // that difference is not the divergence we are hunting.
    std::vector<std::string> names;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(baked)) {
        if (entry.is_regular_file() && entry.path().extension() == ".trojsav") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());

    std::string out;
    // The header names the format, not the machine. Nothing platform-specific
    // is allowed into this file — that is what makes `fc /b` a real test rather
    // than a diff of banners.
    out += "granadad-content cross-toolchain fingerprint v" +
           dec(static_cast<std::uint64_t>(kFingerprintVersion)) + "\n";
    out += "worlds " + dec(names.size()) + "\n";
    for (const std::string& name : names) {
        reportWorld(out, name);
    }
    return out;
}

bool writeReportFile(const std::string& path, const std::string& text, std::string* error) {
    const std::filesystem::path target(path);
    std::FILE* handle = nullptr;
#if defined(_WIN32)
    // "wb", and it matters: text mode on Windows would turn every '\n' into
    // "\r\n" and the byte comparison against the Linux report would fail for a
    // reason that has nothing to do with the simulation.
    handle = _wfopen(target.c_str(), L"wb");
#else
    handle = std::fopen(target.c_str(), "wb");
#endif
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = "cannot open for writing: " + path;
        }
        return false;
    }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), handle);
    const bool flushed = std::fflush(handle) == 0;
    const bool closed = std::fclose(handle) == 0;
    if (written != text.size() || !flushed || !closed) {
        if (error != nullptr) {
            *error = "short write to " + path;
        }
        return false;
    }
    return true;
}

}  // namespace granadad::content::testing
