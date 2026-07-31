#include "granadad/content/chunk_codec.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "granadad/content/world.hpp"

using namespace granadad::content;

namespace {

/// Builds codec frames byte by byte.
///
/// This is a TEST helper, not a production encoder — M0 is read-path only. It
/// exists so the RAW mode and the non-empty overlay blocks, neither of which
/// appears anywhere in the shipped corpus, are exercised against bytes rather
/// than against nothing.
class FrameBuilder {
public:
    FrameBuilder& u8(std::uint8_t value) {
        bytes_.push_back(value);
        return *this;
    }

    FrameBuilder& u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        return *this;
    }

    FrameBuilder& header(std::uint8_t laneCount, std::uint8_t version = kCodecVersion) {
        return u8(version).u8(laneCount);
    }

    /// A whole lane as one RLE run of 8192 identical cells.
    FrameBuilder& solidLane(int width, std::uint16_t value) {
        u8(static_cast<std::uint8_t>(width)).u8(kModeRle).u16(1).u16(
            static_cast<std::uint16_t>(kTilesPerChunk));
        return width == 1 ? u8(static_cast<std::uint8_t>(value)) : u16(value);
    }

    /// A whole lane in RAW mode from an explicit cell list.
    FrameBuilder& rawLane(int width, const std::vector<std::uint16_t>& cells) {
        REQUIRE(cells.size() == static_cast<std::size_t>(kTilesPerChunk));
        u8(static_cast<std::uint8_t>(width)).u8(kModeRaw);
        for (const std::uint16_t cell : cells) {
            if (width == 1) {
                u8(static_cast<std::uint8_t>(cell));
            } else {
                u16(cell);
            }
        }
        return *this;
    }

    FrameBuilder& overlayBlock(std::uint8_t ordinal,
                               const std::vector<std::pair<std::uint16_t, std::uint16_t>>& cells) {
        u8(ordinal).u16(static_cast<std::uint16_t>(cells.size()));
        for (const auto& [localIdx, value] : cells) {
            u16(localIdx).u16(value);
        }
        return *this;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

/// The smallest legal world: 3x3x3 chunks, all border.
World makeWorld() {
    return World(Coords(3, 3, 3), LaneLayout::core());
}

/// A frame with all seven core lanes solid, and one empty CHARGE block.
FrameBuilder emptyVoidFrame() {
    FrameBuilder frame;
    frame.header(7)
        .solidLane(2, 0)                                // material
        .solidLane(1, static_cast<std::uint8_t>(TileForm::Void))  // form
        .solidLane(1, flag_bits::kBlocksMove | flag_bits::kBlocksLight)  // flags
        .solidLane(2, 0)                                // temperature
        .solidLane(2, 0)                                // fluid
        .solidLane(2, 0)                                // light
        .solidLane(1, 0);                               // opacity
    frame.u8(1).overlayBlock(0, {});
    return frame;
}

}  // namespace

TEST_CASE("the empty VOID border frame decodes and is exactly 59 bytes") {
    // This is the literal frame every border chunk in every shipped world
    // carries. Size arithmetic: 2 + (8+7+7+8+8+8+7) + 1 + 3 = 59.
    const FrameBuilder frame = emptyVoidFrame();
    CHECK(frame.bytes().size() == 59);

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(frame.bytes(), "test frame");
    std::vector<DecodedOverlayCell> cells;
    codec.decode(in, world.chunk(0), cells);

    CHECK(in.exhausted());
    CHECK(cells.empty());
    CHECK(world.form(0) == TileForm::Void);
    CHECK(world.form(kTilesPerChunk - 1) == TileForm::Void);
    CHECK(world.flags(0) == (flag_bits::kBlocksMove | flag_bits::kBlocksLight));
    CHECK(world.material(0) == 0);
}

TEST_CASE("the empty VOID frame matches the bytes the shipped worlds carry") {
    // Byte-for-byte, from the brief's worked example and confirmed present in
    // all three baked files.
    const std::vector<std::uint8_t> expected{
        0x01,                                      // codecVersion
        0x07,                                      // laneCount
        0x02, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00,  // material w=2 RLE 1 run of 8192, value 0
        0x01, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00,        // form        value 0 (VOID)
        0x01, 0x00, 0x01, 0x00, 0x00, 0x20, 0x03,        // flags       value 3
        0x02, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00,  // temperature value 0
        0x02, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00,  // fluid       value 0
        0x02, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00,  // light       value 0
        0x01, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00,        // opacity     value 0
        0x01,                                            // overlayCount
        0x00, 0x00, 0x00,                                // CHARGE, 0 cells
    };
    CHECK(emptyVoidFrame().bytes() == expected);
}

TEST_CASE("RAW mode decodes a short lane") {
    // VERIFICATION GAP (M0) closure: no shipped chunk uses RAW, so this is the
    // only coverage the path gets. Alternating values would encode as 8192 runs
    // on the write side, far past the 4096-run RAW threshold.
    std::vector<std::uint16_t> cells(kTilesPerChunk);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cells[i] = static_cast<std::uint16_t>(i & 0xFFFFu);
    }

    FrameBuilder frame;
    frame.header(7)
        .rawLane(2, cells)
        .solidLane(1, static_cast<std::uint8_t>(TileForm::Wall))
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    frame.u8(1).overlayBlock(0, {});

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(frame.bytes(), "raw short frame");
    std::vector<DecodedOverlayCell> overlays;
    codec.decode(in, world.chunk(0), overlays);

    CHECK(in.exhausted());
    CHECK(world.material(0) == 0);
    CHECK(world.material(1) == 1);
    CHECK(world.material(4095) == 4095);
    CHECK(world.material(static_cast<std::size_t>(kTilesPerChunk) - 1) == 8191);
}

TEST_CASE("RAW mode decodes a byte lane") {
    std::vector<std::uint16_t> cells(kTilesPerChunk);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        // Six forms, cycled — 8192 runs on the encode side, past the 2730 RAW
        // threshold for a byte lane.
        cells[i] = static_cast<std::uint16_t>(i % kTileFormCount);
    }

    FrameBuilder frame;
    frame.header(7)
        .solidLane(2, 0)
        .rawLane(1, cells)
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    frame.u8(1).overlayBlock(0, {});

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(frame.bytes(), "raw byte frame");
    std::vector<DecodedOverlayCell> overlays;
    codec.decode(in, world.chunk(0), overlays);

    CHECK(in.exhausted());
    CHECK(world.form(0) == TileForm::Void);
    CHECK(world.form(1) == TileForm::Open);
    CHECK(world.form(5) == TileForm::Stair);
    CHECK(world.form(6) == TileForm::Void);
    CHECK(world.form(static_cast<std::size_t>(kTilesPerChunk) - 1) ==
          static_cast<TileForm>((kTilesPerChunk - 1) % kTileFormCount));
}

TEST_CASE("multi-run RLE reconstructs the exact cell pattern") {
    FrameBuilder frame;
    frame.header(7);
    // material: 100 x 8, then 8092 x 0
    frame.u8(2).u8(kModeRle).u16(2).u16(100).u16(8).u16(8092).u16(0);
    frame.solidLane(1, static_cast<std::uint8_t>(TileForm::Wall))
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    frame.u8(1).overlayBlock(0, {});

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(frame.bytes(), "multi-run frame");
    std::vector<DecodedOverlayCell> overlays;
    codec.decode(in, world.chunk(0), overlays);

    CHECK(in.exhausted());
    CHECK(world.material(0) == 8);
    CHECK(world.material(99) == 8);
    CHECK(world.material(100) == 0);
    CHECK(world.material(static_cast<std::size_t>(kTilesPerChunk) - 1) == 0);
}

TEST_CASE("overlay cells decode in ascending localIdx order") {
    // Second VERIFICATION GAP (M0) closure: every shipped overlay block is
    // empty, so non-empty blocks are only ever exercised here.
    FrameBuilder frame = emptyVoidFrame();
    // Rebuild with a populated CHARGE block instead of the empty one.
    FrameBuilder populated;
    populated.header(7)
        .solidLane(2, 0)
        .solidLane(1, static_cast<std::uint8_t>(TileForm::Void))
        .solidLane(1, 3)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    populated.u8(1).overlayBlock(0, {{7, 900}, {64, 1}, {8191, 65535}});

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(populated.bytes(), "overlay frame");
    std::vector<DecodedOverlayCell> cells;
    codec.decode(in, world.chunk(2), cells);

    CHECK(in.exhausted());
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].localIdx == 7);
    CHECK(cells[0].value == 900);
    CHECK(cells[1].localIdx == 64);
    CHECK(cells[1].value == 1);
    CHECK(cells[2].localIdx == 8191);
    CHECK(cells[2].value == 65535);
    for (const DecodedOverlayCell& cell : cells) {
        CHECK(cell.ordinal == static_cast<std::uint8_t>(OverlayId::Charge));
    }

    // And they land in the world at the right global tile indices.
    for (const DecodedOverlayCell& cell : cells) {
        world.putOverlay(OverlayId::Charge,
                         static_cast<std::uint32_t>(World::tileIndex(2, cell.localIdx)),
                         cell.value);
    }
    const std::span<const OverlayEntry> stored = world.overlay(OverlayId::Charge);
    REQUIRE(stored.size() == 3);
    CHECK(stored[0].tileIndex == 2 * kTilesPerChunk + 7);
    CHECK(stored[2].tileIndex == 2 * kTilesPerChunk + 8191);
    CHECK(stored[2].value == 65535);
}

// ---------------------------------------------------------------------------
// Strictness. Corruption must be rejected, not absorbed.
// ---------------------------------------------------------------------------

namespace {

void expectReject(const FrameBuilder& frame, const char* label) {
    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(frame.bytes(), label);
    std::vector<DecodedOverlayCell> cells;
    CHECK_THROWS_AS(codec.decode(in, world.chunk(0), cells), FormatError);
}

}  // namespace

TEST_CASE("a future codec version is rejected rather than guessed at") {
    FrameBuilder frame;
    frame.header(7, 2).solidLane(2, 0);
    expectReject(frame, "bad version");
}

TEST_CASE("a lane-count mismatch is rejected") {
    FrameBuilder frame;
    frame.header(6).solidLane(2, 0);
    expectReject(frame, "bad lane count");
}

TEST_CASE("a lane-width mismatch is rejected") {
    FrameBuilder frame;
    frame.header(7).solidLane(1, 0);  // material declared 1 byte, world says 2
    expectReject(frame, "bad lane width");
}

TEST_CASE("runs that do not cover exactly 8192 cells are rejected") {
    SUBCASE("short") {
        FrameBuilder frame;
        frame.header(7);
        frame.u8(2).u8(kModeRle).u16(1).u16(8191).u16(0);
        expectReject(frame, "runs short");
    }
    SUBCASE("overrun") {
        FrameBuilder frame;
        frame.header(7);
        frame.u8(2).u8(kModeRle).u16(2).u16(8192).u16(0).u16(1).u16(0);
        expectReject(frame, "runs overrun");
    }
    SUBCASE("zero-length run") {
        FrameBuilder frame;
        frame.header(7);
        frame.u8(2).u8(kModeRle).u16(2).u16(0).u16(0).u16(8192).u16(0);
        expectReject(frame, "zero run");
    }
}

TEST_CASE("an unknown encoding mode is rejected") {
    FrameBuilder frame;
    frame.header(7);
    frame.u8(2).u8(7);
    expectReject(frame, "bad mode");
}

TEST_CASE("an unknown overlay ordinal is rejected") {
    // No forward compatibility by design: an unknown overlay would mean
    // silently dropping state the world hash depends on.
    FrameBuilder frame = emptyVoidFrame();
    FrameBuilder bad;
    bad.header(7)
        .solidLane(2, 0)
        .solidLane(1, 0)
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    bad.u8(1).overlayBlock(1, {});  // ordinal 1 does not exist
    expectReject(bad, "bad overlay ordinal");
}

TEST_CASE("out-of-order overlay ordinals are rejected") {
    FrameBuilder bad;
    bad.header(7)
        .solidLane(2, 0)
        .solidLane(1, 0)
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    bad.u8(2).overlayBlock(0, {}).overlayBlock(0, {});  // repeated ordinal
    expectReject(bad, "repeated overlay ordinal");
}

TEST_CASE("non-ascending overlay cells are rejected") {
    FrameBuilder bad;
    bad.header(7)
        .solidLane(2, 0)
        .solidLane(1, 0)
        .solidLane(1, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(2, 0)
        .solidLane(1, 0);
    bad.u8(1).overlayBlock(0, {{64, 1}, {7, 2}});
    expectReject(bad, "descending overlay cells");
}

TEST_CASE("a truncated frame is rejected rather than read past the end") {
    FrameBuilder frame = emptyVoidFrame();
    std::vector<std::uint8_t> truncated = frame.bytes();
    truncated.resize(truncated.size() - 4);

    World world = makeWorld();
    ChunkCodec codec(world.lanes());
    ByteReader in(truncated, "truncated frame");
    std::vector<DecodedOverlayCell> cells;
    CHECK_THROWS_AS(codec.decode(in, world.chunk(0), cells), FormatError);
}
