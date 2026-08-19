#pragma once

// Paths to the owner's baked worlds, plus the constants pinned from them.
//
// content/ is READ-ONLY canon. These tests open the real shipped files and
// never write anything back.

#include <cstdint>
#include <filesystem>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/trojsav.hpp"

namespace granadad::content::testing {

// contentDir() / bakedMap() moved to granadad/content/content_dir.hpp: the
// simulation's test suite and the twin-run gate resolve the same directory, and
// the docker build's proof that $GRANADAD_CONTENT_DIR is honoured is only worth
// anything if there is one implementation for it to be a proof about. Re-exported
// here so the existing test includes keep reading the way they did.
using granadad::content::bakedMap;
using granadad::content::contentDir;
using granadad::content::kContentDirEnvVar;

// --- facts pinned from the shipped files -----------------------------------
// Every value here was read out of the actual bytes, not out of the Java.

/// All three shipped worlds were baked from the same raws at tick 0.
inline constexpr std::uint64_t kShippedRawsFingerprint = 0x6101F30069B57FF1ull;

struct BakedWorldFacts {
    const char* name;
    std::uintmax_t fileBytes;
    std::int32_t chunksX;
    std::int32_t chunksY;
    std::int32_t chunksZ;
    std::int32_t chunkCount;
    std::uint32_t metaCrc32c;
    std::uint32_t wrldCrc32c;
    std::uint64_t metaUncompressedLen;
    std::uint64_t wrldUncompressedLen;
};

// Rebaked by the archetype-diversity pass (Docks building-material/height/roof-cap pass):
// gen_docks_surface.py changed material/height/roof-cap authoring on ~20 of the ward's ~35
// non-compound buildings (see tools/scripts/gen_docks_surface.py's own per-building "Archetype
// pass" comments), then content/maps/src/docks_surface.tmx was regenerated and re-baked via
// `import-map`. Only the WRLD-affecting facts below moved; META (dimensions, rawsFingerprint)
// did not, because the raws themselves are unchanged -- only which existing materials get
// painted where, and several roof-cap frect() calls were added or removed (FLOOR<->OPEN, not a
// footprint change, so chunk shape/count are identical too).
//
// REBAKED AGAIN by District Phase B (Thresholds, 2026-08-19): the Saltgate gate-house, the
// four compound gate frames and the Mission's lantern-turret. 128 authored cells changed
// across FIVE chunks and no others, which is why META is untouched a second time -- the raws,
// the dimensions and the chunk count cannot move when the pass only repaints cells inside
// chunks that already exist. wrldUncompressedLen is +602, and that number is not a mystery:
// the WRLD section is a per-chunk RLE frame per lane, and diffing the two decompressed
// sections frame by frame gives 123 (+328, the Mission turret), 125 (-2, C2's gate frame),
// 126 (+6, C4's), 131 (+250, the gate-house and C1's frame) and 132 (+20, C3's) -- summing to
// exactly +602, with the other 187 chunk frames byte-identical.
//
// REBAKED A THIRD TIME by District Phase C (Quarters, 2026-08-19): the Quayward's second
// gate, two gate-lintel roof bridges, one roof hut shrunk off a wall it was sealing a deck
// with, and three roof caches. TWENTY-SEVEN authored cells across FIVE chunks and no others,
// so META is untouched for the third time and for the third time for the same reason -- the
// raws, the dimensions and the chunk count cannot move when a pass only repaints cells inside
// chunks that already exist. wrldUncompressedLen is +29, decomposed frame by frame off the two
// decompressed sections, and every changed frame is a chunk one of the twenty-seven cells
// lands in:
//
//   chunk 125 (local x128-159 y64-95 z8-15)  +27   C2's 8-cell lintel bridge + 1 cache crate
//   chunk 126 (local x160-191 y64-95 z8-15)  +22   C4's 4-cell lintel bridge + 1 cache crate
//   chunk 131 (local x64-95  y96-127 z8-15)  +16   the Quayward gate's 2 ramp cells
//   chunk 132 (local x96-127 y96-127 z8-15)  -24   3 cells of the roofhut_12 shrink
//   chunk 133 (local x128-159 y96-127 z8-15) -12   8 cells of the shrink + 1 cache crate
//                                            ---
//                                            +29   the other 187 frames byte-identical
//
// The two NEGATIVE frames are the shrink paying for itself: turning a six-cell leather wall
// run into deck-material floor merges it with the runs either side, so those lanes encode in
// FEWER RLE runs than before. A pass that only ever built could not do that.
inline constexpr BakedWorldFacts kDocksSurface{
    /*name=*/"docks_surface",
    /*fileBytes=*/17954,
    /*chunksX=*/8,
    /*chunksY=*/6,
    /*chunksZ=*/4,
    /*chunkCount=*/192,
    /*metaCrc32c=*/0x12FBB6D6u,
    /*wrldCrc32c=*/0xA82FE35Au,
    /*metaUncompressedLen=*/77,
    /*wrldUncompressedLen=*/86707,
};

inline constexpr BakedWorldFacts kTavernFixture{
    /*name=*/"tavern_fixture",
    /*fileBytes=*/845,
    /*chunksX=*/4,
    /*chunksY=*/3,
    /*chunksZ=*/3,
    /*chunkCount=*/36,
    /*metaCrc32c=*/0x4990D67Bu,
    /*wrldCrc32c=*/0x817C47BDu,
    /*metaUncompressedLen=*/77,
    /*wrldUncompressedLen=*/5668,
};

inline constexpr BakedWorldFacts kCompoundBlock{
    /*name=*/"compound_block",
    /*fileBytes=*/2051,
    /*chunksX=*/6,
    /*chunksY=*/6,
    /*chunksZ=*/3,
    /*chunkCount=*/108,
    /*metaCrc32c=*/0u,  // not pinned; this world is the third-file cross-check
    /*wrldCrc32c=*/0u,
    /*metaUncompressedLen=*/77,
    /*wrldUncompressedLen=*/22483,
};

/// MATERIAL-lane ids, assigned by sorting the material string ids and numbering
/// from 0. 21 authored raws plus one treatment-minted derived material.
inline constexpr std::uint16_t kMaterialAsh = 0;
inline constexpr std::uint16_t kMaterialBrick = 1;
inline constexpr std::uint16_t kMaterialDirt = 6;
inline constexpr std::uint16_t kMaterialGranite = 8;
inline constexpr std::uint16_t kMaterialOak = 14;
inline constexpr std::uint16_t kMaterialRemanConcrete = 16;
inline constexpr std::uint16_t kMaterialThatch = 19;
inline constexpr std::uint16_t kMaterialTrudgeonWood = 20;
inline constexpr std::uint16_t kMaterialCount = 22;

}  // namespace granadad::content::testing
