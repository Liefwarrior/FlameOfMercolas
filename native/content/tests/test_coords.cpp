#include "granadad/content/coords.hpp"

#include <doctest/doctest.h>

#include "granadad/content/format_error.hpp"

using namespace granadad::content;

TEST_CASE("packed position round-trips across the whole address space") {
    for (const std::int32_t x : {0, 1, 31, 32, 255, 4094, 4095}) {
        for (const std::int32_t y : {0, 1, 31, 32, 255, 4094, 4095}) {
            for (const std::int32_t z : {0, 1, 7, 8, 62, 63}) {
                const std::int32_t packed = packed_pos::pack(x, y, z);
                CHECK(packed_pos::x(packed) == x);
                CHECK(packed_pos::y(packed) == y);
                CHECK(packed_pos::z(packed) == z);
                // 30 bits, so the top two are always clear and the value is
                // never negative — queues store these as plain ints.
                CHECK(packed >= 0);
            }
        }
    }
}

TEST_CASE("packed position uses the documented shifts") {
    CHECK(packed_pos::pack(1, 0, 0) == 1);
    CHECK(packed_pos::pack(0, 1, 0) == (1 << 12));
    CHECK(packed_pos::pack(0, 0, 1) == (1 << 24));
    CHECK(packed_pos::pack(4095, 4095, 63) == 0x3FFFFFFF);
}

TEST_CASE("step is a wrapping add of the packed deltas") {
    const std::int32_t origin = packed_pos::pack(100, 200, 3);
    CHECK(packed_pos::step(origin, 1, 0, 0) == packed_pos::pack(101, 200, 3));
    CHECK(packed_pos::step(origin, -1, 0, 0) == packed_pos::pack(99, 200, 3));
    CHECK(packed_pos::step(origin, 0, 1, 0) == packed_pos::pack(100, 201, 3));
    CHECK(packed_pos::step(origin, 0, -1, 0) == packed_pos::pack(100, 199, 3));
    CHECK(packed_pos::step(origin, 0, 0, 1) == packed_pos::pack(100, 200, 4));
    CHECK(packed_pos::step(origin, 0, 0, -1) == packed_pos::pack(100, 200, 2));
    CHECK(packed_pos::step(origin, -1, -1, -1) == packed_pos::pack(99, 199, 2));
}

TEST_CASE("localIdx is x-fastest then y then z") {
    CHECK(Coords::localIdxOf(0, 0, 0) == 0);
    CHECK(Coords::localIdxOf(1, 0, 0) == 1);
    CHECK(Coords::localIdxOf(0, 1, 0) == 32);
    CHECK(Coords::localIdxOf(0, 0, 1) == 1024);
    CHECK(Coords::localIdxOf(31, 31, 7) == kTilesPerChunk - 1);
}

TEST_CASE("chunkIndex is (cz * chunksY + cy) * chunksX + cx") {
    const Coords coords(8, 6, 4);
    CHECK(coords.chunkCount() == 192);
    CHECK(coords.chunkIndexOf(0, 0, 0) == 0);
    CHECK(coords.chunkIndexOf(1, 0, 0) == 1);
    CHECK(coords.chunkIndexOf(0, 1, 0) == 8);
    CHECK(coords.chunkIndexOf(0, 0, 1) == 48);
    CHECK(coords.chunkIndexOf(7, 5, 3) == 191);

    for (std::int32_t i = 0; i < coords.chunkCount(); ++i) {
        CHECK(coords.chunkIndexOf(coords.chunkX(i), coords.chunkY(i), coords.chunkZ(i)) == i);
    }
}

TEST_CASE("packedPos and chunkIndex/localIdx are inverses over a whole world") {
    const Coords coords(4, 3, 3);
    for (std::int32_t chunkIndex = 0; chunkIndex < coords.chunkCount(); ++chunkIndex) {
        // Sampling the corners and a few interior cells covers every bit field
        // without decoding 300k positions.
        for (const std::int32_t localIdx : {0, 1, 31, 32, 1023, 1024, 4095, 8190, 8191}) {
            const std::int32_t packed = coords.packedPos(chunkIndex, localIdx);
            CHECK(coords.chunkIndex(packed) == chunkIndex);
            CHECK(Coords::localIdx(packed) == localIdx);
        }
    }
}

TEST_CASE("the VOID border ring is every outermost chunk") {
    const Coords coords(4, 3, 3);
    // 4x3x3 with a 1-chunk border leaves a 2x1x1 interior.
    std::int32_t interior = 0;
    for (std::int32_t i = 0; i < coords.chunkCount(); ++i) {
        if (!coords.isVoidBorder(i)) {
            ++interior;
        }
    }
    CHECK(interior == 2);
    CHECK(coords.isVoidBorder(coords.chunkIndexOf(0, 0, 0)));
    CHECK(coords.isVoidBorder(coords.chunkIndexOf(3, 2, 2)));
    CHECK_FALSE(coords.isVoidBorder(coords.chunkIndexOf(1, 1, 1)));
    CHECK_FALSE(coords.isVoidBorder(coords.chunkIndexOf(2, 1, 1)));
}

TEST_CASE("world dimensions outside the WorldConfig limits are rejected") {
    CHECK_NOTHROW(Coords::checked(3, 3, 3));
    CHECK_NOTHROW(Coords::checked(128, 128, 8));
    CHECK_THROWS_AS(Coords::checked(2, 3, 3), FormatError);
    CHECK_THROWS_AS(Coords::checked(3, 2, 3), FormatError);
    CHECK_THROWS_AS(Coords::checked(3, 3, 2), FormatError);
    CHECK_THROWS_AS(Coords::checked(129, 3, 3), FormatError);
    CHECK_THROWS_AS(Coords::checked(3, 3, 9), FormatError);
    CHECK_THROWS_AS(Coords::checked(-1, 3, 3), FormatError);
}
