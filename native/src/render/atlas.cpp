#include "granadad/render/atlas.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace granadad::render {

namespace {

/// The material registry's ordering, as a literal. See the header: the ids come
/// from sorting content/raws/materials' string ids and numbering from zero, and
/// test_atlas.cpp pins the entries the TROJSAV suite already reads out of the
/// shipped worlds (ash 0, brick 1, dirt 6, granite 8, oak 14, reman_concrete
/// 16, thatch 19, trudgeon_wood 20, 22 in total).
constexpr std::string_view kMaterialIds[] = {
    "ash",           "brick",          "brick_facade",   "chromatis",
    "chromatis_melt", "cloth",         "dirt",           "glowstone",
    "granite",       "granite_facade", "ice",            "leather",
    "lightstone",    "lightstone_shards", "oak",         "phorys",
    "reman_concrete", "reman_facade",  "steel",          "thatch",
    "trudgeon_wood", "trudgeon_wood@getilia_soak",
};

constexpr std::size_t kMaterialCount = sizeof(kMaterialIds) / sizeof(kMaterialIds[0]);

/// Base colours for the procedural fallback. Not the authored palette — the
/// authored palette is IN the sheet, which is the whole point of the pack. This
/// is only so a checkout with no art tree still renders something a person can
/// navigate by.
struct MaterialTone {
    std::string_view id;
    Rgb colour;
    bool speckled;
};

constexpr MaterialTone kFallbackTones[] = {
    {"ash", {0.24F, 0.23F, 0.22F}, true},
    {"brick", {0.44F, 0.24F, 0.19F}, false},
    {"brick_facade", {0.47F, 0.27F, 0.21F}, false},
    {"chromatis", {0.31F, 0.42F, 0.55F}, true},
    {"chromatis_melt", {0.38F, 0.30F, 0.44F}, true},
    {"cloth", {0.52F, 0.44F, 0.34F}, false},
    {"dirt", {0.32F, 0.26F, 0.18F}, true},
    {"glowstone", {0.62F, 0.58F, 0.36F}, true},
    {"granite", {0.35F, 0.35F, 0.37F}, true},
    {"granite_facade", {0.38F, 0.38F, 0.40F}, true},
    {"ice", {0.62F, 0.72F, 0.78F}, false},
    {"leather", {0.36F, 0.26F, 0.18F}, false},
    {"lightstone", {0.58F, 0.56F, 0.48F}, true},
    {"lightstone_shards", {0.50F, 0.49F, 0.44F}, true},
    {"oak", {0.40F, 0.29F, 0.17F}, false},
    {"phorys", {0.28F, 0.36F, 0.32F}, true},
    {"reman_concrete", {0.42F, 0.41F, 0.38F}, true},
    {"reman_facade", {0.45F, 0.44F, 0.41F}, true},
    {"steel", {0.44F, 0.46F, 0.49F}, false},
    {"thatch", {0.50F, 0.42F, 0.22F}, false},
    {"trudgeon_wood", {0.33F, 0.24F, 0.16F}, false},
    {"trudgeon_wood@getilia_soak", {0.28F, 0.22F, 0.19F}, false},
};

/// The pack's region suffix for each face kind.
constexpr std::string_view kFaceSuffix[kFaceKindCount] = {"face",  "floor", "ramp",
                                                          "stair", "top",   "water"};

/// The pack's form key for each face kind, inside `materials.<id>.forms`.
constexpr std::string_view kFaceFormKey[kFaceKindCount] = {"wall",  "floor", "ramp",
                                                           "stair", "block", ""};

[[nodiscard]] std::uint32_t hash32(std::uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] Rgb parseHexColour(std::string_view text, Rgb fallback) {
    if (text.size() < 7 || text.front() != '#') {
        return fallback;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    int component[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const int hi = nibble(text[static_cast<std::size_t>(1 + i * 2)]);
        const int lo = nibble(text[static_cast<std::size_t>(2 + i * 2)]);
        if (hi < 0 || lo < 0) {
            return fallback;
        }
        component[i] = hi * 16 + lo;
    }
    return Rgb{static_cast<float>(component[0]) / 255.0F,
               static_cast<float>(component[1]) / 255.0F,
               static_cast<float>(component[2]) / 255.0F};
}

}  // namespace

std::span<const std::string_view> materialIds() noexcept {
    return std::span<const std::string_view>(kMaterialIds, kMaterialCount);
}

std::size_t TileAtlas::appendProceduralTile(Rgb base, std::uint32_t seed, bool speckled) {
    const std::size_t tile = texels_.size() / kTileTexels;
    texels_.resize(texels_.size() + kTileTexels);
    for (int v = 0; v < kTilePx; ++v) {
        for (int u = 0; u < kTilePx; ++u) {
            const std::uint32_t noise =
                hash32(seed ^ (static_cast<std::uint32_t>(u) * 73856093U) ^
                       (static_cast<std::uint32_t>(v) * 19349663U));
            float grain = static_cast<float>(noise & 0xFFU) / 255.0F - 0.5F;
            grain *= speckled ? 0.22F : 0.10F;
            // A one-texel dark rim, so tile boundaries read at close range and
            // the chunkiness is deliberate rather than mush.
            const bool rim = u == 0 || v == 0;
            const float edge = rim ? -0.16F : 0.0F;
            const Rgb colour{base.r + grain + edge, base.g + grain + edge, base.b + grain + edge};
            texels_[tile * kTileTexels + static_cast<std::size_t>(v * kTilePx + u)] =
                packRgb(colour);
        }
    }
    return tile;
}

TileAtlas TileAtlas::procedural() {
    TileAtlas atlas;
    atlas.byMaterialFace_.assign(kMaterialCount,
                                 std::vector<std::vector<std::size_t>>(kFaceKindCount));
    atlas.missingTile_ = atlas.appendProceduralTile(Rgb{0.85F, 0.10F, 0.70F}, 0xDEADU, true);
    for (std::size_t m = 0; m < kMaterialCount; ++m) {
        const MaterialTone& tone = kFallbackTones[m];
        for (std::size_t f = 0; f < kFaceKindCount; ++f) {
            if (f == static_cast<std::size_t>(FaceKind::Water)) {
                continue;
            }
            // Tops read a shade lighter than sides, which is the cheapest
            // possible substitute for a lighting model and reads immediately.
            const float lift = f == static_cast<std::size_t>(FaceKind::Side) ? 0.0F : 0.10F;
            const Rgb shade{tone.colour.r + lift, tone.colour.g + lift, tone.colour.b + lift};
            const std::uint32_t seed =
                hash32(static_cast<std::uint32_t>(m) * 977U + static_cast<std::uint32_t>(f));
            atlas.byMaterialFace_[m][f].push_back(
                atlas.appendProceduralTile(shade, seed, tone.speckled));
        }
    }
    // Water is material-independent; give every material the same surface tile.
    const std::size_t water =
        atlas.appendProceduralTile(Rgb{0.06F, 0.13F, 0.16F}, 0x5EAU, true);
    for (std::size_t m = 0; m < kMaterialCount; ++m) {
        atlas.byMaterialFace_[m][static_cast<std::size_t>(FaceKind::Water)].push_back(water);
    }
    atlas.lightTintQ8_.reserve(32);
    for (int level = 0; level < 32; ++level) {
        // The same shape the authored pack ships: a floor of 36/256 so the
        // unlit world is dark rather than black, rising to 256 at full light.
        const float t = static_cast<float>(level) / 31.0F;
        atlas.lightTintQ8_.push_back(
            static_cast<std::int32_t>(36.0F + (256.0F - 36.0F) * t * t + 0.5F));
    }
    atlas.waterDepthAlphaQ8_ = {0, 96, 120, 144, 168, 192, 216, 240};
    return atlas;
}

TileAtlas TileAtlas::load(const std::filesystem::path& contentDir) {
    TileAtlas atlas = procedural();

    const std::filesystem::path packDir = contentDir / "art" / "custom";
    const std::string mappingText = readTextFile(packDir / "art-mapping.json");
    if (mappingText.empty()) {
        return atlas;
    }
    nlohmann::json mapping = nlohmann::json::parse(mappingText, nullptr, false);
    if (mapping.is_discarded() || !mapping.is_object()) {
        return atlas;
    }

    // stb is built with STBI_NO_STDIO, so the bytes are read here and decoded
    // from memory. That is also the portable answer on Windows, where stb's
    // own fopen would choke on a path outside the active code page.
    const std::string sheetBytes = readTextFile(packDir / "tiles.png");
    if (sheetBytes.empty()) {
        return atlas;
    }
    int sheetW = 0;
    int sheetH = 0;
    int channels = 0;
    stbi_uc* image = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(sheetBytes.data()),
        static_cast<int>(sheetBytes.size()), &sheetW, &sheetH, &channels, 4);
    if (image == nullptr) {
        return atlas;
    }

    const int tilePx = mapping.value("tilePx", kTilePx);
    if (tilePx != kTilePx) {
        stbi_image_free(image);
        return atlas;
    }
    const int columns = sheetW / kTilePx;

    // Copy every region's tiles out of the sheet into the flat store, keeping a
    // name -> index list so the material table below can point at them.
    std::vector<std::pair<std::string, std::vector<std::size_t>>> regions;
    if (mapping.contains("regions") && mapping["regions"].is_object()) {
        for (const auto& [name, cells] : mapping["regions"].items()) {
            if (!cells.is_array()) {
                continue;
            }
            std::vector<std::size_t> tiles;
            for (const nlohmann::json& cell : cells) {
                if (!cell.is_array() || cell.size() != 2) {
                    continue;
                }
                const int col = cell[0].get<int>();
                const int row = cell[1].get<int>();
                if (col < 0 || row < 0 || col >= columns ||
                    (row + 1) * kTilePx > sheetH) {
                    continue;
                }
                const std::size_t tile = atlas.texels_.size() / kTileTexels;
                atlas.texels_.resize(atlas.texels_.size() + kTileTexels);
                for (int v = 0; v < kTilePx; ++v) {
                    for (int u = 0; u < kTilePx; ++u) {
                        const std::size_t src =
                            (static_cast<std::size_t>(row * kTilePx + v) *
                                 static_cast<std::size_t>(sheetW) +
                             static_cast<std::size_t>(col * kTilePx + u)) *
                            4U;
                        const std::uint32_t r = image[src];
                        const std::uint32_t g = image[src + 1];
                        const std::uint32_t b = image[src + 2];
                        atlas.texels_[tile * kTileTexels +
                                      static_cast<std::size_t>(v * kTilePx + u)] =
                            r | (g << 8) | (b << 16) | 0xFF000000U;
                    }
                }
                tiles.push_back(tile);
            }
            if (!tiles.empty()) {
                regions.emplace_back(name, std::move(tiles));
            }
        }
    }
    stbi_image_free(image);
    if (regions.empty()) {
        return atlas;
    }

    // Sorted lookup, never a hash container: iteration order of this table
    // decides nothing here, but the ban is repo-wide and enforced by grep.
    const auto findRegion = [&regions](std::string_view name) -> const std::vector<std::size_t>* {
        for (const auto& entry : regions) {
            if (entry.first == name) {
                return &entry.second;
            }
        }
        return nullptr;
    };

    if (const std::vector<std::size_t>* missing =
            findRegion(mapping.value("missingRegion", std::string("missing")))) {
        atlas.missingTile_ = missing->front();
    }
    atlas.voidColour_ = parseHexColour(mapping.value("voidColor", std::string()),
                                       atlas.voidColour_);
    if (mapping.contains("lightTintQ8") && mapping["lightTintQ8"].is_array() &&
        mapping["lightTintQ8"].size() == 32) {
        atlas.lightTintQ8_ = mapping["lightTintQ8"].get<std::vector<std::int32_t>>();
    }
    if (mapping.contains("fluids") && mapping["fluids"].contains("water") &&
        mapping["fluids"]["water"].contains("depthAlphaQ8")) {
        const nlohmann::json& alpha = mapping["fluids"]["water"]["depthAlphaQ8"];
        if (alpha.is_array() && alpha.size() == 8) {
            atlas.waterDepthAlphaQ8_ = alpha.get<std::vector<std::int32_t>>();
        }
    }

    const std::vector<std::size_t>* water = findRegion("water");

    bool anyBound = false;
    for (std::size_t m = 0; m < kMaterialCount; ++m) {
        const std::string id(kMaterialIds[m]);
        for (std::size_t f = 0; f < kFaceKindCount; ++f) {
            std::vector<std::size_t> chosen;
            if (f == static_cast<std::size_t>(FaceKind::Water)) {
                if (water != nullptr) {
                    chosen = *water;
                }
            } else {
                // Prefer the pack's own byAppearance list, which carries the
                // authored variants; fall back to the bare `<id>.<suffix>`
                // region for anything the materials table does not name.
                const std::string formKey(kFaceFormKey[f]);
                if (mapping.contains("materials") && mapping["materials"].contains(id) &&
                    mapping["materials"][id].contains("forms") &&
                    mapping["materials"][id]["forms"].contains(formKey)) {
                    const nlohmann::json& form = mapping["materials"][id]["forms"][formKey];
                    if (form.contains("byAppearance") && form["byAppearance"].is_array()) {
                        for (const nlohmann::json& regionName : form["byAppearance"]) {
                            if (const std::vector<std::size_t>* tiles =
                                    findRegion(regionName.get<std::string>())) {
                                chosen.insert(chosen.end(), tiles->begin(), tiles->end());
                            }
                        }
                    }
                }
                if (chosen.empty()) {
                    if (const std::vector<std::size_t>* tiles =
                            findRegion(id + "." + std::string(kFaceSuffix[f]))) {
                        chosen = *tiles;
                    }
                }
            }
            if (!chosen.empty()) {
                atlas.byMaterialFace_[m][f] = std::move(chosen);
                anyBound = true;
            }
        }
    }

    // The three facade materials carry no art of their own; the pack's own
    // notes say so. Point them at the material they face, which is what a
    // facade IS, rather than at the missing-texture chequer.
    const auto alias = [&atlas](std::string_view facade, std::string_view base) {
        std::size_t facadeIndex = kMaterialCount;
        std::size_t baseIndex = kMaterialCount;
        for (std::size_t m = 0; m < kMaterialCount; ++m) {
            if (kMaterialIds[m] == facade) {
                facadeIndex = m;
            }
            if (kMaterialIds[m] == base) {
                baseIndex = m;
            }
        }
        if (facadeIndex == kMaterialCount || baseIndex == kMaterialCount) {
            return;
        }
        for (std::size_t f = 0; f < kFaceKindCount; ++f) {
            atlas.byMaterialFace_[facadeIndex][f] = atlas.byMaterialFace_[baseIndex][f];
        }
    };
    alias("brick_facade", "brick");
    alias("granite_facade", "granite");
    alias("reman_facade", "reman_concrete");

    atlas.fromAuthoredArt_ = anyBound;
    return atlas;
}

std::size_t TileAtlas::tileFor(std::uint16_t material, FaceKind face,
                               std::uint32_t variantKey) const noexcept {
    const std::size_t m = material;
    if (m >= byMaterialFace_.size()) {
        return missingTile_;
    }
    const std::vector<std::size_t>& variants =
        byMaterialFace_[m][static_cast<std::size_t>(face)];
    if (variants.empty()) {
        return missingTile_;
    }
    const std::uint32_t pick = hash32(variantKey) % static_cast<std::uint32_t>(variants.size());
    return variants[pick];
}

std::uint32_t TileAtlas::texelRaw(std::size_t tile, int u, int v) const noexcept {
    const std::size_t base = tile * kTileTexels;
    if (base + kTileTexels > texels_.size()) {
        return 0xFFFF00FFU;
    }
    const int uu = u & (kTilePx - 1);
    const int vv = v & (kTilePx - 1);
    return texels_[base + static_cast<std::size_t>(vv * kTilePx + uu)];
}

Rgb TileAtlas::texel(std::size_t tile, int u, int v) const noexcept {
    return unpackRgb(texelRaw(tile, u, v));
}

Rgb TileAtlas::averageOf(std::size_t tile) const noexcept {
    Rgb sum;
    for (int v = 0; v < kTilePx; ++v) {
        for (int u = 0; u < kTilePx; ++u) {
            const Rgb c = texel(tile, u, v);
            sum.r += c.r;
            sum.g += c.g;
            sum.b += c.b;
        }
    }
    const float inv = 1.0F / static_cast<float>(kTileTexels);
    return Rgb{sum.r * inv, sum.g * inv, sum.b * inv};
}

}  // namespace granadad::render
