#include "granadad/content/trojsav.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <cstdint>
#include <vector>

#include "fixtures.hpp"
#include "granadad/content/crc32c.hpp"

using namespace granadad::content;
namespace fx = granadad::content::testing;

namespace {

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::FILE* handle = std::fopen(path.string().c_str(), "rb");
    REQUIRE(handle != nullptr);
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[8192];
    while (true) {
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), handle);
        if (read == 0) {
            break;
        }
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(handle);
    return bytes;
}

const fx::BakedWorldFacts kAllWorlds[] = {fx::kTavernFixture, fx::kDocksSurface,
                                          fx::kCompoundBlock};

}  // namespace

TEST_CASE("every shipped .trojsav opens with the expected header") {
    for (const fx::BakedWorldFacts& facts : kAllWorlds) {
        CAPTURE(facts.name);
        const std::filesystem::path path = fx::bakedMap(facts.name);
        REQUIRE(std::filesystem::exists(path));
        CHECK(std::filesystem::file_size(path) == facts.fileBytes);

        const TrojSav save = TrojSav::readFile(path);
        CHECK(save.header().formatVersion == kTrojSavFormatVersion);
        CHECK(save.header().worldSeed == 0);
        CHECK(save.header().tick == 0);
        // All three were baked from the same raws.
        CHECK(save.header().rawsFingerprint == fx::kShippedRawsFingerprint);
    }
}

TEST_CASE("the TOC holds META then WRLD, sorted, contiguous, no padding") {
    for (const fx::BakedWorldFacts& facts : kAllWorlds) {
        CAPTURE(facts.name);
        const TrojSav save = TrojSav::readFile(fx::bakedMap(facts.name));
        const std::vector<TrojSav::TocEntry>& toc = save.toc();

        // Only META and WRLD are written by the current baker; the systems that
        // would own FLUD/THRM/LGHT/BUBL/ECON do not exist in sim-core yet.
        REQUIRE(toc.size() == 2);
        CHECK(toc[0].id == sections::kMeta);
        CHECK(toc[1].id == sections::kWrld);
        CHECK(toc[0].id < toc[1].id);  // the TOC is sorted ascending by id

        // Blobs start right after the header + TOC and run contiguously.
        const std::uint64_t firstBlob =
            kTrojSavHeaderBytes + toc.size() * kTrojSavTocEntryBytes;
        CHECK(toc[0].offset == firstBlob);
        CHECK(toc[1].offset == toc[0].offset + toc[0].compressedLen);
        CHECK(toc[1].offset + toc[1].compressedLen == facts.fileBytes);

        CHECK(toc[0].uncompressedLen == facts.metaUncompressedLen);
        CHECK(toc[1].uncompressedLen == facts.wrldUncompressedLen);

        CHECK(save.hasSection(sections::kMeta));
        CHECK(save.hasSection(sections::kWrld));
        CHECK_FALSE(save.hasSection(sections::kFlud));
        CHECK_FALSE(save.hasSection(sections::kEcon));
    }
}

TEST_CASE("sections inflate to their declared length and pass CRC32C") {
    for (const fx::BakedWorldFacts& facts : kAllWorlds) {
        CAPTURE(facts.name);
        TrojSav save = TrojSav::readFile(fx::bakedMap(facts.name));

        const std::span<const std::uint8_t> meta = save.section(sections::kMeta);
        const std::span<const std::uint8_t> wrld = save.section(sections::kWrld);
        CHECK(meta.size() == facts.metaUncompressedLen);
        CHECK(wrld.size() == facts.wrldUncompressedLen);

        // Second access is served from cache and must be identical.
        CHECK(save.section(sections::kMeta).data() == meta.data());
    }
}

TEST_CASE("section blobs are zlib-wrapped, not raw deflate") {
    // Every blob starts 78 01 — CMF 0x78 (32K window) and FLG 0x01 (level-1
    // FLEVEL). Java's Deflater defaults to nowrap=false. Feeding these to a raw
    // inflater would fail on the first two bytes.
    for (const fx::BakedWorldFacts& facts : kAllWorlds) {
        CAPTURE(facts.name);
        const std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(facts.name));
        const TrojSav save = TrojSav::readFile(fx::bakedMap(facts.name));
        for (const TrojSav::TocEntry& entry : save.toc()) {
            CHECK(bytes[static_cast<std::size_t>(entry.offset)] == 0x78);
            CHECK(bytes[static_cast<std::size_t>(entry.offset) + 1] == 0x01);
        }
    }
}

TEST_CASE("an absent section is an error, not an empty result") {
    TrojSav save = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    CHECK_THROWS_AS(save.section(sections::kFlud), FormatError);
}

// ---------------------------------------------------------------------------
// Strictness. The Java hard-fails on all of these; so must we. A corrupt world
// quietly loaded as a slightly-different world would poison the world hash.
// ---------------------------------------------------------------------------

TEST_CASE("a bad magic is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes[0] = 'X';
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("an unsupported format version is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes[4] = 2;  // formatVersion
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a truncated header is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes.resize(20);
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a TOC that runs past the end of the file is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes[32] = 0xFF;  // sectionCount low byte -> 255 sections
    bytes[33] = 0x00;
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a negative section count is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes[32] = 0x00;
    bytes[33] = 0x00;
    bytes[34] = 0x00;
    bytes[35] = 0x80;  // sectionCount = INT_MIN as a signed int
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a section extending past the end of the file is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    // First TOC entry's compressedLen (offset 36 + 4 + 8 = 48), low byte.
    bytes[48] = 0xFF;
    bytes[49] = 0xFF;
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a non-printable section id is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    bytes[36] = 0x01;  // first char of the first TOC id
    CHECK_THROWS_AS(TrojSav::readBytes(bytes, "mutated"), FormatError);
}

TEST_CASE("a CRC32C mismatch is rejected on access, not at open") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    // First TOC entry's crc32c field sits at 36 + 28.
    bytes[36 + 28] ^= 0xFF;

    // Opening still succeeds: the Java verifies lazily and so do we.
    TrojSav save = TrojSav::readBytes(bytes, "mutated");
    CHECK(save.toc().size() == 2);
    // WRLD is untouched and still readable.
    CHECK_NOTHROW(save.section(sections::kWrld));
    // META now fails its checksum.
    CHECK_THROWS_AS(save.section(sections::kMeta), FormatError);
}

TEST_CASE("a corrupt compressed blob is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    const TrojSav probe = TrojSav::readFile(fx::bakedMap(fx::kTavernFixture.name));
    const std::size_t wrldOffset = static_cast<std::size_t>(probe.toc()[1].offset);
    // Scribble over the middle of the deflate stream.
    for (std::size_t i = wrldOffset + 16; i < wrldOffset + 48; ++i) {
        bytes[i] ^= 0xA5;
    }
    TrojSav save = TrojSav::readBytes(bytes, "mutated");
    CHECK_THROWS_AS(save.section(sections::kWrld), FormatError);
}

TEST_CASE("a wrong declared uncompressed length is rejected") {
    std::vector<std::uint8_t> bytes = readAllBytes(fx::bakedMap(fx::kTavernFixture.name));
    // Second TOC entry (WRLD) uncompressedLen sits at 36 + 32 + 20.
    const std::size_t field = 36 + 32 + 20;
    bytes[field] = static_cast<std::uint8_t>(bytes[field] ^ 0x01);
    TrojSav save = TrojSav::readBytes(bytes, "mutated");
    CHECK_THROWS_AS(save.section(sections::kWrld), FormatError);
}

TEST_CASE("opening a file that does not exist is an error") {
    CHECK_THROWS_AS(TrojSav::readFile(fx::contentDir() / "maps" / "baked" / "nope.trojsav"),
                    FormatError);
}
