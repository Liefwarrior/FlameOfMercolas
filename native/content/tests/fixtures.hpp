#pragma once

// Paths to the owner's baked worlds, plus the constants pinned from them.
//
// content/ is READ-ONLY canon. These tests open the real shipped files and
// never write anything back.

#include <cstdint>
#include <filesystem>
#include <string>

#include "granadad/content/trojsav.hpp"

namespace granadad::content::testing {

/// The repo's content/ directory, baked in at configure time.
inline std::filesystem::path contentDir() {
    return std::filesystem::path(GRANADAD_CONTENT_DIR);
}

inline std::filesystem::path bakedMap(const std::string& name) {
    return contentDir() / "maps" / "baked" / (name + ".trojsav");
}

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

inline constexpr BakedWorldFacts kDocksSurface{
    /*name=*/"docks_surface",
    /*fileBytes=*/17695,
    /*chunksX=*/8,
    /*chunksY=*/6,
    /*chunksZ=*/4,
    /*chunkCount=*/192,
    /*metaCrc32c=*/0x12FBB6D6u,
    /*wrldCrc32c=*/0x7E6E9738u,
    /*metaUncompressedLen=*/77,
    /*wrldUncompressedLen=*/86436,
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
