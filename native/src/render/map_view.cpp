#include "granadad/render/map_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "granadad/content/lanes.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks_signs_generated.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render {

namespace {

/// How many bands BELOW the player's own the plan still reads for water. Six
/// reaches the harbour surface (z18) from the roof-slum plane (z22) with a
/// band to spare, so "the harbour water is always shown" is true from every
/// walkable band this district has.
constexpr std::int32_t kWaterScanBands = 6;

/// How many bands below the player's own still draw as (dimmed) ground.
/// Three: from the roofs (z22) that reaches the upper and mid-slope streets;
/// from the quayside it reaches the strand under the piers and stops well
/// short of the unbuilt substrate.
constexpr std::int32_t kLowerScanBands = 3;

/// THE COLOUR DISCIPLINE, KEPT. Way names are a step greyer than door names so
/// the street grid reads as background and the doors -- the things the owner
/// could not locate -- read as figures. That rule survived the rewrite
/// unchanged; only where the ink lands did.
constexpr Rgb kDoorInk{0.95F, 0.93F, 0.84F};
constexpr Rgb kWayInk{0.66F, 0.65F, 0.60F};
constexpr Rgb kDoorDot{0.98F, 0.82F, 0.36F};
constexpr Rgb kPlayerTone{1.0F, 0.97F, 0.88F};
/// The cursor. panel.hpp's own accent, because the box around the selected
/// footprint and the inverted fill on the selected row of the Index are the
/// same object seen twice and must not be two different yellows.
constexpr Rgb kCursorTone{0.98F, 0.86F, 0.42F};

/// The zoom ladder: fit, then integer multiples of it. See the header's scale
/// ruling. Four steps is enough to take the smallest authored footprint from
/// unnamed to named at every window size this build runs at, and few enough
/// that the player can walk the whole ladder without thinking about it.
constexpr int kZoomSteps = 4;

// ---------------------------------------------------------------------------
// the deduped place table
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<MapPlace> buildPlaces() {
    namespace signs = sim::docks;
    std::vector<MapPlace> out;
    out.reserve(signs::kSignCount);
    for (std::size_t i = 0; i < signs::kSignCount; ++i) {
        const signs::Sign& sign = signs::kSigns[i];
        MapPlace place;
        place.id = sign.id;
        place.name = sign.place;
        place.what = sign.what;
        place.way = sign.kind == signs::SignKind::Way;
        place.x0 = sign.x0;
        place.y0 = sign.y0;
        place.x1 = sign.x1;
        place.y1 = sign.y1;
        place.anchorX = sign.anchorX;
        place.anchorY = sign.anchorY;
        place.band = sign.band;
        place.segments.push_back(MapRect{sign.x0, sign.y0, sign.x1, sign.y1});
        // DEDUPED BY NAME, LARGEST FOOTPRINT WINS THE LABEL -- and every other
        // segment is KEPT, in `segments`, rather than thrown away. Tarwalk is
        // signed nine times; the plan says its name once, on the segment with
        // the most room to set it in, and the cursor still knows about all
        // nine. See MapPlace::segments for the three places that matters.
        // Ties by id, so the pick cannot drift between runs.
        bool merged = false;
        for (MapPlace& held : out) {
            if (held.name != place.name) {
                continue;
            }
            merged = true;
            const bool better = place.area() > held.area() ||
                                (place.area() == held.area() && place.id < held.id);
            if (better) {
                std::vector<MapRect> segments = std::move(held.segments);
                segments.push_back(place.segments.front());
                held = place;
                held.segments = std::move(segments);
            } else {
                held.segments.push_back(place.segments.front());
            }
            break;
        }
        if (!merged) {
            out.push_back(std::move(place));
        }
    }
    std::sort(out.begin(), out.end(), [](const MapPlace& a, const MapPlace& b) {
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.id < b.id;
    });
    return out;
}

[[nodiscard]] float centreXOf(const MapPlace& place) noexcept {
    return (static_cast<float>(place.x0) + static_cast<float>(place.x1) + 1.0F) * 0.5F;
}
[[nodiscard]] float centreYOf(const MapPlace& place) noexcept {
    return (static_cast<float>(place.y0) + static_cast<float>(place.y1) + 1.0F) * 0.5F;
}

/// Upper case, and nothing else -- the register every panel in this build sets
/// its labels in.
[[nodiscard]] std::string shout(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// classification
// ---------------------------------------------------------------------------

MapCell mapCellAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                  std::int32_t band) noexcept {
    // Masonry at the player's own band. form(), not solid(): solid() also
    // answers true for VOID, and the world's own border ring drawn as a wall
    // would put a rampart around the district that no body can walk to.
    if (tiles.form(x, y, band) == content::TileForm::Wall) {
        return MapCell::Wall;
    }
    if (tiles.walkable(x, y, band)) {
        return MapCell::Floor;
    }
    // Water before lower ground: the harbour surface at z18 read from the
    // quay (z19) must say WATER, not "there is a seabed down there somewhere".
    for (std::int32_t z = band; z >= band - kWaterScanBands; --z) {
        if (tiles.fluidDepth(x, y, z) >= sim::kBlockingFluidDepth) {
            return MapCell::Water;
        }
    }
    for (std::int32_t z = band - 1; z >= band - kLowerScanBands; --z) {
        if (tiles.walkable(x, y, z)) {
            return MapCell::LowerFloor;
        }
    }
    return MapCell::Void;
}

// ---------------------------------------------------------------------------
// the palette
// ---------------------------------------------------------------------------

MapPalette MapPalette::fromAtlas(const TileAtlas& atlas) {
    MapPalette out;
    const std::size_t materials = materialIds().size();
    out.wall.reserve(materials);
    out.floor.reserve(materials);
    for (std::size_t m = 0; m < materials; ++m) {
        const std::uint16_t material = static_cast<std::uint16_t>(m);
        // The material's OWN tones, off the same tile images the first-person
        // pass draws -- variantKey 0, which is a pure function of nothing, so
        // the plan cannot shimmer between frames. Walls sit LOW and floors
        // sit LIFTED so the ground reads light and the masonry dark whatever
        // the material's own albedo is; the material tone survives inside
        // that register, which is what makes brick and granite tell apart.
        const Rgb side = atlas.averageOf(atlas.tileFor(material, FaceKind::Side, 0));
        const Rgb top = atlas.averageOf(atlas.tileFor(material, FaceKind::FloorTop, 0));
        out.wall.push_back(side * 0.52F);
        const Rgb lifted = top * 0.85F + Rgb{0.10F, 0.10F, 0.09F};
        out.floor.push_back(Rgb{std::min(1.0F, lifted.r), std::min(1.0F, lifted.g),
                                std::min(1.0F, lifted.b)});
    }
    // The water region is material-independent in the pack; tilt its average
    // toward blue-green so depth-7 harbour reads as water even out of a pack
    // whose water tile is mostly foam.
    const Rgb wet = atlas.averageOf(atlas.tileFor(0, FaceKind::Water, 0));
    out.water = Rgb{wet.r * 0.35F, wet.g * 0.55F + 0.06F, wet.b * 0.75F + 0.12F};
    out.voidTone = atlas.voidColour();
    return out;
}

Rgb MapPalette::wallTone(std::uint16_t material) const noexcept {
    const std::size_t index = material;
    return index < wall.size() ? wall[index] : Rgb{0.22F, 0.20F, 0.20F};
}

Rgb MapPalette::floorTone(std::uint16_t material) const noexcept {
    const std::size_t index = material;
    return index < floor.size() ? floor[index] : Rgb{0.55F, 0.52F, 0.48F};
}

// ---------------------------------------------------------------------------
// frame geometry
// ---------------------------------------------------------------------------

MapBounds mapContentBounds(const sim::TileQuery& tiles) {
    MapBounds bounds;
    bounds.minX = tiles.sizeX();
    bounds.minY = tiles.sizeY();
    bounds.maxX = 0;
    bounds.maxY = 0;
    for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
        for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
            bool authored = false;
            for (std::int32_t z = 0; z < tiles.sizeZ() && !authored; ++z) {
                const content::TileForm form = tiles.form(x, y, z);
                authored = form != content::TileForm::Void;
            }
            if (!authored) {
                continue;
            }
            bounds.minX = std::min(bounds.minX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }
    if (bounds.maxX < bounds.minX) {
        // An empty world (a unit test's ten-tile box). One tile of nothing.
        bounds = MapBounds{0, 0, 0, 0};
    }
    return bounds;
}

// ---------------------------------------------------------------------------
// the places
// ---------------------------------------------------------------------------

const std::vector<MapPlace>& mapPlaces() {
    static const std::vector<MapPlace> table = buildPlaces();
    return table;
}

int mapPlaceIndex(std::string_view name) {
    const std::vector<MapPlace>& places = mapPlaces();
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (places[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int mapPlaceUnder(std::int32_t x, std::int32_t y) {
    const std::vector<MapPlace>& places = mapPlaces();
    int best = -1;
    std::int64_t bestArea = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (!places[i].contains(x, y)) {
            continue;
        }
        // SMALLEST WINS: footprints nest, and the inner one is what a finger
        // on that ground means.
        if (best < 0 || places[i].area() < bestArea) {
            best = static_cast<int>(i);
            bestArea = places[i].area();
        }
    }
    return best;
}

void mapAimPoint(const MapPlace& place, std::int32_t fromX, std::int32_t fromY,
                 std::int32_t& outX, std::int32_t& outY) {
    outX = static_cast<std::int32_t>(place.anchorX);
    outY = static_cast<std::int32_t>(place.anchorY);
    if (place.segments.size() <= 1) {
        return;  // one signed point, and it is the way in
    }
    std::int64_t bestD = -1;
    for (const MapRect& rect : place.segments) {
        const std::int32_t cx = std::clamp(fromX, rect.x0, rect.x1);
        const std::int32_t cy = std::clamp(fromY, rect.y0, rect.y1);
        const std::int64_t dx = cx - fromX;
        const std::int64_t dy = cy - fromY;
        const std::int64_t d = dx * dx + dy * dy;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            outX = cx;
            outY = cy;
        }
    }
}

int mapWayUnder(std::int32_t x, std::int32_t y, std::int32_t reachTiles) {
    // WALKS THE RAW SIGN TABLE, NOT mapPlaces(), and that is the whole reason
    // this function is not two lines of mapPlaceUnder.
    //
    // mapPlaces() keeps ONE segment per street name -- the roomiest, because
    // the label has to fit inside the shape -- so the Tarwalk in that table is
    // the worn eastern stretch and nothing else. Asking it "which street is
    // (153,65) on" gets NO for an answer even though (153,65) is standing on
    // the Tarwalk, in the Gull's own segment. The FIRST version of this
    // function did exactly that and the gate caught it. A street's identity is
    // its NAME; every segment carries the name; so the containment test runs
    // over every authored post and the answer is handed back as the deduped
    // index of the name it found.
    namespace signs = sim::docks;
    const signs::Sign* best = nullptr;
    std::int64_t bestArea = 0;
    for (std::size_t i = 0; i < signs::kSignCount; ++i) {
        const signs::Sign& sign = signs::kSigns[i];
        if (sign.kind != signs::SignKind::Way) {
            continue;
        }
        if (x < sign.x0 || x > sign.x1 || y < sign.y0 || y > sign.y1) {
            continue;
        }
        const std::int64_t area = static_cast<std::int64_t>(sign.x1 - sign.x0 + 1) *
                                  static_cast<std::int64_t>(sign.y1 - sign.y0 + 1);
        if (best == nullptr || area < bestArea ||
            (area == bestArea && std::strcmp(sign.id, best->id) < 0)) {
            best = &sign;
            bestArea = area;
        }
    }
    if (best == nullptr && reachTiles > 0) {
        // A door whose anchor sits ON its own doorstep rather than in the
        // roadway: the nearest signed post inside reach names the street it
        // fronts. Ties by id, so the answer cannot drift between runs.
        const std::int64_t reach = static_cast<std::int64_t>(reachTiles) * reachTiles;
        std::int64_t bestD = 0;
        for (std::size_t i = 0; i < signs::kSignCount; ++i) {
            const signs::Sign& sign = signs::kSigns[i];
            if (sign.kind != signs::SignKind::Way) {
                continue;
            }
            const std::int64_t dx = static_cast<std::int64_t>(sign.anchorX) - x;
            const std::int64_t dy = static_cast<std::int64_t>(sign.anchorY) - y;
            const std::int64_t d = dx * dx + dy * dy;
            if (d > reach) {
                continue;
            }
            if (best == nullptr || d < bestD ||
                (d == bestD && std::strcmp(sign.id, best->id) < 0)) {
                best = &sign;
                bestD = d;
            }
        }
    }
    if (best == nullptr) {
        return -1;
    }
    return mapPlaceIndex(best->place);
}

int mapPlaceToward(int from, MapStep step) {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return from;
    }
    const int at = std::clamp(from, 0, static_cast<int>(places.size()) - 1);
    const float fx = centreXOf(places[static_cast<std::size_t>(at)]);
    const float fy = centreYOf(places[static_cast<std::size_t>(at)]);

    int best = at;
    float bestScore = 0.0F;
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (static_cast<int>(i) == at) {
            continue;
        }
        const float dx = centreXOf(places[i]) - fx;
        const float dy = centreYOf(places[i]) - fy;
        float along = 0.0F;
        float across = 0.0F;
        switch (step) {
            case MapStep::North:
                along = -dy;
                across = std::fabs(dx);
                break;
            case MapStep::South:
                along = dy;
                across = std::fabs(dx);
                break;
            case MapStep::East:
                along = dx;
                across = std::fabs(dy);
                break;
            case MapStep::West:
            default:
                along = -dx;
                across = std::fabs(dy);
                break;
        }
        if (along <= 0.0F) {
            continue;  // not that way at all
        }
        // NEAREST ALONG THE DIRECTION OF TRAVEL, PENALISED FOR DRIFT. A pure
        // "nearest in the half-plane" pick sidles along the coast instead of
        // walking inland; a pure "smallest angle" pick jumps the width of the
        // ward to find a place dead ahead. Two-to-one is the ratio that walks
        // the Tarwalk east to west without ever leaving it.
        const float score = along + 2.0F * across;
        if (best == at || score < bestScore ||
            (score == bestScore && places[i].id < places[static_cast<std::size_t>(best)].id)) {
            best = static_cast<int>(i);
            bestScore = score;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// THE ONE LABEL RULE
// ---------------------------------------------------------------------------

int mapLabelScale(int frameHeight) noexcept { return std::max(1, frameHeight / 540); }

std::vector<std::string> fitFootprintLabel(std::string_view name, int widthPx, int heightPx,
                                           int labelScale) {
    const int scale = std::max(1, labelScale);
    const int glyph = 5 * scale;   // drawText's own advance
    const int rowPx = 7 * scale;   // the 4x6 cell plus its shadow row
    if (widthPx < glyph || heightPx < 6 * scale) {
        return {};
    }
    std::string text = shout(name);
    // DROP A LEADING ARTICLE. Eleven of the forty doors are "The Something",
    // and on a map face the article is four glyphs that name nothing. This is
    // the only word the rule ever removes and it removes it from every name
    // that has one.
    if (text.rfind("THE ", 0) == 0) {
        text = text.substr(4);
    }

    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t space = text.find(' ', at);
        const std::size_t end = space == std::string::npos ? text.size() : space;
        if (end > at) {
            words.push_back(text.substr(at, end - at));
        }
        at = end == text.size() ? end : end + 1;
    }
    if (words.empty()) {
        return {};
    }

    const int columns = widthPx / glyph;
    const int rows = heightPx / rowPx;
    if (columns <= 0 || rows <= 0) {
        return {};
    }
    // NEVER A WORD CUT IN HALF. "THE SLOP-CH" names nothing, so a name with a
    // word wider than the shape gets NO label at all and falls back to the
    // door dot, the cursor and the Index. That is the fallback firing, and it
    // is deliberate -- see the header.
    for (const std::string& word : words) {
        if (static_cast<int>(word.size()) > columns) {
            return {};
        }
    }

    std::vector<std::string> lines;
    std::string current;
    for (const std::string& word : words) {
        if (current.empty()) {
            current = word;
        } else if (static_cast<int>(current.size() + 1 + word.size()) <= columns) {
            current += ' ';
            current += word;
        } else {
            lines.push_back(current);
            current = word;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    if (static_cast<int>(lines.size()) > rows) {
        return {};
    }
    return lines;
}

// ---------------------------------------------------------------------------
// the viewport
// ---------------------------------------------------------------------------

std::int32_t MapViewport::tileOfPx(int px) const noexcept {
    const int local = px - originX;
    const int tile = local >= 0 ? local / std::max(1, scale)
                                : -((-local + scale - 1) / std::max(1, scale));
    return bounds.minX + tile;
}

std::int32_t MapViewport::tileOfPy(int py) const noexcept {
    const int local = py - originY;
    const int tile = local >= 0 ? local / std::max(1, scale)
                                : -((-local + scale - 1) / std::max(1, scale));
    return bounds.minY + tile;
}

int mapFitScale(const PanelRect& pane, const MapBounds& bounds) noexcept {
    const int tilesX = std::max(1, bounds.tilesX());
    const int tilesY = std::max(1, bounds.tilesY());
    return std::max(1, std::min(std::max(0, pane.w) / tilesX, std::max(0, pane.h) / tilesY));
}

int mapZoomSteps() noexcept { return kZoomSteps; }

int mapZoomScale(int step, const PanelRect& pane, const MapBounds& bounds) noexcept {
    const int clamped = std::clamp(step, 0, kZoomSteps - 1);
    return mapFitScale(pane, bounds) * (clamped + 1);
}

MapViewport mapViewport(const PanelRect& pane, const MapBounds& bounds, int scale, float centreX,
                        float centreY) noexcept {
    MapViewport view;
    view.pane = pane;
    view.bounds = bounds;
    view.scale = std::max(1, scale);
    const int planW = bounds.tilesX() * view.scale;
    const int planH = bounds.tilesY() * view.scale;
    // The plan's own top-left, if it were centred on (centreX, centreY).
    const float localX = centreX - static_cast<float>(bounds.minX);
    const float localY = centreY - static_cast<float>(bounds.minY);
    int originX = pane.x + pane.w / 2 - static_cast<int>(localX * static_cast<float>(view.scale));
    int originY = pane.y + pane.h / 2 - static_cast<int>(localY * static_cast<float>(view.scale));
    // CLAMPED, so the plan never pulls a dead margin into the pane while there
    // is district left to show; a plan SMALLER than its pane is centred in it
    // instead, which is what the whole-ward zoom does at every ordinary size.
    if (planW <= pane.w) {
        originX = pane.x + (pane.w - planW) / 2;
    } else {
        originX = std::clamp(originX, pane.x + pane.w - planW, pane.x);
    }
    if (planH <= pane.h) {
        originY = pane.y + (pane.h - planH) / 2;
    } else {
        originY = std::clamp(originY, pane.y + pane.h - planH, pane.y);
    }
    view.originX = originX;
    view.originY = originY;
    return view;
}

int mapPlaceAtPixel(const MapViewport& view, int px, int py) {
    if (px < view.pane.x || px >= view.pane.right() || py < view.pane.y ||
        py >= view.pane.bottom()) {
        return -1;
    }
    return mapPlaceUnder(view.tileOfPx(px), view.tileOfPy(py));
}

// ---------------------------------------------------------------------------
// the composition
// ---------------------------------------------------------------------------

namespace {

/// THE SHARES THE COMPOSITION WILL TRY FOR THE MAP PANE, out of 100, and it
/// takes THE ONE THAT BUYS THE PLAN A WHOLE EXTRA PIXEL PER TILE.
///
/// A fixed share is a guess, and this page cannot afford one, because the plan
/// only ever grows in WHOLE pixels per tile: a map pane one cell short of the
/// next rung is a map pane whose extra cells do literally nothing for the
/// picture, and those cells are worth more to the detail pane. The first
/// capture at 1920x1080 proved both halves of that -- a fixed 66 came out one
/// cell short of the minimum detail width, collapsed the split, and left the
/// plan floating in the middle of a frame twice its size with nothing beside
/// it (docs/frames/map/after-ward-1920x1080.png, first version).
///
/// So the composition asks the same question keys_page.cpp asks about its list:
/// would another notch actually buy anything? Widest fit scale wins; on a tie
/// the NARROWEST share wins, because the cells it gives back are cells the
/// facts can use. Still a FIXED composition -- it responds to the window and to
/// the district's own dimensions, never to a player.
constexpr int kMapShares[] = {58, 62, 66};
/// Thirty cells is a plan you can still read a quarter of the ward off.
constexpr int kMinMapCells = 30;
/// Twenty-two cells holds "STANDS ON" and a street name at a common value
/// column. Deliberately no larger, because THE BIGGEST WINDOW IS THE NARROWEST
/// IN CELLS -- hudMinorScale steps up with height, so 1920x1080 has 76 cells
/// across where 960x540 has 96 -- and a floor that ignored that inversion
/// loses the detail pane at the resolution with the most pixels in it, which is
/// exactly what twenty-four did.
constexpr int kMinDetailCells = 22;

[[nodiscard]] std::string_view tabName(MapTab tab) noexcept {
    switch (tab) {
        case MapTab::People:
            return "PEOPLE";
        case MapTab::Index:
            return "INDEX";
        case MapTab::Legend:
            return "LEGEND";
        case MapTab::Overview:
        default:
            return "OVERVIEW";
    }
}

/// The four verbs along the foot of the page. Built in one place because the
/// COMPOSITION has to know how wide they are before it can decide how many rows
/// to give them -- see navRowsFor.
[[nodiscard]] std::vector<PanelOption> navOptions(const DistrictMapState& state) {
    return {
        PanelOption{"ARROWS", "NEXT PLACE", "", kCursorTone, InkRole::Dim, false},
        PanelOption{"TAB", std::string(tabName(state.tab)), "", kCursorTone, InkRole::Dim,
                    false},
        PanelOption{"+ -",
                    "ZOOM " + std::to_string(state.zoom + 1) + "/" +
                        std::to_string(mapZoomSteps()),
                    "", kCursorTone, InkRole::Dim, false},
        PanelOption{"M", "CLOSE", "", kCursorTone, InkRole::Dim, false},
    };
}

[[nodiscard]] OptionListStyle navListStyle() {
    OptionListStyle style;
    style.showKeys = true;
    style.maxColumns = 4;
    style.gutterCells = 2;
    // NO minRows: the BAND decides the height, and navRowsFor decides the band.
    style.minRows = 0;
    return style;
}

/// HOW MANY ROWS THE NAV BAND TAKES -- ONE IF THE FOUR VERBS FIT ON ONE.
///
/// This is a row of the plan's own height and it is worth asking for. A fixed
/// two-row band is what a one-row band costs you at 320x180, where the four
/// verbs cannot make four columns (the widest is seventeen cells and four of
/// those want seventy-six of sixty-two), drawOptionList falls to three columns,
/// the fourth entry goes to a second row, and a one-row band CLIPS IT. The
/// clipped entry was "M - CLOSE": the key that shuts the page, missing from the
/// page, at the one size where a player most needs telling.
///
/// But a fixed TWO-row band costs the plan a whole pixel per tile at 1280x720,
/// where the four verbs do fit on one row and the twenty-one pixels the second
/// one takes are exactly the twenty-one that stood between the district at
/// three pixels per tile and the district at four. So the band asks the list.
/// Still a fixed composition: it responds to the window and to the length of
/// four words, never to a player.
[[nodiscard]] int navRowsFor(const DistrictMapState& state, const PanelRect& interior,
                             const PanelMetric& metric) {
    const PanelRect oneRow{interior.x, interior.y, interior.w, metric.cellH()};
    const OptionListPlan plan =
        planOptionList(navOptions(state), oneRow, metric, navListStyle());
    return plan.columns * plan.rows >= 4 ? 1 : 2;
}

}  // namespace

MapPageLayout mapPageLayout(int frameWidth, int frameHeight, const DistrictMapState& state) {
    MapPageLayout out;
    out.metric = panelMetric(frameHeight);
    const int cells = out.metric.cellsIn(frameWidth);
    const int rows = out.metric.rowsIn(frameHeight);
    if (cells < 12 || rows < 12) {
        return out;
    }
    // Centred in the window, so the pixels that do not divide into whole cells
    // are split between the two margins rather than all landing on one edge.
    out.bounds = PanelRect{(frameWidth - out.metric.widthOf(cells)) / 2,
                           (frameHeight - out.metric.heightOf(rows)) / 2,
                           out.metric.widthOf(cells), out.metric.heightOf(rows)};
    out.interior =
        PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                  out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    // THE COMPOSITION, and there is not one pixel constant in it. The
    // reference's map frame is four stacked panels; this is that stack with the
    // detail pane moved BESIDE the plan rather than under it, because a 16:9
    // frame has width to spare and height it cannot spare -- every row of
    // chrome is a row the plan does not get, and the plan's fit scale is bound
    // by height at every window size this build runs at.
    const int navRows = navRowsFor(state, out.interior, out.metric);
    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),   // the breadcrumb
                                                       spanCells(1),   // tabs + readout
                                                       spanCells(1),   // rule
                                                       spanWeight(1),  // plan | detail
                                                       spanCells(1),   // rule
                                                       spanCells(1),   // the selection, named
                                                       spanCells(1),   // rule
                                                       spanCells(navRows),  // global nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.headerBand = bands[0];
    out.headerRow = rowOf(bands[0]);
    out.tabRow = rowOf(bands[1]);
    out.bodyBand = bands[3];
    out.bodyRow = rowOf(bands[3]);
    out.bodyRows = out.metric.rowsIn(bands[3].h);
    out.selectionBand = bands[5];
    out.selectionRow = rowOf(bands[5]);
    out.navBand = bands[7];
    out.navRow = rowOf(bands[7]);
    out.ruleRows = {rowOf(bands[2]), rowOf(bands[4]), rowOf(bands[6])};

    // THE SHARE THAT BUYS THE PLAN A WHOLE PIXEL PER TILE -- see kMapShares.
    const auto planOf = [&out](const PanelRect& pane) {
        // The plan sits INSIDE the pane with a cell of air, so a footprint
        // block touching the edge does not read as fused to the frame's border.
        return pane.inset(out.metric.cellW() / 2, out.metric.cellH() / 4);
    };
    MasterDetail body =
        splitMasterDetail(out.bodyBand, out.metric, kMapShares[0], kMinMapCells, kMinDetailCells);
    int bestFit = body.split ? mapFitScale(planOf(body.master), state.bounds) : 0;
    for (const int share : kMapShares) {
        const MasterDetail candidate =
            splitMasterDetail(out.bodyBand, out.metric, share, kMinMapCells, kMinDetailCells);
        if (!candidate.split) {
            continue;  // never widen the plan by deleting the detail pane
        }
        const int fit = mapFitScale(planOf(candidate.master), state.bounds);
        if (!body.split || fit > bestFit) {
            body = candidate;
            bestFit = fit;
        }
    }
    out.split = body.split;
    out.mapPane = body.master;
    out.detailPane = body.detail;
    out.dividerCell = body.dividerCell;

    const PanelRect plan = planOf(out.mapPane);
    const int scale = mapZoomScale(state.zoom, plan, state.bounds);
    const std::vector<MapPlace>& places = mapPlaces();
    float cx = static_cast<float>(state.bounds.minX) + static_cast<float>(state.bounds.tilesX()) / 2.0F;
    float cy = static_cast<float>(state.bounds.minY) + static_cast<float>(state.bounds.tilesY()) / 2.0F;
    if (!places.empty()) {
        // THE VIEW FOLLOWS THE CURSOR, and holds no pan of its own. One piece
        // of state fewer, and the selection can never be off screen -- which
        // it could be if the pan and the cursor were allowed to disagree.
        const MapPlace& at =
            places[static_cast<std::size_t>(std::clamp(state.selected, 0,
                                                       static_cast<int>(places.size()) - 1))];
        cx = centreXOf(at);
        cy = centreYOf(at);
    }
    out.viewport = mapViewport(plan, state.bounds, scale, cx, cy);
    out.usable = out.bodyRows >= 3;
    return out;
}

// ---------------------------------------------------------------------------
// the detail pane's rows
// ---------------------------------------------------------------------------

namespace {

/// Rows the detail pane spends on furniture rather than on content: the subject
/// badge, a row of air under it, the page indicator, and the commit verb on the
/// last row. Held whether or not each has something to say, so the verb never
/// moves as the cursor runs through places with different amounts to report.
constexpr int kDetailChromeRows = 4;

[[nodiscard]] int detailListRows(const MapPageLayout& layout) {
    if (!layout.split) {
        return 0;
    }
    return std::max(0, layout.metric.rowsIn(layout.detailPane.h) - kDetailChromeRows);
}

[[nodiscard]] int scrollingCount(const DistrictMapState& state) {
    switch (state.tab) {
        case MapTab::People:
            return static_cast<int>(state.people.size());
        case MapTab::Index:
            return static_cast<int>(mapPlaces().size());
        default:
            return 0;
    }
}

}  // namespace

MapDetailScroll mapDetailScroll(const DistrictMapState& state, int frameWidth, int frameHeight) {
    MapDetailScroll out;
    const MapPageLayout layout = mapPageLayout(frameWidth, frameHeight, state);
    const int rows = detailListRows(layout);
    out.perScreen = std::max(1, rows);
    const int count = scrollingCount(state);
    out.screens = std::max(1, (count + out.perScreen - 1) / out.perScreen);
    // The Index follows the CURSOR (it is the same selection the map shows);
    // People follows its own row, which is what detailFirst carries.
    const int anchor = state.tab == MapTab::Index ? state.selected : state.detailFirst;
    out.screen = std::clamp(std::max(0, anchor) / out.perScreen, 0, out.screens - 1);
    out.firstRow = out.screen * out.perScreen;
    return out;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

namespace {

/// A tiny filled triangle, for the one wedge this page draws. Edge functions
/// over the bounding box; render-side float is legal here.
void fillTriangle(Framebuffer& target, float x0, float y0, float x1, float y1, float x2, float y2,
                  const Rgb& colour, float alpha) {
    const auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    const float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0.0F) {
        return;
    }
    const int minX = static_cast<int>(std::floor(std::min({x0, x1, x2})));
    const int maxX = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
    const int minY = static_cast<int>(std::floor(std::min({y0, y1, y2})));
    const int maxY = static_cast<int>(std::ceil(std::max({y0, y1, y2})));
    const float sign = area > 0.0F ? 1.0F : -1.0F;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!target.contains(x, y)) {
                continue;
            }
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            if (edge(x0, y0, x1, y1, px, py) * sign >= 0.0F &&
                edge(x1, y1, x2, y2, px, py) * sign >= 0.0F &&
                edge(x2, y2, x0, y0, px, py) * sign >= 0.0F) {
                target.blend(x, y, colour, alpha);
            }
        }
    }
}

/// One pixel rectangle outline, clipped by the framebuffer's own blend.
void strokeRect(Framebuffer& target, const PanelRect& rect, const Rgb& colour, float alpha,
                int weight) {
    const int w = std::max(1, weight);
    for (int i = 0; i < w; ++i) {
        target.fillRect(rect.x + i, rect.y + i, std::max(0, rect.w - 2 * i), 1, colour, alpha);
        target.fillRect(rect.x + i, rect.bottom() - 1 - i, std::max(0, rect.w - 2 * i), 1, colour,
                        alpha);
        target.fillRect(rect.x + i, rect.y + i, 1, std::max(0, rect.h - 2 * i), colour, alpha);
        target.fillRect(rect.right() - 1 - i, rect.y + i, 1, std::max(0, rect.h - 2 * i), colour,
                        alpha);
    }
}

struct PxRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    [[nodiscard]] bool overlaps(const PxRect& o) const noexcept {
        return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
    }
};

[[nodiscard]] PanelRect footprintRect(const MapViewport& view, const MapPlace& place) {
    const int x = view.pxOfX(static_cast<float>(place.x0));
    const int y = view.pxOfY(static_cast<float>(place.y0));
    const int x1 = view.pxOfX(static_cast<float>(place.x1) + 1.0F);
    const int y1 = view.pxOfY(static_cast<float>(place.y1) + 1.0F);
    return PanelRect{x, y, x1 - x, y1 - y};
}

[[nodiscard]] PanelRect clipToPane(const PanelRect& rect, const PanelRect& pane) {
    const int x0 = std::max(rect.x, pane.x);
    const int y0 = std::max(rect.y, pane.y);
    const int x1 = std::min(rect.right(), pane.right());
    const int y1 = std::min(rect.bottom(), pane.bottom());
    return PanelRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

/// How many authored posts carry this name. A street signed nine times along
/// its reach is a long street, and the sign table is the only place that knows.
[[nodiscard]] int signPostsNamed(std::string_view name) {
    int n = 0;
    for (std::size_t i = 0; i < sim::docks::kSignCount; ++i) {
        if (std::string_view(sim::docks::kSigns[i].place) == name) {
            ++n;
        }
    }
    return n;
}

}  // namespace

void drawDistrictMap(Framebuffer& target, const DistrictMapState& state) {
    if (state.tiles == nullptr || state.palette == nullptr || state.openAmount <= 0.0F) {
        return;
    }
    const float alpha = std::min(1.0F, state.openAmount);
    const MapPageLayout layout = mapPageLayout(target.width(), target.height(), state);
    if (!layout.usable) {
        return;
    }
    const PanelMetric metric = layout.metric;
    const PanelInk& ink = panelInk();
    const std::vector<MapPlace>& places = mapPlaces();
    const int selected =
        places.empty() ? -1
                       : std::clamp(state.selected, 0, static_cast<int>(places.size()) - 1);

    // --- the frame ---------------------------------------------------------
    FrameStyle style;
    style.junction = Motif::Diamond;
    style.alpha = alpha;
    style.stipple = false;
    // ALL BUT OPAQUE, and denser even than the controls page settled on. This
    // surface is a FULL takeover -- the brief's "the world still renders behind
    // and around these surfaces" applies where a screen is not one, and there
    // is nobody to look at while you read a map. At 0.97 the district's own
    // signage came through the panel: the first 1920x1080 capture of this page
    // has "THE GILDE" legible in the empty left third of the map pane, which is
    // a place name printed on a map where no such place stands.
    style.groundAlpha = kPageGroundAlpha;
    PanelFrame frame(target, layout.bounds, metric, style);
    for (const int r : layout.ruleRows) {
        frame.addRule(r);
    }
    if (layout.split) {
        frame.addDivider(layout.dividerCell, layout.bodyRow, layout.bodyRows);
    }
    frame.draw();

    // --- the breadcrumb ----------------------------------------------------
    // EVERY PANEL CARRIES ONE. The ward, the ground you are standing on, and
    // the place the cursor is pointing at -- so the page says where you are
    // AND what you are looking at without either having to be worked out.
    // A bouncer's warning outranks a map read and takes this row while it
    // lasts, which is the same routing every other page in this build uses.
    if (!state.alert.empty()) {
        drawCellText(target, layout.headerBand, metric, 0, 0,
                     clipToWidth(state.alert, layout.headerBand.w, metric.scale),
                     Rgb{0.95F, 0.62F, 0.35F}, alpha);
    } else {
        std::vector<std::string> crumbs{"THE DOCKS"};
        if (!state.title.empty() && shout(state.title) != "THE DOCKS") {
            crumbs.push_back(shout(state.title));
        }
        if (selected >= 0) {
            crumbs.push_back(shout(places[static_cast<std::size_t>(selected)].name));
        }
        drawBreadcrumb(target, layout.headerBand, metric, crumbs, kCursorTone, alpha);
    }

    // --- the tab row: views over the selection ------------------------------
    // NUMBERED, because they really are direct-select: 1 through 4 switch the
    // view and TAB cycles them. The reference prints `d - Dominions` for
    // exactly this reason -- the key on the tab is a key you can press.
    const std::vector<PanelTab> tabs{
        PanelTab{"1", "OVERVIEW"}, PanelTab{"2", "PEOPLE"},
        PanelTab{"3", "INDEX"},    PanelTab{"4", "LEGEND"},
    };
    drawTabRow(target, frame.band(layout.tabRow, 1), metric, "", tabs,
               static_cast<int>(state.tab), state.readout, kCursorTone, alpha);

    // --- the plan ----------------------------------------------------------
    const MapViewport& view = layout.viewport;
    const PanelRect plan = view.pane;
    const sim::TileQuery& tiles = *state.tiles;
    const MapPalette& palette = *state.palette;
    {
        // Only the columns actually inside the pane -- at the far zoom steps
        // the ward is many times the pane and walking all 24,384 of them would
        // be most of them thrown away.
        const std::int32_t x0 = std::max(state.bounds.minX, view.tileOfPx(plan.x));
        const std::int32_t x1 = std::min(state.bounds.maxX, view.tileOfPx(plan.right() - 1));
        const std::int32_t y0 = std::max(state.bounds.minY, view.tileOfPy(plan.y));
        const std::int32_t y1 = std::min(state.bounds.maxY, view.tileOfPy(plan.bottom() - 1));
        for (std::int32_t y = y0; y <= y1; ++y) {
            for (std::int32_t x = x0; x <= x1; ++x) {
                const MapCell cell = mapCellAt(tiles, x, y, state.band);
                if (cell == MapCell::Void) {
                    continue;  // the panel ground IS the void
                }
                Rgb tone;
                switch (cell) {
                    case MapCell::Wall:
                        tone = palette.wallTone(tiles.material(x, y, state.band));
                        break;
                    case MapCell::Floor:
                        tone = palette.floorTone(tiles.material(x, y, state.band));
                        break;
                    case MapCell::LowerFloor: {
                        std::uint16_t material = 0;
                        for (std::int32_t z = state.band - 1; z >= state.band - kLowerScanBands;
                             --z) {
                            if (tiles.walkable(x, y, z)) {
                                material = tiles.material(x, y, z);
                                break;
                            }
                        }
                        tone = palette.floorTone(material) * 0.42F;
                        break;
                    }
                    case MapCell::Water:
                        tone = palette.water;
                        break;
                    default:
                        continue;
                }
                const PanelRect cellRect =
                    clipToPane(PanelRect{view.pxOfX(static_cast<float>(x)),
                                         view.pxOfY(static_cast<float>(y)), view.scale,
                                         view.scale},
                               plan);
                if (cellRect.empty()) {
                    continue;
                }
                target.fillRect(cellRect.x, cellRect.y, cellRect.w, cellRect.h, tone,
                                0.96F * alpha);
            }
        }
    }

    // --- THE BUILDINGS, AS BLOCKS ------------------------------------------
    // The Daggerfall note, literally: a door place is drawn as its whole
    // authored FOOTPRINT, filled a step off the district's own masonry tone and
    // bordered, so a building is a large legible shape rather than a scatter of
    // wall pixels with a plate hung off it. Streets are NOT filled -- a way is
    // ground, and a glebe strip a hundred and seventy tiles long painted solid
    // would be the whole map.
    //
    // LARGEST FIRST, so a nested footprint (The Netter house sits inside The
    // Netters' Compound) draws over its parent instead of under it.
    std::vector<int> blocks;
    blocks.reserve(places.size());
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (!places[i].way) {
            blocks.push_back(static_cast<int>(i));
        }
    }
    std::sort(blocks.begin(), blocks.end(), [&places](int a, int b) {
        const std::int64_t aa = places[static_cast<std::size_t>(a)].area();
        const std::int64_t bb = places[static_cast<std::size_t>(b)].area();
        if (aa != bb) {
            return aa > bb;
        }
        return places[static_cast<std::size_t>(a)].id < places[static_cast<std::size_t>(b)].id;
    });
    for (const int index : blocks) {
        const MapPlace& place = places[static_cast<std::size_t>(index)];
        const PanelRect rect = clipToPane(footprintRect(view, place), plan);
        if (rect.empty()) {
            continue;
        }
        const std::uint16_t material =
            tiles.material(static_cast<std::int32_t>(place.anchorX),
                           static_cast<std::int32_t>(place.anchorY), place.band);
        const Rgb base = palette.wallTone(material);
        const Rgb fill{std::min(1.0F, base.r * 1.45F + 0.045F),
                       std::min(1.0F, base.g * 1.45F + 0.042F),
                       std::min(1.0F, base.b * 1.45F + 0.050F)};
        target.fillRect(rect.x, rect.y, rect.w, rect.h, fill, 0.88F * alpha);
        strokeRect(target, rect, Rgb{std::min(1.0F, fill.r * 1.7F), std::min(1.0F, fill.g * 1.7F),
                                     std::min(1.0F, fill.b * 1.7F)},
                   0.85F * alpha, 1);
    }

    // --- THE NAMES, INSIDE THE SHAPES --------------------------------------
    // SMALLEST FOOTPRINT FIRST. A small shape has exactly one place its name
    // can go; a large one has room to move. Placing the cramped ones first and
    // letting the roomy ones shift within their OWN footprint is what keeps a
    // nested pair (the Netter house inside the Netters' Compound) from printing
    // one name over the other -- and every candidate position is still inside
    // the shape that owns the name. Nothing floats and nothing is displaced
    // onto a neighbour.
    const int labelScale = mapLabelScale(target.height());
    {
        std::vector<int> order;
        order.reserve(places.size());
        for (std::size_t i = 0; i < places.size(); ++i) {
            order.push_back(static_cast<int>(i));
        }
        std::sort(order.begin(), order.end(), [&places](int a, int b) {
            const std::int64_t aa = places[static_cast<std::size_t>(a)].area();
            const std::int64_t bb = places[static_cast<std::size_t>(b)].area();
            if (aa != bb) {
                return aa < bb;
            }
            return places[static_cast<std::size_t>(a)].id < places[static_cast<std::size_t>(b)].id;
        });
        std::vector<PxRect> taken;
        const int rowPx = 7 * labelScale;
        for (const int index : order) {
            const MapPlace& place = places[static_cast<std::size_t>(index)];
            const PanelRect shape = footprintRect(view, place);
            const std::vector<std::string> lines =
                fitFootprintLabel(place.name, shape.w, shape.h, labelScale);
            if (lines.empty()) {
                continue;  // the fallback: no floating plate, ever
            }
            const int blockH = static_cast<int>(lines.size()) * rowPx;
            int widest = 0;
            for (const std::string& line : lines) {
                widest = std::max(widest, textWidth(line, labelScale));
            }
            const int x = shape.x + (shape.w - widest) / 2;
            // Centred in the shape first, then the top of it, then the bottom.
            // All three are INSIDE the footprint; there is no fourth.
            const int seats[3] = {shape.y + (shape.h - blockH) / 2, shape.y + 1,
                                  shape.bottom() - blockH - 1};
            for (const int y : seats) {
                if (y < shape.y || y + blockH > shape.bottom()) {
                    continue;
                }
                const PxRect box{x - 1, y - 1, x + widest + 1, y + blockH + 1};
                if (box.x0 < plan.x || box.y0 < plan.y || box.x1 > plan.right() ||
                    box.y1 > plan.bottom()) {
                    continue;
                }
                bool clear = true;
                for (const PxRect& held : taken) {
                    if (box.overlaps(held)) {
                        clear = false;
                        break;
                    }
                }
                if (!clear) {
                    continue;
                }
                taken.push_back(box);
                // A thin scrim UNDER the text and inside the shape, so the name
                // reads over the building's own colour without a plate around
                // it. The old page's plates were the clutter; this is the same
                // legibility with none of the footprint.
                target.fillRect(x - 1, y - 1, widest + 2, blockH + 1, Rgb{0.04F, 0.035F, 0.05F},
                                0.55F * alpha);
                for (std::size_t l = 0; l < lines.size(); ++l) {
                    const int lw = textWidth(lines[l], labelScale);
                    drawText(target, x + (widest - lw) / 2, y + static_cast<int>(l) * rowPx,
                             lines[l], place.way ? kWayInk : kDoorInk, alpha, labelScale);
                }
                break;
            }
        }
    }

    // --- door dots ---------------------------------------------------------
    // Every authored door keeps its tick whether or not its shape could hold
    // its name, so an unnamed building is still a marked entrance.
    for (const MapPlace& place : places) {
        if (place.way) {
            continue;
        }
        const int dot = std::max(2, view.scale);
        const PanelRect at = clipToPane(PanelRect{view.pxOfX(place.anchorX) - dot / 2,
                                                  view.pxOfY(place.anchorY) - dot / 2, dot, dot},
                                        plan);
        if (!at.empty()) {
            target.fillRect(at.x, at.y, at.w, at.h, kDoorDot, 0.92F * alpha);
        }
    }

    // --- THE CURSOR --------------------------------------------------------
    // The reference's own idiom: a box outline in the accent around the current
    // selection. Two pixels, and a second darker ring outside it so it reads
    // over a pale quay as well as over the harbour.
    if (selected >= 0) {
        // EVERY SEGMENT OF THE SELECTION, not only the one carrying the name.
        // A door has one; picking a street lights the whole street, round its
        // bends, which is what "the Tarwalk" means and what a box on one block
        // of it does not say. See MapPlace::segments.
        const MapPlace& place = places[static_cast<std::size_t>(selected)];
        for (const MapRect& segment : place.segments) {
            const int x = view.pxOfX(static_cast<float>(segment.x0));
            const int y = view.pxOfY(static_cast<float>(segment.y0));
            const PanelRect shape{x, y, view.pxOfX(static_cast<float>(segment.x1) + 1.0F) - x,
                                  view.pxOfY(static_cast<float>(segment.y1) + 1.0F) - y};
            const PanelRect outer{shape.x - 3, shape.y - 3, shape.w + 6, shape.h + 6};
            const PanelRect box{shape.x - 2, shape.y - 2, shape.w + 4, shape.h + 4};
            strokeRect(target, clipToPane(outer, plan), Rgb{0.06F, 0.05F, 0.04F}, 0.75F * alpha,
                       1);
            strokeRect(target, clipToPane(box, plan), kCursorTone, alpha, 2);
        }

        // THE CURSOR NAMES WHAT IT IS ON, when the footprint could not.
        //
        // THE ONE LABEL RULE is right and it stays: a name is printed inside
        // the shape that owns it or it is not printed at all, because
        // "WEIGHHOU" names nothing and a plate floating on a neighbour names
        // the wrong building. But the Weighhouse is the building this demo's
        // investigation turns on, its shape is too narrow for its name at every
        // default zoom, and what a player saw was a cursor sitting on a blank
        // block with IMPOUND YARD -- a DIFFERENT place -- labelled underneath
        // it. Correct by the rule, and it reads as an oversight.
        //
        // So the tag belongs to the CURSOR, not to the map: one name, for the
        // one shape you are pointing at, gone the moment you point elsewhere.
        // The static map face is untouched, nothing is printed for a place you
        // have not selected, and no unselected footprint gains a plate.
        const PanelRect shape = footprintRect(view, place);
        if (fitFootprintLabel(place.name, shape.w, shape.h, labelScale).empty()) {
            const int rowPx = 7 * labelScale;
            const std::string tag = shout(place.name);
            const int tw = textWidth(tag, labelScale);
            // Centred on the shape, then pushed back inside the plan rather
            // than clipped -- a half-printed name is the thing the rule exists
            // to prevent and that does not stop being true here.
            int tx = shape.x + (shape.w - tw) / 2;
            tx = std::clamp(tx, plan.x + 1, std::max(plan.x + 1, plan.right() - tw - 1));
            // Above the cursor box by preference: the shapes that refuse a
            // label are small, and above keeps the tag off the footprint you
            // are trying to look at. Below when there is no room above.
            int ty = shape.y - 3 - rowPx;
            if (ty < plan.y + 1) {
                ty = shape.bottom() + 3;
            }
            if (ty >= plan.y && ty + rowPx <= plan.bottom() && tw > 0) {
                // The same thin scrim the footprint labels use, so a cursor tag
                // and a map label read as the same kind of mark rather than as
                // two typographies.
                target.fillRect(tx - 1, ty - 1, tw + 2, rowPx, Rgb{0.04F, 0.035F, 0.05F},
                                0.72F * alpha);
                drawText(target, tx, ty, tag, kCursorTone, alpha, labelScale);
            }
        }
    }

    // --- the player, as a facing wedge -------------------------------------
    {
        const float rad = static_cast<float>(state.yawBam) *
                          (6.28318530718F / static_cast<float>(sim::kTurnFull));
        const float fx = std::sin(rad);
        const float fy = -std::cos(rad);
        const float px = static_cast<float>(view.pxOfX(state.playerX));
        const float py = static_cast<float>(view.pxOfY(state.playerY));
        if (px >= static_cast<float>(plan.x) && px < static_cast<float>(plan.right()) &&
            py >= static_cast<float>(plan.y) && py < static_cast<float>(plan.bottom())) {
            const float size = 3.0F + 1.2F * static_cast<float>(view.scale);
            const float tipX = px + fx * size;
            const float tipY = py + fy * size;
            const float backX = px - fx * size * 0.7F;
            const float backY = py - fy * size * 0.7F;
            const float sideX = -fy * size * 0.55F;
            const float sideY = fx * size * 0.55F;
            fillTriangle(target, tipX + fx, tipY + fy, backX + sideX * 1.5F,
                         backY + sideY * 1.5F, backX - sideX * 1.5F, backY - sideY * 1.5F,
                         Rgb{0.05F, 0.04F, 0.05F}, 0.85F * alpha);
            fillTriangle(target, tipX, tipY, backX + sideX, backY + sideY, backX - sideX,
                         backY - sideY, kPlayerTone, alpha);
        }
    }

    // --- the selection, NAMED IN PROSE UNDER THE PLAN ----------------------
    // The reference's `Selected tile: (28,13) The Poisoned Dusts`. You never
    // have to work out what you are pointing at -- which is what makes the
    // sixteen shapes too small to hold a name a deliberate fallback rather than
    // a hole.
    if (selected >= 0) {
        const MapPlace& place = places[static_cast<std::size_t>(selected)];
        const PanelRect row = layout.selectionBand;
        int cell = 0;
        cell += drawCellText(target, row, metric, cell, 0, "SELECTED", ink.dim, alpha) + 1;
        const std::string where = "(" + std::to_string(static_cast<int>(place.anchorX)) + "," +
                                  std::to_string(static_cast<int>(place.anchorY)) + ")";
        cell += drawCellText(target, row, metric, cell, 0, where, ink.number, alpha) + 1;
        cell += drawCellText(target, row, metric, cell, 0, shout(place.name),
                             place.way ? kWayInk : kDoorInk, alpha) + 2;
        drawCellText(target, row, metric, cell, 0, shout(place.what), ink.dim, alpha);
    }

    // --- the detail pane ---------------------------------------------------
    if (layout.split && selected >= 0) {
        const MapPlace& place = places[static_cast<std::size_t>(selected)];
        const PanelRect pane = layout.detailPane;
        const Rgb accent = place.way ? kWayInk : kCursorTone;
        const int paneRows = metric.rowsIn(pane.h);

        // The subject, inverted -- the same badge the controls page and the
        // creation flow wear, so a selected footprint and its detail pane read
        // as one object seen twice.
        const std::string badge = shout(place.name);
        const int badgeCells = std::min(metric.cellsIn(pane.w),
                                        static_cast<int>(badge.size()) + 2);
        drawInvertedFill(target, pane, metric, 0, 0, badgeCells, accent, alpha);
        drawCellTextKnockout(target, pane, metric, 1, 0, badge, ink.knockout, alpha);

        // ONE CELL OF AIR OFF THE RIGHT EDGE. Anything RIGHT-aligned in this
        // pane -- the People view's trade column, the Index's values -- would
        // otherwise end flush against the frame's own `|`/`!` and the eye reads
        // the two together: the first capture of the People view over the
        // Tarwalk printed a hundred and three rows of "WASTREL!" and "SERF!".
        // drawTabRow already learned this for the readout; the edge is
        // furniture and it must not be able to punctuate a word.
        const PanelRect bodyPane{pane.x, pane.y + metric.heightOf(2),
                                 std::max(0, pane.w - metric.cellW()),
                                 metric.heightOf(std::max(0, paneRows - kDetailChromeRows))};
        const MapDetailScroll scroll = mapDetailScroll(state, target.width(), target.height());

        switch (state.tab) {
            case MapTab::Overview: {
                const std::int32_t px = static_cast<std::int32_t>(state.playerX);
                const std::int32_t py = static_cast<std::int32_t>(state.playerY);
                // WHERE YOU WOULD HEAD FOR, not where the sign stands. See
                // mapAimPoint: the Tarwalk is signed at its eastern end and a
                // body standing on it outside the Gull was being told E, 54
                // PACES by a pane whose next line said YOU ARE STANDING IN IT.
                std::int32_t ax = 0;
                std::int32_t ay = 0;
                mapAimPoint(place, px, py, ax, ay);
                const std::int32_t dx = ax - px;
                const std::int32_t dy = ay - py;
                const std::int32_t paces =
                    static_cast<std::int32_t>(std::lround(std::sqrt(
                        static_cast<double>(dx) * dx + static_cast<double>(dy) * dy)));
                const std::string way(sim::compass_point(sim::bearingTo(px, py, ax, ay)));
                // STANDS ON: the street this door fronts, which is the thing a
                // player can actually follow. "Go to the Gilded Gull" means
                // "walk the Tarwalk", and the authored way footprints know it.
                const int onWay = mapWayUnder(ax, ay, 6);
                // PEOPLE: the count, on the Overview, so the People tab
                // advertises what is inside it before you press it, which is
                // the reference's own "v - View traits (Con Artist +1)": a door
                // with a label on it, telling you the payoff before you open
                // it. And an empty room is WORDED rather than left blank, so a
                // reader knows the question was asked.
                const std::string inside =
                    state.people.empty()
                        ? std::string("NOBODY, RIGHT NOW")
                        : std::to_string(state.people.size()) +
                              (state.people.size() == 1 ? " PERSON" : " PEOPLE");
                // SIX FACTS, ALWAYS, WHATEVER IS SELECTED. The second row is
                // the one that changes with the kind of thing, and it changes
                // its LABEL along with its value rather than saying something
                // untrue in a fixed one: a door STANDS ON a street, and a
                // street does not stand on anything -- it is signed along its
                // reach, and how many posts carry its name is the honest
                // measure of how long it is. The row count is fixed so nothing
                // below it moves as the cursor crosses between the two kinds.
                const bool onSelf = onWay >= 0 && onWay == selected;
                const PanelFact second =
                    place.way || onSelf
                        ? PanelFact{"SIGNED", std::to_string(signPostsNamed(place.name)) +
                                                  (signPostsNamed(place.name) == 1 ? " POST"
                                                                                   : " POSTS"),
                                    InkRole::Number}
                        : PanelFact{"STANDS ON",
                                    onWay >= 0
                                        ? shout(places[static_cast<std::size_t>(onWay)].name)
                                        : std::string("NO SIGNED WAY"),
                                    onWay >= 0 ? InkRole::Prose : InkRole::Dim};
                std::vector<PanelFact> facts{
                    PanelFact{"KIND", place.way ? "STREET" : "DOOR", InkRole::Prose},
                    second,
                    PanelFact{"FOOTPRINT",
                              std::to_string(place.tilesX()) + "x" +
                                  std::to_string(place.tilesY()) + " TILES",
                              InkRole::Number},
                    PanelFact{"BAND", std::to_string(place.band), InkRole::Number},
                    PanelFact{"FROM YOU", shout(way) + "  " + std::to_string(paces) + " PACES",
                              InkRole::Number},
                    PanelFact{"INSIDE NOW", inside,
                              state.people.empty() ? InkRole::Dim : InkRole::Number},
                };
                drawFacts(target, bodyPane, metric, facts, -1, alpha);
                const int used = static_cast<int>(facts.size());
                const PanelRect proseRect{bodyPane.x, bodyPane.y + metric.heightOf(used + 1),
                                          bodyPane.w,
                                          std::max(0, bodyPane.h - metric.heightOf(used + 1))};
                const int wrote =
                    drawProse(target, proseRect, metric,
                              {PanelLine{Bullet::None, "", shout(place.what), InkRole::Prose,
                                         accent}},
                              alpha);
                const int spare = metric.rowsIn(proseRect.h) - wrote;
                if (spare >= 3) {
                    const PanelRect rest{proseRect.x, proseRect.y + metric.heightOf(wrote + 1),
                                         proseRect.w, metric.heightOf(spare - 1)};
                    drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
                }
                break;
            }
            case MapTab::People: {
                // THE ANSWER TO "FINDING THE PERSON I WANT AT THAT PLACE".
                if (state.people.empty()) {
                    // EMPTY STATES ARE WORDED, NOT BLANK -- the reference's own
                    // `no trinket`. Absence is stated so the reader knows it was
                    // considered.
                    drawCellText(target, bodyPane, metric, 0, 0, "NOBODY IS IN THERE", ink.dim,
                                 alpha);
                    drawCellText(target, bodyPane, metric, 0, 1, "RIGHT NOW.", ink.dim, alpha);
                    break;
                }
                // AN OPTION LIST, NOT A FACTS BLOCK, and the difference is not
                // cosmetic: a facts block puts the value at a column computed
                // off the LONGEST LABEL and clamped to half the pane, which is
                // right for four labelled facts and wrong for thirty names of
                // unpredictable length. The option list sizes its value column
                // off the content and right-aligns into it, so a name and a
                // trade always have a gutter between them however long either
                // gets.
                //
                // ONE COLUMN, and that is deliberate rather than an oversight:
                // mapDetailScroll counts a screenful in ROWS, so a list allowed
                // to take two columns would show twice what the scroll thinks
                // it showed and the page indicator would lie. A name and a
                // trade want twenty-eight cells and this pane has twenty-three
                // at 1920x1080, so a second column would never be chosen at any
                // size this build runs at -- but a latent disagreement between
                // what is drawn and what is counted is exactly the bug the
                // vocabulary's own optionListAt note warns about.
                std::vector<PanelOption> rows;
                rows.reserve(state.people.size());
                for (const MapPersonRow& person : state.people) {
                    PanelOption option;
                    option.label = shout(person.name);
                    option.value = shout(person.what);
                    option.valueInk = InkRole::Dim;
                    option.selectable = false;
                    rows.push_back(std::move(option));
                }
                OptionListStyle peopleStyle;
                peopleStyle.showKeys = false;
                peopleStyle.maxColumns = 1;
                peopleStyle.gutterCells = 2;
                peopleStyle.minRows = 0;
                peopleStyle.alignValues = true;
                // PLANNED AGAINST THE WHOLE ROLL so the value column is sized
                // for the longest trade that exists and does not shuffle when
                // the page turns.
                const OptionListPlan peoplePlan =
                    planOptionList(rows, bodyPane, metric, peopleStyle);
                const int first = std::clamp(scroll.firstRow, 0, static_cast<int>(rows.size()));
                const int last = std::min(static_cast<int>(rows.size()),
                                          first + scroll.perScreen);
                const std::vector<PanelOption> page(rows.begin() + first, rows.begin() + last);
                drawOptionListPlanned(target, bodyPane, metric, page, -1, peoplePlan, alpha);
                break;
            }
            case MapTab::Index: {
                // EVERY NAMED PLACE IN THE WARD, and the cursor lives in it --
                // so a player told "go to the Weighhouse" reads the list, the
                // plan pans to it, and the shape lights up. This is the half of
                // the answer footprint labels cannot give, because sixteen
                // shapes are too small to hold their own name.
                std::vector<PanelOption> options;
                options.reserve(places.size());
                for (const MapPlace& entry : places) {
                    PanelOption option;
                    option.label = shout(entry.name);
                    option.accent = entry.way ? kWayInk : kCursorTone;
                    option.labelTakesAccent = entry.way;
                    option.valueInk = InkRole::Dim;
                    options.push_back(std::move(option));
                }
                OptionListStyle listStyle;
                listStyle.showKeys = false;
                listStyle.maxColumns = 1;
                listStyle.gutterCells = 2;
                listStyle.minRows = 0;
                listStyle.alignValues = false;
                // PLANNED AGAINST THE WHOLE LIST, never one screenful, so the
                // geometry does not shuffle when the page turns.
                const OptionListPlan plan2 =
                    planOptionList(options, bodyPane, metric, listStyle);
                const int first = std::clamp(scroll.firstRow, 0, static_cast<int>(options.size()));
                const int last = std::min(static_cast<int>(options.size()),
                                          first + scroll.perScreen);
                const std::vector<PanelOption> page(options.begin() + first,
                                                    options.begin() + last);
                drawOptionListPlanned(target, bodyPane, metric, page, selected - first, plan2,
                                      alpha);
                break;
            }
            case MapTab::Legend:
            default: {
                // A LEGEND SHOWS THE COLOUR, IT DOES NOT NAME IT. The first
                // version of this pane was a drawFacts block reading "BLUE
                // WATER" -- in white, because drawFacts colours by ROLE and
                // there is no role for "the colour I am telling you about".
                // Printing the word "blue" in white on a page whose whole job
                // is a colour code is the machine's-name-for-a-thing defect one
                // pane over. So each row carries a real swatch of the real tone
                // the plan is drawn with, taken from the same MapPalette the
                // plan just used, and an art-pack swap recolours the key by the
                // act of swapping.
                const Rgb ground = palette.floorTone(0);
                const Rgb masonry = palette.wallTone(0);
                const Rgb block{std::min(1.0F, masonry.r * 1.45F + 0.045F),
                                std::min(1.0F, masonry.g * 1.45F + 0.042F),
                                std::min(1.0F, masonry.b * 1.45F + 0.050F)};
                struct Key {
                    Rgb tone;
                    const char* what;
                };
                const Key keys[] = {
                    {block, "A NAMED BUILDING"},
                    {ground, "GROUND YOU CAN WALK"},
                    {palette.floorTone(0) * 0.42F, "GROUND A BAND BELOW"},
                    {palette.water, "THE HARBOUR"},
                    {kDoorDot, "A DOOR"},
                    {kCursorTone, "WHAT THE CURSOR HAS"},
                    {kPlayerTone, "YOU, AND YOUR FACING"},
                    {kDoorInk, "A BUILDING'S NAME"},
                    {kWayInk, "A STREET'S NAME"},
                };
                const int swatchCells = 2;
                for (std::size_t row = 0; row < sizeof(keys) / sizeof(keys[0]); ++row) {
                    const int y = bodyPane.y + metric.heightOf(static_cast<int>(row));
                    if (y + metric.cellH() > bodyPane.bottom()) {
                        break;
                    }
                    target.fillRect(bodyPane.x, y + metric.cellH() / 6,
                                    metric.widthOf(swatchCells),
                                    metric.cellH() - metric.cellH() / 3, keys[row].tone, alpha);
                    drawCellText(target, bodyPane, metric, swatchCells + 1,
                                 static_cast<int>(row), keys[row].what, ink.prose, alpha);
                }
                break;
            }
        }

        // Page indicator, on the pane's own second-to-last row so the commit
        // verb below it never moves.
        if (scroll.screens > 1 && paneRows >= 2) {
            drawCellText(target, pane, metric, 0, paneRows - 2,
                         "MORE  " + std::to_string(scroll.screen + 1) + "/" +
                             std::to_string(scroll.screens),
                         ink.dim, alpha);
        }

        // STATE CHANGES THE VERB. Standing inside the thing you have selected,
        // there is nothing to turn toward -- so the row says so, in the same
        // place, rather than offering a key that would do nothing.
        const bool standingIn = place.contains(static_cast<std::int32_t>(state.playerX),
                                               static_cast<std::int32_t>(state.playerY));
        if (standingIn) {
            drawCellText(target, pane, metric, 0, paneRows - 1, "YOU ARE STANDING IN IT",
                         ink.number, alpha);
        } else {
            std::int32_t ax = 0;
            std::int32_t ay = 0;
            mapAimPoint(place, static_cast<std::int32_t>(state.playerX),
                        static_cast<std::int32_t>(state.playerY), ax, ay);
            const std::string way(sim::compass_point(
                sim::bearingTo(static_cast<std::int32_t>(state.playerX),
                               static_cast<std::int32_t>(state.playerY), ax, ay)));
            drawCommitVerb(target, pane, metric, "ENTER - FACE IT", "(" + shout(way) + ")",
                           ink.key, alpha);
        }
    }

    // --- global nav, below its own rule ------------------------------------
    const std::vector<PanelOption> nav = navOptions(state);
    OptionListStyle navStyle = navListStyle();
    drawOptionList(target, layout.navBand, metric, nav, -1, navStyle, alpha);
}

}  // namespace granadad::render
