#include "granadad/content/crc32c.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "granadad/content/trojsav.hpp"

using namespace granadad::content;
namespace fx = granadad::content::testing;

namespace {

std::span<const std::uint8_t> bytesOf(const std::string& text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                         text.size());
}

}  // namespace

TEST_CASE("crc32c matches the published CRC-32/ISCSI check vector") {
    // The canonical check value for CRC-32/ISCSI. If this is wrong, everything
    // downstream silently rejects good saves.
    CHECK(crc32c(std::string_view("123456789")) == 0xE3069283u);
}

TEST_CASE("crc32c matches known Castagnoli vectors") {
    CHECK(crc32c(std::string_view("")) == 0x00000000u);
    CHECK(crc32c(std::string_view("a")) == 0xC1D04330u);
    CHECK(crc32c(std::string_view("abc")) == 0x364B3FB7u);
    CHECK(crc32c(std::string_view("hello world")) == 0xC99465AAu);

    // 32 zero bytes and 32 0xFF bytes — the two vectors RFC 3720 appendix B.4
    // pins for iSCSI.
    const std::vector<std::uint8_t> zeros(32, 0x00);
    const std::vector<std::uint8_t> ones(32, 0xFF);
    CHECK(crc32c(std::span<const std::uint8_t>(zeros)) == 0x8A9136AAu);
    CHECK(crc32c(std::span<const std::uint8_t>(ones)) == 0x62A8AB43u);
}

TEST_CASE("crc32c is NOT the IEEE CRC32 that miniz ships") {
    // The whole trap in one assertion. IEEE CRC32 of "123456789" is 0xCBF43926;
    // if these ever compare equal somebody swapped the polynomial.
    CHECK(crc32c(std::string_view("123456789")) != 0xCBF43926u);
}

TEST_CASE("crc32cUpdate composes across a split buffer") {
    const std::string whole = "the docks district of Trojia";
    const std::string head = whole.substr(0, 11);
    const std::string tail = whole.substr(11);

    const std::uint32_t oneShot = crc32c(bytesOf(whole));
    const std::uint32_t split = crc32cUpdate(crc32cUpdate(0u, bytesOf(head)), bytesOf(tail));
    CHECK(split == oneShot);
}

TEST_CASE("crc32c matches the CRC the real .trojsav files declare") {
    // Not a synthetic vector: inflate a section out of the owner's baked world
    // and check our checksum against the value the TOC carries.
    for (const fx::BakedWorldFacts& facts : {fx::kTavernFixture, fx::kDocksSurface}) {
        CAPTURE(facts.name);
        TrojSav save = TrojSav::readFile(fx::bakedMap(facts.name));

        const std::span<const std::uint8_t> meta = save.section(sections::kMeta);
        const std::span<const std::uint8_t> wrld = save.section(sections::kWrld);

        CHECK(crc32c(meta) == facts.metaCrc32c);
        CHECK(crc32c(wrld) == facts.wrldCrc32c);

        // And the TOC agrees with the pinned constants, so the pins are not
        // just this implementation agreeing with itself.
        for (const TrojSav::TocEntry& entry : save.toc()) {
            if (entry.id == sections::kMeta) {
                CHECK(entry.crc32c == facts.metaCrc32c);
            } else if (entry.id == sections::kWrld) {
                CHECK(entry.crc32c == facts.wrldCrc32c);
            }
        }
    }
}
