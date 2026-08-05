#include "granadad/render/actor_sheet.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace granadad::render {

namespace {

[[nodiscard]] std::string readBytes(const std::filesystem::path& file) {
    // std::ifstream and not fopen, for the reason atlas.cpp records: the path
    // can be outside the active code page on Windows.
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// The tag sets, verbatim from the index's own `actorQueries` block. Held here
/// as a table rather than read out of the file, because the MAPPING from this
/// build's sixteen types to the index's eleven queries is this build's decision
/// and belongs in this build's source; the tags themselves are the owner's.
struct Query {
    sim::WardType type;
    const char* tags;
};

constexpr Query kQueries[] = {
    {sim::WardType::Serf, "actor,humanoid,laborer"},
    {sim::WardType::Sailor, "actor,humanoid,laborer"},
    {sim::WardType::Fisher, "actor,humanoid,laborer"},
    {sim::WardType::Carter, "actor,humanoid,laborer,keeper"},
    {sim::WardType::Shopkeeper, "actor,humanoid,merchant"},
    {sim::WardType::MilitiaWatch, "actor,humanoid,guard"},
    {sim::WardType::Wastrel, "actor,humanoid,vagrant"},
    {sim::WardType::Urchin, "actor,humanoid,vagrant,ragged"},
    {sim::WardType::Thief, "actor,humanoid,vagrant,ragged"},
    {sim::WardType::PriestOfTheFlame, "actor,humanoid,clergy,robed"},
    {sim::WardType::DiscipleOfTheFlame, "actor,humanoid,clergy,ragged"},
    {sim::WardType::AnimalKeeper, "actor,humanoid,laborer,keeper"},
    {sim::WardType::Dog, "actor,beast,livestock"},
    {sim::WardType::Stray, "actor,beast,vermin"},
    {sim::WardType::Cat, "actor,beast,cat"},
    {sim::WardType::Mouse, "actor,beast,mouse"},
};

static_assert(sizeof(kQueries) / sizeof(kQueries[0]) == sim::kWardTypeCount,
              "every ward type has to know what it looks like");

[[nodiscard]] std::vector<std::string> splitTags(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        if (end > start) {
            out.emplace_back(text.substr(start, end - start));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

[[nodiscard]] std::uint32_t packRgba(int r, int g, int b, int a) noexcept {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
}

/// Measures the ink. A sprite drawn fourteen rows tall inside a sixteen-row
/// cell is two rows of nothing, and a billboard sized to the cell stands those
/// two rows off the ground.
void measure(ActorSprite& sprite) {
    sprite.firstRow = ActorSprite::kPx;
    sprite.lastRow = -1;
    sprite.firstCol = ActorSprite::kPx;
    sprite.lastCol = -1;
    for (int v = 0; v < ActorSprite::kPx; ++v) {
        for (int u = 0; u < ActorSprite::kPx; ++u) {
            if ((sprite.texels[static_cast<std::size_t>(v * ActorSprite::kPx + u)] >> 24) < 128u) {
                continue;
            }
            sprite.firstRow = std::min(sprite.firstRow, v);
            sprite.lastRow = std::max(sprite.lastRow, v);
            sprite.firstCol = std::min(sprite.firstCol, u);
            sprite.lastCol = std::max(sprite.lastCol, u);
        }
    }
    if (sprite.lastRow < 0) {
        sprite.firstRow = 0;
        sprite.lastRow = ActorSprite::kPx - 1;
        sprite.firstCol = 0;
        sprite.lastCol = ActorSprite::kPx - 1;
    }
}

/// The fallback figure: the same three-part body the ellipse stack drew, in
/// pixels, so a checkout with no art tree still has people in it.
[[nodiscard]] ActorSprite proceduralFigure(std::uint32_t coat, std::uint32_t trouser,
                                           std::uint32_t skin, bool quadruped) {
    ActorSprite sprite;
    const auto put = [&](int u, int v, std::uint32_t rgba) {
        if (u < 0 || v < 0 || u >= ActorSprite::kPx || v >= ActorSprite::kPx) {
            return;
        }
        sprite.texels[static_cast<std::size_t>(v * ActorSprite::kPx + u)] = rgba;
    };
    if (quadruped) {
        for (int v = 8; v <= 12; ++v) {
            for (int u = 3; u <= 12; ++u) {
                put(u, v, coat);
            }
        }
        for (int v = 13; v <= 15; ++v) {
            put(4, v, trouser);
            put(6, v, trouser);
            put(10, v, trouser);
            put(12, v, trouser);
        }
        for (int v = 6; v <= 9; ++v) {
            for (int u = 11; u <= 14; ++u) {
                put(u, v, skin);
            }
        }
        measure(sprite);
        return sprite;
    }
    for (int v = 1; v <= 4; ++v) {  // head
        for (int u = 6; u <= 9; ++u) {
            put(u, v, skin);
        }
    }
    for (int v = 5; v <= 10; ++v) {  // torso
        for (int u = 5; u <= 10; ++u) {
            put(u, v, coat);
        }
    }
    for (int v = 11; v <= 15; ++v) {  // legs
        for (int u = 5; u <= 6; ++u) {
            put(u, v, trouser);
        }
        for (int u = 9; u <= 10; ++u) {
            put(u, v, trouser);
        }
    }
    measure(sprite);
    return sprite;
}

}  // namespace

std::string_view actorSheetQuery(sim::WardType type) noexcept {
    for (const Query& query : kQueries) {
        if (query.type == type) {
            return query.tags;
        }
    }
    return "actor,humanoid,laborer";
}

void ActorSheet::buildFallback() {
    // Colours picked to be told apart at a glance and in lamplight, which is
    // the same rule the ellipse stack's RoleLook table used. They are NOT the
    // MERCOLAS-24 palette -- the palette is in the sheet, and that is the point
    // of the sheet.
    struct Tone {
        sim::WardType type;
        std::uint32_t coat;
        std::uint32_t trouser;
        std::uint32_t skin;
        bool quadruped;
    };
    static constexpr Tone kTones[] = {
        {sim::WardType::Serf, 0xFF3E5A78u, 0xFF2B3E52u, 0xFF5878A0u, false},
        {sim::WardType::Shopkeeper, 0xFF2E7CB4u, 0xFF203C55u, 0xFF5878A0u, false},
        {sim::WardType::Sailor, 0xFF6B5A38u, 0xFF3B3120u, 0xFF5878A0u, false},
        {sim::WardType::Fisher, 0xFF52705Au, 0xFF2E3E30u, 0xFF5878A0u, false},
        {sim::WardType::Carter, 0xFF44506Au, 0xFF2A3040u, 0xFF5878A0u, false},
        {sim::WardType::MilitiaWatch, 0xFF3038A8u, 0xFF20244Fu, 0xFFB0B4C0u, false},
        {sim::WardType::Wastrel, 0xFF34424Cu, 0xFF232B32u, 0xFF4E6A88u, false},
        {sim::WardType::Urchin, 0xFF3A4A3Au, 0xFF242E24u, 0xFF5878A0u, false},
        {sim::WardType::Thief, 0xFF20242Cu, 0xFF16181Cu, 0xFF3C5068u, false},
        {sim::WardType::PriestOfTheFlame, 0xFFE0E4EAu, 0xFFD0D8E0u, 0xFF5878A0u, false},
        {sim::WardType::DiscipleOfTheFlame, 0xFFC8CCD4u, 0xFF9098A4u, 0xFF5878A0u, false},
        {sim::WardType::AnimalKeeper, 0xFF3C5A44u, 0xFF283A2Cu, 0xFF5878A0u, false},
        {sim::WardType::Dog, 0xFF2C4058u, 0xFF1E2C3Cu, 0xFF203040u, true},
        {sim::WardType::Stray, 0xFFD8DCE4u, 0xFF9CA4B0u, 0xFF404C58u, true},
        {sim::WardType::Cat, 0xFF283038u, 0xFF1C2228u, 0xFF303840u, true},
        {sim::WardType::Mouse, 0xFF303840u, 0xFF242A30u, 0xFF383F46u, true},
    };
    sprites_.clear();
    byType_.assign(sim::kWardTypeCount, {});
    for (const Tone& tone : kTones) {
        sprites_.push_back(proceduralFigure(tone.coat, tone.trouser, tone.skin, tone.quadruped));
        byType_[static_cast<std::size_t>(tone.type)].push_back(sprites_.size() - 1);
    }
}

ActorSheet ActorSheet::procedural() {
    ActorSheet sheet;
    sheet.buildFallback();
    return sheet;
}

ActorSheet ActorSheet::load(const std::filesystem::path& contentDir) {
    ActorSheet sheet;
    sheet.buildFallback();

    const std::filesystem::path dir = contentDir / "art" / "sprites";
    const std::string indexText = readBytes(dir / "sprite-index.json");
    if (indexText.empty()) {
        return sheet;
    }
    nlohmann::json index = nlohmann::json::parse(indexText, nullptr, false);
    if (index.is_discarded() || !index.contains("sprites")) {
        return sheet;
    }
    if (index.value("tilePx", 16) != ActorSprite::kPx) {
        // A sheet at another cell size is a sheet this build cannot read, and
        // saying so by falling back is better than reading it wrong.
        return sheet;
    }
    const std::string sheetBytes = readBytes(dir / "sprites.png");
    if (sheetBytes.empty()) {
        return sheet;
    }
    int sheetW = 0;
    int sheetH = 0;
    int channels = 0;
    stbi_uc* image = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(sheetBytes.data()),
                                           static_cast<int>(sheetBytes.size()), &sheetW, &sheetH,
                                           &channels, 4);
    if (image == nullptr) {
        return sheet;
    }

    // Read every ACTOR sprite out of the sheet, keeping its tags. The face
    // parts share this index and are deliberately skipped: the two tag
    // vocabularies are validated disjoint by the generator, so `actor` is a
    // sufficient filter and this build never has to know what a face part is.
    struct Entry {
        std::vector<std::string> tags;
        std::size_t sprite;
    };
    std::vector<Entry> entries;
    for (const nlohmann::json& row : index["sprites"]) {
        if (!row.contains("tags") || !row.contains("cell")) {
            continue;
        }
        std::vector<std::string> tags;
        for (const nlohmann::json& tag : row["tags"]) {
            tags.push_back(tag.get<std::string>());
        }
        if (std::find(tags.begin(), tags.end(), "actor") == tags.end()) {
            continue;
        }
        const int col = row["cell"][0].get<int>();
        const int rowIndex = row["cell"][1].get<int>();
        if (col < 0 || rowIndex < 0 || (col + 1) * ActorSprite::kPx > sheetW ||
            (rowIndex + 1) * ActorSprite::kPx > sheetH) {
            continue;
        }
        ActorSprite sprite;
        for (int v = 0; v < ActorSprite::kPx; ++v) {
            for (int u = 0; u < ActorSprite::kPx; ++u) {
                const std::size_t at =
                    (static_cast<std::size_t>(rowIndex * ActorSprite::kPx + v) *
                         static_cast<std::size_t>(sheetW) +
                     static_cast<std::size_t>(col * ActorSprite::kPx + u)) *
                    4;
                sprite.texels[static_cast<std::size_t>(v * ActorSprite::kPx + u)] =
                    packRgba(image[at], image[at + 1], image[at + 2], image[at + 3]);
            }
        }
        measure(sprite);
        sheet.sprites_.push_back(sprite);
        entries.push_back(Entry{std::move(tags), sheet.sprites_.size() - 1});
    }
    stbi_image_free(image);

    if (entries.empty()) {
        return sheet;
    }

    // Bind each type to every sprite carrying all of its tags. Sheet order, so
    // which variant an actor wears is a fact about the file rather than about
    // iteration.
    std::vector<std::vector<std::size_t>> bound(sim::kWardTypeCount);
    for (const Query& query : kQueries) {
        const std::vector<std::string> want = splitTags(query.tags);
        for (const Entry& entry : entries) {
            bool all = true;
            for (const std::string& tag : want) {
                if (std::find(entry.tags.begin(), entry.tags.end(), tag) == entry.tags.end()) {
                    all = false;
                    break;
                }
            }
            if (all) {
                bound[static_cast<std::size_t>(query.type)].push_back(entry.sprite);
            }
        }
    }
    // A type the pack has nothing for keeps its procedural figure. The
    // fallback sprites are already in sprites_ ahead of the authored ones, and
    // buildFallback put exactly one index per type into byType_, so this is a
    // per-type overwrite and never a half-empty table.
    for (std::size_t t = 0; t < sim::kWardTypeCount; ++t) {
        if (!bound[t].empty()) {
            sheet.byType_[t] = bound[t];
        }
    }
    sheet.fromAuthoredArt_ = true;
    return sheet;
}

const ActorSprite& ActorSheet::forType(sim::WardType type,
                                       std::uint32_t variantKey) const noexcept {
    const std::vector<std::size_t>& options = byType_[static_cast<std::size_t>(type)];
    const std::size_t pick = options.empty() ? 0 : variantKey % options.size();
    return sprites_[options.empty() ? 0 : options[pick]];
}

std::size_t ActorSheet::variantsFor(sim::WardType type) const noexcept {
    return byType_[static_cast<std::size_t>(type)].size();
}

}  // namespace granadad::render
