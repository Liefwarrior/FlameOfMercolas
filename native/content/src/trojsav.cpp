#include "granadad/content/trojsav.hpp"

#include <cstdio>
#include <limits>

#include "granadad/content/byte_reader.hpp"
#include "granadad/content/crc32c.hpp"

// miniz's include path differs between the FetchContent build tree and an
// installed copy.
#if __has_include(<miniz/miniz.h>)
#include <miniz/miniz.h>
#else
#include <miniz.h>
#endif

namespace granadad::content {
namespace {

constexpr std::uint64_t kJavaMaxSigned64 = static_cast<std::uint64_t>(
    std::numeric_limits<std::int64_t>::max());
constexpr std::uint64_t kJavaMaxSigned32 = static_cast<std::uint64_t>(
    std::numeric_limits<std::int32_t>::max());

std::string hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return std::string(buffer);
}

/// Inflates a zlib-wrapped blob into exactly `expectedLen` bytes.
///
/// The Java sizes its output buffer to the declared length and treats both a
/// short inflate and any leftover output as corruption. mz_uncompress2 reports
/// the bytes it actually produced, so both cases are caught here: a stream that
/// ends early leaves outLen short, and a stream with more to give cannot fit
/// and fails outright.
std::vector<std::uint8_t> inflateSection(std::span<const std::uint8_t> compressed,
                                         std::uint64_t expectedLen, const std::string& label) {
    if (expectedLen > kJavaMaxSigned32) {
        throw FormatError(label + " too large: " + std::to_string(expectedLen));
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(expectedLen));
    if (expectedLen == 0) {
        // Nothing to inflate. The Java's fill loop never runs for an empty
        // section either, so this matches its behaviour rather than inventing
        // a stricter rule.
        return out;
    }
    mz_ulong outLen = static_cast<mz_ulong>(out.size());
    mz_ulong inLen = static_cast<mz_ulong>(compressed.size());
    const int status = mz_uncompress2(out.data(), &outLen, compressed.data(), &inLen);
    if (status != MZ_OK) {
        throw FormatError(label + " is corrupt: miniz status " + std::to_string(status) + " (" +
                          mz_error(status) + ")");
    }
    if (outLen != out.size()) {
        throw FormatError(label + " is truncated: inflated " + std::to_string(outLen) +
                          " bytes, expected " + std::to_string(out.size()));
    }
    return out;
}

}  // namespace

bool isValidSectionId(SectionId id) noexcept {
    for (const char c : id.chars) {
        const auto value = static_cast<unsigned char>(c);
        if (value < 0x20u || value > 0x7Eu) {
            return false;
        }
    }
    return true;
}

TrojSav TrojSav::readFile(const std::filesystem::path& file) {
    std::FILE* handle = nullptr;
#if defined(_WIN32)
    handle = _wfopen(file.c_str(), L"rb");
#else
    handle = std::fopen(file.c_str(), "rb");
#endif
    if (handle == nullptr) {
        throw FormatError("cannot open TROJSAV: " + file.string());
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[64 * 1024];
    while (true) {
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), handle);
        if (read == 0) {
            break;
        }
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    const bool failed = std::ferror(handle) != 0;
    std::fclose(handle);
    if (failed) {
        throw FormatError("error reading TROJSAV: " + file.string());
    }
    return readBytes(std::move(bytes), file.string());
}

TrojSav TrojSav::readBytes(std::vector<std::uint8_t> bytes, std::string origin) {
    TrojSav sav;
    sav.origin_ = std::move(origin);
    sav.bytes_ = std::move(bytes);

    const std::size_t fileLen = sav.bytes_.size();
    if (fileLen < kTrojSavHeaderBytes) {
        throw FormatError("truncated TROJSAV header: " + sav.origin_);
    }
    ByteReader in(sav.bytes_, sav.origin_);

    const std::uint32_t magic = in.u32();
    if (magic != kTrojSavMagic) {
        throw FormatError("not a TROJSAV (magic " + hex32(magic) + "): " + sav.origin_);
    }
    sav.header_.formatVersion = in.u32();
    if (sav.header_.formatVersion != kTrojSavFormatVersion) {
        throw FormatError("unsupported TROJSAV format version " +
                          std::to_string(sav.header_.formatVersion) + " (this build reads " +
                          std::to_string(kTrojSavFormatVersion) + "): " + sav.origin_);
    }
    sav.header_.worldSeed = in.u64();
    sav.header_.tick = in.u64();
    sav.header_.rawsFingerprint = in.u64();

    // sectionCount is a signed int on disk; a negative value is corruption, not
    // a huge count.
    const std::uint32_t sectionCountRaw = in.u32();
    if (sectionCountRaw > kJavaMaxSigned32 ||
        kTrojSavHeaderBytes + static_cast<std::uint64_t>(sectionCountRaw) * kTrojSavTocEntryBytes >
            fileLen) {
        throw FormatError("corrupt TROJSAV TOC (" + std::to_string(sectionCountRaw) +
                          " sections): " + sav.origin_);
    }
    const std::size_t sectionCount = sectionCountRaw;

    sav.toc_.reserve(sectionCount);
    for (std::size_t i = 0; i < sectionCount; ++i) {
        TocEntry entry;
        const std::span<const std::uint8_t> idBytes = in.take(4);
        for (std::size_t c = 0; c < 4; ++c) {
            entry.id.chars[c] = static_cast<char>(idBytes[c]);
        }
        if (!isValidSectionId(entry.id)) {
            throw FormatError("TROJSAV section id must be printable ASCII: " + sav.origin_);
        }
        entry.offset = in.u64();
        entry.compressedLen = in.u64();
        entry.uncompressedLen = in.u64();
        entry.crc32c = in.u32();
        if (entry.offset > kJavaMaxSigned64 || entry.compressedLen > kJavaMaxSigned64 ||
            entry.uncompressedLen > kJavaMaxSigned64 || entry.offset > fileLen ||
            entry.compressedLen > fileLen - entry.offset) {
            throw FormatError("section '" + entry.id.str() + "' extends past end of file: " +
                              sav.origin_);
        }
        // The Java reads the TOC into a TreeMap, so a duplicated id silently
        // keeps whichever entry came last. That is corruption; reject it.
        for (const TocEntry& seen : sav.toc_) {
            if (seen.id == entry.id) {
                throw FormatError("duplicate TROJSAV section '" + entry.id.str() + "': " +
                                  sav.origin_);
            }
        }
        sav.toc_.push_back(entry);
    }

    sav.inflated_.resize(sav.toc_.size());
    sav.inflatedDone_.assign(sav.toc_.size(), 0);
    return sav;
}

std::size_t TrojSav::indexOf(SectionId id) const noexcept {
    for (std::size_t i = 0; i < toc_.size(); ++i) {
        if (toc_[i].id == id) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

bool TrojSav::hasSection(SectionId id) const noexcept {
    return indexOf(id) != static_cast<std::size_t>(-1);
}

std::span<const std::uint8_t> TrojSav::section(SectionId id) {
    const std::size_t index = indexOf(id);
    if (index == static_cast<std::size_t>(-1)) {
        throw FormatError("TROJSAV section '" + id.str() + "' is absent: " + origin_);
    }
    if (inflatedDone_[index] != 0) {
        return inflated_[index];
    }
    const TocEntry& entry = toc_[index];
    const std::string label = "TROJSAV section '" + id.str() + "' in " + origin_;
    const std::span<const std::uint8_t> compressed(bytes_.data() +
                                                       static_cast<std::size_t>(entry.offset),
                                                   static_cast<std::size_t>(entry.compressedLen));
    std::vector<std::uint8_t> content = inflateSection(compressed, entry.uncompressedLen, label);

    const std::uint32_t actual = crc32c(std::span<const std::uint8_t>(content));
    if (actual != entry.crc32c) {
        throw FormatError(label + " CRC32C mismatch: computed " + hex32(actual) + ", TOC declares " +
                          hex32(entry.crc32c));
    }

    inflated_[index] = std::move(content);
    inflatedDone_[index] = 1;
    return inflated_[index];
}

}  // namespace granadad::content
